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

//#define LOG_NDEBUG 0
#define LOG_TAG "AwesomePlayer"
#include <utils/Log.h>

#include <ctype.h>
#include <dlfcn.h>

#include "include/ARTSPController.h"
#include "include/AwesomePlayer.h"
#include "include/LiveSource.h"
#include "include/SoftwareRenderer.h"
#include "include/NuCachedSource2.h"
#include "include/ThrottledSource.h"
#include "include/MPEG2TSExtractor.h"

#include "ARTPSession.h"
#include "APacketSource.h"
#include "ASessionDescription.h"
#include "UDPPusher.h"

#include <binder/IPCThreadState.h>
#include <media/stagefright/AudioPlayer.h>
#include <media/stagefright/LPAPlayer.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/OMXCodec.h>
#include <media/stagefright/Utils.h>

#include <surfaceflinger/ISurface.h>
#include <cutils/properties.h>

#include <media/stagefright/foundation/ALooper.h>

#include <cutils/properties.h>

namespace android {

static int64_t kLowWaterMarkUs = 2000000ll;  // 2secs
static int64_t kHighWaterMarkUs = 10000000ll;  // 10secs
static float kThresholdPaddingFactor = 0.1f;   // 10%
static int64_t kVideoEarlyMarginUs = -10000;   //10 ms
static int64_t kVideoLateMarginUs = 200000;  //200 ms
static const size_t kLowWaterMarkBytes = 40000;
static const size_t kHighWaterMarkBytes = 200000;

struct AwesomeEvent : public TimedEventQueue::Event {
    AwesomeEvent(
            AwesomePlayer *player,
            void (AwesomePlayer::*method)())
        : mPlayer(player),
          mMethod(method) {
    }

protected:
    virtual ~AwesomeEvent() {}

    virtual void fire(TimedEventQueue *queue, int64_t /* now_us */) {
        (mPlayer->*mMethod)();
    }

private:
    AwesomePlayer *mPlayer;
    void (AwesomePlayer::*mMethod)();

    AwesomeEvent(const AwesomeEvent &);
    AwesomeEvent &operator=(const AwesomeEvent &);
};

struct AwesomeRemoteRenderer : public AwesomeRenderer {
    AwesomeRemoteRenderer(const sp<IOMXRenderer> &target)
        : mTarget(target) {
    }

    virtual status_t initCheck() const {
        return OK;
    }

    virtual void render(MediaBuffer *buffer) {
        void *id;
        if (buffer->meta_data()->findPointer(kKeyBufferID, &id)) {
            mTarget->render((IOMX::buffer_id)id);
        }
    }

private:
    sp<IOMXRenderer> mTarget;

    AwesomeRemoteRenderer(const AwesomeRemoteRenderer &);
    AwesomeRemoteRenderer &operator=(const AwesomeRemoteRenderer &);
};

struct AwesomeLocalRenderer : public AwesomeRenderer {
    AwesomeLocalRenderer(
            bool previewOnly,
            const char *componentName,
            OMX_COLOR_FORMATTYPE colorFormat,
            const sp<ISurface> &surface,
            size_t displayWidth, size_t displayHeight,
            size_t decodedWidth, size_t decodedHeight,
            int32_t rotationDegrees)
        : mInitCheck(NO_INIT),
          mTarget(NULL),
          mLibHandle(NULL) {
            mInitCheck = init(previewOnly, componentName,
                 colorFormat, surface, displayWidth,
                 displayHeight, decodedWidth, decodedHeight,
                 rotationDegrees);
    }

    virtual status_t initCheck() const {
        return mInitCheck;
    }

    virtual void render(MediaBuffer *buffer) {
        render((const uint8_t *)buffer->data() + buffer->range_offset(),
               buffer->range_length());
    }

    void render(const void *data, size_t size) {
        mTarget->render(data, size, NULL);
    }

protected:
    virtual ~AwesomeLocalRenderer() {
        delete mTarget;
        mTarget = NULL;

        if (mLibHandle) {
            dlclose(mLibHandle);
            mLibHandle = NULL;
        }
    }

private:
    status_t mInitCheck;
    VideoRenderer *mTarget;
    void *mLibHandle;

    status_t init(
            bool previewOnly,
            const char *componentName,
            OMX_COLOR_FORMATTYPE colorFormat,
            const sp<ISurface> &surface,
            size_t displayWidth, size_t displayHeight,
            size_t decodedWidth, size_t decodedHeight,
            int32_t rotationDegrees);

    AwesomeLocalRenderer(const AwesomeLocalRenderer &);
    AwesomeLocalRenderer &operator=(const AwesomeLocalRenderer &);;
};

status_t AwesomeLocalRenderer::init(
        bool previewOnly,
        const char *componentName,
        OMX_COLOR_FORMATTYPE colorFormat,
        const sp<ISurface> &surface,
        size_t displayWidth, size_t displayHeight,
        size_t decodedWidth, size_t decodedHeight,
        int32_t rotationDegrees) {
    if (!previewOnly) {
        // We will stick to the vanilla software-color-converting renderer
        // for "previewOnly" mode, to avoid unneccessarily switching overlays
        // more often than necessary.

        mLibHandle = dlopen("libstagefrighthw.so", RTLD_NOW);

        if (mLibHandle) {
            typedef VideoRenderer *(*CreateRendererWithRotationFunc)(
                    const sp<ISurface> &surface,
                    const char *componentName,
                    OMX_COLOR_FORMATTYPE colorFormat,
                    size_t displayWidth, size_t displayHeight,
                    size_t decodedWidth, size_t decodedHeight,
                    int32_t rotationDegrees);

            typedef VideoRenderer *(*CreateRendererFunc)(
                    const sp<ISurface> &surface,
                    const char *componentName,
                    OMX_COLOR_FORMATTYPE colorFormat,
                    size_t displayWidth, size_t displayHeight,
                    size_t decodedWidth, size_t decodedHeight);

            CreateRendererWithRotationFunc funcWithRotation =
                (CreateRendererWithRotationFunc)dlsym(
                        mLibHandle,
                        "_Z26createRendererWithRotationRKN7android2spINS_8"
                        "ISurfaceEEEPKc20OMX_COLOR_FORMATTYPEjjjji");

            if (funcWithRotation) {
                mTarget =
                    (*funcWithRotation)(
                            surface, componentName, colorFormat,
                            displayWidth, displayHeight,
                            decodedWidth, decodedHeight,
                            rotationDegrees);
            } else {
                if (rotationDegrees != 0) {
                    LOGW("renderer does not support rotation.");
                }

                CreateRendererFunc func =
                    (CreateRendererFunc)dlsym(
                            mLibHandle,
                            "_Z14createRendererRKN7android2spINS_8ISurfaceEEEPKc20"
                            "OMX_COLOR_FORMATTYPEjjjj");

                if (func) {
                    mTarget =
                        (*func)(surface, componentName, colorFormat,
                            displayWidth, displayHeight,
                            decodedWidth, decodedHeight);
                }
            }
        }
    }

    if (mTarget != NULL) {
        return OK;
    }

    mTarget = new SoftwareRenderer(
            colorFormat, surface, displayWidth, displayHeight,
            decodedWidth, decodedHeight, rotationDegrees);

    return ((SoftwareRenderer *)mTarget)->initCheck();
}

AwesomePlayer::AwesomePlayer()
    : mQueueStarted(false),
      mTimeSource(NULL),
      mVideoRendererIsPreview(false),
      mAudioPlayer(NULL),
      mFlags(0),
      mExtractorFlags(0),
      mCodecFlags(0),
      mFramesDropped(0),
      mConsecutiveFramesDropped(0),
      mCatchupTimeStart(0),
      mNumTimesSyncLoss(0),
      mMaxEarlyDelta(0),
      mMaxLateDelta(0),
      mMaxTimeSyncLoss(0),
      mTotalFrames(0),
#ifndef YUVCLIENT
      mSuspensionState(NULL) {
#else
      mSuspensionState(NULL),
      mNumFrames(0),
      mFrameSize(0),
      mDecodedWidth(0),
      mDecodedHeight(0),
      mFormatChanged(false) {
#endif

    mVideoBuffer = new MediaBuffer*[BUFFER_QUEUE_CAPACITY];
    for(int i=0;i<BUFFER_QUEUE_CAPACITY;i++)
        mVideoBuffer[i] = NULL;

    CHECK_EQ(mClient.connect(), OK);

    DataSource::RegisterDefaultSniffers();

    mVideoEvent = new AwesomeEvent(this, &AwesomePlayer::onVideoEvent);
    mVideoEventPending = false;
    mStreamDoneEvent = new AwesomeEvent(this, &AwesomePlayer::onStreamDone);
    mStreamDoneEventPending = false;
    mBufferingEvent = new AwesomeEvent(this, &AwesomePlayer::onBufferingUpdate);
    mBufferingEventPending = false;

    mCheckAudioStatusEvent = new AwesomeEvent(
            this, &AwesomePlayer::onCheckAudioStatus);

    mAudioStatusEventPending = false;

    mVideoQueueFront = 0;
    mVideoQueueBack  = 0;
    mVideoQueueLastRendered = 0;
    mVideoQueueSize  = 0;

    //for statistics profiling
    char value[PROPERTY_VALUE_MAX];
    mStatistics = false;
    property_get("persist.debug.sf.statistics", value, "0");
    if(atoi(value)) mStatistics = true;

    reset();
}

AwesomePlayer::~AwesomePlayer() {
    if (mQueueStarted) {
        mQueue.stop();
    }

    reset();
    delete [] mVideoBuffer;

    mClient.disconnect();
}

void AwesomePlayer::cancelPlayerEvents(bool keepBufferingGoing) {
    mQueue.cancelEvent(mVideoEvent->eventID());
    mVideoEventPending = false;
    mQueue.cancelEvent(mStreamDoneEvent->eventID());
    mStreamDoneEventPending = false;
    mQueue.cancelEvent(mCheckAudioStatusEvent->eventID());
    mAudioStatusEventPending = false;

    if (!keepBufferingGoing) {
        mQueue.cancelEvent(mBufferingEvent->eventID());
        mBufferingEventPending = false;
    }
}

void AwesomePlayer::setListener(const wp<MediaPlayerBase> &listener) {
    Mutex::Autolock autoLock(mLock);
    mListener = listener;
}

status_t AwesomePlayer::setDataSource(
        const char *uri, const KeyedVector<String8, String8> *headers) {
    Mutex::Autolock autoLock(mLock);
    return setDataSource_l(uri, headers);
}

status_t AwesomePlayer::setDataSource_l(
        const char *uri, const KeyedVector<String8, String8> *headers) {
    reset_l();

    mUri = uri;

    if (headers) {
        mUriHeaders = *headers;
    }

    // The actual work will be done during preparation in the call to
    // ::finishSetDataSource_l to avoid blocking the calling thread in
    // setDataSource for any significant time.

    return OK;
}

status_t AwesomePlayer::setDataSource(
        int fd, int64_t offset, int64_t length) {
    Mutex::Autolock autoLock(mLock);

    reset_l();

    sp<DataSource> dataSource = new FileSource(fd, offset, length);

    status_t err = dataSource->initCheck();

    if (err != OK) {
        return err;
    }

    mFileSource = dataSource;

    return setDataSource_l(dataSource);
}

status_t AwesomePlayer::setDataSource_l(
        const sp<DataSource> &dataSource) {
    if( mMediaExtractor == NULL ) {
       mMediaExtractor = MediaExtractor::Create(dataSource);
    }
    if (mMediaExtractor == NULL) {
        return UNKNOWN_ERROR;
    }

    return setDataSource_l(mMediaExtractor);
}

status_t AwesomePlayer::setDataSource_l(const sp<MediaExtractor> &extractor) {
    // Attempt to approximate overall stream bitrate by summing all
    // tracks' individual bitrates, if not all of them advertise bitrate,
    // we have to fail.

    int64_t totalBitRate = 0;

    for (size_t i = 0; i < extractor->countTracks(); ++i) {
        sp<MetaData> meta = extractor->getTrackMetaData(i);

        int32_t bitrate;
        if (!meta->findInt32(kKeyBitRate, &bitrate)) {
            totalBitRate = -1;
            break;
        }

        totalBitRate += bitrate;
    }

    mBitrate = totalBitRate;

    LOGV("mBitrate = %lld bits/sec", mBitrate);

    bool haveAudio = false;
    bool haveVideo = false;
    for (size_t i = 0; i < extractor->countTracks(); ++i) {
        sp<MetaData> meta = extractor->getTrackMetaData(i);

        const char *mime;
        CHECK(meta->findCString(kKeyMIMEType, &mime));

        if (!haveVideo && !strncasecmp(mime, "video/", 6)) {
            setVideoSource(extractor->getTrack(i));
            haveVideo = true;
        } else if (!haveAudio && !strncasecmp(mime, "audio/", 6)) {
            setAudioSource(extractor->getTrack(i));
            haveAudio = true;

            if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_VORBIS)) {
                // Only do this for vorbis audio, none of the other audio
                // formats even support this ringtone specific hack and
                // retrieving the metadata on some extractors may turn out
                // to be very expensive.
                sp<MetaData> fileMeta = extractor->getMetaData();
                int32_t loop;
                if (fileMeta != NULL
                        && fileMeta->findInt32(kKeyAutoLoop, &loop) && loop != 0) {
                    mFlags |= AUTO_LOOPING;
                }
            }
        }

        if (haveAudio && haveVideo) {
            break;
        }
    }

