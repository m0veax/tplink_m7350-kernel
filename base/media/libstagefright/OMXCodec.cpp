/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (C) 2010-2011 The Linux Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*--------------------------------------------------------------------------
Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
--------------------------------------------------------------------------*/

//#define LOG_NDEBUG 0
#define LOG_TAG "OMXCodec"
#include <utils/Log.h>

#include "include/AACDecoder.h"
#include "include/AACEncoder.h"
#include "include/AMRNBDecoder.h"
#include "include/AMRNBEncoder.h"
#include "include/AMRWBDecoder.h"
#include "include/AMRWBEncoder.h"
#include "include/AVCDecoder.h"
#include "include/AVCEncoder.h"
#include "include/G711Decoder.h"
#include "include/M4vH263Decoder.h"
#include "include/M4vH263Encoder.h"
#include "include/MP3Decoder.h"
#include "include/VorbisDecoder.h"
#include "include/VPXDecoder.h"

#include "include/ESDS.h"

#include <binder/IServiceManager.h>
#include <binder/MemoryDealer.h>
#include <binder/ProcessState.h>
#include <binder/MemoryBase.h>
#include <media/IMediaPlayerService.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/OMXCodec.h>
#include <media/stagefright/Utils.h>
#include <utils/Vector.h>
#include <cutils/properties.h>

#include <cutils/properties.h>

#include <OMX_Audio.h>
#include <OMX_Component.h>

#include <OMX_QCOMExtns.h>
#include <QOMX_AudioExtensions.h>
#include "include/ThreadedSource.h"

#define OMX_COMPONENT_CAPABILITY_TYPE_INDEX 0xFF7A347

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace android {

struct CodecInfo {
    const char *mime;
    const char *codec;
};

class ColorFormatInfo {
    private:
        enum {
            LOCAL = 0,
            REMOTE = 1,
            END = 2
        };
        static const int32_t preferredColorFormat[END];
    public:
        static int32_t getPreferredColorFormat(bool isLocal) {
            char colorformat[10]="";
            if(!property_get("sf.debug.colorformat", colorformat, NULL)){
                if(isLocal) {
                    return preferredColorFormat[LOCAL];
                }
                return preferredColorFormat[REMOTE];
            } else {
                if(!strcmp(colorformat, "yamato")) {
                    return QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka;
                }
                return preferredColorFormat[LOCAL];
            }
        }
};

const int32_t ColorFormatInfo::preferredColorFormat[] = {
#ifdef TARGET7x30
    QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka,
    QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka
#endif
#ifdef TARGET8x60
    QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka,
    QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka
#endif
#ifdef TARGET7x27
    OMX_QCOM_COLOR_FormatYVU420SemiPlanar,
    QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka
#endif
#ifdef TARGET7x27A
    OMX_QCOM_COLOR_FormatYVU420SemiPlanar,
    OMX_QCOM_COLOR_FormatYVU420SemiPlanar
#endif
#ifdef TARGET8x50
    OMX_QCOM_COLOR_FormatYVU420SemiPlanar,
    QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka
#endif
};


#define FACTORY_CREATE(name) \
static sp<MediaSource> Make##name(const sp<MediaSource> &source) { \
    return new name(source); \
}

#define FACTORY_CREATE_ENCODER(name) \
static sp<MediaSource> Make##name(const sp<MediaSource> &source, const sp<MetaData> &meta) { \
    return new name(source, meta); \
}

#define FACTORY_REF(name) { #name, Make##name },

FACTORY_CREATE(MP3Decoder)
FACTORY_CREATE(AMRNBDecoder)
FACTORY_CREATE(AMRWBDecoder)
FACTORY_CREATE(AACDecoder)
FACTORY_CREATE(AVCDecoder)
FACTORY_CREATE(G711Decoder)
FACTORY_CREATE(M4vH263Decoder)
FACTORY_CREATE(VorbisDecoder)
FACTORY_CREATE(VPXDecoder)
FACTORY_CREATE_ENCODER(AMRNBEncoder)
FACTORY_CREATE_ENCODER(AMRWBEncoder)
FACTORY_CREATE_ENCODER(AACEncoder)
FACTORY_CREATE_ENCODER(AVCEncoder)
FACTORY_CREATE_ENCODER(M4vH263Encoder)

static sp<MediaSource> InstantiateSoftwareEncoder(
        const char *name, const sp<MediaSource> &source,
        const sp<MetaData> &meta) {
    struct FactoryInfo {
        const char *name;
        sp<MediaSource> (*CreateFunc)(const sp<MediaSource> &, const sp<MetaData> &);
    };

    static const FactoryInfo kFactoryInfo[] = {
        FACTORY_REF(AMRNBEncoder)
        FACTORY_REF(AMRWBEncoder)
        FACTORY_REF(AACEncoder)
        FACTORY_REF(AVCEncoder)
        FACTORY_REF(M4vH263Encoder)
    };
    for (size_t i = 0;
         i < sizeof(kFactoryInfo) / sizeof(kFactoryInfo[0]); ++i) {
        if (!strcmp(name, kFactoryInfo[i].name)) {
            return (*kFactoryInfo[i].CreateFunc)(source, meta);
        }
    }

    return NULL;
}

static sp<MediaSource> InstantiateSoftwareCodec(
        const char *name, const sp<MediaSource> &source) {
    struct FactoryInfo {
        const char *name;
        sp<MediaSource> (*CreateFunc)(const sp<MediaSource> &);
    };

    static const FactoryInfo kFactoryInfo[] = {
        FACTORY_REF(MP3Decoder)
        FACTORY_REF(AMRNBDecoder)
        FACTORY_REF(AMRWBDecoder)
        FACTORY_REF(AACDecoder)
        FACTORY_REF(AVCDecoder)
        FACTORY_REF(G711Decoder)
        FACTORY_REF(M4vH263Decoder)
        FACTORY_REF(VorbisDecoder)
        FACTORY_REF(VPXDecoder)
    };
    for (size_t i = 0;
         i < sizeof(kFactoryInfo) / sizeof(kFactoryInfo[0]); ++i) {
        if (!strcmp(name, kFactoryInfo[i].name)) {
            if (!strcmp(name, "VPXDecoder")) {
                return new ThreadedSource(
                        (*kFactoryInfo[i].CreateFunc)(source));
            }
            return (*kFactoryInfo[i].CreateFunc)(source);
        }
    }

    return NULL;
}

#undef FACTORY_REF
#undef FACTORY_CREATE

static const CodecInfo kDecoderInfo[] = {
    { MEDIA_MIMETYPE_IMAGE_JPEG, "OMX.TI.JPEG.decode" },
    { MEDIA_MIMETYPE_AUDIO_MPEG, "OMX.qcom.audio.decoder.mp3" },
//    { MEDIA_MIMETYPE_AUDIO_MPEG, "OMX.TI.MP3.decode" },
    { MEDIA_MIMETYPE_AUDIO_MPEG, "MP3Decoder" },
//    { MEDIA_MIMETYPE_AUDIO_MPEG, "OMX.PV.mp3dec" },
    { MEDIA_MIMETYPE_AUDIO_AMR_NB, "OMX.qcom.audio.decoder.amrnb" },
//    { MEDIA_MIMETYPE_AUDIO_AMR_NB, "OMX.TI.AMR.decode" },
    { MEDIA_MIMETYPE_AUDIO_AMR_NB, "AMRNBDecoder" },
//    { MEDIA_MIMETYPE_AUDIO_AMR_NB, "OMX.PV.amrdec" },
    { MEDIA_MIMETYPE_AUDIO_AMR_WB, "OMX.qcom.audio.decoder.amrwb" },
    { MEDIA_MIMETYPE_AUDIO_AMR_WB, "OMX.TI.WBAMR.decode" },
    { MEDIA_MIMETYPE_AUDIO_AMR_WB, "AMRWBDecoder" },
//    { MEDIA_MIMETYPE_AUDIO_AMR_WB, "OMX.PV.amrdec" },
#ifdef USE_AAC_HW_DEC
    { MEDIA_MIMETYPE_AUDIO_AAC, "OMX.qcom.audio.decoder.aac" },
#endif
    { MEDIA_MIMETYPE_AUDIO_AAC, "OMX.TI.AAC.decode" },
    { MEDIA_MIMETYPE_AUDIO_AAC, "AACDecoder" },
//    { MEDIA_MIMETYPE_AUDIO_AAC, "OMX.PV.aacdec" },
    { MEDIA_MIMETYPE_AUDIO_G711_ALAW, "G711Decoder" },
    { MEDIA_MIMETYPE_AUDIO_G711_MLAW, "G711Decoder" },
//    { MEDIA_MIMETYPE_VIDEO_MPEG4, "OMX.qcom.7x30.video.decoder.mpeg4" },
    { MEDIA_MIMETYPE_VIDEO_MPEG4, "OMX.qcom.video.decoder.mpeg4" },
    { MEDIA_MIMETYPE_VIDEO_MPEG4, "OMX.TI.Video.Decoder" },
    { MEDIA_MIMETYPE_VIDEO_MPEG4, "OMX.SEC.MPEG4.Decoder" },
    { MEDIA_MIMETYPE_VIDEO_MPEG4, "M4vH263Decoder" },
//    { MEDIA_MIMETYPE_VIDEO_MPEG4, "OMX.PV.mpeg4dec" },
//    { MEDIA_MIMETYPE_VIDEO_H263, "OMX.qcom.7x30.video.decoder.h263" },
    { MEDIA_MIMETYPE_VIDEO_H263, "OMX.qcom.video.decoder.h263" },
    { MEDIA_MIMETYPE_VIDEO_H263, "OMX.SEC.H263.Decoder" },
    { MEDIA_MIMETYPE_VIDEO_H263, "M4vH263Decoder" },
//    { MEDIA_MIMETYPE_VIDEO_H263, "OMX.PV.h263dec" },
//    { MEDIA_MIMETYPE_VIDEO_AVC, "OMX.qcom.7x30.video.decoder.avc" },
    { MEDIA_MIMETYPE_VIDEO_AVC, "OMX.qcom.video.decoder.avc" },
    { MEDIA_MIMETYPE_VIDEO_AVC, "OMX.TI.Video.Decoder" },
    { MEDIA_MIMETYPE_VIDEO_AVC, "OMX.SEC.AVC.Decoder" },
    { MEDIA_MIMETYPE_VIDEO_AVC, "AVCDecoder" },
//    { MEDIA_MIMETYPE_VIDEO_AVC, "OMX.PV.avcdec" },
    { MEDIA_MIMETYPE_AUDIO_VORBIS, "VorbisDecoder" },
    { MEDIA_MIMETYPE_VIDEO_VPX, "OMX.qcom.video.decoder.vp8" },
    { MEDIA_MIMETYPE_VIDEO_VPX, "VPXDecoder" },
    { MEDIA_MIMETYPE_VIDEO_DIVX, "OMX.qcom.video.decoder.divx"},
    { MEDIA_MIMETYPE_VIDEO_DIVX311, "OMX.qcom.video.decoder.divx311"},
    { MEDIA_MIMETYPE_VIDEO_DIVX4, "OMX.qcom.video.decoder.divx4"},
    { MEDIA_MIMETYPE_AUDIO_AC3, "OMX.qcom.audio.decoder.ac3" },
    { MEDIA_MIMETYPE_VIDEO_SPARK,"OMX.qcom.video.decoder.spark"},
    { MEDIA_MIMETYPE_VIDEO_VP6,"OMX.qcom.video.decoder.vp"},
    { MEDIA_MIMETYPE_AUDIO_WMA, "OMX.qcom.audio.decoder.wma"},
    { MEDIA_MIMETYPE_AUDIO_WMA, "OMX.qcom.audio.decoder.wmaLossLess"},
    { MEDIA_MIMETYPE_AUDIO_WMA, "OMX.qcom.audio.decoder.wma10Pro"},
    { MEDIA_MIMETYPE_VIDEO_WMV, "OMX.qcom.video.decoder.vc1"},
    { MEDIA_MIMETYPE_AUDIO_QCELP, "OMX.qcom.audio.decoder.Qcelp13Hw"},
    { MEDIA_MIMETYPE_AUDIO_QCELP, "OMX.qcom.audio.decoder.Qcelp13"},
    { MEDIA_MIMETYPE_AUDIO_EVRC, "OMX.qcom.audio.decoder.evrchw" },
    { MEDIA_MIMETYPE_AUDIO_EVRC, "OMX.qcom.audio.decoder.evrc" },
    { MEDIA_MIMETYPE_VIDEO_MPEG2, "OMX.qcom.video.decoder.mpeg2" },
};

static const CodecInfo kEncoderInfo[] = {
    { MEDIA_MIMETYPE_AUDIO_AMR_NB, "OMX.TI.AMR.encode" },
    { MEDIA_MIMETYPE_AUDIO_AMR_NB, "AMRNBEncoder" },
    { MEDIA_MIMETYPE_AUDIO_AMR_WB, "OMX.TI.WBAMR.encode" },
    { MEDIA_MIMETYPE_AUDIO_AMR_WB, "AMRWBEncoder" },
    { MEDIA_MIMETYPE_AUDIO_AAC, "OMX.qcom.audio.encoder.aac" },
    { MEDIA_MIMETYPE_AUDIO_AAC, "OMX.TI.AAC.encode" },
    { MEDIA_MIMETYPE_AUDIO_AAC, "AACEncoder" },
//    { MEDIA_MIMETYPE_AUDIO_AAC, "OMX.PV.aacenc" },
    { MEDIA_MIMETYPE_AUDIO_EVRC,   "OMX.qcom.audio.encoder.evrc" },
    { MEDIA_MIMETYPE_AUDIO_QCELP,  "OMX.qcom.audio.encoder.qcelp13" },
//    { MEDIA_MIMETYPE_VIDEO_MPEG4, "OMX.qcom.7x30.video.encoder.mpeg4" },
    { MEDIA_MIMETYPE_VIDEO_MPEG4, "OMX.qcom.video.encoder.mpeg4" },
    { MEDIA_MIMETYPE_VIDEO_MPEG4, "OMX.TI.Video.encoder" },
    { MEDIA_MIMETYPE_VIDEO_MPEG4, "OMX.SEC.MPEG4.Encoder" },
    { MEDIA_MIMETYPE_VIDEO_MPEG4, "M4vH263Encoder" },
//    { MEDIA_MIMETYPE_VIDEO_MPEG4, "OMX.PV.mpeg4enc" },
//    { MEDIA_MIMETYPE_VIDEO_H263, "OMX.qcom.7x30.video.encoder.h263" },
    { MEDIA_MIMETYPE_VIDEO_H263, "OMX.qcom.video.encoder.h263" },
    { MEDIA_MIMETYPE_VIDEO_H263, "OMX.TI.Video.encoder" },
    { MEDIA_MIMETYPE_VIDEO_H263, "OMX.SEC.H263.Encoder" },
    { MEDIA_MIMETYPE_VIDEO_H263, "M4vH263Encoder" },
//    { MEDIA_MIMETYPE_VIDEO_H263, "OMX.PV.h263enc" },
//    { MEDIA_MIMETYPE_VIDEO_AVC, "OMX.qcom.7x30.video.encoder.avc" },
    { MEDIA_MIMETYPE_VIDEO_AVC, "OMX.qcom.video.encoder.avc" },
    { MEDIA_MIMETYPE_VIDEO_AVC, "OMX.TI.Video.encoder" },
    { MEDIA_MIMETYPE_VIDEO_AVC, "OMX.SEC.AVC.Encoder" },
    { MEDIA_MIMETYPE_VIDEO_AVC, "AVCEncoder" },
//    { MEDIA_MIMETYPE_VIDEO_AVC, "OMX.PV.avcenc" },
};

#undef OPTIONAL

#define CODEC_LOGI(x, ...) LOGI("[%s] "x, mComponentName, ##__VA_ARGS__)
#define CODEC_LOGV(x, ...) LOGV("[%s] "x, mComponentName, ##__VA_ARGS__)
#define CODEC_LOGE(x, ...) LOGE("[%s] "x, mComponentName, ##__VA_ARGS__)

struct OMXCodecObserver : public BnOMXObserver {
    OMXCodecObserver() {
    }

    void setCodec(const sp<OMXCodec> &target) {
        mTarget = target;
    }

    // from IOMXObserver
    virtual void onMessage(const omx_message &msg) {
        sp<OMXCodec> codec = mTarget.promote();

        if (codec.get() != NULL) {
            Mutex::Autolock autoLock(codec->mLock);
            codec->on_message(msg);
            codec.clear();
        }
    }

    virtual void registerBuffers(const sp<IMemoryHeap> &mem) {
        sp<OMXCodec> codec = mTarget.promote();
        if (codec.get() != NULL) {
            codec->registerBuffers(mem);
        }
    }

protected:
    virtual ~OMXCodecObserver() {}

private:
    wp<OMXCodec> mTarget;

    OMXCodecObserver(const OMXCodecObserver &);
    OMXCodecObserver &operator=(const OMXCodecObserver &);
};

char *HwAacRoles[]={
"OMX.qcom.audio.decoder.multiaac",
"OMX.qcom.audio.decoder.aac",
};

static const char *GetCodec(const CodecInfo *info, size_t numInfos,
                            const char *mime, int index) {
    CHECK(index >= 0);
    char value[PROPERTY_VALUE_MAX];

    for(size_t i = 0; i < numInfos; ++i) {
        if (!strcasecmp(mime, info[i].mime)) {
            if (index == 0) {

                if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AAC)){
                    if (property_get("media.aaccodectype", value, NULL)){
                        LOGE("using h/w aac decoder");
                        if((atoi(value) >= 0 )&& (atoi(value) < 2) )
                            return HwAacRoles[atoi(value)];
                        else
                            LOGE("media.aaccodectype value not supported");
                    }
                }

                return info[i].codec;
            }

            --index;
        }
    }

    return NULL;
}

enum {
    kAVCProfileBaseline      = 0x42,
    kAVCProfileMain          = 0x4d,
    kAVCProfileExtended      = 0x58,
    kAVCProfileHigh          = 0x64,
    kAVCProfileHigh10        = 0x6e,
    kAVCProfileHigh422       = 0x7a,
    kAVCProfileHigh444       = 0xf4,
    kAVCProfileCAVLC444Intra = 0x2c
};

static const char *AVCProfileToString(uint8_t profile) {
    switch (profile) {
        case kAVCProfileBaseline:
            return "Baseline";
        case kAVCProfileMain:
            return "Main";
        case kAVCProfileExtended:
            return "Extended";
        case kAVCProfileHigh:
            return "High";
        case kAVCProfileHigh10:
            return "High 10";
        case kAVCProfileHigh422:
            return "High 422";
        case kAVCProfileHigh444:
            return "High 444";
        case kAVCProfileCAVLC444Intra:
            return "CAVLC 444 Intra";
        default:   return "Unknown";
    }
}

template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}

static bool IsSoftwareCodec(const char *componentName) {
    if (!strncmp("OMX.PV.", componentName, 7)) {
        return true;
    }

    return false;
}

// A sort order in which non-OMX components are first,
// followed by software codecs, i.e. OMX.PV.*, followed
// by all the others.
static int CompareSoftwareCodecsFirst(
        const String8 *elem1, const String8 *elem2) {
    bool isNotOMX1 = strncmp(elem1->string(), "OMX.", 4);
    bool isNotOMX2 = strncmp(elem2->string(), "OMX.", 4);

    if (isNotOMX1) {
        if (isNotOMX2) { return 0; }
        return -1;
    }
    if (isNotOMX2) {
        return 1;
    }

    bool isSoftwareCodec1 = IsSoftwareCodec(elem1->string());
    bool isSoftwareCodec2 = IsSoftwareCodec(elem2->string());

    if (isSoftwareCodec1) {
        if (isSoftwareCodec2) { return 0; }
        return -1;
    }

    if (isSoftwareCodec2) {
        return 1;
    }

    return 0;
}

// static
uint32_t OMXCodec::getComponentQuirks(
        const char *componentName, bool isEncoder) {
    uint32_t quirks = 0;
    char mDeviceName[128];
    property_get("ro.board.platform",mDeviceName,"0");

    if (!strcmp(componentName, "OMX.PV.avcdec")) {
        quirks |= kWantsNALFragments;
    }
    if (!strcmp(componentName, "OMX.TI.MP3.decode")) {
        quirks |= kNeedsFlushBeforeDisable;
        quirks |= kDecoderLiesAboutNumberOfChannels;
    }
    if (!strcmp(componentName, "OMX.TI.AAC.decode")) {
        quirks |= kNeedsFlushBeforeDisable;
        quirks |= kRequiresFlushCompleteEmulation;
        quirks |= kSupportsMultipleFramesPerInputBuffer;
    }
#ifdef USE_AAC_HW_DEC
    if (!strcmp(componentName, "OMX.qcom.audio.decoder.aac")) {
        quirks |= kRequiresAllocateBufferOnInputPorts;
        quirks |= kRequiresAllocateBufferOnOutputPorts;
    }
#endif
    if (!strcmp(componentName, "OMX.qcom.audio.encoder.evrc")) {
        quirks |= kRequiresAllocateBufferOnInputPorts;
        quirks |= kRequiresAllocateBufferOnOutputPorts;
    }

    if (!strcmp(componentName, "OMX.qcom.audio.encoder.qcelp13")) {
        quirks |= kRequiresAllocateBufferOnInputPorts;
        quirks |= kRequiresAllocateBufferOnOutputPorts;
    }

    if (!strcmp(componentName, "OMX.qcom.audio.encoder.aac")) {
        quirks |= kRequiresAllocateBufferOnInputPorts;
        quirks |= kRequiresAllocateBufferOnOutputPorts;
    }

    if (!strncmp(componentName, "OMX.qcom.video.encoder.", 23)) {
        quirks |= kRequiresLoadedToIdleAfterAllocation;
        //quirks |= kRequiresAllocateBufferOnInputPorts;
        quirks |= kRequiresAllocateBufferOnOutputPorts;
        quirks |= kAvoidMemcopyInputRecordingFrames;

        if (strncmp(mDeviceName, "msm7627", 7)) {
            quirks |= kBFrameFlagInExtensions;
            quirks |= kRequiresEOSMessage;
        }
        if (!strncmp(componentName, "OMX.qcom.video.encoder.avc", 26)) {

            // The AVC encoder advertises the size of output buffers
            // based on the input video resolution and assumes
            // the worst/least compression ratio is 0.5. It is found that
            // sometimes, the output buffer size is larger than
            // size advertised by the encoder.
            //quirks |= kRequiresLargerEncoderOutputBuffer;
        }
    }
    if (!strncmp(componentName, "OMX.qcom.7x30.video.encoder.", 28)) {
    }
    if (!strncmp(componentName, "OMX.qcom.video.decoder.", 23)) {
        quirks |= kRequiresAllocateBufferOnInputPorts;
        quirks |= kRequiresAllocateBufferOnOutputPorts;
        quirks |= kDefersOutputBufferAllocation;
        quirks |= kDoesNotRequireMemcpyOnOutputPort;
    }
    if (!strncmp(componentName, "OMX.qcom.7x30.video.decoder.", 28)) {
        quirks |= kRequiresAllocateBufferOnInputPorts;
        quirks |= kRequiresAllocateBufferOnOutputPorts;
        quirks |= kDefersOutputBufferAllocation;
    }

    if (!strncmp(componentName, "OMX.TI.", 7)) {
        // Apparently I must not use OMX_UseBuffer on either input or
        // output ports on any of the TI components or quote:
        // "(I) may have unexpected problem (sic) which can be timing related
        //  and hard to reproduce."

        quirks |= kRequiresAllocateBufferOnInputPorts;
        quirks |= kRequiresAllocateBufferOnOutputPorts;
        if (!strncmp(componentName, "OMX.TI.Video.encoder", 20)) {
            quirks |= kAvoidMemcopyInputRecordingFrames;
        }
    }

    if (!strcmp(componentName, "OMX.TI.Video.Decoder")) {
        quirks |= kInputBufferSizesAreBogus;
    }

    if (!strncmp(componentName, "OMX.SEC.", 8) && !isEncoder) {
        // These output buffers contain no video data, just some
        // opaque information that allows the overlay to display their
        // contents.
        quirks |= kOutputBuffersAreUnreadable;
    }

    if (!strncmp(componentName, "OMX.SEC.", 8) && isEncoder) {
        // These input buffers contain meta data (for instance,
        // information helps locate the actual YUV data, or
        // the physical address of the YUV data).
        quirks |= kStoreMetaDataInInputVideoBuffers;
    }
    if(!strcmp(componentName,"OMX.qcom.audio.decoder.ac3")) {
        LOGV("AC3 enabling allocate buffer on input and output ports");
        quirks |= kRequiresAllocateBufferOnInputPorts;
        quirks |= kRequiresAllocateBufferOnOutputPorts;
    }

    if (!strcmp(componentName, "OMX.qcom.audio.decoder.wma")) {
        quirks |= kRequiresWMAProComponent;
    }

    return quirks;
}

