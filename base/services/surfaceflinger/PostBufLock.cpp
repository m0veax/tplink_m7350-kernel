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

#include "PostBufLock.h"
#include <surfaceflinger/ISurfaceComposer.h>
#include "LayerBuffer.h"

namespace android {
   class LayerBase;
   class LayerBuffer;

   PostBufferSingleton* PostBufferSingleton::mInstance=0;

   void PostBufLockPolicy::wait(Mutex & m, Condition& c, postBufState_t& s)
   {
      const nsecs_t TIMEOUT = ms2ns(20);
      Mutex::Autolock _l(m);
      unsigned int count = 0;
      enum { MAX_WAIT_COUNT = 50 };
      /* That is the heart of postBuffer block call.
       * we set wakeup to 3 sec. That should never deadblock
       * since we always have *any* UI update that can release it */
      while(s == PostBufPolicyBase::POSTBUF_BLOCK) {
         (void) c.waitRelative(m, TIMEOUT);
         if(++count > MAX_WAIT_COUNT){
            LOGE("BufferSource::setBuffer too many wait count=%d", count);
            s = PostBufPolicyBase::POSTBUF_GO;
         }
      }
      // Mark it as block so no other setBuffer can sneak in
      s = PostBufPolicyBase::POSTBUF_BLOCK;
   }

   bool isPushBuffer(LayerBase* l) {
      return (l->getLayerInitFlags() & android::ISurfaceComposer::ePushBuffers);
   }

   void PostBufferSingleton::addPushBufferLayers(const Vector< sp<LayerBase> >& l) {
      for(size_t i=0 ; i<l.size() ; ++i) {
         if(isPushBuffer(l[i].get())){
            mPushBufList.add(l[i]);
            // safe since it is push buffer
            static_cast<LayerBuffer*>(l[i].get())->setDirtyQueueSignal();
         }
      }
   }

   void PostBufferSingleton::onQueueBuf() {
      for(size_t i=0 ; i<mPushBufList.size() ; ++i) {
         static_cast<LayerBuffer*>(mPushBufList[i].get())->onQueueBuf();
      }
   }
}