    if (!haveAudio && !haveVideo) {
        return UNKNOWN_ERROR;
    }

    mExtractorFlags = extractor->flags();

    return OK;
}

void AwesomePlayer::reset() {
    Mutex::Autolock autoLock(mLock);
    reset_l();
}

void AwesomePlayer::releaseAllVideoBuffersHeld() {
    for(int i=0;i<BUFFER_QUEUE_CAPACITY;i++){
        if (mVideoBuffer[i] != NULL) {
            mVideoBuffer[i]->release();
            mVideoBuffer[i] = NULL;
        }
    }
    mVideoQueueFront = 0;
    mVideoQueueBack  = 0;
    mVideoQueueLastRendered = 0;
    mVideoQueueSize  = 0;
}

void AwesomePlayer::reset_l() {
    if (mFlags & PREPARING) {
        mFlags |= PREPARE_CANCELLED;
        if (mConnectingDataSource != NULL) {
            LOGI("interrupting the connection process");
            mConnectingDataSource->disconnect();
        }
    }

    while (mFlags & PREPARING) {
        mPreparedCondition.wait(mLock);
    }

    cancelPlayerEvents();

    if (mStatistics && mVideoSource != NULL) {
        logStatistics();
        logSyncLoss();
    }

    mCachedSource.clear();
    mAudioTrack.clear();
    mVideoTrack.clear();

    // Shutdown audio first, so that the respone to the reset request
    // appears to happen instantaneously as far as the user is concerned
    // If we did this later, audio would continue playing while we
    // shutdown the video-related resources and the player appear to
    // not be as responsive to a reset request.
    if (mAudioPlayer == NULL && mAudioSource != NULL) {
        // If we had an audio player, it would have effectively
        // taken possession of the audio source and stopped it when
        // _it_ is stopped. Otherwise this is still our responsibility.
        mAudioSource->stop();
    }
    mAudioSource.clear();

    mTimeSource = NULL;

    delete mAudioPlayer;
    mAudioPlayer = NULL;

    mVideoRenderer.clear();

    releaseAllVideoBuffersHeld();

    if (mRTSPController != NULL) {
        mRTSPController->disconnect();
        mRTSPController.clear();
    }

    mRTPPusher.clear();
    mRTCPPusher.clear();
    mRTPSession.clear();

    if (mVideoSource != NULL) {
        mVideoSource->stop();

        // The following hack is necessary to ensure that the OMX
        // component is completely released by the time we may try
        // to instantiate it again.
        wp<MediaSource> tmp = mVideoSource;
        mVideoSource.clear();
        while (tmp.promote() != NULL) {
            usleep(1000);
        }
        IPCThreadState::self()->flushCommands();
    }

    mDurationUs = mVideoDurationUs = mAudioDurationUs = -1;
    mFlags = 0;
    mExtractorFlags = 0;
    mVideoWidth = mVideoHeight = -1;
    mTimeSourceDeltaUs = 0;
    mVideoTimeUs = 0;

    mSeeking = false;
    mSeekNotificationSent = false;
    mSeekTimeUs = 0;

    mUri.setTo("");
    mUriHeaders.clear();

    mFileSource.clear();
    mMediaExtractor.clear();

    delete mSuspensionState;
    mSuspensionState = NULL;

    mBitrate = -1;

#ifdef YUVCLIENT
    resetFrameBuffers();
#endif
}

void AwesomePlayer::notifyListener_l(int msg, int ext1, int ext2) {
    if (mListener != NULL) {
        sp<MediaPlayerBase> listener = mListener.promote();

        if (listener != NULL) {
            listener->sendEvent(msg, ext1, ext2);
        }
    }
}

bool AwesomePlayer::getBitrate(int64_t *bitrate) {
    off64_t size;
    if (mDurationUs >= 0 && mCachedSource != NULL
            && mCachedSource->getSize(&size) == OK) {
        *bitrate = size * 8000000ll / mDurationUs;  // in bits/sec
        return true;
    }

    if (mBitrate >= 0) {
        *bitrate = mBitrate;
        return true;
    }

    *bitrate = 0;

    return false;
}

// Returns true iff cached duration is available/applicable.
bool AwesomePlayer::getCachedDuration_l(int64_t *durationUs, bool *eos) {
    int64_t bitrate;

    if (mRTSPController != NULL) {
        *durationUs = mRTSPController->getQueueDurationUs(eos);
        return true;
    } else if (mCachedSource != NULL && getBitrate(&bitrate)) {
        size_t cachedDataRemaining = mCachedSource->approxDataRemaining(eos);
        *durationUs = cachedDataRemaining * 8000000ll / bitrate;
        return true;
    }

    return false;
}

