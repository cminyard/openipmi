/*
 * ipmi_sol.c
 *
 * IPMI Serial-over-LAN Client Code
 *
 * Author: Cyclades Australia Pty. Ltd.
 *         Darius Davis <darius.davis@cyclades.com>
 *
 * Copyright 2005 Cyclades Australia Pty. Ltd.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * TODO:
 *	- We only support UDP port 623 for now.  Add support for other ports.
 *
 * CAVEATS:
 *	- Multiple connections at once: should work, but UNTESTED.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <assert.h>

#include <OpenIPMI/ipmi_conn.h>
#include <OpenIPMI/ipmi_msgbits.h>
#include <OpenIPMI/ipmi_err.h>
#include <OpenIPMI/ipmi_lan.h>
#include <OpenIPMI/internal/ipmi_malloc.h>
#include <OpenIPMI/ipmi_auth.h>
#include <OpenIPMI/internal/locked_list.h>
#include <OpenIPMI/internal/ipmi_int.h>
#include <OpenIPMI/ipmi_sol.h>

/*
 * Bit masks for status conditions sent BMC -> console, see Table 15-2
 * [1], page 208, 3rd column.
 */
#define IPMI_SOL_STATUS_NACK_PACKET 0x40
#define IPMI_SOL_STATUS_CHARACTER_TRANSFER_UNAVAIL 0x20
#define IPMI_SOL_STATUS_DEACTIVATED 0x10
#define IPMI_SOL_STATUS_BMC_TX_OVERRUN 0x08
#define IPMI_SOL_STATUS_BREAK_DETECTED 0x04


/*
 * Bit masks for operations sent console -> BMC, see Table 15-2 [1],
 * page 208, 4th column.
 */
#define IPMI_SOL_OPERATION_NACK_PACKET 0x40
#define IPMI_SOL_OPERATION_RING_REQUEST 0x20
#define IPMI_SOL_OPERATION_GENERATE_BREAK 0x10
#define IPMI_SOL_OPERATION_CTS_PAUSE 0x08
#define IPMI_SOL_OPERATION_DROP_DCD_DSR 0x04
#define IPMI_SOL_OPERATION_FLUSH_CONSOLE_TO_BMC 0x02
#define IPMI_SOL_OPERATION_FLUSH_BMC_TO_CONSOLE 0x01


#define IPMI_SOL_AUX_USE_ENCRYPTION 0x80
#define IPMI_SOL_AUX_USE_AUTHENTICATION 0x40
#define IPMI_SOL_AUX_SHARED_SERIAL_BEHAVIOR_SHIFT 2
#define IPMI_SOL_AUX_SHARED_SERIAL_BEHAVIOR_MASK 0x03
#define IPMI_SOL_AUX_DEASSERT_HANDSHAKE 0x02


#define IPMI_SOL_MAX_DATA_SIZE 103

#if 0
#define IPMI_SOL_DEBUG_TRANSMIT
#define IPMI_SOL_VERBOSE
#define IPMI_SOL_DEBUG_RECEIVE
#endif


/**
 * Offsets of fields within the SoL packet
 */
#define PACKET_SEQNR 0
#define PACKET_ACK_NACK_SEQNR 1
#define PACKET_ACCEPTED_CHARACTER_COUNT 2
#define PACKET_OP 3
#define PACKET_STATUS 3
#define PACKET_DATA 4

/* Reserve 15 for testing. */
#define MAX_SEQ_TO_SEND 14
#define TEST_SEQ 15

struct ipmi_sol_conn_s;

struct sol_callback {
    ipmi_sol_transmit_complete_cb cb;
    ipmi_sol_flush_complete_cb flush_cb;
    void *cb_data;
    unsigned int pos;
    int queue_selectors;
    int inuse;
    void (*free)(struct ipmi_sol_conn_s *sol, struct sol_callback *c);
    struct sol_callback *next;
};

struct sol_callback_list {
    struct sol_callback *head;
    struct sol_callback *tail;
};

#define NR_SOL_XMIT_PENDING 20

static void
sol_callback_add_tail(struct sol_callback_list *list, struct sol_callback *item)
{
    item->next = NULL;
    if (list->tail) {
	list->tail->next = item;
	list->tail = item;
    } else {
	list->head = item;
	list->tail = item;
    }
}

static struct sol_callback *
sol_callback_dequeue_head(struct sol_callback_list *list)
{
    struct sol_callback *item;

    item = list->head;
    if (item) {
	list->head = item->next;
	if (!list->head)
	    list->tail = NULL;
    }

    return item;
}

static void
sol_callback_list_init(struct sol_callback_list *list)
{
    list->head = NULL;
    list->tail = NULL;
}

/* Holds state changes and incoming data that had to be queued. */
struct sol_pending {
    int is_data;
    union {
	struct {
	    int new_state;
	    int error;
	};
	struct {
	    unsigned char pkt[259];
	    unsigned int pkt_len;
	};
    };
    struct sol_pending *next;
};

struct sol_pending_list {
    struct sol_pending *head;
    struct sol_pending *tail;
};

#define NR_SOL_PENDING 20

static void
sol_pending_add_tail(struct sol_pending_list *list, struct sol_pending *item)
{
    item->next = NULL;
    if (list->tail) {
	list->tail->next = item;
	list->tail = item;
    } else {
	list->head = item;
	list->tail = item;
    }
}

static struct sol_pending *
sol_pending_dequeue_head(struct sol_pending_list *list)
{
    struct sol_pending *item;

    item = list->head;
    if (item) {
	list->head = item->next;
	if (!list->head)
	    list->tail = NULL;
    }

    return item;
}

struct ipmi_sol_conn_s {
    /* The IPMI connection for commands. */
    ipmi_con_t *ipmi;

    /* The IPMI connection for SOL data. */
    ipmi_con_t *ipmid;

    /* Used to know how many users are using this right now. */
    unsigned int refcount;

    /* The system interface address is cached here for sending RMCP+
       commands. */
    ipmi_system_interface_addr_t addr;

    /* The RMCP+ destination address is cached here for sending SoL
       packets. */
    ipmi_rmcpp_addr_t sol_payload_addr;

    unsigned char initial_bit_rate;
    unsigned char privilege_level;

    /* Nonzero allows ipmi_sol_open to alter the nonvolatile
       configuration to force SoL to come up if at all possible.  Only
       for debugging, please! */
    int force_connection_configure;

    /* Connects more quickly, but will give a lot less diagnostic info
       if it fails. */
    int try_fast_connect;

    /* The current state of the SoL connection.  Note that state
       changes are protected by the packet lock. */
    ipmi_sol_state state;

    /* Max payload size outbound from here->BMC */
    unsigned int max_outbound_payload_size;

    /* Max payload size inbound from BMC->here */
    unsigned int max_inbound_payload_size;

    unsigned int payload_port_number;

    /* We choose a payload instance number when activating the SoL payload */
    unsigned int payload_instance;

    /* Configuration data used at Payload Activation */
    unsigned char auxiliary_payload_data;
    int ACK_timeout_usec;
    int ACK_retries;

    /* A list of callbacks that are called when data received from the BMC. */
    locked_list_t *data_received_callback_list;

    /* A list of callbacks that are called when a break is reported by
       the BMC. */
    locked_list_t *break_detected_callback_list;

    /* A list of callbacks that are called when a transmit overrun is
       reported by the BMC. */
    locked_list_t *bmc_transmit_overrun_callback_list;

    /* A list of callbacks that are called when the SoL connection
       changes state. */
    locked_list_t *connection_state_callback_list;

    /* Lock for the sol connection. */
    ipmi_lock_t *lock;

    /* Has the payload been activated? */
    int activated;

    /* Pending error on close. */
    int close_err;

    /* The timer to manage retransmits. */
    int timer_running;
    unsigned int retries;
    struct timeval curr_timeout;
    os_hnd_timer_id_t *ack_timer;

    /*
     * Maximum data we can send in each message, either the max or
     * provided by the other end.
     */
    unsigned int max_xmit_data_size;

    /* Last received sequence number.  Set to zero once the ack is sent. */
    unsigned int recv_ack;

    /* Last received sequence number. */
    unsigned int last_recv_seq;

    /* Number of characters we got in the last message. */
    unsigned int acc_char_count;

    /* Used to keep track of transmit sequences. */
    unsigned int curr_xmit_seq;

    /* Last sequence number transmited.  Well be set to zero on response. */
    unsigned int xmit_seq;

    /* Will the remote end send acks for packets with no data? */
    int remote_acks_nodata;

    /* Is any transmit info pending, and if so, the header holding it. */
    int xmit_pending;
    unsigned char xmit_pending_ops;

    /* Packet that is currently outstanding, if xmit_waiting_ack is true. */
    int xmit_waiting_ack;
    unsigned char xmit_pkt[259];
    unsigned int xmit_pkt_data_len;
    ipmi_sol_transmit_complete_cb curr_pkt_cb;
    unsigned int rexmit_count;
    struct sol_callback_list xmit_waiting_cbs;

    struct sol_callback_list pending_xmit_cbs;
    struct sol_callback_list pending_xmit_free;
    struct sol_callback pending_xmit_data[NR_SOL_XMIT_PENDING];

    struct sol_callback_list pending_op_cbs;
    struct sol_callback break_cb;
    struct sol_callback cts_cb;
    struct sol_callback dcd_cb;
    struct sol_callback ri_cb;
    struct sol_callback flush_cb;

    /* Held data to transmit next. */
    unsigned char xmit_buf[1024];
    unsigned int xmit_buf_len;

    /* Nack returns pending that we need release_nack calls for. */
    unsigned int nack_count;

    /*
     * Currently running the receive code.  Hold off any transmits until
     * it's done.
     */
    int in_recv;

    /*
     * Used to keep operations that had to pend.
     */
    struct sol_pending_list pendings;
    struct sol_pending_list free_pendings_pkt;
    struct sol_pending_list free_pendings_conrpt;
    struct sol_pending pending_data[NR_SOL_PENDING];

    /* Remote end has requested a nack. */
    int remote_nack;

    /* Used to make a linked-list of these */
    ipmi_sol_conn_t *next;
};


static void
dump_hex(unsigned char *data, int len)
{
    int i;
    for (i=0; i<len; i++) {
	if ((i != 0) && ((i % 16) == 0))
	    ipmi_log(IPMI_LOG_DEBUG_CONT, "\n  ");
	ipmi_log(IPMI_LOG_DEBUG_CONT, " %2.2x", data[i]);
    }
}

#if 0
static void
print_hex(unsigned char *data, unsigned int len)
{
    unsigned int i, j = 0;

    for (i=0; i<len; i++) {
	if ((i != 0) && ((i % 16) == 0)) {
	    printf(" ");
	    for (; j < i; j++) {
		if (isprint(data[j]))
		    printf("%c", data[j]);
		else
		    printf(".");
	    }
	    printf("\n  ");
	}
	printf(" %2.2x", data[i]);
    }
    while (i % 16) {
	printf("   ");
	i++;
    }
    for (; j < len; j++) {
	if (isprint(data[j]))
		    printf("%c", data[j]);
	else
	    printf(".");
    }
    printf("\n");
}
#endif

/****************************************************************************
 * SoL Connection List
 *
 * Used to match up an incoming packet with the SoL connection that should
 * be interested in that packet.
 */

/* FIXME - a list is ineffecient for large numbers of connections.  It
   probably doesn't matter for now, but a hash table might be a good
   idea in the future. */

static ipmi_sol_conn_t *sol_list = NULL;
static ipmi_lock_t *sol_lock = NULL;

/**
 * Adds the given (ipmi, sol) pairing to the list of connections we're
 * managing.
 */
