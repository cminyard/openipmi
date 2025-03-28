/*
 * emu.c
 *
 * MontaVista IPMI code for emulating a MC.
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2003,2012 MontaVista Software Inc.
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
 *
 * Modified BSD Licence
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *   3. The name of the author may not be used to endorse or promote
 *      products derived from this software without specific prior
 *      written permission.
 */

#include "bmc.h"

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>

#include <OpenIPMI/ipmi_err.h>
#include <OpenIPMI/ipmi_msgbits.h>
#include <OpenIPMI/ipmi_picmg.h>
#include <OpenIPMI/ipmi_bits.h>
#include <OpenIPMI/ipmi_mc.h>
#include <OpenIPMI/ipmi_lan.h>
#include <OpenIPMI/extcmd.h>
#include <OpenIPMI/serv.h>
#include "ipmi_sim.h"

static void ipmi_mc_start_cmd(lmc_data_t *mc);


const char *
get_lanserv_version(void)
{
    return PVERSION;
}

struct {
    cmd_handler_f handler;
    void *cb_data;
} group_extension_handlers[256];

void
ipmi_emu_register_group_extension_handler(uint8_t group_extension,
					  cmd_handler_f handler, void *cb_data)
{
    group_extension_handlers[group_extension].handler = handler;
    group_extension_handlers[group_extension].cb_data = cb_data;
}

static void
handle_group_extension_netfn(lmc_data_t    *mc,
			     msg_t         *msg,
			     unsigned char *rdata,
			     unsigned int  *rdata_len,
			     void          *cb_data)
{
    uint8_t ge;

    if (check_msg_length(msg, 1, rdata, rdata_len))
	return;

    ge = msg->data[0];
    if (group_extension_handlers[ge].handler)
	group_extension_handlers[ge].handler(mc, msg, rdata, rdata_len,
				     group_extension_handlers[ge].cb_data);
    else
	handle_invalid_cmd(mc, rdata, rdata_len);
}

static struct iana_handler_elem {
    uint32_t iana;
    cmd_handler_f handler;
    void *cb_data;
    struct iana_handler_elem *next;
} *iana_handlers;

static struct iana_handler_elem *find_iana(uint32_t iana)
{
    struct iana_handler_elem *p = iana_handlers;

    while (p) {
	if (p->iana == iana)
	    return p;
	p = p->next;
    }
    return NULL;
}

int
ipmi_emu_register_iana_handler(uint32_t iana, cmd_handler_f handler,
			       void *cb_data)
{
    struct iana_handler_elem *p;

    if (iana > 0xffffff)
	return EINVAL;
    if (find_iana(iana))
	return EAGAIN;
    p = malloc(sizeof(*p));
    if (!p)
	return ENOMEM;
    p->iana = iana;
    p->handler = handler;
    p->cb_data = cb_data;
    p->next = iana_handlers;
    iana_handlers = p;
    return 0;
}

static void
handle_iana_netfn(lmc_data_t    *mc,
		  msg_t         *msg,
		  unsigned char *rdata,
		  unsigned int  *rdata_len,
		  void          *cb_data)
{
    struct iana_handler_elem *p;

    if (check_msg_length(msg, 3, rdata, rdata_len))
	return;

    msg->iana = msg->data[0] | (msg->data[1] << 8) | (msg->data[2] << 16);
    p = find_iana(msg->iana);
    if (!p) {
	handle_invalid_cmd(mc, rdata, rdata_len);
	goto out;
    }

    /* Remove the IANA */
    memmove(msg->data, msg->data + 3, msg->len - 3);
    msg->len -= 3;

    p->handler(mc, msg, rdata, rdata_len, p->cb_data);

 out:
    /* Insert the IANA back in. */
    memmove(rdata + 4, rdata + 1, *rdata_len);
    rdata[1] = msg->iana & 0xff;
    rdata[2] = (msg->iana >> 8) & 0xff;
    rdata[3] = (msg->iana >> 16) & 0xff;
    *rdata_len += 3;
}

static struct oi_iana_cmd_elem {
    uint8_t cmd;
    cmd_handler_f handler;
    void *cb_data;
    struct oi_iana_cmd_elem *next;
} *oi_iana_cmds;

static struct oi_iana_cmd_elem *find_oi_iana(uint8_t cmd)
{
    struct oi_iana_cmd_elem *p = oi_iana_cmds;

    while (p) {
	if (p->cmd == cmd)
	    return p;
	p = p->next;
    }
    return NULL;
}

static void handle_oi_iana_cmd(lmc_data_t    *mc,
			       msg_t         *msg,
			       unsigned char *rdata,
			       unsigned int  *rdata_len,
			       void          *cb_data)
{
    struct oi_iana_cmd_elem *p;

    p = find_oi_iana(msg->cmd);
    if (!p) {
	handle_invalid_cmd(mc, rdata, rdata_len);
	return;
    }

    p->handler(mc, msg, rdata, rdata_len, p->cb_data);
}

