/*
 * Copyright (C) 2007 The Android Open Source Project
 * Copyright (C) 2010, The Linux Foundation. All rights reserved.
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

#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <sys/types.h>

#include <utils/Errors.h>
#include <utils/Log.h>
#include <utils/StopWatch.h>

#include <ui/GraphicBuffer.h>
#include <ui/PixelFormat.h>
#include <ui/FramebufferNativeWindow.h>
#include <ui/Rect.h>
#include <ui/Region.h>

#include <hardware/copybit.h>

#include "LayerBuffer.h"
#include "SurfaceFlinger.h"
#include "DisplayHardware/DisplayHardware.h"

#include "gralloc_priv.h"

namespace android {

// ---------------------------------------------------------------------------

gralloc_module_t const* LayerBuffer::sGrallocModule = 0;

// ---------------------------------------------------------------------------

LayerBuffer::LayerBuffer(SurfaceFlinger* flinger, DisplayID display,
        const sp<Client>& client)
    : LayerBaseClient(flinger, display, client),
      mNeedsBlending(false), mInvalidate(false), mIsReconfiguring(false),
      mBlitEngine(0)
{
    char property[PROPERTY_VALUE_MAX];
    if(property_get("debug.composition.type", property, NULL) > 0 &&
                            (strncmp(property, "mdp", 3) == 0)) {
        mInvalidate = true;
    }
    setVisualParamType(-1);
}

LayerBuffer::~LayerBuffer()
{
    if (mBlitEngine) {
        copybit_close(mBlitEngine);
    }

    const DisplayHardware& hw(mFlinger->graphicPlane(0).displayHardware());
    hw.videoOverlayStarted(false);
}

void LayerBuffer::setVisualParam(int8_t paramType, float paramValue) {
    setVisualParamType(paramType);
    setVisualParamValue(paramValue);
}

void LayerBuffer::setVisualParamType(int8_t paramType) {
    mVisualParamType = paramType;
}

void LayerBuffer::setVisualParamValue(float paramValue) {
    mVisualParamValue = paramValue;
}

int8_t LayerBuffer::getVisualParamType() const {
    return mVisualParamType;
}

float LayerBuffer::getVisualParamValue() const {
    return mVisualParamValue;
}

void LayerBuffer::onFirstRef()
{
    LayerBaseClient::onFirstRef();
    mSurface = new SurfaceLayerBuffer(mFlinger, this);

    hw_module_t const* module = (hw_module_t const*)sGrallocModule;
    if (!module) {
        // NOTE: technically there is a race here, but it shouldn't
        // cause any problem since hw_get_module() always returns
        // the same value.
        if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0) {
            sGrallocModule = (gralloc_module_t const *)module;
        }
    }

    if (hw_get_module(COPYBIT_HARDWARE_MODULE_ID, &module) == 0) {
        copybit_open(module, &mBlitEngine);
    }
}

sp<LayerBaseClient::Surface> LayerBuffer::createSurface() const
{
    return mSurface;
}

status_t LayerBuffer::ditch()
{
    mSurface.clear();
    return NO_ERROR;
}

bool LayerBuffer::needsBlending() const {
    return mNeedsBlending;
}

void LayerBuffer::setNeedsBlending(bool blending) {
    mNeedsBlending = blending;
}

void LayerBuffer::onQueueBuf()
{
  sp<Source> source(getSource());
  if (source != 0)
    source->onQueueBuf();
}

void LayerBuffer::setDirtyQueueSignal()
{
  sp<Source> source(getSource());
  if (source != 0)
    source->setDirtyQueueSignal();
}

void LayerBuffer::postBuffer(ssize_t offset)
{
    sp<Source> source(getSource());
    if (source != 0) {
        if (mIsReconfiguring) {
            // We have received the first buffer after reconfiguration,
            // clear the reconfig. flag
            mIsReconfiguring = false;
        }
        source->postBuffer(offset);
    }
}

void LayerBuffer::wait(Mutex & m, Condition& c, reconfigBufState_t& s) {
  const nsecs_t TIMEOUT = ms2ns(20);
  unsigned int count = 0;
  enum { MAX_WAIT_COUNT = 50 };
  /* we set wakeup to 3 sec. That should never deadblock */
  while(s == RECONFIG_BUFFER_BLOCK) {
    (void) c.waitRelative(m, TIMEOUT);
    if(++count > MAX_WAIT_COUNT) {
      LOGE("LayerBuffer::wait reconfig too many wait count=%d", count);
      s = RECONFIG_BUFFER_GO;
    }
  }
  // Mark it as block so no other setBuffer can sneak in
  s = RECONFIG_BUFFER_BLOCK;

}

void LayerBuffer::resetReconfigStatus() const
{
    if (mIsReconfiguring) {
        mReconfigStatus = RECONFIG_BUFFER_GO;
        mIsReconfiguring = false;
    }
}

void LayerBuffer::reconfigureBuffers()
{
}

void LayerBuffer::unregisterBuffers()
{
    sp<Source> src(getSource());
    Mutex::Autolock _l(mReconfigMutex);
    if(src != 0) {
        // Set the reconfig flag
        mIsReconfiguring = true;
        // wait
        mReconfigStatus = RECONFIG_BUFFER_BLOCK;
        // signal the thread loop
        invalidate();
        wait(mReconfigMutex, mReconfigCond, mReconfigStatus);
        LOGD("Exit wait condition in reconfigureBuffers and return");
    }
    sp<Source> source(clearSource());
    if (source != 0)
        source->unregisterBuffers(mIsReconfiguring);
}

