#libutils from Android.

LOCAL_SRC_FILES:= \
	Asset.cpp \
	AssetDir.cpp \
	BufferedTextOutput.cpp \
	CallStack.cpp \
	Debug.cpp \
	FileMap.cpp \
	Flattenable.cpp \
	ObbFile.cpp \
	Pool.cpp \
	RefBase.cpp \
	SharedBuffer.cpp \
	Static.cpp \
	StopWatch.cpp \
	StreamingZipInflater.cpp \
	String8.cpp \
	String16.cpp \
	StringArray.cpp \
	SystemClock.cpp \
	TextOutput.cpp \
	Threads.cpp \
	Timers.cpp \
	VectorImpl.cpp \
	ZipFileCRO.cpp \
	ZipFileRO.cpp \
	ZipUtils.cpp \
	misc.cpp


LOCAL_LIBS += -lpthread  -llog  -lcutils
LOCAL_CFLAGS += -I./
#we should use autoconf later.
LOCAL_CFLAGS += -DHAVE_ENDIAN_H  -DHAVE_PTHREADS  -DHAVE_SYS_UIO_H  -DHAVE_POSIX_FILEMAP

LIB_OBJECTS := $(LOCAL_SRC_FILES:.cpp=.o)

TARGET = libutils.so

.PHONY: all

all:${TARGET}

%.o:%.cpp
	${CC} -fPIC ${CFLAGS} ${LOCAL_CFLAGS} -c   $< -o  $@

${TARGET}:${LIB_OBJECTS}
	${CC}  -shared ${LDFLAGS} ${LOCAL_LIBS} ${LIB_OBJECTS} -o $@