int
ipmi_emu_register_oi_iana_handler(uint8_t cmd, cmd_handler_f handler,
				  void *cb_data)
{
    struct oi_iana_cmd_elem *p;
    int rv;

    if (find_oi_iana(cmd))
	return EAGAIN;
    rv = ipmi_emu_register_iana_handler(OPENIPMI_IANA, handle_oi_iana_cmd,
					NULL);
    if (rv != 0 && rv != EAGAIN)
	return rv;
    p = malloc(sizeof(*p));
    if (!p)
	return ENOMEM;
    p->cmd = cmd;
    p->handler = handler;
    p->cb_data = cb_data;
    p->next = oi_iana_cmds;
    oi_iana_cmds = p;
    return 0;
}

static int
check_chassis_capable(lmc_data_t *mc)
{
    return (mc->device_support & IPMI_DEVID_CHASSIS_DEVICE);
}

typedef struct netfn_handler_s {
    cmd_handler_f *handlers;
    void          **cb_data;
    cmd_handler_f main_handler;
    void          *main_handler_cb_data;
    int (*check_capable)(lmc_data_t *mc);
} netfn_handler_t;

static netfn_handler_t netfn_handlers[32] = {
    [IPMI_APP_NETFN >> 1] = { .handlers = app_netfn_handlers },
    [IPMI_STORAGE_NETFN >> 1] = { .handlers = storage_netfn_handlers },
    [IPMI_CHASSIS_NETFN >> 1] = { .handlers = chassis_netfn_handlers,
			     .check_capable = check_chassis_capable },
    [IPMI_TRANSPORT_NETFN >> 1] = { .handlers = transport_netfn_handlers },
    [IPMI_SENSOR_EVENT_NETFN >> 1] = { .handlers = sensor_event_netfn_handlers },
    [IPMI_GROUP_EXTENSION_NETFN >> 1] = { .main_handler = handle_group_extension_netfn },
    [IPMI_OEM_GROUP_NETFN >> 1] = { .main_handler = handle_iana_netfn },
    [0x30 >> 1] = { .handlers = oem0_netfn_handlers }
};

int
ipmi_emu_register_cmd_handler(unsigned char netfn, unsigned char cmd,
			      cmd_handler_f handler, void *cb_data)
{
    unsigned int ni = netfn >> 1;

    if (ni >= 32)
	return EINVAL;

    if (!netfn_handlers[ni].handlers) {
	netfn_handlers[ni].handlers = malloc(256 * sizeof(cmd_handler_f));
	if (!netfn_handlers[ni].handlers)
	    return ENOMEM;
	memset(netfn_handlers[ni].handlers, 0, 256 * sizeof(cmd_handler_f));
    }
    if (!netfn_handlers[ni].cb_data) {
	netfn_handlers[ni].cb_data = malloc(256 * sizeof(void *));
	if (!netfn_handlers[ni].cb_data)
	    return ENOMEM;
	memset(netfn_handlers[ni].cb_data, 0, 256 * sizeof(void *));
    }

    netfn_handlers[ni].cb_data[cmd] = cb_data;
    netfn_handlers[ni].handlers[cmd] = handler;
    return 0;
}

void
ipmi_emu_tick(emu_data_t *emu, unsigned int seconds)
{
    if (emu->atca_fru_inv_locked) {
	emu->atca_fru_inv_lock_timeout -= seconds;
	if (emu->atca_fru_inv_lock_timeout < 0) {
	    emu->atca_fru_inv_locked = 0;
	    free(emu->temp_fru_inv_data);
	    emu->temp_fru_inv_data = NULL;
	}
    }

    if (emu->users_changed) {
	emu->users_changed = 0;
	write_persist_users(emu->sysinfo);
    }
}

static void
enqueue_xmit_msg(channel_t *chan, lmc_data_t *mc, msg_t *msg)
{
    if (chan->xmit_q_tail) {
	msg->next = chan->xmit_q_tail;
	chan->xmit_q_tail = msg;
    } else {
	msg->next = NULL;
	chan->xmit_q_head = msg;
	chan->xmit_q_tail = msg;
	if (chan->channel_num == 15) {
	    chan->mc->msg_flags |= IPMI_MC_MSG_FLAG_RCV_MSG_QUEUE;
	    if (chan->set_atn)
		chan->set_atn(chan, 1, IPMI_MC_MSG_INTS_ON(chan->mc));
	}
    }
}

static void
deliver_msg_to_mc(lmc_data_t *mc, msg_t *msg,
		  unsigned char *rdata, unsigned int *rdata_len)
{
    /* Now handle the message on the destination mc. */
    if (netfn_handlers[msg->netfn >> 1].check_capable &&
	!netfn_handlers[msg->netfn >> 1].check_capable(mc))
	handle_invalid_cmd(mc, rdata, rdata_len);
    else if (netfn_handlers[msg->netfn >> 1].main_handler)
	netfn_handlers[msg->netfn >> 1].main_handler(mc, msg, rdata, rdata_len,
			 netfn_handlers[msg->netfn >> 1].main_handler_cb_data);
    else if (netfn_handlers[msg->netfn >> 1].handlers &&
	     netfn_handlers[msg->netfn >> 1].handlers[msg->cmd]) {
	void *cb_data = NULL;
	if (netfn_handlers[msg->netfn >> 1].cb_data)
	    cb_data = netfn_handlers[msg->netfn >> 1].cb_data[msg->cmd];
	netfn_handlers[msg->netfn >> 1].handlers[msg->cmd](mc, msg, rdata,
		 rdata_len, cb_data);
    } else
	handle_invalid_cmd(mc, rdata, rdata_len);

    if (mc->emu->sysinfo->debug & DEBUG_MSG)
	debug_log_raw_msg(mc->emu->sysinfo, rdata, *rdata_len,
			  "Response message:");
}