// static
void OMXCodec::findMatchingCodecs(
        const char *mime,
        bool createEncoder, const char *matchComponentName,
        uint32_t flags,
        Vector<String8> *matchingCodecs) {
    matchingCodecs->clear();

    for (int index = 0;; ++index) {
        const char *componentName;

        if (createEncoder) {
            componentName = GetCodec(
                    kEncoderInfo,
                    sizeof(kEncoderInfo) / sizeof(kEncoderInfo[0]),
                    mime, index);
        } else {
            componentName = GetCodec(
                    kDecoderInfo,
                    sizeof(kDecoderInfo) / sizeof(kDecoderInfo[0]),
                    mime, index);
        }

        if (!componentName) {
            break;
        }

        // If a specific codec is requested, skip the non-matching ones.
        if (matchComponentName && strcmp(componentName, matchComponentName)) {
            continue;
        }

        matchingCodecs->push(String8(componentName));
    }

    if (flags & kPreferSoftwareCodecs) {
        matchingCodecs->sort(CompareSoftwareCodecsFirst);
    }
}

// static
sp<MediaSource> OMXCodec::Create(
        const sp<IOMX> &omx,
        const sp<MetaData> &meta, bool createEncoder,
        const sp<MediaSource> &source,
        const char *matchComponentName,
        uint32_t flags) {
    const char *mime;
    bool success = meta->findCString(kKeyMIMEType, &mime);
    CHECK(success);

    Vector<String8> matchingCodecs;
    findMatchingCodecs(
            mime, createEncoder, matchComponentName, flags, &matchingCodecs);

    if (matchingCodecs.isEmpty()) {
        return NULL;
    }

    sp<OMXCodecObserver> observer = new OMXCodecObserver;
    IOMX::node_id node = 0;

    const char *componentName;
    for (size_t i = 0; i < matchingCodecs.size(); ++i) {
        componentName = matchingCodecs[i].string();

        sp<MediaSource> softwareCodec = createEncoder?
            InstantiateSoftwareEncoder(componentName, source, meta):
            InstantiateSoftwareCodec(componentName, source);

        if (softwareCodec != NULL) {
            LOGV("Successfully allocated software codec '%s'", componentName);

            return softwareCodec;
        }

        LOGV("Attempting to allocate OMX node '%s'", componentName);

        uint32_t quirks = getComponentQuirks(componentName, createEncoder);

        if(quirks & kRequiresWMAProComponent)
        {
           int32_t version;
           CHECK(meta->findInt32(kKeyWMAVersion, &version));
           if(version==kTypeWMA)
           {
              componentName = "OMX.qcom.audio.decoder.wma";
           }
           else if(version==kTypeWMAPro)
           {
              componentName= "OMX.qcom.audio.decoder.wma10Pro";
           }
           else if(version==kTypeWMALossLess)
           {
              componentName= "OMX.qcom.audio.decoder.wmaLossLess";
           }
        }
#ifdef USE_AAC_HW_DEC
        int aacformattype = 0;
        int aacLTPType = 0;
        sp<MetaData> metadata = source->getFormat();
        metadata->findInt32(kkeyAacFormatAdif, &aacformattype);
        metadata->findInt32(kkeyAacFormatLtp, &aacLTPType);

        if (aacformattype == true || aacLTPType == true)  {
            LOGE("This is ADIF/LTP clip , so using sw decoder ");
            componentName= "AACDecoder";
        }
#endif

        if (!createEncoder
                && (quirks & kOutputBuffersAreUnreadable)
                && (flags & kClientNeedsFramebuffer)) {
            if (strncmp(componentName, "OMX.SEC.", 8)) {
                // For OMX.SEC.* decoders we can enable a special mode that
                // gives the client access to the framebuffer contents.

                LOGW("Component '%s' does not give the client access to "
                     "the framebuffer contents. Skipping.",
                     componentName);

                continue;
            }
        }

        status_t err = omx->allocateNode(componentName, observer, &node);
        if (err == OK) {
            LOGV("Successfully allocated OMX node '%s'", componentName);

            sp<OMXCodec> codec = new OMXCodec(
                    omx, node, quirks,
                    createEncoder, mime, componentName,
                    source);

            observer->setCodec(codec);

            codec->parseFlags(flags);

            err = codec->configureCodec(meta, flags);

            if (err == OK) {
                return codec;
            }

            LOGV("Failed to configure codec '%s'", componentName);
        }
    }

    return NULL;
}

status_t OMXCodec::configureCodec(const sp<MetaData> &meta, uint32_t flags) {
    if (!(flags & kIgnoreCodecSpecificData)) {
        uint32_t type;
        const void *data;
        size_t size;
        if (meta->findData(kKeyESDS, &type, &data, &size)) {
            ESDS esds((const char *)data, size);
            CHECK_EQ(esds.InitCheck(), OK);

            const void *codec_specific_data;
            size_t codec_specific_data_size;
            esds.getCodecSpecificInfo(
                    &codec_specific_data, &codec_specific_data_size);

            addCodecSpecificData(
                    codec_specific_data, codec_specific_data_size);
        } else if (meta->findData(kKeyAVCC, &type, &data, &size)) {
            // Parse the AVCDecoderConfigurationRecord

            const uint8_t *ptr = (const uint8_t *)data;

            CHECK(size >= 7);
            CHECK_EQ(ptr[0], 1);  // configurationVersion == 1
            uint8_t profile = ptr[1];
            uint8_t level = ptr[3];

            // There is decodable content out there that fails the following
            // assertion, let's be lenient for now...
            // CHECK((ptr[4] >> 2) == 0x3f);  // reserved

            size_t lengthSize = 1 + (ptr[4] & 3);

            // commented out check below as H264_QVGA_500_NO_AUDIO.3gp
            // violates it...
            // CHECK((ptr[5] >> 5) == 7);  // reserved

            size_t numSeqParameterSets = ptr[5] & 31;

            SpsInfo info;
            if ( parseSps( size - 1, ptr + 1, &info ) == OK ) {
                mSPSParsed = true;
                if (info.mInterlaced)
                    mInterlaceFormatDetected = true;
            }
            else {
                LOGI("ParseSPS failed to find if content is interlaced");
                mSPSParsed = false;
                mInterlaceFormatDetected = false;
            }

            ptr += 6;
            size -= 6;

            for (size_t i = 0; i < numSeqParameterSets; ++i) {
                CHECK(size >= 2);
                size_t length = U16_AT(ptr);

                ptr += 2;
                size -= 2;

                CHECK(size >= length);

                addCodecSpecificData(ptr, length);

                ptr += length;
                size -= length;
            }

            CHECK(size >= 1);
            size_t numPictureParameterSets = *ptr;
            ++ptr;
            --size;

            for (size_t i = 0; i < numPictureParameterSets; ++i) {
                CHECK(size >= 2);
                size_t length = U16_AT(ptr);

                ptr += 2;
                size -= 2;

                CHECK(size >= length);

                addCodecSpecificData(ptr, length);

                ptr += length;
                size -= length;
            }

            CODEC_LOGV(
                    "AVC profile = %d (%s), level = %d",
                    (int)profile, AVCProfileToString(profile), level);

            if (!strcmp(mComponentName, "OMX.TI.Video.Decoder")
                && (profile != kAVCProfileBaseline || level > 30)) {
                // This stream exceeds the decoder's capabilities. The decoder
                // does not handle this gracefully and would clobber the heap
                // and wreak havoc instead...

                LOGE("Profile and/or level exceed the decoder's capabilities.");
                return ERROR_UNSUPPORTED;
            }
        } else if (meta->findData(kKeyRawCodecSpecificData, &type, &data, &size)) {
            LOGV("OMXCodec::configureCodec found kKeyRawCodecSpecificData of size %d\n", size);
            addCodecSpecificData(data, size);
        }
    }

    if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_DIVX, mMIME) || !strcasecmp(MEDIA_MIMETYPE_VIDEO_DIVX4, mMIME)) {
        LOGV("Setting the QOMX_VIDEO_PARAM_DIVXTYPE params ");
        QOMX_VIDEO_PARAM_DIVXTYPE paramDivX;
        InitOMXParams(&paramDivX);
        paramDivX.nPortIndex = mIsEncoder ? kPortIndexOutput : kPortIndexInput;
        int32_t DivxVersion = 0;
        CHECK(meta->findInt32(kKeyDivXVersion,&DivxVersion));
        CODEC_LOGV("Divx Version Type %d\n",DivxVersion);

        if(DivxVersion == kTypeDivXVer_4) {
            paramDivX.eFormat = QOMX_VIDEO_DIVXFormat4;
        } else if(DivxVersion == kTypeDivXVer_5) {
            paramDivX.eFormat = QOMX_VIDEO_DIVXFormat5;
        } else if(DivxVersion == kTypeDivXVer_6) {
            paramDivX.eFormat = QOMX_VIDEO_DIVXFormat6;
        } else {
            paramDivX.eFormat = QOMX_VIDEO_DIVXFormatUnused;
        }

        paramDivX.eProfile = (QOMX_VIDEO_DIVXPROFILETYPE)0;//Not used for now.

        paramDivX.pDrmHandle = NULL;
        if (meta->findPointer(kKeyDivXDrm, &paramDivX.pDrmHandle) ) {
            if( paramDivX.pDrmHandle != NULL ) {
                LOGV("This DivX Clip is DRM encrypted, set the DRM handle ");
            }
            else {
                LOGV("This DivX Clip is not DRM encrypted ");
            }
        }
        status_t err =  mOMX->setParameter(mNode,
                         (OMX_INDEXTYPE)OMX_QcomIndexParamVideoDivx,
                         &paramDivX, sizeof(paramDivX));
        if (err!=OK) {
            return err;
        }
    }

    // Set params for divx311 and configure
    // decoder in frame by frame mode
    if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_DIVX311, mMIME)) {
        CODEC_LOGV("Setting the QOMX_VIDEO_PARAM_DIVX311TYPE params ");
        QOMX_VIDEO_PARAM_DIVXTYPE paramDivX;
        InitOMXParams(&paramDivX);
        paramDivX.nPortIndex = mIsEncoder ? kPortIndexOutput : kPortIndexInput;
        int32_t DivxVersion = 0;
        CHECK(meta->findInt32(kKeyDivXVersion,&DivxVersion));
        CODEC_LOGV("Divx Version Type %d\n",DivxVersion);

        if(DivxVersion == kTypeDivXVer_3_11 ) {
            paramDivX.eFormat = QOMX_VIDEO_DIVXFormat311;
            paramDivX.eProfile = (QOMX_VIDEO_DIVXPROFILETYPE)0;//Not used for now.
            paramDivX.pDrmHandle = NULL;
            if (meta->findPointer(kKeyDivXDrm, &paramDivX.pDrmHandle) ) {
                if( paramDivX.pDrmHandle != NULL ) {
                    CODEC_LOGV("This DivX Clip is DRM encrypted, set the DRM handle ");
                }
                else {
                    CODEC_LOGV("This DivX Clip is not DRM encrypted ");
                }
            }

            status_t err =  mOMX->setParameter(mNode,
                             (OMX_INDEXTYPE)OMX_QcomIndexParamVideoDivx,
                             &paramDivX, sizeof(paramDivX));
            if (err!=OK) {
                CODEC_LOGE("Set params DIVX error");
                return err;
            }

            CODEC_LOGV("kTypeDivXVer_3_11 - set frame by frame mode");
            OMX_QCOM_PARAM_PORTDEFINITIONTYPE portdef;
            portdef.nSize = sizeof(OMX_QCOM_PARAM_PORTDEFINITIONTYPE);
            portdef.nPortIndex = 0; //Input port.
            portdef.nMemRegion = OMX_QCOM_MemRegionInvalid;
            portdef.nCacheAttr = OMX_QCOM_CacheAttrNone;
            portdef.nFramePackingFormat = OMX_QCOM_FramePacking_OnlyOneCompleteFrame;
            err = mOMX->setParameter(mNode, (OMX_INDEXTYPE)OMX_QcomIndexPortDefn, &portdef, sizeof(OMX_QCOM_PARAM_PORTDEFINITIONTYPE));
            if (err != OK) {
                CODEC_LOGE("DIVX 311 set frame by frame mode error");
                return err;
            }
        }
    }

    int32_t bitRate = 0;
    if (mIsEncoder) {
        CHECK(meta->findInt32(kKeyBitRate, &bitRate));
    }

    if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_AMR_NB, mMIME)) {
        setAMRFormat(false /* isWAMR */, bitRate);
    }
    if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_AMR_WB, mMIME)) {
        setAMRFormat(true /* isWAMR */, bitRate);
    }
    if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_AAC, mMIME)) {
        int32_t numChannels, sampleRate;
        CHECK(meta->findInt32(kKeyChannelCount, &numChannels));
        CHECK(meta->findInt32(kKeySampleRate, &sampleRate));
#ifdef USE_AAC_HW_DEC
        uint32_t type;
        const void *data;
        size_t size;
	    if (meta->findData(kKeyAacCodecSpecificData, &type, &data, &size)) {
                LOGV("OMXCodec::configureCodec found kKeyAacCodecSpecificData of size %d\n", size);
                addCodecSpecificData(data, size);
        }
#endif
        setAACFormat(numChannels, sampleRate, bitRate);
    }

    if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_AC3, mMIME)) {
        return BAD_TYPE;
        /*int32_t numChannels, sampleRate;
        CHECK(meta->findInt32(kKeyChannelCount, &numChannels));
        CHECK(meta->findInt32(kKeySampleRate, &sampleRate));
        setAC3Format(numChannels, sampleRate);*/
    }

    if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_EVRC, mMIME)) {
        int32_t numChannels, sampleRate;
        CHECK(meta->findInt32(kKeyChannelCount, &numChannels));
        CHECK(meta->findInt32(kKeySampleRate, &sampleRate));
        setEVRCFormat(numChannels, sampleRate, bitRate);
    }

    if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_QCELP, mMIME)) {
        int32_t numChannels, sampleRate;
        CHECK(meta->findInt32(kKeyChannelCount, &numChannels));
        CHECK(meta->findInt32(kKeySampleRate, &sampleRate));
        setQCELPFormat(numChannels, sampleRate, bitRate);
    }

     if(!strcasecmp(MEDIA_MIMETYPE_VIDEO_SPARK, mMIME)) {
         LOGV("Setting the QOMX_VIDEO_PARAM_SPARKTYPE params ");
         QOMX_VIDEO_PARAM_SPARKTYPE paramSpark;

         InitOMXParams(&paramSpark);
         paramSpark.nPortIndex = kPortIndexInput;
         paramSpark.eFormat = QOMX_VIDEO_SparkFormat1;

         status_t err = mOMX->setParameter(mNode,
                          (OMX_INDEXTYPE)OMX_QcomIndexParamVideoSpark,
                          &paramSpark, sizeof(paramSpark));
         if (err != OK) {
              return err;
         }
     }
     if(!strcasecmp(MEDIA_MIMETYPE_VIDEO_VP6, mMIME)) {
         LOGV("Setting the QOMX_VIDEO_PARAM_VPTYPE params ");
         QOMX_VIDEO_PARAM_VPTYPE paramVp;

         InitOMXParams(&paramVp);
         paramVp.nPortIndex = kPortIndexInput;
         /*Right now Supporting only VP6 and Advanced Profile*/
         paramVp.eFormat =  QOMX_VIDEO_VPFormat6;
         paramVp.eProfile = QOMX_VIDEO_VPProfileAdvanced;

          status_t err = mOMX->setParameter(mNode,
                           (OMX_INDEXTYPE)OMX_QcomIndexParamVideoVp,
                           &paramVp, sizeof(paramVp));
         if (err != OK) {
             return err;
         }
     }

    if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_WMA, mMIME))  {
        status_t err = setWMAFormat(meta);
        if(err!=OK){
           return err;
        }
    }
    if(!strcasecmp(MEDIA_MIMETYPE_VIDEO_WMV, mMIME)) {
        OMX_QCOM_PARAM_PORTDEFINITIONTYPE portdef;
        portdef.nSize = sizeof(OMX_QCOM_PARAM_PORTDEFINITIONTYPE);
        portdef.nPortIndex = 0; //Input port.
        portdef.nMemRegion = OMX_QCOM_MemRegionInvalid;
        portdef.nCacheAttr = OMX_QCOM_CacheAttrNone;
        int32_t WMVProfile = 0;
        CHECK(meta->findInt32(kKeyWMVProfile,&WMVProfile));

        if(WMVProfile == kTypeWMVAdvance)
        {
            portdef.nFramePackingFormat = OMX_QCOM_FramePacking_Arbitrary;
            LOGV("Setting decoder in Arbitary Mode --- ADVANCE PROFILE");
        }
        else
        {
            portdef.nFramePackingFormat = OMX_QCOM_FramePacking_OnlyOneCompleteFrame;
            LOGV("Setting decoder in Frame-By-Frame Mode --- SIMPLE Profile");
        }

        char value[PROPERTY_VALUE_MAX];
        status_t err = mOMX->setParameter(mNode, (OMX_INDEXTYPE)OMX_QcomIndexPortDefn, &portdef, sizeof(OMX_QCOM_PARAM_PORTDEFINITIONTYPE));
        if (!property_get("ro.board.platform", value, "1")
            || !strcmp(value, "msm7627_surf") || !strcmp(value, "msm7627_ffa")
            || !strcmp(value, "msm7627_7x_surf") || !strcmp(value, "msm7627_7x_ffa")
            || !strcmp(value, "msm7627a")
            || !strcmp(value, "msm7625_surf") || !strcmp(value, "msm7625_ffa"))
        {
            LOGE("OMX_QCOM_FramePacking_OnlyOneCompleteFrame not supported by component err: %d", err);
        }
        else
            if(err!=OK){
               return err;
            }
    }
    if (!strncasecmp(mMIME, "video/", 6)) {

        if (mThumbnailMode) {
            LOGV("Enabling thumbnail mode.");
            QOMX_ENABLETYPE enableType;
            OMX_INDEXTYPE indexType;

            status_t err = mOMX->getExtensionIndex(
                mNode, OMX_QCOM_INDEX_PARAM_VIDEO_SYNCFRAMEDECODINGMODE, &indexType);

            CHECK_EQ(err, OK);

            enableType.bEnable = OMX_TRUE;

            err = mOMX->setParameter(
                    mNode, indexType, &enableType, sizeof(enableType));
            CHECK_EQ(err, OK);

            LOGV("Thumbnail mode enabled.");
        }

        if (mIsEncoder) {
            setVideoInputFormat(mMIME, meta);
        } else {
            int32_t width, height;
            bool success = meta->findInt32(kKeyWidth, &width);
            success = success && meta->findInt32(kKeyHeight, &height);
            CHECK(success);
            status_t err = setVideoOutputFormat(
                    mMIME, width, height);

            if (err != OK) {
                return err;
            }
            if ((flags & kLocalFileMode) &&
                (strcmp(mMIME, MEDIA_MIMETYPE_VIDEO_AVC) == 0) &&
                (mSPSParsed) &&
                (!mInterlaceFormatDetected)){
                CODEC_LOGI("Local file playback frame by frame mode enabled");
                OMX_QCOM_PARAM_PORTDEFINITIONTYPE portFmt;
                portFmt.nPortIndex = kPortIndexInput;
                portFmt.nFramePackingFormat = OMX_QCOM_FramePacking_OnlyOneCompleteFrame;
                err = mOMX->setParameter(
                        mNode, (OMX_INDEXTYPE)OMX_QcomIndexPortDefn, (void *)&portFmt, sizeof(portFmt));
                if(err != OK) {
                    LOGW("Failed to set frame packing format on component");
                }
            }
        }
    }

    if (!strcasecmp(mMIME, MEDIA_MIMETYPE_IMAGE_JPEG)
        && !strcmp(mComponentName, "OMX.TI.JPEG.decode")) {
        OMX_COLOR_FORMATTYPE format =
            OMX_COLOR_Format32bitARGB8888;
            // OMX_COLOR_FormatYUV420PackedPlanar;
            // OMX_COLOR_FormatCbYCrY;
            // OMX_COLOR_FormatYUV411Planar;

        int32_t width, height;
        bool success = meta->findInt32(kKeyWidth, &width);
        success = success && meta->findInt32(kKeyHeight, &height);

        int32_t compressedSize;
        success = success && meta->findInt32(
                kKeyMaxInputSize, &compressedSize);

        CHECK(success);
        CHECK(compressedSize > 0);

        setImageOutputFormat(format, width, height);
        setJPEGInputFormat(width, height, (OMX_U32)compressedSize);
    }

    int32_t maxInputSize;
    if (meta->findInt32(kKeyMaxInputSize, &maxInputSize)) {
        setMinBufferSize(kPortIndexInput, (OMX_U32)maxInputSize);
    }

    if (!strcmp(mComponentName, "OMX.TI.AMR.encode")
        || !strcmp(mComponentName, "OMX.TI.WBAMR.encode")
        || !strcmp(mComponentName, "OMX.TI.AAC.encode")) {
        setMinBufferSize(kPortIndexOutput, 8192);  // XXX
    }

    if(mState == ERROR) {
        LOGE("Configure Codec failed with BAD_VALUE");
        return BAD_VALUE;
    }

    initOutputFormat(meta);

    if (!strncasecmp(mMIME, "audio/", 6)) {
        OMX_PARAM_SUSPENSIONPOLICYTYPE suspensionPolicy;
        // Suspension policy for the OMX component to honor the Power collapse (TCXO shutdown)
        // Whenever there is a power collapse, OMX component releases the hardware
        // resources and hence enabling TCXO shutdown, reducing power consumption.
        // Return value is ignored, since this is not mandated for all the OMX components.
        memset(&suspensionPolicy,0,sizeof(suspensionPolicy));
        suspensionPolicy.ePolicy = OMX_SuspensionEnabled;

        status_t err = mOMX->setParameter(mNode,
            OMX_IndexParamSuspensionPolicy, &suspensionPolicy, sizeof(suspensionPolicy));
        if ( err != OMX_ErrorNone ) {
            CODEC_LOGV("OMXCodec::configureCodec Problem setting suspension"
                "policy parameters in output port ");
        } else {
            CODEC_LOGV("OMXCodec::configureCodec SUCCESS setting suspension "
                "policy parameters in output port ");
        }
     }
    if ((flags & kClientNeedsFramebuffer)
            && !strncmp(mComponentName, "OMX.SEC.", 8)) {
        OMX_INDEXTYPE index;

        status_t err =
            mOMX->getExtensionIndex(
                    mNode,
                    "OMX.SEC.index.ThumbnailMode",
                    &index);

        if (err != OK) {
            return err;
        }

        OMX_BOOL enable = OMX_TRUE;
        err = mOMX->setConfig(mNode, index, &enable, sizeof(enable));

        if (err != OK) {
            CODEC_LOGE("setConfig('OMX.SEC.index.ThumbnailMode') "
                       "returned error 0x%08x", err);

            return err;
        }

        mQuirks &= ~kOutputBuffersAreUnreadable;
    }

    return OK;
}

void OMXCodec::setMinBufferSize(OMX_U32 portIndex, OMX_U32 size) {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = portIndex;

    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);

    if ((portIndex == kPortIndexInput && (mQuirks & kInputBufferSizesAreBogus))
        || (def.nBufferSize < size)) {
        def.nBufferSize = size;
    }

    err = mOMX->setParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    if(err != OK) {
        LOGE("Set Buffer Size Failed - error = %d",err);
        setState(ERROR);
        return;
    }

    err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);

    // Make sure the setting actually stuck.
    if (portIndex == kPortIndexInput
            && (mQuirks & kInputBufferSizesAreBogus)) {
        CHECK_EQ(def.nBufferSize, size);
    } else {
        CHECK(def.nBufferSize >= size);
    }
}

status_t OMXCodec::setVideoPortFormatType(
        OMX_U32 portIndex,
        OMX_VIDEO_CODINGTYPE compressionFormat,
        OMX_COLOR_FORMATTYPE colorFormat) {
    OMX_VIDEO_PARAM_PORTFORMATTYPE format;
    InitOMXParams(&format);
    format.nPortIndex = portIndex;
    format.nIndex = 0;
    bool found = false;

    OMX_U32 index = 0;
    for (;;) {
        format.nIndex = index;
        status_t err = mOMX->getParameter(
                mNode, OMX_IndexParamVideoPortFormat,
                &format, sizeof(format));

        if (err != OK) {
            return err;
        }

        // The following assertion is violated by TI's video decoder.
        // CHECK_EQ(format.nIndex, index);

#if 1
        CODEC_LOGV("portIndex: %ld, index: %ld, eCompressionFormat=%d eColorFormat=%d",
             portIndex,
             index, format.eCompressionFormat, format.eColorFormat);
#endif

        if (!strcmp("OMX.TI.Video.encoder", mComponentName)) {
            if (portIndex == kPortIndexInput
                    && colorFormat == format.eColorFormat) {
                // eCompressionFormat does not seem right.
                found = true;
                break;
            }
            if (portIndex == kPortIndexOutput
                    && compressionFormat == format.eCompressionFormat) {
                // eColorFormat does not seem right.
                found = true;
                break;
            }
        }

        if (format.eCompressionFormat == compressionFormat
            && format.eColorFormat == colorFormat) {
            found = true;
            break;
        }

        ++index;
    }

    if (!found) {
        return UNKNOWN_ERROR;
    }

    CODEC_LOGV("found a match.");
    status_t err = mOMX->setParameter(
            mNode, OMX_IndexParamVideoPortFormat,
            &format, sizeof(format));

    return err;
}

