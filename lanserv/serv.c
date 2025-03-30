/*
 * lanserv_ipmi.c
 *
 * MontaVista IPMI IPMI LAN interface protocol engine
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2003,2004,2005,2012 MontaVista Software Inc.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * Lesser General Public License (GPL) Version 2 or the modified BSD
 * license below.  The following disclamer applies to both licenses:
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
 * GNU Lesser General Public Licence
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
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

#include <config.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/time.h>

#include <OpenIPMI/ipmi_mc.h>
#include <OpenIPMI/ipmi_msgbits.h>
#include <OpenIPMI/lanserv.h>
#include <OpenIPMI/serv.h>
#include <OpenIPMI/ipmi_err.h>

int
ipmi_oem_send_msg(channel_t     *chan,
		  unsigned char netfn,
		  unsigned char cmd,
		  unsigned char *data,
		  unsigned int  len,
		  long          oem_data)
{
    msg_t *nmsg;
    int rv;
    sys_data_t *sys = chan->sys;

    nmsg = sys->alloc(sys, sizeof(*nmsg)+len);
    if (!nmsg) {
	sys->log(sys, OS_ERROR, NULL, "SMI message: out of memory");
	return ENOMEM;
    }
    nmsg->orig_channel = chan;
    nmsg->oem_data = oem_data;
    nmsg->netfn = netfn;
    nmsg->cmd = cmd;
    nmsg->data = ((unsigned char *) nmsg) + sizeof(*nmsg);
    nmsg->len = len;
    if (len > 0)
	memcpy(nmsg->data, data, len);
    
    rv = chan->smi_send(chan, nmsg);
    if (rv) {
	sys->log(sys, OS_ERROR, nmsg, "SMI send: error %d", rv);
	sys->free(sys, nmsg);
    }

    return rv;
}

void
ipmi_handle_smi_rsp(channel_t *chan, msg_t *msg, uint8_t *rspd, int rsp_len)
{
    rsp_msg_t rsp;

    if (!chan->return_rsp)
	return;

    rsp.netfn = msg->netfn | 1;
    rsp.cmd = msg->cmd;
    rsp.data = rspd;
    rsp.data_len = rsp_len;

    if (chan->oem.oem_handle_rsp &&
	chan->oem.oem_handle_rsp(chan, msg, &rsp))
	/* OEM code handled the response. */
	return;

    chan->return_rsp(chan, msg, &rsp);
}

static oem_handler_t *oem_handlers = NULL;

void
ipmi_register_oem(oem_handler_t *handler)
{
    handler->next = oem_handlers;
    oem_handlers = handler;
}

static void
check_oem_handlers(channel_t *chan)
{
    oem_handler_t *c;

    c = oem_handlers;
    while (c) {
	if ((c->manufacturer_id == chan->manufacturer_id)
	    && (c->product_id == chan->product_id))
	{
	    c->handler(chan, c->cb_data);
	    break;
	}
	c = c->next;
    }
}

int
channel_smi_send(channel_t *chan, msg_t *msg)
{
    msg->orig_channel = chan;
    msg->channel = chan->channel_num;

    /* Let the low-level interface intercept. */
    if (chan->oem_intf_recv_handler) {
	unsigned char    msgd[36];
	unsigned int     msgd_len = sizeof(msgd);

	if (chan->oem_intf_recv_handler(chan, msg, msgd, &msgd_len)) {
	    ipmi_handle_smi_rsp(chan, msg, msgd, msgd_len);
	    return 0;
	}
    }
    
    return chan->smi_send(chan, msg);
}

static int
look_for_get_devid(channel_t *chan, msg_t *msg, rsp_msg_t *rsp)
{
    if ((rsp->netfn == (IPMI_APP_NETFN | 1))
	&& (rsp->cmd == IPMI_GET_DEVICE_ID_CMD)
	&& (rsp->data_len >= 12)
	&& (rsp->data[0] == 0))
    {
	chan->oem.oem_handle_rsp = NULL;
	chan->manufacturer_id = (rsp->data[7]
				 | (rsp->data[8] << 8)
				 | (rsp->data[9] << 16));
	chan->product_id = rsp->data[10] | (rsp->data[11] << 8);
	check_oem_handlers(chan);

	/* Will be set to 1 if we sent it. */
	if (msg->oem_data) {
	    chan->sys->free(chan->sys, msg);
	    return 1;
	}
    }
    return 0;
}

int
chan_init(channel_t *chan)
{
    int rv = 0;

    /* If the calling code already hasn't set up an OEM handler, we
       set up our own to look for a get device id.  When we find a get
       device ID, we call the OEM code to install their own.  Hijack
       channel 0 for this. */
    if ((chan->channel_num == 15) && (chan->oem.oem_handle_rsp == NULL)) {
	chan->oem.oem_handle_rsp = look_for_get_devid;

	/* Send a get device id to the low-level code so we can
           discover who we are. */
	rv = ipmi_oem_send_msg(chan,
			       IPMI_APP_NETFN, IPMI_GET_DEVICE_ID_CMD,
			       NULL, 0, 1);
    }

    return rv;
}

