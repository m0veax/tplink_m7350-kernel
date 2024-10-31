# Copyright 2006 The Android Open Source Project
# Copyright (c) 2009, The Linux Foundation. All rights reserved.

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= logcat.cpp event.logtags

LOCAL_SHARED_LIBRARIES := liblog

ifeq ($(strip $(QC_PROP)),true)
LOCAL_SHARED_LIBRARIES += libdiag
LOCAL_CFLAGS += -DUSE_DIAG
LOCAL_C_INCLUDES := vendor/qcom/opensource/diag
endif # QC_PROP

LOCAL_MODULE:= logcat

include $(BUILD_EXECUTABLE)
