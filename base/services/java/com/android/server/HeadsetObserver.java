/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Copyright (c) 2010, The Linux Foundation. All rights reserved.
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

package com.android.server;

import android.app.ActivityManagerNative;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Message;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;
import android.os.UEventObserver;
import android.util.Slog;
import android.media.AudioManager;

import java.io.FileReader;
import java.io.FileNotFoundException;

/**
 * <p>HeadsetObserver monitors for a wired headset.
 */
class HeadsetObserver extends UEventObserver {
    private static final String TAG = HeadsetObserver.class.getSimpleName();
    private static final boolean LOG = true;

    private static final String HEADSET_UEVENT_MATCH = "DEVPATH=/devices/virtual/switch/h2w";
    private static final String HEADSET_STATE_PATH = "/sys/class/switch/h2w/state";
    private static final String HEADSET_NAME_PATH = "/sys/class/switch/h2w/name";

    private static final int BIT_HEADSET = (1 << 0);
    private static final int BIT_HEADSET_SPEAKER_ONLY = (1 << 1);
    private static final int BIT_HEADSET_MIC_ONLY = (1 << 2);
    private static final int BIT_ANCHEADSET = (1 << 3);
    private static final int BIT_ANCHEADSET_SPEAKER_ONLY = (1 << 4);
    private static final int BIT_ANCHEADSET_MIC_ONLY = (1 << 5);
    private static final int SUPPORTED_HEADSETS = (BIT_HEADSET|BIT_HEADSET_SPEAKER_ONLY|BIT_HEADSET_MIC_ONLY |
                                                   BIT_ANCHEADSET|BIT_ANCHEADSET_SPEAKER_ONLY|BIT_ANCHEADSET_MIC_ONLY );
    private static final int HEADSETS_WITH_MIC_AND_SPEAKER = BIT_HEADSET;
    private static final int HEADSETS_WITH_SPEAKER_ONLY = BIT_HEADSET_SPEAKER_ONLY;
    private static final int HEADSETS_WITH_MIC_ONLY = BIT_HEADSET_MIC_ONLY;

    private static final int ANCHEADSETS_WITH_MIC_AND_SPEAKER = BIT_ANCHEADSET;
    private static final int ANCHEADSETS_WITH_SPEAKER_ONLY = BIT_ANCHEADSET_SPEAKER_ONLY;
    private static final int ANCHEADSETS_WITH_MIC_ONLY = BIT_ANCHEADSET_MIC_ONLY;

    private int mHeadsetState;
    private int mPrevHeadsetState;
    private String mHeadsetName;

    private final Context mContext;
    private final WakeLock mWakeLock;  // held while there is a pending route change

    public HeadsetObserver(Context context) {
        mContext = context;
        PowerManager pm = (PowerManager)context.getSystemService(Context.POWER_SERVICE);
        mWakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "HeadsetObserver");
        mWakeLock.setReferenceCounted(false);

        startObserving(HEADSET_UEVENT_MATCH);

