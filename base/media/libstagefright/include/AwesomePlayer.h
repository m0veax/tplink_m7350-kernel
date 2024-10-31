/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
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

#ifndef AWESOME_PLAYER_H_

#define AWESOME_PLAYER_H_

#include "NuHTTPDataSource.h"
#include "TimedEventQueue.h"

#ifdef YUVCLIENT
#include <binder/IMemory.h>
#endif
#include <media/MediaPlayerInterface.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/OMXClient.h>
#include <media/stagefright/TimeSource.h>
#include <media/stagefright/MediaExtractor.h>
#include <utils/threads.h>
#define BUFFER_QUEUE_CAPACITY 3

namespace android {

struct AudioPlayer;
struct DataSource;
struct MediaBuffer;
struct MediaExtractor;
struct MediaSource;
struct NuCachedSource2;

struct ALooper;
struct ARTSPController;
struct ARTPSession;
struct UDPPusher;

struct AwesomeRenderer : public RefBase {
    AwesomeRenderer() {}

    virtual status_t initCheck() const = 0;
    virtual void render(MediaBuffer *buffer) = 0;

private:
    AwesomeRenderer(const AwesomeRenderer &);
    AwesomeRenderer &operator=(const AwesomeRenderer &);
};

struct AwesomePlayer {
    AwesomePlayer();
    ~AwesomePlayer();

    void setListener(const wp<MediaPlayerBase> &listener);

    status_t setDataSource(
            const char *uri,
            const KeyedVector<String8, String8> *headers = NULL);

    status_t setDataSource(int fd, int64_t offset, int64_t length);

    void reset();

    status_t prepare();
    status_t prepare_l();
    status_t prepareAsync();
    status_t prepareAsync_l();

    status_t play();
    status_t pause();

    bool isPlaying() const;

    void setISurface(const sp<ISurface> &isurface);
    void setAudioSink(const sp<MediaPlayerBase::AudioSink> &audioSink);
    status_t setLooping(bool shouldLoop);

    status_t getDuration(int64_t *durationUs);
    status_t getPosition(int64_t *positionUs);

    status_t seekTo(int64_t timeUs);

    status_t getVideoDimensions(int32_t *width, int32_t *height) const;

    status_t suspend();
    status_t resume();

    // This is a mask of MediaExtractor::Flags.
    uint32_t flags() const;

    void postAudioEOS();
    void postAudioSeekComplete();

    status_t setParameters(const String8& params);

#ifdef YUVCLIENT
    status_t registerFrameBufferHeap(const sp<IMemoryHeap> memoryHeap, int numFrames, int frameSize);
    status_t unregisterFrameBufferHeap();
    status_t queueFrameBuffer(int frame);
    status_t queryBufferFormat(int *format);
#endif

private:
    friend struct AwesomeEvent;

    enum {
        PLAYING             = 1,
        LOOPING             = 2,
        FIRST_FRAME         = 4,
        PREPARING           = 8,
        PREPARED            = 16,
        AT_EOS              = 32,
        PREPARE_CANCELLED   = 64,
        CACHE_UNDERRUN      = 128,
        AUDIO_AT_EOS        = 256,
        VIDEO_AT_EOS        = 512,
        AUTO_LOOPING        = 1024,
    };

    mutable Mutex mLock;
    Mutex mMiscStateLock;

    OMXClient mClient;
    TimedEventQueue mQueue;
    bool mQueueStarted;
    wp<MediaPlayerBase> mListener;

    sp<ISurface> mISurface;
    sp<MediaPlayerBase::AudioSink> mAudioSink;

    SystemTimeSource mSystemTimeSource;
    TimeSource *mTimeSource;

    String8 mUri;
    KeyedVector<String8, String8> mUriHeaders;

    sp<DataSource> mFileSource;
    sp<MediaExtractor> mMediaExtractor;

    sp<MediaSource> mVideoTrack;
    sp<MediaSource> mVideoSource;
    sp<AwesomeRenderer> mVideoRenderer;
    bool mVideoRendererIsPreview;

    sp<MediaSource> mAudioTrack;
    sp<MediaSource> mAudioSource;
    AudioPlayer *mAudioPlayer;
    int64_t mDurationUs, mVideoDurationUs, mAudioDurationUs;

    uint32_t mFlags;
    uint32_t mExtractorFlags;
    uint32_t mCodecFlags;

    int32_t mVideoWidth, mVideoHeight;
    int64_t mTimeSourceDeltaUs;
    int64_t mVideoTimeUs;

    bool mSeeking;
    bool mSeekNotificationSent;
    int64_t mSeekTimeUs;

    int64_t mBitrate;  // total bitrate of the file (in bps) or -1 if unknown.

    bool mWatchForAudioSeekComplete;
    bool mWatchForAudioEOS;

    sp<TimedEventQueue::Event> mVideoEvent;
    bool mVideoEventPending;
    sp<TimedEventQueue::Event> mStreamDoneEvent;
    bool mStreamDoneEventPending;
    sp<TimedEventQueue::Event> mBufferingEvent;
    bool mBufferingEventPending;
    sp<TimedEventQueue::Event> mCheckAudioStatusEvent;
    bool mAudioStatusEventPending;

