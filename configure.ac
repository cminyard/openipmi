OPENIPMI_PKG_NAME=OpenIPMI
AC_INIT([OpenIPMI],[2.0.37],[minyard@acm.org],[OpenIPMI])
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE([-Wall])
AC_CONFIG_HEADERS([config.h])
AM_PROG_AR

AC_CONFIG_MACRO_DIR([m4])

AC_SUBST(OPENIPMI_VERSION_MAJOR, 2)
AC_SUBST(OPENIPMI_VERSION_MINOR, 0)
AC_SUBST(OPENIPMI_VERSION_RELEASE, 37)
AC_SUBST(OPENIPMI_VERSION_EXTRA, )


OPENIPMI_SMI=false

case $target_os in
	linux*)
		OPENIPMI_SMI=true
		AC_DEFINE([HAVE_OPENIPMI_SMI], 1,
			[Have Linux local interface])
		;;

	cygwin*)
		AC_DEFINE([HAVE_CYGWIN_TARGET], 1, [Running on CYGWIN])
		;;

	solaris*)
		# Some solaris systems have epoll, but it's too broken to use.
		ax_cv_have_epoll_pwait=false
		;;

	mingw*)
		LIBS="$LIBS -lws2_32 -liphlpapi -lgdi32 -lbcrypt"
		;;
esac

AM_CONDITIONAL([HAVE_OPENIPMI_SMI], [${OPENIPMI_SMI}])

SNMPLIBS=

# Where do we find the NETSNMP includes and libs.  We keep the old udc
# name for backwards compatibility.
trynetsnmp=yes
AC_ARG_WITH(ucdsnmp,
[  --with-ucdsnmp=PATH             Look for netsnmp in PATH.],
    if test "x$withval" = "xyes"; then
      trynetsnmp=yes
    elif test "x$withval" = "xno"; then
      trynetsnmp=no
    elif test -d "$withval"; then
      CPPFLAGS="-I$withval/include $CPPFLAGS"
      SNMPLIBS="-L$withval/lib $SNMPLIBS"
      trynetsnmp=yes
    fi,
)
AC_ARG_WITH(netsnmp,
[  --with-netsnmp=PATH             Look for netsnmp in PATH.],
    if test "x$withval" = "xyes"; then
      trynetsnmp=yes
    elif test "x$withval" = "xno"; then
      trynetsnmp=no
    elif test -d "$withval"; then
      CPPFLAGS="-I$withval/include $CPPFLAGS"
      SNMPLIBS="-L$withval/lib $SNMPLIBS"
      trynetsnmp=yes
    fi,
)

AC_ARG_WITH(snmpflags,
[  --with-snmpflags=flags          Set the flags to compile with SNMP.],
    CPPFLAGS="$withval $CPPFLAGS"
)

AC_ARG_WITH(snmplibs,
[  --with-snmplibs=libs            Set the libraries to link with SNMP.],
    SNMPLIBS="$withval"
)

POPTCFLAGS=
AC_ARG_WITH(poptflags,
[  --with-poptflags=flags          Set the flags to compile with popt.],
    CPPFLAGS="$withval $CPPFLAGS"
)

POPTCFLAGS=
AC_ARG_WITH(poptlibs,
[  --with-poptlibs=libs            Set the libraries to link with popt.],
    POPTLIBS="$withval"
)

# If NETSNMP requires OpenSSL, this tells where to find the crypto lib
tryopenssl=yes
AC_ARG_WITH(openssl,
[  --with-openssl[[=yes|no|PATH]]    Look for openssl, with the optional path.],
    if test "x$withval" = "xyes"; then
      tryopenssl=yes
    elif test "x$withval" = "xno"; then
      tryopenssl=no
    elif test -d "$withval"; then
      SNMPLIBS="-L$withval/lib $SNMPLIBS"
      OPENSSLLIBS="-L$withval/lib"
      OPENSSLINCS="-I$withval/include"
      tryopenssl=yes
    fi,
)