uint32_t LayerBuffer::doTransaction(uint32_t flags)
{
    sp<Source> source(getSource());
    if (source != 0)
        source->onTransaction(flags);
    uint32_t res = LayerBase::doTransaction(flags);
    // we always want filtering for these surfaces
    mNeedsFiltering = !(mFlags & DisplayHardware::SLOW_CONFIG);
    return res;
}

void LayerBuffer::unlockPageFlip(const Transform& planeTransform,
        Region& outDirtyRegion)
{
    // this code-path must be as tight as possible, it's called each time
    // the screen is composited.
    sp<Source> source(getSource());
    if (source != 0)
        source->onVisibilityResolved(planeTransform);
    if(mInvalidate)
        outDirtyRegion.orSelf(getTransformedBounds());
    LayerBase::unlockPageFlip(planeTransform, outDirtyRegion);
}

void LayerBuffer::validateVisibility(const Transform& globalTransform)
{
    sp<Source> source(getSource());
    if (source != 0)
        source->onvalidateVisibility(globalTransform);
    LayerBase::validateVisibility(globalTransform);
}

void LayerBuffer::drawForSreenShot() const
{
    const DisplayHardware& hw(graphicPlane(0).displayHardware());
    clearWithOpenGL( Region(hw.bounds()) );
}

void LayerBuffer::onDraw(const Region& clip) const
{
    sp<Source> source(getSource());
    if (LIKELY(source != 0)) {
        source->onDraw(clip);
    } else {
        clearWithOpenGL(clip);
    }
}

status_t LayerBuffer::drawWithOverlay(const Region& clip, 
                                  bool hdmiConnected, bool waitVsync) const
{
#if defined(TARGET_USES_OVERLAY)
    sp<Source> source(getSource());
    Mutex::Autolock _l(mReconfigMutex);
    if (LIKELY(source != 0)) {
        status_t ret;
        if (mIsReconfiguring) {
            ret = source->drawWithOverlayReconfigure(clip, hdmiConnected, waitVsync);
            // After we draw the reconfig Buffer, reset the reconfig state
            resetReconfigStatus();
        }
        else
            ret = source->drawWithOverlay(clip, hdmiConnected, waitVsync);
        return ret;
    } else if (mIsReconfiguring)
        return NO_ERROR;
#endif
    return INVALID_OPERATION;
}

void LayerBuffer::serverDestroy()
{
    sp<Source> source(clearSource());
    if (source != 0) {
        source->destroy();
    }
}

/**
 * This creates a "buffer" source for this surface
 */
status_t LayerBuffer::registerBuffers(const ISurface::BufferHeap& buffers)
{
    Mutex::Autolock _l(mLock);
    if (mSource != 0)
        return INVALID_OPERATION;

    sp<BufferSource> source = new BufferSource(*this, buffers);

    status_t result = source->getStatus();
    if (result == NO_ERROR) {
        mSource = source;
    }
    return result;
}

/**
 * This creates an "overlay" source for this surface
 */
sp<OverlayRef> LayerBuffer::createOverlay(uint32_t w, uint32_t h, int32_t f,
        int32_t orientation)
{
    sp<OverlayRef> result;
    Mutex::Autolock _l(mLock);
    if (mSource != 0)
        return result;

    sp<OverlaySource> source = new OverlaySource(*this, &result, w, h, f, orientation);
    if (result != 0) {
        mSource = source;
    }
    return result;
}

sp<LayerBuffer::Source> LayerBuffer::getSource() const {
    Mutex::Autolock _l(mLock);
    return mSource;
}

sp<LayerBuffer::Source> LayerBuffer::clearSource() {
    sp<Source> source;
    Mutex::Autolock _l(mLock);
    source = mSource;
    mSource.clear();
    return source;
}

// ============================================================================
// LayerBuffer::SurfaceLayerBuffer
// ============================================================================

LayerBuffer::SurfaceLayerBuffer::SurfaceLayerBuffer(
        const sp<SurfaceFlinger>& flinger, const sp<LayerBuffer>& owner)
    : LayerBaseClient::Surface(flinger, owner->getIdentity(), owner)
{
}

LayerBuffer::SurfaceLayerBuffer::~SurfaceLayerBuffer()
{
    unregisterBuffers();
}

status_t LayerBuffer::SurfaceLayerBuffer::registerBuffers(
        const ISurface::BufferHeap& buffers)
{
    sp<LayerBuffer> owner(getOwner());
    if (owner != 0)
        return owner->registerBuffers(buffers);
    return NO_INIT;
}

void LayerBuffer::SurfaceLayerBuffer::postBuffer(ssize_t offset)
{
    sp<LayerBuffer> owner(getOwner());
    if (owner != 0)
        owner->postBuffer(offset);
}

void LayerBuffer::SurfaceLayerBuffer::reconfigureBuffers()
{
    sp<LayerBuffer> owner(getOwner());
    if (owner != 0)
        owner->reconfigureBuffers();
}