static void
ipmb_handle_send_msg(channel_t *chan,
		     msg_t *omsg,
		     send_msg_handle_msg_fixup fixup, void *fixup_data,
		     unsigned char *ordata,
		     unsigned int *ordata_len)
{
    lmc_data_t *mc = chan->chan_info;
    emu_data_t *emu = mc->emu;
    channel_t *ochan = omsg->orig_channel;
    unsigned int  data_len;
    msg_t smsg, qmsg;
    uint8_t qmsg_data[IPMI_SIM_MAX_MSG_LENGTH];
    unsigned char slave;
    unsigned char *data = NULL;
    unsigned char *rdata, tmp_rdata[1];
    unsigned int  *rdata_len, tmp_rdata_len = 1;

    if (check_msg_length(omsg, 8, ordata, ordata_len))
	return;

    data = omsg->data + 1;
    data_len = omsg->len - 1;

    if (data[0] == 0) {
	/* Broadcast, just skip the first byte, but check len. */
	data++;
	data_len--;
	if (data_len < 7) {
	    ordata[0] = IPMI_REQUEST_DATA_LENGTH_INVALID_CC;
	    *ordata_len = 1;
	    return;
	}
    }
    slave = data[0];
    mc = emu->sysinfo->ipmb_addrs[slave];
    if (!mc || !mc->enabled) {
	ordata[0] = 0x83; /* NAK on Write */
	*ordata_len = 1;
	return;
    }

    smsg.netfn = data[1] >> 2;
    if (smsg.netfn & 1) {
	/* FIXME - It's a response, deliver it differently. */
	ordata[0] = 0xff;
	*ordata_len = 1;
	return;
    }

    /* Set up a response message. */
    memset(&qmsg, 0, sizeof(qmsg));
    qmsg.data = qmsg_data;
    qmsg.len = IPMI_SIM_MAX_MSG_LENGTH;
    rdata = qmsg.data;
    rdata_len = &qmsg.len;

    /* Now create a message to deliver to the MC for handling. */
    smsg.src_addr = omsg->src_addr;
    smsg.src_len = omsg->src_len;
    smsg.daddr = data[0];
    smsg.dlun = data[1] & 0x3;
    smsg.saddr = data[3];
    smsg.slun = data[4] & 0x3;
    smsg.rq_seq = data[4] >> 2;
    smsg.cmd = data[5];
    smsg.data = data + 6;
    smsg.len = data_len - 7; /* Subtract off the header and
				the end checksum */
    smsg.channel = omsg->channel;
    smsg.orig_channel = omsg->orig_channel;
    smsg.sid = omsg->sid;

    /* Set up tracking, replace the sequence number and track. */
    if (omsg->track && reserve_mc_seq(ochan->mc, &smsg, rdata, rdata_len))
	return;

    if (fixup(fixup_data, chan, &smsg, rdata, rdata_len))
	return;

    if (mc->emu->sysinfo->debug & DEBUG_MSG)
	chan->log(chan, DEBUG, &smsg, "IPMB deliver to MC:");

    deliver_msg_to_mc(mc, &smsg, rdata, rdata_len);

    /*
     * The actual send message succeeded.  The response from the MC is
     * separate, in qmsg.
     */
    ordata[0] = 0;
    *ordata_len = 1;

    /* Now set up the response message as if we received it from IPMB. */
    qmsg.channel = chan->channel_num;
    qmsg.orig_channel = chan;
    qmsg.netfn = smsg.netfn | 1;
    qmsg.cmd = smsg.cmd;
    qmsg.daddr = smsg.saddr;
    qmsg.dlun = smsg.slun;
    qmsg.saddr = smsg.daddr;
    qmsg.slun = smsg.dlun;
    qmsg.rq_seq = smsg.rq_seq;

    if (ochan->recv_in_q) {
	if (ochan->recv_in_q(ochan, &qmsg))
	    return;
    }

    /* Act as though we received a message over IPMB. */
    ipmi_emu_handle_msg(mc->emu, chan->mc, &qmsg, tmp_rdata, &tmp_rdata_len);
}

static int
ipmb_format_lun_2(channel_t *chan, msg_t *qmsg,
		  unsigned char *rdata, unsigned int *rdata_len)
{
    qmsg->data[0] = (qmsg->netfn << 2) | qmsg->dlun;
    qmsg->data[1] = -ipmb_checksum(qmsg->data, 1, 0);
    qmsg->data[2] = qmsg->saddr;
    qmsg->data[3] = (qmsg->rq_seq << 2) | qmsg->slun;
    qmsg->data[4] = qmsg->cmd;
    qmsg->data[qmsg->len - 1] = -ipmb_checksum(qmsg->data, qmsg->len - 2, 0);
    return 0;
}

