/*
 * Copyright (C) 2010 The Android Open Source Project
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
#define LOG_TAG "AudioSource"
#include <utils/Log.h>

#include <media/stagefright/AudioSource.h>

#include <media/AudioRecord.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <cutils/properties.h>
#include <stdlib.h>

#define AMR_FRAMESIZE 32
#define QCELP_FRAMESIZE 35
#define EVRC_FRAMESIZE 23

// AAC silence frame size which includes size of the frame also
#define AAC_MONO_SILENCE_FRAME_SIZE         12 // this includes the length of silence frame
#define AAC_STEREO_SILENCE_FRAME_SIZE       13 // this includes the length of silence frame
#define SILENCE_INSERTION_TIME_PERIOD_USEC  800000
#define AMR_NB_FRAME_DURATION_USEC          20000 // One AMR-NB frame = 20msec

//AMR-NB silence frame size
#define AMR_NB_SILENCE_FRAME_SIZE 32

//AMR-NB silence frame with homing frame data
static const uint8_t  AMR_NB_SILENCE_FRAME[] = {
    0x3C,0x54,0x08,0x96,0xdb,0xad,0xaa,0x00,0x60,0x00,0x00,0x1b,0x00,0x7f,0x58,0x83,
    0x66,0x40,0x79,0x04,0x90,0x85,0x15,0x10,0x4f,0xb0,0xf6,0x03,0x24,0xea,0xc7,0x00
};
//AAC silence frame data
// First two bytes indicate the length of silence frame
static const uint8_t  AAC_MONO_SILENCE_FRAME_WITH_SIZE[] = {
    0x0A, 0x00, 0x01, 0x40, 0x20, 0x06, 0x4F, 0xDE, 0x02, 0x70, 0x0C, 0x1C
};
static const uint8_t  AAC_STEREO_SILENCE_FRAME_WITH_SIZE[] = {
    0x0B, 0x00, 0x21, 0x10, 0x05, 0x00, 0xA0, 0x19, 0x33, 0x87, 0xC0, 0x00, 0x7E
};

namespace android {

AudioSource::AudioSource(
        int inputSource, uint32_t sampleRate, uint32_t channels)
    : mStarted(false),
      mCollectStats(false),
      mPrevSampleTimeUs(0),
      mTotalLostFrames(0),
      mPrevLostBytes(0),
      mGroup(NULL),
      mFormat(AudioSystem::PCM_16_BIT),
      mMime(MEDIA_MIMETYPE_AUDIO_RAW),
      mFirstFrame(false) {

    LOGV("sampleRate: %d, channels: %d", sampleRate, channels);
    CHECK(channels == 1 || channels == 2);
    uint32_t flags = AudioRecord::RECORD_AGC_ENABLE |
                     AudioRecord::RECORD_NS_ENABLE  |
                     AudioRecord::RECORD_IIR_ENABLE;

    if ( NO_ERROR != AudioSystem::getInputBufferSize(
        sampleRate, mFormat, channels, (size_t*)&mMaxBufferSize) ) {
        mMaxBufferSize = kMaxBufferSize;
        LOGV("mMaxBufferSize = %d", mMaxBufferSize);
    }

    mNumChannels   = channels;
    mRecord = new AudioRecord(
                inputSource, sampleRate, AudioSystem::PCM_16_BIT,
                channels > 1? AudioSystem::CHANNEL_IN_STEREO: AudioSystem::CHANNEL_IN_MONO,
                4 * mMaxBufferSize / (sizeof(int16_t) * channels), /* Enable ping-pong buffers */
                flags);

    mInitCheck = mRecord->initCheck();
}