void LayerBuffer::SurfaceLayerBuffer::unregisterBuffers()
{
    sp<LayerBuffer> owner(getOwner());
    if (owner != 0)
        owner->unregisterBuffers();
}

sp<OverlayRef> LayerBuffer::SurfaceLayerBuffer::createOverlay(
        uint32_t w, uint32_t h, int32_t format, int32_t orientation) {
    sp<OverlayRef> result;
    sp<LayerBuffer> owner(getOwner());
    if (owner != 0)
        result = owner->createOverlay(w, h, format, orientation);
    return result;
}

// ============================================================================
// LayerBuffer::Buffer
// ============================================================================

LayerBuffer::Buffer::Buffer(const ISurface::BufferHeap& buffers,
        ssize_t offset, size_t bufferSize)
    : mBufferHeap(buffers), mCurrOffset(offset)
{
    NativeBuffer& src(mNativeBuffer);
    src.crop.l = 0;
    src.crop.t = 0;
    src.crop.r = buffers.w;
    src.crop.b = buffers.h;
    src.hor_stride = buffers.hor_stride;
    src.ver_stride = buffers.ver_stride;

    src.img.w       = buffers.hor_stride ?: buffers.w;
    src.img.h       = buffers.ver_stride ?: buffers.h;
    src.img.format  = buffers.format;
    src.img.base    = (void*)(intptr_t(buffers.heap->base()) + offset);
    src.img.handle  = 0;
    src.img.horiz_padding =buffers.hor_stride-buffers.w;
    src.img.vert_padding =0;

    gralloc_module_t const * module = LayerBuffer::getGrallocModule();
    if (module && module->perform) {
        int err = module->perform(module,
                GRALLOC_MODULE_PERFORM_CREATE_HANDLE_FROM_BUFFER,
                buffers.heap->heapID(), bufferSize,
                offset, buffers.heap->base(),
                &src.img.handle, GRALLOC_USAGE_PRIVATE_1);

    }
 }

LayerBuffer::Buffer::~Buffer()
{
    NativeBuffer& src(mNativeBuffer);
    if (src.img.handle) {
        native_handle_delete(src.img.handle);
    }
}

// ============================================================================
// LayerBuffer::Source
// LayerBuffer::BufferSource
// LayerBuffer::OverlaySource
// ============================================================================

LayerBuffer::Source::Source(LayerBuffer& layer)
    : mLayer(layer)
{
}
LayerBuffer::Source::~Source() {
}
void LayerBuffer::Source::onDraw(const Region& clip) const {
}
status_t LayerBuffer::Source::drawWithOverlay(const Region& clip,
                        bool hdmiConnected, bool waitVsync) const {
    return INVALID_OPERATION;
}
status_t LayerBuffer::Source::drawWithOverlayReconfigure(const Region& clip,
                        bool hdmiConnected, bool waitVsync) const {
    return INVALID_OPERATION;
}
void LayerBuffer::Source::onTransaction(uint32_t flags) {
}
void LayerBuffer::Source::onVisibilityResolved(
        const Transform& planeTransform) {
}
void LayerBuffer::Source::postBuffer(ssize_t offset) {
}
void LayerBuffer::Source::unregisterBuffers(bool isReconfiguring) {
}

// ---------------------------------------------------------------------------

LayerBuffer::BufferSource::BufferSource(LayerBuffer& layer,
        const ISurface::BufferHeap& buffers)
    : Source(layer), mStatus(NO_ERROR), mBufferSize(0),
      mUseEGLImageDirectly(0), mNeedConversion(1),
      mPostBufState(PostBufPolicyBase::POSTBUF_GO),
      mPrevOffset(-1), mDirtyQueueBit(true)
{
    if (buffers.heap == NULL) {
        // this is allowed, but in this case, it is illegal to receive
        // postBuffer(). The surface just erases the framebuffer with
        // fully transparent pixels.
        mBufferHeap = buffers;
        mLayer.setNeedsBlending(false);
        return;
    }

    status_t err = (buffers.heap->heapID() >= 0) ? NO_ERROR : NO_INIT;
    if (err != NO_ERROR) {
        LOGE("LayerBuffer::BufferSource: invalid heap (%s)", strerror(err));
        mStatus = err;
        return;
    }


    const DisplayHardware& hw(mLayer.mFlinger->
                               graphicPlane(0).displayHardware());
    int flags = hw.getFlags();
    int hasBlitEngine = 0;
    char compositionUsed[4];
    if(flags & DisplayHardware::CPU_COMPOSITION) {
        snprintf(compositionUsed, 4, "cpu");
    } else if(flags & DisplayHardware::SLOW_CONFIG) {
        if (hw.getFlags() & DisplayHardware::C2D_COMPOSITION) {
            snprintf(compositionUsed, 4, "c2d");
        } else {
            snprintf(compositionUsed, 4, "mdp");
        }
    } else {
        snprintf(compositionUsed, 4, "gpu");
    }

    if(mLayer.mBlitEngine) {
        hasBlitEngine = 1;
    }

    gralloc_module_t const * module = LayerBuffer::getGrallocModule();
    if (module && module->perform) {
        int err = module->perform (module,GRALLOC_MODULE_PERFORM_DECIDE_PUSH_BUFFER_HANDLING,
                  buffers.format, buffers.w, buffers.h, compositionUsed, hasBlitEngine,
                  &mNeedConversion, &mUseEGLImageDirectly, &mBufferSize);
        if(err != NO_ERROR) {
            LOGE("module,GRALLOC_MODULE_PERFORM_DECIDE_PUSH_BUFFER_HANDLING returned an error");
        }
    }

    if(buffers.format >= 0x100) {
        mLayer.setNeedsBlending(false);
        mStatus = err;
    } else {
        PixelFormatInfo info;
        err = getPixelFormatInfo(buffers.format, &info);
        if (err != NO_ERROR) {
            LOGE("LayerBuffer::BufferSource: invalid format %d (%s)",
                    buffers.format, strerror(err));
            mStatus = err;
            return;
        }
        mLayer.setNeedsBlending((info.h_alpha - info.l_alpha) > 0);
        mBufferSize = info.getScanlineSize(buffers.hor_stride)*buffers.ver_stride;
    }

    if (buffers.hor_stride<0 || buffers.ver_stride<0) {
        LOGE("LayerBuffer::BufferSource: invalid parameters "
             "(w=%d, h=%d, xs=%d, ys=%d)",
             buffers.w, buffers.h, buffers.hor_stride, buffers.ver_stride);
        mStatus = BAD_VALUE;
        return;
    }

    mBufferHeap = buffers;
    mLayer.forceVisibilityTransaction();
}

