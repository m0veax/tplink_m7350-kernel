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

#ifndef DUMP_FRAME_H
#define DUMP_FRAME_H

#include <stdint.h>
#include <sys/types.h>

#include <cutils/properties.h>

#include <ui/GraphicBuffer.h>
#include <ui/Region.h>

namespace android {
   class Layer;
   class Region;
   class DumpFrame{
   private:
      /* helper class for buffer scoped lock/unlock */
      class SimpleLock{
      public:
         SimpleLock(sp<GraphicBuffer> b,
                    const Region& clip,
                    void* &vaddr) : buf(b), result(NO_ERROR) {
            result = buf->lock(GRALLOC_USAGE_SW_READ_OFTEN |
                               GRALLOC_USAGE_SW_WRITE_OFTEN, clip.bounds(),
                               &vaddr);
         }
         ~SimpleLock() { if (result == NO_ERROR) buf->unlock(); }
         bool res() const { return result; }
      private:
         sp<GraphicBuffer> buf;
         status_t result;
      };

      /* reeval property to re-dump frames */
      void eval() {
         char property[PROPERTY_VALUE_MAX] = {0};
         if (!property_get("debug.dumpframe", property, "0"))
            return;
         mLeftFrames = atoi(property);
         if(!mLeftFrames) return;
         ++mSession;
         mCurrFrame = 0;
      }

   public:
      DumpFrame() : mCurrFrame(0), mLeftFrames(0), mSession(0), mDumpWH(false) {
         eval();
      }

      /* actual dump */
      void dump(const Layer* const layer, const Region& clip);
   private:
      // current frame used for filename
      unsigned int mCurrFrame;
      // counter for how many frames left
      unsigned int mLeftFrames;
      // for printing the filename <sessid-framenum>
      // session starts from 1
      unsigned int mSession;
      // dump w/h just once
      bool mDumpWH;
   };
}

#endif // DUMP_FRAME_H
