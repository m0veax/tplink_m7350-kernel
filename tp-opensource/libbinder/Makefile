#libbinder from Android.

LOCAL_SRC_FILES := \
    Binder.cpp \
    BpBinder.cpp \
    IInterface.cpp \
    IMemory.cpp \
    IPCThreadState.cpp \
    IPermissionController.cpp \
    IServiceManager.cpp \
    MemoryDealer.cpp \
    MemoryBase.cpp \
    MemoryHeapBase.cpp \
    MemoryHeapPmem.cpp \
    Parcel.cpp \
    Permission.cpp \
    ProcessState.cpp \
    Static.cpp

LOCAL_LIBS += -lpthread -llog -lcutils -lutils
LOCAL_CFLAGS += -I./
#we should use autoconf later.
LOCAL_CFLAGS += -DHAVE_ENDIAN_H  -DHAVE_PTHREADS  -DHAVE_SYS_UIO_H  -DHAVE_POSIX_FILEMAP

LIB_OBJECTS := $(LOCAL_SRC_FILES:.cpp=.o)


.PHONY: all

TARGET = libbinder.so

all:${TARGET}

%.o:%.cpp
	${CC} -fPIC ${CFLAGS} ${LOCAL_CFLAGS} -c   $< -o  $@

${TARGET}:${LIB_OBJECTS}
	${CC}  -shared ${LDFLAGS} ${LOCAL_LIBS} ${LIB_OBJECTS} -o $@
