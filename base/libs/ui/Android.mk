LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	EGLUtils.cpp \
	EventHub.cpp \
	EventRecurrence.cpp \
	FramebufferNativeWindow.cpp \
	GraphicBuffer.cpp \
	GraphicBufferAllocator.cpp \
	GraphicBufferMapper.cpp \
	GraphicLog.cpp \
	KeyLayoutMap.cpp \
	KeyCharacterMap.cpp \
	Input.cpp \
	InputDispatcher.cpp \
	InputManager.cpp \
	InputReader.cpp \
	InputTransport.cpp \
	IOverlay.cpp \
	Overlay.cpp \
	PixelFormat.cpp \
	Rect.cpp \
	Region.cpp

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libutils \
	libEGL \
	libbinder \
	libpixelflinger \
	libhardware \
	libhardware_legacy

ifeq ($(TARGET_HAVE_TSLIB),true)
	LOCAL_CFLAGS += -DHAVE_TSLIB
	LOCAL_SHARED_LIBRARIES += libtslib
	LOCAL_C_INCLUDES += external/tslib/src
endif

ifeq ($(TARGET_FAKE_TOUCHSCREEN),true)
	LOCAL_CFLAGS += -DFAKE_TOUCHSCREEN
endif

ifeq ($(BOARD_USES_ALSA_AUDIO),true)
    LOCAL_CFLAGS += -DALSA_HEADSET_DETECTION
    LOCAL_CFLAGS += -include $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/linux/input.h
    LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
    LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

LOCAL_MODULE:= libui

ifeq ($(TARGET_SIMULATOR),true)
    LOCAL_LDLIBS += -lpthread
endif

include $(BUILD_SHARED_LIBRARY)


# Include subdirectory makefiles
# ============================================================

# If we're building with ONE_SHOT_MAKEFILE (mm, mmm), then what the framework
# team really wants is to build the stuff defined by this makefile.
ifeq (,$(ONE_SHOT_MAKEFILE))
include $(call first-makefiles-under,$(LOCAL_PATH))
endif
