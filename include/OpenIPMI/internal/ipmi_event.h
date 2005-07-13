/*
 * ipmi_event.h
 *
 * Routines for dealing with events.
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002,2003 MontaVista Software Inc.
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

#ifndef __IPMI_EVENT_H
#define __IPMI_EVENT_H

#include <OpenIPMI/ipmi_types.h>

/* The event code here is considered internal to OpenIPMI, normal
   users shouldn't use it. */

/* Allocate an event with the given data. */
ipmi_event_t *ipmi_event_alloc(ipmi_mcid_t   mcid,
			       unsigned int  record_id,
			       unsigned int  type,
			       ipmi_time_t   timestamp,
			       unsigned char *data,
			       unsigned int  data_len);

/* The only part of an event that can be set is the mcid, because the
   lower layers may need to allocate an event without knowing what the
   MC is.  Normal users shouldn't do this, obviously, it should be a
   one-time thing done in the domain code. */
void ipmi_event_set_mcid(ipmi_event_t *event, ipmi_mcid_t mcid);

/* Get a pointer to the event's data.  Note that this pointer will be
   valid only as long as the event is valid. */
const unsigned char *ipmi_event_get_data_ptr(const ipmi_event_t *event);

/* Returns true if the event came in before the time we started up.
   false if not. */
int ipmi_event_is_old(const ipmi_event_t *event);
void ipmi_event_set_is_old(ipmi_event_t *event, int val);

#endif /* __IPMI_EVENT_H */
