/*
 * Copyright (C) 2007 The Android Open Source Project
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

#ifndef ANDROID_LAYER_BUFFER_H
#define ANDROID_LAYER_BUFFER_H

#include <stdint.h>
#include <sys/types.h>

#include "LayerBase.h"
#include "TextureManager.h"

#include "PostBufLock.h"

struct copybit_device_t;

namespace android {

// ---------------------------------------------------------------------------

class Buffer;
class Region;
class OverlayRef;

// ---------------------------------------------------------------------------

/* Interface being called after Queuing buffer to overlay in case of
 * waitVsync=true, or after swap being called in case of waitVsync=false */
class IOnQueueBuf{
public:
  virtual void onQueueBuf() {}
  virtual void setDirtyQueueSignal() {}
protected:
  virtual ~IOnQueueBuf(){}
};

class LayerBuffer : public LayerBaseClient, public IOnQueueBuf
{
    class Source : public LightRefBase<Source>, public IOnQueueBuf {
    public:
        Source(LayerBuffer& layer);
        virtual ~Source();
        virtual void onDraw(const Region& clip) const;
        virtual status_t drawWithOverlay(const Region& clip,
                bool hdmiConnected, bool waitVsync = true) const;
        virtual status_t drawWithOverlayReconfigure(const Region& clip,
                bool hdmiConnected, bool waitVsync = true) const;
        virtual void onTransaction(uint32_t flags);
        virtual void onVisibilityResolved(const Transform& planeTransform);
        virtual void onvalidateVisibility(const Transform& globalTransform) { }
        virtual void postBuffer(ssize_t offset);
        virtual void unregisterBuffers(bool isReconfiguring = false);
        virtual void destroy() { }
        SurfaceFlinger* getFlinger() const { return mLayer.mFlinger.get(); }
    protected:
        LayerBuffer& mLayer;
    };

public:
            LayerBuffer(SurfaceFlinger* flinger, DisplayID display,
                    const sp<Client>& client);
        virtual ~LayerBuffer();

    virtual void onFirstRef();
    virtual bool needsBlending() const;
    virtual const char* getTypeId() const { return "LayerBuffer"; }

    virtual sp<LayerBaseClient::Surface> createSurface() const;
    virtual status_t ditch();
    virtual void onDraw(const Region& clip) const;
    virtual void drawForSreenShot() const;
    virtual status_t drawWithOverlay(const Region& clip,
              bool hdmiConnected, bool waitVsync = true) const;
    virtual uint32_t doTransaction(uint32_t flags);
    virtual void unlockPageFlip(const Transform& planeTransform, Region& outDirtyRegion);
    virtual void validateVisibility(const Transform& globalTransform);

    status_t registerBuffers(const ISurface::BufferHeap& buffers);
    void postBuffer(ssize_t offset);
    void reconfigureBuffers();
    void unregisterBuffers();
    bool isReconfiguring() const { return mIsReconfiguring; }
    sp<OverlayRef> createOverlay(uint32_t w, uint32_t h, int32_t format,
            int32_t orientation);
    
    sp<Source> getSource() const;
    sp<Source> clearSource();
    void setNeedsBlending(bool blending);
    Rect getTransformedBounds() const {
        return mTransformedBounds;
    }

    void serverDestroy();

    /* pass through to buffer source. See BufferSource for details */
    virtual void onQueueBuf();

    /* pass through to buffer source. See BufferSource for details */
    virtual void setDirtyQueueSignal();

    void setVisualParam(int8_t paramType, float paramValue);
    void setVisualParamType(int8_t paramType);
    void setVisualParamValue(float paramValue);
    int8_t getVisualParamType() const;
    float getVisualParamValue() const;
private:
    int8_t mVisualParamType;
    float mVisualParamValue;
    struct NativeBuffer {
        copybit_image_t   img;
        copybit_rect_t    crop;
        int hor_stride;
        int ver_stride;
    };

    typedef enum { RECONFIG_BUFFER_GO, RECONFIG_BUFFER_BLOCK } reconfigBufState_t;
    void wait(Mutex & m, Condition& c, reconfigBufState_t& s);
    void resetReconfigStatus() const;

    static gralloc_module_t const* sGrallocModule;
    static gralloc_module_t const* getGrallocModule() {
        return sGrallocModule;
    }

    class Buffer : public LightRefBase<Buffer> {
    public:
        struct Offset{
          Offset(ssize_t offset) : mOffset(offset), mOrigin(POST_BUFFER) {}
          ssize_t mOffset;
          operator ssize_t() const { return mOffset; }
          enum { POST_BUFFER, EVENT_LOOP } mOrigin;
        };

        Buffer(const ISurface::BufferHeap& buffers,
                ssize_t offset, size_t bufferSize);
        inline status_t getStatus() const {
            return mBufferHeap.heap!=0 ? NO_ERROR : NO_INIT;
        }
        inline NativeBuffer& getBuffer() {
            return mNativeBuffer;
        }
        /* return the current offset */
        Offset& getCurrOffset() { return mCurrOffset; }
        const Offset& getCurrOffset() const { return mCurrOffset; }

    protected:
        friend class LightRefBase<Buffer>;
        Buffer& operator = (const Buffer& rhs);
        Buffer(const Buffer& rhs);
        ~Buffer();
    private:
        ISurface::BufferHeap    mBufferHeap;
        NativeBuffer            mNativeBuffer;
        Offset                  mCurrOffset;
    };