void AwesomePlayer::onBufferingUpdate() {
    Mutex::Autolock autoLock(mLock);
    if (!mBufferingEventPending) {
        return;
    }
    mBufferingEventPending = false;
    bool cacheFull = false;

    if (mCachedSource != NULL) {
        bool eos;
        size_t cachedDataRemaining = mCachedSource->approxDataRemaining(&eos);
        cacheFull = mCachedSource->isCacheFull();

        if (eos) {
            notifyListener_l(MEDIA_BUFFERING_UPDATE, 100);
            if (mFlags & PREPARING) {
                LOGI("cache has reached EOS, prepare is done.");
                finishAsyncPrepare_l();
            }
        } else {
            int64_t bitrate;
            if (getBitrate(&bitrate)) {
                LOGV("onBufferingUpdate: bitrate: %lld", bitrate);
                size_t cachedSize = mCachedSource->cachedSize();
                int64_t cachedDurationUs = cachedSize * 8000000ll / bitrate;

                int percentage = 100.0 * (double)cachedDurationUs / mDurationUs;
                if (percentage > 100) {
                    percentage = 100;
                }

                notifyListener_l(MEDIA_BUFFERING_UPDATE, percentage);
            } else {
                // We don't know the bitrate of the stream, use absolute size
                // limits to maintain the cache.

                if ((mFlags & PLAYING) && !eos
                        && (cachedDataRemaining < kLowWaterMarkBytes)) {
                    LOGI("cache is running low (< %d) , pausing.",
                         kLowWaterMarkBytes);
                    mFlags |= CACHE_UNDERRUN;
                    pause_l();
                    mCachedSource->resumeFetchingIfNecessary();
                    notifyListener_l(MEDIA_INFO, MEDIA_INFO_BUFFERING_START);
                } else if (eos || cachedDataRemaining > kHighWaterMarkBytes || cacheFull) {
                    if (mFlags & CACHE_UNDERRUN) {
                        LOGI("cache has filled up (> %d), resuming.",
                             kHighWaterMarkBytes);
                        mFlags &= ~CACHE_UNDERRUN;
                        play_l();
                        notifyListener_l(MEDIA_INFO, MEDIA_INFO_BUFFERING_END);
                    } else if (mFlags & PREPARING) {
                        LOGV("cache has filled up (> %d), prepare is done",
                             kHighWaterMarkBytes);
                        finishAsyncPrepare_l();
                    }
                }
            }
        }
    }

    int64_t cachedDurationUs;
    bool eos;
    if (getCachedDuration_l(&cachedDurationUs, &eos)) {
        if ((mFlags & PLAYING) && !eos
                && (cachedDurationUs < kLowWaterMarkUs)) {
            LOGI("cache is running low (%.2f secs) , pausing.",
                 cachedDurationUs / 1E6);
            mFlags |= CACHE_UNDERRUN;
            pause_l();
            if (mCachedSource != NULL) {
                mCachedSource->resumeFetchingIfNecessary();
            }
            notifyListener_l(MEDIA_INFO, MEDIA_INFO_BUFFERING_START);
        } else if (eos || cachedDurationUs > kHighWaterMarkUs || cacheFull) {
            if (mFlags & CACHE_UNDERRUN) {
                LOGI("cache has filled up (%.2f secs), resuming.",
                     cachedDurationUs / 1E6);
                mFlags &= ~CACHE_UNDERRUN;
                play_l();
                notifyListener_l(MEDIA_INFO, MEDIA_INFO_BUFFERING_END);
            } else if (mFlags & PREPARING) {
                LOGV("cache has filled up (%.2f secs), prepare is done",
                     cachedDurationUs / 1E6);
                finishAsyncPrepare_l();
            }
        }
    }

    postBufferingEvent_l();
}

void AwesomePlayer::partial_reset_l() {
    // Only reset the video renderer and shut down the video decoder.
    // Then instantiate a new video decoder and resume video playback.

    mVideoRenderer.clear();

    if( mVideoBuffer[mVideoQueueBack] ){
      mVideoBuffer[mVideoQueueBack]->release( );
      mVideoBuffer[mVideoQueueBack] = NULL;
    }
    
    if ( mVideoBuffer[mVideoQueueFront] ){
      mVideoBuffer[mVideoQueueFront]->release();
      mVideoBuffer[mVideoQueueFront] = NULL;
    }

    {
        mVideoSource->stop();

        // The following hack is necessary to ensure that the OMX
        // component is completely released by the time we may try
        // to instantiate it again.
        wp<MediaSource> tmp = mVideoSource;
        mVideoSource.clear();
        while (tmp.promote() != NULL) {
            usleep(1000);
        }
        IPCThreadState::self()->flushCommands();
    }

    CHECK_EQ(OK, initVideoDecoder(OMXCodec::kIgnoreCodecSpecificData));
}

void AwesomePlayer::onStreamDone() {
    // Posted whenever any stream finishes playing.

    Mutex::Autolock autoLock(mLock);
    if (!mStreamDoneEventPending) {
        return;
    }
    mStreamDoneEventPending = false;

    if (mStreamDoneStatus == INFO_DISCONTINUITY) {
        // This special status is returned because an http live stream's
        // video stream switched to a different bandwidth at this point
        // and future data may have been encoded using different parameters.
        // This requires us to shutdown the video decoder and reinstantiate
        // a fresh one.

        LOGV("INFO_DISCONTINUITY");

        CHECK(mVideoSource != NULL);

        partial_reset_l();
        postVideoEvent_l();
        return;
    } else if ((mStreamDoneStatus != ERROR_IO) && (mStreamDoneStatus != ERROR_END_OF_STREAM)) {
        LOGV("MEDIA_ERROR %d", mStreamDoneStatus);

        notifyListener_l(
                MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, mStreamDoneStatus);

        pause_l(true /* at eos */);

        mFlags |= AT_EOS;
        return;
    }

    const bool allDone =
        (mVideoSource == NULL || (mFlags & VIDEO_AT_EOS))
            && (mAudioSource == NULL || (mFlags & AUDIO_AT_EOS));

    if (!allDone) {
        return;
    }

    if (mFlags & (LOOPING | AUTO_LOOPING)) {
        seekTo_l(0);

        if (mVideoSource != NULL) {
            postVideoEvent_l();
            if (mStatistics) {
                logStatistics();
                logSyncLoss();
            }
        }
    } else {
        LOGV("MEDIA_PLAYBACK_COMPLETE");
        notifyListener_l(MEDIA_PLAYBACK_COMPLETE);

        pause_l(true /* at eos */);

        mFlags |= AT_EOS;
    }
}

status_t AwesomePlayer::play() {
    Mutex::Autolock autoLock(mLock);

    mFlags &= ~CACHE_UNDERRUN;

    return play_l();
}

status_t AwesomePlayer::play_l() {
    if (mFlags & PLAYING) {
        return OK;
    }

    if (!(mFlags & PREPARED)) {
        status_t err = prepare_l();

        if (err != OK) {
            return err;
        }
    }

    mFlags |= PLAYING;
    mFlags |= FIRST_FRAME;


    if (mAudioSource != NULL) {
        if (mAudioPlayer == NULL) {
            if (mAudioSink != NULL) {
                sp<MetaData> format = mAudioTrack->getFormat();
                const char *mime;
                bool success = format->findCString(kKeyMIMEType, &mime);
                CHECK(success);

                int64_t durationUs;
                success = format->findInt64(kKeyDuration, &durationUs);
                /*
                 * Some clips may not have kKeyDuration set, especially so for clips in a MP3
                 * container with the Frames field absent in the Xing header.
                 */
                if (!success)
                    durationUs = 0;

                LOGV("LPAPlayer::getObjectsAlive() %d",LPAPlayer::objectsAlive);

                char lpaDecode[128];
                property_get("lpa.decode",lpaDecode,"0");
                char lpaStagefright[128];
                property_get("lpa.use-stagefright",lpaStagefright,"0");
                if(strcmp("true",lpaDecode) == 0 && strcmp("true",lpaStagefright) == 0)
                {
                    LOGV("LPAPlayer::getObjectsAlive() %d",LPAPlayer::objectsAlive);
                    if ( durationUs > 60000000 && (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_MPEG) || !strcasecmp(mime,MEDIA_MIMETYPE_AUDIO_AAC)) && LPAPlayer::objectsAlive == 0 && mVideoSource == NULL) {
                        LOGE("LPAPlayer created, LPA MODE detected mime %s duration %d\n", mime, durationUs);
                        bool initCheck =  false;
                        mAudioPlayer = new LPAPlayer(mAudioSink, initCheck, this);
                        if(!initCheck) {
                            delete mAudioPlayer;
                            mAudioPlayer = NULL;
                        }
                    }
                }
                if(mAudioPlayer == NULL) {
                    LOGE("AudioPlayer created, Non-LPA mode mime %s duration %d\n", mime, durationUs);
                    mAudioPlayer = new AudioPlayer(mAudioSink, this);
                }

                LOGV("Setting Audio source");
                mAudioPlayer->setSource(mAudioSource);

                mWatchForAudioSeekComplete = false;

                // If there was a seek request while we were paused
                // and we're just starting up again, honor the request now.
                seekAudioIfNecessary_l();

                // We've already started the MediaSource in order to enable
                // the prefetcher to read its data.
                status_t err = mAudioPlayer->start(
                        true /* sourceAlreadyStarted */);

                if (err != OK) {
                    delete mAudioPlayer;
                    mAudioPlayer = NULL;

                    mFlags &= ~(PLAYING | FIRST_FRAME);

                    return err;
                }

                mTimeSource = mAudioPlayer;

                mWatchForAudioEOS = true;
            }
        } else {
            mAudioPlayer->resume();
        }
    }

    if (mTimeSource == NULL && mAudioPlayer == NULL) {
        mTimeSource = &mSystemTimeSource;
    }

    if (mStatistics) {
        mFirstFrameLatencyStartUs = getTimeOfDayUs();
        mVeryFirstFrame = true;
    }

    if (mVideoSource != NULL) {
        // Kick off video playback
        postVideoEvent_l();
    }

    if (mFlags & AT_EOS) {
        // Legacy behaviour, if a stream finishes playing and then
        // is started again, we play from the start...
        seekTo_l(0);
    }

    return OK;
}