static size_t getFrameSize(
        OMX_COLOR_FORMATTYPE colorFormat, int32_t width, int32_t height) {
    switch (colorFormat) {
        case OMX_COLOR_FormatYCbYCr:
        case OMX_COLOR_FormatCbYCrY:
            return width * height * 2;

        case OMX_COLOR_FormatYUV420Planar:
        case OMX_COLOR_FormatYUV420SemiPlanar:
            return (width * height * 3) / 2;

        default:
            CHECK(!"Should not be here. Unsupported color format.");
            break;
    }
}

status_t OMXCodec::findTargetColorFormat(
        const sp<MetaData>& meta, OMX_COLOR_FORMATTYPE *colorFormat) {
    LOGV("findTargetColorFormat");
    CHECK(mIsEncoder);

    *colorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    int32_t targetColorFormat;
    if (meta->findInt32(kKeyColorFormat, &targetColorFormat)) {
        *colorFormat = (OMX_COLOR_FORMATTYPE) targetColorFormat;
    } else {
        if (!strcasecmp("OMX.TI.Video.encoder", mComponentName)) {
            *colorFormat = OMX_COLOR_FormatYCbYCr;
        }
    }

    // Check whether the target color format is supported.
    return isColorFormatSupported(*colorFormat, kPortIndexInput);
}

status_t OMXCodec::isColorFormatSupported(
        OMX_COLOR_FORMATTYPE colorFormat, int portIndex) {
    LOGV("isColorFormatSupported: %d", static_cast<int>(colorFormat));

    // Enumerate all the color formats supported by
    // the omx component to see whether the given
    // color format is supported.
    OMX_VIDEO_PARAM_PORTFORMATTYPE portFormat;
    InitOMXParams(&portFormat);
    portFormat.nPortIndex = portIndex;
    OMX_U32 index = 0;
    portFormat.nIndex = index;
    while (true) {
        if (OMX_ErrorNone != mOMX->getParameter(
                mNode, OMX_IndexParamVideoPortFormat,
                &portFormat, sizeof(portFormat))) {
            break;
        }
        // Make sure that omx component does not overwrite
        // the incremented index (bug 2897413).
        CHECK_EQ(index, portFormat.nIndex);
        if ((portFormat.eColorFormat == colorFormat)) {
            LOGV("Found supported color format: %d", portFormat.eColorFormat);
            return OK;  // colorFormat is supported!
        }
        ++index;
        portFormat.nIndex = index;

        // OMX Spec defines less than 50 color formats
        // 1000 is more than enough for us to tell whether the omx
        // component in question is buggy or not.
        if (index >= 1000) {
            LOGE("More than %ld color formats are supported???", index);
            break;
        }
    }

    LOGE("color format %d is not supported", colorFormat);
    return UNKNOWN_ERROR;
}

void OMXCodec::setVideoInputFormat(
        const char *mime, const sp<MetaData>& meta) {

    int32_t width, height, frameRate, bitRate, stride, sliceHeight, hfr;

    char value[PROPERTY_VALUE_MAX];
    if ( property_get("encoder.video.bitrate", value, 0) > 0 && atoi(value) > 0){
        LOGV("Setting bit rate to %d", atoi(value));
        meta->setInt32(kKeyBitRate, atoi(value));
    }

    bool success = meta->findInt32(kKeyWidth, &width);
    success = success && meta->findInt32(kKeyHeight, &height);
    success = success && meta->findInt32(kKeySampleRate, &frameRate);
    success = success && meta->findInt32(kKeyBitRate, &bitRate);
    success = success && meta->findInt32(kKeyStride, &stride);
    success = success && meta->findInt32(kKeySliceHeight, &sliceHeight);
    success = success && meta->findInt32(kKeyHFR, &hfr);
    CHECK(success);
    CHECK(stride != 0);

    frameRate = hfr?hfr:frameRate;

    OMX_VIDEO_CODINGTYPE compressionFormat = OMX_VIDEO_CodingUnused;
    if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_AVC, mime)) {
        compressionFormat = OMX_VIDEO_CodingAVC;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_MPEG4, mime)) {
        compressionFormat = OMX_VIDEO_CodingMPEG4;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_H263, mime)) {
        compressionFormat = OMX_VIDEO_CodingH263;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_DIVX, mime)){
        compressionFormat= (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingDivx;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_DIVX311, mime)){
        compressionFormat= (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingDivx;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_DIVX4, mime)){
        compressionFormat= (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingDivx;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_WMV, mime)){
        compressionFormat = OMX_VIDEO_CodingWMV;
    } else if (!strcasecmp(MEDIA_MIMETYPE_CONTAINER_MPEG2, mime)){
        compressionFormat = OMX_VIDEO_CodingMPEG2;
    } else {
        LOGE("Not a supported video mime type: %s", mime);
        CHECK(!"Should not be here. Not a supported video mime type.");
    }

    OMX_COLOR_FORMATTYPE colorFormat;
    CHECK_EQ(OK, findTargetColorFormat(meta, &colorFormat));

    status_t err;
    OMX_PARAM_PORTDEFINITIONTYPE def;
    OMX_VIDEO_PORTDEFINITIONTYPE *video_def = &def.format.video;

    //////////////////////// Input port /////////////////////////
    CHECK_EQ(setVideoPortFormatType(
            kPortIndexInput, OMX_VIDEO_CodingUnused,
            colorFormat), OK);

    InitOMXParams(&def);
    def.nPortIndex = kPortIndexInput;

    err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);

    // Qualcomm codecs calculate the buffer size internally, and expect
    // that the size value set is the same as the one provided on getParameter.
    // So buffer size will be calculated here only when using third
    // party codecs.
    if (strncmp(mComponentName, "OMX.qcom",8) != 0) {
        def.nBufferSize = getFrameSize(colorFormat,
                stride > 0? stride: -stride, sliceHeight);
    }

    CHECK_EQ(def.eDomain, OMX_PortDomainVideo);

    video_def->nFrameWidth = width;
    video_def->nFrameHeight = height;
    video_def->nStride = stride;
    video_def->nSliceHeight = sliceHeight;
    video_def->xFramerate = (frameRate << 16);  // Q16 format
    video_def->eCompressionFormat = OMX_VIDEO_CodingUnused;
    video_def->eColorFormat = colorFormat;

    err = mOMX->setParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);

    //////////////////////// Output port /////////////////////////
    CHECK_EQ(setVideoPortFormatType(
            kPortIndexOutput, compressionFormat, OMX_COLOR_FormatUnused),
            OK);
    InitOMXParams(&def);
    def.nPortIndex = kPortIndexOutput;

    err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));

    CHECK_EQ(err, OK);
    CHECK_EQ(def.eDomain, OMX_PortDomainVideo);

    video_def->nFrameWidth = width;
    video_def->nFrameHeight = height;

   /*
    * TODO - why is this not needed?
    * instead, they are setting nPFrames to the
    * product of frame rate and iframeinterval.
    * Should the encoder work with that? 
    */

    video_def->xFramerate = (frameRate << 16);       // No need for output port
    video_def->nBitrate = bitRate;  // Q16 format
    video_def->eCompressionFormat = compressionFormat;
    video_def->eColorFormat = OMX_COLOR_FormatUnused;
    if (mQuirks & kRequiresLargerEncoderOutputBuffer) {
        // Increases the output buffer size
        def.nBufferSize = ((def.nBufferSize * 3) >> 1);
    }

    err = mOMX->setParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);

    /////////////////// Codec-specific ////////////////////////
    switch (compressionFormat) {
        case OMX_VIDEO_CodingMPEG4:
        {
            CHECK_EQ(setupMPEG4EncoderParameters(meta), OK);
            break;
        }

        case OMX_VIDEO_CodingH263:
            CHECK_EQ(setupH263EncoderParameters(meta), OK);
            break;

        case OMX_VIDEO_CodingAVC:
        {
            CHECK_EQ(setupAVCEncoderParameters(meta), OK);
            break;
        }

        default:
            CHECK(!"Support for this compressionFormat to be implemented.");
            break;
    }

    //If compression is AVC and 3D encoding is requested, we'll inform the encoder
    int32_t want3D = 0;
    if (meta->findInt32(kKey3D, &want3D) && want3D)
    {
        if (compressionFormat == OMX_VIDEO_CodingAVC)
        {
            OMX_QCOM_FRAME_PACK_ARRANGEMENT fpa;

            fpa.id = 0;
            fpa.cancel_flag = 0;
            fpa.type = 3; //Left-Right arrangement
            fpa.quincunx_sampling_flag = 0;
            fpa.content_interpretation_type = 0;
            fpa.spatial_flipping_flag = 0;
            fpa.frame0_flipped_flag = 0;
            fpa.field_views_flag = 0;
            fpa.current_frame_is_frame0_flag = 0;
            fpa.frame0_self_contained_flag = 0;
            fpa.frame1_self_contained_flag = 0;
            fpa.frame0_grid_position_x = 0;
            fpa.frame0_grid_position_y = 0;
            fpa.frame1_grid_position_x = 0;
            fpa.frame1_grid_position_y = 0;
            fpa.reserved_byte = 0;
            fpa.repetition_period = 0;
            fpa.extension_flag = 0;

            err = mOMX->setConfig(mNode,
                    (OMX_INDEXTYPE)OMX_QcomIndexConfigVideoFramePackingArrangement,
                    &fpa, (size_t)sizeof(fpa));
        }
        else
        {
            LOGE("Only H264 supports SEI info, setting 3d failed");
            err = ERROR_UNSUPPORTED;
        }

        if (err != OMX_ErrorNone)
        {
            CHECK(!"Enabling 3d failed");
        }
        else
            LOGI("Enabled 3d");

    }
}

static OMX_U32 setPFramesSpacing(int32_t iFramesInterval, int32_t frameRate) {
    if (iFramesInterval < 0) {
        return 0xFFFFFFFF;
    } else if (iFramesInterval == 0) {
        return 0;
    }
    OMX_U32 ret = frameRate * iFramesInterval;
    CHECK(ret > 1);
    return ret;
}

status_t OMXCodec::setupErrorCorrectionParameters() {
    OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE errorCorrectionType;
    InitOMXParams(&errorCorrectionType);
    errorCorrectionType.nPortIndex = kPortIndexOutput;

    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamVideoErrorCorrection,
            &errorCorrectionType, sizeof(errorCorrectionType));
    if (err != OK) {
        LOGW("Error correction param query is not supported");
        return OK;  // Optional feature. Ignore this failure
    }

    errorCorrectionType.bEnableHEC = OMX_FALSE;
    errorCorrectionType.bEnableResync = OMX_FALSE;
    errorCorrectionType.nResynchMarkerSpacing = 0;
    errorCorrectionType.bEnableDataPartitioning = OMX_FALSE;
    errorCorrectionType.bEnableRVLC = OMX_FALSE;

    err = mOMX->setParameter(
            mNode, OMX_IndexParamVideoErrorCorrection,
            &errorCorrectionType, sizeof(errorCorrectionType));
    if (err != OK) {
        LOGW("Error correction param configuration is not supported");
    }

    // Optional feature. Ignore the failure.
    return OK;
}

status_t OMXCodec::setupBitRate(int32_t bitRate) {
    OMX_VIDEO_PARAM_BITRATETYPE bitrateType;
    InitOMXParams(&bitrateType);
    bitrateType.nPortIndex = kPortIndexOutput;

    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamVideoBitrate,
            &bitrateType, sizeof(bitrateType));
    CHECK_EQ(err, OK);

    OMX_VIDEO_CONTROLRATETYPE controlRate = OMX_Video_ControlRateVariable;
    char value[PROPERTY_VALUE_MAX];

    if ( mIsEncoder && property_get("encoder.video.rc", value, NULL ) > 0 ) {
        int rv = atoi( value );
        switch ( rv ) {
        case 0:
            controlRate = OMX_Video_ControlRateDisable;
            break;
        case 1:
            controlRate = OMX_Video_ControlRateVariable;
            break;
        case 2:
            controlRate = OMX_Video_ControlRateConstant;
            break;
        case 3:
            controlRate = OMX_Video_ControlRateVariableSkipFrames;
            break;
        case 4:
            controlRate = OMX_Video_ControlRateConstantSkipFrames;
            break;
        default:
            LOGW("Unknown rate control value, assume default");
            break;
        }
    }

    bitrateType.eControlRate = controlRate;
    bitrateType.nTargetBitrate = bitRate;

    err = mOMX->setParameter(
            mNode, OMX_IndexParamVideoBitrate,
            &bitrateType, sizeof(bitrateType));
    CHECK_EQ(err, OK);
    return OK;
}

status_t OMXCodec::getVideoProfileLevel(
        const sp<MetaData>& meta,
        const CodecProfileLevel& defaultProfileLevel,
        CodecProfileLevel &profileLevel) {
    CODEC_LOGV("Default profile: %ld, level %ld",
            defaultProfileLevel.mProfile, defaultProfileLevel.mLevel);

    // Are the default profile and level overwriten?
    int32_t profile, level;
    if (!meta->findInt32(kKeyVideoProfile, &profile)) {
        profile = defaultProfileLevel.mProfile;
    }
    if (!meta->findInt32(kKeyVideoLevel, &level)) {
        level = defaultProfileLevel.mLevel;
    }
    CODEC_LOGV("Target profile: %d, level: %d", profile, level);

    // Are the target profile and level supported by the encoder?
    OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
    InitOMXParams(&param);
    param.nPortIndex = kPortIndexOutput;
    for (param.nProfileIndex = 0;; ++param.nProfileIndex) {
        status_t err = mOMX->getParameter(
                mNode, OMX_IndexParamVideoProfileLevelQuerySupported,
                &param, sizeof(param));

        if (err != OK) { 
            LOGW("getParameter with index OMX_IndexParamVideoProfileLevelQuerySupported failed");
            break;
        }

        int32_t supportedProfile = static_cast<int32_t>(param.eProfile);
        int32_t supportedLevel = static_cast<int32_t>(param.eLevel);
        CODEC_LOGV("Supported profile: %d, level %d",
            supportedProfile, supportedLevel);

        if (profile == supportedProfile &&
            level <= supportedLevel) {
            // We can further check whether the level is a valid
            // value; but we will leave that to the omx encoder component
            // via OMX_SetParameter call.
            profileLevel.mProfile = profile;
            profileLevel.mLevel = level;
            return OK;
        }
    }

    CODEC_LOGE("Target profile (%d) and level (%d) is not supported",
            profile, level);
    return BAD_VALUE;
}

status_t OMXCodec::setupH263EncoderParameters(const sp<MetaData>& meta) {
    int32_t iFramesInterval, frameRate, bitRate;
    bool success = meta->findInt32(kKeyBitRate, &bitRate);
    success = success && meta->findInt32(kKeySampleRate, &frameRate);
    success = success && meta->findInt32(kKeyIFramesInterval, &iFramesInterval);
    CHECK(success);
    OMX_VIDEO_PARAM_H263TYPE h263type;
    InitOMXParams(&h263type);
    h263type.nPortIndex = kPortIndexOutput;

    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamVideoH263, &h263type, sizeof(h263type));
    CHECK_EQ(err, OK);

    h263type.nAllowedPictureTypes =
        OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP;

    h263type.nPFrames = setPFramesSpacing(iFramesInterval, frameRate);
    if (h263type.nPFrames == 0) {
        h263type.nAllowedPictureTypes = OMX_VIDEO_PictureTypeI;
    }
    h263type.nBFrames = 0;

    // Check profile and level parameters
    CodecProfileLevel defaultProfileLevel, profileLevel;
    defaultProfileLevel.mProfile = h263type.eProfile;
    defaultProfileLevel.mLevel = h263type.eLevel;
    err = getVideoProfileLevel(meta, defaultProfileLevel, profileLevel);
    if (err != OK) return err;
    h263type.eProfile = static_cast<OMX_VIDEO_H263PROFILETYPE>(profileLevel.mProfile);
    h263type.eLevel = static_cast<OMX_VIDEO_H263LEVELTYPE>(profileLevel.mLevel);

    h263type.bPLUSPTYPEAllowed = OMX_FALSE;
    h263type.bForceRoundingTypeToZero = OMX_FALSE;
    h263type.nPictureHeaderRepetition = 0;
    h263type.nGOBHeaderInterval = 0;

    err = mOMX->setParameter(
            mNode, OMX_IndexParamVideoH263, &h263type, sizeof(h263type));
    CHECK_EQ(err, OK);

    CHECK_EQ(setupBitRate(bitRate), OK);
    CHECK_EQ(setupErrorCorrectionParameters(), OK);

    return OK;
}


status_t OMXCodec::setupMPEG2EncoderParameters(const sp<MetaData>& meta) {
    return OK;
}

status_t OMXCodec::setupMPEG4EncoderParameters(const sp<MetaData>& meta) {
    int32_t iFramesInterval, frameRate, bitRate;
    bool success = meta->findInt32(kKeyBitRate, &bitRate);
    success = success && meta->findInt32(kKeySampleRate, &frameRate);
    success = success && meta->findInt32(kKeyIFramesInterval, &iFramesInterval);
    CHECK(success);
    OMX_VIDEO_PARAM_MPEG4TYPE mpeg4type;
    InitOMXParams(&mpeg4type);
    mpeg4type.nPortIndex = kPortIndexOutput;

    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamVideoMpeg4, &mpeg4type, sizeof(mpeg4type));
    CHECK_EQ(err, OK);

    mpeg4type.nSliceHeaderSpacing = 0;
    mpeg4type.bSVH = OMX_FALSE;
    mpeg4type.bGov = OMX_FALSE;

    mpeg4type.nAllowedPictureTypes =
        OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP;

    mpeg4type.nPFrames = setPFramesSpacing(iFramesInterval, frameRate);
    if (mpeg4type.nPFrames == 0) {
        mpeg4type.nAllowedPictureTypes = OMX_VIDEO_PictureTypeI;
    }
    mpeg4type.nBFrames = 0;
    mpeg4type.nIDCVLCThreshold = 0;
    mpeg4type.bACPred = OMX_TRUE;
    mpeg4type.nMaxPacketSize = 256;
    mpeg4type.nTimeIncRes = 1000;
    mpeg4type.nHeaderExtension = 0;
    mpeg4type.bReversibleVLC = OMX_FALSE;

    // Check profile and level parameters
    CodecProfileLevel defaultProfileLevel, profileLevel;
    defaultProfileLevel.mProfile = mpeg4type.eProfile;
    defaultProfileLevel.mLevel = mpeg4type.eLevel;
    err = getVideoProfileLevel(meta, defaultProfileLevel, profileLevel);
    if (err != OK) return err;
    mpeg4type.eProfile = static_cast<OMX_VIDEO_MPEG4PROFILETYPE>(profileLevel.mProfile);
    mpeg4type.eLevel = static_cast<OMX_VIDEO_MPEG4LEVELTYPE>(profileLevel.mLevel);

    if (mpeg4type.eProfile == OMX_VIDEO_MPEG4ProfileAdvancedSimple) {
        mpeg4type.nBFrames = 1;
    }

    err = mOMX->setParameter(
            mNode, OMX_IndexParamVideoMpeg4, &mpeg4type, sizeof(mpeg4type));
    CHECK_EQ(err, OK);

    CHECK_EQ(setupBitRate(bitRate), OK);
    CHECK_EQ(setupErrorCorrectionParameters(), OK);

    return OK;
}

status_t OMXCodec::setupAVCEncoderParameters(const sp<MetaData>& meta) {
    int32_t iFramesInterval, frameRate, bitRate;
    bool success = meta->findInt32(kKeyBitRate, &bitRate);
    success = success && meta->findInt32(kKeySampleRate, &frameRate);
    success = success && meta->findInt32(kKeyIFramesInterval, &iFramesInterval);
    CHECK(success);

    OMX_VIDEO_PARAM_AVCTYPE h264type;
    InitOMXParams(&h264type);
    h264type.nPortIndex = kPortIndexOutput;

    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamVideoAvc, &h264type, sizeof(h264type));
    CHECK_EQ(err, OK);

    h264type.nAllowedPictureTypes =
        OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP;

    h264type.nSliceHeaderSpacing = 0;
    h264type.nBFrames = 0;   // hardcoding B-Frame support to 1
    h264type.nPFrames = setPFramesSpacing(iFramesInterval, frameRate);
    if (h264type.nPFrames == 0) {
        h264type.nAllowedPictureTypes = OMX_VIDEO_PictureTypeI;
    }

    // Check profile and level parameters
    CodecProfileLevel defaultProfileLevel, profileLevel;
    defaultProfileLevel.mProfile = h264type.eProfile;
    defaultProfileLevel.mLevel = h264type.eLevel;
    err = getVideoProfileLevel(meta, defaultProfileLevel, profileLevel);
    if (err != OK) return err;
    h264type.eProfile = static_cast<OMX_VIDEO_AVCPROFILETYPE>(profileLevel.mProfile);
    h264type.eLevel = static_cast<OMX_VIDEO_AVCLEVELTYPE>(profileLevel.mLevel);

    if (h264type.eProfile == OMX_VIDEO_AVCProfileBaseline) {
        h264type.bUseHadamard = OMX_TRUE;
        h264type.nRefFrames = 1;
        h264type.nRefIdx10ActiveMinus1 = 0;
        h264type.nRefIdx11ActiveMinus1 = 0;
        h264type.bEntropyCodingCABAC = OMX_FALSE;
        h264type.bWeightedPPrediction = OMX_FALSE;
        h264type.bconstIpred = OMX_FALSE;
        h264type.bDirect8x8Inference = OMX_FALSE;
        h264type.bDirectSpatialTemporal = OMX_FALSE;
        h264type.nCabacInitIdc = 0;
    }

    if (h264type.eProfile > OMX_VIDEO_AVCProfileBaseline) {
        h264type.nBFrames = 1;
    }

    if (h264type.nBFrames != 0) {
        h264type.nAllowedPictureTypes |= OMX_VIDEO_PictureTypeB;
    }

    h264type.bEnableUEP = OMX_FALSE;
    h264type.bEnableFMO = OMX_FALSE;
    h264type.bEnableASO = OMX_FALSE;
    h264type.bEnableRS = OMX_FALSE;
    h264type.bFrameMBsOnly = OMX_TRUE;
    h264type.bMBAFF = OMX_FALSE;
    h264type.eLoopFilterMode = OMX_VIDEO_AVCLoopFilterEnable;

    err = mOMX->setParameter(
            mNode, OMX_IndexParamVideoAvc, &h264type, sizeof(h264type));
    CHECK_EQ(err, OK);

    CHECK_EQ(setupBitRate(bitRate), OK);

    return OK;
}

