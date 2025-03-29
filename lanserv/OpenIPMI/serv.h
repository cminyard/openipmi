/* This file is now pulled into lanserv. */
#include <OpenIPMI/lanserv.h>

int check_msg_length(msg_t         *msg,
		     unsigned int  len,
		     unsigned char *rdata,
		     unsigned int  *rdata_len);

sys_data_t *ipmi_emu_get_sysconfig(emu_data_t *emu);