/*
 * Route a send message command.
 */
static int
handle_lun_2_cmd(emu_data_t    *emu,
		 lmc_data_t    *mc,
		 msg_t         *omsg,
		 unsigned char *rdata,
		 unsigned int  *rdata_len)
{
    channel_t *ochan = omsg->orig_channel;
    msg_t *qmsg;
    unsigned char tmp_rdata[1];
    unsigned int  tmp_rdata_len = 1;

    if (!mc->channels[15]) {
	rdata[0] = 0xff;
	*rdata_len = 1;
	return 1;
    }

    if (!ochan->format_lun_2) {
	rdata[0] = IPMI_INVALID_DATA_FIELD_CC;
	*rdata_len = 1;
	return 1;
    }

    qmsg = malloc(sizeof(*qmsg) + omsg->len + ochan->get_msg_overhead);
    if (!qmsg) {
	rdata[0] = IPMI_OUT_OF_SPACE_CC;
	*rdata_len = 1;
	return 1;
    }
    memset(qmsg, 0, sizeof(*qmsg));

    *qmsg = *omsg;

    qmsg->data = ((unsigned char *) qmsg) + sizeof(*qmsg);
    qmsg->len = omsg->len + ochan->get_msg_overhead;
    memcpy(qmsg->data + ochan->get_msg_header_size, omsg->data, omsg->len);
    qmsg->orig_channel = ochan;
    qmsg->channel = ochan->channel_num;

    if (ochan->session_support != IPMI_CHANNEL_SESSION_LESS)
	qmsg->track = 1;

    if (qmsg->track && reserve_mc_seq(ochan->mc, qmsg, rdata, rdata_len))
	return 1;

    if (ochan->format_lun_2(ochan, qmsg, rdata, rdata_len)) {
	if (qmsg->track)
	    find_mc_seq(ochan->mc, qmsg, tmp_rdata, &tmp_rdata_len);
	free(qmsg);
	return 1;
    }

    if (mc->recv_q_tail) {
	qmsg->next = mc->recv_q_tail;
	mc->recv_q_tail = qmsg;
    } else {
	channel_t *chan = mc->channels[15];

	qmsg->next = NULL;
	mc->recv_q_head = qmsg;
	mc->recv_q_tail = qmsg;
	mc->msg_flags |= IPMI_MC_MSG_FLAG_RCV_MSG_QUEUE;
	if (chan->set_atn)
	    chan->set_atn(chan, 1, IPMI_MC_MSG_INTS_ON(mc));
    }

    return 0; /* Don't return a response. */
}

int
reserve_mc_seq(lmc_data_t *mc, msg_t *msg, 
	       unsigned char *rdata,
	       unsigned int  *rdata_len)
{
    unsigned int i, j;

    for (i = 0, j = mc->next_seq; i < 64; i++) {
	if (!mc->seq_entries[j].inuse) {
	    struct seq_entry *e = &mc->seq_entries[j];

	    e->inuse = 1;
	    e->orig_seq = msg->rq_seq;
	    e->chan_num = msg->channel;
	    e->sid = msg->sid;
	    msg->rq_seq = j;
	    mc->next_seq = (j + 1) % 64;
	    return 0;
	}
	j = (j + 1) % 64;
    }
    *rdata = IPMI_OUT_OF_SPACE_CC;
    *rdata_len = 1;
    return 1;
}

int
find_mc_seq(lmc_data_t *mc, msg_t *msg,
	    unsigned char *rdata,
	    unsigned int  *rdata_len)
{
    struct seq_entry *e = &mc->seq_entries[msg->rq_seq];

    if (!e->inuse) {
	rdata[0] = IPMI_NOT_PRESENT_CC;
	*rdata_len = 1;
	return 1;
    }
    msg->channel = e->chan_num;
    msg->rq_seq = e->orig_seq;
    msg->orig_channel = mc->channels[msg->channel];
    msg->sid = e->sid;
    e->inuse = 0;
    return 0;
}

int
handle_response(emu_data_t    *emu,
		lmc_data_t    *mc,
		msg_t         *msg,
		unsigned char *rdata,
		unsigned int  *rdata_len)
{
    msg_t *qmsg;

    qmsg = malloc(sizeof(*qmsg) + msg->len);
    if (!qmsg) {
	rdata[0] = IPMI_OUT_OF_SPACE_CC;
	*rdata_len = 1;
	return 1;
    }
    memset(qmsg, 0, sizeof(*qmsg));

    *qmsg = *msg;

    qmsg->data = ((unsigned char *) qmsg) + sizeof(*qmsg);
    qmsg->len = msg->len;
    memcpy(qmsg->data, msg->data, msg->len);

    if (find_mc_seq(mc, qmsg, rdata, rdata_len)) {
	free(qmsg);
	return 1;
    }

    enqueue_xmit_msg(qmsg->orig_channel, mc, qmsg);

    return 0;
}