status_t OMXCodec::setVideoOutputFormat(
        const char *mime, OMX_U32 width, OMX_U32 height) {
    CODEC_LOGV("setVideoOutputFormat width=%ld, height=%ld", width, height);

    OMX_VIDEO_CODINGTYPE compressionFormat = OMX_VIDEO_CodingUnused;
    if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_AVC, mime)) {
        compressionFormat = OMX_VIDEO_CodingAVC;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_MPEG4, mime)) {
        compressionFormat = OMX_VIDEO_CodingMPEG4;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_H263, mime)) {
        compressionFormat = OMX_VIDEO_CodingH263;
    } else if(!strcasecmp(MEDIA_MIMETYPE_VIDEO_DIVX, mime)) {
        compressionFormat = (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingDivx;
    } else if(!strcasecmp(MEDIA_MIMETYPE_VIDEO_SPARK, mime)) {
        compressionFormat= (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingSpark;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_VP6, mime)) {
        compressionFormat= (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingVp;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_WMV, mime)){
        compressionFormat = OMX_VIDEO_CodingWMV;
    } else if(!strcasecmp(MEDIA_MIMETYPE_VIDEO_DIVX311, mime)) {
        compressionFormat = (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingDivx;
    } else if(!strcasecmp(MEDIA_MIMETYPE_VIDEO_DIVX4, mime)) {
        compressionFormat = (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingDivx;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_VPX, mime)) {
        compressionFormat= (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingVp8;
    } else if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_MPEG2, mime)) {
        compressionFormat= (OMX_VIDEO_CODINGTYPE)OMX_VIDEO_CodingMPEG2;
    } else {
        LOGE("Not a supported video mime type: %s", mime);
        CHECK(!"Should not be here. Not a supported video mime type.");
    }

    status_t err = setVideoPortFormatType(
            kPortIndexInput, compressionFormat, OMX_COLOR_FormatUnused);

    if (err != OK) {
        return err;
    }

    //Enable decoder to report if there is any SEI data
    //(supported only by H264)
    if (compressionFormat == OMX_VIDEO_CodingAVC)
    {
        QOMX_ENABLETYPE enable_sei_reporting;
        enable_sei_reporting.bEnable = OMX_TRUE;

        err = mOMX->setParameter(
                mNode, (OMX_INDEXTYPE)OMX_QcomIndexParamFrameInfoExtraData,
                &enable_sei_reporting, (size_t)sizeof(enable_sei_reporting));

        if (err != OK)
            LOGV("Not supported parameter OMX_QcomIndexParamFrameInfoExtraData");
    }

    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE format;
        InitOMXParams(&format);
        format.nPortIndex = kPortIndexOutput;

        if (!strncmp(mComponentName, "OMX.qcom",8)) {
            int32_t reqdColorFormat = ColorFormatInfo::getPreferredColorFormat(mOMXLivesLocally);
            for(format.nIndex = 0;
                    (OK == mOMX->getParameter(mNode, OMX_IndexParamVideoPortFormat, &format, sizeof(format)));
                    format.nIndex++) {
                if(format.eColorFormat == reqdColorFormat)
                    break;
            }
        } else
            format.nIndex = 0;

        CODEC_LOGV("Video O/P format.nIndex 0x%x",format.nIndex);
        CODEC_LOGV("Video O/P format.eColorFormat 0x%x",format.eColorFormat);

        status_t err = mOMX->getParameter(
                mNode, OMX_IndexParamVideoPortFormat,
                &format, sizeof(format));
        CHECK_EQ(err, OK);
        CHECK_EQ(format.eCompressionFormat, OMX_VIDEO_CodingUnused);

        CHECK(format.eColorFormat == OMX_COLOR_FormatYUV420Planar
               || format.eColorFormat == OMX_COLOR_FormatYUV420SemiPlanar
               || format.eColorFormat == OMX_COLOR_FormatCbYCrY
               || format.eColorFormat == OMX_QCOM_COLOR_FormatYVU420SemiPlanar
               || format.eColorFormat == QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka
               || format.eColorFormat == QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka
               || format.eColorFormat == (QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka ^ QOMX_INTERLACE_FLAG)
               || format.eColorFormat == (OMX_COLOR_FormatYUV420SemiPlanar ^ QOMX_INTERLACE_FLAG)
               || format.eColorFormat == (OMX_QCOM_COLOR_FormatYVU420SemiPlanar ^ QOMX_INTERLACE_FLAG)) ;

        if (mGPUComposition) {
            LOGV("Set GPU Composition");
            format.eColorFormat = (OMX_COLOR_FORMATTYPE)QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka;
        }

        err = mOMX->setParameter(
                mNode, OMX_IndexParamVideoPortFormat,
                &format, sizeof(format));

        if (err != OK) {
            return err;
        }
    }

    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = kPortIndexInput;

    OMX_VIDEO_PORTDEFINITIONTYPE *video_def = &def.format.video;

    err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));

    CHECK_EQ(err, OK);

#if 1
    // XXX Need a (much) better heuristic to compute input buffer sizes.
    const size_t X = 64 * 1024;
    if (def.nBufferSize < X) {
        def.nBufferSize = X;
    }
#endif

    CHECK_EQ(def.eDomain, OMX_PortDomainVideo);

    video_def->nFrameWidth = width;
    video_def->nFrameHeight = height;

    video_def->eCompressionFormat = compressionFormat;
    video_def->eColorFormat = OMX_COLOR_FormatUnused;

    err = mOMX->setParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));

    if (err != OK) {
        return err;
    }

    ////////////////////////////////////////////////////////////////////////////

    InitOMXParams(&def);
    def.nPortIndex = kPortIndexOutput;

    err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);
    CHECK_EQ(def.eDomain, OMX_PortDomainVideo);

#if 0
    def.nBufferSize =
        (((width + 15) & -16) * ((height + 15) & -16) * 3) / 2;  // YUV420
#endif

    video_def->nFrameWidth = width;
    video_def->nFrameHeight = height;

    err = mOMX->setParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));

    return err;
}

OMXCodec::OMXCodec(
        const sp<IOMX> &omx, IOMX::node_id node, uint32_t quirks,
        bool isEncoder,
        const char *mime,
        const char *componentName,
        const sp<MediaSource> &source)
    : mOMX(omx),
      mOMXLivesLocally(omx->livesLocally(getpid())),
      mNode(node),
      mQuirks(quirks),
      mIsEncoder(isEncoder),
      mMIME(strdup(mime)),
      mComponentName(strdup(componentName)),
      mSource(source),
      mCodecSpecificDataIndex(0),
      mState(LOADED),
      mInitialBufferSubmit(true),
      mSignalledEOS(false),
      mNoMoreOutputData(false),
      mOutputPortSettingsHaveChanged(false),
      mSeekTimeUs(-1),
      mSeekMode(ReadOptions::SEEK_CLOSEST_SYNC),
      mTargetTimeUs(-1),
      mSkipTimeUs(-1),
      mStateBeforeError(mState),
      mPaused(false),
      mGPUComposition(false),
      mThumbnailMode(false),
      mLeftOverBuffer(NULL),
      mPmemInfo(NULL),
      mInterlaceFormatDetected(false),
      m3DVideoDetected(false),
      mSendEOS(false),
      mForce3D(0),
      mFinalStatus(OK),
      mSPSParsed(false),
      bInvalidState(false) {
    mPortStatus[kPortIndexInput] = ENABLED;
    mPortStatus[kPortIndexOutput] = ENABLED;

    setComponentRole();
}

// static
void OMXCodec::setComponentRole(
        const sp<IOMX> &omx, IOMX::node_id node, bool isEncoder,
        const char *mime) {
    struct MimeToRole {
        const char *mime;
        const char *decoderRole;
        const char *encoderRole;
    };

    static const MimeToRole kMimeToRole[] = {
        { MEDIA_MIMETYPE_AUDIO_MPEG,
            "audio_decoder.mp3", "audio_encoder.mp3" },
        { MEDIA_MIMETYPE_AUDIO_AMR_NB,
            "audio_decoder.amrnb", "audio_encoder.amrnb" },
        { MEDIA_MIMETYPE_AUDIO_AMR_WB,
            "audio_decoder.amrwb", "audio_encoder.amrwb" },
        { MEDIA_MIMETYPE_AUDIO_AAC,
            "audio_decoder.aac", "audio_encoder.aac" },
        { MEDIA_MIMETYPE_AUDIO_EVRC,
            "audio_decoder.evrchw", "audio_encoder.evrc" },
        { MEDIA_MIMETYPE_AUDIO_QCELP,
            "audio_decoder,qcelp13Hw", "audio_encoder.qcelp13" },
        { MEDIA_MIMETYPE_VIDEO_AVC,
            "video_decoder.avc", "video_encoder.avc" },
        { MEDIA_MIMETYPE_VIDEO_MPEG4,
            "video_decoder.mpeg4", "video_encoder.mpeg4" },
        { MEDIA_MIMETYPE_VIDEO_H263,
            "video_decoder.h263", "video_encoder.h263" },
        { MEDIA_MIMETYPE_VIDEO_DIVX,
            "video_decoder.divx", NULL },
        { MEDIA_MIMETYPE_AUDIO_AC3,
            "audio_decoder.ac3", NULL },
        { MEDIA_MIMETYPE_VIDEO_DIVX311,
            "video_decoder.divx", NULL },
    };

    static const size_t kNumMimeToRole =
        sizeof(kMimeToRole) / sizeof(kMimeToRole[0]);

    size_t i;
    for (i = 0; i < kNumMimeToRole; ++i) {
        if (!strcasecmp(mime, kMimeToRole[i].mime)) {
            break;
        }
    }

    if (i == kNumMimeToRole) {
        return;
    }

    const char *role =
        isEncoder ? kMimeToRole[i].encoderRole
                  : kMimeToRole[i].decoderRole;

    if (role != NULL) {
        OMX_PARAM_COMPONENTROLETYPE roleParams;
        InitOMXParams(&roleParams);

        strncpy((char *)roleParams.cRole,
                role, OMX_MAX_STRINGNAME_SIZE - 1);

        roleParams.cRole[OMX_MAX_STRINGNAME_SIZE - 1] = '\0';

        status_t err = omx->setParameter(
                node, OMX_IndexParamStandardComponentRole,
                &roleParams, sizeof(roleParams));

        if (err != OK) {
            LOGW("Failed to set standard component role '%s'.", role);
        }
    }
}

void OMXCodec::setComponentRole() {
    setComponentRole(mOMX, mNode, mIsEncoder, mMIME);
}

OMXCodec::~OMXCodec() {

    mPmemInfo.clear();
    mSource.clear();

    CHECK(mState == LOADED || mState == ERROR);

    status_t err = mOMX->freeNode(mNode);
    CHECK_EQ(err, OK);

    mNode = NULL;
    setState(DEAD);

    clearCodecSpecificData();

    free(mComponentName);
    mComponentName = NULL;

    free(mMIME);
    mMIME = NULL;
}

status_t OMXCodec::init() {
    // mLock is held.

    CHECK_EQ(mState, LOADED);

    status_t err;
    if (!(mQuirks & kRequiresLoadedToIdleAfterAllocation)) {
        err = mOMX->sendCommand(mNode, OMX_CommandStateSet, OMX_StateIdle);
        CHECK_EQ(err, OK);
        setState(LOADED_TO_IDLE);
    }

    err = allocateBuffers();
    if(err != OK) {
        LOGE("Allocate Buffer failed - error = %d",err);
        setState(ERROR);
        return NO_MEMORY;
    }

    if (mQuirks & kRequiresLoadedToIdleAfterAllocation) {
        err = mOMX->sendCommand(mNode, OMX_CommandStateSet, OMX_StateIdle);
        CHECK_EQ(err, OK);

        setState(LOADED_TO_IDLE);
    }

    while (mState != EXECUTING && mState != ERROR) {
        mAsyncCompletion.wait(mLock);
    }

    return mState == ERROR ? UNKNOWN_ERROR : OK;
}

// static
bool OMXCodec::isIntermediateState(State state) {
    return state == LOADED_TO_IDLE
        || state == IDLE_TO_EXECUTING
        || state == EXECUTING_TO_IDLE
        || state == IDLE_TO_LOADED
        || state == PAUSING
        || state == FLUSHING
        || state == RECONFIGURING;
}

status_t OMXCodec::allocateBuffers() {
    status_t err = allocateBuffersOnPort(kPortIndexInput);

    if (err != OK) {
        return err;
    }

    return allocateBuffersOnPort(kPortIndexOutput);
}

status_t OMXCodec::allocateBuffersOnPort(OMX_U32 portIndex) {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = portIndex;

    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));

    if (err != OK) {
        return err;
    }

    sp<IMemoryHeap> pFrameHeap = NULL;
    size_t alignedSize = 0;
    size_t size = 0;
    if (mIsEncoder && (portIndex == kPortIndexInput) &&
        (mQuirks & kAvoidMemcopyInputRecordingFrames))
    {
        sp<IMemory> pFrame = NULL;
        ssize_t offset = 0;
        size_t nBuffers = 0;

        mSource->getBufferInfo(pFrame, &alignedSize);
        CHECK(pFrame != NULL);

        pFrameHeap = pFrame->getMemory(&offset, &size);
        nBuffers = pFrameHeap->getSize( )/alignedSize;

        //update OMX with new buffer count.
        if( nBuffers < def.nBufferCountMin || size < def.nBufferSize ) {
            LOGE("Buffer count/size less than minimum required");
            return UNKNOWN_ERROR;
        }

        def.nBufferCountActual = nBuffers;

        status_t err = mOMX->setParameter(
                mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));

        if (err != OK) {
            LOGE("Updating buffer count failed");
            return err;
        }
    }

    CODEC_LOGI("allocating %lu buffers of size %lu on %s port",
            def.nBufferCountActual, def.nBufferSize,
            portIndex == kPortIndexInput ? "input" : "output");

    size_t totalSize = def.nBufferCountActual * ((def.nBufferSize + 31) & (~31));
    mDealer[portIndex] = new MemoryDealer(totalSize, "OMXCodec");

    for (OMX_U32 i = 0; i < def.nBufferCountActual; ++i) {
        sp<IMemory> mem = mDealer[portIndex]->allocate(def.nBufferSize);
        CHECK(mem.get() != NULL);

        BufferInfo info;
        info.mData = NULL;
        info.mSize = def.nBufferSize;

        IOMX::buffer_id buffer;
        if (portIndex == kPortIndexInput
                && (mQuirks & kRequiresAllocateBufferOnInputPorts)) {
            if (mOMXLivesLocally) {
                mem.clear();

                err = mOMX->allocateBuffer(
                        mNode, portIndex, def.nBufferSize, &buffer,
                        &info.mData);
            } else {
                err = mOMX->allocateBufferWithBackup(
                        mNode, portIndex, mem, &buffer);
            }
        } else if (portIndex == kPortIndexOutput
                && (mQuirks & kRequiresAllocateBufferOnOutputPorts)) {
            if (mOMXLivesLocally || (mQuirks & kDoesNotRequireMemcpyOnOutputPort)) {
                mem.clear();

                err = mOMX->allocateBuffer(
                        mNode, portIndex, def.nBufferSize, &buffer,
                        &info.mData);
            } else {
                err = mOMX->allocateBufferWithBackup(
                        mNode, portIndex, mem, &buffer);
            }
        } else {
            if(pFrameHeap != NULL && mIsEncoder && (mQuirks & kAvoidMemcopyInputRecordingFrames)) {
                ssize_t temp_offset = i * alignedSize;
                size_t temp_size = size;
                sp<IMemory> pFrame = NULL;
                sp<MemoryBase> pFrameBase = new MemoryBase(pFrameHeap, temp_offset, temp_size);
                pFrame = pFrameBase;
                LOGI("getParametersSync: pmem_fd = %d, base = %p, temp_offset %d," \
                       " temp_size %d, alignedSize = %d, pointer %p, Total Size = %d", pFrameHeap->getHeapID(), pFrameHeap->base(), temp_offset,
                       temp_size, alignedSize, pFrame->pointer(), pFrameHeap->getSize());
                err = mOMX->useBuffer(mNode, portIndex, pFrame, &buffer);
            }
            else {
                err = mOMX->useBuffer(mNode, portIndex, mem, &buffer);
            }
        }

        if (err != OK) {
            LOGE("allocate_buffer_with_backup failed");
            return err;
        }

        if (mem != NULL) {
            info.mData = mem->pointer();
        }

        info.mBuffer = buffer;
        info.mOwnedByComponent = false;
        info.mMem = mem;
        info.mMediaBuffer = NULL;

        if (portIndex == kPortIndexOutput) {
            if (!((mOMXLivesLocally || (mQuirks & kDoesNotRequireMemcpyOnOutputPort))
                        && (mQuirks & kRequiresAllocateBufferOnOutputPorts)
                        && (mQuirks & kDefersOutputBufferAllocation))) {
                // If the node does not fill in the buffer ptr at this time,
                // we will defer creating the MediaBuffer until receiving
                // the first FILL_BUFFER_DONE notification instead.
                info.mMediaBuffer = new MediaBuffer(info.mData, info.mSize);
                info.mMediaBuffer->setObserver(this);
            }
        }

        mPortBuffers[portIndex].push(info);

        CODEC_LOGV("allocated buffer %p on %s port", buffer,
             portIndex == kPortIndexInput ? "input" : "output");
    }

    // dumpPortStatus(portIndex);

    return OK;
}

void OMXCodec::on_message(const omx_message &msg) {
    switch (msg.type) {
        case omx_message::EVENT:
        {
            onEvent(
                 msg.u.event_data.event, msg.u.event_data.data1,
                 msg.u.event_data.data2);

            break;
        }

        case omx_message::EMPTY_BUFFER_DONE:
        {
            IOMX::buffer_id buffer = msg.u.extended_buffer_data.buffer;

            CODEC_LOGV("EMPTY_BUFFER_DONE(buffer: %p)", buffer);

            Vector<BufferInfo> *buffers = &mPortBuffers[kPortIndexInput];
            size_t i = 0;
            while (i < buffers->size() && (*buffers)[i].mBuffer != buffer) {
                ++i;
            }

            CHECK(i < buffers->size());
            if (!(*buffers)[i].mOwnedByComponent) {
                LOGW("We already own input buffer %p, yet received "
                     "an EMPTY_BUFFER_DONE.", buffer);
            }

            {
                BufferInfo *info = &buffers->editItemAt(i);
                info->mOwnedByComponent = false;
                if (info->mMediaBuffer != NULL) {
                    // It is time to release the media buffers storing meta data
                    info->mMediaBuffer->release();
                    info->mMediaBuffer = NULL;
                }
            }

            if( (NULL != (*buffers)[i].mMediaBuffer) && mIsEncoder && (mQuirks & kAvoidMemcopyInputRecordingFrames))
            {
                 CODEC_LOGV("EBD: %x %d", (*buffers)[i].mMediaBuffer, (*buffers)[i].mMediaBuffer->refcount() );
                 (*buffers)[i].mMediaBuffer->release();
                 buffers->editItemAt(i).mMediaBuffer = NULL;
            }

            if (mState == ERROR && (bInvalidState == true)) {
                CODEC_LOGV("mState ERROR, freeing i/p buffer %p", buffer);
                status_t err = mOMX->freeBuffer(mNode, kPortIndexInput, buffer);
                CHECK_EQ(err, (status_t)OK);
                buffers->removeAt(i);
            }
            if (mPortStatus[kPortIndexInput] == DISABLING) {
                CODEC_LOGV("Port is disabled, freeing buffer %p", buffer);

                status_t err =
                    mOMX->freeBuffer(mNode, kPortIndexInput, buffer);
                CHECK_EQ(err, OK);

                buffers->removeAt(i);
            } else if (mState != ERROR
                    && mPortStatus[kPortIndexInput] != SHUTTING_DOWN) {
                CHECK_EQ(mPortStatus[kPortIndexInput], ENABLED);
                drainInputBuffer(&buffers->editItemAt(i));
            }
            break;
        }

        case omx_message::FILL_BUFFER_DONE:
        {
            IOMX::buffer_id buffer = msg.u.extended_buffer_data.buffer;
            OMX_U32 flags = msg.u.extended_buffer_data.flags;

            CODEC_LOGV("FILL_BUFFER_DONE(buffer: %p, size: %ld, flags: 0x%08lx, timestamp: %lld us (%.2f secs))",
                 buffer,
                 msg.u.extended_buffer_data.range_length,
                 flags,
                 msg.u.extended_buffer_data.timestamp,
                 msg.u.extended_buffer_data.timestamp / 1E6);

            Vector<BufferInfo> *buffers = &mPortBuffers[kPortIndexOutput];
            size_t i = 0;
            while (i < buffers->size() && (*buffers)[i].mBuffer != buffer) {
                ++i;
            }

            CHECK(i < buffers->size());
            BufferInfo *info = &buffers->editItemAt(i);

            if (!info->mOwnedByComponent) {
                LOGW("We already own output buffer %p, yet received "
                     "a FILL_BUFFER_DONE.", buffer);
            }

            info->mOwnedByComponent = false;

#if 0
            {
                //temporary - will be removed
                OMX_BUFFERHEADERTYPE *header = (OMX_BUFFERHEADERTYPE *) info->mBuffer;
                CODEC_LOGE("FILL BUFFER DONE buffer %p, hdr %p, len = %u",
                           info->mBuffer, (OMX_U8 *)header->pBuffer, msg.u.extended_buffer_data.range_length);
                if (msg.u.extended_buffer_data.flags & OMX_BUFFERFLAG_EOS) {
                    CODEC_LOGE("EOS done message received from encoder");
                    mNoMoreOutputData = true;
                }
            }
#endif
            if (mState == ERROR && (bInvalidState == true)) {
                CODEC_LOGV("mState ERROR, freeing o/p buffer %p", buffer);
                status_t err = mOMX->freeBuffer(mNode, kPortIndexOutput, buffer);
                CHECK_EQ(err, (status_t)OK);
                buffers->removeAt(i);
            }

            if (mPortStatus[kPortIndexOutput] == DISABLING) {
                CODEC_LOGV("Port is disabled, freeing buffer %p", buffer);

                freeBuffersOnOutPortIfAllAreWithUs();
#if 0
            } else if (mPortStatus[kPortIndexOutput] == ENABLED
                       && (flags & OMX_BUFFERFLAG_EOS)) {
                CODEC_LOGV("No more output data.");
                mNoMoreOutputData = true;
                mBufferFilled.signal();
#endif
            } else if (mPortStatus[kPortIndexOutput] != SHUTTING_DOWN) {
                CHECK_EQ(mPortStatus[kPortIndexOutput], ENABLED);

                if (info->mMediaBuffer == NULL) {
                    CHECK(mOMXLivesLocally || (mQuirks & kDoesNotRequireMemcpyOnOutputPort));
                    CHECK(mQuirks & kRequiresAllocateBufferOnOutputPorts);
                    CHECK(mQuirks & kDefersOutputBufferAllocation);

                    // The qcom video decoders on Nexus don't actually allocate
                    // output buffer memory on a call to OMX_AllocateBuffer
                    // the "pBuffer" member of the OMX_BUFFERHEADERTYPE
                    // structure is only filled in later.

                    info->mMediaBuffer = new MediaBuffer(
                            msg.u.extended_buffer_data.data_ptr,
                            info->mSize);
                    info->mMediaBuffer->setObserver(this);
                  if(!mOMXLivesLocally && mPmemInfo != NULL && buffer != NULL) {
                    uint32_t base = (uint32_t)mPmemInfo->getBase();
                    uint32_t fd = (uint32_t)mPmemInfo->getHeapID();
                    uint32_t offset = msg.u.extended_buffer_data.pmem_offset;
                    int32_t width, height, colorFormat;
                    mOutputFormat->findInt32(kKeyWidth, &width);
                    mOutputFormat->findInt32(kKeyHeight, &height);
                    mOutputFormat->findInt32(kKeyColorFormat, &colorFormat);
                    info->mNativeBuffer = new NativeBuffer(width, height, colorFormat, fd, info->mSize, offset, base);
                  }
                }

                MediaBuffer *buffer = info->mMediaBuffer;

                if (msg.u.extended_buffer_data.range_offset
                        + msg.u.extended_buffer_data.range_length
                            > buffer->size()) {
                    CODEC_LOGE(
                            "Codec lied about its buffer size requirements, "
                            "sending a buffer larger than the originally "
                            "advertised size in FILL_BUFFER_DONE!");
                }
                
                if(!mOMXLivesLocally && mPmemInfo != NULL && buffer != NULL) {
                    OMX_U8* base = (OMX_U8*)mPmemInfo->getBase();
                    OMX_U8* data = base + msg.u.extended_buffer_data.pmem_offset;
                    buffer->setData(data);
                }

                buffer->set_range(
                        msg.u.extended_buffer_data.range_offset,
                        msg.u.extended_buffer_data.range_length);

                buffer->meta_data()->clear();

                buffer->meta_data()->setInt64(
                        kKeyTime, msg.u.extended_buffer_data.timestamp);

                if (msg.u.extended_buffer_data.flags & OMX_BUFFERFLAG_SYNCFRAME) {
                    buffer->meta_data()->setInt32(kKeyIsSyncFrame, true);
                }
                else if ( msg.u.extended_buffer_data.flags &
                         QOMX_VIDEO_BUFFERFLAG_BFRAME ) {
                        buffer->meta_data()->setInt32(kKeyIsBFrame, true);
                }

                if (msg.u.extended_buffer_data.flags & OMX_BUFFERFLAG_CODECCONFIG) {
                    buffer->meta_data()->setInt32(kKeyIsCodecConfig, true);
                }

                if (mQuirks & kOutputBuffersAreUnreadable) {
                    buffer->meta_data()->setInt32(kKeyIsUnreadable, true);
                }

                if(!mOMXLivesLocally && (info->mNativeBuffer != NULL)) {
                  buffer->meta_data()->setPointer(
                      kKeyPlatformPrivate,
                      (void*)(info->mNativeBuffer->getNativeBuffer()));
                } else {
                  buffer->meta_data()->setPointer(
                      kKeyPlatformPrivate,
                      msg.u.extended_buffer_data.platform_private);
                }

                buffer->meta_data()->setPointer(
                        kKeyBufferID,
                        msg.u.extended_buffer_data.buffer);

                if (buffer->range_length() > 0) {
                    CODEC_LOGV("Calling processExtraDataOfbuffer");
                    processExtraDataBlocksOfBuffer(buffer,
                            msg.u.extended_buffer_data.flags);

                    if(!mIsEncoder){
                        CODEC_LOGV("Calling processSEIData");
                        processSEIData();
                    }
                }

                if (msg.u.extended_buffer_data.flags & OMX_BUFFERFLAG_EOS) {
                    CODEC_LOGV("No more output data");
                    mNoMoreOutputData = true;
                }

                if (mTargetTimeUs >= 0) {
                    CHECK(msg.u.extended_buffer_data.timestamp <= mTargetTimeUs);

                    if (msg.u.extended_buffer_data.timestamp < mTargetTimeUs) {
                        CODEC_LOGV(
                                "skipping output buffer at timestamp %lld us",
                                msg.u.extended_buffer_data.timestamp);

                        fillOutputBuffer(info);
                        break;
                    }

                    CODEC_LOGV(
                            "returning output buffer at target timestamp "
                            "%lld us",
                            msg.u.extended_buffer_data.timestamp);

                    mTargetTimeUs = -1;
                }

                mFilledBuffers.push_back(i);
                mBufferFilled.signal();
            }

            break;
        }

        default:
        {
            CHECK(!"should not be here.");
            break;
        }
    }
}
void OMXCodec::registerBuffers(const sp<IMemoryHeap> &mem) {
    mPmemInfo = mem;
}
void OMXCodec::onEvent(OMX_EVENTTYPE event, OMX_U32 data1, OMX_U32 data2) {
    switch (event) {
        case OMX_EventCmdComplete:
        {
            onCmdComplete((OMX_COMMANDTYPE)data1, data2);
            break;
        }

        case OMX_EventError:
        {
            CODEC_LOGE("ERROR(0x%08lx, %ld)", data1, data2);
            if (data1 == OMX_ErrorInvalidState) {
                bInvalidState = true;
                mPortStatus[kPortIndexInput] = SHUTTING_DOWN;
                mPortStatus[kPortIndexOutput] = SHUTTING_DOWN;
            }
            setState(ERROR);
            if (data1 == OMX_ErrorInvalidState)
                mStateBeforeError = ERROR;
            break;
        }

        case OMX_EventPortSettingsChanged:
        {
            if(mState == EXECUTING){
              onPortSettingsChanged(data1);
            }
            else if(mState == FLUSHING){
              LOGV("Received port reconfig while waiting for FBD\n");
              // Seek is completed and waiting for FBD, at this instance
              // Port reconfig is received rather than FBD.
              // Unblock from buffer fill wait and complete the port reconfig.
              mBufferFilled.signal();
              onPortSettingsChanged(data1);
            }
            else
              LOGE("Ignore PortSettingsChanged event \n");
            break;
        }

#if 0
        case OMX_EventBufferFlag:
        {
            CODEC_LOGV("EVENT_BUFFER_FLAG(%ld)", data1);

            if (data1 == kPortIndexOutput) {
                mNoMoreOutputData = true;
            }
            break;
        }
#endif

        default:
        {
            CODEC_LOGV("EVENT(%d, %ld, %ld)", event, data1, data2);
            break;
        }
    }
}

