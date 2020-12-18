/*
 * mcserv.h
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

#ifndef __MCSERV_H
#define __MCSERV_H

#include <OpenIPMI/lanserv.h>
typedef struct sensor_s sensor_t;

#define MC	1
#define NOMC	0
typedef int (*ipmi_emu_cmd_handler)(emu_out_t  *out,
				    emu_data_t *emu,
				    lmc_data_t *mc,
				    char       **toks);
int ipmi_emu_add_cmd(const char *name, unsigned int flags,
		     ipmi_emu_cmd_handler handler);


#define CHASSIS_CONTROL_POWER 0
#define CHASSIS_CONTROL_RESET 1
#define CHASSIS_CONTROL_BOOT  2
#define CHASSIS_CONTROL_BOOT_INFO_ACK  3
#define CHASSIS_CONTROL_GRACEFUL_SHUTDOWN  4
#define CHASSIS_CONTROL_IDENTIFY 5
void ipmi_mc_set_chassis_control_func(lmc_data_t *mc,
				      int (*set)(lmc_data_t *mc, int op,
						 unsigned char *val,
						 void *cb_data),
				      int (*get)(lmc_data_t *mc, int op,
						 unsigned char *val,
						 void *cb_data),
				      void *cb_data);

/*
 * FRUs have a semaphore that can be use to grant exclusive access.
 * The semaphore is attempted to get before read and write operations,
 * if it fails then an error is returned.  If something else reads or
 * writes the FRU, then it should claim the semaphore before posting.
 */
int ipmi_mc_fru_sem_wait(lmc_data_t *mc, unsigned char device_id);
int ipmi_mc_fru_sem_trywait(lmc_data_t *mc, unsigned char device_id);
int ipmi_mc_fru_sem_post(lmc_data_t *mc, unsigned char device_id);

int ipmi_mc_sensor_set_enabled(lmc_data_t    *mc,
			       unsigned char lun,
			       unsigned char sens_num,
			       unsigned char enabled);

int ipmi_mc_sensor_set_bit(lmc_data_t   *mc,
			   unsigned char lun,
			   unsigned char sens_num,
			   unsigned char bit,
			   unsigned char value,
			   int           gen_event);

int ipmi_mc_sensor_set_bit_clr_rest(lmc_data_t   *mc,
				    unsigned char lun,
				    unsigned char sens_num,
				    unsigned char bit,
				    int           gen_event);

int ipmi_mc_sensor_set_value(lmc_data_t    *mc,
			     unsigned char lun,
			     unsigned char sens_num,
			     unsigned char value,
			     int           gen_event);

int ipmi_mc_sensor_set_hysteresis(lmc_data_t    *mc,
				  unsigned char lun,
				  unsigned char sens_num,
				  unsigned char support,
				  unsigned char positive,
				  unsigned char negative);

int ipmi_mc_sensor_set_threshold(lmc_data_t    *mc,
				 unsigned char lun,
				 unsigned char sens_num,
				 unsigned char support,
				 uint16_t supported,
				 int set_values,
				 unsigned char values[6]);

int ipmi_mc_sensor_add_rearm_handler(lmc_data_t    *mc,
				     unsigned char lun,
				     unsigned char sens_num,
				     int (*handler)(void *cb_data,
						    uint16_t assert,
						    uint16_t deassert),
				     void *cb_data);

int ipmi_mc_sensor_set_event_support(lmc_data_t    *mc,
				     unsigned char lun,
				     unsigned char sens_num,
				     unsigned char init_events,
				     unsigned char events_enable,
				     unsigned char init_scanning,
				     unsigned char scanning_enable,
				     unsigned char support,
				     uint16_t assert_supported,
				     uint16_t deassert_supported,
				     uint16_t assert_enabled,
				     uint16_t deassert_enabled);

/*
 * Message handling.
 */

void handle_invalid_cmd(lmc_data_t    *mc,
			unsigned char *rdata,
			unsigned int  *rdata_len);
int check_msg_length(msg_t         *msg,
		     unsigned int  len,
		     unsigned char *rdata,
		     unsigned int  *rdata_len);

void ipmi_mc_set_dev_revision(lmc_data_t *mc, unsigned char dev_revision);
void ipmi_mc_set_fw_revision(lmc_data_t *mc, unsigned char fw_revision_major,
			     unsigned char fw_revision_minor);
void ipmi_mc_set_aux_fw_revision(lmc_data_t *mc,
				 unsigned char aux_fw_revision[4]);
const char *get_lanserv_version(void);

/*
 * Types and functions for registering handlers with the MC emulator.
 */
typedef void (*cmd_handler_f)(lmc_data_t    *mc,
			      msg_t         *msg,
			      unsigned char *rdata,
			      unsigned int  *rdata_len,
			      void          *cb_data);
int ipmi_emu_register_cmd_handler(unsigned char netfn, unsigned char cmd,
				  cmd_handler_f handler, void *cb_data);

/*
 * Note that for IANA command handlers the IANA is stripped (and put into
 * msg->iana) before being passed to the handler, and inserted into the
 * response message automatically.  So the handler should handle this
 * like a normal message, setting the data and length as if the IANA was
 * not there.  This way standard handling functions will work properly,
 * and it simplifies the handling of IANA messages.
 */
int ipmi_emu_register_iana_handler(uint32_t iana, cmd_handler_f handler,
				   void *cb_data);
int ipmi_emu_register_oi_iana_handler(uint8_t cmd, cmd_handler_f handler,
				      void *cb_data);

#define OPENIPMI_IANA_CMD_SET_HISTORY_RETURN_SIZE	1
#define OPENIPMI_IANA_CMD_GET_HISTORY_RETURN_SIZE	2

/*
 * Registration for group extensions
 */
void ipmi_emu_register_group_extension_handler(uint8_t group_extension,
					       cmd_handler_f handler,
					       void *cb_data);

void mc_new_event(lmc_data_t *mc,
		  unsigned char record_type,
		  unsigned char event[13]);

#endif /* __MCSERV_H */