AC_ARG_WITH(opensslflags,
[  --with-opensslflags=flags       Set the flags to compile with OpenSSL.],
    OPENSSLINCS="$withval"
)

AC_ARG_WITH(openssllibs,
[  --with-openssllibs=libs         Set the libraries to link with OpenSSL.],
    OPENSSLLIBS="$withval",
)

tryglib=yes
AC_ARG_WITH(glib,
[  --with-glib=yes|no              Look for glib.],
    if test "x$withval" = "xyes"; then
      tryglib=yes
    elif test "x$withval" = "xno"; then
      tryglib=no
    fi,
)

glibver=
AC_ARG_WITH(glibver,
[  --with-glibver=ver              Set the glib version, either 1.2 or 2.0.],
    glibver="$withval",
)

glibcflags=
AC_ARG_WITH(glibcflags,
[  --with-glibcflags=flags         Set the flags to compile with glib.],
    glibcflags="$withval",
)

gliblibs=
AC_ARG_WITH(gliblibs,
[  --with-gliblibs=libs            Set the libraries to link with glib.],
    gliblibs="$withval",
)

trytcl=yes
AC_ARG_WITH(tcl,
[  --with-tcl=yes|no               Look for tcl.],
    if test "x$withval" = "xyes"; then
      trytcl=yes
    elif test "x$withval" = "xno"; then
      trytcl=no
    fi,
)

tclcflags=
AC_ARG_WITH(tclcflags,
[  --with-tclcflags=flags          Set the flags to compile with tcl.],
    tclcflags="$withval",
)

tcllibs=
AC_ARG_WITH(tcllibs,
[  --with-tcllibs=libs             Set the libraries to link with tcl.],
    tcllibs="$withval",
)

tryperl=yes
perldir=
AC_ARG_WITH(perl,
[  --with-perl[[=yes|no|PATH]]       Look for perl, with the optional path.],
    if test "x$withval" = "xyes"; then
      tryperl=yes
    elif test "x$withval" = "xno"; then
      tryperl=no
    elif test -d "$withval"; then
      perldir="$withval"
      tryperl=yes
    fi,
)

perlinstalldir=
AC_ARG_WITH(perlinstall,
[  --with-perlinstall=PATH         Install perl modules in the given location.],
    perlinstalldir="$withval",
)

perlcflags=
AC_ARG_WITH(perlcflags,
[  --with-perlcflags=PATH          Use the given flags when compiling perl parts.],
    perlcflags="$withval",
)

perlusepthreads=
AC_ARG_WITH(perlusepthreads,
[  --with-perlusepthreads[[=yes|no]] Use threads with perl.],
   if test "x$withval" = "xyes"; then
      perlusepthreads="yes"
   elif test "x$withval" = "xno"; then
      perlusepthreads="no"
   elif test "x$withval" = "x"; then
      perlusepthreads="yes"
   fi,
)

pythonprog=
trypython=yes
AC_ARG_WITH(python,
[  --with-python[[=yes|no|PATH]]     Look for python, with the optional path.],
    if test "x$withval" = "x"; then
      trypython=yes
    elif test "x$withval" = "xyes"; then
      trypython=yes
    elif test "x$withval" = "xno"; then
      trypython=no
    else
      pythonprog="$withval"
      trypython=yes
    fi,
)

pythoninstalldir=
AC_ARG_WITH(pythoninstall,
[  --with-pythoninstall=PATH       Install python modules in the given location.],
    pythoninstalldir="$withval",
)

pythoninstalllibdir=
AC_ARG_WITH(pythoninstalllib,
[  --with-pythoninstalllib=PATH       Install python libraries in the given location.],
    pythoninstalllibdir="$withval",
)

pythoncflags=
AC_ARG_WITH(pythoncflags,
[  --with-pythoncflags=PATH        Use the given flags when compiling python parts.],
    pythoncflags="$withval",
)