// Has the format changed in any way that the client would have to be aware of?
static bool formatHasNotablyChanged(
        const sp<MetaData> &from, const sp<MetaData> &to) {
    if (from.get() == NULL && to.get() == NULL) {
        return false;
    }

    if ((from.get() == NULL && to.get() != NULL)
        || (from.get() != NULL && to.get() == NULL)) {
        return true;
    }

    const char *mime_from, *mime_to;
    CHECK(from->findCString(kKeyMIMEType, &mime_from));
    CHECK(to->findCString(kKeyMIMEType, &mime_to));

    if (strcasecmp(mime_from, mime_to)) {
        return true;
    }

    if (!strcasecmp(mime_from, MEDIA_MIMETYPE_VIDEO_RAW)) {
        int32_t colorFormat_from, colorFormat_to;
        CHECK(from->findInt32(kKeyColorFormat, &colorFormat_from));
        CHECK(to->findInt32(kKeyColorFormat, &colorFormat_to));

        if (colorFormat_from != colorFormat_to) {
            return true;
        }

        int32_t width_from, width_to;
        CHECK(from->findInt32(kKeyWidth, &width_from));
        CHECK(to->findInt32(kKeyWidth, &width_to));

        if (width_from != width_to) {
            return true;
        }

        int32_t height_from, height_to;
        CHECK(from->findInt32(kKeyHeight, &height_from));
        CHECK(to->findInt32(kKeyHeight, &height_to));

        if (height_from != height_to) {
            return true;
        }
    } else if (!strcasecmp(mime_from, MEDIA_MIMETYPE_AUDIO_RAW)) {
        int32_t numChannels_from, numChannels_to;
        CHECK(from->findInt32(kKeyChannelCount, &numChannels_from));
        CHECK(to->findInt32(kKeyChannelCount, &numChannels_to));

        if (numChannels_from != numChannels_to) {
            return true;
        }

        int32_t sampleRate_from, sampleRate_to;
        CHECK(from->findInt32(kKeySampleRate, &sampleRate_from));
        CHECK(to->findInt32(kKeySampleRate, &sampleRate_to));

        if (sampleRate_from != sampleRate_to) {
            return true;
        }
    }

    return false;
}

void OMXCodec::onCmdComplete(OMX_COMMANDTYPE cmd, OMX_U32 data) {
    switch (cmd) {
        case OMX_CommandStateSet:
        {
            onStateChange((OMX_STATETYPE)data);
            break;
        }

        case OMX_CommandPortDisable:
        {
            if(mState == ERROR) {
              CODEC_LOGE("Ignoring OMX_CommandPortDisable in ERROR state");
              break;
            }
            OMX_U32 portIndex = data;
            CODEC_LOGV("PORT_DISABLED(%ld)", portIndex);

            CHECK(mState == EXECUTING || mState == RECONFIGURING);
            CHECK_EQ(mPortStatus[portIndex], DISABLING);
            CHECK_EQ(mPortBuffers[portIndex].size(), 0);

            mPortStatus[portIndex] = DISABLED;

            if (mState == RECONFIGURING) {
                CHECK_EQ(portIndex, kPortIndexOutput);

                enablePortAsync(portIndex);

                status_t err = allocateBuffersOnPort(portIndex);
                if(err != OK) {
                    LOGE("Allocate Buffer failed for portindex = %d - error = %d",portIndex,err);
                    setState(ERROR);
                }
            }
            break;
        }

        case OMX_CommandPortEnable:
        {
            OMX_U32 portIndex = data;
            CODEC_LOGV("PORT_ENABLED(%ld)", portIndex);

            if(ERROR == mState) {
              CODEC_LOGE("Ignoring port Enable since component is in ERROR state");
              break;
            }

            CHECK(mState == EXECUTING || mState == RECONFIGURING);
            CHECK_EQ(mPortStatus[portIndex], ENABLING);

            mPortStatus[portIndex] = ENABLED;

            if (mState == RECONFIGURING) {
                CHECK_EQ(portIndex, kPortIndexOutput);

                setState(EXECUTING);

                fillOutputBuffers();
            }
            break;
        }

        case OMX_CommandFlush:
        {
            OMX_U32 portIndex = data;

            CODEC_LOGV("FLUSH_DONE(%ld)", portIndex);

            if (portIndex == -1) {
                CHECK_EQ(mPortStatus[kPortIndexInput], SHUTTING_DOWN);
                mPortStatus[kPortIndexInput] = ENABLED;

                CHECK_EQ(mPortStatus[kPortIndexOutput], SHUTTING_DOWN);
                mPortStatus[kPortIndexOutput] = ENABLED;
            } else {
            CHECK_EQ(mPortStatus[portIndex], SHUTTING_DOWN);
            mPortStatus[portIndex] = ENABLED;

            CHECK_EQ(countBuffersWeOwn(mPortBuffers[portIndex]),
                     mPortBuffers[portIndex].size());
            }

            if(mState == ERROR) {
                CODEC_LOGE("Ignoring OMX_CommandFlush in ERROR state");
                break;
            }


            if (mState == RECONFIGURING) {
                CHECK_EQ(portIndex, kPortIndexOutput);

                disablePortAsync(portIndex);
            } else if (mState == EXECUTING_TO_IDLE) {
                if (mPortStatus[kPortIndexInput] == ENABLED
                    && mPortStatus[kPortIndexOutput] == ENABLED) {
                    CODEC_LOGV("Finished flushing both ports, now completing "
                         "transition from EXECUTING to IDLE.");

                    mPortStatus[kPortIndexInput] = SHUTTING_DOWN;
                    mPortStatus[kPortIndexOutput] = SHUTTING_DOWN;

                    status_t err =
                        mOMX->sendCommand(mNode, OMX_CommandStateSet, OMX_StateIdle);
                    CHECK_EQ(err, OK);
                }
            } else {
                // We're flushing both ports in preparation for seeking.

                if (mPortStatus[kPortIndexInput] == ENABLED
                    && mPortStatus[kPortIndexOutput] == ENABLED) {
                    CODEC_LOGV("Finished flushing both ports, now continuing from"
                         " seek-time.");

                    // We implicitly resume pulling on our upstream source.
                    mPaused = false;

                    drainInputBuffers();
                    fillOutputBuffers();
                }
            }

            break;
        }

        default:
        {
            CODEC_LOGV("CMD_COMPLETE(%d, %ld)", cmd, data);
            break;
        }
    }
}

void OMXCodec::onStateChange(OMX_STATETYPE newState) {
    CODEC_LOGV("onStateChange %d", newState);

    switch (newState) {
        case OMX_StateIdle:
        {
            CODEC_LOGV("Now Idle.");
            if (mState == LOADED_TO_IDLE) {
                status_t err = mOMX->sendCommand(
                        mNode, OMX_CommandStateSet, OMX_StateExecuting);

                CHECK_EQ(err, OK);

                setState(IDLE_TO_EXECUTING);
            } else {
                CHECK_EQ(mState, EXECUTING_TO_IDLE);

                CHECK_EQ(
                    countBuffersWeOwn(mPortBuffers[kPortIndexInput]),
                    mPortBuffers[kPortIndexInput].size());

                CHECK_EQ(
                    countBuffersWeOwn(mPortBuffers[kPortIndexOutput]),
                    mPortBuffers[kPortIndexOutput].size());

                status_t err = mOMX->sendCommand(
                        mNode, OMX_CommandStateSet, OMX_StateLoaded);

                CHECK_EQ(err, OK);

                err = freeBuffersOnPort(kPortIndexInput);
                CHECK_EQ(err, OK);

                err = freeBuffersOnPort(kPortIndexOutput);
                CHECK_EQ(err, OK);

                mPortStatus[kPortIndexInput] = ENABLED;
                mPortStatus[kPortIndexOutput] = ENABLED;

                setState(IDLE_TO_LOADED);
            }
            break;
        }

        case OMX_StateExecuting:
        {
            CHECK_EQ(mState, IDLE_TO_EXECUTING);

            CODEC_LOGV("Now Executing.");

            setState(EXECUTING);
            mStateBeforeError = EXECUTING;

            // Buffers will be submitted to the component in the first
            // call to OMXCodec::read as mInitialBufferSubmit is true at
            // this point. This ensures that this on_message call returns,
            // releases the lock and ::init can notice the state change and
            // itself return.
            break;
        }

        case OMX_StateLoaded:
        {
            CHECK_EQ(mState, IDLE_TO_LOADED);

            CODEC_LOGV("Now Loaded.");

            setState(LOADED);
            mStateBeforeError = LOADED;
            break;
        }
        case OMX_StatePause:
        {
           CODEC_LOGV("Now paused.");

           CHECK_EQ(mState, PAUSING);

           setState(PAUSED);
           mStateBeforeError = PAUSED;
           break;
        }
        case OMX_StateInvalid:
        {
            setState(ERROR);
            mStateBeforeError = ERROR; //Component is in invalid state
            break;
        }

        default:
        {
            CHECK(!"should not be here.");
            break;
        }
    }
}

// static
size_t OMXCodec::countBuffersWeOwn(const Vector<BufferInfo> &buffers) {
    size_t n = 0;
    for (size_t i = 0; i < buffers.size(); ++i) {
        if (!buffers[i].mOwnedByComponent) {
            ++n;
        }
    }

    return n;
}

/*Qualcomm Video driver has issues when buffers
 * are freed one by one, so freeing them together
 */
status_t OMXCodec::freeBuffersOnOutPortIfAllAreWithUs() {
    if((countBuffersWeOwn(mPortBuffers[kPortIndexOutput]) != mPortBuffers[kPortIndexOutput].size())) {
        CODEC_LOGV("Some Buffers are with component");
        return OK;
    }
    bool weOwnAllBuffers = true;
    Vector<BufferInfo> *buffers = &mPortBuffers[kPortIndexOutput];
    for (size_t i = 0; i < buffers->size(); ++i) {
        BufferInfo *info = &buffers->editItemAt(i);
        if(info->mMediaBuffer && info->mMediaBuffer->refcount() != 0){
            CODEC_LOGV("Some Buffers are with renderer");
            weOwnAllBuffers = false;
            break;
        }
    }
    if(weOwnAllBuffers == false) {
        CODEC_LOGV("We dont own all buffers yet");
        return OK;
    }
    return freeBuffersOnPort(kPortIndexOutput,true);
}

status_t OMXCodec::freeBuffersOnPort(
        OMX_U32 portIndex, bool onlyThoseWeOwn) {
    Vector<BufferInfo> *buffers = &mPortBuffers[portIndex];

    status_t stickyErr = OK;

    for (size_t i = buffers->size(); i-- > 0;) {
        BufferInfo *info = &buffers->editItemAt(i);

        if (onlyThoseWeOwn && info->mOwnedByComponent) {
            continue;
        }

        CHECK_EQ(info->mOwnedByComponent, false);

        CODEC_LOGV("freeing buffer %p on port %ld", info->mBuffer, portIndex);

        status_t err =
            mOMX->freeBuffer(mNode, portIndex, info->mBuffer);

        if (err != OK) {
            stickyErr = err;
        }

        if (info->mMediaBuffer != NULL) {
            info->mMediaBuffer->setObserver(NULL);

            // Make sure nobody but us owns this buffer at this point.
            CHECK_EQ(info->mMediaBuffer->refcount(), 0);

            info->mMediaBuffer->release();
            info->mMediaBuffer = NULL;
        }

        buffers->removeAt(i);
    }

    CHECK(onlyThoseWeOwn || buffers->isEmpty());

    return stickyErr;
}

void OMXCodec::onPortSettingsChanged(OMX_U32 portIndex) {
    CODEC_LOGV("PORT_SETTINGS_CHANGED(%ld)", portIndex);

    CHECK(mState == EXECUTING || mState == FLUSHING);
    CHECK_EQ(portIndex, kPortIndexOutput);
    setState(RECONFIGURING);

    if (mQuirks & kNeedsFlushBeforeDisable) {
        if (!flushPortAsync(portIndex)) {
            onCmdComplete(OMX_CommandFlush, portIndex);
        }
    } else {
        disablePortAsync(portIndex);
    }
}

bool OMXCodec::flushPortAsync(OMX_U32 portIndex) {
    CHECK(mState == EXECUTING || mState == RECONFIGURING
            || mState == EXECUTING_TO_IDLE || mState == FLUSHING);

    if ( portIndex == -1 ) {
        mPortStatus[kPortIndexInput] = SHUTTING_DOWN;
        mPortStatus[kPortIndexOutput] = SHUTTING_DOWN;
    } else {
        CODEC_LOGV("flushPortAsync(%ld): we own %d out of %d buffers already.",
             portIndex, countBuffersWeOwn(mPortBuffers[portIndex]),
             mPortBuffers[portIndex].size());

        CHECK_EQ(mPortStatus[portIndex], ENABLED);
        mPortStatus[portIndex] = SHUTTING_DOWN;

        if ((mQuirks & kRequiresFlushCompleteEmulation)
            && countBuffersWeOwn(mPortBuffers[portIndex])
                    == mPortBuffers[portIndex].size()) {
            // No flush is necessary and this component fails to send a
            // flush-complete event in this case.

            return false;
        }
    }

    status_t err =
        mOMX->sendCommand(mNode, OMX_CommandFlush, portIndex);
    CHECK_EQ(err, OK);

    return true;
}

void OMXCodec::disablePortAsync(OMX_U32 portIndex) {
    CHECK(mState == EXECUTING || mState == RECONFIGURING);

    if (mPortStatus[portIndex] == DISABLED || mPortStatus[portIndex] == DISABLING)
       return;

    CHECK_EQ(mPortStatus[portIndex], ENABLED);
    mPortStatus[portIndex] = DISABLING;

    sp<MetaData> oldOutputFormat = mOutputFormat;
    initOutputFormat(mSource->getFormat());

    // Don't notify clients if the output port settings change
    // wasn't of importance to them, i.e. it may be that just the
    // number of buffers has changed and nothing else.
    mOutputPortSettingsHaveChanged =
        formatHasNotablyChanged(oldOutputFormat, mOutputFormat);

    status_t err =
        mOMX->sendCommand(mNode, OMX_CommandPortDisable, portIndex);
    CHECK_EQ(err, OK);

    if(portIndex == kPortIndexInput) {
        freeBuffersOnPort(portIndex, true);
    } else {
        freeBuffersOnOutPortIfAllAreWithUs();
    }
}

void OMXCodec::enablePortAsync(OMX_U32 portIndex) {
    CHECK(mState == EXECUTING || mState == RECONFIGURING);

    CHECK_EQ(mPortStatus[portIndex], DISABLED);
    mPortStatus[portIndex] = ENABLING;

    status_t err =
        mOMX->sendCommand(mNode, OMX_CommandPortEnable, portIndex);
    CHECK_EQ(err, OK);
}

void OMXCodec::fillOutputBuffers() {
    CHECK(mState == EXECUTING || mState == FLUSHING);

    // This is a workaround for some decoders not properly reporting
    // end-of-output-stream. If we own all input buffers and also own
    // all output buffers and we already signalled end-of-input-stream,
    // the end-of-output-stream is implied.
    //
    // NOTE: Thumbnail mode needs a call to fillOutputBuffer in order
    // to get the decoded frame from the component. Currently,
    // thumbnail mode calls emptyBuffer with an EOS flag on its first
    // frame and sets mSignalledEOS to true, so without the check for
    // !mThumbnailMode, fillOutputBuffer will never be called.
    if (!mThumbnailMode) {
        if (mSignalledEOS
                && countBuffersWeOwn(mPortBuffers[kPortIndexInput])
                    == mPortBuffers[kPortIndexInput].size()
                && countBuffersWeOwn(mPortBuffers[kPortIndexOutput])
                    == mPortBuffers[kPortIndexOutput].size()) {
            mNoMoreOutputData = true;
            mBufferFilled.signal();

            return;
        }
    }

    Vector<BufferInfo> *buffers = &mPortBuffers[kPortIndexOutput];
    for (size_t i = 0; i < buffers->size(); ++i) {
        fillOutputBuffer(&buffers->editItemAt(i));
    }
}

void OMXCodec::drainInputBuffers() {
    CHECK(mState == EXECUTING || mState == RECONFIGURING || mState == FLUSHING);
    size_t CAMERA_BUFFERS = 0;
    Vector<BufferInfo> *buffers = &mPortBuffers[kPortIndexInput];
    for (size_t i = 0; i < buffers->size(); ++i) {
		//we need do this since camera holds 3 buffer + 1 is for index starts from 0
		//if we don't do this it will be a deadlock since we will be waiting for camera to be done
		//and camera will not give any more unless we give release
        char value[PROPERTY_VALUE_MAX];
        if (!property_get("ro.board.platform", value, "1")
            || !strcmp(value, "msm7627_surf") || !strcmp(value, "msm7627_ffa")
            || !strcmp(value, "msm7627a")
            || !strcmp(value, "msm7625_surf") || !strcmp(value, "msm7625_ffa"))
           CAMERA_BUFFERS = 1;
        else
           CAMERA_BUFFERS = 4;

        if (mIsEncoder && (mQuirks & kAvoidMemcopyInputRecordingFrames) && (i == CAMERA_BUFFERS) )
            break;
        drainInputBuffer(&buffers->editItemAt(i));
    }
}

void OMXCodec::drainInputBuffer(BufferInfo *info) {
    CHECK_EQ(info->mOwnedByComponent, false);

    if (mSignalledEOS) {
        return;
    }

    if (mCodecSpecificDataIndex < mCodecSpecificData.size()) {
        const CodecSpecificData *specific =
            mCodecSpecificData[mCodecSpecificDataIndex];

        size_t size = specific->mSize;

        if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_AVC, mMIME)
                && !(mQuirks & kWantsNALFragments)) {
            static const uint8_t kNALStartCode[4] =
                    { 0x00, 0x00, 0x00, 0x01 };

            CHECK(info->mSize >= specific->mSize + 4);

            size += 4;

            memcpy(info->mData, kNALStartCode, 4);
            memcpy((uint8_t *)info->mData + 4,
                   specific->mData, specific->mSize);
        } else {
            CHECK(info->mSize >= specific->mSize);
            memcpy(info->mData, specific->mData, specific->mSize);
        }

        mNoMoreOutputData = false;

        CODEC_LOGV("calling emptyBuffer with codec specific data");

        status_t err = mOMX->emptyBuffer(
                mNode, info->mBuffer, 0, size,
                OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_CODECCONFIG,
                0);
        CHECK_EQ(err, OK);

        info->mOwnedByComponent = true;

        ++mCodecSpecificDataIndex;
        return;
    }

    if (mPaused) {
        return;
    }

    status_t err;

    bool signalEOS = false;
    int64_t timestampUs = 0;

    size_t offset = 0;
    int32_t n = 0;
    for (;;) {
        MediaBuffer *srcBuffer;
        MediaSource::ReadOptions options;
        if (mSkipTimeUs >= 0) {
            options.setSkipFrame(mSkipTimeUs);
        }
        if (mSeekTimeUs >= 0) {
            if (mLeftOverBuffer) {
                mLeftOverBuffer->release();
                mLeftOverBuffer = NULL;
            }
            options.setSeekTo(mSeekTimeUs, mSeekMode);

            mSeekTimeUs = -1;
            mSeekMode = ReadOptions::SEEK_CLOSEST_SYNC;
            mBufferFilled.signal();

            err = mSource->read(&srcBuffer, &options);

            if (err == OK) {
                int64_t targetTimeUs;
                if (srcBuffer->meta_data()->findInt64(
                            kKeyTargetTime, &targetTimeUs)
                        && targetTimeUs >= 0) {
                    mTargetTimeUs = targetTimeUs;
                } else {
                    mTargetTimeUs = -1;
                }
            }
        } else if (mLeftOverBuffer) {
            srcBuffer = mLeftOverBuffer;
            mLeftOverBuffer = NULL;

            err = OK;
        } else {
            err = mSource->read(&srcBuffer, &options);
        }

        if (err == ERROR_CORRUPT_NAL) {
            LOGW("Ignore Corrupt NAL");
            continue;
        }
        else if (err != OK) {
            signalEOS = true;
            mFinalStatus = err;
            mSignalledEOS = true;
            break;
        }

        size_t remainingBytes = info->mSize - offset;
        size_t srcBufferLength = srcBuffer->range_length();

        if (srcBufferLength > remainingBytes) {
            if (offset == 0) {
                CODEC_LOGE(
                     "Codec's input buffers are too small to accomodate "
                     "buffer read from source (info->mSize = %d, srcLength = %d)",
                     info->mSize, srcBufferLength);

                srcBuffer->release();
                srcBuffer = NULL;

                setState(ERROR);
                return;
            }

            mLeftOverBuffer = srcBuffer;
            break;
        }

        // Do not release the media buffer if it stores meta data
        // instead of YUV data. The release is delayed until
        // EMPTY_BUFFER_DONE callback is received.
        bool releaseBuffer = true;
        if (mIsEncoder && (mQuirks & kAvoidMemcopyInputRecordingFrames)) {
            CHECK(mOMXLivesLocally && offset == 0);
            OMX_BUFFERHEADERTYPE *header = (OMX_BUFFERHEADERTYPE *) info->mBuffer;
            header->pBuffer = (OMX_U8 *) srcBuffer->data() + srcBuffer->range_offset();
        } else {
            if (mQuirks & kStoreMetaDataInInputVideoBuffers) {
                releaseBuffer = false;
                info->mMediaBuffer = srcBuffer;
            }
            if (srcBuffer->data()) {
                memcpy((uint8_t *)info->mData + offset,
                        (const uint8_t *)srcBuffer->data() + srcBuffer->range_offset(),
                        srcBufferLength);
            } else {
                if (srcBufferLength != 0) {
                    LOGW("Source buffer was NULL but the size wasn't 0 bytes (is %d bytes)", srcBufferLength);
                    srcBufferLength = 0;
                }
            }
        }

        int64_t lastBufferTimeUs;
        CHECK(srcBuffer->meta_data()->findInt64(kKeyTime, &lastBufferTimeUs));
        CHECK(lastBufferTimeUs >= 0);

        if (offset == 0) {
            timestampUs = lastBufferTimeUs;
        }

        offset += srcBufferLength;

        if (mIsEncoder && (mQuirks & kAvoidMemcopyInputRecordingFrames)) {
            info->mMediaBuffer = srcBuffer;
        } else if (releaseBuffer) {
            srcBuffer->release();
            srcBuffer = NULL;
        }

        ++n;

        if (!(mQuirks & kSupportsMultipleFramesPerInputBuffer)) {
            break;
        }

        int64_t coalescedDurationUs = lastBufferTimeUs - timestampUs;

        if (coalescedDurationUs > 250000ll) {
            // Don't coalesce more than 250ms worth of encoded data at once.
            break;
        }
    }

    if (n > 1) {
        LOGV("coalesced %d frames into one input buffer", n);
    }

    OMX_U32 flags = OMX_BUFFERFLAG_ENDOFFRAME;

    if (mIsEncoder && mSendEOS) {
        LOGI("drainInputBuffer instructed to send EOS to component");
        signalEOS = true;
        mSignalledEOS = true;
        mFinalStatus = err;
    }

    if (signalEOS) {
        flags |= OMX_BUFFERFLAG_EOS;
        if ( mIsEncoder && ( mQuirks & kRequiresEOSMessage ) ) {
            if (info->mMediaBuffer != NULL ) {
                size_t off = info->mMediaBuffer->range_offset();
                info->mMediaBuffer->set_range( 0, 0 );
            }
            offset = 0;
        }
    } else if (mThumbnailMode) {
        // Because we don't get an EOS after getting the first frame, we
        // need to notify the component with OMX_BUFFERFLAG_EOS, set
        // mNoMoreOutputData to false so fillOutputBuffer gets called on
        // the first output buffer (see comment in fillOutputBuffer), and
        // mSignalledEOS must be true so drainInputBuffer is not executed
        // on extra frames. Setting mFinalStatus to ERROR_END_OF_STREAM as
        // we dont want to return OK and NULL buffer in read.
        flags |= OMX_BUFFERFLAG_EOS;
        mNoMoreOutputData = false;
        mSignalledEOS = true;
        mFinalStatus = ERROR_END_OF_STREAM;
    } else {
        mNoMoreOutputData = false;
    }

    CODEC_LOGV("Calling emptyBuffer on buffer %p, (length %d), "
               "timestamp %lld us (%.2f secs)",
               info->mBuffer, offset,
               timestampUs, timestampUs / 1E6);

    err = mOMX->emptyBuffer(
            mNode, info->mBuffer, 0, offset,
            flags, timestampUs);

    if (err != OK) {
        setState(ERROR);
        return;
    }

    info->mOwnedByComponent = true;

    // This component does not ever signal the EOS flag on output buffers,
    // Thanks for nothing.
    if (mSignalledEOS && !strcmp(mComponentName, "OMX.TI.Video.encoder")) {
        mNoMoreOutputData = true;
        mBufferFilled.signal();
    }
}

