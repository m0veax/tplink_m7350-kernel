LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    clz.cpp.arm \
    DisplayHardware/DisplayHardware.cpp \
    DisplayHardware/DisplayHardwareBase.cpp \
    BlurFilter.cpp.arm \
    GLExtensions.cpp \
    Layer.cpp \
    LayerBase.cpp \
    LayerBuffer.cpp \
    LayerBlur.cpp \
    LayerDim.cpp \
    MessageQueue.cpp \
    SurfaceFlinger.cpp \
    TextureManager.cpp \
    Transform.cpp \
    DumpFrame.cpp \
    PostBufLock.cpp

LOCAL_CFLAGS:= -DLOG_TAG=\"SurfaceFlinger\"
LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES

ifeq ($(TARGET_BOARD_PLATFORM), omap3)
	LOCAL_CFLAGS += -DNO_RGBX_8888
endif
ifeq ($(TARGET_BOARD_PLATFORM), s5pc110)
	LOCAL_CFLAGS += -DHAS_CONTEXT_PRIORITY
endif

ifeq ($(TARGET_AVOID_DRAW_TEXTURE_EXTENSION), true)
	LOCAL_CFLAGS += -DAVOID_DRAW_TEXTURE
endif

# need "-lrt" on Linux simulator to pick up clock_gettime
ifeq ($(TARGET_SIMULATOR),true)
	ifeq ($(HOST_OS),linux)
		LOCAL_LDLIBS += -lrt -lpthread
	endif
endif

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libhardware \
	libutils \
	libEGL \
	libGLESv1_CM \
	libbinder \
	libui \
	libsurfaceflinger_client

LOCAL_C_INCLUDES := \
	$(call include-path-for, corecg graphics)

LOCAL_C_INCLUDES += hardware/libhardware/modules/gralloc

ifeq ($(TARGET_USES_SF_BYPASS),true)
LOCAL_CFLAGS += -DSF_BYPASS
endif

ifeq ($(TARGET_USES_OVERLAY),true)
LOCAL_CFLAGS += -DTARGET_USES_OVERLAY
LOCAL_SHARED_LIBRARIES += liboverlay
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_C_INCLUDES += hardware/msm7k/liboverlay \
		    hardware/qcom/display/libgralloc
endif
LOCAL_MODULE:= libsurfaceflinger

ifeq ($(TARGET_USES_16BPPSURFACE_FOR_OPAQUE),true)
LOCAL_CFLAGS += -DUSE_16BPPSURFACE_FOR_OPAQUE
endif

include $(BUILD_SHARED_LIBRARY)