AudioSource::AudioSource( int inputSource, const sp<MetaData>& meta )
    : mStarted(false),
      mCollectStats(false),
      mPrevSampleTimeUs(0),
      mTotalLostFrames(0),
      mPrevLostBytes(0),
      mGroup(NULL),
      mFirstFrame(false) {

    const char * mime;
    CHECK( meta->findCString( kKeyMIMEType, &mime ) );

    mMime = mime;
    int32_t sampleRate = 0; //these are the only supported values
    int32_t channels = 0;      //for the below tunnel formats

    CHECK( meta->findInt32( kKeyChannelCount, &channels ) );
    CHECK( meta->findInt32( kKeySampleRate, &sampleRate ) );

    int32_t frameSize = -1;

    mNumChannels = channels;
    if ( !strcasecmp( mime, MEDIA_MIMETYPE_AUDIO_AMR_NB ) ) {
        mFormat = AudioSystem::AMR_NB;
        frameSize = AMR_FRAMESIZE;
        mMaxBufferSize = AMR_FRAMESIZE*10;
    }
    else if ( !strcasecmp( mime, MEDIA_MIMETYPE_AUDIO_QCELP ) ) {
        mFormat = AudioSystem::QCELP;
        frameSize = 35;
        mMaxBufferSize = 350;
    }
    else if ( !strcasecmp( mime, MEDIA_MIMETYPE_AUDIO_EVRC ) ) {
        mFormat = AudioSystem::EVRC;
        frameSize = 23;
        mMaxBufferSize = 230;
    }
    else {
        CHECK(0);
    }

    CHECK(channels == 1 || channels == 2);
    uint32_t flags = AudioRecord::RECORD_AGC_ENABLE |
                     AudioRecord::RECORD_NS_ENABLE  |
                     AudioRecord::RECORD_IIR_ENABLE;

    mRecord = new AudioRecord(
                inputSource, sampleRate, mFormat,
                channels > 1? AudioSystem::CHANNEL_IN_STEREO:
                                  AudioSystem::CHANNEL_IN_MONO,
                4*mMaxBufferSize/channels/frameSize,
                flags);

    mInitCheck = mRecord->initCheck();
}

AudioSource::~AudioSource() {
    if (mStarted) {
        stop();
    }

    delete mRecord;
    mRecord = NULL;
}

status_t AudioSource::initCheck() const {
    return mInitCheck;
}

status_t AudioSource::start(MetaData *params) {
    if (mStarted) {
        return UNKNOWN_ERROR;
    }

    if (mInitCheck != OK) {
        return NO_INIT;
    }

    char value[PROPERTY_VALUE_MAX];
    if (property_get("media.stagefright.record-stats", value, NULL)
        && (!strcmp(value, "1") || !strcasecmp(value, "true"))) {
        mCollectStats = true;
    }

    mTrackMaxAmplitude = false;
    mMaxAmplitude = 0;
    mInitialReadTimeUs = 0;
    mStartTimeUs = 0;
    int64_t startTimeUs;
    if (params && params->findInt64(kKeyTime, &startTimeUs)) {
        mStartTimeUs = startTimeUs;
    }
    status_t err = mRecord->start();

    if (err == OK) {
        mGroup = new MediaBufferGroup;
        if (mFormat == AudioSystem::AMR_NB) {
            mGroup->add_buffer(new MediaBuffer(mMaxBufferSize/10));
        } else {
            mGroup->add_buffer(new MediaBuffer(mMaxBufferSize));
        }
        mStarted = true;
    }

    return err;
}

status_t AudioSource::stop() {
    if (!mStarted) {
        return UNKNOWN_ERROR;
    }

    if (mInitCheck != OK) {
        return NO_INIT;
    }

    mRecord->stop();

    delete mGroup;
    mGroup = NULL;

    mStarted = false;

    if (mCollectStats) {
        LOGI("Total lost audio frames: %lld",
            mTotalLostFrames + (mPrevLostBytes >> 1));
    }

    return OK;
}