LayerBuffer::BufferSource::~BufferSource()
{
  /* release any IBinder threads */
  mPostBufState = PostBufPolicyBase::POSTBUF_GO;
  mPostBufCond.signal(); // Let waiting thread to be done.
    class MessageDestroyTexture : public MessageBase {
        SurfaceFlinger* flinger;
        GLuint name;
    public:
        MessageDestroyTexture(
                SurfaceFlinger* flinger, GLuint name)
            : flinger(flinger), name(name) { }
        virtual bool handler() {
            glDeleteTextures(1, &name);
            return true;
        }
    };

    if (mTexture.name != -1U) {
        // GL textures can only be destroyed from the GL thread
        getFlinger()->mEventQueue.postMessage(
                new MessageDestroyTexture(getFlinger(), mTexture.name) );
    }
    if (mTexture.image != EGL_NO_IMAGE_KHR) {
        EGLDisplay dpy(getFlinger()->graphicPlane(0).getEGLDisplay());
        eglDestroyImageKHR(dpy, mTexture.image);
    }
}

void LayerBuffer::BufferSource::postBuffer(ssize_t offset)
{
    ISurface::BufferHeap buffers;
    { // scope for the lock
        Mutex::Autolock _l(mBufferSourceLock);
        buffers = mBufferHeap;
        if (buffers.heap != 0) {
            const size_t memorySize = buffers.heap->getSize();
            if ((size_t(offset) + mBufferSize) > memorySize) {
                LOGE("LayerBuffer::BufferSource::postBuffer() "
                     "invalid buffer (offset=%d, size=%d, heap-size=%d",
                     int(offset), int(mBufferSize), int(memorySize));
                return;
            }
        }
    }

    sp<Buffer> buffer;
    if (buffers.heap != 0) {
        buffer = new LayerBuffer::Buffer(buffers, offset, mBufferSize);
        if (buffer->getStatus() != NO_ERROR)
            buffer.clear();
        setBuffer(buffer);
        mLayer.invalidate();
    }
}

void LayerBuffer::BufferSource::unregisterBuffers(bool isReconfiguring)
{
    Mutex::Autolock _l(mBufferSourceLock);
    mBufferHeap.heap.clear();
    mBuffer.clear();
    if (!isReconfiguring) {
        // Since we are not in the middle of reconfiguration,
        // signal the server
        mLayer.invalidate();
    }
}

sp<LayerBuffer::Buffer> LayerBuffer::BufferSource::getBuffer() const
{
    Mutex::Autolock _l(mBufferSourceLock);
    return mBuffer;
}

void LayerBuffer::BufferSource::onQueueBuf()
{
  // callback is being called when we are done queuing and it is safe for
  // postBuffer to continue
  PostBufferSingleton::instance()->onQueueBuf(mPostBufLock, mPostBufCond,
                                              mDirtyQueueBit, mPostBufState);
}


void LayerBuffer::BufferSource::setBuffer(const sp<LayerBuffer::Buffer>& buffer)
{
  PostBufferSingleton::instance()->wait(mPostBufLock, mPostBufCond, mPostBufState);
  Mutex::Autolock _l(mBufferSourceLock);
  mBuffer = buffer;
}

