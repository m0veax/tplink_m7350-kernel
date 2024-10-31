
# NOTE: Only supports builds of shared/static libraries for the target

common_sources := array.c
common_sources += hashmap.c
common_sources += atomic.c
common_sources += native_handle.c
common_sources += buffer.c
common_sources += socket_inaddr_any_server.c
common_sources += socket_local_client.c
common_sources += socket_local_server.c
common_sources += socket_loopback_client.c
common_sources += socket_loopback_server.c
common_sources += socket_network_client.c
common_sources += config_utils.c
common_sources += cpu_info.c
common_sources += load_file.c
common_sources += strdup16to8.c
common_sources += strdup8to16.c
common_sources += record_stream.c
common_sources += process_name.c
common_sources += properties.c
common_sources += threads.c
# common_sources += sched_policy.c
# common_sources += iosched_policy.c

# TODO: When building against Mingw, these files should NOT be built
common_sources += abort_socket.c
common_sources += mspace.c
common_sources += selector.c
common_sources += tztime.c
# common_sources += adb_networking.c
common_sources += zygote.c

# Target libcutils
lib_LIBRARIES := libcutils.a

libcutils_a_CPPFLAGS := -I../include
libcutils_a_CPPFLAGS += -I../liblog
libcutils_a_CPPFLAGS += -DHAVE_PTHREADS
libcutils_a_CPPFLAGS += -DLINUX_ENABLED
libcutils_a_CPPFLAGS += -DANDROID_SMP=0
libcutils_a_CPPFLAGS += -include ../include/arch/linux-arm/OEConfig.h

# libcutils_a_LDFLAGS := -version-info 1:0:0
# libcutils_a_LIBADD  := -lpthread
requiredlibs = -lpthread

libcutils_a_SOURCES := $(common_sources)
libcutils_a_SOURCES += ashmem-dev.c
libcutils_a_SOURCES += mq.c
libcutils_a_SOURCES += memory.c

if ARM
libcutils_a_CPPFLAGS += -DHAVE_ASM_MEMSET
libcutils_a_SOURCES += memset32.S
endif # !ARM


# Build a DSO too
lib_LTLIBRARIES := libcutils.la
libcutils_la_SOURCES := $(libcutils_a_SOURCES)
libcutils_la_CPPFLAGS := $(libcutils_a_CPPFLAGS)

pkgconfigdir := $(libdir)/pkgconfig
pkgconfig_DATA := libcutils.pc
EXTRA_DIST := $(pkgconfig_DATA)
