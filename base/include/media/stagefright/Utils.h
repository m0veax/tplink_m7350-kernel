/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (C) 2011 The Linux Foundation
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

#ifndef UTILS_H_

#define UTILS_H_

#include <stdint.h>
#include <utils/Errors.h>

namespace android {

//Define non-standard colorformats
static const int OMX_QCOM_COLOR_FormatYVU420SemiPlanar = 0x7FA30C00;
static const int QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka = 0x7FA30C01;
static const int QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka = 0x7FA30C03;
//Flags that are XORed with colorformats
static const int QOMX_INTERLACE_FLAG = 0x49283654;
static const int QOMX_3D_LEFT_RIGHT_VIDEO_FLAG = 0x23784238;
static const int QOMX_3D_RIGHT_LEFT_VIDEO_FLAG = 0x23784239;
static const int QOMX_3D_TOP_BOTTOM_VIDEO_FLAG = 0x4678239b;
static const int QOMX_3D_BOTTOM_TOP_VIDEO_FLAG = 0x4678239c;
//Compression formats
static const int QOMX_VIDEO_CodingDivx = 0x7FA30C02;
static const int QOMX_VIDEO_CodingSpark = 0x7FA30C03;
static const int QOMX_VIDEO_CodingVp = 0x7FA30C04;

/*
 * Checks if the colorformat is a 3D colorformat.
 * Puts QOMX_3D_TOP_BOTTOM_VIDEO_FLAG
 * or QOMX_3D_LEFT_RIGHT_VIDEO_FLAG in format if top-bottom
 * or left-right respectively.
 * Otherwise puts 0.
 */
#define GET_3D_FORMAT(colorFormat, format) \
    do { \
        switch (colorFormat) { \
            case OMX_COLOR_FormatYUV420Planar: \
            case OMX_COLOR_FormatCbYCrY: \
            case OMX_QCOM_COLOR_FormatYVU420SemiPlanar: \
            case OMX_COLOR_FormatYUV420SemiPlanar: \
            case QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka: \
            case (OMX_QCOM_COLOR_FormatYVU420SemiPlanar ^ QOMX_INTERLACE_FLAG): \
            case (QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka ^ QOMX_INTERLACE_FLAG): \
            case (OMX_COLOR_FormatYUV420SemiPlanar ^ QOMX_INTERLACE_FLAG): \
                 format = 0; \
            break; \
            case (OMX_COLOR_FormatYUV420SemiPlanar ^ QOMX_3D_LEFT_RIGHT_VIDEO_FLAG): \
            case (OMX_QCOM_COLOR_FormatYVU420SemiPlanar ^ QOMX_3D_LEFT_RIGHT_VIDEO_FLAG): \
            case (QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka ^ QOMX_3D_LEFT_RIGHT_VIDEO_FLAG): \
                 format = QOMX_3D_LEFT_RIGHT_VIDEO_FLAG; \
                 break; \
            case (OMX_COLOR_FormatYUV420SemiPlanar ^ QOMX_3D_RIGHT_LEFT_VIDEO_FLAG): \
            case (OMX_QCOM_COLOR_FormatYVU420SemiPlanar ^ QOMX_3D_RIGHT_LEFT_VIDEO_FLAG): \
            case (QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka ^ QOMX_3D_RIGHT_LEFT_VIDEO_FLAG): \
                 format = QOMX_3D_RIGHT_LEFT_VIDEO_FLAG; \
                 break; \
            case (QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka ^ QOMX_3D_TOP_BOTTOM_VIDEO_FLAG): \
            case (OMX_COLOR_FormatYUV420SemiPlanar ^ QOMX_3D_TOP_BOTTOM_VIDEO_FLAG): \
            case (OMX_QCOM_COLOR_FormatYVU420SemiPlanar ^ QOMX_3D_TOP_BOTTOM_VIDEO_FLAG): \
                format = QOMX_3D_TOP_BOTTOM_VIDEO_FLAG; \
                break; \
            case (QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka ^ QOMX_3D_BOTTOM_TOP_VIDEO_FLAG): \
            case (OMX_COLOR_FormatYUV420SemiPlanar ^ QOMX_3D_BOTTOM_TOP_VIDEO_FLAG): \
            case (OMX_QCOM_COLOR_FormatYVU420SemiPlanar ^ QOMX_3D_BOTTOM_TOP_VIDEO_FLAG): \
                format = QOMX_3D_BOTTOM_TOP_VIDEO_FLAG; \
                break; \
            default: \
                format = 0; \
            } \
    } while(0)


/*
 * Checks if the colorformat is an interlaced colorformat.
 * Puts QOMX_INTERLACE_FLAG if so, else puts 0
 * Otherwise returns 0.
 */

#define GET_INTERLACE_FORMAT(colorFormat, format) \
    do { \
        switch (colorFormat) { \
            case OMX_COLOR_FormatYUV420Planar: \
            case OMX_COLOR_FormatCbYCrY: \
            case OMX_QCOM_COLOR_FormatYVU420SemiPlanar: \
            case OMX_COLOR_FormatYUV420SemiPlanar: \
            case QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka: \
            case (OMX_COLOR_FormatYUV420SemiPlanar ^ QOMX_3D_LEFT_RIGHT_VIDEO_FLAG): \
            case (OMX_QCOM_COLOR_FormatYVU420SemiPlanar ^ QOMX_3D_LEFT_RIGHT_VIDEO_FLAG): \
            case (QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka ^ QOMX_3D_LEFT_RIGHT_VIDEO_FLAG): \
            case (QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka ^ QOMX_3D_TOP_BOTTOM_VIDEO_FLAG): \
            case (OMX_COLOR_FormatYUV420SemiPlanar ^ QOMX_3D_TOP_BOTTOM_VIDEO_FLAG): \
            case (OMX_QCOM_COLOR_FormatYVU420SemiPlanar ^ QOMX_3D_TOP_BOTTOM_VIDEO_FLAG): \
                format = 0; \
                break; \
            break; \
            case (OMX_QCOM_COLOR_FormatYVU420SemiPlanar ^ QOMX_INTERLACE_FLAG): \
            case (QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka ^ QOMX_INTERLACE_FLAG): \
            case (OMX_COLOR_FormatYUV420SemiPlanar ^ QOMX_INTERLACE_FLAG): \
                format = QOMX_INTERLACE_FLAG; \
                break; \
            break; \
            default: \
                format =  0; \
        } \
    } while(0)

/*
 * From a colorformat that is composed of the real colorformat XOR some modifier flag,
 * extract the real colorformat.
 *
 * Puts the base color format in format.  If passed in colorformat was invalid, puts 0
 * in format.
 *
 * temp1 and temp2 are temporary 32bit variables used by this macro.
 *
 * Returns the real colorformat if passed in colorformat is valid, returns 0 otherwise.
 */
#define GET_BASE_COLOR_FORMAT(colorFormat, format, temp1, temp2) \
    do { \
        /* Risky declaring inside a macro, mangling the  */ \
        /* variable name so no one else uses the samename*/ \
        GET_INTERLACE_FORMAT(colorFormat, (temp1)); \
        GET_3D_FORMAT(colorFormat, (temp2)); \
        /* Since foo XOR 0 = foo, we can blindly xor with*/ \
        /* the interlace flags regardless of if they're  */ \
        /* set or not.  We'll just end up only xoring out*/ \
        /* flags that we actually set                    */ \
        format  = (colorFormat) ^ (temp1)  ^ (temp2); \
        switch (format) { \
            case OMX_COLOR_FormatYUV420Planar: \
            case OMX_COLOR_FormatCbYCrY: \
            case OMX_QCOM_COLOR_FormatYVU420SemiPlanar: \
            case OMX_COLOR_FormatYUV420SemiPlanar: \
            case QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka: \
                break; \
            default: \
                format = 0; \
        } \
    } while(0)

#define FOURCC(c1, c2, c3, c4) \
    (c1 << 24 | c2 << 16 | c3 << 8 | c4)

uint16_t U16_AT(const uint8_t *ptr);
uint32_t U32_AT(const uint8_t *ptr);
uint64_t U64_AT(const uint8_t *ptr);

uint16_t U16LE_AT(const uint8_t *ptr);
uint32_t U32LE_AT(const uint8_t *ptr);
uint64_t U64LE_AT(const uint8_t *ptr);

uint64_t ntoh64(uint64_t x);
uint64_t hton64(uint64_t x);

typedef struct {
    uint8_t mProfile;
    uint8_t mLevel;
    int32_t mHeightInMBs;
    int32_t mWidthInMBs;
    int32_t mNumRefFrames;
    int32_t mInterlaced;
} SpsInfo;

status_t
parseSps(uint8_t naluSize,const uint8_t *encodedBytes, SpsInfo *info);

}  // namespace android

#endif  // UTILS_H_