void OMXCodec::fillOutputBuffer(BufferInfo *info) {
    CHECK_EQ(info->mOwnedByComponent, false);

    if (mNoMoreOutputData) {
        CODEC_LOGV("There is no more output data available, not "
             "calling fillOutputBuffer");
        return;
    }

    CODEC_LOGV("Calling fill_buffer on buffer %p", info->mBuffer);
    status_t err = mOMX->fillBuffer(mNode, info->mBuffer);

    if (err != OK) {
        CODEC_LOGE("fillBuffer failed w/ error 0x%08x", err);

        setState(ERROR);
        return;
    }

    info->mOwnedByComponent = true;
}

void OMXCodec::drainInputBuffer(IOMX::buffer_id buffer) {
    Vector<BufferInfo> *buffers = &mPortBuffers[kPortIndexInput];
    for (size_t i = 0; i < buffers->size(); ++i) {
        if ((*buffers)[i].mBuffer == buffer) {
            drainInputBuffer(&buffers->editItemAt(i));
            return;
        }
    }

    CHECK(!"should not be here.");
}

void OMXCodec::fillOutputBuffer(IOMX::buffer_id buffer) {
    Vector<BufferInfo> *buffers = &mPortBuffers[kPortIndexOutput];
    for (size_t i = 0; i < buffers->size(); ++i) {
        if ((*buffers)[i].mBuffer == buffer) {
            fillOutputBuffer(&buffers->editItemAt(i));
            return;
        }
    }

    CHECK(!"should not be here.");
}

void OMXCodec::setState(State newState) {
    mState = newState;
    mAsyncCompletion.signal();

    // This may cause some spurious wakeups but is necessary to
    // unblock the reader if we enter ERROR state.
    mBufferFilled.signal();
}

void OMXCodec::setRawAudioFormat(
        OMX_U32 portIndex, int32_t sampleRate, int32_t numChannels) {

    // port definition
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = portIndex;
    def.format.audio.cMIMEType = NULL;
    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);
    def.format.audio.eEncoding = OMX_AUDIO_CodingPCM;
    CHECK_EQ(mOMX->setParameter(mNode, OMX_IndexParamPortDefinition,
            &def, sizeof(def)), OK);

    // pcm param
    OMX_AUDIO_PARAM_PCMMODETYPE pcmParams;
    InitOMXParams(&pcmParams);
    pcmParams.nPortIndex = portIndex;

    err = mOMX->getParameter(
            mNode, OMX_IndexParamAudioPcm, &pcmParams, sizeof(pcmParams));

    CHECK_EQ(err, OK);

    pcmParams.nChannels = numChannels;
    pcmParams.eNumData = OMX_NumericalDataSigned;
    pcmParams.bInterleaved = OMX_TRUE;
    pcmParams.nBitPerSample = 16;
    pcmParams.nSamplingRate = sampleRate;
    pcmParams.ePCMMode = OMX_AUDIO_PCMModeLinear;

    if (numChannels == 1) {
        pcmParams.eChannelMapping[0] = OMX_AUDIO_ChannelCF;
    } else {
        CHECK_EQ(numChannels, 2);

        pcmParams.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
        pcmParams.eChannelMapping[1] = OMX_AUDIO_ChannelRF;
    }

    err = mOMX->setParameter(
            mNode, OMX_IndexParamAudioPcm, &pcmParams, sizeof(pcmParams));

    CHECK_EQ(err, OK);
}

static OMX_AUDIO_AMRBANDMODETYPE pickModeFromBitRate(bool isAMRWB, int32_t bps) {
    if (isAMRWB) {
        if (bps <= 6600) {
            return OMX_AUDIO_AMRBandModeWB0;
        } else if (bps <= 8850) {
            return OMX_AUDIO_AMRBandModeWB1;
        } else if (bps <= 12650) {
            return OMX_AUDIO_AMRBandModeWB2;
        } else if (bps <= 14250) {
            return OMX_AUDIO_AMRBandModeWB3;
        } else if (bps <= 15850) {
            return OMX_AUDIO_AMRBandModeWB4;
        } else if (bps <= 18250) {
            return OMX_AUDIO_AMRBandModeWB5;
        } else if (bps <= 19850) {
            return OMX_AUDIO_AMRBandModeWB6;
        } else if (bps <= 23050) {
            return OMX_AUDIO_AMRBandModeWB7;
        }

        // 23850 bps
        return OMX_AUDIO_AMRBandModeWB8;
    } else {  // AMRNB
        if (bps <= 4750) {
            return OMX_AUDIO_AMRBandModeNB0;
        } else if (bps <= 5150) {
            return OMX_AUDIO_AMRBandModeNB1;
        } else if (bps <= 5900) {
            return OMX_AUDIO_AMRBandModeNB2;
        } else if (bps <= 6700) {
            return OMX_AUDIO_AMRBandModeNB3;
        } else if (bps <= 7400) {
            return OMX_AUDIO_AMRBandModeNB4;
        } else if (bps <= 7950) {
            return OMX_AUDIO_AMRBandModeNB5;
        } else if (bps <= 10200) {
            return OMX_AUDIO_AMRBandModeNB6;
        }

        // 12200 bps
        return OMX_AUDIO_AMRBandModeNB7;
    }
}

void OMXCodec::setAMRFormat(bool isWAMR, int32_t bitRate) {
    OMX_U32 portIndex = mIsEncoder ? kPortIndexOutput : kPortIndexInput;

    OMX_AUDIO_PARAM_AMRTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = portIndex;

    status_t err =
        mOMX->getParameter(mNode, OMX_IndexParamAudioAmr, &def, sizeof(def));

    CHECK_EQ(err, OK);

    def.eAMRFrameFormat = OMX_AUDIO_AMRFrameFormatFSF;

    def.eAMRBandMode = pickModeFromBitRate(isWAMR, bitRate);
    err = mOMX->setParameter(mNode, OMX_IndexParamAudioAmr, &def, sizeof(def));
    CHECK_EQ(err, OK);

    ////////////////////////

    if (mIsEncoder) {
        sp<MetaData> format = mSource->getFormat();
        int32_t sampleRate;
        int32_t numChannels;
        CHECK(format->findInt32(kKeySampleRate, &sampleRate));
        CHECK(format->findInt32(kKeyChannelCount, &numChannels));

        setRawAudioFormat(kPortIndexInput, sampleRate, numChannels);
    }
}

void OMXCodec::setAACFormat(int32_t numChannels, int32_t sampleRate, int32_t bitRate) {
    if (mIsEncoder) {
        //////////////// input port ////////////////////
        setRawAudioFormat(kPortIndexInput, sampleRate, numChannels);

        //////////////// output port ////////////////////
        // format
        OMX_AUDIO_PARAM_PORTFORMATTYPE format;
        format.nPortIndex = kPortIndexOutput;
        format.nIndex = 0;
        status_t err = OMX_ErrorNone;
        while (OMX_ErrorNone == err) {
            CHECK_EQ(mOMX->getParameter(mNode, OMX_IndexParamAudioPortFormat,
                    &format, sizeof(format)), OK);
            if (format.eEncoding == OMX_AUDIO_CodingAAC) {
                break;
            }
            format.nIndex++;
        }
        CHECK_EQ(OK, err);
        CHECK_EQ(mOMX->setParameter(mNode, OMX_IndexParamAudioPortFormat,
                &format, sizeof(format)), OK);

        // port definition
        OMX_PARAM_PORTDEFINITIONTYPE def;
        InitOMXParams(&def);
        def.nPortIndex = kPortIndexOutput;
        CHECK_EQ(mOMX->getParameter(mNode, OMX_IndexParamPortDefinition,
                &def, sizeof(def)), OK);
        def.format.audio.bFlagErrorConcealment = OMX_TRUE;
        def.format.audio.eEncoding = OMX_AUDIO_CodingAAC;
        CHECK_EQ(mOMX->setParameter(mNode, OMX_IndexParamPortDefinition,
                &def, sizeof(def)), OK);

        // profile
        OMX_AUDIO_PARAM_AACPROFILETYPE profile;
        InitOMXParams(&profile);
        profile.nPortIndex = kPortIndexOutput;
        CHECK_EQ(mOMX->getParameter(mNode, OMX_IndexParamAudioAac,
                &profile, sizeof(profile)), OK);
        profile.nChannels = numChannels;
        profile.eChannelMode = (numChannels == 1?
                OMX_AUDIO_ChannelModeMono: OMX_AUDIO_ChannelModeStereo);
        profile.nSampleRate = sampleRate;
        profile.nBitRate = bitRate;
        profile.nAudioBandWidth = 0;
        profile.nFrameLength = 0;
        profile.nAACtools = OMX_AUDIO_AACToolAll;
        profile.nAACERtools = OMX_AUDIO_AACERNone;
        profile.eAACProfile = OMX_AUDIO_AACObjectLC;
        profile.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4FF;
        CHECK_EQ(mOMX->setParameter(mNode, OMX_IndexParamAudioAac,
                &profile, sizeof(profile)), OK);

    } else {
        OMX_AUDIO_PARAM_AACPROFILETYPE profile;
        InitOMXParams(&profile);
        profile.nPortIndex = kPortIndexInput;

        status_t err = mOMX->getParameter(
                mNode, OMX_IndexParamAudioAac, &profile, sizeof(profile));
        CHECK_EQ(err, OK);

        profile.nChannels = numChannels;
        profile.nSampleRate = sampleRate;
        profile.eAACStreamFormat = OMX_AUDIO_AACStreamFormatRAW;

        err = mOMX->setParameter(
                mNode, OMX_IndexParamAudioAac, &profile, sizeof(profile));
        CHECK_EQ(err, OK);
    }
}

void OMXCodec::setAC3Format(int32_t /*numChannels*/, int32_t /*sampleRate*/) {
/*
    QOMX_AUDIO_PARAM_AC3TYPE profileAC3;
    QOMX_AUDIO_PARAM_AC3PP profileAC3PP;
    OMX_INDEXTYPE indexTypeAC3;
    OMX_INDEXTYPE indexTypeAC3PP;
    OMX_PARAM_PORTDEFINITIONTYPE portParam;

    //configure input port
    InitOMXParams(&portParam);
    portParam.nPortIndex = 0;
    status_t err = mOMX->getParameter(
       mNode, OMX_IndexParamPortDefinition, &portParam, sizeof(portParam));
    CHECK_EQ(err, OK);

    portParam.nBufferSize = 2*4096;
    err = mOMX->setParameter(
       mNode, OMX_IndexParamPortDefinition, &portParam, sizeof(portParam));
    CHECK_EQ(err, OK);

    //configure output port
    portParam.nPortIndex = 1;
    err = mOMX->getParameter(
       mNode, OMX_IndexParamPortDefinition, &portParam, sizeof(portParam));
    CHECK_EQ(err, OK);
    portParam.nBufferSize = 20*4096;
    err = mOMX->setParameter(
       mNode, OMX_IndexParamPortDefinition, &portParam, sizeof(portParam));
    CHECK_EQ(err, OK);

    err = mOMX->getExtensionIndex(mNode, OMX_QCOM_INDEX_PARAM_AC3TYPE, &indexTypeAC3);

    InitOMXParams(&profileAC3);
    profileAC3.nPortIndex = kPortIndexInput;
    err = mOMX->getParameter(mNode, indexTypeAC3, &profileAC3, sizeof(profileAC3));
    CHECK_EQ(err, OK);

    profileAC3.nSamplingRate  =  sampleRate;
    profileAC3.nChannels      =  2;
    profileAC3.eChannelConfig =  OMX_AUDIO_AC3_CHANNEL_CONFIG_2_0;

    LOGE("numChannels = %d, profileAC3.nChannels = %d", numChannels, profileAC3.nChannels);

    err = mOMX->setParameter(mNode, indexTypeAC3, &profileAC3, sizeof(profileAC3));
    CHECK_EQ(err, OK);

    //for output port
    OMX_AUDIO_PARAM_PCMMODETYPE profilePcm;
    InitOMXParams(&profilePcm);
    profilePcm.nPortIndex = kPortIndexOutput;
    err = mOMX->getParameter(mNode, OMX_IndexParamAudioPcm, &profilePcm, sizeof(profilePcm));
    profilePcm.nSamplingRate  =  sampleRate;
    err = mOMX->setParameter(mNode, OMX_IndexParamAudioPcm, &profilePcm, sizeof(profilePcm));
    LOGE("for output port profileAC3.nSamplingRate = %d", profileAC3.nSamplingRate);

    mOMX->getExtensionIndex(mNode, OMX_QCOM_INDEX_PARAM_AC3PP, &indexTypeAC3PP);

    InitOMXParams(&profileAC3PP);
    profileAC3PP.nPortIndex = kPortIndexInput;
    err = mOMX->getParameter(mNode, indexTypeAC3PP, &profileAC3PP, sizeof(profileAC3PP));
    CHECK_EQ(err, OK);

    int i;
    int channel_routing[6];

    for (i=0; i<6; i++) {
        channel_routing[i] = -1;
    }
    for (i=0; i<6; i++) {
        profileAC3PP.eChannelRouting[i] =  (OMX_AUDIO_AC3_CHANNEL_ROUTING)channel_routing[i];
    }
    profileAC3PP.eChannelRouting[0] =  OMX_AUDIO_AC3_CHANNEL_LEFT;
    profileAC3PP.eChannelRouting[1] =  OMX_AUDIO_AC3_CHANNEL_RIGHT;
    err = mOMX->setParameter(mNode, indexTypeAC3PP, &profileAC3PP, sizeof(profileAC3PP));
    CHECK_EQ(err, OK);
*/
}
status_t OMXCodec::setWMAFormat(const sp<MetaData> &meta)
{
    if (mIsEncoder) {
        CODEC_LOGE("WMA encoding not supported");
        return OK;
    } else {
        int32_t version;
        OMX_AUDIO_PARAM_WMATYPE paramWMA;
        QOMX_AUDIO_PARAM_WMA10PROTYPE paramWMA10;

        CHECK(meta->findInt32(kKeyWMAVersion, &version));

        int32_t numChannels;
        int32_t bitRate;
        int32_t sampleRate;
        int32_t encodeOptions;
        int32_t blockAlign;
        int32_t bitspersample;
        int32_t formattag;
        int32_t advencopt1;
        int32_t advencopt2;
        int32_t VirtualPktSize;
        if(version==kTypeWMAPro || version==kTypeWMALossLess) {
           CHECK(meta->findInt32(kKeyWMABitspersample, &bitspersample));
           CHECK(meta->findInt32(kKeyWMAFormatTag, &formattag));
           CHECK(meta->findInt32(kKeyWMAAdvEncOpt1,&advencopt1));
           CHECK(meta->findInt32(kKeyWMAAdvEncOpt2,&advencopt2));
           CHECK(meta->findInt32(kKeyWMAVirPktSize,&VirtualPktSize));
        }

        if(version==kTypeWMA) {
           InitOMXParams(&paramWMA);
           paramWMA.nPortIndex = kPortIndexInput;
        } else if(version==kTypeWMAPro || version==kTypeWMALossLess) {
           InitOMXParams(&paramWMA10);
           paramWMA10.nPortIndex = kPortIndexInput;
        }
        CHECK(meta->findInt32(kKeyChannelCount, &numChannels));
        CHECK(meta->findInt32(kKeySampleRate, &sampleRate));
        CHECK(meta->findInt32(kKeyBitRate, &bitRate));
        CHECK(meta->findInt32(kKeyWMAEncodeOpt, &encodeOptions));
        CHECK(meta->findInt32(kKeyWMABlockAlign, &blockAlign));
        CODEC_LOGV("Channels: %d, SampleRate: %d, BitRate; %d"
                   "EncodeOptions: %d, blockAlign: %d", numChannels,
                   sampleRate, bitRate, encodeOptions, blockAlign);

        if(sampleRate>48000 || numChannels>2)
        {
           LOGE("Unsupported samplerate/channels");
           return ERROR_UNSUPPORTED;
        }
        if(version==kTypeWMAPro || version==kTypeWMALossLess)
        {
           CODEC_LOGV("Bitspersample: %d, wmaformattag: %d,"
                      "advencopt1: %d, advencopt2: %d VirtualPktSize %d", bitspersample,
                      formattag, advencopt1, advencopt2, VirtualPktSize);
        }

        status_t err = OK;
        OMX_INDEXTYPE index;
        if(version==kTypeWMA) {
           err = mOMX->getParameter(
                   mNode, OMX_IndexParamAudioWma, &paramWMA, sizeof(paramWMA));
        } else if(version==kTypeWMAPro || version==kTypeWMALossLess) {
           mOMX->getExtensionIndex(mNode,"OMX.Qualcomm.index.audio.wma10Pro",&index);
           err = mOMX->getParameter(
                   mNode, index, &paramWMA10, sizeof(paramWMA10));
        }
        CHECK_EQ(err, OK);

        if(version==kTypeWMA) {
           paramWMA.nChannels = numChannels;
           paramWMA.nSamplingRate = sampleRate;
           paramWMA.nEncodeOptions = encodeOptions;
           paramWMA.nBitRate = bitRate;
           //paramWMA.eFormat = eFormat;
           //paramWMA.eProfile = eProfile;
           paramWMA.nBlockAlign = blockAlign;
        } else if(version==kTypeWMAPro || version==kTypeWMALossLess) {
           paramWMA10.nChannels = numChannels;
           paramWMA10.nSamplingRate = sampleRate;
           paramWMA10.nEncodeOptions = encodeOptions;
           paramWMA10.nBitRate = bitRate;
           //paramWMA10.eFormat = eFormat;
           //paramWMA10.eProfile = eProfile;
           paramWMA10.nBlockAlign = blockAlign;
           //paramWMA10.nSuperBlockAlign = superBlockAlign;
        }
        if(version==kTypeWMAPro || version==kTypeWMALossLess) {
           paramWMA10.advancedEncodeOpt = advencopt1;
           paramWMA10.advancedEncodeOpt2 = advencopt2;
           paramWMA10.formatTag = formattag;
           paramWMA10.validBitsPerSample = bitspersample;
           paramWMA10.nVirtualPktSize = VirtualPktSize;
        }

        if(version==kTypeWMA) {
           err = mOMX->setParameter(
                   mNode, OMX_IndexParamAudioWma, &paramWMA, sizeof(paramWMA));
        } else if(version==kTypeWMAPro || version==kTypeWMALossLess) {
           err = mOMX->setParameter(
                   mNode, index, &paramWMA10, sizeof(paramWMA10));
        }
        return err;
    }
}

void OMXCodec::setImageOutputFormat(
        OMX_COLOR_FORMATTYPE format, OMX_U32 width, OMX_U32 height) {
    CODEC_LOGV("setImageOutputFormat(%ld, %ld)", width, height);

#if 0
    OMX_INDEXTYPE index;
    status_t err = mOMX->get_extension_index(
            mNode, "OMX.TI.JPEG.decode.Config.OutputColorFormat", &index);
    CHECK_EQ(err, OK);

    err = mOMX->set_config(mNode, index, &format, sizeof(format));
    CHECK_EQ(err, OK);
#endif

    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = kPortIndexOutput;

    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);

    CHECK_EQ(def.eDomain, OMX_PortDomainImage);

    OMX_IMAGE_PORTDEFINITIONTYPE *imageDef = &def.format.image;

    CHECK_EQ(imageDef->eCompressionFormat, OMX_IMAGE_CodingUnused);
    imageDef->eColorFormat = format;
    imageDef->nFrameWidth = width;
    imageDef->nFrameHeight = height;

    switch (format) {
        case OMX_COLOR_FormatYUV420PackedPlanar:
        case OMX_COLOR_FormatYUV411Planar:
        {
            def.nBufferSize = (width * height * 3) / 2;
            break;
        }

        case OMX_COLOR_FormatCbYCrY:
        {
            def.nBufferSize = width * height * 2;
            break;
        }

        case OMX_COLOR_Format32bitARGB8888:
        {
            def.nBufferSize = width * height * 4;
            break;
        }

        case OMX_COLOR_Format16bitARGB4444:
        case OMX_COLOR_Format16bitARGB1555:
        case OMX_COLOR_Format16bitRGB565:
        case OMX_COLOR_Format16bitBGR565:
        {
            def.nBufferSize = width * height * 2;
            break;
        }

        default:
            CHECK(!"Should not be here. Unknown color format.");
            break;
    }

    def.nBufferCountActual = def.nBufferCountMin;

    err = mOMX->setParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);
}

void OMXCodec::setJPEGInputFormat(
        OMX_U32 width, OMX_U32 height, OMX_U32 compressedSize) {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = kPortIndexInput;

    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);

    CHECK_EQ(def.eDomain, OMX_PortDomainImage);
    OMX_IMAGE_PORTDEFINITIONTYPE *imageDef = &def.format.image;

    CHECK_EQ(imageDef->eCompressionFormat, OMX_IMAGE_CodingJPEG);
    imageDef->nFrameWidth = width;
    imageDef->nFrameHeight = height;

    def.nBufferSize = compressedSize;
    def.nBufferCountActual = def.nBufferCountMin;

    err = mOMX->setParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);
}

void OMXCodec::addCodecSpecificData(const void *data, size_t size) {
    CodecSpecificData *specific =
        (CodecSpecificData *)malloc(sizeof(CodecSpecificData) + size - 1);

    specific->mSize = size;
    memcpy(specific->mData, data, size);

    mCodecSpecificData.push(specific);
}

void OMXCodec::clearCodecSpecificData() {
    for (size_t i = 0; i < mCodecSpecificData.size(); ++i) {
        free(mCodecSpecificData.editItemAt(i));
    }
    mCodecSpecificData.clear();
    mCodecSpecificDataIndex = 0;
}

