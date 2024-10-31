LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(call is-board-platform-in-list,msm7627a msm7627_surf),true)
    LOCAL_CFLAGS += -DUSE_AAC_HW_DEC
endif

ifeq ($(call is-chipset-in-board-platform,msm7627),true)
    LOCAL_CFLAGS += -DTARGET7x27
endif
ifeq ($(call is-board-platform,msm7627a),true)
    LOCAL_CFLAGS += -DTARGET7x27A
endif
ifeq ($(call is-chipset-in-board-platform,msm7630),true)
    LOCAL_CFLAGS += -DTARGET7x30
endif
ifeq ($(call is-board-platform-in-list,$(QSD8K_BOARD_PLATFORMS)),true)
    LOCAL_CFLAGS += -DTARGET8x50
endif
ifeq ($(call is-board-platform-in-list,msm8660 msm8960),true)
    LOCAL_CFLAGS += -DTARGET8x60
endif

include frameworks/base/media/libstagefright/codecs/common/Config.mk

LOCAL_SRC_FILES:=                         \
        AMRExtractor.cpp                  \
        AMRWriter.cpp                     \
        AudioPlayer.cpp                   \
        AudioSource.cpp                   \
        AwesomePlayer.cpp                 \
        CameraSource.cpp                  \
        DataSource.cpp                    \
        ESDS.cpp                          \
        ExtendedExtractor.cpp             \
        FileSource.cpp                    \
        HTTPStream.cpp                    \
        JPEGSource.cpp                    \
        MP3Extractor.cpp                  \
        MPEG2TSWriter.cpp                 \
        MPEG4Extractor.cpp                \
        MPEG4Writer.cpp                   \
        MediaBuffer.cpp                   \
        MediaBufferGroup.cpp              \
        MediaDefs.cpp                     \
        MediaExtractor.cpp                \
        MediaSource.cpp                   \
        MetaData.cpp                      \
        NuCachedSource2.cpp               \
        NuHTTPDataSource.cpp              \
        OMXClient.cpp                     \
        OMXCodec.cpp                      \
        OggExtractor.cpp                  \
        SampleIterator.cpp                \
        SampleTable.cpp                   \
        ShoutcastSource.cpp               \
        StagefrightMediaScanner.cpp       \
        StagefrightMetadataRetriever.cpp  \
        ThreadedSource.cpp                \
        ThrottledSource.cpp               \
        TimeSource.cpp                    \
        TimedEventQueue.cpp               \
        Utils.cpp                         \
        WAVExtractor.cpp                  \
        avc_utils.cpp                     \
        string.cpp                        \
        ExtendedWriter.cpp                \
        NativeBuffer.cpp              \
        FMA2DPWriter.cpp

#LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
#        $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include \


LOCAL_C_INCLUDES:= \
        $(JNI_H_INCLUDE) \
        $(TOP)/frameworks/base/include/media/stagefright/openmax \
        $(TOP)/frameworks/base/include/media/stagefright \
        $(TOP)/external/tremolo \
        $(TOP)/frameworks/base/media/libstagefright/rtsp \
        $(TOP)/vendor/qcom/opensource/omx/mm-core/omxcore/inc \
        $(TOP)/hardware/msm7k/libgralloc-qsd8k \
        $(TOP)/hardware/libhardware_legacy/include

LOCAL_SHARED_LIBRARIES := \
        libbinder         \
        libmedia          \
        libutils          \
        libcutils         \
        libui             \
        libsonivox        \
        libvorbisidec     \
        libsurfaceflinger_client \
        libcamera_client  \
        libhardware_legacy

LOCAL_STATIC_LIBRARIES := \
        libstagefright_aacdec \
        libstagefright_aacenc \
        libstagefright_amrnbdec \
        libstagefright_amrnbenc \
        libstagefright_amrwbdec \
        libstagefright_amrwbenc \
        libstagefright_avcdec \
        libstagefright_avcenc \
        libstagefright_m4vh263dec \
        libstagefright_m4vh263enc \
        libstagefright_mp3dec \
        libstagefright_vorbisdec \
        libstagefright_matroska \
        libstagefright_vpxdec \
        libvpx \
        libstagefright_mpeg2ts \
        libstagefright_httplive \
        libstagefright_rtsp \
        libstagefright_id3 \
        libstagefright_g711dec \

ifeq ($(BOARD_USES_ALSA_AUDIO),true)
        LOCAL_SRC_FILES += LPAPlayerALSA.cpp
        LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/libalsa-intf
        LOCAL_C_INCLUDES += $(TOP)/kernel/include/sound
        LOCAL_SHARED_LIBRARIES += libalsa-intf
else
        LOCAL_SRC_FILES += LPAPlayer.cpp
endif

LOCAL_SHARED_LIBRARIES += \
        libstagefright_amrnb_common \
        libstagefright_enc_common \
        libstagefright_avc_common \
        libstagefright_foundation \
        libstagefright_color_conversion

ifeq ($(TARGET_OS)-$(TARGET_SIMULATOR),linux-true)
        LOCAL_LDLIBS += -lpthread -ldl
        LOCAL_SHARED_LIBRARIES += libdvm
        LOCAL_CPPFLAGS += -DANDROID_SIMULATOR
endif

ifneq ($(TARGET_SIMULATOR),true)
LOCAL_SHARED_LIBRARIES += libdl
endif

ifeq ($(TARGET_OS)-$(TARGET_SIMULATOR),linux-true)
        LOCAL_LDLIBS += -lpthread
endif

LOCAL_CFLAGS += -Wno-multichar

ifeq ($(WEBCORE_INPAGE_VIDEO), true)
LOCAL_CFLAGS += -DYUVCLIENT
endif

LOCAL_MODULE:= libstagefright

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))