status_t AwesomePlayer::initRenderer_l() {
    if (mISurface == NULL) {
        return OK;
    }

    sp<MetaData> meta = mVideoSource->getFormat();

    int32_t format;
    const char *component;
	int32_t decodedWidth = 0, decodedHeight = 0;
	CHECK(meta->findInt32(kKeyColorFormat, &format));
	CHECK(meta->findCString(kKeyDecoderComponent, &component));
	//Update width, height stride and slice height from metadata
	//and use this to create renderer
	CHECK(meta->findInt32(kKeyWidth, &mVideoWidth));
	CHECK(meta->findInt32(kKeyHeight, &mVideoHeight));
	//Software decoder does not use stride and slice height.
	//Update decode width and height to width and height
	//if the stride or slice height returned from decoder
	//is zero.
	if (!(meta->findInt32(kKeyStride, &decodedWidth)))
			decodedWidth = mVideoWidth;
	if(!(meta->findInt32(kKeySliceHeight, &decodedHeight)))
			decodedHeight = mVideoHeight;


	int32_t rotationDegrees;
	if (!mVideoTrack->getFormat()->findInt32(
							kKeyRotation, &rotationDegrees)) {
			rotationDegrees = 0;
    }

    mVideoRenderer.clear();

    // Must ensure that mVideoRenderer's destructor is actually executed
    // before creating a new one.
    IPCThreadState::self()->flushCommands();

    if (!strncmp("OMX.", component, 4)) {
        // Our OMX codecs allocate buffers on the media_server side
        // therefore they require a remote IOMXRenderer that knows how
        // to display them.

        sp<IOMXRenderer> native =
            mClient.interface()->createRenderer(
                    mISurface, component,
                    (OMX_COLOR_FORMATTYPE)format,
                    decodedWidth, decodedHeight,
                    mVideoWidth, mVideoHeight,
                    rotationDegrees);

        if (native == NULL) {
            return NO_INIT;
        }

        mVideoRenderer = new AwesomeRemoteRenderer(native);
    } else {
        // Other decoders are instantiated locally and as a consequence
        // allocate their buffers in local address space.
        mVideoRenderer = new AwesomeLocalRenderer(
            false,  // previewOnly
            component,
            (OMX_COLOR_FORMATTYPE)format,
            mISurface,
            mVideoWidth, mVideoHeight,
            decodedWidth, decodedHeight, rotationDegrees);
    }

    return mVideoRenderer->initCheck();
}

status_t AwesomePlayer::pause() {
    Mutex::Autolock autoLock(mLock);

    mFlags &= ~CACHE_UNDERRUN;

    return pause_l();
}

status_t AwesomePlayer::pause_l(bool at_eos) {
    if (!(mFlags & PLAYING)) {
        return OK;
    }

    cancelPlayerEvents(true /* keepBufferingGoing */);

    if (mAudioPlayer != NULL) {
        if (at_eos) {
            // If we played the audio stream to completion we
            // want to make sure that all samples remaining in the audio
            // track's queue are played out.
            mAudioPlayer->pause(true /* playPendingSamples */);
        } else {
            mAudioPlayer->pause();
        }
    }

    mFlags &= ~PLAYING;

    if(mStatistics && !(mFlags & AT_EOS)) logPause();
    return OK;
}

bool AwesomePlayer::isPlaying() const {
    return (mFlags & PLAYING) || (mFlags & CACHE_UNDERRUN);
}

void AwesomePlayer::setISurface(const sp<ISurface> &isurface) {
    Mutex::Autolock autoLock(mLock);

    mISurface = isurface;
}

void AwesomePlayer::setAudioSink(
        const sp<MediaPlayerBase::AudioSink> &audioSink) {
    Mutex::Autolock autoLock(mLock);

    mAudioSink = audioSink;
}

status_t AwesomePlayer::setLooping(bool shouldLoop) {
    Mutex::Autolock autoLock(mLock);

    mFlags = mFlags & ~LOOPING;

    if (shouldLoop) {
        mFlags |= LOOPING;
    }

    return OK;
}

status_t AwesomePlayer::getDuration(int64_t *durationUs) {
    Mutex::Autolock autoLock(mMiscStateLock);

    if (mDurationUs < 0) {
        return UNKNOWN_ERROR;
    }

    *durationUs = mDurationUs;

    return OK;
}

status_t AwesomePlayer::getPosition(int64_t *positionUs) {
    if (mRTSPController != NULL) {
        *positionUs = mRTSPController->getNormalPlayTimeUs();
    } else if (mSeeking) {
        *positionUs = mSeekTimeUs;
    } else if (mVideoSource != NULL && !(mFlags & VIDEO_AT_EOS)) {
        Mutex::Autolock autoLock(mMiscStateLock);
        *positionUs = mVideoTimeUs;
    } else if (mAudioPlayer != NULL && !(mFlags & AUDIO_AT_EOS)) {
        *positionUs = mAudioPlayer->getMediaTimeUs();
    } else if ((mFlags & VIDEO_AT_EOS) || (mFlags & AUDIO_AT_EOS)){
         Mutex::Autolock autoLock(mMiscStateLock);
         if(mVideoSource != NULL) *positionUs = mVideoDurationUs;
         if(mAudioPlayer != NULL) {
            if (mAudioPlayer->getMediaTimeUs() > mVideoDurationUs)
                *positionUs = mAudioPlayer->getMediaTimeUs();
            }
    }else {
        *positionUs = 0;
    }

    return OK;
}

status_t AwesomePlayer::seekTo(int64_t timeUs) {
    if (mExtractorFlags & MediaExtractor::CAN_SEEK) {
        Mutex::Autolock autoLock(mLock);
        return seekTo_l(timeUs);
    } else {
       notifyListener_l(MEDIA_SEEK_COMPLETE);
       mSeekNotificationSent = true;
    }

    return OK;
}

// static
void AwesomePlayer::OnRTSPSeekDoneWrapper(void *cookie) {
    static_cast<AwesomePlayer *>(cookie)->onRTSPSeekDone();
}

void AwesomePlayer::onRTSPSeekDone() {
    notifyListener_l(MEDIA_SEEK_COMPLETE);
    mSeekNotificationSent = true;
}

status_t AwesomePlayer::seekTo_l(int64_t timeUs) {
    if (mRTSPController != NULL) {
        mRTSPController->seekAsync(timeUs, OnRTSPSeekDoneWrapper, this);
        return OK;
    }

    if (mFlags & CACHE_UNDERRUN) {
        mFlags &= ~CACHE_UNDERRUN;
        play_l();
    }

    mSeeking = true;
    if (mStatistics) mFirstFrameLatencyStartUs = getTimeOfDayUs();
    mSeekNotificationSent = false;
    mSeekTimeUs = timeUs;

    //Clear the VIDEO and AUDIO EOS flags only conditionally
    //for better performance; but clear AT_EOS unconditionally
    if (timeUs < mVideoDurationUs)
        mFlags &= ~VIDEO_AT_EOS;

    if (timeUs < mAudioDurationUs)
        mFlags &= ~AUDIO_AT_EOS;

    mFlags &= ~AT_EOS;

    seekAudioIfNecessary_l();

    if (!(mFlags & PLAYING)) {
        LOGV("seeking while paused, sending SEEK_COMPLETE notification"
             " immediately.");
        notifyListener_l(MEDIA_SEEK_COMPLETE);
        mSeekNotificationSent = true;
    }

    return OK;
}

void AwesomePlayer::seekAudioIfNecessary_l() {
    if (mSeeking
        && (mVideoSource == NULL || (mFlags & VIDEO_AT_EOS))
        && mAudioPlayer != NULL) {
        mAudioPlayer->seekTo(mSeekTimeUs);

        mWatchForAudioSeekComplete = true;
        mWatchForAudioEOS = true;
        mSeekNotificationSent = false;
    }
}

status_t AwesomePlayer::getVideoDimensions(
        int32_t *width, int32_t *height) const {
    Mutex::Autolock autoLock(mLock);

    if (mVideoWidth < 0 || mVideoHeight < 0) {
        return UNKNOWN_ERROR;
    }

    *width = mVideoWidth;
    *height = mVideoHeight;

    return OK;
}

void AwesomePlayer::setAudioSource(sp<MediaSource> source) {
    CHECK(source != NULL);

    mAudioTrack = source;
}

status_t AwesomePlayer::initAudioDecoder() {
    sp<MetaData> meta = mAudioTrack->getFormat();

    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));

    if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_RAW)) {
        mAudioSource = mAudioTrack;
    } else {
        mAudioSource = OMXCodec::Create(
                mClient.interface(), mAudioTrack->getFormat(),
                false, // createEncoder
                mAudioTrack, NULL, mCodecFlags);
    }

    if (mAudioSource != NULL) {
        int64_t durationUs;
        if (mAudioTrack->getFormat()->findInt64(kKeyDuration, &durationUs)) {
            Mutex::Autolock autoLock(mMiscStateLock);
            if (mDurationUs < 0 || durationUs > mDurationUs) {
                mDurationUs = durationUs;
            }
            mAudioDurationUs = durationUs;
        }

        status_t err = mAudioSource->start();

        if (err != OK) {
            mAudioSource.clear();
            return err;
        }
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_QCELP)) {
        // For legacy reasons we're simply going to ignore the absence
        // of an audio decoder for QCELP instead of aborting playback
        // altogether.
        return OK;
    }

    return mAudioSource != NULL ? OK : UNKNOWN_ERROR;
}

void AwesomePlayer::setVideoSource(sp<MediaSource> source) {
    CHECK(source != NULL);

    mVideoTrack = source;
}

status_t AwesomePlayer::initVideoDecoder(uint32_t flags) {
    char value[PROPERTY_VALUE_MAX];

    flags |= mCodecFlags; //or whatever was added to mCodecFlags
                          //from setParameter calls to the flags
    property_get("sys.media.vdec.sw", value, "0");
    if (atoi(value)) {
        LOGW("Software Codec is prefered for Video");
        flags |= OMXCodec::kPreferSoftwareCodecs;
    }

    if(mFileSource != NULL) {
        flags |= OMXCodec::kLocalFileMode;
    }

    mVideoSource = OMXCodec::Create(
            mClient.interface(), mVideoTrack->getFormat(),
            false, // createEncoder
            mVideoTrack,
            NULL, flags);

    if (mVideoSource != NULL) {
        int64_t durationUs;
        if (mVideoTrack->getFormat()->findInt64(kKeyDuration, &durationUs)) {
            Mutex::Autolock autoLock(mMiscStateLock);
            if (mDurationUs < 0 || durationUs > mDurationUs) {
                mDurationUs = durationUs;
            }
            mVideoDurationUs = durationUs;
        }

        CHECK(mVideoTrack->getFormat()->findInt32(kKeyWidth, &mVideoWidth));
        CHECK(mVideoTrack->getFormat()->findInt32(kKeyHeight, &mVideoHeight));

        status_t err = mVideoSource->start();

        if (err != OK) {
            mVideoSource.clear();
            return err;
        }
    }

    return mVideoSource != NULL ? OK : UNKNOWN_ERROR;
}