sp<MetaData> AudioSource::getFormat() {
    if (mInitCheck != OK) {
        return 0;
    }

    sp<MetaData> meta = new MetaData;
    meta->setCString(kKeyMIMEType, mMime);
    meta->setInt32(kKeySampleRate, mRecord->getSampleRate());
    meta->setInt32(kKeyChannelCount, mRecord->channelCount());
    meta->setInt32(kKeyMaxInputSize, mMaxBufferSize);

    return meta;
}

/*
 * Returns -1 if frame skipping request is too long.
 * Returns  0 if there is no need to skip frames.
 * Returns  1 if we need to skip frames.
 */
static int skipFrame(int64_t timestampUs,
        const MediaSource::ReadOptions *options) {

    int64_t skipFrameUs;
    if (!options || !options->getSkipFrame(&skipFrameUs)) {
        return 0;
    }

    if (skipFrameUs <= timestampUs) {
        return 0;
    }

    // Safe guard against the abuse of the kSkipFrame_Option.
    if (skipFrameUs - timestampUs >= 1E6) {
        LOGE("Frame skipping requested is way too long: %lld us",
            skipFrameUs - timestampUs);

        return -1;
    }

    LOGV("skipFrame: %lld us > timestamp: %lld us",
        skipFrameUs, timestampUs);

    return 1;

}

void AudioSource::rampVolume(
        int32_t startFrame, int32_t rampDurationFrames,
        uint8_t *data,   size_t bytes) {

    const int32_t kShift = 14;
    int32_t fixedMultiplier = (startFrame << kShift) / rampDurationFrames;
    const int32_t nChannels = mRecord->channelCount();
    int32_t stopFrame = startFrame + bytes / sizeof(int16_t);
    int16_t *frame = (int16_t *) data;
    if (stopFrame > rampDurationFrames) {
        stopFrame = rampDurationFrames;
    }

    while (startFrame < stopFrame) {
        if (nChannels == 1) {  // mono
            frame[0] = (frame[0] * fixedMultiplier) >> kShift;
            ++frame;
            ++startFrame;
        } else {               // stereo
            frame[0] = (frame[0] * fixedMultiplier) >> kShift;
            frame[1] = (frame[1] * fixedMultiplier) >> kShift;
            frame += 2;
            startFrame += 2;
        }

        // Update the multiplier every 4 frames
        if ((startFrame & 3) == 0) {
            fixedMultiplier = (startFrame << kShift) / rampDurationFrames;
        }
    }
}

