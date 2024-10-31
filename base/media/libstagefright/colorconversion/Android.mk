LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

#Definition to log RGB
LOCAL_CFLAGS := -UOUTPUT_RGB565_LOGGING

LOCAL_SRC_FILES:=                     \
        ColorConverter.cpp            \
        SoftwareRenderer.cpp

LOCAL_C_INCLUDES := \
        $(TOP)/frameworks/base/include/media/stagefright/openmax \
        $(TOP)/frameworks/base/include/media/stagefright         \
        $(TOP)/hardware/msm7k/libgralloc-qsd8k                   \
        $(TOP)/vendor/qcom/proprietary/mm-color-converter

ifeq ($(TARGET_USES_ION),true)
LOCAL_CFLAGS += -DUSE_ION
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/
endif

LOCAL_SHARED_LIBRARIES :=       \
        libbinder               \
        libmedia                \
        libutils                \
        libui                   \
        libcutils               \
        libsurfaceflinger_client\
        libcamera_client        \
        libdl

LOCAL_MODULE:= libstagefright_color_conversion

include $(BUILD_SHARED_LIBRARY)
