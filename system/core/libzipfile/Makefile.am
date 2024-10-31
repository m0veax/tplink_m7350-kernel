noinst_lib_LIBRARIES := libzipfile.a
noinst_libdir = noinstlibdir

libzipfile_a_CFLAGS := -O2 -g
libzipfile_a_CFLAGS += -I../include
libzipfile_a_CFLAGS += -include ../include/arch/linux-arm/OEConfig.h

libzipfile_a_LDFLAGS = -lpthread -lz

libzipfile_a_SOURCES = centraldir.c zipfile.c