pythonusepthreads=
AC_ARG_WITH(pythonusepthreads,
[  --with-pythonusepthreads[[=yes|no]] Use threads with python.],
   if test "x$withval" = "xyes"; then
      pythonusepthreads="yes"
   elif test "x$withval" = "xno"; then
      pythonusepthreads="no"
   elif test "x$withval" = "x"; then
      pythonusepthreads="yes"
   fi,
)

tkinter=
AC_ARG_WITH(tkinter,
[  --with-tkinter[[=yes|no]]       GUI package for python.],
   if test "x$withval" = "xyes"; then
      tkinter="yes"
   elif test "x$withval" = "xno"; then
      tkinter="no"
   elif test "x$withval" = "x"; then
      tkinter="yes"
   fi,
)

tryswig=yes
swigprog=
AC_ARG_WITH(swig,
[  --with-swig[[=yes|no|PATH]]       Look for swig, with the optional path.],
    if test "x$withval" = "x"; then
      tryswig=yes
    elif test "x$withval" = "xyes"; then
      tryswig=yes
    elif test "x$withval" = "xno"; then
      tryswig=no
    else
      swigprog=$withval
    fi,
)

MARVELL_BMC_DIR=
AC_ARG_WITH(marvell-bmc,
[  --with-marvell-bmc            Enable the Marvell BMC.],
    MARVELL_BMC_DIR=marvell-bmc
)
AC_SUBST([MARVELL_BMC_DIR])

AM_PROG_CC_C_O
LT_INIT

AC_CHECK_HEADERS(execinfo.h)
AC_CHECK_HEADERS([netinet/ether.h])
AC_CHECK_HEADERS([sys/ethernet.h])

# Check whether we need -lrt added.
AC_CHECK_LIB(c, clock_gettime, RT_LIB=, RT_LIB=-lrt)
AC_SUBST(RT_LIB)

AC_SUBST(POPTLIBS)

FOUND_POPT_HEADER=no
AC_CHECK_HEADER(popt.h, FOUND_POPT_HEADER=yes; )
if test "x$FOUND_POPT_HEADER" != "xyes"; then
   echo "Could not find headers for the popt library"
   exit 1
fi

if test "x$POPTLIBS" = "x"; then
   AC_CHECK_LIB(popt, poptGetContext, POPTLIBS=-lpopt)
   if test "x$POPTLIBS" = ""; then
      echo "Could not find the popt library"
      exit 1
   fi
fi

# Check for GDBM
have_gdbm_h="no"
GDBM_LIB=
AC_CHECK_HEADER(gdbm.h, have_gdbm_h="yes"; )
if test "x$have_gdbm_h" = "xyes"; then
   AC_CHECK_LIB(gdbm, gdbm_open,
		AC_DEFINE([HAVE_GDBM], [], [Have GDBM libraries])
		GDBM_LIB=-lgdbm; )
fi
AC_SUBST(GDBM_LIB)

# Find pkg-config
pkgprog=
AC_PATH_PROG(pkgprog, pkg-config)

# Handle GLIB support
haveglib=no
if test "x$glibver" = "x" -o "x$glibcflags" = "x" -o "x$gliblibs" = "x"; then
   glibprog=
   if test "x$tryglib" != "xno"; then
      if test "x$pkgprog" != "x"; then
         glibprog=$pkgprog
      fi
   fi
   GLIB_CFLAGS=
   GLIB_LIBS=
   if test "x$glibprog" != "x"; then
      GLIB_CFLAGS=`$glibprog --cflags gthread-2.0 2>/dev/null`
      if test $? = 0; then
         haveglib=yes
         GLIB_VERSION='2.0'
         GLIB_LIBS=`$glibprog --libs gthread-2.0 2>/dev/null`
      fi
   fi
else
   haveglib=yes
   GLIB_CFLAGS="$glibcflags"
   GLIB_VERSION="$glibver"
   GLIB_LIBS="$gliblibs"
fi
echo "checking for glib... $haveglib"