status_t LayerBuffer::BufferSource::drawWithOverlayReconfigure(
                  const Region& clip, bool hdmiConnected, bool waitVsync) const
{
#if defined(TARGET_USES_OVERLAY)
    LOGD("LayerBuffer::BufferSource::drawWithOverlayReconfigure E");
    sp<Buffer> ourBuffer(getBuffer());
    if (UNLIKELY(ourBuffer == 0))  {
        // nothing to do, we don't have a buffer
        // happens in suspend resume
        mLayer.clearWithOpenGL(clip);
        // return no error as no point trying to draw this null buffer on FB
        return NO_ERROR;
    }
    bool needsClearFB = false;
    Buffer::Offset& currOffset = ourBuffer->getCurrOffset();
    // That will ensure no signaling would happen
    mDirtyQueueBit = true;
    // Dirty queue bit is false so no signaling would happen, only when the
    // origin of the offset comes from the event loop and it is the same offset
    // That is because in seek operations, same offset can come from postBuffer
    // so the decoder is sending multiple postBuf with same offset and we want
    // to signal in that case. (otherwise we will have a 2-3 of ui freeze)
    if(currOffset.mOrigin == Buffer::Offset::EVENT_LOOP &&
        currOffset == mPrevOffset) {
      mDirtyQueueBit = false;
    }
    mPrevOffset = currOffset;
    currOffset.mOrigin = Buffer::Offset::EVENT_LOOP;

    NativeBuffer src(ourBuffer->getBuffer());
    const DisplayHardware& hw(mLayer.mFlinger->
                               graphicPlane(0).displayHardware());
    hw.videoOverlayStarted(true);
    overlay::Overlay* ov = hw.getOverlayObject();
    Transform finalTransform(Transform(mLayer.getOrientation()) *
                        Transform(mBufferHeap.transform));

    int w = src.hor_stride;
    int h = src.ver_stride;
    int format = src.img.format;

    overlay_rect crop, dst;
    crop.x = src.crop.l;
    crop.y = src.crop.t;
    crop.w = src.crop.r - src.crop.l;
    crop.h = src.crop.b - src.crop.t;

    const Rect bounds(mLayer.mTransformedBounds);
    dst.x = bounds.left;
    dst.y = bounds.top;
    dst.w = bounds.width();
    dst.h = bounds.height();

    private_handle_t *hnd = (private_handle_t *)src.img.handle;
    overlay_play_info playInfo;
    playInfo.fd = hnd->fd;
    playInfo.offset = hnd->offset;
    bool ret =  ov->setReconfigurationInfo(RECONFIG_ON,
                            w, h, format, crop, dst, playInfo,
                            finalTransform.getOrientation());
    if (!ret) {
        // if we fail to setup for reconfig
        // release the lock
        mLayer.onQueueBuf();
        return INVALID_OPERATION;
    }
    if (ov->queueBuffer(src.img.handle))
        needsClearFB = true;
    else
        return INVALID_OPERATION;

    // Need to inform video layer here after queuing
    // It is safe to call onQueueBuf only when waitVsync==true since in this case
    // it is safe to let postBuffer thread (video renderer) to move ahead
    if(waitVsync)  {
        mLayer.onQueueBuf();
    }
    if (!waitVsync || needsClearFB) {
        mLayer.clearWithOpenGL(clip);
    }
    LOGD("LayerBuffer::BufferSource::drawWithOverlayReconfigure X");
    return NO_ERROR;
#endif
    return INVALID_OPERATION;
}

status_t LayerBuffer::BufferSource::drawWithOverlay(const Region& clip,
                              bool hdmiConnected, bool waitVsync) const
{
#if defined(TARGET_USES_OVERLAY)
    sp<Buffer> ourBuffer(getBuffer());
    if (UNLIKELY(ourBuffer == 0))  {
        // nothing to do, we don't have a buffer
        // happens in suspend resume
        mLayer.clearWithOpenGL(clip);
        // return no error as no point trying to draw this null buffer on FB
        return NO_ERROR;
    }

    Buffer::Offset& currOffset = ourBuffer->getCurrOffset();
    // That will ensure no signaling would happen
    mDirtyQueueBit = true;
    // Dirty queue bit is false so no signaling would happen, only when the
    // origin of the offset comes from the event loop and it is the same offset
    // That is because in seek operations, same offset can come from postBuffer
    // so the decoder is sending multiple postBuf with same offset and we want
    // to signal in that case. (otherwise we will have a 2-3 of ui freeze)
    if(currOffset.mOrigin == Buffer::Offset::EVENT_LOOP &&
        currOffset == mPrevOffset) {
      mDirtyQueueBit = false;
    }
    mPrevOffset = currOffset;
    currOffset.mOrigin = Buffer::Offset::EVENT_LOOP;

    NativeBuffer src(ourBuffer->getBuffer());
    const DisplayHardware& hw(mLayer.mFlinger->
                               graphicPlane(0).displayHardware());
    hw.videoOverlayStarted(true);
    overlay::Overlay* ov = hw.getOverlayObject();
    ov->resetReconfigStatus();
    int s3dFormat = mLayer.getStereoscopic3DFormat();
    Transform finalTransform(Transform(mLayer.getOrientation()) *
                        Transform(mBufferHeap.transform));

    int device_orientation = finalTransform.getOrientation();
    bool ret = false;

    if (!ov->setSource(src.hor_stride, src.ver_stride,
                       src.img.format|s3dFormat, device_orientation,
                       hdmiConnected, waitVsync)) {
        LOGE("%s: setSource(%d %d %d %d) failed", __FUNCTION__, src.hor_stride,
                                   src.ver_stride, src.img.format|s3dFormat,
                                   device_orientation);
        return INVALID_OPERATION;
    }
    if(!ov->setParameter(OVERLAY_TRANSFORM, device_orientation)) {
        LOGE("%s: setparam failed for orientation %d", __FUNCTION__, device_orientation);
        return INVALID_OPERATION;
    }
    if (!ov->setCrop(0, 0, src.crop.r, src.crop.b)) {
        LOGE("DWO: setCrop(0, 0, %d, %d) failed", src.crop.r, src.crop.b);
        return INVALID_OPERATION;
    }
    const Rect bounds(mLayer.mTransformedBounds);
    int x = bounds.left;
    int y = bounds.top;
    int w = bounds.width();
    int h = bounds.height();
    int ovpos_x, ovpos_y;
    uint32_t ovpos_w, ovpos_h;
    ret = ov->getPosition(ovpos_x, ovpos_y, ovpos_w, ovpos_h);
    if (ret) {
        if ((ovpos_x != x) || (ovpos_y != y) || (ovpos_w != w) || (ovpos_h != h)) {
            ret = ov->setPosition(x, y, w, h);
        }
    }
    else
        ret = ov->setPosition(x, y, w, h);
    if (!ret) {
      // Need to release postBuffer loc in case of bad setPos
      // That happens in youtube app
      // FIXME TODO - onQueueBuf should be probably call when we have all sort
      // of errors in DWO function.
      // That one is specifically here to _faster_ release a lock when dealing
      // w/ youtube apk instead of waiting for setBuffer/wait timeout.
      LOGE("%s: setPosition(%d, %d, %d, %d) failed", __FUNCTION__, x, y, w, h);
      mLayer.onQueueBuf();
      return INVALID_OPERATION;
    }

    if (mLayer.getVisualParamType() != -1) {
        ov->setVisualParam(mLayer.getVisualParamType(), mLayer.getVisualParamValue());
        mLayer.setVisualParamType(-1);
    }

    ret = ov->queueBuffer(src.img.handle);
    if (!ret)
        return INVALID_OPERATION;

    // Need to inform video layer here after queuing
    // It is safe to call onQueueBuf only when waitVsync==true since in this case
    // it is safe to let postBuffer thread (video renderer) to move ahead
    if(waitVsync)
        mLayer.onQueueBuf();
    else
        mLayer.clearWithOpenGL(clip);
    return NO_ERROR;
#endif
    return INVALID_OPERATION;
}

