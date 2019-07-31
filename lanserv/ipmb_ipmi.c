/*
 * ipmb_ipmi.c
 *
 * IPMB server interface.
 *
 * Copyright 2019 Mellanox
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

#include <string.h>
#include <stdlib.h>
#include <OpenIPMI/serv.h>
#include <OpenIPMI/ipmbserv.h>
#include <OpenIPMI/ipmi_mc.h>

#define	IPMIDEV_MAX_SIZE	15

static void
ipmb_send(msg_t *imsg, ipmbserv_data_t *ipmb)
{
    unsigned char msg[(IPMI_SIM_MAX_MSG_LENGTH + 7) * 3];
    unsigned int msg_len;

    msg[0] = imsg->len + 7;
    msg[1] = imsg->rs_addr;
    msg[2] = (imsg->netfn << 2) | imsg->rs_lun;
    msg[3] = -ipmb_checksum(msg + 1, 2, 0);
    msg[4] = imsg->rq_addr;
    msg[5] = (imsg->rq_seq << 2) | imsg->rq_lun;
    msg[6] = imsg->cmd;
    memcpy(msg + 7, imsg->data, imsg->len);
    msg_len = imsg->len + 7;
    msg[msg_len] = -ipmb_checksum(msg + 4, msg_len - 4, 0);
    msg_len++;

    if (ipmb->sysinfo->debug & DEBUG_RAW_MSG)
	debug_log_raw_msg(ipmb->sysinfo, msg, msg_len, "Raw ipmb send:");
    ipmb->send_out(ipmb, msg, msg_len);
}

static void
ipmb_return_rsp(channel_t *chan, msg_t *imsg, rsp_msg_t *rsp)
{
    ipmbserv_data_t *ipmb = chan->chan_info;
    msg_t msg;

    msg.netfn = rsp->netfn;
    msg.cmd = rsp->cmd;
    msg.data = rsp->data;
    msg.len = rsp->data_len;
    msg.rq_lun = imsg->rs_lun;
    msg.rq_addr = imsg->rs_addr;
    msg.rs_lun = imsg->rq_lun;
    msg.rs_addr = imsg->rq_addr;
    msg.rq_seq = imsg->rq_seq;

    ipmb_send(&msg, ipmb);
}

int
ipmbserv_init(ipmbserv_data_t *ipmb)
{
    ipmb->channel.return_rsp = ipmb_return_rsp;
    chan_init(&ipmb->channel);

    return 0;
}

void
ipmbserv_handle_data(ipmbserv_data_t *ipmb, uint8_t *imsg, unsigned int len)
{
    msg_t msg;

    if (len < 8) {
	fprintf(stderr, "Message too short\n");
	return;
    }
    /* subtract len field and checksum */
    len--;
    imsg++;

    if (ipmb_checksum(imsg, len, 0) != 0) {
	fprintf(stderr, "Message checksum failure\n");
	return;
    }
    len--;

    memset(&msg, 0, sizeof(msg));

    msg.rs_addr = imsg[0];
    msg.netfn = imsg[1] >> 2;
    msg.rs_lun = imsg[1] & 3;
    /* imsg[2] is first checksum */
    msg.rq_addr = imsg[3];
    msg.rq_seq = imsg[4] >> 2;
    msg.rq_lun = imsg[4] & 3;
    msg.cmd = imsg[5];

    msg.len = len - 6;
    msg.data = imsg + 6;

    msg.src_addr = NULL;
    msg.src_len = 0;

    channel_smi_send(&ipmb->channel, &msg);
}

int
ipmbserv_read_config(char **tokptr, sys_data_t *sys, const char **errstr)
{
    ipmbserv_data_t *ipmb;
    unsigned int chan_num;
    int err;
    const char *tok;
    char *ipmbdev;

    err = get_uint(tokptr, &chan_num, errstr);
    if (err)
	return -1;

    if (chan_num >= IPMI_MAX_CHANNELS) {
	*errstr = "Invalid channel number, must be 0-15";
	return -1;
    }

    /*
     * Allow an IPMB channel to override the default channel 0.
     */
    if (chan_num != 0 && sys->chan_set[chan_num]) {
	    *errstr = "Channel already in use";
	    return -1;
    }

    tok = mystrtok(NULL, " \t\n", tokptr);
    if (!tok || strcmp(tok, "ipmb_dev_int")) {
	*errstr = "Config file missing <linux ipmb driver name>";
	return -1;
    }

    tok = mystrtok(NULL, " \t\n", tokptr);
    if (strlen(tok) > IPMIDEV_MAX_SIZE) {
	*errstr = "Length of device file name %s > 15";
	return -1;
    }
    ipmbdev = strdup(tok);
    if (!ipmbdev) {
	*errstr = "Unable to alloc device file name";
	return -1;
    }

    ipmb = malloc(sizeof(*ipmb));
    if (!ipmb) {
	free(ipmbdev);
	*errstr = "Out of memory";
	return -1;
    }
    memset(ipmb, 0, sizeof(*ipmb));
    ipmb->ipmbdev = ipmbdev;

    ipmb->channel.session_support = IPMI_CHANNEL_SESSION_LESS;
    ipmb->channel.medium_type = IPMI_CHANNEL_MEDIUM_IPMB;
    ipmb->channel.protocol_type = IPMI_CHANNEL_PROTOCOL_IPMB;

    ipmb->channel.channel_num = chan_num;

    ipmb->sysinfo = sys;
    ipmb->channel.chan_info = ipmb;

    if (chan_num == 0)
	ipmb->channel.prim_ipmb_in_cfg_file = 1;
    else
	ipmb->channel.prim_ipmb_in_cfg_file = 0;

    sys->chan_set[chan_num] = &ipmb->channel;

    return 0;
}
