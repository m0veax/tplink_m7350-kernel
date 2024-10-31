AM_CFLAGS := -Wundef \
        -Wstrict-prototypes \
        -Wno-trigraphs \
        -g -O0 \
        -fno-inline \
        -fno-short-enums \
        -fpic

AM_CPPFLAGS := \
              -I.

AM_LDFLAGS := -shared -version-info 1:0:0
c_sources := duadummy.c

library_includedir := $(pkgincludedir)
library_include_HEADERS := $(h_sources)

lib_LTLIBRARIES := libduadummy.la
if USE_GLIB
libduadummy_la_SOURCES := $(c_sources) $(h_sources)
libduadummy_la_CFLAGS := $(AM_CFLAGS) -DUSE_GLIB @GLIB_CFLAGS@
libduadummy_la_CPPFLAGS := $(AM_CPPFLAGS) -DUSE_GLIB @GLIB_CFLAGS@
libduadummy_la_LIBADD := -lm -lpthread @GLIB_LIBS@
libduadummy_la_LDFLAGS := -all-static
else
libduadummy_la_SOURCES := $(c_sources) $(h_sources)
libduadummy_la_CFLAGS := $(AM_CFLAGS)
libduadummy_la_CPPFLAGS := $(AM_CPPFLAGS)
libduadummy_la_LIBADD := -lm -lpthread
libduadummy_la_LDFLAGS := -all-static
endif
