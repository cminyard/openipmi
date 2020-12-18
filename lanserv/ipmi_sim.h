/*
 * bmc.h
 *
 * MontaVista IPMI LAN server include file
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2020 MontaVista Software Inc.
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

#ifndef IPMI_SIM_H
#define IPMI_SIM_H

int is_mc_alloc_unconfigured(sys_data_t *sys, unsigned char ipmb,
			     lmc_data_t **rmc);
unsigned char is_mc_get_ipmb(lmc_data_t *mc);
channel_t **is_mc_get_channelset(lmc_data_t *mc);
ipmi_sol_t *is_mc_get_sol(lmc_data_t *mc);
startcmd_t *is_mc_get_startcmdinfo(lmc_data_t *mc);
user_t *is_mc_get_users(lmc_data_t *mc);
int is_mc_users_changed(lmc_data_t *mc);
pef_data_t *is_mc_get_pef(lmc_data_t *mc);
msg_t *is_mc_get_next_recv_q(channel_t *chan);
int is_sol_read_config(char **tokptr, sys_data_t *sys, const char **err);
void is_set_chassis_control_prog(lmc_data_t *mc, const char *prog);
void is_resend_atn(channel_t *chan);

typedef unsigned char *(*get_frudata_f)(lmc_data_t *mc, unsigned int *size);
typedef void (*free_frudata_f)(lmc_data_t *mc, unsigned char *data);
int ipmi_mc_set_frudata_handler(lmc_data_t *mc, unsigned int fru,
				get_frudata_f handler, free_frudata_f freefunc);

#endif /* IPMI_SIM_H */