void LayerBuffer::BufferSource::onDraw(const Region& clip) const
{
    sp<Buffer> ourBuffer(getBuffer());
    if (UNLIKELY(ourBuffer == 0))  {
        // nothing to do, we don't have a buffer
        mLayer.clearWithOpenGL(clip);
        return;
    }

    status_t err = NO_ERROR;
    NativeBuffer& src=ourBuffer->getBuffer();
    if(src.img.format == HAL_PIXEL_FORMAT_YV12) {
        /* converting yuv buffer using copybit stretch */
        copybit_device_t* copybit = mLayer.mBlitEngine;
        if (copybit ) {
            region_iterator clip(Region(Rect(src.crop.r, src.crop.b)));
            copybit_image_t cimage(src.img);
            copybit->set_parameter(copybit, COPYBIT_TRANSFORM, 0);
            copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA, 0xFF);
            copybit->set_parameter(copybit, COPYBIT_DITHER, COPYBIT_ENABLE);
            int err = copybit->stretch(copybit, &cimage, &src.img, &src.crop, &src.crop, &clip);
            if(err <0){
                LOGE(" copybit stretch for color conversion failed ");
            }
            else {
                src.img.format =HAL_PIXEL_FORMAT_YCrCb_420_SP;
            }
        }
    }

    const Rect transformedBounds(mLayer.getTransformedBounds());

#if defined(EGL_ANDROID_image_native_buffer)
    if (GLExtensions::getInstance().haveDirectTexture()) {
        err = INVALID_OPERATION;
        if (mNeedConversion) {
            copybit_device_t* copybit = mLayer.mBlitEngine;
            if (copybit && err != NO_ERROR) {
                // create our EGLImageKHR the first time
                err = initTempBuffer();
                if (err == NO_ERROR) {
                    // NOTE: Assume the buffer is allocated with the proper USAGE flags
                    const NativeBuffer& dst(mTempBuffer);
                    region_iterator clip(Region(Rect(dst.crop.r, dst.crop.b)));
                    copybit->set_parameter(copybit, COPYBIT_TRANSFORM, 0);
                    copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA, 0xFF);
                    copybit->set_parameter(copybit, COPYBIT_DITHER, COPYBIT_ENABLE);
                    err = copybit->stretch(copybit, &dst.img, &src.img,
                            &dst.crop, &src.crop, &clip);
                    if (err != NO_ERROR) {
                        clearTempBufferImage();
                    }
                }
            }
        } else if (mUseEGLImageDirectly) {
               clearTempBufferImage();
               sp<GraphicBuffer> mTempGraphicBuffer = new GraphicBuffer(
                           src.crop.r, src.crop.b, src.img.format,
                           GraphicBuffer::USAGE_HW_TEXTURE, src.img.w,
                           src.img.handle, false);
               mTempGraphicBuffer->setVerticalStride(src.img.h);
               EGLDisplay dpy(getFlinger()->graphicPlane(0).getEGLDisplay());
               EGLContext ctx(getFlinger()->graphicPlane(0).getEGLContext());
               err = mTextureManager.initEglImage(&mTexture, dpy, mTempGraphicBuffer, ctx);
        } else {
               err = INVALID_OPERATION;
        }
    }