static int
add_connection(ipmi_sol_conn_t *new_sol)
{
    ipmi_sol_conn_t *sol;

    ipmi_lock(sol_lock);

    /* Make sure the connection doesn't already exist */
    sol = sol_list;
    while (sol) {
	if (sol->ipmi == new_sol->ipmi) {
	    ipmi_unlock(sol_lock);
	    return EAGAIN;
	}
	sol = sol->next;
    }

    new_sol->next = sol_list;
    sol_list = new_sol;
    ipmi_unlock(sol_lock);
    return 0;
}


/**
 * Removes the given connection from the list of connections we're managing.
 */
static void delete_connection(ipmi_sol_conn_t *sol)
{
    ipmi_sol_conn_t *curr;
    ipmi_sol_conn_t *prev = NULL;

    ipmi_lock(sol_lock);
    curr = sol_list;
    while (curr) {
	if (curr == sol) {
	    /*
	     * Delete me!
	     */
	    if (!prev)
		/* Deleting from head of list... */
		sol_list = sol_list->next;
	    else
		/* Deleting from within list */
		prev->next = curr->next;
	    break;
	}

	prev = curr;
	curr = curr->next;
    }
    ipmi_unlock(sol_lock);
}


/**
 * Finds the sol connection for a given ipmi connection.  Returns with
 * a refcount increment and with the sol read lock and lock held.
 */
static ipmi_sol_conn_t *
find_sol_connection_for_ipmi(ipmi_con_t *ipmi)
{
    ipmi_sol_conn_t *sol, *rv = NULL;

    ipmi_lock(sol_lock);
    sol = sol_list;
    while (sol) {
	if (sol->ipmi == ipmi) {
	    ipmi_lock(sol->lock);
	    if (sol->refcount == 0) {
		ipmi_unlock(sol->lock);
		break;
	    }
	    sol->refcount++;
	    rv = sol;
	    break;
	}
	ipmi_unlock(sol->lock);
	sol = sol->next;
    }
    ipmi_unlock(sol_lock);

    return rv;
}

static void
sol_free_connection(ipmi_sol_conn_t *sol)
{
    os_handler_t *os_hnd = sol->ipmi->os_hnd;

    if (sol->lock)
	ipmi_destroy_lock(sol->lock);
    if (sol->ack_timer)
	os_hnd->free_timer(os_hnd, sol->ack_timer);
    if (sol->data_received_callback_list)
	locked_list_destroy(sol->data_received_callback_list);
    if (sol->break_detected_callback_list)
	locked_list_destroy(sol->break_detected_callback_list);
    if (sol->bmc_transmit_overrun_callback_list)
	locked_list_destroy(sol->bmc_transmit_overrun_callback_list);
    if (sol->connection_state_callback_list)
	locked_list_destroy(sol->connection_state_callback_list);
    ipmi_mem_free(sol);
}

/* Must be holding sol->lock to call. */
static void
sol_get_connection(ipmi_sol_conn_t *sol)
{
    assert(sol->refcount > 0);
    sol->refcount++;
}

/* Must be holding sol->lock to call.  Cannot be the final put. */
static void
sol_put_connection(ipmi_sol_conn_t *sol)
{
    assert(sol->refcount > 1);
    sol->refcount--;
}

/* Must be holding sol->lock to call.  Unlocks on return. */
static void
sol_put_connection_unlock(ipmi_sol_conn_t *sol)
{
    assert(sol->refcount > 0);
    sol->refcount--;
    if (sol->refcount > 0) {
	ipmi_unlock(sol->lock);
	return;
    }

    /* No more users, destroy the connection. */
    ipmi_unlock(sol->lock);
    delete_connection(sol);
    sol_free_connection(sol);
}


/****************************************************************************
 ** Async callback handling - list management, registration, deregistration
 **/

typedef struct do_data_received_callback_s
{
    ipmi_sol_conn_t *sol;
    const void      *buf;
    size_t          count;
    int             nack;
} do_data_received_callback_t;

static int
do_data_received_callback(void *cb_data, void *item1, void *item2)
{
    do_data_received_callback_t *info = cb_data;
    ipmi_sol_data_received_cb   cb = item1;
    
    if (cb(info->sol, info->buf, info->count, item2))
	info->nack++;
    return LOCKED_LIST_ITER_CONTINUE;
}

static int
do_data_received_callbacks(ipmi_sol_conn_t *sol,
			   const void      *buf,
			   size_t          count)
{
    do_data_received_callback_t    info;

    info.sol = sol;
    info.buf = buf;
    info.count = count;
    info.nack = 0;
    locked_list_iterate(sol->data_received_callback_list,
			do_data_received_callback,
			&info);

    /* Only called from the packet handling routine, no need for any
       special handling. for waiting */
    return info.nack;
}

static int
do_break_detected_callback(void *cb_data, void *item1, void *item2)
{
    ipmi_sol_conn_t            *sol = cb_data; 
    ipmi_sol_break_detected_cb cb = item1;
    
    cb(sol, item2);
    return LOCKED_LIST_ITER_CONTINUE;
}

static void
do_break_detected_callbacks(ipmi_sol_conn_t *sol)
{
    locked_list_iterate(sol->break_detected_callback_list,
			do_break_detected_callback,
			sol);

    /* Only called from the packet handling routine, no need for any
       special handling. for waiting */
}

static int
do_transmit_overrun_callback(void *cb_data, void *item1, void *item2)
{
    ipmi_sol_conn_t                  *sol = cb_data; 
    ipmi_sol_bmc_transmit_overrun_cb cb = item1;
    
    cb(sol, item2);
    return LOCKED_LIST_ITER_CONTINUE;
}

static void
do_transmit_overrun_callbacks(ipmi_sol_conn_t *sol)
{
    locked_list_iterate(sol->bmc_transmit_overrun_callback_list,
			do_transmit_overrun_callback,
			sol);

    /* Only called from the packet handling routine, no need for any
       special handling. for waiting */
}

typedef struct do_connection_state_callback_s
{
    ipmi_sol_conn_t *sol;
    ipmi_sol_state  state;
    int             error;
} do_connection_state_callback_t;

static int
do_connection_state_callback(void *cb_data, void *item1, void *item2)
{
    do_connection_state_callback_t *info = cb_data;
    ipmi_sol_connection_state_cb   cb = item1;
    
    cb(info->sol, info->state, info->error, item2);
    return LOCKED_LIST_ITER_CONTINUE;
}

void
do_connection_state_callbacks(ipmi_sol_conn_t *sol,
			      ipmi_sol_state  new_state,
			      int             error)
{
    do_connection_state_callback_t info;

    info.sol = sol;
    info.state = new_state;
    info.error = error;
    locked_list_iterate(sol->connection_state_callback_list,
			do_connection_state_callback,
			&info);
}

int
ipmi_sol_register_data_received_callback(ipmi_sol_conn_t           *sol,
					 ipmi_sol_data_received_cb cb,
					 void                      *cb_data)
{
    if (locked_list_add(sol->data_received_callback_list, cb, cb_data))
	return 0;
    else
	return ENOMEM;
}

int
ipmi_sol_deregister_data_received_callback(ipmi_sol_conn_t           *sol,
					   ipmi_sol_data_received_cb cb,
					   void                      *cb_data)
{
    if (locked_list_remove(sol->data_received_callback_list, cb, cb_data))
	return 0;
    else
	return EINVAL;
}


int
ipmi_sol_register_break_detected_callback(ipmi_sol_conn_t            *sol,
					  ipmi_sol_break_detected_cb cb,
					  void                       *cb_data)
{
    if (locked_list_add(sol->break_detected_callback_list, cb, cb_data))
	return 0;
    else
	return ENOMEM;
}

int
ipmi_sol_deregister_break_detected_callback(ipmi_sol_conn_t            *sol,
					    ipmi_sol_break_detected_cb cb,
					    void                      *cb_data)
{
    if (locked_list_remove(sol->break_detected_callback_list, cb, cb_data))
	return 0;
    else
	return EINVAL;
}


int
ipmi_sol_register_bmc_transmit_overrun_callback(ipmi_sol_conn_t *sol,
						ipmi_sol_bmc_transmit_overrun_cb cb,
						void *cb_data)
{
    if (locked_list_add(sol->bmc_transmit_overrun_callback_list, cb, cb_data))
	return 0;
    else
	return ENOMEM;
}

int
ipmi_sol_deregister_bmc_transmit_overrun_callback(ipmi_sol_conn_t *sol,
						  ipmi_sol_bmc_transmit_overrun_cb cb,
						  void *cb_data)
{
    if (locked_list_remove(sol->bmc_transmit_overrun_callback_list, cb,
			   cb_data))
	return 0;
    else
	return EINVAL;
}


int
ipmi_sol_register_connection_state_callback(ipmi_sol_conn_t              *sol,
					    ipmi_sol_connection_state_cb cb,
					    void                       *cb_data)
{
    if (locked_list_add(sol->connection_state_callback_list, cb, cb_data))
	return 0;
    else
	return ENOMEM;
}

int
ipmi_sol_deregister_connection_state_callback(ipmi_sol_conn_t         *sol,
					      ipmi_sol_connection_state_cb cb,
					      void                    *cb_data)
{
    if (locked_list_remove(sol->connection_state_callback_list, cb, cb_data))
	return 0;
    else
	return EINVAL;
}

/***************************************************************************
 ** Various configuration items.
 **/

void
ipmi_sol_set_ACK_timeout(ipmi_sol_conn_t *sol, int timeout_usec)
{
    sol->ACK_timeout_usec = timeout_usec;
}

int
ipmi_sol_get_ACK_timeout(ipmi_sol_conn_t *sol)
{
    return sol->ACK_timeout_usec;
}

void
ipmi_sol_set_ACK_retries(ipmi_sol_conn_t *sol, int retries)
{
    sol->ACK_retries = retries;
}

int
ipmi_sol_get_ACK_retries(ipmi_sol_conn_t *sol)
{
    return sol->ACK_retries;
}

/******************************************************************************
 * SoL auxiliary payload data configuration
 *
 * These parameters will be set when the payload is activated:
 *	- authentication (enabled, disabled)
 *	- encryption (enabled, disabled)
 *	- shared serial alert behaviour (fail, defer, succeed)
 *	- Deassert DSR/DCD/CTS on connect (enabled, disabled)
 */

int
ipmi_sol_set_use_authentication(ipmi_sol_conn_t *sol,
				int             use_authentication)
{
    if (!sol)
	return EINVAL;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_closed) {
	ipmi_unlock(sol->lock);
	return EINVAL;
    }

    if (use_authentication)
	sol->auxiliary_payload_data |= IPMI_SOL_AUX_USE_AUTHENTICATION;
    else
	sol->auxiliary_payload_data &= ~IPMI_SOL_AUX_USE_AUTHENTICATION;
    ipmi_unlock(sol->lock);
    
    return 0;
}


int
ipmi_sol_get_use_authentication(ipmi_sol_conn_t *sol)
{
    return ((sol->auxiliary_payload_data & IPMI_SOL_AUX_USE_AUTHENTICATION)
	    != 0);
}

int
ipmi_sol_set_use_encryption(ipmi_sol_conn_t *sol, int use_encryption)
{
    if (!sol)
	return EINVAL;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_closed) {
	ipmi_unlock(sol->lock);
	return EINVAL;
    }

    if (use_encryption)
	sol->auxiliary_payload_data |= IPMI_SOL_AUX_USE_ENCRYPTION;
    else
	sol->auxiliary_payload_data &= ~IPMI_SOL_AUX_USE_ENCRYPTION;
    ipmi_unlock(sol->lock);

    return 0;
}

int
ipmi_sol_get_use_encryption(ipmi_sol_conn_t *sol)
{
    return ((sol->auxiliary_payload_data & IPMI_SOL_AUX_USE_ENCRYPTION) != 0);
}


