bin_PROGRAMS = logcat

 
logcat_SOURCES = logcat.cpp

logcat_CPPFLAGS = -O2 \
	-g \
	-Wall \
	-Wno-unused-parameter \
	-D_XOPEN_SOURCE \
	-D_GNU_SOURCE \
	-DHAVE_IOCTL \
	-DHAVE_SYS_UIO_H \
	-I../include \
	-include ../include/arch/linux-arm/OEConfig.h

logcat_LDADD =  ../liblog/liblog.a
