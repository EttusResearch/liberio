AC_INIT([Liberio DMA Streaming], [0.3], [moritz.fischer@ettus.com])
AM_INIT_AUTOMAKE([foreign dist-xz])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])

LT_INIT

AC_PROG_CC
AC_PROG_LIBTOOL
AC_PROG_INSTALL

AC_CHECK_HEADERS([fcntl.h])
AC_CHECK_HEADERS([unistd.h])
AC_CHECK_HEADERS([sys/ioctl.h])
AC_CHECK_HEADERS([sys/select.h])
AC_CHECK_HEADERS([sys/mman.h])
AC_CHECK_HEADERS([sys/socket.h])

PKG_CHECK_MODULES([libudev], [libudev], [], AC_MSG_ERROR([Liberio requires libudev]))

AC_CONFIG_FILES([Makefile include/Makefile src/Makefile examples/Makefile liberio.pc])
AC_OUTPUT


cat <<EOF

------------------ Summary ------------------
 $PACKAGE_NAME version $PACKAGE_VERSION
  Prefix.........: $prefix
  C Compiler.....: $CC $MORE_CFLAGS $MORE_CPPFLAGS $CFLAGS $CPPFLAGS
  Linker.........: $LD $MORE_LDFLAGS $LDFLAGS $LIBS
---------------------------------------------

EOF