    sp<TimedEventQueue::Event> mAsyncPrepareEvent;
    Condition mPreparedCondition;
    bool mIsAsyncPrepare;
    status_t mPrepareResult;
    status_t mStreamDoneStatus;

    void postVideoEvent_l(int64_t delayUs = -1);
    void postBufferingEvent_l();
    void postStreamDoneEvent_l(status_t status);
    void postCheckAudioStatusEvent_l();
    status_t play_l();

    MediaBuffer **mVideoBuffer;
    int32_t mVideoQueueFront;
    int32_t mVideoQueueBack;
    int32_t mVideoQueueLastRendered;
    int32_t mVideoQueueSize;
    int32_t mNumFramesToHold;

    sp<NuHTTPDataSource> mConnectingDataSource;
    sp<NuCachedSource2> mCachedSource;

    sp<ALooper> mLooper;
    sp<ARTSPController> mRTSPController;
    sp<ARTPSession> mRTPSession;
    sp<UDPPusher> mRTPPusher, mRTCPPusher;

    struct SuspensionState {
        String8 mUri;
        KeyedVector<String8, String8> mUriHeaders;
        sp<DataSource> mFileSource;
        sp<MediaExtractor> mMediaExtractor;

        uint32_t mFlags;
        int64_t mPositionUs;

        void *mLastVideoFrame;
        size_t mLastVideoFrameSize;
        int32_t mColorFormat;
        int32_t mVideoWidth, mVideoHeight;
        int32_t mDecodedWidth, mDecodedHeight;
        int32_t mRotation;

        SuspensionState()
            : mLastVideoFrame(NULL) {
        }

        ~SuspensionState() {
            if (mLastVideoFrame) {
                free(mLastVideoFrame);
                mLastVideoFrame = NULL;
            }
        }
    } *mSuspensionState;

    status_t setDataSource_l(
            const char *uri,
            const KeyedVector<String8, String8> *headers = NULL);

    status_t setDataSource_l(const sp<DataSource> &dataSource);
    status_t setDataSource_l(const sp<MediaExtractor> &extractor);
    void reset_l();
    void partial_reset_l();
    status_t seekTo_l(int64_t timeUs);
    status_t pause_l(bool at_eos = false);
    status_t initRenderer_l();
    void seekAudioIfNecessary_l();

    void cancelPlayerEvents(bool keepBufferingGoing = false);

    void setAudioSource(sp<MediaSource> source);
    status_t initAudioDecoder();

    void setVideoSource(sp<MediaSource> source);
    status_t initVideoDecoder(uint32_t flags = 0);

    void onStreamDone();

    void notifyListener_l(int msg, int ext1 = 0, int ext2 = 0);

    void onVideoEvent();
    void onBufferingUpdate();
    void onCheckAudioStatus();
    void onPrepareAsyncEvent();
    void abortPrepare(status_t err);
    void finishAsyncPrepare_l();

    bool getCachedDuration_l(int64_t *durationUs, bool *eos);

    status_t finishSetDataSource_l();

    static bool ContinuePreparation(void *cookie);
    void setNumFramesToHold();

    static void OnRTSPSeekDoneWrapper(void *cookie);
    void onRTSPSeekDone();

    bool getBitrate(int64_t *bitrate);

    void finishSeekIfNecessary(int64_t videoTimeUs);

    status_t setParameter(const String8& key, const String8& value);

    AwesomePlayer(const AwesomePlayer &);
    AwesomePlayer &operator=(const AwesomePlayer &);
    void releaseAllVideoBuffersHeld();

    //Statistics profiling
    uint32_t mFramesDropped;
    uint32_t mConsecutiveFramesDropped;
    uint32_t mCatchupTimeStart;
    uint32_t mNumTimesSyncLoss;
    uint32_t mMaxEarlyDelta;
    uint32_t mMaxLateDelta;
    uint32_t mMaxTimeSyncLoss;
    uint32_t mTotalFrames;
    int64_t mFirstFrameLatencyStartUs; //first frame latency start
    bool mVeryFirstFrame;
    bool mStatistics;

    void logStatistics();
    void logFirstFrame();
    void logSeek();
    void logPause();
    void logCatchUp(int64_t ts, int64_t clock, int64_t delta);
    void logLate(int64_t ts, int64_t clock, int64_t delta);
    void logOnTime(int64_t ts, int64_t clock, int64_t delta);
    void logSyncLoss();
    int64_t getTimeOfDayUs();

#ifdef YUVCLIENT
    int mColorFormat;
    sp<IMemoryHeap> mFrameBuffers; // shared memory region for frame buffers
    int mNumFrames;
    int mFrameSize;
    int mDecodedWidth;
    int mDecodedHeight;
    bool mFormatChanged; // return all frames to client
    Vector<int> mFrameBufferList; // buffer index queue-return vector
    Mutex mFrameBufferListLock;

    void* getFrameBufferAddress(int index);
    int getAvailableFrameBuffer();
    void sendBackFrameBuffers();
    void resetFrameBuffers();
#endif
};

}  // namespace android

#endif  // AWESOME_PLAYER_H_