void AwesomePlayer::finishSeekIfNecessary(int64_t videoTimeUs) {
    if (!mSeeking) {
        return;
    }

    if (mAudioPlayer != NULL) {
        LOGV("seeking audio to %lld us (%.2f secs).", videoTimeUs, videoTimeUs / 1E6);

        // If we don't have a video time, seek audio to the originally
        // requested seek time instead.

        mAudioPlayer->seekTo(videoTimeUs < 0 ? mSeekTimeUs : videoTimeUs);
        mAudioPlayer->resume();
        mWatchForAudioSeekComplete = true;
        mWatchForAudioEOS = true;
    } else if (!mSeekNotificationSent) {
        // If we're playing video only, report seek complete now,
        // otherwise audio player will notify us later.
        notifyListener_l(MEDIA_SEEK_COMPLETE);
    }

    mFlags |= FIRST_FRAME;
    mSeeking = false;
    mSeekNotificationSent = false;
    if (mStatistics) logSeek();
}

void AwesomePlayer::onVideoEvent() {
    Mutex::Autolock autoLock(mLock);
    if (!mVideoEventPending) {
        // The event has been cancelled in reset_l() but had already
        // been scheduled for execution at that time.
        return;
    }
    mVideoEventPending = false;

    if (mFlags & VIDEO_AT_EOS)
    {
        // If video is at EOS, we have nothing to do, we'll continue
        // doing nothing until the video EOS flag is cleared
        if(mAudioPlayer == NULL || (mFlags & AUDIO_AT_EOS)) {
            /*
            To handle the below scenario:
            Play the Clip, Reach the EOS and called postStreamDoneEvent_l
            b4 notification happens to app  PAUSE called immediately
            which clears the event.So the lost EOS Event is Returned
            on next Play call. For Audio Video clips AudioPlayer::resume()
            should take care of it
            */
            postStreamDoneEvent_l(ERROR_END_OF_STREAM);
            return;
        }
        postVideoEvent_l();
        return;
    }

    if (mSeeking) {
        releaseAllVideoBuffersHeld();

        if (mCachedSource != NULL && mAudioSource != NULL) {
            // We're going to seek the video source first, followed by
            // the audio source.
            // In order to avoid jumps in the DataSource offset caused by
            // the audio codec prefetching data from the old locations
            // while the video codec is already reading data from the new
            // locations, we'll "pause" the audio source, causing it to
            // stop reading input data until a subsequent seek.

            if (mAudioPlayer != NULL) {
                mAudioPlayer->pause();
            }
            mAudioSource->pause();
        }
    }

#ifdef YUVCLIENT
    if (mFormatChanged && mFrameBuffers != NULL)
        sendBackFrameBuffers();
#endif

    if (mVideoBuffer[mVideoQueueBack] == NULL) {
        MediaSource::ReadOptions options;
        if (mSeeking) {
            LOGV("seeking to %lld us (%.2f secs)", mSeekTimeUs, mSeekTimeUs / 1E6);

            options.setSeekTo(
                    mSeekTimeUs, MediaSource::ReadOptions::SEEK_CLOSEST_SYNC);
        }
        for (;;) {
            status_t err = mVideoSource->read(&mVideoBuffer[mVideoQueueBack], &options);
            options.clearSeekTo();

            if (err != OK) {
                CHECK_EQ(mVideoBuffer[mVideoQueueBack], NULL);

                if (err == INFO_FORMAT_CHANGED) {
                    LOGV("VideoSource signalled format change.");

#ifdef YUVCLIENT
                    if (mFrameBuffers != NULL) {
                        int32_t width;
                        int32_t height;
                        int32_t colorFormat;

                        sp<MetaData> meta = mVideoSource->getFormat();
                        CHECK(meta->findInt32(kKeyWidth, &width));
                        CHECK(meta->findInt32(kKeyHeight, &height));
                        CHECK(meta->findInt32(kKeyColorFormat, &colorFormat));

                        if ((width != mDecodedWidth) || (height != mDecodedHeight) || (colorFormat != mColorFormat)) {
                            mDecodedWidth = width;
                            mDecodedHeight = height;
                            mColorFormat = colorFormat;

                            notifyListener_l(MEDIA_SET_VIDEO_SIZE, mDecodedWidth, mDecodedHeight);
                            notifyListener_l(MEDIA_FORMAT_CHANGED);

                            mFormatChanged = true;
                            sendBackFrameBuffers();
                        }
                    }
#endif

                    if (mVideoRenderer != NULL) {
                        mVideoRendererIsPreview = false;
                        err = initRenderer_l();
                        CHECK_EQ(err, OK);
                    }
                    releaseAllVideoBuffersHeld();
                    continue;
                }

                // So video playback is complete, but we may still have
                // a seek request pending that needs to be applied
                // to the audio track.
                if (mSeeking) {
                    LOGV("video stream ended while seeking!");
                }
                finishSeekIfNecessary(-1);

                LOGW("Got video EOS!");
                mFlags |= VIDEO_AT_EOS;
                if (mVideoTimeUs > mVideoDurationUs) mVideoDurationUs = mVideoTimeUs;
                postStreamDoneEvent_l(err);
                postVideoEvent_l();
                return;
            }

            if (mVideoBuffer[mVideoQueueBack]->range_length() == 0) {
                // Some decoders, notably the PV AVC software decoder
                // return spurious empty buffers that we just want to ignore.

                mVideoBuffer[mVideoQueueBack]->release();
                mVideoBuffer[mVideoQueueBack] = NULL;
                continue;
            }

            break;
        }
    }

    int64_t timeUs;
    CHECK(mVideoBuffer[mVideoQueueBack]->meta_data()->findInt64(kKeyTime, &timeUs));

    {
        Mutex::Autolock autoLock(mMiscStateLock);
        mVideoTimeUs = timeUs;

	int64_t decodingTime = timeUs;
	int64_t mEditTime = 0;
	mVideoTrack->getFormat( )->findInt64( kKeyEditOffset, &mEditTime );
	decodingTime += mEditTime;
	timeUs = decodingTime;

	mVideoTimeUs = timeUs;

    }

    bool wasSeeking = mSeeking;
    finishSeekIfNecessary(timeUs);

    TimeSource *ts = (mFlags & AUDIO_AT_EOS) ? &mSystemTimeSource : mTimeSource;

    if (mFlags & FIRST_FRAME) {
        mFlags &= ~FIRST_FRAME;

        mTimeSourceDeltaUs = ts->getRealTimeUs() - timeUs;
        setNumFramesToHold();
        if (mStatistics && mVeryFirstFrame) logFirstFrame();
    }

    int64_t realTimeUs, mediaTimeUs;
    if (!(mFlags & AUDIO_AT_EOS) && mAudioPlayer != NULL
        && mAudioPlayer->getMediaTimeMapping(&realTimeUs, &mediaTimeUs)) {
        mTimeSourceDeltaUs = realTimeUs - mediaTimeUs;
    }

    int64_t nowUs = ts->getRealTimeUs() - mTimeSourceDeltaUs;

    int64_t latenessUs = nowUs - timeUs;

    if (wasSeeking) {
        // Let's display the first frame after seeking right away.
        latenessUs = 0;
    }

    if (mRTPSession != NULL) {
        // We'll completely ignore timestamps for gtalk videochat
        // and we'll play incoming video as fast as we get it.
        latenessUs = 0;
    }

    if (latenessUs > kVideoLateMarginUs) {
        // We're more than 200ms late.
        LOGV("we're late by %lld us (%.2f secs)", latenessUs, latenessUs / 1E6);

        mVideoBuffer[mVideoQueueBack]->release();
        mVideoBuffer[mVideoQueueBack] = NULL;

        if (mStatistics) {
            mFramesDropped++;
            mConsecutiveFramesDropped++;
            if (mConsecutiveFramesDropped == 1) {
                mCatchupTimeStart = mTimeSource->getRealTimeUs();
            }
            if (!(mFlags & AT_EOS)) logLate(timeUs,nowUs,latenessUs);
        }
        postVideoEvent_l();
        return;
    }

    if (latenessUs < kVideoEarlyMarginUs) {
        // We're more than 10ms early.

        if (mStatistics) {
            logOnTime(timeUs,nowUs,latenessUs);
            mConsecutiveFramesDropped = 0;
        }
        postVideoEvent_l(10000);
        return;
    }

    if (mVideoRendererIsPreview || mVideoRenderer == NULL) {
        mVideoRendererIsPreview = false;

#ifdef YUVCLIENT
        if (mFrameBuffers == NULL) {  // initialize render if not sending yuv frames to client
#endif
            status_t err = initRenderer_l();

            if (err != OK) {
                finishSeekIfNecessary(-1);

                mFlags |= VIDEO_AT_EOS;
                postStreamDoneEvent_l(err);
                return;
            }
#ifdef YUVCLIENT
        }
#endif
    }

#ifdef YUVCLIENT
    if (mFrameBuffers != NULL) {
        // send yuv frames to client
        int frame = getAvailableFrameBuffer();
        if (-1 == frame) {
            LOGV("No frame buffer available, yuv frame lost");
            postVideoEvent_l();
            return;
        }

        size_t copySize =
            (mVideoBuffer[mVideoQueueBack]->range_length() < mFrameSize) ?
            mVideoBuffer[mVideoQueueBack]->range_length() : mFrameSize;

        // need to copy YUV data from decoder memory pool to shared memory
        // region -- ideally, the decoder memory pool can be shared with the
        // mediaplayer client
        void* bufferAddress = getFrameBufferAddress(frame);
        if (bufferAddress != NULL) {
            memcpy(bufferAddress, mVideoBuffer[mVideoQueueBack]->data(), copySize);

            notifyListener_l(MEDIA_BUFFER_READY, frame, 1);
        }
    }
#endif

    if (mVideoRenderer != NULL) {
        mVideoRenderer->render(mVideoBuffer[mVideoQueueBack]);
        if (mStatistics) {
            mTotalFrames++;
            logOnTime(timeUs,nowUs,latenessUs);
            mConsecutiveFramesDropped = 0;
        }
    }

    mVideoQueueLastRendered = mVideoQueueBack;
    mVideoQueueBack = (++mVideoQueueBack)%(BUFFER_QUEUE_CAPACITY);
    mVideoQueueSize++;
    if (mVideoQueueSize > mNumFramesToHold) {
        mVideoBuffer[mVideoQueueFront]->release();
        mVideoBuffer[mVideoQueueFront] = NULL;
        mVideoQueueFront = (++mVideoQueueFront)%(BUFFER_QUEUE_CAPACITY);
        mVideoQueueSize--;
    }

    postVideoEvent_l();
}

