bin_PROGRAMS = adbd adb

# adb host tool for linux
# =========================================================
adb_SOURCES := \
	adb.c \
	console.c \
	transport.c \
	transport_local.c \
	transport_usb.c \
	commandline.c \
	adb_client.c \
	sockets.c \
	services.c \
	file_sync_client.c  \
	utils.c \
	usb_vendors.c \
	fdevent.c \
	usb_linux.c \
	get_my_path_linux.c

adb_CFLAGS = -O2 \
	-g \
	-DADB_HOST=1 \
	-Wall \
	-Wno-unused-parameter \
	-D_XOPEN_SOURCE \
	-D_GNU_SOURCE \
	-I../include \
	-include ../include/arch/linux-arm/OEConfig.h

adb_LDFLAGS = -lpthread -all-static

adb_LDADD = ../libcutils/libcutils.a
adb_LDADD += ../libzipfile/libzipfile.a
adb_LDADD += -lz

# adbd device daemon
# =========================================================
adbd_CC = @CC@

adbd_SOURCES = adb.c \
	fdevent.c \
	transport.c \
	transport_local.c \
	transport_usb.c \
	sockets.c \
	services.c \
	file_sync_service.c \
	jdwp_service.c \
	framebuffer_service.c \
	remount_service.c \
	usb_linux_client.c \
	log_service.c \
	utils.c

adbd_CFLAGS = -O2 \
	-g \
	-DADB_HOST=0 \
	-Wall \
	-Wno-unused-parameter \
	-D_XOPEN_SOURCE \
	-D_GNU_SOURCE \
	-DANDROID_GADGET=1 \
	-DADB_REBOOT_ENABLED=1 \
	-I../include \
	-include ../include/arch/linux-arm/OEConfig.h

adbd_LDFLAGS = -lpthread -all-static

adbd_LDADD =  ../libcutils/libcutils.a
