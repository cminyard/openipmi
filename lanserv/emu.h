/*
 * emu.h
 *
 * MontaVista IPMI LAN server include file
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

#ifndef __EMU_IPMI_
#define __EMU_IPMI_

#include <sys/time.h>
#include <OpenIPMI/lanserv.h>
#include <OpenIPMI/mcserv.h>

void ipmi_emu_tick(emu_data_t *emu, unsigned int seconds);

typedef void (*ipmi_emu_sleep_cb)(emu_data_t *emu, struct timeval *time);

emu_data_t *ipmi_emu_alloc(void *user_data, ipmi_emu_sleep_cb sleeper,
			   sys_data_t *sysinfo);

void *ipmi_emu_get_user_data(emu_data_t *emu);

void ipmi_emu_sleep(emu_data_t *emu, struct timeval *time);

int ipmi_emu_handle_msg(emu_data_t    *emu,
			lmc_data_t    *srcmc,
			msg_t         *msg,
			unsigned char *rdata,
			unsigned int  *rdata_len);

#define IPMI_MC_DYNAMIC_SENSOR_POPULATION	(1 << 0)
#define IPMI_MC_PERSIST_SDR			(1 << 1)

int ipmi_emu_add_mc(emu_data_t    *emu,
		    unsigned char ipmb,
		    unsigned char device_id,
		    unsigned char has_device_sdrs,
		    unsigned char device_revision,
		    unsigned char major_fw_rev,
		    unsigned char minor_fw_rev,
		    unsigned char device_support,
		    unsigned char mfg_id[3],
		    unsigned char product_id[2],
		    unsigned int  flags);

lmc_data_t *ipmi_emu_get_bmc_mc(emu_data_t *emu);

int ipmi_emu_set_bmc_mc(emu_data_t *emu, unsigned char ipmb);

int ipmi_emu_get_mc_by_addr(emu_data_t    *emu,
			    unsigned char ipmb,
			    lmc_data_t    **mc);

/* ATCA support */
int ipmi_emu_atca_enable(emu_data_t *emu);
int ipmi_emu_atca_set_site(emu_data_t    *emu,
			   unsigned char hw_address,
			   unsigned char site_type,
			   unsigned char site_number);

int ipmi_emu_set_addr(emu_data_t *emu, unsigned int addr_num,
		      unsigned char addr_type,
		      void *addr_data, unsigned int addr_len);
int ipmi_emu_clear_addr(emu_data_t *emu, unsigned int addr_num);

/* In emu_cmd.c */
void ipmi_emu_shutdown(emu_data_t *emu);
int ipmi_emu_cmd(emu_out_t *out, emu_data_t *emu, char *cmd_str);
int read_command_file(emu_out_t *out, emu_data_t *emu,
		      const char *command_file);

void emu_set_debug_level(emu_data_t *emu, unsigned int debug_level);

int ipmi_emu_set_mc_guid(lmc_data_t *mc,
			 unsigned char guid[16],
			 int force);

void ipmi_mc_destroy(lmc_data_t *mc);

void ipmi_mc_disable(lmc_data_t *mc);
void ipmi_mc_enable(lmc_data_t *mc);

int ipmi_mc_enable_sel(lmc_data_t    *emu,
		       int           max_entries,
		       unsigned char flags);

int ipmi_mc_add_to_sel(lmc_data_t    *emu,
		       unsigned char record_type,
		       unsigned char event[13],
		       unsigned int  *recid);

int ipmi_mc_add_main_sdr(lmc_data_t    *mc,
			 unsigned char *data,
			 unsigned int  data_len);

int ipmi_mc_add_device_sdr(lmc_data_t    *mc,
			   unsigned char lun,
			   unsigned char *data,
			   unsigned int  data_len);

enum fru_io_cb_op { FRU_IO_READ, FRU_IO_WRITE };

typedef int (*fru_io_cb)(void *cb_data,
			 enum fru_io_cb_op op,
			 unsigned char *data,
			 unsigned int offset,
			 unsigned int length);

/*
 * Add a fru inventory device to the MC.  If fru_io_cb is NULL, the data
 * and length is the initial data for the FRU.  Otherwise, fru_io_cb is
 * called for reads and writes, and the data is the callback data for
 * fru_io_cb.
 */
int ipmi_mc_add_fru_data(lmc_data_t    *mc,
			 unsigned char device_id,
			 unsigned int  length,
			 fru_io_cb     fru_io_cb,
			 void          *data);

/*
 * Add a fru inventory device to the MC, mapping it to a file at the
 * given filename, starting in the file at the given offset.
 */
int ipmi_mc_add_fru_file(lmc_data_t    *mc,
			 unsigned char device_id,
			 unsigned int  length,
			 unsigned int  file_offset,
			 const char    *filename);

int ipmi_mc_get_fru_data_len(lmc_data_t    *mc,
			     unsigned char device_id,
			     unsigned int  *length);

int ipmi_mc_get_fru_data(lmc_data_t    *mc,
			 unsigned char device_id,
			 unsigned int  length,
			 unsigned char *data);

struct ipmi_sensor_handler_s
{
    char *name;
    int (*poll)(void *cb_data, unsigned int *val, const char **errstr);
    int (*init)(lmc_data_t *mc, unsigned char lun, unsigned char sensor_num,
		char **toks, void *cb_data, void **rcb_data,
		const char **errstr);
    int (*postinit)(void *cb_data, const char **errstr);
    void *cb_data;

    struct ipmi_sensor_handler_s *next;
};
typedef struct ipmi_sensor_handler_s ipmi_sensor_handler_t;

int ipmi_sensor_add_handler(ipmi_sensor_handler_t *handler);
ipmi_sensor_handler_t *ipmi_sensor_find_handler(const char *name);

int ipmi_mc_add_sensor(lmc_data_t    *mc,
		       unsigned char lun,
		       unsigned char sens_num,
		       unsigned char type,
		       unsigned char event_reading_code,
		       int           event_only);

int ipmi_mc_add_polled_sensor(lmc_data_t    *mc,
			      unsigned char lun,
			      unsigned char sens_num,
			      unsigned char type,
			      unsigned char event_reading_code,
			      unsigned int poll_rate,
			      int (*poll)(void *cb_data, unsigned int *val,
					  const char **errstr),
			      void *cb_data);

int ipmi_mc_set_power(lmc_data_t *mc, unsigned char power, int gen_int);

int ipmi_mc_set_num_leds(lmc_data_t   *mc,
			 unsigned int count);

#endif /* __EMU_IPMI_ */