status_t AudioSource::read(
        MediaBuffer **out, const ReadOptions *options) {

    if (mInitCheck != OK) {
        return NO_INIT;
    }

    int64_t readTimeUs = systemTime() / 1000;
    if (mFirstFrame == false) {
        mStartTimeUs = readTimeUs;
        mFirstFrame = true;
    }
    *out = NULL;

    MediaBuffer *buffer;
    CHECK_EQ(mGroup->acquire_buffer(&buffer), OK);

    int err = 0;
    while (mStarted) {

        uint32_t numFramesRecorded;
        mRecord->getPosition(&numFramesRecorded);


        if (numFramesRecorded == 0 && mPrevSampleTimeUs == 0) {
            mInitialReadTimeUs = readTimeUs;
            // Initial delay
            if (mStartTimeUs > 0) {
                mStartTimeUs = readTimeUs - mStartTimeUs;
            } else {
                // Assume latency is constant.
                mStartTimeUs += mRecord->latency() * 1000;
            }
            mPrevSampleTimeUs = mStartTimeUs;
        }

        uint32_t sampleRate = mRecord->getSampleRate();

        // Insert null frames when lost frames are detected.
        int64_t timestampUs = mPrevSampleTimeUs;
        uint32_t numLostBytes = mRecord->getInputFramesLost() << 1;
        numLostBytes += mPrevLostBytes;
#if 0
        // Simulate lost frames
        numLostBytes = ((rand() * 1.0 / RAND_MAX)) * 2 * kMaxBufferSize;
        numLostBytes &= 0xFFFFFFFE; // Alignment requirement

        // Reduce the chance to lose
        if (rand() * 1.0 / RAND_MAX >= 0.05) {
            numLostBytes = 0;
        }
#endif
        if (numLostBytes > 0) {
            if (numLostBytes > kMaxBufferSize) {
                mPrevLostBytes = numLostBytes - kMaxBufferSize;
                numLostBytes = kMaxBufferSize;
            } else {
                mPrevLostBytes = 0;
            }

            CHECK_EQ(numLostBytes & 1, 0);
            timestampUs += ((1000000LL * (numLostBytes >> 1)) +
                    (sampleRate >> 1)) / sampleRate;

            CHECK(timestampUs > mPrevSampleTimeUs);
            if (mCollectStats) {
                mTotalLostFrames += (numLostBytes >> 1);
            }
            if ((err = skipFrame(timestampUs, options)) == -1) {
                buffer->release();
                return UNKNOWN_ERROR;
            } else if (err != 0) {
                continue;
            }
            memset(buffer->data(), 0, numLostBytes);
            buffer->set_range(0, numLostBytes);
            if (numFramesRecorded == 0) {
                buffer->meta_data()->setInt64(kKeyAnchorTime, mStartTimeUs);
            }
            buffer->meta_data()->setInt64(kKeyTime, mStartTimeUs + mPrevSampleTimeUs);
            buffer->meta_data()->setInt64(kKeyDriftTime, readTimeUs - mInitialReadTimeUs);
            mPrevSampleTimeUs = timestampUs;
            *out = buffer;
            return OK;
        }

        ssize_t n = mRecord->read(buffer->data(), buffer->size());
        if (n <= 0) {
            LOGE("Read from AudioRecord returns: %ld", n);
            buffer->release();
            return UNKNOWN_ERROR;
        }

        int64_t recordDurationUs = 0;
        if ( mFormat == AudioSystem::PCM_16_BIT ){
            recordDurationUs = (1000000LL * n >> 1) / (sampleRate * mNumChannels);
        }
        else {
            recordDurationUs = bufferDurationUs( n );
            if( timestampUs < SILENCE_INSERTION_TIME_PERIOD_USEC ) {
                if( mFormat == android::AudioSystem::AMR_NB ) {
                    int frameCount = recordDurationUs/AMR_NB_FRAME_DURATION_USEC; // 20000us is duration of one amr-nb frame
                    uint8_t* dataPtr = (uint8_t*)buffer->data();
                    n = 0;
                    while(frameCount > 0) {
                        memcpy(dataPtr,AMR_NB_SILENCE_FRAME,AMR_NB_SILENCE_FRAME_SIZE);
                        dataPtr += AMR_NB_SILENCE_FRAME_SIZE;
                        n += AMR_NB_SILENCE_FRAME_SIZE;
                        frameCount--;
                    }
                }
                // The below code is required if in future AAC tunnel mode is supported
#if 0
                if(mFormat == android::AudioSystem::AAC) {
                    int frameCount = numFrames,silenceFrameSize = 0;
                    uint8_t* dataPtr = data + 6; // dataPtr points to size of first frame
                    const uint8_t* silenceFrameData = NULL;
                    n = 0;
                    if(mNumChannels > 1) {
                        silenceFrameData = AAC_STEREO_SILENCE_FRAME_WITH_SIZE;
                        silenceFrameSize = AAC_STEREO_SILENCE_FRAME_SIZE;
                    }
                    else {
                        silenceFrameData = AAC_MONO_SILENCE_FRAME_WITH_SIZE;
                        silenceFrameSize = AAC_MONO_SILENCE_FRAME_SIZE;
                    }
                    while(frameCount > 0) {
                        memcpy(dataPtr,silenceFrameData,silenceFrameSize);
                        dataPtr += silenceFrameSize;
                        n += silenceFrameSize - 2;
                        frameCount--;
                    }
                }
#endif
            }
        }

        timestampUs += recordDurationUs;
        if ((err = skipFrame(timestampUs, options)) == -1) {
            buffer->release();
            return UNKNOWN_ERROR;
        } else if (err != 0) {
            continue;
        }

        if ( mFormat == AudioSystem::PCM_16_BIT ) {
            if (mPrevSampleTimeUs - mStartTimeUs < kAutoRampStartUs) {
                // Mute the initial video recording signal
                memset((uint8_t *) buffer->data(), 0, n);
            } else if (mPrevSampleTimeUs - mStartTimeUs < kAutoRampStartUs + kAutoRampDurationUs) {
                int32_t autoRampDurationFrames =
                    (kAutoRampDurationUs * sampleRate + 500000LL) / 1000000LL;

                int32_t autoRampStartFrames =
                    (kAutoRampStartUs * sampleRate + 500000LL) / 1000000LL;

                int32_t nFrames = numFramesRecorded - autoRampStartFrames;
                rampVolume(nFrames, autoRampDurationFrames, (uint8_t *) buffer->data(), n);
            }
        }

        if (mTrackMaxAmplitude && mFormat == AudioSystem::PCM_16_BIT) {
            trackMaxAmplitude((int16_t *) buffer->data(), n >> 1);
        }

        if (numFramesRecorded == 0) {
            buffer->meta_data()->setInt64(kKeyAnchorTime, mStartTimeUs);
        }

        buffer->meta_data()->setInt64(kKeyTime, mStartTimeUs + mPrevSampleTimeUs);

        if (mFormat == AudioSystem::PCM_16_BIT) {
            buffer->meta_data()->setInt64(kKeyDriftTime, readTimeUs - mInitialReadTimeUs);
        }
        else {
            int64_t wallClockTimeUs = readTimeUs - mInitialReadTimeUs;
            int64_t mediaTimeUs = mStartTimeUs + mPrevSampleTimeUs;
            LOGV("AudioSource , new drift time = %lld", mediaTimeUs - wallClockTimeUs );
            buffer->meta_data()->setInt64(kKeyDriftTime, mediaTimeUs - wallClockTimeUs);
        }

        CHECK(timestampUs > mPrevSampleTimeUs);
        mPrevSampleTimeUs = timestampUs;
        LOGV("initial delay: %lld, sample rate: %d, timestamp: %lld",
                mStartTimeUs, sampleRate, timestampUs);

        buffer->set_range(0, n);

        *out = buffer;
        return OK;
    }

    return OK;
}

