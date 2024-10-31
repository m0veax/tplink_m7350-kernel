/*
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "DumpFrame.h"
#include "Layer.h"

#include <cutils/properties.h>
#include <cutils/native_handle.h>

#include <ui/PixelFormat.h>

#include <utils/Errors.h>
#include <utils/Log.h>

#include "SurfaceFlinger.h"

#include "gralloc_priv.h"

namespace android {

   /* actual dump */
   void DumpFrame::dump(const Layer* const layer, const Region& clip)
   {
      // done dumping frames?, set dumpframe to 0
      // user can go ahead and re-setprop
      if(!mLeftFrames){
         eval();
         return;
      }

      if (layer->isNothingToUpdate())
         return;

      sp<GraphicBuffer> mybuffer(layer->getBypassBuffer());
      void* vaddr=0;
      uint32_t w = mybuffer->getWidth();
      uint32_t h = mybuffer->getHeight();
      Region region(Rect(0, 0, w, h));
      LOGE_IF(!mDumpWH, "Layer=%x w=%d h=%d", reinterpret_cast<unsigned int>(layer) ,w, h);
      mDumpWH=true;
      SimpleLock l (mybuffer, region, vaddr);
      if (l.res() != NO_ERROR) {
         LOGE("dumpFrame can not lock. Will not dump");
         return;
      }

      --mLeftFrames;
      // stop dumpframe.
      if(!mLeftFrames)
         property_set("debug.dumpframe", "0");

      char filename[256] = {0};
      ::memset(filename, 0, 256);
      sprintf(filename, "/data/dump.%x-%d-%d.raw",
              reinterpret_cast<unsigned int>(layer),
              mSession, mCurrFrame++);
      FILE* fp = fopen(filename, "w+");
      if (fp == NULL) {
         LOGE("dumpFrame cannot open file %s", filename);
         return;
      }
      buffer_handle_t handle = (mybuffer->getNativeBuffer())->handle;
      private_handle_t const* hnd = reinterpret_cast
         <private_handle_t const*>(handle);
      size_t size = hnd->size;
      fwrite(vaddr, hnd->size, 1, fp);
      fclose(fp);
   }
}