AM_CONDITIONAL([HAVE_GLIB], [test "x$haveglib" = "xyes"])
if test "x$haveglib" = "xyes"; then
   AC_DEFINE([HAVE_GLIB], [], [Have GLIB libraries])
   GLIB_LIB='$(top_builddir)/glib/libOpenIPMIglib.la'
   GLIB_SO='$(top_builddir)/glib/.libs/libOpenIPMIglib.so'
   GLIB_PKGCONF=OpenIPMIglib.pc
   GLIB_TARGET=libOpenIPMIglib.la
   GLIB_DIR=glib
else
   GLIB_LIB=
   GLIB_SO=
   GLIB_PKGCONF=
   GLIB_TARGET=
   GLIB_DIR=
fi
AC_SUBST(GLIB_VERSION)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB_LIB)
AC_SUBST(GLIB_SO)
AC_SUBST(GLIB_PKGCONF)
AC_SUBST(GLIB_TARGET)
AC_SUBST(GLIB_DIR)

# Handle TCL support
TCL_LIBS=
TCL_CFLAGS=
havetcl=no
if test "x$trytcl" != "xno"; then
   FOUND_TCL_HEADER=no
   ver=`echo 'puts \$tcl_version' | tclsh`
   if test "x$tclcflags" = "x"; then
      AC_CHECK_HEADER(tcl/tcl.h, FOUND_TCL_HEADER=yes; )
      if test "x$FOUND_TCL_HEADER" != "xyes"; then
         AC_CHECK_HEADER(tcl/tcl.h, FOUND_TCL_HEADER=yes; )
	 if test "x$FOUND_TCL_HEADER" == "xyes"; then
	    tclcflags="-I /usr/include/tcl$ver"
            TCL_CFLAGS="$tclcflags"
	 fi
      else
	tclcflags="-I /usr/include/tcl"
        TCL_CFLAGS="$tclcflags"
      fi
   else
      TCL_CFLAGS="$tclcflags"
      FOUND_TCL_HEADER=yes
   fi
   if test "x$tcllibs" = "x"; then
      if test "x$FOUND_TCL_HEADER" = "xyes"; then
         AC_CHECK_LIB(tcl, Tcl_DoOneEvent, TCL_LIBS=-ltcl)
         if test "x$TCL_LIBS" = "x"; then
            AC_CHECK_LIB(tcl$ver, Tcl_DoOneEvent, TCL_LIBS=-ltcl$ver)
	 fi
      fi
   else
      TCL_LIBS="$tcllibs"
   fi
   if test "x$FOUND_TCL_HEADER" = "xyes" -a "x$TCL_LIBS" != "x"; then
      havetcl=yes
   fi
fi

echo "checking for tcl... $havetcl"
   
if test "x$havetcl" = "xyes"; then
   OLDLIBS="$LIBS"
   LIBS="$LIBS $TCL_LIBS"
   AC_LINK_IFELSE([AC_LANG_CALL(, Tcl_GetTime)], [echo "Have Tcl_GetTime"],
		  [echo "Redefine Tcl_GetTime as TclpGetTime";
		   AC_DEFINE([Tcl_GetTime], [TclpGetTime],
                            [If no Tcl_GetTime, redefine as TclpGetTime])])
   LIBS="$OLDLIBS"
fi

if test "x$havetcl" = "xyes"; then
   AC_DEFINE([HAVE_TCL], [], [Have TCL libraries])
   TCL_LIB='$(top_builddir)/tcl/libOpenIPMItcl.la'
   TCL_SO='$(top_builddir)/tcl/.libs/libOpenIPMItcl.so'
   TCL_PKGCONF=OpenIPMItcl.pc
   TCL_TARGET=libOpenIPMItcl.la
   TCL_DIR=tcl
else
   TCL_LIB=
   TCL_SO=
   TCL_PKGCONF=
   TCL_TARGET=
   TCL_DIR=
fi
AC_SUBST(TCL_CFLAGS)
AC_SUBST(TCL_LIBS)

AC_SUBST(TCL_LIB)
AC_SUBST(TCL_SO)
AC_SUBST(TCL_PKGCONF)
AC_SUBST(TCL_TARGET)
AC_SUBST(TCL_DIR)