status_t OMXCodec::start(MetaData *meta) {
    CODEC_LOGV("OMXCodec::start ");
    Mutex::Autolock autoLock(mLock);
    if(mIsPaused) {
        while (isIntermediateState(mState)) {
            mAsyncCompletion.wait(mLock);
        }
        CHECK_EQ(mState, PAUSED);
        status_t err = mOMX->sendCommand(mNode,
        OMX_CommandStateSet, OMX_StateExecuting);
        CHECK_EQ(err, OK);
        setState(IDLE_TO_EXECUTING);
        mIsPaused = false;
        mPaused = false;
        while (mState != EXECUTING && mState != ERROR) {
            mAsyncCompletion.wait(mLock);
        }
        return mState == ERROR ? UNKNOWN_ERROR : OK;
    }
    if (mState != LOADED) {
        return UNKNOWN_ERROR;
    }

    sp<MetaData> params = new MetaData;
    if (mQuirks & kWantsNALFragments) {
        params->setInt32(kKeyWantsNALFragments, true);
    }
    if (meta) {
        int64_t startTimeUs = 0;
        int64_t timeUs;
        if (meta->findInt64(kKeyTime, &timeUs)) {
            startTimeUs = timeUs;
        }
        params->setInt64(kKeyTime, startTimeUs);
    }
    status_t err = mSource->start(params.get());

    if (err != OK) {
        return err;
    }

    mCodecSpecificDataIndex = 0;
    mInitialBufferSubmit = true;
    mSignalledEOS = false;
    mNoMoreOutputData = false;
    mOutputPortSettingsHaveChanged = false;
    mSeekTimeUs = -1;
    mSeekMode = ReadOptions::SEEK_CLOSEST_SYNC;
    mTargetTimeUs = -1;
    mFilledBuffers.clear();
    mPaused = false;

    return init();
}

status_t OMXCodec::pause() {
   CODEC_LOGV("pause mState=%d", mState);

   Mutex::Autolock autoLock(mLock);

   if (mState != EXECUTING) {
       return UNKNOWN_ERROR;
   }

   while (isIntermediateState(mState)) {
       mAsyncCompletion.wait(mLock);
   }
   status_t err = mOMX->sendCommand(mNode,
       OMX_CommandStateSet, OMX_StatePause);
   CHECK_EQ(err, OK);
   setState(PAUSING);

   mIsPaused = true;

   while (mState != PAUSED && mState != ERROR) {
       mAsyncCompletion.wait(mLock);
   }
   return mState == ERROR ? UNKNOWN_ERROR : OK;
}

status_t OMXCodec::stop() {
    CODEC_LOGV("stop mState=%d", mState);

    Mutex::Autolock autoLock(mLock);

    while (isIntermediateState(mState)) {
        mAsyncCompletion.wait(mLock);
    }

    //If error was received during executing or paused states,
    //OMXCodec state is restored so that the component returns
    //all buffers and the component state is moved to loaded.
    if (mState == ERROR) {
        mState = mStateBeforeError;
    }

    switch (mState) {
        case LOADED:
        case ERROR:
            break;
        case PAUSED:
        case EXECUTING:
        {
            //send EOS to h/w encoders.
            if ( mIsEncoder && ( mQuirks & kRequiresEOSMessage )) {
                sendEOSToOMXComponent( ); //TODO - will this adversely affect 7x27?
            }
            setState(EXECUTING_TO_IDLE);

            if (mQuirks & kRequiresFlushBeforeShutdown) {
                CODEC_LOGE("This component requires a flush before transitioning "
                     "from EXECUTING to IDLE...");
            //DSP supports flushing of ports simultaneously. Flushing individual port is not supported.
#if 0
                bool emulateInputFlushCompletion =
                    !flushPortAsync(kPortIndexInput);

                bool emulateOutputFlushCompletion =
                    !flushPortAsync(kPortIndexOutput);

                if (emulateInputFlushCompletion) {
                    onCmdComplete(OMX_CommandFlush, kPortIndexInput);
                }

                if (emulateOutputFlushCompletion) {
                    onCmdComplete(OMX_CommandFlush, kPortIndexOutput);
                }
#endif

                bool emulateFlushCompletion = !flushPortAsync(kPortIndexBoth);
                if (emulateFlushCompletion) {
                    onCmdComplete(OMX_CommandFlush, kPortIndexBoth);
                }
            } else {
                mPortStatus[kPortIndexInput] = SHUTTING_DOWN;
                mPortStatus[kPortIndexOutput] = SHUTTING_DOWN;

                status_t err =
                    mOMX->sendCommand(mNode, OMX_CommandStateSet, OMX_StateIdle);
                CHECK_EQ(err, OK);
            }

            while (mState != LOADED && mState != ERROR) {
                mAsyncCompletion.wait(mLock);
            }

            break;
        }

        default:
        {
            CHECK(!"should not be here.");
            break;
        }
    }

    if (mLeftOverBuffer) {
        mLeftOverBuffer->release();
        mLeftOverBuffer = NULL;
    }

    LOGV("stop: no more output data, sending the mBufferFilled signal");
    mNoMoreOutputData = true;
    mBufferFilled.signal();

    mSource->stop();

    int i = 0;
    while(getStrongCount() != 1) {
        usleep(100);
        i++;
        if( i > 5) {
            LOGE("Someone else, besides client, is holding the refernce. We might have trouble.");
            break;
        }
    }

    CODEC_LOGV("stopped");

    return OK;
}

sp<MetaData> OMXCodec::getFormat() {
    Mutex::Autolock autoLock(mLock);

    int32_t targetColorFormat;
    if (mOutputFormat->findInt32(kKeyColorFormat, &targetColorFormat)) {
        CODEC_LOGV("OMXCodec::getFormat targetColorFormat (OutPut) 0x%x",targetColorFormat);
        }
    return mOutputFormat;
}

status_t OMXCodec::read(
        MediaBuffer **buffer, const ReadOptions *options) {
    *buffer = NULL;

    Mutex::Autolock autoLock(mLock);

    if (mState == PAUSING) {
        return INVALID_OPERATION;
    }
    if (mState != EXECUTING && mState != RECONFIGURING) {
        return UNKNOWN_ERROR;
    }

    bool seeking = false;
    int64_t seekTimeUs;
    ReadOptions::SeekMode seekMode;
    if (options && options->getSeekTo(&seekTimeUs, &seekMode)) {
        seeking = true;
    }
    int64_t skipTimeUs;
    if (options && options->getSkipFrame(&skipTimeUs)) {
        mSkipTimeUs = skipTimeUs;
    } else {
        mSkipTimeUs = -1;
    }

    if (mInitialBufferSubmit) {
        mInitialBufferSubmit = false;

        if (seeking) {
            if(seekTimeUs < 0)
               return ERROR_UNSUPPORTED;
            mSeekTimeUs = seekTimeUs;
            mSeekMode = seekMode;

            // There's no reason to trigger the code below, there's
            // nothing to flush yet.
            seeking = false;
            mPaused = false;
        }

        drainInputBuffers();

        if (mState == EXECUTING) {
            // Otherwise mState == RECONFIGURING and this code will trigger
            // after the output port is reenabled.
            fillOutputBuffers();
        }
    }

    if (seeking) {
        CODEC_LOGV("seeking to %lld us (%.2f secs)", seekTimeUs, seekTimeUs / 1E6);

        mSignalledEOS = false;

        CHECK(seekTimeUs >= 0);
        mSeekTimeUs = seekTimeUs;
        mSeekMode = seekMode;

        if(mState != EXECUTING && mState != RECONFIGURING){
           LOGE("Seek is issued in a invalid state");
           return INVALID_OPERATION;
        }

        if(mState == EXECUTING){
           mFilledBuffers.clear();
           setState(FLUSHING);
        //DSP supports flushing of ports simultaneously. Flushing individual port is not supported.
#if 0
        bool emulateInputFlushCompletion = !flushPortAsync(kPortIndexInput);
        bool emulateOutputFlushCompletion = !flushPortAsync(kPortIndexOutput);

        if (emulateInputFlushCompletion) {
            onCmdComplete(OMX_CommandFlush, kPortIndexInput);
        }

        if (emulateOutputFlushCompletion) {
            onCmdComplete(OMX_CommandFlush, kPortIndexOutput);
        }
#endif
           bool emulateFlushCompletion = !flushPortAsync(kPortIndexBoth);
           if (emulateFlushCompletion) {
               onCmdComplete(OMX_CommandFlush, kPortIndexBoth);
           }
        }

        // for cases like , seek is issued while in port reconfiguring state
        // wait till the reconfiguration is completed
        while (mSeekTimeUs >= 0) {
               mBufferFilled.wait(mLock);
        }
    }

    while (mState != ERROR && !mNoMoreOutputData && !mOutputPortSettingsHaveChanged && mFilledBuffers.empty()) {
        mBufferFilled.wait(mLock);
    }
    if(seeking)
    {
        if(mState == RECONFIGURING){
           LOGV("Received reconfig while in seek, wait till reconfig is complete");
           mBufferFilled.wait(mLock);
        }
        if(mState != EXECUTING){
           CHECK_EQ(mState, FLUSHING);
           setState(EXECUTING);
        }
        else {
           CHECK_EQ(mState, EXECUTING);
        }
    }
    if (mState == ERROR) {
        return UNKNOWN_ERROR;
    }

     if (mOutputPortSettingsHaveChanged) {
        mOutputPortSettingsHaveChanged = false;
        mFilledBuffers.clear();
        return INFO_FORMAT_CHANGED;
    }

    if (mFilledBuffers.empty()) {
        return mSignalledEOS ? mFinalStatus : ERROR_END_OF_STREAM;
    }

    if (mOutputPortSettingsHaveChanged) {
        mOutputPortSettingsHaveChanged = false;

        return INFO_FORMAT_CHANGED;
    }

    size_t index = *mFilledBuffers.begin();
    mFilledBuffers.erase(mFilledBuffers.begin());

    BufferInfo *info = &mPortBuffers[kPortIndexOutput].editItemAt(index);
    info->mMediaBuffer->add_ref();
    *buffer = info->mMediaBuffer;

    return OK;
}

void OMXCodec::signalBufferReturned(MediaBuffer *buffer) {
    Mutex::Autolock autoLock(mLock);

    Vector<BufferInfo> *buffers = &mPortBuffers[kPortIndexOutput];
    for (size_t i = 0; i < buffers->size(); ++i) {
        BufferInfo *info = &buffers->editItemAt(i);

        if (info->mMediaBuffer == buffer) {
            CHECK((mPortStatus[kPortIndexOutput] == ENABLED) || (mPortStatus[kPortIndexOutput] == DISABLING)
                 || (mPortStatus[kPortIndexOutput] == SHUTTING_DOWN));
             if(mPortStatus[kPortIndexOutput] == ENABLED)
                 fillOutputBuffer(info);
             else if(mPortStatus[kPortIndexOutput] == DISABLING) {
                 freeBuffersOnOutPortIfAllAreWithUs();
             }
            return;
        }
    }

    CHECK(!"should not be here.");
}

static const char *imageCompressionFormatString(OMX_IMAGE_CODINGTYPE type) {
    static const char *kNames[] = {
        "OMX_IMAGE_CodingUnused",
        "OMX_IMAGE_CodingAutoDetect",
        "OMX_IMAGE_CodingJPEG",
        "OMX_IMAGE_CodingJPEG2K",
        "OMX_IMAGE_CodingEXIF",
        "OMX_IMAGE_CodingTIFF",
        "OMX_IMAGE_CodingGIF",
        "OMX_IMAGE_CodingPNG",
        "OMX_IMAGE_CodingLZW",
        "OMX_IMAGE_CodingBMP",
    };

    size_t numNames = sizeof(kNames) / sizeof(kNames[0]);

    if (type < 0 || (size_t)type >= numNames) {
        return "UNKNOWN";
    } else {
        return kNames[type];
    }
}

static const char *colorFormatString(OMX_COLOR_FORMATTYPE type) {
    static const char *kNames[] = {
        "OMX_COLOR_FormatUnused",
        "OMX_COLOR_FormatMonochrome",
        "OMX_COLOR_Format8bitRGB332",
        "OMX_COLOR_Format12bitRGB444",
        "OMX_COLOR_Format16bitARGB4444",
        "OMX_COLOR_Format16bitARGB1555",
        "OMX_COLOR_Format16bitRGB565",
        "OMX_COLOR_Format16bitBGR565",
        "OMX_COLOR_Format18bitRGB666",
        "OMX_COLOR_Format18bitARGB1665",
        "OMX_COLOR_Format19bitARGB1666",
        "OMX_COLOR_Format24bitRGB888",
        "OMX_COLOR_Format24bitBGR888",
        "OMX_COLOR_Format24bitARGB1887",
        "OMX_COLOR_Format25bitARGB1888",
        "OMX_COLOR_Format32bitBGRA8888",
        "OMX_COLOR_Format32bitARGB8888",
        "OMX_COLOR_FormatYUV411Planar",
        "OMX_COLOR_FormatYUV411PackedPlanar",
        "OMX_COLOR_FormatYUV420Planar",
        "OMX_COLOR_FormatYUV420PackedPlanar",
        "OMX_COLOR_FormatYUV420SemiPlanar",
        "OMX_COLOR_FormatYUV422Planar",
        "OMX_COLOR_FormatYUV422PackedPlanar",
        "OMX_COLOR_FormatYUV422SemiPlanar",
        "OMX_COLOR_FormatYCbYCr",
        "OMX_COLOR_FormatYCrYCb",
        "OMX_COLOR_FormatCbYCrY",
        "OMX_COLOR_FormatCrYCbY",
        "OMX_COLOR_FormatYUV444Interleaved",
        "OMX_COLOR_FormatRawBayer8bit",
        "OMX_COLOR_FormatRawBayer10bit",
        "OMX_COLOR_FormatRawBayer8bitcompressed",
        "OMX_COLOR_FormatL2",
        "OMX_COLOR_FormatL4",
        "OMX_COLOR_FormatL8",
        "OMX_COLOR_FormatL16",
        "OMX_COLOR_FormatL24",
        "OMX_COLOR_FormatL32",
        "OMX_COLOR_FormatYUV420PackedSemiPlanar",
        "OMX_COLOR_FormatYUV422PackedSemiPlanar",
        "OMX_COLOR_Format18BitBGR666",
        "OMX_COLOR_Format24BitARGB6666",
        "OMX_COLOR_Format24BitABGR6666",
    };

    size_t numNames = sizeof(kNames) / sizeof(kNames[0]);

    if (type == OMX_QCOM_COLOR_FormatYVU420SemiPlanar) {
        return "OMX_QCOM_COLOR_FormatYVU420SemiPlanar";
    } else if (type == QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka) {
        return "QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka";
    }else if (type == QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka) {
        return "QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka";
    } else if (type ==  (OMX_QCOM_COLOR_FormatYVU420SemiPlanar ^ QOMX_INTERLACE_FLAG)) {
        return "OMX_QCOM_COLOR_FormatYVU420SemiPlanar(Interlace)";
    } else if (type == QOMX_VIDEO_CodingDivx) {
        return "QOMX_VIDEO_CodingDivx";
    } else if (type == (QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka ^ QOMX_INTERLACE_FLAG)) {
        return "QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka(Interlace)";
    } else if (type == (OMX_COLOR_FormatYUV420SemiPlanar ^ QOMX_INTERLACE_FLAG)) {
        return "OMX_COLOR_FormatYUV420SemiPlanar(Interlace)";
    } else if (type < 0 || (size_t)type >= numNames) {
        return "UNKNOWN";
    } else {
        return kNames[type];
    }
}

static const char *videoCompressionFormatString(OMX_VIDEO_CODINGTYPE type) {
    static const char *kNames[] = {
        "OMX_VIDEO_CodingUnused",
        "OMX_VIDEO_CodingAutoDetect",
        "OMX_VIDEO_CodingMPEG2",
        "OMX_VIDEO_CodingH263",
        "OMX_VIDEO_CodingMPEG4",
        "OMX_VIDEO_CodingWMV",
        "OMX_VIDEO_CodingRV",
        "OMX_VIDEO_CodingAVC",
        "OMX_VIDEO_CodingMJPEG",
        "OMX_VIDEO_CodingMPEG2",
    };

    size_t numNames = sizeof(kNames) / sizeof(kNames[0]);

    if (type < 0 || (size_t)type >= numNames) {
        return "UNKNOWN";
    } else {
        return kNames[type];
    }
}

static const char *audioCodingTypeString(OMX_AUDIO_CODINGTYPE type) {
    static const char *kNames[] = {
        "OMX_AUDIO_CodingUnused",
        "OMX_AUDIO_CodingAutoDetect",
        "OMX_AUDIO_CodingPCM",
        "OMX_AUDIO_CodingADPCM",
        "OMX_AUDIO_CodingAMR",
        "OMX_AUDIO_CodingGSMFR",
        "OMX_AUDIO_CodingGSMEFR",
        "OMX_AUDIO_CodingGSMHR",
        "OMX_AUDIO_CodingPDCFR",
        "OMX_AUDIO_CodingPDCEFR",
        "OMX_AUDIO_CodingPDCHR",
        "OMX_AUDIO_CodingTDMAFR",
        "OMX_AUDIO_CodingTDMAEFR",
        "OMX_AUDIO_CodingQCELP8",
        "OMX_AUDIO_CodingQCELP13",
        "OMX_AUDIO_CodingEVRC",
        "OMX_AUDIO_CodingSMV",
        "OMX_AUDIO_CodingG711",
        "OMX_AUDIO_CodingG723",
        "OMX_AUDIO_CodingG726",
        "OMX_AUDIO_CodingG729",
        "OMX_AUDIO_CodingAAC",
        "OMX_AUDIO_CodingMP3",
        "OMX_AUDIO_CodingSBC",
        "OMX_AUDIO_CodingVORBIS",
        "OMX_AUDIO_CodingWMA",
        "OMX_AUDIO_CodingRA",
        "OMX_AUDIO_CodingMIDI",
    };

    size_t numNames = sizeof(kNames) / sizeof(kNames[0]);

    if (type < 0 || (size_t)type >= numNames) {
        return "UNKNOWN";
    } else {
        return kNames[type];
    }
}

static const char *audioPCMModeString(OMX_AUDIO_PCMMODETYPE type) {
    static const char *kNames[] = {
        "OMX_AUDIO_PCMModeLinear",
        "OMX_AUDIO_PCMModeALaw",
        "OMX_AUDIO_PCMModeMULaw",
    };

    size_t numNames = sizeof(kNames) / sizeof(kNames[0]);

    if (type < 0 || (size_t)type >= numNames) {
        return "UNKNOWN";
    } else {
        return kNames[type];
    }
}

static const char *amrBandModeString(OMX_AUDIO_AMRBANDMODETYPE type) {
    static const char *kNames[] = {
        "OMX_AUDIO_AMRBandModeUnused",
        "OMX_AUDIO_AMRBandModeNB0",
        "OMX_AUDIO_AMRBandModeNB1",
        "OMX_AUDIO_AMRBandModeNB2",
        "OMX_AUDIO_AMRBandModeNB3",
        "OMX_AUDIO_AMRBandModeNB4",
        "OMX_AUDIO_AMRBandModeNB5",
        "OMX_AUDIO_AMRBandModeNB6",
        "OMX_AUDIO_AMRBandModeNB7",
        "OMX_AUDIO_AMRBandModeWB0",
        "OMX_AUDIO_AMRBandModeWB1",
        "OMX_AUDIO_AMRBandModeWB2",
        "OMX_AUDIO_AMRBandModeWB3",
        "OMX_AUDIO_AMRBandModeWB4",
        "OMX_AUDIO_AMRBandModeWB5",
        "OMX_AUDIO_AMRBandModeWB6",
        "OMX_AUDIO_AMRBandModeWB7",
        "OMX_AUDIO_AMRBandModeWB8",
    };

    size_t numNames = sizeof(kNames) / sizeof(kNames[0]);

    if (type < 0 || (size_t)type >= numNames) {
        return "UNKNOWN";
    } else {
        return kNames[type];
    }
}

static const char *amrFrameFormatString(OMX_AUDIO_AMRFRAMEFORMATTYPE type) {
    static const char *kNames[] = {
        "OMX_AUDIO_AMRFrameFormatConformance",
        "OMX_AUDIO_AMRFrameFormatIF1",
        "OMX_AUDIO_AMRFrameFormatIF2",
        "OMX_AUDIO_AMRFrameFormatFSF",
        "OMX_AUDIO_AMRFrameFormatRTPPayload",
        "OMX_AUDIO_AMRFrameFormatITU",
    };

    size_t numNames = sizeof(kNames) / sizeof(kNames[0]);

    if (type < 0 || (size_t)type >= numNames) {
        return "UNKNOWN";
    } else {
        return kNames[type];
    }
}

void OMXCodec::dumpPortStatus(OMX_U32 portIndex) {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = portIndex;

    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);

    printf("%s Port = {\n", portIndex == kPortIndexInput ? "Input" : "Output");

    CHECK((portIndex == kPortIndexInput && def.eDir == OMX_DirInput)
          || (portIndex == kPortIndexOutput && def.eDir == OMX_DirOutput));

    printf("  nBufferCountActual = %ld\n", def.nBufferCountActual);
    printf("  nBufferCountMin = %ld\n", def.nBufferCountMin);
    printf("  nBufferSize = %ld\n", def.nBufferSize);

    switch (def.eDomain) {
        case OMX_PortDomainImage:
        {
            const OMX_IMAGE_PORTDEFINITIONTYPE *imageDef = &def.format.image;

            printf("\n");
            printf("  // Image\n");
            printf("  nFrameWidth = %ld\n", imageDef->nFrameWidth);
            printf("  nFrameHeight = %ld\n", imageDef->nFrameHeight);
            printf("  nStride = %ld\n", imageDef->nStride);

            printf("  eCompressionFormat = %s\n",
                   imageCompressionFormatString(imageDef->eCompressionFormat));

            printf("  eColorFormat = %s\n",
                   colorFormatString(imageDef->eColorFormat));

            break;
        }

        case OMX_PortDomainVideo:
        {
            OMX_VIDEO_PORTDEFINITIONTYPE *videoDef = &def.format.video;

            printf("\n");
            printf("  // Video\n");
            printf("  nFrameWidth = %ld\n", videoDef->nFrameWidth);
            printf("  nFrameHeight = %ld\n", videoDef->nFrameHeight);
            printf("  nStride = %ld\n", videoDef->nStride);

            printf("  eCompressionFormat = %s\n",
                   videoCompressionFormatString(videoDef->eCompressionFormat));

            printf("  eColorFormat = %s\n",
                   colorFormatString(videoDef->eColorFormat));

            break;
        }

        case OMX_PortDomainAudio:
        {
            OMX_AUDIO_PORTDEFINITIONTYPE *audioDef = &def.format.audio;

            printf("\n");
            printf("  // Audio\n");
            printf("  eEncoding = %s\n",
                   audioCodingTypeString(audioDef->eEncoding));

            if (audioDef->eEncoding == OMX_AUDIO_CodingPCM) {
                OMX_AUDIO_PARAM_PCMMODETYPE params;
                InitOMXParams(&params);
                params.nPortIndex = portIndex;

                err = mOMX->getParameter(
                        mNode, OMX_IndexParamAudioPcm, &params, sizeof(params));
                CHECK_EQ(err, OK);

                printf("  nSamplingRate = %ld\n", params.nSamplingRate);
                printf("  nChannels = %ld\n", params.nChannels);
                printf("  bInterleaved = %d\n", params.bInterleaved);
                printf("  nBitPerSample = %ld\n", params.nBitPerSample);

                printf("  eNumData = %s\n",
                       params.eNumData == OMX_NumericalDataSigned
                        ? "signed" : "unsigned");

                printf("  ePCMMode = %s\n", audioPCMModeString(params.ePCMMode));
            } else if (audioDef->eEncoding == OMX_AUDIO_CodingAMR) {
                OMX_AUDIO_PARAM_AMRTYPE amr;
                InitOMXParams(&amr);
                amr.nPortIndex = portIndex;

                err = mOMX->getParameter(
                        mNode, OMX_IndexParamAudioAmr, &amr, sizeof(amr));
                CHECK_EQ(err, OK);

                printf("  nChannels = %ld\n", amr.nChannels);
                printf("  eAMRBandMode = %s\n",
                        amrBandModeString(amr.eAMRBandMode));
                printf("  eAMRFrameFormat = %s\n",
                        amrFrameFormatString(amr.eAMRFrameFormat));
            }

            break;
        }

        default:
        {
            printf("  // Unknown\n");
            break;
        }
    }

    printf("}\n");
}