/*
 * Messages handline depends on the source of the message, the
 * destination, and whether the message is a command or responses.
 * There are two basic source and destination types: session-less
 * (IPMB, system interface) and session-oriented (LAN). Messages have
 * three basic routes:
 *
 * - A message directly to the MC to LUN 0.  These are handled
 *   directly, the response to the message is returned in rdata.
 *   If it's a send message, that is handled in the third scenario
 *   below.  Responses are currently ignored, there is nothing that
 *   generates commands here.
 *
 * - A message to LUN 2, which means that the message is routed to the
 *   host of the MC.  If delivered, these go into the receive queue
 *   for the MC with the formatting described by the Get Message
 *   command.
 *
 *   - Messages from a session-less channel are commands from other
 *     MCs to this MC or responses to commands sent from this MC.  No
 *     response is returned in rdata unless the delivery fails.
 *
 *   - Commands from session-oriented channel, a sequence number is
 *     generated and the message information is stored.  A timer also
 *     times these and removes them after a time if no response is
 *     received.  No response is returned to rdata directly from this
 *     unless the send fails, the response will be generated later
 *     when the host does a send message response or the timeout
 *     happens.
 *
 *   - Responses from a session-oriented channel.  These are just
 *     delivered, a send success/failure response is returned in
 *     rdata.
 *
 * - A send message command, which is routed to a channel on the MC.
 *   rdata returns whether the send succeeded or failed.  If the send
 *   fails, no tracking is done.  This is done in bmc_app.c in
 *   handle_send_msg().
 *
 *   - Commands to session-oriented channels are permitted, but probably
 *     not very useful.
 *
 *   - Commands from a session-less channel must be untracked and are
 *     just sent.  rdata is used to say whether the send succeeded or
 *     failed.  IPMB is not permitted as a source, and the response
 *     LUN must be 2 to map back to the system interface.
 *
 *   - Responses from a session-less channel to a session-less channel
 *     are just sent.
 *
 *   - Responses from a session-less channel to a session-oriented
 *     channel must have been tracked.  These are looked up by sequence
 *     number and the response is returned.
 *
 *   - Commands from a session-oriented channel.  These must be
 *     tracked and will generate a sequence number.  The original
 *     sequence number is stored and restored in the later response.
 *     A timer times these.
 *
 *   - Responses from a session-oriented channel are just delivered.
 *
 * Note that rdata_len holds the length of rdata, and it must be at
 * least 1.
 */
int
ipmi_emu_handle_msg(emu_data_t    *emu,
		    lmc_data_t    *mc,
		    msg_t         *omsg,
		    unsigned char *rdata,
		    unsigned int  *rdata_len)
{
    if (emu->sysinfo->debug & DEBUG_MSG)
	emu->sysinfo->log(emu->sysinfo, DEBUG, omsg, "Receive message:");

    if (!mc->enabled) {
	rdata[0] = 0xff;
	*rdata_len = 1;
	return 1;
    }

    /* Figure out where the message goes. */
    if (omsg->dlun == 2)
	return handle_lun_2_cmd(emu, mc, omsg, rdata, rdata_len);

    if (omsg->netfn & 1)
	handle_response(emu, mc, omsg, rdata, rdata_len);
    else
	deliver_msg_to_mc(mc, omsg, rdata, rdata_len);

    return 1; /* Return a response. */
}

void
is_resend_atn(channel_t *chan)
{
    lmc_data_t *mc = chan->mc;

    if (chan->set_atn)
	chan->set_atn(chan, !!mc->msg_flags, IPMI_MC_MSG_INTS_ON(mc));
}

emu_data_t *
ipmi_emu_alloc(void *user_data, ipmi_emu_sleep_cb sleeper, sys_data_t *sysinfo)
{
    emu_data_t *data = malloc(sizeof(*data));

    if (data) {
	memset(data, 0, sizeof(*data));
	data->user_data = user_data;
	data->sleeper = sleeper;
	data->sysinfo = sysinfo;
    }
	
    return data;
}

int
ipmi_emu_set_addr(emu_data_t *emu, unsigned int addr_num,
		  unsigned char addr_type,
		  void *addr_data, unsigned int addr_len)
{
    emu_addr_t *addr;

    if (addr_num >= MAX_EMU_ADDR)
	return EINVAL;

    addr = &(emu->addr[addr_num]);
    if (addr_len > sizeof(addr->addr_data))
	return EINVAL;

    emu->sysinfo->get_monotonic_time(emu->sysinfo, &emu->last_addr_change_time);
    addr->addr_type = addr_type;
    memcpy(addr->addr_data, addr_data, addr_len);
    addr->addr_len = addr_len;
    addr->valid = 1;
    return 0;
}

int
ipmi_emu_clear_addr(emu_data_t *emu, unsigned int addr_num)
{
    emu_addr_t *addr;

    if (addr_num >= MAX_EMU_ADDR)
	return EINVAL;

    addr = &(emu->addr[addr_num]);
    addr->valid = 0;
    return 0;
}

void
ipmi_emu_sleep(emu_data_t *emu, struct timeval *time)
{
    emu->sleeper(emu, time);
}

void *
ipmi_emu_get_user_data(emu_data_t *emu)
{
    return emu->user_data;
}

void
ipmi_mc_destroy(lmc_data_t *mc)
{
    sel_entry_t *entry, *n_entry;

    entry = mc->sel.entries;
    while (entry) {
	n_entry = entry->next;
	free(entry);
	entry = n_entry;
    }
    free(mc);
}