# Handle PERL support
if test "x$perlcflags" = "x" -o "x$perlinstalldir" = "x"; then
   perlprog=
   if test "x$tryperl" != "xno"; then
      AC_PATH_PROG(perlprog, perl)
   fi
   if test "x$perlprog" != "x"; then
      # Find the place where perl lives.
      if test "x$perldir" = "x"; then
	 perldir=`$perlprog -e 'for $i (@INC) { if (-r "$i/CORE/perl.h") { print "$i"; last; } }'`
      fi

      # Now find a proper installation location.
      if test "x$perlinstalldir" = "x"; then
	 perlinstalldir=`(eval \`perl -V:installvendorarch\`; echo $installvendorarch)`
	 if test "x$perlinstalldir" = "x" -o ! -d "$perlinstalldir"; then
	    perlinstalldir=`$perlprog -e 'for $i (@INC) { if ($i =~ /site_perl\/.+\/.+/) { print "$i"; last; } }'`
	 fi
	 if test "x$perlinstalldir" = "x" -o ! -d "$perlinstalldir"; then
	    perlinstalldir=`$perlprog -e 'for $i (@INC) { if ($i =~ /vendor_perl\/.+\/.+/) { print "$i"; last; } }'`
	 fi
	 if test "x$perlinstalldir" = "x" -o ! -d "$perlinstalldir"; then
	    perlinstalldir=$perldir
	 fi
      fi
   fi

   if test "x$perldir" != "x"; then
      tpprog=`$perlprog -e "\\$p = \"$perlinstalldir\"; \\$u = \"$prefix\"; \\$p =~ s/\\$u//; print \\$p"`
      AC_DEFINE([HAVE_PERL], [], [Have perl installed])
      PERL_DIR=perl
      if test "x$perlcflags" = "x"; then
         PERL_CFLAGS="-I $perldir/CORE `$perlprog -V:ccflags | grep ccflags | sed 's/^.*ccflags=.\(.*\).;$/\1/'`"
      else
	  PERL_CFLAGS="$perlcflags"
      fi
      if test "$tpprog" = "$perlinstalldir"; then
	 PERL_INSTALL_DIR="$perlinstalldir"
      else
	 PERL_INSTALL_DIR="\${prefix}$tpprog"
      fi

   else
      PERL_DIR=
      PERL_CFLAGS=
      PERL_INSTALL_DIR=
      PERL_HAS_POSIX_THREADS=
      PERL_POSIX_LIB=
      PERL_POSIX_SO=
   fi
else
   AC_DEFINE([HAVE_PERL], [], [Have perl installed])
   PERL_DIR=perl
   PERL_CFLAGS="$perlcflags"
   PERL_INSTALL_DIR="$perlinstalldir"
fi
if test "x$perlprog" != "x"; then
   if test "x$perlusepthreads" = "x"; then
      if $perlprog -V:usethreads | grep -q 'define'; then
         perlusepthreads="yes"
      fi
   fi
   echo "checking for perl threads... $perlusepthreads"
fi
if test "x$perlusepthreads" = "xyes"; then
   PERL_HAS_POSIX_THREADS=1
   PERL_POSIX_LIB='$(top_builddir)/unix/libOpenIPMIpthread.la'
   PERL_POSIX_SO='$(top_builddir)/unix/.libs/libOpenIPMIpthread.so'
else
   PERL_HAS_POSIX_THREADS=0
   PERL_POSIX_LIB='$(top_builddir)/unix/libOpenIPMIposix.la'
   PERL_POSIX_SO='$(top_builddir)/unix/.libs/libOpenIPMIposix.so'
fi
AC_SUBST(PERL_DIR)
AC_SUBST(PERL_CFLAGS)
AC_SUBST(PERL_INSTALL_DIR)
AC_SUBST(PERL_HAS_POSIX_THREADS)
AC_SUBST(PERL_POSIX_LIB)
AC_SUBST(PERL_POSIX_SO)

