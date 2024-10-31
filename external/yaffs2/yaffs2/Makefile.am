bin_PROGRAMS = mkyaffs2image

mkyaffs2image_SOURCES = utils/mkyaffs2image.c \
	yaffs_packedtags2.c \
	yaffs_tagsvalidity.c

AM_CFLAGS = -O2 \
	-Wall \
	-DCONFIG_YAFFS_UTIL \
	-Wshadow \
	-Wpointer-arith \
	-Wwrite-strings \
	-Wstrict-prototypes \
	-Wmissing-declarations \
	-Wmissing-prototypes \
	-Wredundant-decls \
	-Wnested-externs \
	-Winline