int
ipmi_emu_set_mc_guid(lmc_data_t *mc,
		     unsigned char guid[16],
		     int force)
{
    if (force || !mc->guid_set)
	memcpy(mc->guid, guid, 16);
    mc->guid_set = 1;
    return 0;
}

void
ipmi_mc_disable(lmc_data_t *mc)
{
    mc->enabled = 0;
}

void
ipmi_mc_enable(lmc_data_t *mc)
{
    unsigned int i;
    sys_data_t *sys = mc->sysinfo;

    mc->enabled = 1;

    for (i = 0; i < IPMI_MAX_CHANNELS; i++) {
	channel_t *chan = mc->channels[i];
	int err = 0;

	if (!chan)
	    continue;

	chan->smi_send = sys->csmi_send;
	chan->oem.user_data = sys->info;
	chan->alloc = sys->calloc;
	chan->free = sys->cfree;
	chan->log = sys->clog;
	chan->mc = mc;

	if (chan->medium_type == IPMI_CHANNEL_MEDIUM_8023_LAN)
	    err = sys->lan_channel_init(sys->info, chan);
	else if (chan->medium_type == IPMI_CHANNEL_MEDIUM_RS232)
	    err = sys->ser_channel_init(sys->info, chan);
	else if ((chan->medium_type == IPMI_CHANNEL_MEDIUM_IPMB) &&
		((chan->channel_num != 0) || (chan->prim_ipmb_in_cfg_file)))
	    err = sys->ipmb_channel_init(sys->info, chan);
	else 
	    chan_init(chan);
	if (err) {
	    chan->log(chan, SETUP_ERROR, NULL,
		      "Unable to initialize channel for "
		      "IPMB 0x%2.2x, channel %d: %d",
		      mc->ipmb, chan->channel_num, err);
	}
    }

    if (mc->startcmd.startnow && mc->startcmd.startcmd)
	ipmi_mc_start_cmd(mc);
}

int
ipmi_mc_set_num_leds(lmc_data_t   *mc,
		     unsigned int count)
{
    if (count > MAX_LEDS)
	return EINVAL;
    if (mc->emu->atca_mode && (count < MIN_ATCA_LEDS))
	return EINVAL;

    mc->num_leds = count;
    return 0;
}

static int
init_mc(emu_data_t *emu, lmc_data_t *mc, unsigned int persist_sdr)
{
    int err;

    err = mc->sysinfo->alloc_timer(mc->sysinfo, watchdog_timeout,
				   mc, &mc->watchdog_timer);
    if (err)
	return err;

    if (persist_sdr && mc->has_device_sdrs) {
	read_mc_sdrs(mc, &mc->device_sdrs[0], "device0");
	read_mc_sdrs(mc, &mc->device_sdrs[1], "device1");
	read_mc_sdrs(mc, &mc->device_sdrs[2], "device2");
	read_mc_sdrs(mc, &mc->device_sdrs[3], "device3");
    }

    if (persist_sdr && (mc->device_support & IPMI_DEVID_SDR_REPOSITORY_DEV))
	read_mc_sdrs(mc, &mc->main_sdrs, "main");

    return err;
}

static void
ipmi_mc_start_cmd(lmc_data_t *mc)
{
    if (!mc->startcmd.startcmd) {
	mc->sysinfo->log(mc->sysinfo, OS_ERROR, NULL,
			 "Power on issued, no start command set");
	return;
    }

    if (mc->startcmd.vmpid) {
	/* Already running */

	/* If we are waiting for a poweroff, disable that. */
	if (mc->startcmd.wait_poweroff)
	    mc->startcmd.wait_poweroff = 0;
	return;
    }

    ipmi_do_start_cmd(&mc->startcmd);
}

static void
chan_start_cmd(channel_t *chan)
{
    ipmi_mc_start_cmd(chan->mc);
}

static void
ipmi_mc_stop_cmd(lmc_data_t *mc, int do_it_now)
{
    if (mc->startcmd.wait_poweroff || !mc->startcmd.vmpid)
	/* Already powering/powered off. */
	return;
    if (!do_it_now)
	mc->startcmd.wait_poweroff = mc->startcmd.poweroff_wait_time;
    else
	mc->startcmd.wait_poweroff = 1; /* Just power off now. */
}

static void
chan_stop_cmd(channel_t *chan, int do_it_now)
{
    ipmi_mc_stop_cmd(chan->mc, do_it_now);
}

channel_t **
is_mc_get_channelset(lmc_data_t *mc)
{
    return mc->channels;
}

ipmi_sol_t *
is_mc_get_sol(lmc_data_t *mc)
{
    return &mc->sol;
}

unsigned char
is_mc_get_ipmb(lmc_data_t *mc)
{
    return mc->ipmb;
}

int
is_mc_users_changed(lmc_data_t *mc)
{
    int rv = mc->users_changed;
    mc->users_changed = 0;
    return rv;
}

user_t *
is_mc_get_users(lmc_data_t *mc)
{
    return mc->users;
}

pef_data_t *
is_mc_get_pef(lmc_data_t *mc)
{
    return &mc->pef;
}