int
ipmi_sol_set_shared_serial_alert_behavior(ipmi_sol_conn_t *sol,
	ipmi_sol_serial_alert_behavior behavior)
{
    if (!sol)
	return EINVAL;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_closed) {
	ipmi_unlock(sol->lock);
	return EINVAL;
    }

    sol->auxiliary_payload_data
	&= ~(IPMI_SOL_AUX_SHARED_SERIAL_BEHAVIOR_MASK
	     << IPMI_SOL_AUX_SHARED_SERIAL_BEHAVIOR_SHIFT);
    sol->auxiliary_payload_data
	|= behavior << IPMI_SOL_AUX_SHARED_SERIAL_BEHAVIOR_SHIFT;
    ipmi_unlock(sol->lock);

    return 0;
}


ipmi_sol_serial_alert_behavior
ipmi_sol_get_shared_serial_alert_behavior(ipmi_sol_conn_t *sol)
{
    return (ipmi_sol_serial_alert_behavior)
	((sol->auxiliary_payload_data
	  >> IPMI_SOL_AUX_SHARED_SERIAL_BEHAVIOR_SHIFT)
	 & IPMI_SOL_AUX_SHARED_SERIAL_BEHAVIOR_MASK);
}

int
ipmi_sol_set_deassert_CTS_DCD_DSR_on_connect(ipmi_sol_conn_t *sol,
					     int             deassert)
{
    if (!sol)
	return EINVAL;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_closed) {
	ipmi_unlock(sol->lock);
	return EINVAL;
    }

    if (deassert)
	sol->auxiliary_payload_data |= IPMI_SOL_AUX_DEASSERT_HANDSHAKE;
    else
	sol->auxiliary_payload_data &= ~IPMI_SOL_AUX_DEASSERT_HANDSHAKE;
    ipmi_unlock(sol->lock);

    return 0;
}


int
ipmi_sol_get_deassert_CTS_DCD_DSR_on_connect(ipmi_sol_conn_t *sol)
{
    return ((sol->auxiliary_payload_data & IPMI_SOL_AUX_DEASSERT_HANDSHAKE)
	    != 0);
}


int ipmi_sol_set_bit_rate(ipmi_sol_conn_t *sol, unsigned char rate)
{
    if (!sol)
	return EINVAL;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_closed) {
	ipmi_unlock(sol->lock);
	return EINVAL;
    }

    sol->initial_bit_rate = rate;
    ipmi_unlock(sol->lock);

    return 0;
}

unsigned char
ipmi_sol_get_bit_rate(ipmi_sol_conn_t *sol)
{
    return sol->initial_bit_rate;
}

static void process_pending(ipmi_sol_conn_t *sol);

/**
 * Changes the currently recorded "state" for the SoL connection.
 *
 * Does nothing if the currently recorded state is the same as the new state.
 *
 * @param [in] conn	The SoL connection
 * @param [in] state	The new connection state
 * @param [in] error	The error value to pass to callbacks that are listening
 *			for connection state changes.
 */
static void
ipmi_sol_set_connection_state(ipmi_sol_conn_t *sol,
			      ipmi_sol_state new_state,
			      int error)
{
    if (sol->state == new_state)
	return;

    sol->state = new_state;

    if (sol->in_recv) {
	struct sol_pending *p;

	p = sol_pending_dequeue_head(&sol->free_pendings_conrpt);
	if (!p) {
	    /* Should not be able to happen... */
	    ipmi_log(IPMI_LOG_SEVERE,
		     "ipmi_sol.c(ipmi_sol_set_connection_state): "
		     "Could not allocate state change data.");
	    return;
	}
	p->new_state = new_state;
	p->error = error;
	sol_pending_add_tail(&sol->pendings, p);
    } else {
	sol->in_recv = 1;
	ipmi_unlock(sol->lock);
	do_connection_state_callbacks(sol, new_state, error);
	ipmi_lock(sol->lock);

	process_pending(sol);

	sol->in_recv = 0;

	if (new_state == ipmi_sol_state_closed && sol->timer_running) {
	    os_handler_t *os_hnd = sol->ipmi->os_hnd;
	    int rv = os_hnd->stop_timer(os_hnd, sol->ack_timer);

	    if (!rv) {
		/* We successfully stopped the timer. */
		sol->timer_running = 0;
		sol_put_connection(sol);
	    }
	}
    }
}

static void
ipmi_sol_set_connection_state_norep(ipmi_sol_conn_t *sol,
				    ipmi_sol_state new_state)
{
    sol->state = new_state;
}

/***************************************************************************
 ** Timer handling.
 **/

static void sol_ACK_timer_expired(void *cb_data, os_hnd_timer_id_t *id);

/*
 * Set the next timeout time for the connection.
 */
static void
set_ACK_timeout(ipmi_sol_conn_t *sol, struct timeval *now)
{
    struct timeval timeout, tv2;

    if (!now) {
	os_handler_t *os_hnd = sol->ipmi->os_hnd;

	now = &tv2;
	os_hnd->get_monotonic_time(os_hnd, now);
    }

    timeout.tv_sec = sol->ACK_timeout_usec / 1000000;
    timeout.tv_usec = sol->ACK_timeout_usec % 1000000;

    sol->curr_timeout = *now;
    sol->curr_timeout.tv_sec += timeout.tv_sec;
    sol->curr_timeout.tv_usec += timeout.tv_usec;
    while (sol->curr_timeout.tv_usec >= 1000000) {
	sol->curr_timeout.tv_sec += 1;
	sol->curr_timeout.tv_usec -= 1000000;
    }
}

/* (Re)start the ack timer.  Must be holding the sol lock. */
static int
start_ACK_timer(ipmi_sol_conn_t *sol, struct timeval *now)
{
    os_handler_t *os_hnd = sol->ipmi->os_hnd;
    struct timeval timeout, tv;
    int rv;

    if (!now) {
	os_handler_t *os_hnd = sol->ipmi->os_hnd;

	now = &tv;
	os_hnd->get_monotonic_time(os_hnd, now);
    }

    timeout.tv_sec = sol->curr_timeout.tv_sec - tv.tv_sec;
    if (tv.tv_usec <= sol->curr_timeout.tv_usec) {
	timeout.tv_usec = sol->curr_timeout.tv_usec - tv.tv_usec;
    } else {
	timeout.tv_sec--;
	timeout.tv_usec = sol->curr_timeout.tv_usec + 1000000 - tv.tv_usec;
    }

    if (sol->timer_running) {
	rv = os_hnd->stop_timer(os_hnd, sol->ack_timer);
	if (rv) {
	    /* The timer is in the handler.  It will put the
	       connection and set timer_running to zero.  Go ahead and
	       start the timer, if the handler is just started, it
	       will stop and restart the timer. */
	} else {
	    /* We successfully stopped the timer. */
	    sol->timer_running = 0;
	    sol_put_connection(sol);
	}
    }

    rv = os_hnd->start_timer(os_hnd,
			     sol->ack_timer,
			     &timeout,
			     sol_ACK_timer_expired,
			     sol);
    if (!rv) {
	sol_get_connection(sol); /* For the timer. */
	sol->timer_running = 1;
    }

    return rv;
}

static int
transmit_curr_packet(ipmi_sol_conn_t *sol)
{
    int rv;
    ipmi_msg_t msg;
    ipmi_con_option_t options[3];
    int curr_opt = 0;

    options[curr_opt].option = IPMI_CON_MSG_OPTION_CONF;
    options[curr_opt].ival = ipmi_sol_get_use_encryption(sol);
    curr_opt++;
    options[curr_opt].option = IPMI_CON_MSG_OPTION_AUTH;
    options[curr_opt].ival = ipmi_sol_get_use_authentication(sol);
    curr_opt++;
    options[curr_opt].option = IPMI_CON_OPTION_LIST_END;

    msg.netfn = 1;
    msg.cmd = 0;
    msg.data = sol->xmit_pkt;
    msg.data_len = sol->xmit_pkt_data_len + 4;

    sol->xmit_pending = 0;

    /* Always set the current ack when transmitting. */
    sol->xmit_pkt[PACKET_ACK_NACK_SEQNR] = sol->recv_ack;
    sol->recv_ack = 0;

    rv = sol->ipmid->send_command_option
	(sol->ipmi, (ipmi_addr_t *) &sol->sol_payload_addr,
	 sizeof(sol->sol_payload_addr), &msg, options,
	 NULL, NULL);

    return rv;
}

static int
transmit_next_packet(ipmi_sol_conn_t *sol)
{
    unsigned int data_len;
    struct sol_callback *c;
    int rv = 0;

    if (sol->xmit_waiting_ack || sol->in_recv)
	return 0;

    if (sol->xmit_pkt_data_len) {
	data_len = 0;
    } else {
	data_len = sol->xmit_buf_len;
	if (data_len > sol->max_xmit_data_size)
	    data_len = sol->max_xmit_data_size;
    }

    if (!sol->remote_nack &&
		(data_len > 0 ||
		 (sol->remote_acks_nodata && sol->xmit_pending))) {
	/* There is data to transmit that needs an ack. */

	set_ACK_timeout(sol, NULL);
	rv = start_ACK_timer(sol, NULL);
	if (rv)
	    goto out;

	sol->rexmit_count = sol->ACK_retries;

	/*
	 * Wait until after we start the timer to copy over the data,
	 * after the timer start we can't fail.
	 */
	if (data_len) {
	    memcpy(sol->xmit_pkt + 4, sol->xmit_buf, data_len);

	    /* Remove the packet data from the buffer. */
	    sol->xmit_buf_len -= data_len;
	    memmove(sol->xmit_buf, sol->xmit_buf + data_len, sol->xmit_buf_len);
	    sol->xmit_pkt_data_len = data_len;
	}

	/* Get the next sequence number. */
	sol->curr_xmit_seq++;
	if (sol->curr_xmit_seq > MAX_SEQ_TO_SEND)
	    sol->curr_xmit_seq = 1;
	sol->xmit_seq = sol->curr_xmit_seq;
	sol->xmit_pkt[PACKET_SEQNR] = sol->xmit_seq;

	/* get the op (break, DTS, etc.) callbacks. */
	sol->xmit_waiting_cbs = sol->pending_op_cbs;
	sol_callback_list_init(&sol->pending_op_cbs);

	/* Get the callbacks for the data. */
	c = sol->pending_xmit_cbs.head;
	while (c && c->pos <= sol->xmit_pkt_data_len) {
	    sol_callback_dequeue_head(&sol->pending_xmit_cbs);
	    sol_callback_add_tail(&sol->xmit_waiting_cbs, c);
	    c = sol->pending_xmit_cbs.head;
	}

	/* Update all the remaining positions. */
	c = sol->pending_xmit_cbs.head;
	while (c) {
	    c->pos -= sol->xmit_pkt_data_len;
	    c = c->next;
	}

	sol->xmit_waiting_ack = 1;
    } else if (sol->recv_ack == 0 && !sol->xmit_pending) {
	/* No reason to send, just exit. */
	goto out;
    } else {
	sol->xmit_pkt[PACKET_SEQNR] = 0;
    }
    sol->xmit_pending = 0;

    /* Always set the current ack when transmitting. */
    sol->xmit_pkt[PACKET_ACCEPTED_CHARACTER_COUNT] = sol->acc_char_count;
    sol->xmit_pkt[PACKET_OP] = sol->xmit_pending_ops;

    /* Clear out break and flush, they are one-shot. */
    sol->xmit_pending_ops &= ~(IPMI_SOL_OPERATION_GENERATE_BREAK |
			       IPMI_SOL_OPERATION_FLUSH_CONSOLE_TO_BMC |
			       IPMI_SOL_OPERATION_FLUSH_BMC_TO_CONSOLE);

    rv = transmit_curr_packet(sol);
    if (rv) {
	char buf[50];

	ipmi_log(IPMI_LOG_WARNING,
		 "ipmi_sol.c(transmit_next_packet):"
		 " Could not transmit packet: %s.",
		 ipmi_get_error_string(rv, buf, 50));
	/* Transmit errors are not fatal. */
	rv = 0;
    }

 out:
    return rv;
}

