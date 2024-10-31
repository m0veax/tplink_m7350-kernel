  AC_PREREQ(2.61)

  AC_INIT([platform-externa;],1.0.0)

  AC_CONFIG_AUX_DIR([build-aux])

  AM_INIT_AUTOMAKE([-Wall gnu foreign])

  AM_MAINTAINER_MODE

  AC_CONFIG_HEADER([config.h])

  # Checks for programs.

  AC_PROG_CC
  AC_PROG_INSTALL

  AC_C_BIGENDIAN(
    AC_DEFINE(HAVE_BIG_ENDIAN, 1, [machine is bigendian]),
    AC_DEFINE(HAVE_LITTLE_ENDIAN, 1, [machine is littleendian]),
    AC_MSG_ERROR(unknown endianess),
    AC_MSG_ERROR(universial endianess not supported)
  )
  #AC_CHECK_HEADERS([sys/uio.h])

  #
  # Build all components by default
  #
  # To add new components: add AC_ARG_ENABLE, AM_CONDITIONAL and update AC_OUTPUT
  #

  AC_ARG_ENABLE([yaffs2],
          [AS_HELP_STRING([--disable-yaffs2], [disable building of yaffs2])],
          [YAFFS2=$enableval],
          [YAFFS2=yes])

  AS_CASE([$host],
          [arm*], [ARM=yes],
          [ARM=no]
  )
  AM_CONDITIONAL(ARM, [test "x$ARM" = "xyes"])

  AC_ARG_WITH([core_includes],
        AC_HELP_STRING([--with-core-includes=@<:@dir@:>@],
           [Specify the location of the core headers]),
        [core_incdir=$withval],
        with_core_includes=no)

  if test "x$with_core_includes" != "xno"; then
     CFLAGS="${CFLAGS} -I${core_incdir}"
  fi

  AC_SUBST([CFLAGS])

  AC_OUTPUT([ \
          Makefile \
          yaffs2/Makefile \
  ])
