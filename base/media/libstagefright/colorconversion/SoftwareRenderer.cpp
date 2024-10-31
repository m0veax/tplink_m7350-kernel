/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "SoftwareRenderer"
#include <utils/Log.h>

#include "../include/SoftwareRenderer.h"
#ifdef USE_ION
#include <binder/MemoryHeapIon.h>
#else
#include <binder/MemoryHeapBase.h>
#include <binder/MemoryHeapPmem.h>
#endif
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/Utils.h>
#include <surfaceflinger/ISurface.h>
#include "gralloc_priv.h"

#ifdef OUTPUT_RGB565_LOGGING
FILE *outputRGBFile;
char outputRGBFilename [] = "/data/RGBoutput.rgb";
#endif

namespace android {
SoftwareRenderer::SoftwareRenderer(
        OMX_COLOR_FORMATTYPE colorFormat,
        const sp<ISurface> &surface,
        size_t displayWidth, size_t displayHeight,
        size_t decodedWidth, size_t decodedHeight,
        int32_t rotationDegrees)
    : mInitCheck(NO_INIT),
      mColorFormat(colorFormat),
      mConverter(colorFormat, OMX_COLOR_Format16bitRGB565),
      mISurface(surface),
      mDisplayWidth(displayWidth),
      mDisplayHeight(displayHeight),
      mDecodedWidth(decodedWidth),
      mDecodedHeight(decodedHeight),
      mFrameSize(mDecodedWidth * mDecodedHeight * 2),  // RGB565
      mIndex(0) {
    size_t alignedDecodedWidth  = ((decodedWidth + 31) & -32);
    if ((mColorFormat == OMX_COLOR_FormatYUV420SemiPlanar ) ||
        (mColorFormat == OMX_QCOM_COLOR_FormatYVU420SemiPlanar )  ||
        (mColorFormat == OMX_COLOR_FormatYUV420Planar )) {
        mFrameSize = (alignedDecodedWidth * mDecodedHeight * 2);
    }
    else {
        alignedDecodedWidth = decodedWidth;
    }
#ifndef USE_ION
    mMemoryHeap = new MemoryHeapBase("/dev/pmem_adsp", 2 * mFrameSize);
#else
    mMemoryHeap = new MemoryHeapIon("/dev/ion", 2 * mFrameSize,0,(1<<ION_HEAP_ADSP_ID));
#endif
    if (mMemoryHeap->heapID() < 0) {
        LOGI("Creating physical memory heap failed, reverting to regular heap.");
        mMemoryHeap = new MemoryHeapBase(2 * mFrameSize);
    }
#ifndef USE_ION
 else {
        sp<MemoryHeapPmem> pmemHeap = new MemoryHeapPmem(mMemoryHeap);
        pmemHeap->slap();
        mMemoryHeap = pmemHeap;
    }
#endif

    CHECK(mISurface.get() != NULL);
    CHECK(mDecodedWidth > 0);
    CHECK(mDecodedHeight > 0);
    CHECK(mMemoryHeap->heapID() >= 0);
    CHECK(mConverter.isValid());
    CHECK(alignedDecodedWidth > 0);

    uint32_t orientation;
    switch (rotationDegrees) {
        case 0: orientation = ISurface::BufferHeap::ROT_0; break;
        case 90: orientation = ISurface::BufferHeap::ROT_90; break;
        case 180: orientation = ISurface::BufferHeap::ROT_180; break;
        case 270: orientation = ISurface::BufferHeap::ROT_270; break;
        default: orientation = ISurface::BufferHeap::ROT_0; break;
    }

    uint32_t flags3D;
    int format3D;
    GET_3D_FORMAT(mColorFormat, format3D);
    switch (format3D) {
        case QOMX_3D_LEFT_RIGHT_VIDEO_FLAG:
            flags3D = HAL_3D_OUT_SIDE_BY_SIDE | HAL_3D_IN_SIDE_BY_SIDE_L_R;
            break;
        case QOMX_3D_TOP_BOTTOM_VIDEO_FLAG:
            flags3D = HAL_3D_OUT_TOP_BOTTOM | HAL_3D_IN_TOP_BOTTOM;
            break;
        default: //not a 3D colorformat
            flags3D = 0;
            break;
    }

    ISurface::BufferHeap bufferHeap(
            mDisplayWidth, mDisplayHeight,
            alignedDecodedWidth, mDecodedHeight,
            PIXEL_FORMAT_RGB_565 | flags3D,
            orientation, 0,
            mMemoryHeap);

    status_t err = mISurface->registerBuffers(bufferHeap);

    if (err != OK) {
        LOGW("ISurface failed to register buffers (0x%08x)", err);
    }

    mInitCheck = err;

#ifdef OUTPUT_RGB565_LOGGING
    outputRGBFile = fopen (outputRGBFilename, "ab");
    if (!outputRGBFile) {
        LOGE("RGB Output Buffer Open failed");
    }
#endif
}

SoftwareRenderer::~SoftwareRenderer() {
    mISurface->unregisterBuffers();

#ifdef OUTPUT_RGB565_LOGGING
    if(outputRGBFile) {
        fclose (outputRGBFile);
        outputRGBFile = NULL;
    }
#endif
}

status_t SoftwareRenderer::initCheck() const {
    return mInitCheck;
}

void SoftwareRenderer::render(
        const void *data, size_t size, void *platformPrivate) {
    if (mInitCheck != OK) {
        return;
    }

    size_t offset = mIndex * mFrameSize;
    void *dst = (uint8_t *)mMemoryHeap->getBase() + offset;
    size_t dstSkip = mDecodedWidth << 1;

    /* aligning the destination skip width for these color formats */
    if (((mColorFormat == OMX_COLOR_FormatYUV420SemiPlanar )||
         (mColorFormat == OMX_QCOM_COLOR_FormatYVU420SemiPlanar)||
         (mColorFormat == OMX_COLOR_FormatYUV420Planar))) {
            dstSkip = ((mDecodedWidth + 31) & -32) << 1;
       }
    mConverter.convert(
            mDecodedWidth, mDecodedHeight,
            data, 0, dst, dstSkip);

#ifdef OUTPUT_RGB565_LOGGING
    if(outputRGBFile) {
        fwrite (dst, 1, mFrameSize, outputRGBFile);
        //Comment the fwrite above and uncomment the below
        //lines to log RGB from FD and offset
        //void  * testd = mMemoryHeap->getBase() + offset ;
        //fwrite ((const void *)testd, 1, mFrameSize, outputRGBFile);
    }
#endif

    mISurface->postBuffer(offset);
    mIndex = 1 - mIndex;
}

}  // namespace android