static void
sol_ACK_timer_expired(void *cb_data, os_hnd_timer_id_t *id)
{
    ipmi_sol_conn_t *sol = cb_data;
    os_handler_t *os_hnd = sol->ipmi->os_hnd;
    struct timeval tv;
    int rv;

    ipmi_lock(sol->lock);

    sol->timer_running = 0;

    if (sol->remote_nack || sol->xmit_seq == 0 ||
		(sol->state != ipmi_sol_state_connected &&
		 sol->state != ipmi_sol_state_connected_ctu))
	goto out_unlock;

    os_hnd->get_monotonic_time(os_hnd, &tv);
    if (tv.tv_sec < sol->curr_timeout.tv_sec ||
		(tv.tv_sec == sol->curr_timeout.tv_sec &&
		 tv.tv_usec < sol->curr_timeout.tv_usec)) {
	/* Raced on a timer stop/start, just start the timer again. */
	rv = start_ACK_timer(sol, &tv);
	if (rv)
	    goto timer_fail;
	goto out_unlock;
    }

    sol->rexmit_count--;
    if (sol->rexmit_count == 0) {
	/*
	 * Didn't get a response even after retries... connection is lost.
	 */
	ipmi_sol_set_connection_state(sol,
				      ipmi_sol_state_closed,
				      IPMI_SOL_ERR_VAL(IPMI_SOL_DISCONNECTED));
	goto out_unlock;
    }

    if (sol->xmit_pkt[PACKET_SEQNR] == 0)
	/* Don't have a packet to retransmit. */
	goto out_unlock;

    set_ACK_timeout(sol, &tv);
    rv = start_ACK_timer(sol, &tv);
    if (rv) {
	char buf[50];

    timer_fail:
	ipmi_log(IPMI_LOG_WARNING, "ipmi_sol.c(sol_ACK_timer_expired): "
		 "Unable to setup_ACK_timer: %s",
		 ipmi_get_error_string(rv, buf, 50));

	/* Inability to start timer is fatal. */
	ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed, rv);
	goto out_unlock;
    }

    rv = transmit_curr_packet(sol);
    if (rv) {
	char buf[50];

	ipmi_log(IPMI_LOG_WARNING,
		 "ipmi_sol.c(sol_ACK_timer_expired):"
		 " Could not transmit packet: %s.",
		 ipmi_get_error_string(rv, buf, 50));
	/* Transmit errors are not fatal. */
    }

 out_unlock:
    sol_put_connection_unlock(sol);
}

static void
free_xmit_cb(struct ipmi_sol_conn_s *sol, struct sol_callback *c)
{
    sol_callback_add_tail(&sol->pending_xmit_free, c);
}

/*
 * ipmi_sol_write -
 *
 * Send a sequence of bytes to the remote.
 *	buf - the bytes to send.
 *	count - the number of bytes to send from the buffer.
 * This function (like all the others!) will either return an ERROR
 * and never call the callback, or will return no error and then WILL
 * call the callback, indicating an error later if necessary.  The
 * callback is an indication that the BMC has ACKed *all* of the bytes
 * in this request.  There is no guarantee that the packet will not be
 * fragmented or coalesced in transmission.
 */
int
ipmi_sol_write(ipmi_sol_conn_t               *sol,
	       const void                    *buf,
	       int                           count,
	       ipmi_sol_transmit_complete_cb cb,
	       void                          *cb_data)
{
    int rv = EINVAL;
    struct sol_callback *c = NULL;

    if (count <= 0)
	return EINVAL;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_connected &&
		sol->state != ipmi_sol_state_connected_ctu)
	goto out_unlock;

    if (count > (int) (sizeof(sol->xmit_buf) - sol->xmit_buf_len)) {
	rv = EAGAIN;
	goto out_unlock;
    }

    if (cb) {
	c = sol_callback_dequeue_head(&sol->pending_xmit_free);
	if (!c) {
	    rv = EAGAIN;
	    goto out_unlock;
	}
	c->cb = cb;
	c->cb_data = cb_data;
	c->free = free_xmit_cb;
    }

    memcpy(sol->xmit_buf + sol->xmit_buf_len, buf, count);
    sol->xmit_buf_len += count;

    if (c)
	c->pos = sol->xmit_buf_len;

    sol_callback_add_tail(&sol->pending_xmit_cbs, c);

    rv = transmit_next_packet(sol);

 out_unlock:
    ipmi_unlock(sol->lock);
    return rv;
}


/* 
 * ipmi_sol_release_nack -
 *
 * Remove any pending nacks.
 */
int
ipmi_sol_release_nack(ipmi_sol_conn_t *sol)
{
    int rv = 0;

    ipmi_lock(sol->lock);
    if (sol->nack_count == 0) {
	/* Nothing to NACK. */
	rv = EINVAL;
	goto out;
    }

    sol->nack_count--;
    if (sol->nack_count == 0) {
	/* Time to kick things off again. */
	sol->xmit_pending_ops &= ~IPMI_SOL_OPERATION_NACK_PACKET;
	sol->xmit_pending = 1;
	rv = transmit_next_packet(sol);
    }
 out:
    ipmi_unlock(sol->lock);
    return rv;
}

static void
free_op_cb(struct ipmi_sol_conn_s *sol, struct sol_callback *c)
{
    c->inuse = 0;
}

static int
sol_enqueue_op_cb(ipmi_sol_conn_t *sol, struct sol_callback *c,
		  ipmi_sol_transmit_complete_cb cb, void *cb_data)
{
    if (c->inuse)
	return EAGAIN;
    c->cb = cb;
    c->cb_data = cb_data;
    c->inuse = 1;
    c->free = free_op_cb;
    sol_callback_add_tail(&sol->pending_op_cbs, c);
    return 0;
}

/* 
 * ipmi_sol_send_break -
 *
 * See ipmi_sol_write, except we're not sending any bytes, just a
 * serial "break".  Callback contract is the same as for
 * ipmi_sol_write.
 */
int
ipmi_sol_send_break(ipmi_sol_conn_t *sol,
		    ipmi_sol_transmit_complete_cb cb, void *cb_data)
{
    int rv = EINVAL;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_connected &&
		sol->state != ipmi_sol_state_connected_ctu)
	goto out_unlock;

    rv = sol_enqueue_op_cb(sol, &sol->break_cb, cb, cb_data);
    if (rv)
	goto out_unlock;

    sol->xmit_pending_ops |= IPMI_SOL_OPERATION_GENERATE_BREAK;
    sol->xmit_pending = 1;
    rv = transmit_next_packet(sol);
 out_unlock:
    ipmi_unlock(sol->lock);
    return rv;
}


/* 
 * ipmi_sol_set_CTS_assertable -
 *
 * Asserts CTS at the BMC, to request that the system attached to the
 * BMC ceases transmitting characters.  No guarantee is given that the
 * BMC will honour this request.  Further buffered characters might
 * still be received after CTS is asserted.  See ipmi_sol_write,
 * except we're not sending any bytes, just changing control lines.
 * Callback contract is the same as for ipmi_sol_write.
 */
int
ipmi_sol_set_CTS_assertable(ipmi_sol_conn_t               *sol,
			    int                           assertable,
			    ipmi_sol_transmit_complete_cb cb,
			    void                          *cb_data)
{
    int rv = EINVAL;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_connected &&
		sol->state != ipmi_sol_state_connected_ctu)
	goto out_unlock;

    rv = sol_enqueue_op_cb(sol, &sol->cts_cb, cb, cb_data);
    if (rv)
	goto out_unlock;

    if (assertable)
	sol->xmit_pending_ops &= ~IPMI_SOL_OPERATION_CTS_PAUSE;
    else
	sol->xmit_pending_ops |= IPMI_SOL_OPERATION_CTS_PAUSE;
    sol->xmit_pending = 1;

    rv = transmit_next_packet(sol);
 out_unlock:
    ipmi_unlock(sol->lock);
    return rv;
}


/* 
 * ipmi_sol_set_DCD_DSR_asserted -
 *
 * Asserts DCD and DSR, as if we've answered the phone line.
 * 
 * See ipmi_sol_write, except we're not sending any bytes, just
 * changing control lines.  Callback contract is the same as for
 * ipmi_sol_write.
 */
int
ipmi_sol_set_DCD_DSR_asserted(ipmi_sol_conn_t               *sol,
			      int                           asserted,
			      ipmi_sol_transmit_complete_cb cb,
			      void                          *cb_data)
{
    int rv = EINVAL;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_connected &&
		sol->state != ipmi_sol_state_connected_ctu)
	goto out_unlock;

    rv = sol_enqueue_op_cb(sol, &sol->dcd_cb, cb, cb_data);
    if (rv)
	goto out_unlock;

    if (asserted)
	sol->xmit_pending_ops &= ~IPMI_SOL_OPERATION_DROP_DCD_DSR;
    else
	sol->xmit_pending_ops |= IPMI_SOL_OPERATION_DROP_DCD_DSR;
    sol->xmit_pending = 1;

    rv = transmit_next_packet(sol);
 out_unlock:
    ipmi_unlock(sol->lock);
    return rv;
}

/* 
 * ipmi_sol_set_RI_asserted -
 *
 * Asserts RI, as if the phone line is ringing.
 * 
 * See ipmi_sol_write, except we're not sending any bytes, just
 * changing control lines.  Callback contract is the same as for
 * ipmi_sol_write.
 */
int
ipmi_sol_set_RI_asserted(ipmi_sol_conn_t               *sol,
			 int                           asserted,
			 ipmi_sol_transmit_complete_cb cb,
			 void                          *cb_data)
{
    int rv = EINVAL;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_connected &&
		sol->state != ipmi_sol_state_connected_ctu)
	goto out_unlock;

    rv = sol_enqueue_op_cb(sol, &sol->ri_cb, cb, cb_data);
    if (rv)
	goto out_unlock;

    if (asserted)
	sol->xmit_pending_ops |= IPMI_SOL_OPERATION_RING_REQUEST;
    else
	sol->xmit_pending_ops &= ~IPMI_SOL_OPERATION_RING_REQUEST;
    sol->xmit_pending = 1;

    rv = transmit_next_packet(sol);
 out_unlock:
    ipmi_unlock(sol->lock);
    return rv;
}


/**
 * Requests a flush of the transmit queue(s) identified by
 * queue_selector, which is a bitwise-OR of the following:
 *
 *	IPMI_SOL_BMC_TRANSMIT_QUEUE
 *	IPMI_SOL_BMC_RECEIVE_QUEUE
 *	IPMI_SOL_MANAGEMENT_CONSOLE_TRANSMIT_QUEUE
 *	IPMI_SOL_MANAGEMENT_CONSOLE_RECEIVE_QUEUE
 *
 * This operation will never use the callback if it returns an error.
 * 
 * If no error is returned, the callback will be called in a
 * synchronous fashion if it does not involve the BMC, asynchronous
 * otherwise.
 */
