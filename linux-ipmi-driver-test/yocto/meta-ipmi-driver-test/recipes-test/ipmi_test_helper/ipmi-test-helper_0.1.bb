LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE.apache;md5=3b83ef96387f14655fc854ddc3c6bd57"

# No information for SRC_URI yet (only an external source tree was specified)
SRC_URI = "file://ipmi_test_helper.c \
           file://LICENSE.apache \
           file://init"

inherit update-rc.d

INITSCRIPT_PACKAGES = "${PN}"
INITSCRIPT_NAME:${PN} = "ipmi_test_helper.sh"
INITSCRIPT_PARAMS:${PN} = "defaults 50"

DEPENDS = "gensio"

do_installcode () {
        cp ${WORKDIR}/ipmi_test_helper.c ${B}
        cp ${WORKDIR}/LICENSE.apache ${B}
}
addtask installcode after do_unpack before do_compile do_populate_lic

do_configure () {
	:
}

do_compile () {
	${CC} -o ipmi_test_helper -g -Wall ipmi_test_helper.c -lgensio -lgensioosh ${LDFLAGS}
}

do_install () {
        install -d ${D}${bindir}
        install -m 0755 ${B}/ipmi_test_helper ${D}/${bindir}
        install -d ${D}${sysconfdir}/init.d
        install -m 0755 ${WORKDIR}/init ${D}${sysconfdir}/init.d/ipmi_test_helper.sh
}
