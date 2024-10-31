/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (c) 2010-2011, The Linux Foundation
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

#ifndef COLOR_CONVERTER_H_

#define COLOR_CONVERTER_H_

#include <sys/types.h>

#include <stdint.h>

#include <OMX_Video.h>

namespace android {

struct ColorConverter {
    ColorConverter(OMX_COLOR_FORMATTYPE from, OMX_COLOR_FORMATTYPE to);
    ~ColorConverter();

    bool isValid() const;

    void convert(
            size_t width, size_t height,
            const void *srcBits, size_t srcSkip,
            void *dstBits, size_t dstSkip);

private:
    OMX_COLOR_FORMATTYPE mSrcFormat, mDstFormat;
    uint8_t *mClip;

    uint8_t *initClip();

    void convertCbYCrY(
            size_t width, size_t height,
            const void *srcBits, size_t srcSkip,
            void *dstBits, size_t dstSkip);

    void convertYUV420Planar(
            size_t width, size_t height,
            const void *srcBits, size_t srcSkip,
            void *dstBits, size_t dstSkip);

    void convertQCOMYUV420SemiPlanar(
            size_t width, size_t height,
            const void *srcBits, size_t srcSkip,
            void *dstBits, size_t dstSkip);

    void convertYUV420SemiPlanar(
            size_t width, size_t height,
            const void *srcBits, size_t srcSkip,
            void *dstBits, size_t dstSkip);

    void convertYUV420SemiPlanar32Aligned(
            size_t width, size_t height,
            const void *srcBits, size_t srcSkip,
            void *dstBits, size_t dstSkip,
            size_t alignedWidth);

    ColorConverter(const ColorConverter &);
    ColorConverter &operator=(const ColorConverter &);
};



enum ColorConvertFormat {
    RGB565 = 1,
    YCbCr420Tile,
    YCbCr420SP,
    YCbCr420P,
    YCrCb420P,
};

enum ColorConvertFlags {
    COLOR_CONVERT_ALIGN_NONE = 1,
    COLOR_CONVERT_CENTER_OUTPUT = 1<<1,
    COLOR_CONVERT_ALIGN_16 =   1<<4,
    COLOR_CONVERT_ALIGN_2048 = 1<<11,
    COLOR_CONVERT_ALIGN_8192 = 1<<13,
};

struct ColorConvertParams {
    size_t width;
    size_t height;

    size_t cropWidth;
    size_t cropHeight;

    size_t cropLeft;
    size_t cropRight;
    size_t cropTop;
    size_t cropBottom;

    ColorConvertFormat colorFormat;
    const void * data;
    int fd;

    uint64_t flags;
};

typedef int (* ConvertFn)(ColorConvertParams src,
                          ColorConvertParams dst, uint8_t *adjustedClip);

int convert(ColorConvertParams src, ColorConvertParams dst,
            uint8_t *adjustedClip);

}  // namespace android

#endif  // COLOR_CONVERTER_H_