void AwesomePlayer::postVideoEvent_l(int64_t delayUs) {
    if (mVideoEventPending) {
        return;
    }

    mVideoEventPending = true;
    mQueue.postEventWithDelay(mVideoEvent, delayUs < 0 ? 10000 : delayUs);
}

void AwesomePlayer::postStreamDoneEvent_l(status_t status) {
    if (mStreamDoneEventPending) {
        return;
    }
    mStreamDoneEventPending = true;

    mStreamDoneStatus = status;
    mQueue.postEvent(mStreamDoneEvent);
}

void AwesomePlayer::postBufferingEvent_l() {
    if (mBufferingEventPending) {
        return;
    }
    mBufferingEventPending = true;
    mQueue.postEventWithDelay(mBufferingEvent, 1000000ll);
}

void AwesomePlayer::postCheckAudioStatusEvent_l() {
    if (mAudioStatusEventPending) {
        return;
    }
    mAudioStatusEventPending = true;
    mQueue.postEvent(mCheckAudioStatusEvent);
}

void AwesomePlayer::onCheckAudioStatus() {
    Mutex::Autolock autoLock(mLock);
    if (!mAudioStatusEventPending) {
        // Event was dispatched and while we were blocking on the mutex,
        // has already been cancelled.
        return;
    }

    mAudioStatusEventPending = false;
    if (mAudioPlayer != NULL)
    {
        if (mWatchForAudioSeekComplete && !mAudioPlayer->isSeeking()) {
            mWatchForAudioSeekComplete = false;

            if (!mSeekNotificationSent) {
                notifyListener_l(MEDIA_SEEK_COMPLETE);
                mSeekNotificationSent = true;
            }
            mSeeking = false;
        }

        status_t finalStatus;
        if (mWatchForAudioEOS && mAudioPlayer->reachedEOS(&finalStatus)) {
            mWatchForAudioEOS = false;
            mFlags |= AUDIO_AT_EOS;
            mFlags |= FIRST_FRAME;
            postStreamDoneEvent_l(finalStatus);
        }
    }
}

status_t AwesomePlayer::prepare() {
    Mutex::Autolock autoLock(mLock);
    return prepare_l();
}

status_t AwesomePlayer::prepare_l() {
    if (mFlags & PREPARED) {
        return OK;
    }

    if (mFlags & PREPARING) {
        return UNKNOWN_ERROR;
    }

    mIsAsyncPrepare = false;
    status_t err = prepareAsync_l();

    if (err != OK) {
        return err;
    }

    while (mFlags & PREPARING) {
        mPreparedCondition.wait(mLock);
    }

    return mPrepareResult;
}

status_t AwesomePlayer::prepareAsync() {
    Mutex::Autolock autoLock(mLock);

    if (mFlags & PREPARING) {
        return UNKNOWN_ERROR;  // async prepare already pending
    }

    mIsAsyncPrepare = true;
    return prepareAsync_l();
}

status_t AwesomePlayer::prepareAsync_l() {
    if (mFlags & PREPARING) {
        return UNKNOWN_ERROR;  // async prepare already pending
    }

    if (!mQueueStarted) {
        mQueue.start();
        mQueueStarted = true;
    }

    mFlags |= PREPARING;
    mAsyncPrepareEvent = new AwesomeEvent(
            this, &AwesomePlayer::onPrepareAsyncEvent);

    mQueue.postEvent(mAsyncPrepareEvent);

    return OK;
}

status_t AwesomePlayer::finishSetDataSource_l() {
    sp<DataSource> dataSource;

    if (!strncasecmp("http://", mUri.string(), 7)) {
        mConnectingDataSource = new NuHTTPDataSource;

        mLock.unlock();
        status_t err = mConnectingDataSource->connect(mUri, &mUriHeaders);
        mLock.lock();

        if (err != OK) {
            mConnectingDataSource.clear();

            LOGI("mConnectingDataSource->connect() returned %d", err);
            return err;
        }

#if 0
        mCachedSource = new NuCachedSource2(
                new ThrottledSource(
                    mConnectingDataSource, 50 * 1024 /* bytes/sec */));
#else
        mCachedSource = new NuCachedSource2(mConnectingDataSource);
#endif
        mConnectingDataSource.clear();

        dataSource = mCachedSource;

        // We're going to prefill the cache before trying to instantiate
        // the extractor below, as the latter is an operation that otherwise
        // could block on the datasource for a significant amount of time.
        // During that time we'd be unable to abort the preparation phase
        // without this prefill.

        mLock.unlock();

        for (;;) {
            bool eos;
            size_t cachedDataRemaining =
                mCachedSource->approxDataRemaining(&eos);

            if (eos || cachedDataRemaining >= kHighWaterMarkBytes
                    || (mFlags & PREPARE_CANCELLED)) {
                break;
            }

            usleep(200000);
        }

        mLock.lock();

        if (mFlags & PREPARE_CANCELLED) {
            LOGI("Prepare cancelled while waiting for initial cache fill.");
            return UNKNOWN_ERROR;
        }
    } else if (!strncasecmp(mUri.string(), "httplive://", 11)) {
        String8 uri("http://");
        uri.append(mUri.string() + 11);

        sp<LiveSource> liveSource = new LiveSource(uri.string());

        mCachedSource = new NuCachedSource2(liveSource);
        dataSource = mCachedSource;

        sp<MediaExtractor> extractor =
            MediaExtractor::Create(dataSource, MEDIA_MIMETYPE_CONTAINER_MPEG2TS);

        static_cast<MPEG2TSExtractor *>(extractor.get())
            ->setLiveSource(liveSource);

        return setDataSource_l(extractor);
    } else if (!strncmp("rtsp://gtalk/", mUri.string(), 13)) {
        if (mLooper == NULL) {
            mLooper = new ALooper;
            mLooper->setName("gtalk rtp");
            mLooper->start(
                    false /* runOnCallingThread */,
                    false /* canCallJava */,
                    PRIORITY_HIGHEST);
        }

        const char *startOfCodecString = &mUri.string()[13];
        const char *startOfSlash1 = strchr(startOfCodecString, '/');
        if (startOfSlash1 == NULL) {
            return BAD_VALUE;
        }
        const char *startOfWidthString = &startOfSlash1[1];
        const char *startOfSlash2 = strchr(startOfWidthString, '/');
        if (startOfSlash2 == NULL) {
            return BAD_VALUE;
        }
        const char *startOfHeightString = &startOfSlash2[1];

        String8 codecString(startOfCodecString, startOfSlash1 - startOfCodecString);
        String8 widthString(startOfWidthString, startOfSlash2 - startOfWidthString);
        String8 heightString(startOfHeightString);

#if 0
        mRTPPusher = new UDPPusher("/data/misc/rtpout.bin", 5434);
        mLooper->registerHandler(mRTPPusher);

        mRTCPPusher = new UDPPusher("/data/misc/rtcpout.bin", 5435);
        mLooper->registerHandler(mRTCPPusher);
#endif

        mRTPSession = new ARTPSession;
        mLooper->registerHandler(mRTPSession);

#if 0
        // My AMR SDP
        static const char *raw =
            "v=0\r\n"
            "o=- 64 233572944 IN IP4 127.0.0.0\r\n"
            "s=QuickTime\r\n"
            "t=0 0\r\n"
            "a=range:npt=0-315\r\n"
            "a=isma-compliance:2,2.0,2\r\n"
            "m=audio 5434 RTP/AVP 97\r\n"
            "c=IN IP4 127.0.0.1\r\n"
            "b=AS:30\r\n"
            "a=rtpmap:97 AMR/8000/1\r\n"
            "a=fmtp:97 octet-align\r\n";
#elif 1
        String8 sdp;
        sdp.appendFormat(
            "v=0\r\n"
            "o=- 64 233572944 IN IP4 127.0.0.0\r\n"
            "s=QuickTime\r\n"
            "t=0 0\r\n"
            "a=range:npt=0-315\r\n"
            "a=isma-compliance:2,2.0,2\r\n"
            "m=video 5434 RTP/AVP 97\r\n"
            "c=IN IP4 127.0.0.1\r\n"
            "b=AS:30\r\n"
            "a=rtpmap:97 %s/90000\r\n"
            "a=cliprect:0,0,%s,%s\r\n"
            "a=framesize:97 %s-%s\r\n",

            codecString.string(),
            heightString.string(), widthString.string(),
            widthString.string(), heightString.string()
            );
        const char *raw = sdp.string();

#endif

        sp<ASessionDescription> desc = new ASessionDescription;
        CHECK(desc->setTo(raw, strlen(raw)));

        CHECK_EQ(mRTPSession->setup(desc), (status_t)OK);

        if (mRTPPusher != NULL) {
            mRTPPusher->start();
        }

        if (mRTCPPusher != NULL) {
            mRTCPPusher->start();
        }

        CHECK_EQ(mRTPSession->countTracks(), 1u);
        sp<MediaSource> source = mRTPSession->trackAt(0);

#if 0
        bool eos;
        while (((APacketSource *)source.get())
                ->getQueuedDuration(&eos) < 5000000ll && !eos) {
            usleep(100000ll);
        }
#endif

        const char *mime;
        CHECK(source->getFormat()->findCString(kKeyMIMEType, &mime));

        if (!strncasecmp("video/", mime, 6)) {
            setVideoSource(source);
        } else {
            CHECK(!strncasecmp("audio/", mime, 6));
            setAudioSource(source);
        }

        mExtractorFlags = MediaExtractor::CAN_PAUSE;

        return OK;
    } else if (!strncasecmp("rtsp://", mUri.string(), 7)) {
        if (mLooper == NULL) {
            mLooper = new ALooper;
            mLooper->setName("rtsp");
            mLooper->start();
        }
        mRTSPController = new ARTSPController(mLooper);
        status_t err = mRTSPController->connect(mUri.string());

        LOGI("ARTSPController::connect returned %d", err);

        if (err != OK) {
            mRTSPController.clear();
            return err;
        }

        sp<MediaExtractor> extractor = mRTSPController.get();
        return setDataSource_l(extractor);
    } else {
        dataSource = DataSource::CreateFromURI(mUri.string(), &mUriHeaders);
    }

    if (dataSource == NULL) {
        return UNKNOWN_ERROR;
    }

    sp<MediaExtractor> extractor = MediaExtractor::Create(dataSource);

    if (extractor == NULL) {
        return UNKNOWN_ERROR;
    }

    return setDataSource_l(extractor);
}

