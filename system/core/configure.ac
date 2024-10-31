  AC_PREREQ(2.61)
  
  AC_INIT([system-core],1.0.0)

  AC_CONFIG_AUX_DIR([build-aux])  

  AM_INIT_AUTOMAKE([-Wall gnu foreign])
  
  AM_MAINTAINER_MODE

  AC_CONFIG_HEADER([config.h])
  AC_CONFIG_MACRO_DIR([m4])

  # Checks for programs.
  AM_PROG_AS
  AC_PROG_CC
  AM_PROG_CC_C_O
  AC_PROG_LIBTOOL
  AC_PROG_AWK
  AC_PROG_CPP
  AC_PROG_CXX
  AC_PROG_INSTALL
  AC_PROG_LN_S
  AC_PROG_MAKE_SET

  AC_CHECK_HEADERS([sys/uio.h])

  #
  # Build all components by default
  #
  # To add new components: add AC_ARG_ENABLE, AM_CONDITIONAL and update AC_OUTPUT
  #

  AC_ARG_ENABLE([libcutils],
          [AS_HELP_STRING([--disable-libcutils], [disable building of libcutils])],
          [LIBCUTILS=$enableval],
          [LIBCUTILS=yes])
  AM_CONDITIONAL(USELIBCUTILS, [test x$LIBCUTILS = xyes])


  AS_CASE([$host],
          [arm*], [ARM=yes],
          [ARM=no]
  )
  AM_CONDITIONAL(ARM, [test "x$ARM" = "xyes"])

  AC_CHECK_FUNCS([strlcpy],[STRLCPY=yes],[STRLCPY=no])
  AM_CONDITIONAL([HAVE_STRLCPY],[test "x$STRLCPY" = "xyes"])

  AC_ARG_WITH([host-os],
          [AS_HELP_STRING([--with-host-os=HOST_OS],[Host OS to build adb host.(default: linux)])],
          [HOST_OS="$withval"],
          [HOST_OS= 'linux'])


  if test "x$HOST_OS" = "xlinux-gnueabi"; then
	HOST_OS=linux
  else
	AC_MSG_NOTICE([Unknown host OS $HOST_OS detected.])
  fi

  AM_CONDITIONAL([LINUX], [test "x$HOST_OS" = "xlinux"])

  AC_OUTPUT([ \
          Makefile \
          libcutils/Makefile \
          libcutils/libcutils.pc \
          liblog/Makefile \
          logcat/Makefile \
          libmincrypt/Makefile \
          libmincrypt/libmincrypt.pc \
          libzipfile/Makefile \
          fastboot/Makefile \
          adb/Makefile \
          usb/Makefile
  ])