#endif
    else {
        err = INVALID_OPERATION;
    }

    if (err != NO_ERROR) {
        // slower fallback
        GGLSurface t;
        t.version = sizeof(GGLSurface);
        t.width  = src.crop.r;
        t.height = src.crop.b;
        t.stride = src.img.w;
        t.vstride= src.img.h;
        t.format = src.img.format;
        t.data = (GGLubyte*)src.img.base;
        const Region dirty(Rect(t.width, t.height));
        mTextureManager.loadTexture(&mTexture, dirty, t);
    }

    mLayer.setBufferTransform(mBufferHeap.transform);
    mLayer.drawWithOpenGL(clip, mTexture);
}

status_t LayerBuffer::BufferSource::initTempBuffer() const
{
    // figure out the size we need now
    const ISurface::BufferHeap& buffers(mBufferHeap);
    uint32_t w = mLayer.mTransformedBounds.width();
    uint32_t h = mLayer.mTransformedBounds.height();
    if (mLayer.getOrientation() & (Transform::ROT_90 | Transform::ROT_270)) {
        int t = w; w = h; h = t;
    }

    // we're in the copybit case, so make sure we can handle this blit
    // we don't have to keep the aspect ratio here
    copybit_device_t* copybit = mLayer.mBlitEngine;
    const int down = copybit->get(copybit, COPYBIT_MINIFICATION_LIMIT);
    const int up = copybit->get(copybit, COPYBIT_MAGNIFICATION_LIMIT);
    if (buffers.w > w*down)     w = buffers.w / down;
    else if (w > buffers.w*up)  w = buffers.w*up;
    if (buffers.h > h*down)     h = buffers.h / down;
    else if (h > buffers.h*up)  h = buffers.h*up;

    if (mTexture.image != EGL_NO_IMAGE_KHR) {
        // we have an EGLImage, make sure the needed size didn't change
        if (w!=mTexture.width || h!= mTexture.height) {
            // delete the EGLImage and texture
            clearTempBufferImage();
        } else {
            // we're good, we have an EGLImageKHR and it's (still) the
            // right size
            return NO_ERROR;
        }
    }

    // figure out if we need linear filtering
    if (buffers.w * h == buffers.h * w) {
        // same pixel area, don't use filtering
        mLayer.mNeedsFiltering = false;
    }

    // Allocate a temporary buffer and create the corresponding EGLImageKHR
    // once the EGLImage has been created we don't need the
    // graphic buffer reference anymore.
    sp<GraphicBuffer> buffer = new GraphicBuffer(
            w, h, HAL_PIXEL_FORMAT_RGB_565,
            GraphicBuffer::USAGE_HW_TEXTURE |
            GraphicBuffer::USAGE_HW_2D | GRALLOC_USAGE_PRIVATE_1);

    status_t err = buffer->initCheck();
    if (err == NO_ERROR) {
        NativeBuffer& dst(mTempBuffer);
        dst.img.w = buffer->getStride();
        dst.img.h = h;
        dst.img.format = buffer->getPixelFormat();
        dst.img.handle = (native_handle_t *)buffer->handle;
        dst.img.base = 0;
        dst.crop.l = 0;
        dst.crop.t = 0;
        dst.crop.r = w;
        dst.crop.b = h;

        EGLDisplay dpy(getFlinger()->graphicPlane(0).getEGLDisplay());
        err = mTextureManager.initEglImage(&mTexture, dpy, buffer);
    }

    return err;
}

void LayerBuffer::BufferSource::clearTempBufferImage() const
{
    // delete the image
    EGLDisplay dpy(getFlinger()->graphicPlane(0).getEGLDisplay());
    if (mTexture.image != EGL_NO_IMAGE_KHR)
        eglDestroyImageKHR(dpy, mTexture.image);

    // and the associated texture (recreate a name)
    glDeleteTextures(1, &mTexture.name);
    Texture defaultTexture;
    mTexture = defaultTexture;
}

// ---------------------------------------------------------------------------

