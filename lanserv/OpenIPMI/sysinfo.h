/*
 * sysinfo.h
 *
 * MontaVista IPMI lanserv sysinfo include file
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2024 MontaVista Software Inc.
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
` *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
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

#ifndef __SYSINFO_H
#define __SYSINFO_H

#include <sys/socket.h>
#include <netinet/in.h>

struct msg_s;
struct ipmi_timer_s;
struct ipmi_io_s;
struct channel_s;
struct ipmi_tick_handler_s;

/*
 * Note that we keep odd addresses, too.  In some cases that's useful
 * in virtual systems that don't have I2C restrictions.
 */
#define IPMI_MAX_MCS 256

typedef struct sockaddr_ip_s {
    union
        {
	    struct sockaddr s_addr0;
            struct sockaddr_in  s_addr4;
#ifdef PF_INET6
            struct sockaddr_in6 s_addr6;
#endif
        } s_ipsock;
/*    socklen_t addr_len;*/
} sockaddr_ip_t;

/*
 * Generic data about the system that is global for the whole system and
 * required for all server types.
 */
struct sys_data_s {
    char *name;

    /* The MCs in the system */
    struct lmc_data_s *ipmb_addrs[IPMI_MAX_MCS];

#define DEBUG_RAW_MSG	(1 << 0)
#define DEBUG_MSG	(1 << 1)
#define DEBUG_SOL	(1 << 2)
    unsigned int debug;

#define NEW_SESSION			1
#define NEW_SESSION_FAILED		2
#define SESSION_CLOSED			3
#define SESSION_CHALLENGE		4
#define SESSION_CHALLENGE_FAILED	5
#define AUTH_FAILED			6
#define INVALID_MSG			7
#define OS_ERROR			8
#define LAN_ERR				9
#define INFO				10
#define DEBUG				11
#define SETUP_ERROR			12
    void (*log)(struct sys_data_s *sys, int type, struct msg_s *msg,
		const char *format, ...)
	__attribute__ ((__format__ (__printf__, 4, 5)));

    /* Console port.  Length is zero if not set. */
    sockaddr_ip_t console_addr;
    socklen_t console_addr_len;
    int console_fd;

    unsigned char bmc_ipmb;
    int sol_present;

    void *info;

    /*
     * When reading in config, this tracks which information we are
     * working on.  This is initialized to the MC at 0x20, setting
     * the working MC changes these to the new MC.
     */
    struct channel_s **chan_set;
    struct startcmd_s *startcmd;
    struct user_s *cusers;
    struct pef_data_s *cpef;
    struct ipmi_sol_s *sol;
    struct lmc_data_s *mc;

    void *(*alloc)(struct sys_data_s *sys, int size);
    void (*free)(struct sys_data_s *sys, void *data);

    int (*get_monotonic_time)(struct sys_data_s *sys, struct timeval *tv);
    int (*get_real_time)(struct sys_data_s *sys, struct timeval *tv);

    int (*alloc_timer)(struct sys_data_s *sys, void (*cb)(void *cb_data),
		       void *cb_data, struct ipmi_timer_s **timer);
    int (*start_timer)(struct ipmi_timer_s *timer, struct timeval *timeout);
    int (*stop_timer)(struct ipmi_timer_s *timer);
    void (*free_timer)(struct ipmi_timer_s *timer);

    int (*add_io_hnd)(struct sys_data_s *sys, int fd,
		      void (*read_hnd)(int fd, void *cb_data),
		      void *cb_data, struct ipmi_io_s **io);
    void (*remove_io_hnd)(struct ipmi_io_s *io);
    void (*io_set_hnds)(struct ipmi_io_s *io,
			void (*write_hnd)(int fd, void *cb_data),
			void (*except_hnd)(int fd, void *cb_data));
    void (*io_set_enables)(struct ipmi_io_s *io, int read, int write,
			   int except);

    int (*gen_rand)(struct sys_data_s *sys, void *data, int len);

    /* Called by interface code to report that the target did a reset. */
    /* FIXME - move */
    void (*target_reset)(struct sys_data_s *sys);

    /*
     * These are a hack so the channel code in the MCs can pick up
     * these functions.
     */
    int (*csmi_send)(struct channel_s *chan, struct msg_s *msg);

    int (*lan_channel_init)(void *info, struct channel_s *chan);
    int (*ser_channel_init)(void *info, struct channel_s *chan);
    int (*ipmb_channel_init)(void *info, struct channel_s *chan);

    /*
     * Various MC related info that must be provided.
     */
    int (*mc_alloc_unconfigured)(struct sys_data_s *sys, unsigned char ipmb,
				 struct lmc_data_s **rmc);
    void (*resend_atn)(struct channel_s *chan);
    unsigned char (*mc_get_ipmb)(struct lmc_data_s *mc);
    struct channel_s **(*mc_get_channelset)(struct lmc_data_s *mc);
    struct ipmi_sol_s *(*mc_get_sol)(struct lmc_data_s *mc);
    struct startcmd_s *(*mc_get_startcmdinfo)(struct lmc_data_s *mc);
    struct user_s *(*mc_get_users)(struct lmc_data_s *mc);
    int (*mc_users_changed)(struct lmc_data_s *mc);
    struct pef_data_s *(*mc_get_pef)(struct lmc_data_s *mc);
    int (*sol_read_config)(char **tokptr, struct sys_data_s *sys,
			   const char **err);
    void (*set_chassis_control_prog)(struct lmc_data_s *mc, const char *prog);

    void (*register_tick_handler)(struct ipmi_tick_handler_s *handler);
};

char *sys_strdup(struct sys_data_s *sys, const char *s);

#endif /* __SYSINFO_H */