int
ipmi_sol_flush(ipmi_sol_conn_t            *sol,
	       int                        queue_selectors,
	       ipmi_sol_flush_complete_cb cb,
	       void                       *cb_data)
{
    int rv = EINVAL;
    int need_callback;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_connected &&
		sol->state != ipmi_sol_state_connected_ctu)
	goto out_unlock;

    rv = EAGAIN;
    if (sol->flush_cb.pos)
	goto out_unlock;
    sol->flush_cb.flush_cb = cb;
    sol->flush_cb.cb_data = cb_data;
    sol->flush_cb.inuse = 1;
    sol->flush_cb.queue_selectors = queue_selectors;
    sol->flush_cb.free = free_op_cb;

    if (queue_selectors & IPMI_SOL_BMC_TRANSMIT_QUEUE) {
	sol->xmit_pending_ops |= IPMI_SOL_OPERATION_FLUSH_BMC_TO_CONSOLE;
	sol->xmit_pending = 1;
	need_callback = 1;
    }
    if (queue_selectors & IPMI_SOL_BMC_RECEIVE_QUEUE) {
	sol->xmit_pending_ops |= IPMI_SOL_OPERATION_FLUSH_CONSOLE_TO_BMC;
	sol->xmit_pending = 1;
	need_callback = 1;
    }

    if (!need_callback && cb) {
	/* Need to request something to get a callback. */
	sol->flush_cb.pos = 0;
	rv = EINVAL;
	goto out_unlock;
    }

    sol_callback_add_tail(&sol->pending_op_cbs, &sol->flush_cb);
    rv = transmit_next_packet(sol);
    if (rv)
	goto out_unlock;

    if (queue_selectors & IPMI_SOL_MANAGEMENT_CONSOLE_TRANSMIT_QUEUE) {
	/* FIXME - how to handle this?  Especially the callback. */
    }

    /* No receive queue. */

 out_unlock:
    ipmi_unlock(sol->lock);
    return rv;
}

static void
process_next_packet(ipmi_sol_conn_t *sol,
		    unsigned char *packet, unsigned int data_len)
{
    int character_count;
    int do_nack;
    struct sol_callback *to_call = NULL, *to_call_end, *c, *c2;
    int err = 0, new_packet = 0;
    unsigned int count;

    sol->recv_ack = packet[PACKET_SEQNR];

    new_packet = sol->last_recv_seq == packet[PACKET_SEQNR];
    sol->last_recv_seq = packet[PACKET_SEQNR];

    if (data_len > 4) {
	data_len -= 4; /* Skip over header */

	/* FIXME - validate that the sequence numbers are
	   sequentially increasing? */
	if (new_packet) {
	    /* Received the same packet twice, deliver any extra data. */
	    character_count = data_len - sol->acc_char_count;
	} else {
	    /* This whole packet goes to the client(s) */
	    character_count = data_len;
	}

	if (sol->nack_count) {
	    /* The user already sent a NACK, no reason to send any
	       more til they release it. */
	} else if (character_count > 0) {
	    sol->in_recv++;
	    ipmi_unlock(sol->lock);
	    do_nack = do_data_received_callbacks
		(sol, &packet[PACKET_DATA + data_len - character_count],
		 character_count);
	    ipmi_lock(sol->lock);
	    sol->in_recv--;

	    sol->nack_count += do_nack;
	    if (sol->nack_count < 0) {
		ipmi_log(IPMI_LOG_WARNING,
			 "ipmi_sol.c(process_packet): "
			 "Too many NACK releases.");
		sol->nack_count = 0;
	    }

	    if (sol->state == ipmi_sol_state_closed)
		return;
	}

	if (sol->nack_count) {
	    /* FIXME: It is unclear from the spec whether the
	       accepted character count on a NACK should be 0 or
	       the number of bytes not accepted.  Zero seems more
	       reasonable, but neither works with my machine, it
	       just keeps retransmitting then gives up when it
	       gets a NACK. - Corey */
	    sol->acc_char_count = 0;
	    sol->xmit_pending_ops |= IPMI_SOL_OPERATION_NACK_PACKET;
	    sol->xmit_pending = 1;
	} else {
	    sol->acc_char_count = data_len;
	}
    }

    if (packet[PACKET_ACK_NACK_SEQNR] == sol->xmit_seq) {
	sol->xmit_waiting_ack = 0;

	count = packet[PACKET_ACCEPTED_CHARACTER_COUNT];
	if (count > 0) {
	    if (count >= sol->xmit_pkt_data_len) {
		sol->xmit_pkt_data_len = 0;
	    } else {
		/* Set up to retransmit the leftover data. */
		sol->xmit_pkt_data_len -= count;
		memmove(sol->xmit_pkt, sol->xmit_pkt + count,
			sol->xmit_pkt_data_len);
		sol->xmit_pending = 1;
	    }
	} else if (!(packet[PACKET_STATUS] & IPMI_SOL_STATUS_NACK_PACKET)) {
	    /* FIXME: Intel hack */
	    /*
	     * If the packet wasn't NACKed, and the accepted char
	     * count was zero, assume they meant to ACK the whole
	     * packet.
	     */
	    sol->xmit_pkt_data_len = 0;
	}

	/* Pull off all the data items that the remote end has acked. */
	to_call_end = NULL;
	c2 = NULL;
	for (c = sol->xmit_waiting_cbs.head; c; c = c->next) {
	    if (c->pos <= count) {
		if (c2)
		    c2->next = c->next;
		else
		    sol->xmit_waiting_cbs.head = c->next;
		if (to_call_end) {
		    to_call_end->next = c;
		    to_call_end = c;
		} else {
		    to_call_end = c;
		    to_call = c;
		}
	    } else {
		c2 = c;
		c->pos -= count;
	    }
	}
	c = sol->xmit_waiting_cbs.tail = c2;
	sol->xmit_seq = 0;
    } else if (packet[PACKET_ACK_NACK_SEQNR] == TEST_SEQ) {
	/* Got a response to the test packet. */
	sol->remote_acks_nodata = 1;
    }

    if (packet[PACKET_STATUS] & IPMI_SOL_STATUS_NACK_PACKET) {
	/* Check CTU */
	int new_state;

	if (packet[PACKET_STATUS] & IPMI_SOL_STATUS_CHARACTER_TRANSFER_UNAVAIL){
	    new_state = ipmi_sol_state_connected_ctu;
	    /* FIXME - flush outbound data? */
	} else {
	    new_state = ipmi_sol_state_connected;
	}

	ipmi_sol_set_connection_state(sol, new_state, 0);
	sol->remote_nack = 1;
    } else {
	sol->remote_nack = 0;
    }

    if (packet[PACKET_STATUS] & IPMI_SOL_STATUS_DEACTIVATED) {
	ipmi_sol_set_connection_state(sol,
				      ipmi_sol_state_closed,
				      IPMI_SOL_ERR_VAL(IPMI_SOL_DEACTIVATED));
	err = IPMI_SOL_DEACTIVATED;
    }

    ipmi_unlock(sol->lock);

    /* Do all our other callbacks once unlocked. */

    while(to_call) {
	struct sol_callback *n = to_call->next;

	if (to_call->cb)
	    to_call->cb(sol, err, to_call->cb_data);
	else
	    to_call->flush_cb(sol, err, to_call->queue_selectors,
			      to_call->cb_data);
	to_call->free(sol, to_call);
	to_call = n;
    }

    if (new_packet) {
	/* Only do these if it's not a dup receive. */

	if (packet[PACKET_STATUS] & IPMI_SOL_STATUS_BREAK_DETECTED)
	    do_break_detected_callbacks(sol);

	if (packet[PACKET_STATUS] & IPMI_SOL_STATUS_BMC_TX_OVERRUN)
	    do_transmit_overrun_callbacks(sol);
    }

    ipmi_lock(sol->lock);
}

static void
process_packet(ipmi_sol_conn_t *sol,
	       unsigned char *packet, unsigned int data_len)
{
    if (data_len < 4) {
	ipmi_log(IPMI_LOG_WARNING,
		 "ipmi_sol.c(sol_handle_recv_async): "
		 "Dropped incoming SoL packet: Too short, at %d bytes.",
		 data_len);
	return;
    }

    if (data_len > 4 && packet[PACKET_SEQNR] == 0) {
	/* Can't have data in a packet with zero seqnr: error */
	ipmi_log(IPMI_LOG_WARNING,
		 "ipmi_sol.c(sol_handle_recv_async): "
		 "Broken BMC: Received a packet with non-empty data"
		 " and a sequence number of zero.");
	return;
    }

    if (data_len > 259) {
	ipmi_log(IPMI_LOG_WARNING,
		 "ipmi_sol.c(sol_handle_recv_async): "
		 "Broken BMC: Received a packet >259 bytes");
	return;
    }

    if (sol->state != ipmi_sol_state_connected &&
		sol->state != ipmi_sol_state_connected_ctu) {
	ipmi_log(IPMI_LOG_WARNING,
		 "ipmi_sol.c(sol_handle_recv_async): "
		 "Dropped incoming SoL packet: connection closed.");
	return;
    }

    if (sol->in_recv) {
	struct sol_pending *p;

	p = sol_pending_dequeue_head(&sol->free_pendings_conrpt);
	if (!p) {
	    /* Should not be able to happen... */
	    ipmi_log(IPMI_LOG_SEVERE,
		     "ipmi_sol.c(ipmi_sol_set_connection_state): "
		     "Too many pending packets.");
	} else {
	    p->is_data = 1;
	    p->pkt_len = data_len;
	    memcpy(p->pkt, packet, data_len);
	    sol_pending_add_tail(&sol->pendings, p);
	}
	return;
    }

    sol->in_recv = 1;
    process_next_packet(sol, packet, data_len);
    if (sol->state != ipmi_sol_state_closed) {
	process_pending(sol);
	sol->in_recv = 0;
	transmit_next_packet(sol);
    } else {
	sol->in_recv = 0;
    }
}

static void
process_pending(ipmi_sol_conn_t *sol)
{
    struct sol_pending *p;

    for (;;) {
	p = sol_pending_dequeue_head(&sol->pendings);
	if (!p)
	    break;
	if (p->is_data) {
	    process_next_packet(sol, p->pkt, p->pkt_len);
	    sol_pending_add_tail(&sol->free_pendings_pkt, p);
	} else {
	    ipmi_unlock(sol->lock);
	    do_connection_state_callbacks(sol, p->new_state, p->error);
	    ipmi_lock(sol->lock);
	    sol_pending_add_tail(&sol->free_pendings_conrpt, p);
	}
    }
}

/********************************************************
 ** IPMI SoL API
 *******************************************************/

/**
 * Constructs a handle for managing an SoL session.
 *
 * This function does NOT communicate with the BMC or activate the SoL payload.
 *
 * @param [in] ipmi	the existing IPMI over LAN session.
 * @param [out] sol_conn	the address into which to store the allocated
 *				IPMI SoL connection structure.
 * @return	zero on success, or ENOMEM if memory allocation fails.
 */
int
ipmi_sol_create(ipmi_con_t      *ipmi,
		ipmi_sol_conn_t **sol_conn)
{
    ipmi_sol_conn_t *sol;
    os_handler_t *os_hnd = ipmi->os_hnd;
    int rv;
    unsigned int i;

    sol = ipmi_mem_alloc(sizeof(*sol));
    if (!sol)
	return ENOMEM;

    memset(sol, 0, sizeof(*sol));

    sol->refcount = 1;

    /* Enable authentication and encryption by default. */
    sol->auxiliary_payload_data = (IPMI_SOL_AUX_USE_ENCRYPTION
				   | IPMI_SOL_AUX_USE_AUTHENTICATION);

    rv = ipmi_create_lock_os_hnd(os_hnd, &sol->lock);
    if (rv)
	goto out_err;

    rv = os_hnd->alloc_timer(os_hnd, &sol->ack_timer);
    if (rv)
	goto out_err;

    sol->ipmi = ipmi;
    sol->data_received_callback_list = locked_list_alloc(os_hnd);
    if (! sol->data_received_callback_list) {
	rv = ENOMEM;
	goto out_err;
    }
    sol->break_detected_callback_list = locked_list_alloc(os_hnd);
    if (! sol->break_detected_callback_list) {
	rv = ENOMEM;
	goto out_err;
    }
    sol->bmc_transmit_overrun_callback_list = locked_list_alloc(os_hnd);
    if (! sol->bmc_transmit_overrun_callback_list) {
	rv = ENOMEM;
	goto out_err;
    }
    sol->connection_state_callback_list = locked_list_alloc(os_hnd);
    if (! sol->connection_state_callback_list) {
	rv = ENOMEM;
	goto out_err;
    }

    for (i = 0; i < NR_SOL_XMIT_PENDING; i++)
	sol_callback_add_tail(&sol->pending_xmit_free,
			      &sol->pending_xmit_data[i]);
    for (i = 0; i < NR_SOL_PENDING / 2; i++)
	sol_pending_add_tail(&sol->free_pendings_pkt,
			     &sol->pending_data[i]);
    for (; i < NR_SOL_PENDING; i++)
	sol_pending_add_tail(&sol->free_pendings_conrpt,
			     &sol->pending_data[i]);

    sol->state = ipmi_sol_state_closed;
    sol->try_fast_connect = 1;

    sol->ACK_retries = 10;
    sol->ACK_timeout_usec = 1000000;

    rv = add_connection(sol);
    if (rv)
	goto out_err;

    *sol_conn = sol;

    return 0;

 out_err:
    sol_free_connection(sol);
    return rv;
}