LayerBuffer::OverlaySource::OverlaySource(LayerBuffer& layer,
        sp<OverlayRef>* overlayRef,
        uint32_t w, uint32_t h, int32_t format, int32_t orientation)
    : Source(layer), mVisibilityChanged(false),
    mOverlay(0), mOverlayHandle(0), mOverlayDevice(0), mHDMIEnabled(0)
{
    overlay_control_device_t* overlay_dev = getFlinger()->getOverlayEngine();
    if (overlay_dev == NULL) {
        // overlays not supported
        return;
    }

    mOverlayDevice = overlay_dev;
    const DisplayHardware& hw(mLayer.mFlinger->
                               graphicPlane(0).displayHardware());
    mLayer.mFlinger->closeOverlay();
    hw.videoOverlayStarted(true);
    overlay_t* overlay = overlay_dev->createOverlay(overlay_dev, w, h, format);
    if (overlay == NULL) {
        // couldn't create the overlay (no memory? no more overlays?)
        return;
    }

    // Separate the actual orientation from the flip information
    if(orientation & HAL_TRANSFORM_FLIP_SRC_H) {
        mFlip = HAL_TRANSFORM_FLIP_SRC_H;
        mOrientation = orientation & HAL_TRANSFORM_ROT_MASK;
    } else if (orientation & HAL_TRANSFORM_FLIP_SRC_V) {
        mFlip = HAL_TRANSFORM_FLIP_SRC_V;
        mOrientation = orientation & HAL_TRANSFORM_ROT_MASK;
    } else {
        mOrientation = orientation;
        mFlip = 0;
    }

    // enable dithering...
    overlay_dev->setParameter(overlay_dev, overlay,
            OVERLAY_DITHER, OVERLAY_ENABLE);

    mOverlay = overlay;
    mWidth = overlay->w;
    mHeight = overlay->h;
    mFormat = overlay->format;
    mWidthStride = overlay->w_stride;
    mHeightStride = overlay->h_stride;
    mInitialized = false;

    mOverlayHandle = overlay->getHandleRef(overlay);

    sp<OverlayChannel> channel = new OverlayChannel( &layer );

    *overlayRef = new OverlayRef(mOverlayHandle, channel,
            mWidth, mHeight, mFormat, mWidthStride, mHeightStride);
    getFlinger()->signalEvent();
}

LayerBuffer::OverlaySource::~OverlaySource()
{
    if (mOverlay && mOverlayDevice) {
        overlay_control_device_t* overlay_dev = mOverlayDevice;
        overlay_dev->destroyOverlay(overlay_dev, mOverlay);
        const DisplayHardware& hw(mLayer.mFlinger->
                                   graphicPlane(0).displayHardware());
        hw.videoOverlayStarted(false);
    }
}

void LayerBuffer::OverlaySource::onDraw(const Region& clip) const
{
    // this would be where the color-key would be set, should we need it.
    GLclampf red = 0;
    GLclampf green = 0;
    GLclampf blue = 0;
    mLayer.clearWithOpenGL(clip, red, green, blue, 0);
}

void LayerBuffer::OverlaySource::onTransaction(uint32_t flags)
{
    const Layer::State& front(mLayer.drawingState());
    const Layer::State& temp(mLayer.currentState());
    if (temp.sequence != front.sequence) {
        mVisibilityChanged = true;
    }
}

void LayerBuffer::OverlaySource::onvalidateVisibility(const Transform&)
{
    mVisibilityChanged = true;
}

void LayerBuffer::OverlaySource::onVisibilityResolved(
        const Transform& planeTransform)
{
    // this code-path must be as tight as possible, it's called each time
    // the screen is composited.
    if (UNLIKELY(mOverlay != 0)) {
        // we need a lock here to protect "destroy"
        Mutex::Autolock _l(mOverlaySourceLock);
        if (mVisibilityChanged || !mInitialized) {
            mVisibilityChanged = false;
            mInitialized = true;
            const Rect bounds(mLayer.getTransformedBounds());
            int x = bounds.left;
            int y = bounds.top;
            int w = bounds.width();
            int h = bounds.height();
            int currX = x, currY= y;
            uint32_t currW = w, currH = h;
            if (mOverlay) {
                overlay_control_device_t* overlay_dev = mOverlayDevice;
                // we need to combine the layer orientation and the
                // user-requested orientation.
                Transform finalTransform(Transform(mLayer.getOrientation()) *
                        Transform(mOrientation));
                overlay_dev->setParameter(overlay_dev, mOverlay,
                        OVERLAY_TRANSFORM, finalTransform.getOrientation() | mFlip);
                overlay_dev->getPosition(overlay_dev, mOverlay, &currX, &currY, &currW, &currH);
                if((x != currX) || (y != currY) || (w != currW) || (h != currH))
                    overlay_dev->setPosition(overlay_dev, mOverlay, x,y,w,h);
                if (mLayer.getVisualParamType() != -1) {
                    overlay_dev->setVisualParam(overlay_dev, mOverlay, mLayer.getVisualParamType(), mLayer.getVisualParamValue());
                    mLayer.setVisualParamType(-1);
                }
                overlay_dev->commit(overlay_dev, mOverlay);
            }
        }
        if(mOverlayDevice &&
                    (mHDMIEnabled != (mLayer.mFlinger->mHDMIOutput))) {
            overlay_control_device_t* ov_dev = mOverlayDevice;
            if(!ov_dev->setParameter(ov_dev, mOverlay, OVERLAY_HDMI_ENABLE,
                                                 mLayer.mFlinger->mHDMIOutput))
                ov_dev->commit(ov_dev, mOverlay);
            mHDMIEnabled =  mLayer.mFlinger->mHDMIOutput;
        }
    }
}

void LayerBuffer::OverlaySource::destroy()
{
    // we need a lock here to protect "onVisibilityResolved"
    Mutex::Autolock _l(mOverlaySourceLock);
    if (mOverlay && mOverlayDevice) {
        overlay_control_device_t* overlay_dev = mOverlayDevice;
        overlay_dev->destroyOverlay(overlay_dev, mOverlay);
        mOverlay = 0;
        const DisplayHardware& hw(mLayer.mFlinger->
                                   graphicPlane(0).displayHardware());
        hw.videoOverlayStarted(false);
    }
}

// ---------------------------------------------------------------------------
}; // namespace android