void
sysinfo_init(sys_data_t *sys)
{
    memset(sys, 0, sizeof(*sys));
}

void
debug_log_raw_msg(sys_data_t *sys,
		  unsigned char *data, unsigned int len,
		  const char *format, ...)
{
    va_list ap;
    char *str;
    int slen;
    int pos;
    char dummy;
    unsigned int i;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    va_start(ap, format);
    slen = vsnprintf(&dummy, 0, format, ap);
    slen += snprintf(&dummy, 0, " %ld.%6.6ld", tv.tv_sec, tv.tv_usec);
    va_end(ap);
    slen += len * 3 + 2;
    str = sys->alloc(sys, slen);
    if (!str)
	return;
    va_start(ap, format);
    pos = vsprintf(str, format, ap);
    va_end(ap);
    pos += sprintf(str + pos, " %ld.%6.6ld", tv.tv_sec, tv.tv_usec);
    str[pos++] = '\n';
    str[pos] = '\0';
    for (i = 0; i < len; i++)
	pos += sprintf(str + pos, " %2.2x", data[i]);

    sys->log(sys, DEBUG, NULL, "%s", str);
    sys->free(sys, str);
}

/* Deal with multi-byte data, IPMI (little-endian) style. */
unsigned int ipmi_get_uint16(uint8_t *data)
{
    return (data[0]
	    | (data[1] << 8));
}

void ipmi_set_uint16(uint8_t *data, int val)
{
    data[0] = val & 0xff;
    data[1] = (val >> 8) & 0xff;
}

unsigned int ipmi_get_uint32(uint8_t *data)
{
    return (data[0]
	    | (data[1] << 8)
	    | (data[2] << 16)
	    | (data[3] << 24));
}

void ipmi_set_uint32(uint8_t *data, int val)
{
    data[0] = val & 0xff;
    data[1] = (val >> 8) & 0xff;
    data[2] = (val >> 16) & 0xff;
    data[3] = (val >> 24) & 0xff;
}

uint8_t
ipmb_checksum(uint8_t *data, int size, uint8_t start)
{
	uint8_t csum = start;
	
	for (; size > 0; size--, data++)
		csum += *data;

	return csum;
}

int
check_msg_length(msg_t         *msg,
		 unsigned int  len,
		 unsigned char *rdata,
		 unsigned int  *rdata_len)
{
    if (msg->len < len) {
	rdata[0] = IPMI_REQUEST_DATA_LENGTH_INVALID_CC;
	*rdata_len = 1;
	return 1;
    }

    return 0;
}

int
init_msg_q(struct msg_q *q, msg_q_op add_to_empty, msg_q_op now_empty,
	   void *cb_data)
{
    q->head = NULL;
    q->tail = NULL;
    q->add_to_empty = add_to_empty;
    q->now_empty = now_empty;
    q->cb_data = cb_data;
    return 0;
}

void
add_to_msg_q(struct msg_q *q, msg_t *msg)
{
    int was_empty = q->head == NULL;

    msg->next = NULL;
    if (was_empty) {
	q->head = msg;
	q->tail = msg;
    } else {
	q->tail->next = msg;
	q->tail = msg;
    }
    if (was_empty && q->add_to_empty)
	q->add_to_empty(q);
}

msg_t *
get_next_msg_q(struct msg_q *q)
{
    msg_t *msg = q->head;

    if (msg) {
	q->head = msg->next;
	if (!q->head) {
	    q->tail = NULL;
	    if (q->now_empty)
		q->now_empty(q);
	}
    }
    return msg;
}

char *
sys_strdup(sys_data_t *sys, const char *s)
{
    char *r = sys->alloc(sys, strlen(s) + 1);

    if (r)
	strcpy(r, s);
    return r;
}

msg_t *
ipmi_msg_alloc(sys_data_t *sys, unsigned int datalen)
{
    msg_t *msg;

    msg = sys->alloc(sys, sizeof(*msg) + datalen);
    if (msg) {
	msg->data = ((unsigned char *) msg) + sizeof(*msg);
	msg->len = datalen;
    }
    return msg;
}

void
ipmi_msg_free(sys_data_t *sys, msg_t *msg)
{
    if (msg->src_allocated)
	sys->free(sys, msg->src_addr);
    sys->free(sys, msg);
}

msg_t *
ipmi_msg_dup(sys_data_t *sys, msg_t *omsg,
	     unsigned int extra_size, unsigned int data_offset)
{
    msg_t *msg;

    msg = sys->alloc(sys, sizeof(*msg) + omsg->len) + extra_size;
    if (!msg)
	return NULL;

    *msg = *omsg;

    msg->data = ((unsigned char *) msg) + sizeof(*msg);
    msg->len = omsg->len + extra_size;
    memcpy(msg->data + data_offset, omsg->data, msg->len);

    return msg;
}