startcmd_t *
is_mc_get_startcmdinfo(lmc_data_t *mc)
{
    return &mc->startcmd;
}

int
is_mc_alloc_unconfigured(sys_data_t *sys, unsigned char ipmb,
			 lmc_data_t **rmc)
{
    lmc_data_t *mc;
    unsigned int i;
    
    mc = sys->ipmb_addrs[ipmb];
    if (mc) {
	if (mc->configured) {
	    sys->log(sys, SETUP_ERROR, NULL,
		     "MC IPMB specified twice: 0x%x.", ipmb);
	    return EBUSY;
	}
	goto out;
    }

    mc = malloc(sizeof(*mc));
    if (!mc)
	return ENOMEM;
    memset(mc, 0, sizeof(*mc));
    mc->ipmb = ipmb;
    sys->ipmb_addrs[ipmb] = mc;

    mc->startcmd.poweroff_wait_time = 60;
    mc->startcmd.kill_wait_time = 20;
    mc->startcmd.startnow = 0;

    for (i=0; i<=MAX_USERS; i++) {
	mc->users[i].idx = i;
    }

    mc->pef.num_event_filters = MAX_EVENT_FILTERS;
    for (i=0; i<MAX_EVENT_FILTERS; i++) {
	mc->pef.event_filter_table[i][0] = i;
	mc->pef.event_filter_data1[i][0] = i;
    }
    mc->pef.num_alert_policies = MAX_ALERT_POLICIES;
    for (i=0; i<MAX_ALERT_POLICIES; i++)
	mc->pef.alert_policy_table[i][0] = i;
    mc->pef.num_alert_strings = MAX_ALERT_STRINGS;
    for (i=0; i<MAX_ALERT_STRINGS; i++) {
	mc->pef.alert_string_keys[i][0] = i;
    }

    mc->ipmb_channel.medium_type = IPMI_CHANNEL_MEDIUM_IPMB;
    mc->ipmb_channel.channel_num = 0;
    mc->ipmb_channel.protocol_type = IPMI_CHANNEL_PROTOCOL_IPMB;
    mc->ipmb_channel.session_support = IPMI_CHANNEL_SESSION_LESS;
    mc->ipmb_channel.active_sessions = 0;
    mc->ipmb_channel.chan_info = mc;
    mc->ipmb_channel.prim_ipmb_in_cfg_file = 0;
    mc->ipmb_channel.get_msg_overhead = 6;/* 5 byte header and a end checksum. */
    mc->ipmb_channel.get_msg_header_size = 5;
    mc->ipmb_channel.handle_send_msg = ipmb_handle_send_msg;
    mc->ipmb_channel.format_lun_2 = ipmb_format_lun_2;
    mc->channels[0] = &mc->ipmb_channel;
    mc->channels[0]->log = sys->clog;

 out:
    *rmc = mc;
    return 0;
}

void
handle_invalid_cmd(lmc_data_t    *mc,
		   unsigned char *rdata,
		   unsigned int  *rdata_len)
{
    rdata[0] = IPMI_INVALID_CMD_CC;
    *rdata_len = 1;
}

static void
handle_tick(void *info, unsigned int seconds)
{
    lmc_data_t *mc = info;

    if (mc->startcmd.wait_poweroff) {
	if (mc->startcmd.wait_poweroff > 0) {
	    /* Waiting for the first kill */
	    mc->startcmd.wait_poweroff--;
	    if (mc->startcmd.wait_poweroff == 0) {
		if (mc->startcmd.vmpid)
		    ipmi_do_kill(&mc->startcmd, 0);
		mc->startcmd.wait_poweroff = -mc->startcmd.kill_wait_time;
	    }
	} else {
	    mc->startcmd.wait_poweroff++;
	    if (mc->startcmd.wait_poweroff == 0 && mc->startcmd.vmpid)
		ipmi_do_kill(&mc->startcmd, 1);
	}
    }
}

static void
handle_child_quit(void *info, pid_t pid)
{
    lmc_data_t *mc = info;

    if (mc->startcmd.vmpid == pid) {
	mc->startcmd.vmpid = 0;
	mc->startcmd.wait_poweroff = 0;
    }
}

