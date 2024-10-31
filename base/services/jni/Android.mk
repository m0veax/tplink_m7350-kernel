LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    com_android_server_AlarmManagerService.cpp \
    com_android_server_BatteryService.cpp \
    com_android_server_InputManager.cpp \
    com_android_server_LightsService.cpp \
    com_android_server_PowerManagerService.cpp \
    com_android_server_SystemServer.cpp \
    com_android_server_UsbService.cpp \
    com_android_server_VibratorService.cpp \
	com_android_server_location_GpsLocationProvider.cpp \
    onload.cpp

LOCAL_C_INCLUDES += \
	$(JNI_H_INCLUDE)

ifeq ($(BOARD_USES_ALSA_AUDIO),true)
    LOCAL_CFLAGS += -DALSA_HEADSET_DETECTION
    LOCAL_CFLAGS += -include $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/linux/input.h
    LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

LOCAL_SHARED_LIBRARIES := \
    libandroid_runtime \
	libcutils \
	libhardware \
	libhardware_legacy \
	libnativehelper \
    libsystem_server \
	libutils \
	libui \
    libsurfaceflinger_client

ifeq ($(TARGET_SIMULATOR),true)
ifeq ($(TARGET_OS),linux)
ifeq ($(TARGET_ARCH),x86)
LOCAL_LDLIBS += -lpthread -ldl -lrt
endif
endif
endif

ifeq ($(WITH_MALLOC_LEAK_CHECK),true)
	LOCAL_CFLAGS += -DMALLOC_LEAK_CHECK
endif

ifeq ($(TARGET_HAVE_TSLIB),true)
	LOCAL_CFLAGS += -DHAVE_TSLIB
	LOCAL_C_INCLUDES += external/tslib/src
endif

LOCAL_MODULE:= libandroid_servers

include $(BUILD_SHARED_LIBRARY)
    