void AudioSource::trackMaxAmplitude(int16_t *data, int nSamples) {
    for (int i = nSamples; i > 0; --i) {
        int16_t value = *data++;
        if (value < 0) {
            value = -value;
        }
        if (mMaxAmplitude < value) {
            mMaxAmplitude = value;
        }
    }
}

int16_t AudioSource::getMaxAmplitude() {
    // First call activates the tracking.
    if (!mTrackMaxAmplitude) {
        mTrackMaxAmplitude = true;
    }
    int16_t value = mMaxAmplitude;
    mMaxAmplitude = 0;
    LOGV("max amplitude since last call: %d", value);
    return value;
}

int64_t AudioSource::bufferDurationUs( ssize_t n ) {

    int64_t dataDurationMs = 0;
    if (mFormat == AudioSystem::AMR_NB) {
        dataDurationMs = (n/AMR_FRAMESIZE) * 20; //ms
    }
    else if (mFormat == AudioSystem::EVRC) {
        dataDurationMs = (n/EVRC_FRAMESIZE) * 20; //ms
    }
    else if (mFormat == AudioSystem::QCELP) {
        dataDurationMs = (n/QCELP_FRAMESIZE) * 20; //ms
    }
    else
        CHECK(0);

    return dataDurationMs*1000LL;
}

}  // namespace android
