bin_PROGRAMS = fastboot

# fastboot tool for linux
# =========================================================
fastboot_SOURCES := \
	protocol.c \
	engine.c \
	bootimg.c \
	fastboot.c \
	usb_linux.c \
	util_linux.c

AM_CFLAGS = -O2 \
	-g \
	-Wall \
	-Wno-unused-parameter \
	-D_XOPEN_SOURCE \
	-D_GNU_SOURCE \
	-I../include \
	-I../mkbootimg \
	-include ../include/arch/linux-arm/OEConfig.h

fastboot_LDFLAGS = -lpthread -all-static

fastboot_LDADD = ../libzipfile/libzipfile.a
fastboot_LDADD += -lz