        init();  // set initial status
    }

    @Override
    public void onUEvent(UEventObserver.UEvent event) {
        if (LOG) Slog.v(TAG, "Headset UEVENT: " + event.toString());

        try {
            update(event.get("SWITCH_NAME"), Integer.parseInt(event.get("SWITCH_STATE")));
        } catch (NumberFormatException e) {
            Slog.e(TAG, "Could not parse switch state from event " + event);
        }
    }

    private synchronized final void init() {
        char[] buffer = new char[1024];

        String newName = mHeadsetName;
        int newState = mHeadsetState;
        mPrevHeadsetState = mHeadsetState;
        try {
            FileReader file = new FileReader(HEADSET_STATE_PATH);
            int len = file.read(buffer, 0, 1024);
            newState = Integer.valueOf((new String(buffer, 0, len)).trim());

            file = new FileReader(HEADSET_NAME_PATH);
            len = file.read(buffer, 0, 1024);
            newName = new String(buffer, 0, len).trim();

        } catch (FileNotFoundException e) {
            Slog.w(TAG, "This kernel does not have wired headset support");
        } catch (Exception e) {
            Slog.e(TAG, "" , e);
        }

        update(newName, newState);
    }

    private synchronized final void update(String newName, int newState) {
        // Retain only relevant bits
        int headsetState = newState & SUPPORTED_HEADSETS;
        int delay = 0;
        // reject all suspect transitions: only accept state changes from:
        // - a: 0 heaset to 1 headset
        // - b: 1 headset to 0 headset
        if (mHeadsetState == headsetState) {
               return;
        }

        mHeadsetName = newName;
        mPrevHeadsetState = mHeadsetState;
        mHeadsetState = headsetState;

        if (headsetState == 0) {
            Intent intent = new Intent(AudioManager.ACTION_AUDIO_BECOMING_NOISY);
            mContext.sendBroadcast(intent);
            // It can take hundreds of ms flush the audio pipeline after
            // apps pause audio playback, but audio route changes are
            // immediate, so delay the route change by 1000ms.
            // This could be improved once the audio sub-system provides an
            // interface to clear the audio pipeline.
            delay = 1000;
        } else {
            // Insert the same delay for headset connection so that the connection event is not
            // broadcast before the disconnection event in case of fast removal/insertion
            if (mHandler.hasMessages(0)) {
                delay = 1000;
            }
        }
        mWakeLock.acquire();
        mHandler.sendMessageDelayed(mHandler.obtainMessage(0,
                                                           mHeadsetState,
                                                           mPrevHeadsetState,
                                                           mHeadsetName),
                                    delay);
    }

    private synchronized final void sendIntents(int headsetState, int prevHeadsetState, String headsetName) {
        int allHeadsets = SUPPORTED_HEADSETS;
        //Handle unplug events first and then handle plug-in events
        for (int curHeadset = 1; curHeadset < SUPPORTED_HEADSETS; curHeadset <<= 1) {
            if (((headsetState & curHeadset) == 0) && ((prevHeadsetState & curHeadset) == curHeadset)) {
                if ((curHeadset & allHeadsets) != 0) {
                    sendIntent(curHeadset, headsetState, prevHeadsetState, headsetName);
                    allHeadsets &= ~curHeadset;
                }
            }
        }
        for (int curHeadset = 1; curHeadset < SUPPORTED_HEADSETS; curHeadset <<= 1) {
            if (((headsetState & curHeadset) == curHeadset) && ((prevHeadsetState & curHeadset) == 0)) {
                if ((curHeadset & allHeadsets) != 0) {
                    sendIntent(curHeadset, headsetState, prevHeadsetState, headsetName);
                    allHeadsets &= ~curHeadset;
                }
            }
        }
    }

    private final void sendIntent(int headset, int headsetState, int prevHeadsetState, String headsetName) {
        if ((headsetState & headset) != (prevHeadsetState & headset)) {
            //  Pack up the values and broadcast them to everyone
            Intent intent = new Intent(Intent.ACTION_HEADSET_PLUG);
            intent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY);
            int state = 0;
            int microphone = 0;
            int speaker = 0;
            int anc = 0;

            if ((headset & HEADSETS_WITH_SPEAKER_ONLY) != 0) {
                speaker = 1;
            }

            if ((headset & HEADSETS_WITH_MIC_AND_SPEAKER) != 0) {
                microphone = 1;
                speaker = 1;
            }

            if ((headset & HEADSETS_WITH_MIC_ONLY) != 0) {
                microphone = 1;
            }

            if ((headset & ANCHEADSETS_WITH_SPEAKER_ONLY) != 0) {
                speaker = 1;
                anc = 1;
            }

            if ((headset & ANCHEADSETS_WITH_MIC_AND_SPEAKER) != 0) {
                microphone = 1;
                speaker = 1;
                anc = 1;
            }

            if ((headset & ANCHEADSETS_WITH_MIC_ONLY) != 0) {
                microphone = 1;
                anc = 1;
            }

            if ((headsetState & headset) != 0) {
                state = 1;
            }
            intent.putExtra("state", state);
            intent.putExtra("name", headsetName);
            intent.putExtra("microphone", microphone);
            intent.putExtra("speaker", speaker);
            intent.putExtra("anc", anc);

            if (LOG) Slog.v(TAG, "Intent.ACTION_HEADSET_PLUG: state: "+state+" name: "+headsetName+" mic: "+microphone+" speaker: "+speaker + "anc: " + anc);
            // TODO: Should we require a permission?
            ActivityManagerNative.broadcastStickyIntent(intent, null);
        }
    }

    private final Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            sendIntents(msg.arg1, msg.arg2, (String)msg.obj);
            mWakeLock.release();
        }
    };
}