/***************************************************************************
 ** Shorthand IPMI messaging; used to set up or close an ipmi_sol_conn_t.
 ** This is NOT used for handling the SoL data... for that, see the payload
 ** functions towards the end of this file.
 **
 ** Note that the packet lock will be held in the callback.
 **/

typedef void (*sol_command_callback)(ipmi_sol_conn_t *sol, ipmi_msg_t *msg);

static int handle_response(ipmi_con_t *ipmi, ipmi_msgi_t *rspi)
{
    ipmi_sol_conn_t *sol = rspi->data1;
    sol_command_callback cb = rspi->data2;

    ipmi_lock(sol->lock);

    /* FIXME - validate sol */

    if (cb)
	cb(sol, &rspi->msg);

    sol_put_connection_unlock(sol);
    ipmi_free_msg_item(rspi);
    return IPMI_MSG_ITEM_USED;
}

static int
send_message(ipmi_sol_conn_t *sol,
	     ipmi_msg_t *msg_out,
	     sol_command_callback cb)
{
    int rv = 0;
    ipmi_msgi_t *rspi = ipmi_alloc_msg_item();

    if (!rspi)
	return ENOMEM;

    rspi->data1 = sol;
    rspi->data2 = cb;
    rspi->data3 = NULL;
    rspi->data4 = NULL;
    rv = sol->ipmi->send_command(sol->ipmi,
				 (ipmi_addr_t *)&sol->addr,
				 sizeof(sol->addr),
				 msg_out,
				 handle_response,
				 rspi);
    if (rv)
	ipmi_free_msg_item(rspi);
    else
	sol_get_connection(sol);

    return rv;
}

static int
sol_send_close(ipmi_sol_conn_t *sol, sol_command_callback cb)
{
    ipmi_msg_t    msg_out;
    unsigned char data[6];

    /*
     * Send a Deactivate Payload
     */
    msg_out.data_len = 6;
    msg_out.data = data;

    msg_out.data[0] = IPMI_RMCPP_PAYLOAD_TYPE_SOL & 0x3f; /* payload type */
    msg_out.data[1] = sol->payload_instance; /* payload instance number */
    msg_out.data[2] = 0x00; /* payload aux data */
    msg_out.data[3] = 0x00;
    msg_out.data[4] = 0x00;
    msg_out.data[5] = 0x00;

    msg_out.netfn = IPMI_APP_NETFN;
    msg_out.cmd = IPMI_DEACTIVATE_PAYLOAD_CMD;

    return send_message(sol, &msg_out, cb);
}

static void
sol_connection_closed(ipmi_con_t *ipmi, void *cb_data)
{
    ipmi_sol_conn_t *sol = cb_data;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_closed)
	ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				      sol->close_err);
    sol_put_connection_unlock(sol);
}

static void
handle_deactivate_payload_response(ipmi_sol_conn_t *sol,
				   ipmi_msg_t *msg_in)
{
    int err;

    /*
     * We assume that conn hasn't gone away already, since we got the message
     * through the connection table.
     */
    if (sol->state == ipmi_sol_state_closed)
	return;

    /*
     * Did it work?  (Do we care?)
     */
    if (msg_in->data_len != 1) {
	sol->close_err = IPMI_SOL_ERR_VAL(IPMI_SOL_DISCONNECTED);
    } else {
	if (msg_in->data[0] != 0x00)
	    sol->close_err = IPMI_SOL_ERR_VAL(msg_in->data[0]);
    }

    if (sol->ipmid != sol->ipmi) {
	sol_get_connection(sol);
	ipmi_unlock(sol->lock);
	err = sol->ipmi->close_connection_done(sol->ipmid,
					       sol_connection_closed,
					       sol);
	ipmi_lock(sol->lock);
	if (err) {
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed, err);
	    sol_put_connection(sol);
	}
    } else {
	ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				      sol->close_err);
    }
}

static int
sol_do_close(ipmi_sol_conn_t *sol, int norep)
{
    int err;

    ipmi_sol_set_connection_state_norep(sol, ipmi_sol_state_closing);

    if (sol->activated) {
	err = sol_send_close(sol, handle_deactivate_payload_response);
	if (!err)
	    return 0;
    }

    if (sol->ipmid != sol->ipmi) {
	ipmi_unlock(sol->lock);
	err = sol->ipmi->close_connection_done(sol->ipmid,
					       sol_connection_closed,
					       sol);
	ipmi_lock(sol->lock);
	if (!err) {
	    sol_get_connection(sol);
	    return 0;
	}
    }

    if (norep)
	ipmi_sol_set_connection_state_norep(sol, ipmi_sol_state_closed);
    else
	ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed, err);

    return err;
}

/**
 * Figure out the "correct" maximum payload size.  This should *never*
 * be larger than 259 (0x0103) due to the constraint of the 8-bit
 * Accepted Character Count field plus the 4-byte payload header.
 * Some manufacturers (who shall remain nameless) have wrong-endianed
 * the maximum payload size fields, so we have to figure out which way
 * around they should be.  b1 and b2 are in the order they are in the
 * packet. They should be little-endian, so we try that first.
 */
static int
get_sane_payload_size(int b1, int b2)
{
    int result = (b2 << 8) + b1;
    if ((result > 0x0103) || (result < 5)) {
	result = (b1 << 8) + b2;
	if ((result > 0x0103) || (result < 5)) {
	    ipmi_log(IPMI_LOG_WARNING, "ipmi_sol.c(get_sane_payload_size): "
		     "BMC did not supply a sensible buffer size"
		     " (0x%02x, 0x%02x). Defaulting to 16.",
		     b1, b2);
	    result = 0x10; /* 16 bytes should be a safe buffer size. */
	} else
	    ipmi_log(IPMI_LOG_INFO, "ipmi_sol.c(get_sane_payload_size): "
		     "BMC sent a byte-swapped buffer size (%d bytes)."
		     " Using %d bytes.", (b2 << 8) + b1, result);
    }
    return result;
}

static void
finish_activate_payload(ipmi_sol_conn_t *sol)
{
    if (sol->max_outbound_payload_size > IPMI_SOL_MAX_DATA_SIZE)
	sol->max_xmit_data_size = IPMI_SOL_MAX_DATA_SIZE;
    else
	sol->max_xmit_data_size = sol->max_outbound_payload_size;

    ipmi_log(IPMI_LOG_INFO,
	     "ipmi_sol.c(handle_active_payload_response): "
	     "Connected to BMC SoL through port %d.",
	     /*		sol->hostname,*/
	     sol->payload_port_number);

#ifdef IPMI_SOL_VERBOSE
    ipmi_log(IPMI_LOG_INFO,
	     "ipmi_sol.c(handle_active_payload_response): "
	     "BMC requested transmit limit %d bytes, receive limit %d bytes.",
	     sol->max_outbound_payload_size,
	     sol->max_inbound_payload_size);

    if (sol->max_outbound_payload_size > sol->transmitter.scratch_area_size)
	ipmi_log(IPMI_LOG_WARNING,
		 "ipmi_sol.c(handle_active_payload_response): "
		 "Limiting transmit to %d bytes.",
		 sol->transmitter.scratch_area_size);
#endif

    /*
     * Set the hardware handshaking bits to match the "holdoff" option...
     */
    if (sol->auxiliary_payload_data & IPMI_SOL_AUX_DEASSERT_HANDSHAKE)
	sol->xmit_pending_ops |= (IPMI_SOL_OPERATION_CTS_PAUSE |
				  IPMI_SOL_OPERATION_DROP_DCD_DSR);
    else
	sol->xmit_pending_ops &= ~(IPMI_SOL_OPERATION_CTS_PAUSE |
				   IPMI_SOL_OPERATION_DROP_DCD_DSR);
    sol->xmit_pending = 1;

    /* See if the other end acks packets with no data. */
    sol->xmit_pkt[PACKET_SEQNR] = TEST_SEQ;
    sol->xmit_pkt[PACKET_ACCEPTED_CHARACTER_COUNT] = 0;
    sol->xmit_pkt[PACKET_OP] = sol->xmit_pending_ops;
    transmit_curr_packet(sol);

    /*
     * And officially bring the connection "up"!
     */
    ipmi_sol_set_connection_state(sol, ipmi_sol_state_connected, 0);
}

static void ipmid_changed(ipmi_con_t   *ipmid,
			  int          err,
			  unsigned int port_num,
			  int          any_port_up,
			  void         *cb_data)
{
    ipmi_sol_conn_t *sol = cb_data;

    ipmi_lock(sol->lock);
    if (err) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_active_payload_response): "
		 "Error setting up new port: %d", err);
	goto out_err;
    }

    finish_activate_payload(sol);
    ipmi_unlock(sol->lock);
    return;

 out_err:
    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed, err);
    ipmi_unlock(sol->lock);
}

/*
 * Create a new IPMI connection to the BMC on the port specified in
 * the payload port number.
 */
static int
setup_new_ipmi(ipmi_sol_conn_t *sol)
{
    ipmi_args_t *args;
    int         rv;
    char        pname[20];

    ipmi_log(IPMI_LOG_INFO,
	     "ipmi_sol.c(setup_new_ipmi): "
	     "Setting up new IPMI connection to port %d.",
	     sol->payload_port_number);

    if (!sol->ipmi->get_startup_args) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_active_payload_response): "
		 "Required a new port, but connection doesn't support "
		 "fetching arguments.");
	return ENOSYS;
    }

    args = sol->ipmi->get_startup_args(sol->ipmi);
    if (!args) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_active_payload_response): "
		 "Unable to get arguments from the IPMI connection.");
	return ENOMEM;
    }

    snprintf(pname, sizeof(pname), "%d", sol->payload_port_number);
    rv = ipmi_args_set_val(args, -1, "Port", pname);
    if (rv) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_active_payload_response): "
		 "Error setting port argument: %d.", rv);
	return rv;
    }

    rv = ipmi_args_setup_con(args, sol->ipmi->os_hnd, NULL, &sol->ipmid);
    if (rv) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_active_payload_response): "
		 "Error setting up new connection: %d.", rv);
	return rv;
    }
    ipmi_free_args(args);

    rv = sol->ipmid->add_con_change_handler(sol->ipmid, ipmid_changed, sol);
    if (rv) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_active_payload_response): "
		 "Error adding connection change handler: %d.", rv);
	return rv;
    }

    rv = sol->ipmid->start_con(sol->ipmid);
    if (rv) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_active_payload_response): "
		 "Error starting secondary connection: %d.", rv);
	return rv;
    }

    return 0;
}

