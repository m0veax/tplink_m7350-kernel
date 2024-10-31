AM_CFLAGS = -Wall \
        -Wundef \
        -Wstrict-prototypes \
        -Wno-trigraphs \
	$(QMI_CFLAGS)

ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -D__packed__= \
        -DIMAGE_APPS_PROC \
        -DFEATURE_Q_SINGLE_LINK \
        -DFEATURE_Q_NO_SELF_QPTR \
        -DFEATURE_LINUX \
        -DFEATURE_NATIVELINUX \
        -DFEATURE_DSM_DUP_ITEMS \
        -DFEATURE_LE_DIAG

bin_PROGRAMS = qrngd
pkgconfigdir = $(libdir)/pkgconfig
qrngd_SOURCES = qrngd.c
qrngtest_SOURCES = qrngtest.c
