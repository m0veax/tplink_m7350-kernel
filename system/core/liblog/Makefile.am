
lib_LIBRARIES := liblog.a
lib_LTLIBRARIES := liblog.la

liblog_a_CPPFLAGS := -I../include
liblog_a_CPPFLAGS += -DHAVE_PTHREADS
liblog_a_CPPFLAGS += -DHAVE_SYS_UIO_H
liblog_a_CPPFLAGS += -include ../include/arch/linux-arm/OEConfig.h
liblog_la_CPPFLAGS = $(liblog_a_CPPFLAGS)

requiredlibs = -lpthread

liblog_a_SOURCES = logd_write.c logprint.c event_tag_map.c
liblog_la_SOURCES = $(liblog_a_SOURCES)