# Handle PYTHON support
PYTHON_DIR=
PYTHON_INSTALL_DIR=
PYTHON_INSTALL_LIB_DIR=
PYTHON_SWIG_FLAGS=
if test "x$trypython" = "xyes"; then
   if test "x$pythonprog" != "x"; then
      PYTHON="$pythonprog"
   fi
   if test "x$pythoncflags" != "x"; then
      PYTHON_CPPFLAGS="$pythoncflags"
   fi
   AX_PYTHON_DEVEL
fi
if test "x$PYTHON" != "x"; then
   AX_PROG_PYTHON_VERSION([3.0.0], [PYTHON_SWIG_FLAGS=-py3], [])
   AC_DEFINE([HAVE_PYTHON], [], [Have Python installed])
   PYTHON_DIR=python
   if test "x$pythoninstalldir" = "x"; then
      PYTHON_INSTALL_DIR="$PYTHON_SITE_PKG"
   else
      PYTHON_INSTALL_DIR="$pythoninstalldir"
   fi
   if test "x$pythoninstalllibdir" = "x"; then
      PYTHON_INSTALL_LIB_DIR="$PYTHON_INSTALL_DIR"
   else
      PYTHON_INSTALL_LIB_DIR="$pythoninstalllibdir"
   fi

   if test "x$pythonusepthreads" = "x"; then
      cat - <<_ACEOF >conftest.py
try:
  import threading
  print('yes')
except:
  print('no')
_ACEOF
      pythonusepthreads=`$PYTHON conftest.py`
      rm -f conftest.py
   fi
   echo "checking for python threads... $pythonusepthreads"

   if test "x$pythonusepthreads" = "xyes"; then
      PYTHON_HAS_POSIX_THREADS=1
      PYTHON_POSIX_LIB='$(top_builddir)/unix/libOpenIPMIpthread.la'
      PYTHON_POSIX_SO='$(top_builddir)/unix/.libs/libOpenIPMIpthread.so'
   else
      PYTHON_HAS_POSIX_THREADS=0
      PYTHON_POSIX_LIB='$(top_builddir)/unix/libOpenIPMIposix.la'
      PYTHON_POSIX_SO='$(top_builddir)/unix/.libs/libOpenIPMIposix.so'
   fi
fi

AC_SUBST(PYTHON_DIR)
AC_SUBST(PYTHON_INSTALL_DIR)
AC_SUBST(PYTHON_INSTALL_LIB_DIR)
AC_SUBST(PYTHON_HAS_POSIX_THREADS)
AC_SUBST(PYTHON_POSIX_LIB)
AC_SUBST(PYTHON_POSIX_SO)
AC_SUBST(PYTHON_SWIG_FLAGS)

# Check for Tkinter/Tix
if test "x$tkinter" = "x"; then
   if test "x$PYTHON" != "x"; then
      cat - <<_ACEOF >conftest.py
try:
  import Tix
  print('yes')
except:
  try:
    import tkinter
    print('yes')
  except:
    print('no')
_ACEOF
      tkinter=`${PYTHON} conftest.py`
      rm -f conftest.py
   fi
fi
echo "checking for tkinter... $tkinter"
if test "x$tkinter" = "xyes"; then
   PYTHON_GUI_DIR=openipmigui
else
   PYTHON_GUI_DIR=
fi
AC_SUBST(PYTHON_GUI_DIR)

# Now check for swig
SWIG_DIR=
SWIG=
if test "x$tryswig" = "xyes"; then
   if test "x$swigprog" != "x"; then
      SWIG="$swigprog"
   fi
   AX_PKG_SWIG([1.3.21])
   if test "x$SWIG" != "x"; then
      AC_DEFINE([HAVE_SWIG], [], [Have swig installed])
      SWIG_DIR=swig
   fi
fi
AC_SUBST(SWIG_DIR)
AC_SUBST(SWIG)