static void
handle_activate_payload_response(ipmi_sol_conn_t *sol,
				 ipmi_msg_t      *msg_in)
{
    /*
     * Did it work?
     */
    if (msg_in->data_len != 13) {
	if (msg_in->data_len != 1) {
	    ipmi_log(IPMI_LOG_WARNING,
		     "ipmi_sol.c(handle_active_payload_response): "
		     "Received %d bytes... was expecting 13 bytes.\n",
		     msg_in->data_len);
	    dump_hex(msg_in->data, msg_in->data_len);
	}

	if (msg_in->data_len > 0)
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
					  IPMI_IPMI_ERR_VAL(msg_in->data[0]));
	else
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				 IPMI_SOL_ERR_VAL(IPMI_SOL_NOT_AVAILABLE));
	return;
    }

    if (msg_in->data[0] != 0x00) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_active_payload_response): "
		 "Activate payload failed.");
	ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				      IPMI_IPMI_ERR_VAL(msg_in->data[0]));
	return;
    }

    sol->activated = 1;

    /* Recover payload sizes that might be wrong-endianed... */

    /* outbound from here->BMC */
    sol->max_outbound_payload_size
	= get_sane_payload_size(msg_in->data[5], msg_in->data[6]);

    /* inbound from BMC->here */
    sol->max_inbound_payload_size
	= get_sane_payload_size(msg_in->data[7], msg_in->data[8]);

    sol->payload_port_number = (msg_in->data[10] << 8) + msg_in->data[9];
    if (sol->payload_port_number == 28418) {
	/* Bad byte-swapping */
	ipmi_log(IPMI_LOG_WARNING,
		 "ipmi_sol.c(handle_active_payload_response): "
		 "Got a badly byte-swapped UDP port, most likely.  Setting"
		 " it to the proper value.");
	sol->payload_port_number = IPMI_LAN_STD_PORT;
    }

    /* FIXME - get the port from the ipmi, don't use the standard port. */
    if (sol->payload_port_number != IPMI_LAN_STD_PORT) {
	int rv = setup_new_ipmi(sol);
	if (rv) {
	    sol_do_close(sol, 0);
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed, rv);
	}
    } else {
	sol->ipmid = sol->ipmi;
	finish_activate_payload(sol);
    }
}

static int
send_activate_payload(ipmi_sol_conn_t *sol)
{
    ipmi_msg_t    msg_out;
    unsigned char data[6];
	
    /*
     * Send an Activate Payload command
     */
    msg_out.data_len = 6;
    msg_out.data = data;

    msg_out.data[0] = IPMI_RMCPP_PAYLOAD_TYPE_SOL & 0x3f; /* payload type */
    msg_out.data[1] = sol->payload_instance; /* payload instance number */
    /* NOTE: Can't connect to an Intel AXXIMMADV with the
       "Serial/Modem alerts fail" option, it seems. */

    /* enc, auth, Serial alerts behavior, deassert CTS and DCD/DSR */
    msg_out.data[2] = sol->auxiliary_payload_data;
    msg_out.data[3] = 0x00;
    msg_out.data[4] = 0x00;
    msg_out.data[5] = 0x00;

    msg_out.netfn = IPMI_APP_NETFN;
    msg_out.cmd = IPMI_ACTIVATE_PAYLOAD_CMD;
    return send_message(sol, &msg_out,
			handle_activate_payload_response);
}


static void
handle_set_volatile_bitrate_response(ipmi_sol_conn_t *sol,
				     ipmi_msg_t *msg_in)
{
    int err;

    if (msg_in->data_len != 1) {
	ipmi_log(IPMI_LOG_WARNING,
		 "ipmi_sol.c(handle_set_volatile_bitrate_response): "
		 "Received %d bytes... was expecting 1 byte.\n",
		 msg_in->data_len);
	dump_hex(msg_in->data, msg_in->data_len);

	if (msg_in->data_len > 0)
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
					  IPMI_IPMI_ERR_VAL(msg_in->data[0]));
	else
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				IPMI_SOL_ERR_VAL(IPMI_SOL_NOT_AVAILABLE));
	return;
    }

    if (msg_in->data[0] != 0x00) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_set_volatile_bitrate_response): "
		 "Set SoL configuration[Volatile bit rate] failed.");
	ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				      IPMI_IPMI_ERR_VAL(msg_in->data[0]));
	return;
    }

#ifdef IPMI_SOL_VERBOSE
    ipmi_log(IPMI_LOG_INFO,
	     "ipmi_sol.c(handle_set_volatile_bitrate_response): "
	     "Volatile bit rate set.");
#endif
    err = send_activate_payload(sol);
    if (err)
	ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed, err);
}

static int
send_set_volatile_bitrate(ipmi_sol_conn_t *sol)
{
    ipmi_msg_t    msg_out;
    unsigned char data[3];
    /*
     * Send a Set SoL Configuration command
     */
    msg_out.data_len = 3;
    msg_out.data = data;
    msg_out.data[0] = IPMI_SELF_CHANNEL; /* own channel, set param */
    msg_out.data[1] = 6; /* parameter selector = SOL volatile bit rate */
    msg_out.data[2] = sol->initial_bit_rate;
	
    msg_out.netfn = IPMI_TRANSPORT_NETFN;
    msg_out.cmd = IPMI_SET_SOL_CONFIGURATION_PARAMETERS;

    return send_message(sol, &msg_out,
			handle_set_volatile_bitrate_response);
}

static void
handle_get_payload_activation_status_response(ipmi_sol_conn_t *sol,
					      ipmi_msg_t *msg_in)
{
    int count = 0, found, max, byte, index, err;

    if (msg_in->data_len != 4) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_get_payload_activation_status_response): "
		 "Get Payload Activation Status command failed.");
	if (msg_in->data_len > 0)
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
					  IPMI_IPMI_ERR_VAL(msg_in->data[0]));
	else
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				IPMI_SOL_ERR_VAL(IPMI_SOL_NOT_AVAILABLE));
	return;
    }

    found = 0;
    for (byte = 0; byte <= 1; byte++) {
	for (index = 0; index < 7; index++) {
	    if (msg_in->data[2 + byte] & (1 << index)) {
		/* This payload instance slot is in use */
		count++;
	    } else if (!found) {
		found = 1;
		sol->payload_instance = 8 * byte + index + 1;
	    }
	}
    }

    max = msg_in->data[1] & 0x0f;

#ifdef IPMI_SOL_VERBOSE
    ipmi_log(IPMI_LOG_INFO,
	     "ipmi_sol.c(handle_get_payload_activation_status_response): "
	     "BMC currently using %d SoL payload instances; limit is %d.",
	     count, max);
#endif

    if (!found || (count >= max)) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_get_payload_activation_status_response): "
		 "BMC can't accept any more SoL sessions.");
	ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
			IPMI_RMCPP_ERR_VAL(IPMI_RMCPP_INVALID_PAYLOAD_TYPE));
	return;
    }
#ifdef IPMI_SOL_VERBOSE
    ipmi_log(IPMI_LOG_INFO,
	     "ipmi_sol.c(handle_get_payload_activation_status_response): "
	     "SoL sessions are available; Using instance slot %d.",
	     sol->payload_instance);
#endif

    if (sol->initial_bit_rate)
	err = send_set_volatile_bitrate(sol);
    else
	err = send_activate_payload(sol);
    if (err)
	ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed, err);
}

static int
send_get_payload_activation_status_command(ipmi_sol_conn_t *sol)
{
    ipmi_msg_t    msg_out;
    unsigned char data[1];

    /*
     * Send a Get Payload Activation Status command
     */
    msg_out.data_len = 1;
    msg_out.data = data;

    msg_out.data[0] = IPMI_RMCPP_PAYLOAD_TYPE_SOL; /* Payload type */

    msg_out.netfn = IPMI_APP_NETFN;
    msg_out.cmd = IPMI_GET_PAYLOAD_ACTIVATION_STATUS_CMD;

    return send_message(sol, &msg_out,
			handle_get_payload_activation_status_response);
}


static void
handle_session_info_response(ipmi_sol_conn_t *sol,
			     ipmi_msg_t *msg_in)
{
#ifdef IPMI_SOL_VERBOSE
    char *privilege_level[16] = {
	"Unknown", "Callback", "User", "Operator", "Administrator",
	"OEM Proprietary", "Unknown", "Unknown", "Unknown", "Unknown",
	"Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown"};
#endif

    if (msg_in->data_len < 7) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_session_info_response): "
		 "Get Session Info command failed.");
	if (msg_in->data_len > 0)
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
					  IPMI_IPMI_ERR_VAL(msg_in->data[0]));
	else
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				IPMI_SOL_ERR_VAL(IPMI_SOL_NOT_AVAILABLE));

	return;
    }

#ifdef IPMI_SOL_VERBOSE
    ipmi_log(IPMI_LOG_INFO,
	     "ipmi_sol.c(handle_session_info_response): "
	     "This session handle: 0x%02x"
	     "  BMC currently using %d of %d sessions",
	     msg_in->data[1], msg_in->data[3], msg_in->data[2]);
    ipmi_log(IPMI_LOG_INFO,
	     "ipmi_sol.c(handle_session_info_response): "
	     "Current UserID: 0x%02x (%s)  Channel number: 0x%02x",
	     msg_in->data[4] & 0x3f, privilege_level[msg_in->data[5] & 0x0f],
	     msg_in->data[6] & 0x0f);
#endif
    send_get_payload_activation_status_command(sol);
}

static int
send_get_session_info(ipmi_sol_conn_t *sol)
{
    /*
     * Send a Get Session Info command (gives us our User ID, among
     * other things)
     */
    ipmi_msg_t    msg_out;
    unsigned char data[1];

    msg_out.data_len = 1;
    msg_out.data = data;

    msg_out.data[0] = 0x00; /* current session */

    msg_out.netfn = IPMI_APP_NETFN;
    msg_out.cmd = IPMI_GET_SESSION_INFO_CMD;

    return send_message(sol, &msg_out, handle_session_info_response);
}

static void
handle_commit_write_response(ipmi_sol_conn_t *sol,
			     ipmi_msg_t *msg_in)
{
    send_get_session_info(sol);
}

static int
send_commit_write(ipmi_sol_conn_t *sol)
{
    ipmi_msg_t    msg_out;
    unsigned char data[3];

    msg_out.data_len = 3;
    msg_out.data = data;

    /* own channel, get param (not just version) */
    msg_out.data[0] = IPMI_SELF_CHANNEL;
    msg_out.data[1] = 0; /* parameter selector = Set In Progress */
    msg_out.data[2] = 0; /* Commit write */
	
    msg_out.netfn = IPMI_TRANSPORT_NETFN;
    msg_out.cmd = IPMI_SET_SOL_CONFIGURATION_PARAMETERS;

    return send_message(sol, &msg_out, handle_commit_write_response);
}

static void
handle_set_sol_enabled_response(ipmi_sol_conn_t *sol,
				ipmi_msg_t *msg_in)
{
#if 0 /* FIXME - why is this here? */
    if ((msg_in->data_len != 1) || (msg_in->data[0])) {
	if (msg_in->data_len > 0)
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
					  IPMI_IPMI_ERR_VAL(msg_in->data[0]));
	else
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				IPMI_SOL_ERR_VAL(IPMI_SOL_NOT_AVAILABLE));

	return 0;
    }
#endif

    send_commit_write(sol);
}

static int
send_enable_sol_command(ipmi_sol_conn_t *sol)
{
    ipmi_msg_t msg_out;
    unsigned char data[3];

    /*
     * Send a Set SoL Configuration command
     */
    ipmi_log(IPMI_LOG_INFO,
	     "ipmi_sol.c(send_enable_sol_command): "
	     "Attempting to enable SoL on BMC.");

    msg_out.data_len = 3;
    msg_out.data = data;

    /* own channel, get param (not just version) */
    msg_out.data[0] = IPMI_SELF_CHANNEL;
    msg_out.data[1] = 2; /* parameter selector = SOL Auth */
    msg_out.data[2] = 0x02; /* Enable SoL! */
	
    msg_out.netfn = IPMI_TRANSPORT_NETFN;
    msg_out.cmd = IPMI_SET_SOL_CONFIGURATION_PARAMETERS;

    return send_message(sol, &msg_out,
			handle_set_sol_enabled_response);
}

