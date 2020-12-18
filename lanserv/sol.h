/*
 * sol.h
 *
 * MontaVista IPMI LAN server include file
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2003,2004,2005 MontaVista Software Inc.
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
#ifndef SOL_H
#define SOL_H

/*
 * SOL handling
 */

typedef struct solparm_s {
    int enabled;
    int bitrate;
    int bitrate_nonv;
    int default_bitrate;
} solparm_t;

typedef struct soldata_s soldata_t;

typedef struct ipmi_sol_s {
    int configured;

    char *device;

    /* TCP-specific information. */
    const char *tcpdest;
    const char *tcpport;
    int do_telnet;

    int set_in_progress;
    solparm_t solparm;
    solparm_t solparm_rollback;
    void (*update_bitrate)(lmc_data_t *mc);

    int active;
    uint32_t session_id;

    /* A history buffer, hooking to instance 2 will dump it, if it's non-zero */
    unsigned int history_size;
    int history_active;
    uint32_t history_session_id;

    /* History is stored in this file is the program fails. */
    char *backupfile;

    int use_rtscts;
    int readclear;

    soldata_t *soldata;
} ipmi_sol_t;

void ipmi_sol_activate(lmc_data_t    *mc,
		       channel_t     *channel,
		       msg_t         *msg,
		       unsigned char *rdata,
		       unsigned int  *rdata_len);

void ipmi_sol_deactivate(lmc_data_t    *mc,
			 channel_t     *channel,
			 msg_t         *msg,
			 unsigned char *rdata,
			 unsigned int  *rdata_len);

#endif /* SOL_H */
