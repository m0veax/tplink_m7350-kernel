/*--------------------------------------------------------------------------
Copyright (c) 2011, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 * Neither the name of The Linux Foundation nor
 the names of its contributors may be used to endorse or promote
 products derived from this software without specific prior written
 permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

//#define LOG_NDEBUG 0
#define LOG_TAG "NativeBuffer"
#include <utils/Log.h>
#include <cutils/properties.h>
#include <gralloc_priv.h>
#include <OMX_QCOMExtns.h>
#include <media/stagefright/NativeBuffer.h>

namespace android {
    NativeBuffer::NativeBuffer(int w, int h, int fmat, int fd, int size, int offset, int base):BASE() {
        LOGV("Creating Native Buffer: 0x%x",fmat);
        width  = w;
        height = h;
        /*Handling very few cases for now. Should be sufficient for 7x30 and 8x60 */
        if(QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka == fmat) {
                LOGV("Set the HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED ");
                format = HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED;
        } else if (QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka == fmat){
                LOGV("Set the HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO for 7x27");
                format = HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO;
        } else {
            LOGE("Error translating color format. Defaulting to YUV420 semi planar");
            format = HAL_PIXEL_FORMAT_YCrCb_420_SP;
        }
        int flags = PRIV_FLAGS_USES_PMEM;
        private_handle_t* priv = new private_handle_t(fd, size, flags);
        if(priv == NULL)
        {
            LOGE("Private handle is NULL");
            handle = NULL;
        }
        else
        {
        priv->offset = offset;
        priv->base   = base;
        handle  = (buffer_handle_t) priv;
        LOGV("Created native buf = %p", this);
        }
    }

    NativeBuffer::~NativeBuffer()
    {
            LOGV("Destroying native buffer...");
            delete((private_handle_t*) handle);
    }

    android_native_buffer_t* NativeBuffer::getNativeBuffer() const
    {
        LOGV("Got native Buffer = %p", this);
        return static_cast<android_native_buffer_t*>(
            const_cast<NativeBuffer*>(this));
   }
}
