/* ipmi_utils.h - Misc utils for IPMI
 * Copyright (C) 2004 MontaVista Software.
 * Corey Minyard <cminyard@mvista.com>
 *
 * This file is part of the IPMI Interface (IPMIIF).
 *
 * IPMIIF is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * IPMIIF is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 */

#ifndef OPENIPMI_UTILS_H_
#define OPENIPMI_UTILS_H_

#include <OpenIPMI/dllvisibility.h>
#include <OpenIPMI/ipmi_types.h>
#include <OpenIPMI/ipmi_bits.h>

/* Do a hash on a pointer value. */
IPMI_UTILS_DLL_PUBLIC
unsigned int ipmi_hash_pointer(void *);

typedef void (*ipmi_ifru_cb)(ipmi_domain_t *domain, ipmi_fru_t *fru,
			     int err, void *cb_data);
/* Allocate a FRU, but don't make it visible to the list of FRUs. */
#define IPMI_FRU_FTR_INTERNAL_USE_AREA_MASK 1
#define IPMI_FRU_FTR_CHASSIS_INFO_AREA_MASK 2
#define IPMI_FRU_FTR_BOARD_INFO_AREA_MASK   4
#define IPMI_FRU_FTR_PRODUCT_INFO_AREA_MASK 8
#define IPMI_FRU_FTR_MULTI_RECORD_AREA_MASK 16
#define IPMI_FRU_ALL_AREA_MASK 0x1f
IPMI_DLL_PUBLIC
int ipmi_fru_alloc_notrack(ipmi_domain_t *domain,
			   unsigned char is_logical,
			   unsigned char device_address,
			   unsigned char device_id,
			   unsigned char lun,
			   unsigned char private_bus,
			   unsigned char channel,
			   unsigned char fetch_mask,
			   ipmi_ifru_cb  fetched_handler,
			   void          *fetched_cb_data,
			   ipmi_fru_t    **new_fru);

/*
 * Used for destroying notrack FRUs only.
 */
typedef void (*ipmi_fru_idestroyed_cb)(ipmi_fru_t *fru, void *cb_data);
IPMI_DLL_PUBLIC
int ipmi_fru_destroy_internal(ipmi_fru_t            *fru,
			      ipmi_fru_idestroyed_cb handler,
			      void                  *cb_data);

/*
 * Used to append an IPMI string to a character string, converting it to
 * something printable in a normal string.  Unicode and binary strings
 * are currently converted into a hex string.
 */
unsigned int
ipmi_string_append(char *str, unsigned int len,
		   char *to_append, unsigned int append_len,
		   enum ipmi_str_type_e append_type);

#endif /* OPENIPMI_UTILS_H_ */