void AwesomePlayer::abortPrepare(status_t err) {
    CHECK(err != OK);

    if (mIsAsyncPrepare) {
        notifyListener_l(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, err);
    }

    mPrepareResult = err;
    mFlags &= ~(PREPARING|PREPARE_CANCELLED);
    mAsyncPrepareEvent = NULL;
    mPreparedCondition.broadcast();
}

// static
bool AwesomePlayer::ContinuePreparation(void *cookie) {
    AwesomePlayer *me = static_cast<AwesomePlayer *>(cookie);

    return (me->mFlags & PREPARE_CANCELLED) == 0;
}

void AwesomePlayer::onPrepareAsyncEvent() {
    Mutex::Autolock autoLock(mLock);

    if (mFlags & PREPARE_CANCELLED) {
        LOGI("prepare was cancelled before doing anything");
        abortPrepare(UNKNOWN_ERROR);
        return;
    }

    if (mUri.size() > 0) {
        status_t err = finishSetDataSource_l();

        if (err != OK) {
            abortPrepare(err);
            return;
        }
    }

    if (mVideoTrack != NULL && mVideoSource == NULL) {
        status_t err = initVideoDecoder();

        if (err != OK) {
            abortPrepare(err);
            return;
        }
    }

    if (mAudioTrack != NULL && mAudioSource == NULL) {
        status_t err = initAudioDecoder();

        if (err != OK) {
            abortPrepare(err);
            return;
        }
    }

    if (mCachedSource != NULL || mRTSPController != NULL) {

        //Set the max interleaving offset for HTTP Caching source.
        if (mCachedSource != NULL && mVideoSource != NULL && mAudioSource != NULL) {
            int64_t bitrate = 0;
            if( getBitrate( &bitrate ) ) {
                //considering that Audio Video can be apart by 0.3 secs
                mCachedSource->setAVInterleavingOffset( bitrate/3 );
            }
        }

        postBufferingEvent_l();
    } else {
        finishAsyncPrepare_l();
    }
}

void AwesomePlayer::finishAsyncPrepare_l() {
    if (mIsAsyncPrepare) {
        if (mVideoWidth < 0 || mVideoHeight < 0) {
            notifyListener_l(MEDIA_SET_VIDEO_SIZE, 0, 0);
        } else {
            int32_t rotationDegrees;
            if (!mVideoTrack->getFormat()->findInt32(
                        kKeyRotation, &rotationDegrees)) {
                rotationDegrees = 0;
            }

#if 1
            if (rotationDegrees == 90 || rotationDegrees == 270) {
                notifyListener_l(
                        MEDIA_SET_VIDEO_SIZE, mVideoHeight, mVideoWidth);
            } else
#endif
            {
                notifyListener_l(
                        MEDIA_SET_VIDEO_SIZE, mVideoWidth, mVideoHeight);
            }
        }

        notifyListener_l(MEDIA_PREPARED);
    }

    mPrepareResult = OK;
    mFlags &= ~(PREPARING|PREPARE_CANCELLED);
    mFlags |= PREPARED;
    mAsyncPrepareEvent = NULL;
    mPreparedCondition.broadcast();
}

status_t AwesomePlayer::suspend() {
    LOGV("suspend");
    Mutex::Autolock autoLock(mLock);

    if (mSuspensionState != NULL) {
        //This condition is required only for clips with Video
        if (mVideoSource != NULL && mVideoBuffer[mVideoQueueLastRendered] == NULL) {
            //go into here if video is suspended again
            //after resuming without being played between
            //them
            SuspensionState *state = mSuspensionState;
            mSuspensionState = NULL;
            reset_l();
            mSuspensionState = state;
            return OK;
        }

        delete mSuspensionState;
        mSuspensionState = NULL;
    }

    if (mFlags & PREPARING) {
        mFlags |= PREPARE_CANCELLED;
        if (mConnectingDataSource != NULL) {
            LOGI("interrupting the connection process");
            mConnectingDataSource->disconnect();
        }
    }

    while (mFlags & PREPARING) {
        mPreparedCondition.wait(mLock);
    }

    SuspensionState *state = new SuspensionState;
    state->mUri = mUri;
    state->mUriHeaders = mUriHeaders;
    state->mFileSource = mFileSource;
    if( mFileSource != NULL ) {
       state->mMediaExtractor = mMediaExtractor;
    }

    state->mFlags = mFlags & (PLAYING | AUTO_LOOPING | LOOPING | AT_EOS);
    getPosition(&state->mPositionUs);

    if (mVideoBuffer[mVideoQueueLastRendered] != NULL) {
        size_t size = mVideoBuffer[mVideoQueueLastRendered]->range_length();
        if (size) {
            int32_t unreadable;
            if (!mVideoBuffer[mVideoQueueLastRendered]->meta_data()->findInt32(
                        kKeyIsUnreadable, &unreadable)
                    || unreadable == 0) {
                state->mLastVideoFrameSize = size;
                state->mLastVideoFrame = malloc(size);
                memcpy(state->mLastVideoFrame,
                       (const uint8_t *)mVideoBuffer[mVideoQueueLastRendered]->data()
                            + mVideoBuffer[mVideoQueueLastRendered]->range_offset(),
                       size);

                state->mVideoWidth = mVideoWidth;
                state->mVideoHeight = mVideoHeight;

                sp<MetaData> meta = mVideoSource->getFormat();
                CHECK(meta->findInt32(kKeyColorFormat, &state->mColorFormat));
                //Update the decode width and height using stride and slice
                //height key value pair in metadata. Software decoder does not
                //use stride and slice height. Update decode width and height
                //to width and height if the stride or slice height returned
                //from decoder is zero.
                if(!(meta->findInt32(kKeyStride, &state->mDecodedWidth)))
                    state->mDecodedWidth = mVideoWidth;
                if(!(meta->findInt32(kKeySliceHeight, &state->mDecodedHeight)))
                    state->mDecodedHeight = mVideoHeight;

            } else {
                LOGV("Unable to save last video frame, we have no access to "
                     "the decoded video data.");
            }
        }
    }

    int32_t rotationDegrees = 0;
    if (mVideoTrack != NULL &&
        !mVideoTrack->getFormat()->findInt32(
                                             kKeyRotation, &rotationDegrees)) {
        rotationDegrees = 0;
    }
    state->mRotation = rotationDegrees;

    reset_l();

    mSuspensionState = state;

    return OK;
}

status_t AwesomePlayer::resume() {
    LOGV("resume");
    Mutex::Autolock autoLock(mLock);

    if (mSuspensionState == NULL) {
        return INVALID_OPERATION;
    }

    SuspensionState *state = mSuspensionState;
    mSuspensionState = NULL;

    status_t err;

    //Need to set codec and source flags before
    //setDataSource() or play()

    //Check if the video was known to be 3D, if so
    //tell decoder to 3D format (decoder might not
    //recogize 3D sometimes if clip is played from
    //middle as is case in suspend/resume)
    uint32_t colorFormat = state->mColorFormat;
    int format3D;
    GET_3D_FORMAT(colorFormat, format3D);
    switch (format3D)
    {
        case QOMX_3D_TOP_BOTTOM_VIDEO_FLAG:
            mCodecFlags |= OMXCodec::kForce3DTopBottom;
            break;
        case QOMX_3D_LEFT_RIGHT_VIDEO_FLAG:
            mCodecFlags |= OMXCodec::kForce3DLeftRight;
            break;
        case QOMX_3D_RIGHT_LEFT_VIDEO_FLAG:
            mCodecFlags |= OMXCodec::kForce3DRightLeft;
            break;
        case QOMX_3D_BOTTOM_TOP_VIDEO_FLAG:
            mCodecFlags |= OMXCodec::kForce3DBottomTop;
            break;
        default:
            //not a 3D colorformat; move along
            break;
    }

    if (state->mFileSource != NULL) {
        // Set the extractor before calling setDataSource
        if ( state->mMediaExtractor != NULL ) {
           mMediaExtractor = state->mMediaExtractor;
        }
        err = setDataSource_l(state->mFileSource);

        if (err == OK) {
            mFileSource = state->mFileSource;
        }
    } else {
        err = setDataSource_l(state->mUri, &state->mUriHeaders);
    }

    if (err != OK) {
        delete state;
        state = NULL;

        return err;
    }

    seekTo_l(state->mPositionUs);

    mFlags = state->mFlags & (AUTO_LOOPING | LOOPING | AT_EOS);

    if (state->mLastVideoFrame && mISurface != NULL) {
        mVideoRenderer =
            new AwesomeLocalRenderer(
                    true,  // previewOnly
                    "",
                    (OMX_COLOR_FORMATTYPE)state->mColorFormat,
                    mISurface,
                    state->mVideoWidth,
                    state->mVideoHeight,
                    state->mDecodedWidth,
                    state->mDecodedHeight,
                    state->mRotation);

        mVideoRendererIsPreview = true;

        ((AwesomeLocalRenderer *)mVideoRenderer.get())->render(
                state->mLastVideoFrame, state->mLastVideoFrameSize);
    }

    if (state->mFlags & PLAYING) {
        play_l();
    }

    mSuspensionState = state;
    state = NULL;

    return OK;
}