# Check for readline
AC_CHECK_LIB(readline, rl_initialize, READLINE_LIBS=-lreadline,
      [AC_MSG_ERROR([libreadline is required!])])
AC_SUBST(READLINE_LIBS)

# Handle SNMP support
if test "x$trynetsnmp" != "xno"; then
   HAVE_NETSNMP=no
   AC_CHECK_HEADER(net-snmp/net-snmp-config.h, HAVE_NETSNMP=yes; )
   if test "x$HAVE_NETSNMP" = "xyes"; then
      AC_CHECK_LIB(netsnmp, snmp_add, 
		   SNMPLIBS="-lnetsnmp $SNMPLIBS"
		   AC_DEFINE([HAVE_NETSNMP], [], [Have Net SNMP])
		   HAVE_NETSNMP=yes,
		   ,
		   $SNMPLIBS)
      if test "x$HAVE_NETSNMP" = "xno"; then
	  # Try net snmp with crypto
	  AC_CHECK_LIB(netsnmp, snmp_add_full, 
	               SNMPLIBS="-lnetsnmp -lcrypto $SNMPLIBS"
		       AC_DEFINE([HAVE_NETSNMP], [], [Have Net SNMP])
		       HAVE_NETSNMP=yes,
		       ,
		       -lcrypto $SNMPLIBS)
      fi
      if test "x$HAVE_NETSNMP" = "xno"; then
	  AC_MSG_WARN([Found NET SNMP include files, but could not find libraries])
      fi
   fi
fi

AC_SUBST(SNMPLIBS)

# Handle SNMP support
if test "x$tryopenssl" != "xno"; then

   HAVE_OPENSSL=no
   # Try net snmp first
   AC_CHECK_HEADER(openssl/crypto.h, FOUND_OPENSSL=yes; )

   if test "x$FOUND_OPENSSL" = "xyes"; then
      AC_CHECK_LIB(crypto, CRYPTO_malloc, 
		   OPENSSLLIBS="-lcrypto $OPENSSLLIBS"
		   AC_DEFINE([HAVE_OPENSSL], [], [Have the openssl libraries])
		   HAVE_OPENSSL=yes,
		   ,
		   $OPENSSLLIBS)
   fi
fi

AC_SUBST(OPENSSLLIBS)
AC_SUBST(OPENSSLINCS)

AC_CHECK_LIB(socket, bind, [SOCKETLIB=-lsocket], [SOCKETLIB=])
AC_SUBST(SOCKETLIB)

AC_CHECK_LIB(socket, getaddrinfo, [AC_DEFINE([HAVE_GETADDRINFO], 1)],
             [AC_CHECK_FUNCS(getaddrinfo)])

AC_CHECK_FUNCS([syslog])

# Now check for dia and the dia version.  They changed the output format
# specifier without leaving backwards-compatible handling, so lots of ugly
# checks here.
DIA=
AC_PATH_PROG(diaprog, dia)
if test "x$diaprog" != "x"; then
   diaver=`$diaprog --version 2>&1 | grep 'Dia version' | sed 's/Dia version \([[^,]]*\), .*$/\1/' | sed 's/^\([[0-9.]]\+\)+.*$/\1/'`
   tmp=`echo $diaver | sed 's/^[[0-9.]]\+$//'`
   if test "x$diaver" = "x" -o "x$tmp" != 'x'; then
      # Couldn't get the dia version, give up.
      echo "Couldn't determine the dia version from '$diaver'"
      exit 1
   else
      DIA="$diaprog"
      diaver_major=`echo $diaver | sed 's/\.[[0-9.]]\+$//'`
      diaver_minor=`echo $diaver | sed 's/^[[0-9]]\+\.//' | sed 's/\.[[0-9.]]\+$//'`
      echo "dia version is $diaver_major.$diaver_minor"
      if test $diaver_major -lt 1 -a $diaver_minor -lt 95; then
         # 0.94 and below use --export-to-format
	 DIA_FILTER_NAME=--export-to-format
      else
	 DIA_FILTER_NAME=--filter
      fi
   fi
