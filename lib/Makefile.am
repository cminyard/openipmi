
LIB_VERSION = 0.0.5
LD_VERSION = 0:5:0

AM_CFLAGS = -Wall -Wsign-compare -I$(top_builddir)/include \
	-I$(top_srcdir)/include -DIPMI_CHECK_LOCKS $(OPENSSLINCS) \
	-DBUILDING_IPMI_DLL

lib_LTLIBRARIES = libOpenIPMI.la

noinst_HEADERS = manfid.h

libOpenIPMI_la_SOURCES = entity.c ipmi.c domain.c mc.c sdr.c \
	control.c ipmi_utils.c conn.c fru.c chassis.c pet.c event.c \
	opq.c sel.c sensor.c pef.c lanparm.c strings.c normal_fru.c \
	oem_force_conn.c oem_motorola_mxp.c oem_atca_conn.c oem_atca.c \
	ipmi_lan.c oem_test.c oem_intel.c ipmi_payload.c rakp.c aes_cbc.c \
	hmac.c md5.c ipmi_smi.c ipmi_sol.c oem_kontron_conn.c \
	oem_atca_fru.c fru_spd_decode.c solparm.c
libOpenIPMI_la_LIBADD = -lm $(top_builddir)/utils/libOpenIPMIutils.la \
	$(OPENSSLLIBS) $(SOCKETLIB)
libOpenIPMI_la_LDFLAGS = -rdynamic -version-info $(LD_VERSION) \
			 -no-undefined