uint32_t AwesomePlayer::flags() const {
    return mExtractorFlags;
}

void AwesomePlayer::postAudioEOS() {
    postCheckAudioStatusEvent_l();
}

void AwesomePlayer::postAudioSeekComplete() {
    postCheckAudioStatusEvent_l();
}

void AwesomePlayer::setNumFramesToHold() {
    sp<MetaData> meta = mVideoSource->getFormat();
    const char *component;
    CHECK(meta->findCString(kKeyDecoderComponent, &component));
    if (!strncmp("OMX.", component, 4)) {
        // Set number of frames to hold to 2 for qcom decoders to 
        // avoid flicker
        mNumFramesToHold = 2;
    } else {
        // Hold 1 frame for software decoders
        mNumFramesToHold = 1;
    }
    LOGV("Set number of frames to hold = %d \n", mNumFramesToHold);
}

// Trim both leading and trailing whitespace from the given string.
static void TrimString(String8 *s) {
    size_t num_bytes = s->bytes();
    const char *data = s->string();

    size_t leading_space = 0;
    while (leading_space < num_bytes && isspace(data[leading_space])) {
        ++leading_space;
    }

    size_t i = num_bytes;
    while (i > leading_space && isspace(data[i - 1])) {
        --i;
    }

    s->setTo(String8(&data[leading_space], i - leading_space));
}

status_t AwesomePlayer::setParameter(const String8& key, const String8& value) {
    if (key == "gpu-composition") {
        LOGV("setParameter : gpu-composition : key = %s value = %s\n", key.string(), value.string());
        int enableGPU = 0;
        enableGPU = atoi(value.string());
        LOGV("setParameter : gpu-composition : %d \n", enableGPU);
        if (enableGPU) {
            mCodecFlags |= OMXCodec::kEnableGPUComposition;
            LOGV("GPU composition flag set");
        }
    }
    return OK;
}

status_t AwesomePlayer::setParameters(const String8& params) {
    LOGV("setParameters(%s)", params.string());
    status_t ret = OK;
    const char *key_start = params;
    for (;;) {
        const char *equal_pos = strchr(key_start, '=');
        if (equal_pos == NULL) {
            // This key is missing a value.
            ret = UNKNOWN_ERROR;
            break;
        }

        String8 key(key_start, equal_pos - key_start);
        TrimString(&key);

        if (key.length() == 0) {
            ret = UNKNOWN_ERROR;
            break;
        }
        LOGV("setParameters key = %s\n", key.string());

        const char *value_start = equal_pos + 1;
        const char *semicolon_pos = strchr(value_start, ';');
        String8 value;
        if (semicolon_pos == NULL) {
            value.setTo(value_start);
            LOGV("setParameters value = %s\n", value.string());
        } else {
            value.setTo(value_start, semicolon_pos - value_start);
            LOGV("setParameters semicolon value = %s\n", value.string());
        }

        ret = setParameter(key, value);

        if (ret != OK) {
           LOGE("setParameter(%s = %s) failed with result %d",
                 key.string(), value.string(), ret);
           break;
        }

        if (semicolon_pos == NULL) {
            break;
        }

        key_start = semicolon_pos + 1;
    }

    if (ret != OK) {
        LOGE("Ln %d setParameters(\"%s\") error", __LINE__, params.string());
    }

    return ret;
}

//Statistics profiling
void AwesomePlayer::logStatistics() {
    const char *mime;
    mVideoTrack->getFormat()->findCString(kKeyMIMEType, &mime);

    LOGW("=====================================================");
    if (mFlags & LOOPING) {LOGW("Looping Update");}
    LOGW("Mime Type: %s",mime);
    LOGW("Clip duration: %lld ms",mDurationUs/1000);
    LOGW("Number of frames dropped: %u",mFramesDropped);
    LOGW("Number of frames rendered: %u",mTotalFrames);
    LOGW("=====================================================");
}

inline void AwesomePlayer::logFirstFrame() {
    LOGW("=====================================================");
    LOGW("First frame latency: %lld ms",(getTimeOfDayUs()-mFirstFrameLatencyStartUs)/1000);
    LOGW("=====================================================");
    mVeryFirstFrame = false;
}

inline void AwesomePlayer::logSeek() {
    LOGW("=====================================================");
    LOGW("Seek position: %lld ms",mSeekTimeUs/1000);
    LOGW("Seek latency: %lld ms",(getTimeOfDayUs()-mFirstFrameLatencyStartUs)/1000);
    LOGW("=====================================================");
}

inline void AwesomePlayer::logPause() {
    LOGW("=====================================================");
    LOGW("Pause position: %lld ms",mVideoTimeUs/1000);
    LOGW("=====================================================");
}

inline void AwesomePlayer::logCatchUp(int64_t ts, int64_t clock, int64_t delta)
{
    if (mConsecutiveFramesDropped > 0) {
        mNumTimesSyncLoss++;
        if (mMaxTimeSyncLoss < (clock - mCatchupTimeStart) && clock > 0 && ts > 0) {
            mMaxTimeSyncLoss = clock - mCatchupTimeStart;
        }
    }
}

inline void AwesomePlayer::logLate(int64_t ts, int64_t clock, int64_t delta)
{
    if (mMaxLateDelta < delta && clock > 0 && ts > 0) {
        mMaxLateDelta = delta;
    }
}

inline void AwesomePlayer::logOnTime(int64_t ts, int64_t clock, int64_t delta)
{
    logCatchUp(ts, clock, delta);
    if (delta <= 0) {
        if ((-delta) > (-mMaxEarlyDelta) && clock > 0 && ts > 0) {
            mMaxEarlyDelta = delta;
        }
    }
    else logLate(ts, clock, delta);
}

void AwesomePlayer::logSyncLoss()
{
    LOGW("=====================================================");
    LOGW("Number of times AV Sync Losses = %u", mNumTimesSyncLoss);
    LOGW("Max Video Ahead time delta = %u", -mMaxEarlyDelta/1000);
    LOGW("Max Video Behind time delta = %u", mMaxLateDelta/1000);
    LOGW("Max Time sync loss = %u",mMaxTimeSyncLoss/1000);
    LOGW("=====================================================");

}

inline int64_t AwesomePlayer::getTimeOfDayUs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

#ifdef YUVCLIENT
status_t AwesomePlayer::registerFrameBufferHeap(const sp<IMemoryHeap> memoryHeap, int numFrames, int frameSize) {
    if (mFrameBuffers == NULL) {
        mFrameBuffers = memoryHeap;
        mNumFrames = numFrames;
        mFrameSize = frameSize;
        return OK;
    }

    return ALREADY_EXISTS;
}

status_t AwesomePlayer::unregisterFrameBufferHeap() {
    if (mFrameBuffers != NULL)
        resetFrameBuffers();
    return OK;
}

status_t AwesomePlayer::queueFrameBuffer(int frame) {
    Mutex::Autolock autoLock(mFrameBufferListLock);
    mFrameBufferList.push(frame);
    return OK;
}

status_t AwesomePlayer::queryBufferFormat(int *format) {
    if (mVideoSource == NULL)
        mColorFormat = 0;
    else {
        sp<MetaData> meta = mVideoSource->getFormat();
        CHECK(meta->findInt32(kKeyColorFormat, &mColorFormat));
        mDecodedWidth = mVideoWidth;
        mDecodedHeight = mVideoHeight;
    }
    *format = mColorFormat;
    return OK;
}

void* AwesomePlayer::getFrameBufferAddress(int index) {
    if (mFrameBuffers == NULL)
        return NULL;

    uint8_t* p = static_cast<uint8_t*>(mFrameBuffers->getBase());
    p += mFrameSize * index;
    return static_cast<void*>(p);
}

int AwesomePlayer::getAvailableFrameBuffer() {
    Mutex::Autolock autoLock(mFrameBufferListLock);

    if ((mFrameBuffers == NULL) || (mFrameBufferList.isEmpty()))
        return -1;

    int index = mFrameBufferList[0];
    mFrameBufferList.removeAt(0);

    return index;
}

void AwesomePlayer::sendBackFrameBuffers() {
    if (mFrameBuffers == NULL)
        return;

    int index;
    Mutex::Autolock autoLock(mFrameBufferListLock);

    while (!mFrameBufferList.isEmpty()) {
        index = mFrameBufferList[0];
        mFrameBufferList.removeAt(0);
        notifyListener_l(MEDIA_BUFFER_READY, index, 0);
    }
}

void AwesomePlayer::resetFrameBuffers() {
    Mutex::Autolock autoLock(mFrameBufferListLock);
    mFrameBuffers.clear();
    mNumFrames = 0;
    mFrameSize = 0;
    mDecodedWidth = 0;
    mDecodedHeight = 0;
    mFormatChanged = false;
    mFrameBufferList.clear();
}
#endif

}  // namespace android