fi

LATEX=
BIBTEX=
DVIPDF=
AC_PATH_PROG(LATEX, latex)
AC_PATH_PROG(BIBTEX, bibtex)
AC_PATH_PROG(DVIPDF, dvipdf)
if test "x$DIA" = "x" -o "x$LATEX" = "x" -o "x$BIBTEX" = "x" -o "x$DVIPDF" = "x"; then
   echo "dia, latex, bibtex, and/or dvipdf is not present or not valid, you cannot regenerate documentation"
   echo "The documentation comes already generated, so this is generally not a problem"
fi
AC_SUBST(DIA)
AC_SUBST(DIA_FILTER_NAME)
AC_SUBST(LATEX)
AC_SUBST(BIBTEX)
AC_SUBST(DVIPDF)

OPENIPMI_SWIG_LIBS="${PYTHON_POSIX_LIB} \$(top_builddir)/lib/libOpenIPMI.la \$(top_builddir)/utils/libOpenIPMIutils.la \$(top_builddir)/cmdlang/libOpenIPMIcmdlang.la ${GLIB_LIB} ${TCL_LIB}"
OPENIPMI_SWIG_SO="${PYTHON_POSIX_SO}:\$(top_builddir)/lib/.libs/libOpenIPMI.so:\$(top_builddir)/utils/.libs/libOpenIPMIutils.so:\$(top_builddir)/cmdlang/.libs/libOpenIPMIcmdlang.so:${GLIB_SO}:${TCL_SO}"
AC_SUBST(OPENIPMI_SWIG_LIBS)
AC_SUBST(OPENIPMI_SWIG_SO)

AX_HAVE_EPOLL_PWAIT(
   [AX_CONFIG_FEATURE_ENABLE(epoll_pwait)],
   [AX_CONFIG_FEATURE_DISABLE(epoll_pwait)])
AX_CONFIG_FEATURE(
   [epoll_pwait], [This platform supports epoll(7) with epoll_pwait(2)],
   [HAVE_EPOLL_PWAIT], [This platform supports epoll(7) with epoll_pwait(2).])

AC_CHECK_LIB(curses, tgetent, TERM_LIBS=-lcurses,
   [AC_CHECK_LIB(ncursesw, tgetent, TERM_LIBS=-lncursesw,
    [AC_CHECK_LIB(ncurses, tgetent, TERM_LIBS=-lncurses,
      [AC_MSG_ERROR([libtermcap, libcurses or libncurses are required!])])] )] )
AC_SUBST(TERM_LIBS)

AC_CHECK_FUNCS(cfmakeraw)

if test "${build}" != "${host}" ; then
	AC_CHECK_PROGS(BUILD_CC, [${build_alias}-gcc ${build}-gcc gcc])
else
	BUILD_CC="$CC"
fi
AC_SUBST([BUILD_CC])

AC_CONFIG_FILES([Makefile
	  utils/Makefile
	  lib/Makefile
	  unix/Makefile
	  glib/Makefile
	  tcl/Makefile
	  ui/Makefile
	  lanserv/Makefile
	  lanserv/OpenIPMI/Makefile
	  lanserv/sdrcomp/Makefile
	  lanserv/marvell-bmc/Makefile
	  sample/Makefile
	  doc/Makefile
	  man/Makefile
	  swig/Makefile
	  swig/perl/Makefile
	  swig/python/Makefile
	  swig/python/openipmigui/Makefile
	  cmdlang/Makefile
	  include/Makefile
	  include/OpenIPMI/Makefile
	  include/OpenIPMI/ipmiif.h
	  include/OpenIPMI/internal/Makefile
	  OpenIPMI.spec
	  OpenIPMIutils.pc
	  OpenIPMI.pc
	  OpenIPMIpthread.pc
	  OpenIPMIposix.pc
	  OpenIPMIglib.pc
	  OpenIPMItcl.pc
	  OpenIPMIcmdlang.pc
	  OpenIPMIui.pc])
AC_OUTPUT