static void
handle_get_sol_enabled_response(ipmi_sol_conn_t *sol,
				ipmi_msg_t *msg_in)
{
    if (msg_in->data_len != 3) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_get_sol_enabled_response): "
		 "Get SoL Configuration[SoL Enabled] failed.");
	if (msg_in->data_len > 0)
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
					  IPMI_IPMI_ERR_VAL(msg_in->data[0]));
	else
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				IPMI_SOL_ERR_VAL(IPMI_SOL_NOT_AVAILABLE));

	return;
    }

    if ((msg_in->data[2] && 1)) {
#ifdef IPMI_SOL_VERBOSE
	ipmi_log(IPMI_LOG_INFO,
		 "ipmi_sol.c(handle_get_sol_enabled_response): "
		 "BMC says SoL is enabled.");
#endif
	send_get_session_info(sol);
	return;
    }
    ipmi_log(IPMI_LOG_SEVERE,
	     "ipmi_sol.c(handle_get_sol_enabled_response): "
	     "BMC says SoL is disabled.");
	
    if (sol->force_connection_configure)
	send_enable_sol_command(sol);
    else
	ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				      IPMI_SOL_ERR_VAL(IPMI_SOL_NOT_AVAILABLE));
}

static void
send_get_sol_configuration_command(ipmi_sol_conn_t *sol)
{
    ipmi_msg_t    msg_out;
    unsigned char data[4];

    /*
     * Send a Get SoL Configuration command
     */
    msg_out.data_len = 4;
    msg_out.data = data;

    /* own channel, get param (not just version) */
    msg_out.data[0] = IPMI_SELF_CHANNEL;
    msg_out.data[1] = 1; /* parameter selector, 1 = SOL Enabled */
    msg_out.data[2] = 0; /* set selector */
    msg_out.data[3] = 0; /* block selector */

    msg_out.netfn = IPMI_TRANSPORT_NETFN;
    msg_out.cmd = IPMI_GET_SOL_CONFIGURATION_PARAMETERS;

    send_message(sol, &msg_out, handle_get_sol_enabled_response);
}


static void
handle_get_channel_payload_support_response(ipmi_sol_conn_t *sol,
					    ipmi_msg_t *msg_in)
{
    if (msg_in->data_len != 9) {
	ipmi_log(IPMI_LOG_SEVERE,
		 "ipmi_sol.c(handle_get_channel_payload_support_response): "
		 "Get Channel Payload Support command failed.");
	if (msg_in->data_len > 0)
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
					  IPMI_IPMI_ERR_VAL(msg_in->data[0]));
	else
	    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				IPMI_SOL_ERR_VAL(IPMI_SOL_NOT_AVAILABLE));

	return;
    }

    if (!(msg_in->data[1] & (1 << IPMI_RMCPP_PAYLOAD_TYPE_SOL))) {
	/* SoL is not supported! */
	ipmi_log(IPMI_LOG_ERR_INFO,
		 "ipmi_sol.c(handle_get_channel_payload_support_response): "
		 "BMC says SoL is not supported.");
	ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
			IPMI_RMCPP_ERR_VAL(IPMI_RMCPP_INVALID_PAYLOAD_TYPE));
	return;
    }
#ifdef IPMI_SOL_VERBOSE
    ipmi_log(IPMI_LOG_INFO,
	     "ipmi_sol.c(handle_get_channel_payload_support_response): "
	     "BMC says SoL is supported.");
#endif
    send_get_sol_configuration_command(sol);
}

static int
send_get_channel_payload_support_command(ipmi_sol_conn_t *sol)
{
    ipmi_msg_t    msg_out;
    unsigned char data[1];

    /*
     * Send a Get Payload Support command
     */
    msg_out.data_len = 1;
    msg_out.data = data;

    msg_out.data[0] = IPMI_SELF_CHANNEL; /* current channel */

    msg_out.netfn = IPMI_APP_NETFN;
    msg_out.cmd = IPMI_GET_CHANNEL_PAYLOAD_SUPPORT_CMD;

    return send_message(sol, &msg_out,
			handle_get_channel_payload_support_response);
}


int
ipmi_sol_open(ipmi_sol_conn_t *sol)
{
    int rv;

    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_closed) {
	/* It's an error to try to connect when not in closed state. */
	ipmi_unlock(sol->lock);
	ipmi_log(IPMI_LOG_ERR_INFO,
		 "ipmi_sol.c(ipmi_sol_open): "
		 "An attempt was made to open an SoL connection"
		 " that's already open.");
	return EINVAL;
    }

    sol->addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
    sol->addr.channel = IPMI_BMC_CHANNEL;
    sol->addr.lun = 0;
    
    /*
     * Note: For SoL over IPMI 1.5, the ipmi_lan code will translate this
     * RMCP+ address into the right packet format over RMCP (instead of
     * RMCP+).
     */
    sol->sol_payload_addr.addr_type = IPMI_RMCPP_ADDR_SOL;

    if (sol->try_fast_connect)
	rv = send_get_payload_activation_status_command(sol);
    else
	rv = send_get_channel_payload_support_command(sol);

    if (!rv)
	ipmi_sol_set_connection_state_norep(sol, ipmi_sol_state_connecting);

    sol->activated = 0;
    sol->close_err = 0;
    sol->nack_count = 0;
    sol->recv_ack = 0;
    sol->last_recv_seq = 0;
    sol->curr_xmit_seq = 0;
    sol->acc_char_count = 0;
    sol->remote_acks_nodata = 0;
    sol->xmit_pending = 0;
    sol->xmit_pending_ops = 0;
    sol->xmit_waiting_ack = 0;
    sol->break_cb.inuse = 0;
    sol->cts_cb.inuse = 0;
    sol->dcd_cb.inuse = 0;
    sol->ri_cb.inuse = 0;
    sol->flush_cb.inuse = 0;
    sol->xmit_buf_len = 0;
    sol->nack_count = 0;
    sol->in_recv = 0;
    sol->remote_nack = 0;

    ipmi_unlock(sol->lock);
    return rv;
}

int
ipmi_sol_close(ipmi_sol_conn_t *sol)
{
    int err;

    ipmi_lock(sol->lock);
    if (sol->state == ipmi_sol_state_closing ||
		sol->state == ipmi_sol_state_closed) {
	err = EINVAL;
    } else {
	err = sol_do_close(sol, 1);
    }
    ipmi_unlock(sol->lock);

    return err;
}


int
ipmi_sol_force_close_wsend(ipmi_sol_conn_t *sol, int rem_close)
{
    int err = 0;

    ipmi_lock(sol->lock);
    if (sol->state == ipmi_sol_state_closed) {
	err = EINVAL;
	goto out_unlock;
    }

    if (rem_close && sol->state != ipmi_sol_state_closing)
	sol_do_close(sol, 0);

    ipmi_sol_set_connection_state(sol, ipmi_sol_state_closed,
				  IPMI_SOL_ERR_VAL(IPMI_SOL_DISCONNECTED));
 out_unlock:
    ipmi_unlock(sol->lock);

    return err;
}

int
ipmi_sol_force_close(ipmi_sol_conn_t *sol)
{
    return ipmi_sol_force_close_wsend(sol, 1);
}

int
ipmi_sol_free(ipmi_sol_conn_t *sol)
{
    ipmi_lock(sol->lock);
    if (sol->state != ipmi_sol_state_closing &&
		sol->state != ipmi_sol_state_closed)
	ipmi_sol_force_close_wsend(sol, 1);

    sol_put_connection_unlock(sol);
    return 0;
}


/********************************************************************
 ** IPMI SoL Payload handling           *****************************
 ********************************************************************/

/* Format a message for transmit on this payload.  The address and
   message is the one specified by the user.  The out_data is a
   pointer to where to store the output, out_data_len will point
   to the length of the buffer to store the output and should be
   updatated to be the actual length.  The seq is a 6-bit value
   that should be store somewhere so the that response to this
   message can be identified.  If the netfn is odd, the sequence
   number is not used.  The out_of_session variable is set to zero
   by default; if the message is meant to be sent out of session,
   then the formatter should set this value to 1. */

static int
sol_format_msg(ipmi_con_t        *conn,
	       const ipmi_addr_t *addr,
	       unsigned int      addr_len,
	       const ipmi_msg_t  *msg,
	       unsigned char     *out_data,
	       unsigned int      *out_data_len,
	       int               *out_of_session,
	       unsigned char     seq)
{
    if (*out_data_len < msg->data_len)
	return E2BIG;

    memcpy(out_data, msg->data, msg->data_len);
    *out_data_len = msg->data_len;

    *out_of_session = 0;

    return 0;
}


/* Get the recv sequence number from the message.  Return ENOSYS
   if the sequence number is not valid for the message (it is
   asynchronous), zero otherwise */
static int sol_get_recv_seq(ipmi_con_t    *conn,
			    unsigned char *data,
			    unsigned int  data_len,
			    unsigned char *seq)
{
    /*
     * We force the packets to go through to sol_handle_recv_async for
     * our processing.  This is because we can't use the OpenIPMI payload
     * sequence number interface.
     */
    return ENOSYS;
}


/* Fill in the rspi data structure from the given data, responses
   only.  This does *not* deliver the message, that is done by the
   LAN code. */
static int
sol_handle_recv(ipmi_con_t    *conn,
		ipmi_msgi_t   *rspi,
		ipmi_addr_t   *orig_addr,
		unsigned int  orig_addr_len,
		ipmi_msg_t    *orig_msg,
		unsigned char *data,
		unsigned int  data_len)
{
    /*
     * This should NEVER be called.
     */
    return ENOSYS;
}

/* Handle an asynchronous message.  This *should* deliver the
   message, if possible. */
static void
sol_handle_recv_async(ipmi_con_t    *ipmi_conn,
		      unsigned char *packet,
		      unsigned int  data_len)
{
    ipmi_sol_conn_t *sol;

#ifdef IPMI_SOL_DEBUG_RECEIVE
    ipmi_log(IPMI_LOG_INFO,
	     "ipmi_sol.c(sol_handle_recv_async): "
	     "Received SoL packet, %d bytes", data_len);
    dump_hex(packet, data_len);
#endif

    sol = find_sol_connection_for_ipmi(ipmi_conn);
    if (!sol) {
	ipmi_log(IPMI_LOG_WARNING,
		 "ipmi_sol.c(sol_handle_recv_async): "
		 "Dropped incoming SoL packet: Unrecognized connection.");
	return;
    }

    process_packet(sol, packet, data_len);

    sol_put_connection_unlock(sol);
}

static ipmi_payload_t ipmi_sol_payload =
{ sol_format_msg, sol_get_recv_seq, sol_handle_recv,
  sol_handle_recv_async, NULL /*sol_get_msg_tag*/ };

int
i_ipmi_sol_init()
{
    int rv;

    rv = ipmi_rmcpp_register_payload(IPMI_RMCPP_PAYLOAD_TYPE_SOL,
				     &ipmi_sol_payload);
    if (rv)
	goto out;

    rv = ipmi_create_global_lock(&sol_lock);
    if (rv) {
	ipmi_rmcpp_register_payload(IPMI_RMCPP_PAYLOAD_TYPE_SOL, NULL);
	goto out;
    }

 out:
    return rv;
}

void
i_ipmi_sol_shutdown(void)
{
    if (sol_lock) {
	ipmi_destroy_lock(sol_lock);
	sol_lock = NULL;
    }
    ipmi_rmcpp_register_payload(IPMI_RMCPP_PAYLOAD_TYPE_SOL, NULL);
}