    class BufferSource : public Source {
    public:
        BufferSource(LayerBuffer& layer, const ISurface::BufferHeap& buffers);
        virtual ~BufferSource();

        status_t getStatus() const { return mStatus; }
        sp<Buffer> getBuffer() const;
        void setBuffer(const sp<Buffer>& buffer);

        virtual void onDraw(const Region& clip) const;
        virtual status_t drawWithOverlay(const Region& clip,
                bool hdmiConnected, bool waitVsync = true) const;
        virtual status_t drawWithOverlayReconfigure(const Region& clip,
                bool hdmiConnected, bool waitVsync = true) const;
        virtual void postBuffer(ssize_t offset);
        virtual void unregisterBuffers(bool isReconfiguring = false);
        virtual void destroy() { }

       /* OnBufferQ is called from drawWithOverlay, or after egl swap,
        * when buffs are queued to Overlay.
        * Usecases: When waitVsync==true it means WAIT==true in
        * liboverlay and it is safe to invoke OnBufferQ immediately
        * after return from QueueBuf.
        * When waitVsync==false it means WAIT==false and we are going to have
        * that call invoked only after swap. */
        virtual void onQueueBuf();

       /*
        * Set dirty queue signal bit so when onQueueBuf is being called, it will
        * transit the machine state to GO (non blocking postBuf) */
        virtual void setDirtyQueueSignal() { mDirtyQueueBit = true; }
    private:
        status_t initTempBuffer() const;
        void clearTempBufferImage() const;
        mutable Mutex                   mBufferSourceLock;
        sp<Buffer>                      mBuffer;
        status_t                        mStatus;
        ISurface::BufferHeap            mBufferHeap;
        size_t                          mBufferSize;
        mutable Texture                 mTexture;
        mutable NativeBuffer            mTempBuffer;
        mutable TextureManager          mTextureManager;
        mutable int                     mUseEGLImageDirectly;
        mutable int                     mNeedConversion;

        /* PostBuf Lock/Cond and state is being used in postBuffer function to
         * avoid video renderer (or any client of that LayerBuffer/ISurface) to
         * call postBuffer w/o LayerBuffer flow controlling the frequency of
         * that call. w/o controlling it, setBuffer override a used buffer
         * might create video artifacts.
         * */
        Mutex     mPostBufLock;
        Condition mPostBufCond;
        PostBufPolicyBase::postBufState_t mPostBufState;

        // To avoid signaling twice in a raw with the same offset
        // FIXME mutable because DWO is const
        mutable ssize_t mPrevOffset;
        mutable bool    mDirtyQueueBit;
    };
    
    class OverlaySource : public Source {
    public:
        OverlaySource(LayerBuffer& layer,
                sp<OverlayRef>* overlayRef,
                uint32_t w, uint32_t h, int32_t format, int32_t orientation);
        virtual ~OverlaySource();
        virtual void onDraw(const Region& clip) const;
        virtual void onTransaction(uint32_t flags);
        virtual void onVisibilityResolved(const Transform& planeTransform);
        virtual void onvalidateVisibility(const Transform& globalTransform);
        virtual void destroy();
    private:

        class OverlayChannel : public BnOverlay {
            wp<LayerBuffer> mLayer;
            virtual void destroy() {
                sp<LayerBuffer> layer(mLayer.promote());
                if (layer != 0) {
                    layer->serverDestroy();
                }
            }
        public:
            OverlayChannel(const sp<LayerBuffer>& layer)
                : mLayer(layer) {
            }
        };
        
        friend class OverlayChannel;
        bool mVisibilityChanged;

        overlay_t* mOverlay;        
        overlay_handle_t mOverlayHandle;
        overlay_control_device_t* mOverlayDevice;
        uint32_t mWidth;
        uint32_t mHeight;
        int32_t mFormat;
        int32_t mWidthStride;
        int32_t mHeightStride;
        int32_t mOrientation;
        int32_t mFlip;
        int32_t mHDMIEnabled;
        mutable Mutex mOverlaySourceLock;
        bool mInitialized;
    };


    class SurfaceLayerBuffer : public LayerBaseClient::Surface
    {
    public:
        SurfaceLayerBuffer(const sp<SurfaceFlinger>& flinger,
                        const sp<LayerBuffer>& owner);
        virtual ~SurfaceLayerBuffer();

        virtual status_t registerBuffers(const ISurface::BufferHeap& buffers);
        virtual void postBuffer(ssize_t offset);
        virtual void reconfigureBuffers();
        virtual void unregisterBuffers();
        
        virtual sp<OverlayRef> createOverlay(
                uint32_t w, uint32_t h, int32_t format, int32_t orientation);
    private:
        sp<LayerBuffer> getOwner() const {
            return static_cast<LayerBuffer*>(Surface::getOwner().get());
        }
    };

    mutable Mutex   mLock;
    mutable Mutex   mReconfigMutex;
    Condition mReconfigCond;
    mutable reconfigBufState_t mReconfigStatus;

    sp<Source>      mSource;
    sp<Surface>     mSurface;
    bool            mInvalidate;
    bool            mNeedsBlending;
    mutable bool    mIsReconfiguring;
    copybit_device_t* mBlitEngine;
};

// ---------------------------------------------------------------------------

}; // namespace android

#endif // ANDROID_LAYER_BUFFER_H