int
ipmi_emu_add_mc(emu_data_t    *emu,
		unsigned char ipmb,
		unsigned char device_id,
		unsigned char has_device_sdrs,
		unsigned char device_revision,
		unsigned char major_fw_rev,
		unsigned char minor_fw_rev,
		unsigned char device_support,
		unsigned char mfg_id[3],
		unsigned char product_id[2],
		unsigned int  flags)
{
    lmc_data_t     *mc;
    struct timeval t;
    int            i;
    sys_data_t     *sys = emu->sysinfo;

    i = is_mc_alloc_unconfigured(sys, ipmb, &mc);
    if (i)
	return i;

    mc->sysinfo = sys;
    mc->emu = emu;
    mc->ipmb = ipmb;

    mc->device_id = device_id;
    mc->has_device_sdrs = has_device_sdrs;
    mc->device_revision = device_revision;
    mc->major_fw_rev = major_fw_rev;
    mc->minor_fw_rev = minor_fw_rev;
    mc->device_support = device_support;
    mc->dynamic_sensor_population = flags & IPMI_MC_DYNAMIC_SENSOR_POPULATION;
    memcpy(mc->mfg_id, mfg_id, 3);
    memcpy(mc->product_id, product_id, 2);

    /* Enable the event log by default. */
    mc->global_enables = 1 << IPMI_MC_EVENT_LOG_BIT;

    /* Start the time at zero. */
    emu->sysinfo->get_monotonic_time(emu->sysinfo, &t);
    mc->sel.time_offset = 0;
    mc->main_sdrs.time_offset = 0;
    mc->main_sdrs.next_entry = 1;
    mc->main_sdrs.flags |= IPMI_SDR_RESERVE_SDR_SUPPORTED;
    for (i=0; i<4; i++) {
	mc->device_sdrs[i].time_offset = 0;
	mc->device_sdrs[i].next_entry = 1;
    }

    mc->event_receiver = sys->bmc_ipmb;
    mc->event_receiver_lun = 0;

    mc->hs_sensor = NULL;

    if (emu->atca_mode) {
	mc->num_leds = 2;

	/* By default only blue LED has local control. */
	mc->leds[0].loc_cnt = 1;
	mc->leds[0].loc_cnt_sup = 1;

	mc->leds[0].def_loc_cnt_color = 1; /* Blue LED */
	mc->leds[0].def_override_color = 1;
	mc->leds[0].color_sup = 0x2;
	mc->leds[0].color = 0x1;

	for (i=1; i<MAX_LEDS; i++) {
	    /* Others default to red */
	    mc->leds[i].def_loc_cnt_color = 2;
	    mc->leds[i].def_override_color = 2;
	    mc->leds[i].color_sup = 0x2;
	    mc->leds[i].color = 0x2;
	}
    }

    if (ipmb == emu->sysinfo->bmc_ipmb) {
	int rv;

	if (!mc->channels[15]) {
	    /* No one specified a system channel, make one up */
	    mc->sys_channel.medium_type = IPMI_CHANNEL_MEDIUM_SYS_INTF;
	    mc->sys_channel.channel_num = 15;
	    mc->sys_channel.protocol_type = IPMI_CHANNEL_PROTOCOL_KCS;
	    mc->sys_channel.session_support = IPMI_CHANNEL_SESSION_LESS;
	    mc->sys_channel.active_sessions = 0;
	    mc->channels[15] = &mc->sys_channel;
	}

	mc->sysinfo = emu->sysinfo;
	rv = init_mc(emu, mc, flags & IPMI_MC_PERSIST_SDR);
	if (rv) {
	    free(mc);
	    return rv;
	}
    }

    if (mc->startcmd.startcmd) {
	mc->child_quit_handler.info = mc;
	mc->child_quit_handler.handler = handle_child_quit;
	ipmi_register_child_quit_handler(&mc->child_quit_handler);
	mc->tick_handler.info = mc;
	mc->tick_handler.handler = handle_tick;
	emu->sysinfo->register_tick_handler(&mc->tick_handler);
	mc->channels[15]->start_cmd = chan_start_cmd;
	mc->channels[15]->stop_cmd = chan_stop_cmd;
    }

    mc->configured = 1;

    return 0;
}

void
is_set_chassis_control_prog(lmc_data_t *mc, const char *prog)
{
    mc->chassis_control_prog = prog;
}

void
ipmi_mc_set_chassis_control_func(lmc_data_t *mc,
				 int (*set)(lmc_data_t *mc, int op,
					    unsigned char *val,
					    void *cb_data),
				 int (*get)(lmc_data_t *mc, int op,
					    unsigned char *val,
					    void *cb_data),
				 void *cb_data)
{
    mc->chassis_control_set_func = set;
    mc->chassis_control_get_func = get;
    mc->chassis_control_cb_data = cb_data;
}

void
ipmi_get_product_id(lmc_data_t *mc, unsigned char product_id[2])
{
    memcpy(product_id, mc->product_id, 2);
}

int
ipmi_emu_get_mc_by_addr(emu_data_t *emu, unsigned char ipmb, lmc_data_t **mc)
{
    if (!emu->sysinfo->ipmb_addrs[ipmb])
	return ENOSYS;
    *mc = emu->sysinfo->ipmb_addrs[ipmb];
    return 0;
}

int
ipmi_emu_set_bmc_mc(emu_data_t *emu, unsigned char ipmb)
{
    lmc_data_t *mc;

    if (ipmb & 1)
	return EINVAL;
    emu->sysinfo->bmc_ipmb = ipmb;
    if (!ipmi_emu_get_mc_by_addr(emu, ipmb, &mc))
	mc->sysinfo = emu->sysinfo;
    return 0;
}

lmc_data_t *
ipmi_emu_get_bmc_mc(emu_data_t *emu)
{
    lmc_data_t *mc;
    
    if (!ipmi_emu_get_mc_by_addr(emu, emu->sysinfo->bmc_ipmb, &mc))
	return mc;
    return NULL;
}

void
emu_set_debug_level(emu_data_t *emu, unsigned int debug_level)
{
    emu->sysinfo->debug = debug_level;
}