void OMXCodec::initOutputFormat(const sp<MetaData> &inputFormat) {
    mOutputFormat = new MetaData;
    mOutputFormat->setCString(kKeyDecoderComponent, mComponentName);
    if (mIsEncoder) {
        int32_t timeScale;
        if (inputFormat->findInt32(kKeyTimeScale, &timeScale)) {
            mOutputFormat->setInt32(kKeyTimeScale, timeScale);
        }
    }

    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);
    def.nPortIndex = kPortIndexOutput;

    status_t err = mOMX->getParameter(
            mNode, OMX_IndexParamPortDefinition, &def, sizeof(def));
    CHECK_EQ(err, OK);

    switch (def.eDomain) {
        case OMX_PortDomainImage:
        {
            OMX_IMAGE_PORTDEFINITIONTYPE *imageDef = &def.format.image;
            CHECK_EQ(imageDef->eCompressionFormat, OMX_IMAGE_CodingUnused);

            mOutputFormat->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_RAW);
            mOutputFormat->setInt32(kKeyColorFormat, imageDef->eColorFormat);
            mOutputFormat->setInt32(kKeyWidth, imageDef->nFrameWidth);
            mOutputFormat->setInt32(kKeyHeight, imageDef->nFrameHeight);
            break;
        }

        case OMX_PortDomainAudio:
        {
            OMX_AUDIO_PORTDEFINITIONTYPE *audio_def = &def.format.audio;

            if (audio_def->eEncoding == OMX_AUDIO_CodingPCM) {
                OMX_AUDIO_PARAM_PCMMODETYPE params;
                InitOMXParams(&params);
                params.nPortIndex = kPortIndexOutput;

                err = mOMX->getParameter(
                        mNode, OMX_IndexParamAudioPcm, &params, sizeof(params));
                CHECK_EQ(err, OK);

                CHECK_EQ(params.eNumData, OMX_NumericalDataSigned);
                CHECK_EQ(params.nBitPerSample, 16);
                CHECK_EQ(params.ePCMMode, OMX_AUDIO_PCMModeLinear);

                int32_t numChannels, sampleRate;
                inputFormat->findInt32(kKeyChannelCount, &numChannels);
                inputFormat->findInt32(kKeySampleRate, &sampleRate);

                if ((OMX_U32)numChannels != params.nChannels) {
                    LOGW("Codec outputs a different number of channels than "
                         "the input stream contains (contains %d channels, "
                         "codec outputs %ld channels).",
                         numChannels, params.nChannels);
                }

                mOutputFormat->setCString(
                        kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_RAW);

                // Use the codec-advertised number of channels, as some
                // codecs appear to output stereo even if the input data is
                // mono. If we know the codec lies about this information,
                // use the actual number of channels instead.
                mOutputFormat->setInt32(
                        kKeyChannelCount,
                        (mQuirks & kDecoderLiesAboutNumberOfChannels)
                            ? numChannels : params.nChannels);

                mOutputFormat->setInt32(kKeySampleRate, params.nSamplingRate);
                mOutputFormat->setInt32(kKeyChannelCount, params.nChannels);
            } else if (audio_def->eEncoding == OMX_AUDIO_CodingAMR) {
                OMX_AUDIO_PARAM_AMRTYPE amr;
                InitOMXParams(&amr);
                amr.nPortIndex = kPortIndexOutput;

                err = mOMX->getParameter(
                        mNode, OMX_IndexParamAudioAmr, &amr, sizeof(amr));
                CHECK_EQ(err, OK);

                CHECK_EQ(amr.nChannels, 1);
                mOutputFormat->setInt32(kKeyChannelCount, 1);

                if (amr.eAMRBandMode >= OMX_AUDIO_AMRBandModeNB0
                    && amr.eAMRBandMode <= OMX_AUDIO_AMRBandModeNB7) {
                    mOutputFormat->setCString(
                            kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_AMR_NB);
                    mOutputFormat->setInt32(kKeySampleRate, 8000);
                } else if (amr.eAMRBandMode >= OMX_AUDIO_AMRBandModeWB0
                            && amr.eAMRBandMode <= OMX_AUDIO_AMRBandModeWB8) {
                    mOutputFormat->setCString(
                            kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_AMR_WB);
                    mOutputFormat->setInt32(kKeySampleRate, 16000);
                } else {
                    CHECK(!"Unknown AMR band mode.");
                }
            } else if (audio_def->eEncoding == OMX_AUDIO_CodingAAC) {
                mOutputFormat->setCString(
                        kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_AAC);
                int32_t numChannels, sampleRate, bitRate;
                inputFormat->findInt32(kKeyChannelCount, &numChannels);
                inputFormat->findInt32(kKeySampleRate, &sampleRate);
                inputFormat->findInt32(kKeyBitRate, &bitRate);
                mOutputFormat->setInt32(kKeyChannelCount, numChannels);
                mOutputFormat->setInt32(kKeySampleRate, sampleRate);
                mOutputFormat->setInt32(kKeyBitRate, bitRate);
            } else if (audio_def->eEncoding == OMX_AUDIO_CodingQCELP13 ) {
                mOutputFormat->setCString(
                        kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_QCELP);
                int32_t numChannels, sampleRate, bitRate;
                inputFormat->findInt32(kKeyChannelCount, &numChannels);
                inputFormat->findInt32(kKeySampleRate, &sampleRate);
                inputFormat->findInt32(kKeyBitRate, &bitRate);
                mOutputFormat->setInt32(kKeyChannelCount, numChannels);
                mOutputFormat->setInt32(kKeySampleRate, sampleRate);
                mOutputFormat->setInt32(kKeyBitRate, bitRate);
            } else if (audio_def->eEncoding == OMX_AUDIO_CodingEVRC ) {
                mOutputFormat->setCString(
                        kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_EVRC);
                int32_t numChannels, sampleRate, bitRate;
                inputFormat->findInt32(kKeyChannelCount, &numChannels);
                inputFormat->findInt32(kKeySampleRate, &sampleRate);
                inputFormat->findInt32(kKeyBitRate, &bitRate);
                mOutputFormat->setInt32(kKeyChannelCount, numChannels);
                mOutputFormat->setInt32(kKeySampleRate, sampleRate);
                mOutputFormat->setInt32(kKeyBitRate, bitRate);
            } else {
                CHECK(!"Should not be here. Unknown audio encoding.");
            }
            break;
        }

        case OMX_PortDomainVideo:
        {
            OMX_VIDEO_PORTDEFINITIONTYPE *video_def = &def.format.video;

            if (video_def->eCompressionFormat == OMX_VIDEO_CodingUnused) {
                mOutputFormat->setCString(
                        kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_RAW);
            } else if (video_def->eCompressionFormat == OMX_VIDEO_CodingMPEG4) {
                mOutputFormat->setCString(
                        kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_MPEG4);
            } else if (video_def->eCompressionFormat == OMX_VIDEO_CodingH263) {
                mOutputFormat->setCString(
                        kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_H263);
            } else if (video_def->eCompressionFormat == OMX_VIDEO_CodingAVC) {
                mOutputFormat->setCString(
                        kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_AVC);
            } else {
                CHECK(!"Unknown compression format.");
            }

            if (!strcmp(mComponentName, "OMX.PV.avcdec")) {
                // This component appears to be lying to me.
                mOutputFormat->setInt32(
                        kKeyWidth, (video_def->nFrameWidth + 15) & -16);
                mOutputFormat->setInt32(
                        kKeyHeight, (video_def->nFrameHeight + 15) & -16);
            } else {
	      if( mIsEncoder ){
		int32_t width, height;
		bool success = inputFormat->findInt32( kKeyWidth, &width ) &&
                               inputFormat->findInt32( kKeyHeight, &height);
		CHECK( success );
		mOutputFormat->setInt32(kKeyWidth, width );
	        mOutputFormat->setInt32(kKeyHeight, height );

        int32_t frameRate, hfr;
        success = inputFormat->findInt32(kKeyHFR, &hfr);
        success = inputFormat->findInt32(kKeySampleRate, &frameRate);
        CHECK(success);
        mOutputFormat->setInt32(kKeyHFR, hfr);
        mOutputFormat->setInt32(kKeySampleRate, frameRate);
		/*Do dummy call for capability type index so that
                  the component knows we are using pmem!!, we dont use it!!!
		*/

                typedef struct OMXComponentCapabilityFlagsType
                {
                  OMX_BOOL iIsOMXComponentMultiThreaded;
                  OMX_BOOL iOMXComponentSupportsExternalOutputBufferAlloc;
                  OMX_BOOL iOMXComponentSupportsExternalInputBufferAlloc;
                  OMX_BOOL iOMXComponentSupportsMovableInputBuffers;
                  OMX_BOOL iOMXComponentSupportsPartialFrames;
                  OMX_BOOL iOMXComponentUsesNALStartCodes;
                  OMX_BOOL iOMXComponentCanHandleIncompleteFrames;
                  OMX_BOOL iOMXComponentUsesFullAVCFrames;
                } OMXComponentCapabilityFlagsType;

                OMXComponentCapabilityFlagsType junk;
                mOMX->getParameter( mNode, (OMX_INDEXTYPE) OMX_COMPONENT_CAPABILITY_TYPE_INDEX,
                                    &junk, sizeof(junk) );

	      }
	      else {
	        LOGV("video_def->nStride = %d, video_def->nSliceHeight = %d", video_def->nStride,
		     video_def->nSliceHeight );
                LOGV("video_def->nFrameWidth = %d, video_def->nFrameHeight = %d", video_def->nFrameWidth,
                     video_def->nFrameHeight );

                //Update the width,height Stride and Slice Height
                //Allows creation of Renderer with correct parameters
                mOutputFormat->setInt32(kKeyWidth, video_def->nFrameWidth);
                mOutputFormat->setInt32(kKeyHeight, video_def->nFrameHeight);
                mOutputFormat->setInt32(kKeyStride, video_def->nStride);
                mOutputFormat->setInt32(kKeySliceHeight, video_def->nSliceHeight);
	      }
            }

            mOutputFormat->setInt32(kKeyColorFormat, video_def->eColorFormat);
            break;
        }

        default:
        {
            CHECK(!"should not be here, neither audio nor video.");
            break;
        }
    }

#if 0
    //set the rotation value too
    int32_t rotation = 0;
    if( inputFormat->findInt32(kKeyRotation, &rotation ) ){
      LOGV("Setting rotation in output format to %d", rotation );
      mOutputFormat->setInt32( kKeyRotation, rotation );
    }
    else {
      LOGV("InputFormat did not contain any rotation information, setting to 0");
      mOutputFormat->setInt32( kKeyRotation, 0 );
    }
#endif
}


void OMXCodec::parseFlags(uint32_t flags) {
    mGPUComposition = ((flags & kEnableGPUComposition) ? true : false);
    mThumbnailMode = ((flags & kEnableThumbnailMode) ? true : false);

    { //Deal with the 3D flags
        mForce3D |= ((flags & kForce3DTopBottom) ? QOMX_3D_TOP_BOTTOM_VIDEO_FLAG : 0);
        mForce3D |= ((flags & kForce3DLeftRight) ? QOMX_3D_LEFT_RIGHT_VIDEO_FLAG : 0);
        mForce3D |= ((flags & kForce3DRightLeft) ? QOMX_3D_RIGHT_LEFT_VIDEO_FLAG : 0);
        mForce3D |= ((flags & kForce3DBottomTop) ? QOMX_3D_BOTTOM_TOP_VIDEO_FLAG : 0);
    }
}

////////////////////////////////////////////////////////////////////////////////

status_t QueryCodecs(
        const sp<IOMX> &omx,
        const char *mime, bool queryDecoders,
        Vector<CodecCapabilities> *results) {
    results->clear();

    for (int index = 0;; ++index) {
        const char *componentName;

        if (!queryDecoders) {
            componentName = GetCodec(
                    kEncoderInfo, sizeof(kEncoderInfo) / sizeof(kEncoderInfo[0]),
                    mime, index);
        } else {
            componentName = GetCodec(
                    kDecoderInfo, sizeof(kDecoderInfo) / sizeof(kDecoderInfo[0]),
                    mime, index);
        }

        if (!componentName) {
            return OK;
        }

        if (strncmp(componentName, "OMX.", 4)) {
            // Not an OpenMax component but a software codec.

            results->push();
            CodecCapabilities *caps = &results->editItemAt(results->size() - 1);
            caps->mComponentName = componentName;

            continue;
        }

        sp<OMXCodecObserver> observer = new OMXCodecObserver;
        IOMX::node_id node;
        status_t err = omx->allocateNode(componentName, observer, &node);

        if (err != OK) {
            continue;
        }

        OMXCodec::setComponentRole(omx, node, !queryDecoders, mime);

        results->push();
        CodecCapabilities *caps = &results->editItemAt(results->size() - 1);
        caps->mComponentName = componentName;

        OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
        InitOMXParams(&param);

        param.nPortIndex = queryDecoders ? 0 : 1;

        for (param.nProfileIndex = 0;; ++param.nProfileIndex) {
            err = omx->getParameter(
                    node, OMX_IndexParamVideoProfileLevelQuerySupported,
                    &param, sizeof(param));

            if (err != OK) {
                break;
            }

            CodecProfileLevel profileLevel;
            profileLevel.mProfile = param.eProfile;
            profileLevel.mLevel = param.eLevel;

            caps->mProfileLevels.push(profileLevel);
        }

        CHECK_EQ(omx->freeNode(node), OK);
    }
}

status_t OMXCodec::processExtraDataBlocksOfBuffer(MediaBuffer *buffer, OMX_U32 flags) {
    CODEC_LOGV("In ProcessExtraDataBlocksOfBuffer for interlace, buffer = %p, flags = %p",buffer,flags);

    if (!mInterlaceFormatDetected) {
        if (flags & OMX_BUFFERFLAG_EXTRADATA) {
            OMX_OTHER_EXTRADATATYPE *pExtra;
            OMX_STREAMINTERLACEFORMAT *pInterlaceFormat;
            OMX_U8 *pTmp = (OMX_U8 *)(buffer->data()) + buffer->range_offset() + buffer->range_length();

            pExtra = (OMX_OTHER_EXTRADATATYPE *)(((OMX_U32)(pTmp + 3)) & ~3);
            while((pExtra->eType != OMX_ExtraDataNone) && (pExtra->eType != OMX_ExtraDataInterlaceFormat))
            {
                pExtra = (OMX_OTHER_EXTRADATATYPE *) (((OMX_U8 *) pExtra) + pExtra->nSize);
            }

            if(pExtra->eType == OMX_ExtraDataInterlaceFormat)
            {
                pInterlaceFormat = (OMX_STREAMINTERLACEFORMAT *)pExtra->data;

                if(pInterlaceFormat->bInterlaceFormat == OMX_TRUE)
                {
                   if(pInterlaceFormat->nInterlaceFormats == OMX_InterlaceFrameProgressive)
                       LOGV("ProcessExtraDataBlocksOfBuffer: Interlace Type %s","Progressive");
                   else if(pInterlaceFormat->nInterlaceFormats == OMX_InterlaceInterleaveFrameTopFieldFirst)
                       LOGV("ProcessExtraDataBlocksOfBuffer: Interlace Type %s","InterleaveFrameTopFieldFirst");
                   else if(pInterlaceFormat->nInterlaceFormats == OMX_InterlaceInterleaveFrameBottomFieldFirst)
                       LOGV("ProcessExtraDataBlocksOfBuffer: Interlace Type %s","InterleaveFrameBottomFieldFirst");
                   else if(pInterlaceFormat->nInterlaceFormats == OMX_InterlaceFrameTopFieldFirst)
                       LOGV("ProcessExtraDataBlocksOfBuffer: Interlace Type %s","FrameTopFieldFirst");
                   else if(pInterlaceFormat->nInterlaceFormats == OMX_InterlaceFrameBottomFieldFirst)
                       LOGV("ProcessExtraDataBlocksOfBuffer: Interlace Type %s","FrameBottomFieldFirst");
                   else
                   {
                       LOGE("ProcessExtraDataBlocksOfBuffer:Unrecognized interlace format");
                       return ERROR_UNSUPPORTED;
                   }

                   if((pInterlaceFormat->nInterlaceFormats == OMX_InterlaceInterleaveFrameTopFieldFirst) ||
                      (pInterlaceFormat->nInterlaceFormats == OMX_InterlaceInterleaveFrameBottomFieldFirst))
                   {
                       int oldColorFormat, newColorFormat;
                       mOutputFormat->findInt32(kKeyColorFormat, &oldColorFormat);

                       if (oldColorFormat == OMX_COLOR_FormatYUV420SemiPlanar || oldColorFormat == QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka)
                            newColorFormat = (oldColorFormat ^ QOMX_INTERLACE_FLAG);
                       else
                       {
                            LOGE("Unrecognized colorformat %lx; will stick with old color format", oldColorFormat);
                            newColorFormat = oldColorFormat;
                        }

                       CODEC_LOGV("Setting output color format from %s to %s", colorFormatString((OMX_COLOR_FORMATTYPE)oldColorFormat), colorFormatString((OMX_COLOR_FORMATTYPE)newColorFormat));
                       mOutputFormat->setInt32(kKeyColorFormat, newColorFormat);
                   }
                   else
                   {
                       LOGE("ProcessExtraDataBlocksOfBuffer:Unsupported interlace format");
                       return ERROR_UNSUPPORTED;
                   }
                }
            }
            mInterlaceFormatDetected = true;
        }
    }
    return OK;
}

status_t OMXCodec::processSEIData()
{
    CODEC_LOGV("Processing SEI data");
    if (!m3DVideoDetected)
    {
        //We don't want to continue checking every buffer, so we mark as 3D detected
        //regardless. We only take action by xoring the 3d flag when cancel_flag is set
        m3DVideoDetected = true;


        OMX_QCOM_FRAME_PACK_ARRANGEMENT arrangementInfo;
        arrangementInfo.cancel_flag = 1; //Need to initialize to 1, because the core doesn't touch this
                                         //struct if video is not H264
        status_t err = mOMX->getConfig(mNode, (OMX_INDEXTYPE)OMX_QcomIndexConfigVideoFramePackingArrangement,
                                       &arrangementInfo, (size_t)sizeof(arrangementInfo));

        if (err != OK)
        {
            LOGV("Not supported config OMX_QcomIndexConfigVideoFramePackingArrangement");
            return OK;
        }

        int oldColorFormat, newColorFormat;
        mOutputFormat->findInt32(kKeyColorFormat, &oldColorFormat);
        newColorFormat = oldColorFormat;

        if (arrangementInfo.cancel_flag == 1) //not a 3d video
        {
            if (mForce3D)
                newColorFormat ^= mForce3D;
        }
        else
        {
            bool flip = false;
            if (arrangementInfo.content_interpretation_type == 2) //LR should be treated as RL
                flip = true;

            if (arrangementInfo.type == 3) //side by side
            {
                newColorFormat ^= flip ? QOMX_3D_RIGHT_LEFT_VIDEO_FLAG :
                                          QOMX_3D_LEFT_RIGHT_VIDEO_FLAG;
            }
            else if (arrangementInfo.type == 4) //top bottom
            {
                //Flipping top-bottom isn't supported on graphics side yet
                //we'll display what we can without flipping
                if (flip)
                    LOGE("flipping top-bottom 3d video not supported, "
                         "continuing to display as top bottom");
                newColorFormat ^= QOMX_3D_TOP_BOTTOM_VIDEO_FLAG;
            }
            else
            {
                LOGE("This is supposedly a 3d video but the frame arragement [%d] is not supported", (int)arrangementInfo.type);
                return ERROR_UNSUPPORTED;
            }
            CODEC_LOGV("This is a 3d video");
        }

        mOutputFormat->setInt32(kKeyColorFormat, newColorFormat);
    }

    return OK;
}

//very specific to qcom h/w encoders
//needs a valid buffer to be given
//for EOS, just setting EOS flag
//is not enough
status_t OMXCodec::sendEOSToOMXComponent( ) {

    if (mState != EXECUTING) {
        LOGE("EOS send command in wrong state (%d)", mState);
        return INVALID_OPERATION;
    }

    LOGI("Sending EOS to OMX Component");

    Vector<BufferInfo> *buffers = &mPortBuffers[kPortIndexInput];
    //Check if all buffers are with IL client (SF)
    BufferInfo *info = NULL;
    size_t i = 0;
    for (i = 0; i < buffers->size( ); i++ ) {
        info = &buffers->editItemAt(i);
        if ( info->mOwnedByComponent == true ) {
            break;
        }
    }
    mSendEOS = true;
    if (i == buffers->size( )) {
        LOGE("all buffers with IL client,sending EOS from here");
        drainInputBuffer(info);
    }
    CHECK( mState != ERROR );
    return OK;
}

void OMXCodec::setEVRCFormat(int32_t numChannels, int32_t sampleRate, int32_t bitRate) {

    if (mIsEncoder) {
        CHECK(numChannels == 1);
        //////////////// input port ////////////////////
        setRawAudioFormat(kPortIndexInput, sampleRate, numChannels);
        //////////////// output port ////////////////////
        // format
        OMX_AUDIO_PARAM_PORTFORMATTYPE format;
        format.nPortIndex = kPortIndexOutput;
        format.nIndex = 0;
        status_t err = OMX_ErrorNone;
        while (OMX_ErrorNone == err) {
            CHECK_EQ(mOMX->getParameter(mNode, OMX_IndexParamAudioPortFormat,
                    &format, sizeof(format)), OK);
            if (format.eEncoding == OMX_AUDIO_CodingEVRC) {
                break;
            }
            format.nIndex++;
        }
        CHECK_EQ(OK, err);
        CHECK_EQ(mOMX->setParameter(mNode, OMX_IndexParamAudioPortFormat,
                &format, sizeof(format)), OK);

        // port definition
        OMX_PARAM_PORTDEFINITIONTYPE def;
        InitOMXParams(&def);
        def.nPortIndex = kPortIndexOutput;
        def.format.audio.cMIMEType = NULL;
        CHECK_EQ(mOMX->getParameter(mNode, OMX_IndexParamPortDefinition,
                &def, sizeof(def)), OK);
        def.format.audio.bFlagErrorConcealment = OMX_TRUE;
        def.format.audio.eEncoding = OMX_AUDIO_CodingEVRC;
        CHECK_EQ(mOMX->setParameter(mNode, OMX_IndexParamPortDefinition,
                &def, sizeof(def)), OK);

        // profile
        OMX_AUDIO_PARAM_EVRCTYPE profile;
        InitOMXParams(&profile);
        profile.nPortIndex = kPortIndexOutput;
        CHECK_EQ(mOMX->getParameter(mNode, OMX_IndexParamAudioEvrc,
                &profile, sizeof(profile)), OK);
        profile.nChannels = numChannels;
        CHECK_EQ(mOMX->setParameter(mNode, OMX_IndexParamAudioEvrc,
                &profile, sizeof(profile)), OK);
    }
    else
    {
       LOGI("EVRC Decoder");
    }
}

void OMXCodec::setQCELPFormat(int32_t numChannels, int32_t sampleRate, int32_t bitRate) {

    if (mIsEncoder) {
        CHECK(numChannels == 1);
        //////////////// input port ////////////////////
        setRawAudioFormat(kPortIndexInput, sampleRate, numChannels);
        //////////////// output port ////////////////////
        // format
        OMX_AUDIO_PARAM_PORTFORMATTYPE format;
        format.nPortIndex = kPortIndexOutput;
        format.nIndex = 0;
        status_t err = OMX_ErrorNone;
        while (OMX_ErrorNone == err) {
            CHECK_EQ(mOMX->getParameter(mNode, OMX_IndexParamAudioPortFormat,
                    &format, sizeof(format)), OK);
            if (format.eEncoding == OMX_AUDIO_CodingQCELP13) {
                break;
            }
            format.nIndex++;
        }
        CHECK_EQ(OK, err);
        CHECK_EQ(mOMX->setParameter(mNode, OMX_IndexParamAudioPortFormat,
                &format, sizeof(format)), OK);

        // port definition
        OMX_PARAM_PORTDEFINITIONTYPE def;
        InitOMXParams(&def);
        def.nPortIndex = kPortIndexOutput;
        def.format.audio.cMIMEType = NULL;
        CHECK_EQ(mOMX->getParameter(mNode, OMX_IndexParamPortDefinition,
                &def, sizeof(def)), OK);
        def.format.audio.bFlagErrorConcealment = OMX_TRUE;
        def.format.audio.eEncoding = OMX_AUDIO_CodingQCELP13;
        CHECK_EQ(mOMX->setParameter(mNode, OMX_IndexParamPortDefinition,
                &def, sizeof(def)), OK);

        // profile
        OMX_AUDIO_PARAM_QCELP13TYPE profile;
        InitOMXParams(&profile);
        profile.nPortIndex = kPortIndexOutput;
        CHECK_EQ(mOMX->getParameter(mNode, OMX_IndexParamAudioQcelp13,
                &profile, sizeof(profile)), OK);
        profile.nChannels = numChannels;
        CHECK_EQ(mOMX->setParameter(mNode, OMX_IndexParamAudioQcelp13,
                &profile, sizeof(profile)), OK);
    }
    else
    {
       LOGI("QCELP Decoder");
    }
}

}  // namespace android
