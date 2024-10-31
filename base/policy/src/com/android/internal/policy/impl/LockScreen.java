/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
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

package com.android.internal.policy.impl;

import com.android.internal.R;
import com.android.internal.telephony.IccCard;
import com.android.internal.widget.LockPatternUtils;
import com.android.internal.widget.SlidingTab;

import android.content.Context;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.content.res.ColorStateList;
import android.text.format.DateFormat;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.*;
import android.graphics.drawable.Drawable;
import android.util.Log;
import android.media.AudioManager;
import android.os.BatteryManager;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.provider.Settings;
import android.telephony.TelephonyManager;

import java.util.ArrayList;
import java.util.Date;
import java.io.File;

/**
 * The screen within {@link LockPatternKeyguardView} that shows general
 * information about the device depending on its state, and how to get
 * past it, as applicable.
 */
class LockScreen extends LinearLayout implements KeyguardScreen, KeyguardUpdateMonitor.InfoCallback,
        KeyguardUpdateMonitor.SimStateCallback, SlidingTab.OnTriggerListener {

    private static final boolean DBG = false;
    private static final String TAG = "LockScreen";
    private static final String ENABLE_MENU_KEY_FILE = "/data/local/enable_menu_key";

    private Status[] mStatus = {Status.Normal};

    private LockPatternUtils mLockPatternUtils;
    private KeyguardUpdateMonitor mUpdateMonitor;
    private KeyguardScreenCallback mCallback;

    private TextView[] mCarrier;
    private SlidingTab mSelector;
    private TextView mTime;
    private TextView mDate;
    private TextView mStatus1;
    private TextView mStatus2;
    private TextView mScreenLocked;
    private TextView[] mEmergencyCallText;
    private Button mEmergencyCallButton;

    // current configuration state of keyboard and display
    private int mKeyboardHidden;
    private int mCreationOrientation;

    // are we showing battery information?
    private boolean mShowingBatteryInfo = false;

    // last known plugged in state
    private boolean mPluggedIn = false;

    // last known battery level
    private int mBatteryLevel = 100;

    private String mNextAlarm = null;
    private Drawable mAlarmIcon = null;
    private String mCharging = null;
    private Drawable mChargingIcon = null;

    private boolean mSilentMode;
    private AudioManager mAudioManager;
    private String mDateFormatString;
    private java.text.DateFormat mTimeFormat;
    private boolean mEnableMenuKeyInLockScreen;

    private int[] mResId = {R.id.carrier, R.id.carrier_sub2};
    private int[] mEmergencyId = {R.id.emergencyCallText, R.id.emergencyCallTextSub2};

    /**
     * The status of this lock screen.
     */
    enum Status {
        /**
         * Normal case (sim card present, it's not locked)
         */
        Normal(true),

        /**
         * The sim card is 'network locked'.
         */
        NetworkLocked(true),

        /**
         * The sim card is missing.
         */
        SimMissing(false),

        /**
         * The sim card is missing, and this is the device isn't provisioned, so we don't let
         * them get past the screen.
         */
        SimMissingLocked(false),

        /**
         * The sim card is PUK locked, meaning they've entered the wrong sim unlock code too many
         * times.
         */
        SimPukLocked(false),

        /**
         * The sim card is locked.
         */
        SimLocked(true),

        /**
         * The sim card is faulty.
         */
        SimIOError(true),

        /**
         * The ICC card is 'SIM network subset locked'.
         */
        NetworkSubsetLocked(true),

        /**
         * The ICC card is 'SIM corporate locked'.
         */
        CorporateLocked(true),

        /**
         * The ICC card is 'SIM service provider locked'.
         */
        ServiceProviderLocked(true),

        /**
         * The ICC card is 'SIM SIM locked'.
         */
        SimSimLocked(true),

        /**
         * The ICC card is 'RUIM network1 locked'.
         */
        RuimNetwork1Locked(true),

        /**
         * The ICC card is 'RUIM network2 locked'.
         */
        RuimNetwork2Locked(true),

        /**
         * The ICC card is 'RUIM hrpd locked'.
         */
        RuimHrpdLocked(true),

        /**
         * The ICC card is 'RUIM corporate locked'.
         */
        RuimCorporateLocked(true),

        /**
         * The ICC card is 'RUIM service provider locked'.
         */
        RuimServiceProviderLocked(true),

        /**
         * The ICC card is 'RUIM RUIM locked'.
         */
        RuimRuimLocked(true);

        private final boolean mShowStatusLines;

        Status(boolean mShowStatusLines) {
            this.mShowStatusLines = mShowStatusLines;
        }

        /**
         * @return Whether the status lines (battery level and / or next alarm) are shown while
         *         in this state.  Mostly dictated by whether this is room for them.
         */
        public boolean showStatusLines() {
            return mShowStatusLines;
        }
    }

    /**
     * In general, we enable unlocking the insecure key guard with the menu key. However, there are
     * some cases where we wish to disable it, notably when the menu button placement or technology
     * is prone to false positives.
     *
     * @return true if the menu key should be enabled
     */
    private boolean shouldEnableMenuKey() {
        final Resources res = getResources();
        final boolean configDisabled = res.getBoolean(R.bool.config_disableMenuKeyInLockScreen);
        final boolean isMonkey = SystemProperties.getBoolean("ro.monkey", false);
        final boolean fileOverride = (new File(ENABLE_MENU_KEY_FILE)).exists();
        return !configDisabled || isMonkey || fileOverride;
    }

    /**
     * @param context Used to setup the view.
     * @param configuration The current configuration. Used to use when selecting layout, etc.
     * @param lockPatternUtils Used to know the state of the lock pattern settings.
     * @param updateMonitor Used to register for updates on various keyguard related
     *    state, and query the initial state at setup.
     * @param callback Used to communicate back to the host keyguard view.
     */
    LockScreen(Context context, Configuration configuration, LockPatternUtils lockPatternUtils,
            KeyguardUpdateMonitor updateMonitor,
            KeyguardScreenCallback callback) {
        super(context);
        mLockPatternUtils = lockPatternUtils;
        mUpdateMonitor = updateMonitor;
        mCallback = callback;

        mEnableMenuKeyInLockScreen = shouldEnableMenuKey();

        mCreationOrientation = configuration.orientation;

        mKeyboardHidden = configuration.hardKeyboardHidden;

        if (LockPatternKeyguardView.DEBUG_CONFIGURATION) {
            Log.v(TAG, "***** CREATING LOCK SCREEN", new RuntimeException());
            Log.v(TAG, "Cur orient=" + mCreationOrientation
                    + " res orient=" + context.getResources().getConfiguration().orientation);
        }

        final LayoutInflater inflater = LayoutInflater.from(context);
        Log.d(TAG, "Keyguard screen = " + mCreationOrientation);
        if (mCreationOrientation != Configuration.ORIENTATION_LANDSCAPE) {
            inflater.inflate(R.layout.keyguard_screen_tab_unlock, this, true);
        } else {
            inflater.inflate(R.layout.keyguard_screen_tab_unlock_land, this, true);
        }

        int numPhones = TelephonyManager.getPhoneCount();
        // Sim States for the subscription
        mStatus = new Status[numPhones];
        mCarrier = new TextView[numPhones];
        for (int i = 0; i < numPhones; i++) {
            mStatus[i] = Status.Normal;
            mCarrier[i] = (TextView) findViewById(mResId[i]);
            // Required for Marquee to work
            mCarrier[i].setSelected(true);
            mCarrier[i].setTextColor(0xffffffff);
        }
        mEmergencyCallText = new TextView[mEmergencyId.length];
        for (int i=0; i < mEmergencyId.length; i++) {
            mEmergencyCallText[i] = (TextView) findViewById(mEmergencyId[i]);
            mEmergencyCallText[i].setVisibility(View.GONE);
        }

        mDate = (TextView) findViewById(R.id.date);
        mStatus1 = (TextView) findViewById(R.id.status1);
        mStatus2 = (TextView) findViewById(R.id.status2);

        mScreenLocked = (TextView) findViewById(R.id.screenLocked);
        mSelector = (SlidingTab) findViewById(R.id.tab_selector);
        mSelector.setHoldAfterTrigger(true, false);
        mSelector.setLeftHintText(R.string.lockscreen_unlock_label);

        mEmergencyCallButton = (Button) findViewById(R.id.emergencyCallButton);
        mEmergencyCallButton.setText(R.string.lockscreen_emergency_call);

        mLockPatternUtils.updateEmergencyCallButtonState(mEmergencyCallButton);
        mEmergencyCallButton.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                mCallback.takeEmergencyCallAction();
            }
        });


        setFocusable(true);
        setFocusableInTouchMode(true);
        setDescendantFocusability(ViewGroup.FOCUS_BLOCK_DESCENDANTS);

        mUpdateMonitor.registerInfoCallback(this);
        mUpdateMonitor.registerSimStateCallback(this);

        mAudioManager = (AudioManager) getContext().getSystemService(Context.AUDIO_SERVICE);
        mSilentMode = isSilentMode();

        mSelector.setLeftTabResources(
                R.drawable.ic_jog_dial_unlock,
                R.drawable.jog_tab_target_green,
                R.drawable.jog_tab_bar_left_unlock,
                R.drawable.jog_tab_left_unlock);

        updateRightTabResources();

        mSelector.setOnTriggerListener(this);

        resetStatusInfo(updateMonitor);
    }

    private boolean isSilentMode() {
        return mAudioManager.getRingerMode() != AudioManager.RINGER_MODE_NORMAL;
    }

    private void updateRightTabResources() {
        boolean vibe = mSilentMode
            && (mAudioManager.getRingerMode() == AudioManager.RINGER_MODE_VIBRATE);

        mSelector.setRightTabResources(
                mSilentMode ? ( vibe ? R.drawable.ic_jog_dial_vibrate_on
                                     : R.drawable.ic_jog_dial_sound_off )
                            : R.drawable.ic_jog_dial_sound_on,
                mSilentMode ? R.drawable.jog_tab_target_yellow
                            : R.drawable.jog_tab_target_gray,
                mSilentMode ? R.drawable.jog_tab_bar_right_sound_on
                            : R.drawable.jog_tab_bar_right_sound_off,
                mSilentMode ? R.drawable.jog_tab_right_sound_on
                            : R.drawable.jog_tab_right_sound_off);
    }

    private void resetStatusInfo(KeyguardUpdateMonitor updateMonitor) {
        mShowingBatteryInfo = updateMonitor.shouldShowBatteryInfo();
        mPluggedIn = updateMonitor.isDevicePluggedIn();
        mBatteryLevel = updateMonitor.getBatteryLevel();

        for (int i = 0; i < TelephonyManager.getPhoneCount(); i++) {
            mStatus[i] = getCurrentStatus(updateMonitor.getSimState(i));
            updateLayout(mStatus[i], i);
        }

        refreshBatteryStringAndIcon();
        refreshAlarmDisplay();

        mTimeFormat = DateFormat.getTimeFormat(getContext());
        mDateFormatString = getContext().getString(R.string.full_wday_month_day_no_year);
        refreshTimeAndDateDisplay();
        updateStatusLines();
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_MENU && mEnableMenuKeyInLockScreen) {
            mCallback.goToUnlockScreen();
        }
        return false;
    }

    /** {@inheritDoc} */
    public void onTrigger(View v, int whichHandle) {
        if (whichHandle == SlidingTab.OnTriggerListener.LEFT_HANDLE) {
            mCallback.goToUnlockScreen();
        } else if (whichHandle == SlidingTab.OnTriggerListener.RIGHT_HANDLE) {
            // toggle silent mode
            mSilentMode = !mSilentMode;
            if (mSilentMode) {
                final boolean vibe = (Settings.System.getInt(
                    getContext().getContentResolver(),
                    Settings.System.VIBRATE_IN_SILENT, 1) == 1);

                mAudioManager.setRingerMode(vibe
                    ? AudioManager.RINGER_MODE_VIBRATE
                    : AudioManager.RINGER_MODE_SILENT);
            } else {
                mAudioManager.setRingerMode(AudioManager.RINGER_MODE_NORMAL);
            }

            updateRightTabResources();

            String message = mSilentMode ?
                    getContext().getString(R.string.global_action_silent_mode_on_status) :
                    getContext().getString(R.string.global_action_silent_mode_off_status);

            final int toastIcon = mSilentMode
                ? R.drawable.ic_lock_ringer_off
                : R.drawable.ic_lock_ringer_on;

            final int toastColor = mSilentMode
                ? getContext().getResources().getColor(R.color.keyguard_text_color_soundoff)
                : getContext().getResources().getColor(R.color.keyguard_text_color_soundon);
            toastMessage(mScreenLocked, message, toastColor, toastIcon);
            mCallback.pokeWakelock();
        }
    }

    /** {@inheritDoc} */
    public void onGrabbedStateChange(View v, int grabbedState) {
        if (grabbedState == SlidingTab.OnTriggerListener.RIGHT_HANDLE) {
            mSilentMode = isSilentMode();
            mSelector.setRightHintText(mSilentMode ? R.string.lockscreen_sound_on_label
                    : R.string.lockscreen_sound_off_label);
        }
        // Don't poke the wake lock when returning to a state where the handle is
        // not grabbed since that can happen when the system (instead of the user)
        // cancels the grab.
        if (grabbedState != SlidingTab.OnTriggerListener.NO_HANDLE) {
            mCallback.pokeWakelock();
        }
    }

    /**
     * Displays a message in a text view and then restores the previous text.
     * @param textView The text view.
     * @param text The text.
     * @param color The color to apply to the text, or 0 if the existing color should be used.
     * @param iconResourceId The left hand icon.
     */
    private void toastMessage(final TextView textView, final String text, final int color, final int iconResourceId) {
        if (mPendingR1 != null) {
            textView.removeCallbacks(mPendingR1);
            mPendingR1 = null;
        }
        if (mPendingR2 != null) {
            mPendingR2.run(); // fire immediately, restoring non-toasted appearance
            textView.removeCallbacks(mPendingR2);
            mPendingR2 = null;
        }

        final String oldText = textView.getText().toString();
        final ColorStateList oldColors = textView.getTextColors();

        mPendingR1 = new Runnable() {
            public void run() {
                textView.setText(text);
                if (color != 0) {
                    textView.setTextColor(color);
                }
                textView.setCompoundDrawablesWithIntrinsicBounds(iconResourceId, 0, 0, 0);
            }
        };

        textView.postDelayed(mPendingR1, 0);
        mPendingR2 = new Runnable() {
            public void run() {
                textView.setText(oldText);
                textView.setTextColor(oldColors);
                textView.setCompoundDrawablesWithIntrinsicBounds(0, 0, 0, 0);
            }
        };
        textView.postDelayed(mPendingR2, 3500);
    }
    private Runnable mPendingR1;
    private Runnable mPendingR2;

    private void refreshAlarmDisplay() {
        mNextAlarm = mLockPatternUtils.getNextAlarm();
        if (mNextAlarm != null) {
            mAlarmIcon = getContext().getResources().getDrawable(R.drawable.ic_lock_idle_alarm);
        }
        updateStatusLines();
    }

    /** {@inheritDoc} */
    public void onRefreshBatteryInfo(boolean showBatteryInfo, boolean pluggedIn,
            int batteryLevel) {
        if (DBG) Log.d(TAG, "onRefreshBatteryInfo(" + showBatteryInfo + ", " + pluggedIn + ")");
        mShowingBatteryInfo = showBatteryInfo;
        mPluggedIn = pluggedIn;
        mBatteryLevel = batteryLevel;

        refreshBatteryStringAndIcon();
        updateStatusLines();
    }

    private void refreshBatteryStringAndIcon() {
        if (!mShowingBatteryInfo) {
            mCharging = null;
            return;
        }

        if (mChargingIcon == null) {
            mChargingIcon =
                    getContext().getResources().getDrawable(R.drawable.ic_lock_idle_charging);
        }

        if (mPluggedIn) {
            if (mUpdateMonitor.isDeviceCharged()) {
                mCharging = getContext().getString(R.string.lockscreen_charged);
            } else {
                mCharging = getContext().getString(R.string.lockscreen_plugged_in, mBatteryLevel);
            }
        } else {
            mCharging = getContext().getString(R.string.lockscreen_low_battery);
        }
    }

    /** {@inheritDoc} */
    public void onTimeChanged() {
        refreshTimeAndDateDisplay();
    }

    private void refreshTimeAndDateDisplay() {
        mDate.setText(DateFormat.format(mDateFormatString, new Date()));
    }

    private void updateStatusLines() {
        // Use default subscription status for alarm and charging text display.
        int sub = TelephonyManager.getDefaultSubscription();
        if (!mStatus[sub].showStatusLines()
                || (mCharging == null && mNextAlarm == null)) {
            mStatus1.setVisibility(View.INVISIBLE);
            mStatus2.setVisibility(View.INVISIBLE);
        } else if (mCharging != null && mNextAlarm == null) {
            // charging only
            mStatus1.setVisibility(View.VISIBLE);
            mStatus2.setVisibility(View.INVISIBLE);

            mStatus1.setText(mCharging);
            mStatus1.setCompoundDrawablesWithIntrinsicBounds(mChargingIcon, null, null, null);
        } else if (mNextAlarm != null && mCharging == null) {
            // next alarm only
            mStatus1.setVisibility(View.VISIBLE);
            mStatus2.setVisibility(View.INVISIBLE);

            mStatus1.setText(mNextAlarm);
            mStatus1.setCompoundDrawablesWithIntrinsicBounds(mAlarmIcon, null, null, null);
        } else if (mCharging != null && mNextAlarm != null) {
            // both charging and next alarm
            mStatus1.setVisibility(View.VISIBLE);
            mStatus2.setVisibility(View.VISIBLE);

            mStatus1.setText(mCharging);
            mStatus1.setCompoundDrawablesWithIntrinsicBounds(mChargingIcon, null, null, null);
            mStatus2.setText(mNextAlarm);
            mStatus2.setCompoundDrawablesWithIntrinsicBounds(mAlarmIcon, null, null, null);
        }
    }

    /** {@inheritDoc} */
    public void onRefreshCarrierInfo(CharSequence plmn, CharSequence spn, int subscription) {
        if (DBG) Log.d(TAG, "onRefreshCarrierInfo(" + plmn + ", " + spn + ")");
        // Update operator name display.
        updateLayout(mStatus[subscription], subscription);
    }

    /**
     * Determine the current status of the lock screen given the sim state and other stuff.
     */
    private Status getCurrentStatus(IccCard.State simState) {
        boolean missingAndNotProvisioned = (!mUpdateMonitor.isDeviceProvisioned()
                && simState == IccCard.State.ABSENT);
        if (missingAndNotProvisioned) {
            return Status.SimMissingLocked;
        }

        switch (simState) {
            case ABSENT:
                return Status.SimMissing;
            case NETWORK_LOCKED:
                return Status.NetworkLocked;
            case NOT_READY:
                return Status.SimMissing;
            case PIN_REQUIRED:
                return Status.SimLocked;
            case PUK_REQUIRED:
                return Status.SimPukLocked;
            case READY:
                return Status.Normal;
            case UNKNOWN:
                return Status.SimMissing;
            case CARD_IO_ERROR:
                return Status.SimIOError;
            case SIM_NETWORK_SUBSET_LOCKED:
                return Status.NetworkSubsetLocked;
            case SIM_CORPORATE_LOCKED:
                return Status.CorporateLocked;
            case SIM_SERVICE_PROVIDER_LOCKED:
                return Status.ServiceProviderLocked;
            case SIM_SIM_LOCKED:
                return Status.SimSimLocked;
            case RUIM_NETWORK1_LOCKED:
                return Status.RuimNetwork1Locked;
            case RUIM_NETWORK2_LOCKED:
                return Status.RuimNetwork2Locked;
            case RUIM_HRPD_LOCKED:
                return Status.RuimHrpdLocked;
            case RUIM_CORPORATE_LOCKED:
                return Status.RuimCorporateLocked;
            case RUIM_SERVICE_PROVIDER_LOCKED:
                return Status.RuimServiceProviderLocked;
            case RUIM_RUIM_LOCKED:
                return Status.RuimRuimLocked;
        }
        return Status.SimMissing;
    }

    /**
     * Update the layout to match the current status.
     */
    private void updateLayout(Status status, int subscription) {
        // The emergency call button no longer appears on this screen.
        Log.d(TAG, "updateLayout: status=" + status + "subscription=" + subscription);

        mEmergencyCallButton.setVisibility(View.GONE); // in almost all cases

        switch (status) {
            case Normal:
                // text
                mCarrier[subscription].setText(
                         getCarrierString(
                         mUpdateMonitor.getTelephonyPlmn(subscription),
                         mUpdateMonitor.getTelephonySpn(subscription)));
                // Empty now, but used for sliding tab feedback
                mScreenLocked.setText("");

                // layout
                mScreenLocked.setVisibility(View.VISIBLE);
                mSelector.setVisibility(View.VISIBLE);
                mEmergencyCallText[subscription].setVisibility(View.GONE);
                break;
            case NetworkLocked:
                // The carrier string shows both sim card status (i.e. No Sim Card) and
                // carrier's name and/or "Emergency Calls Only" status
                mCarrier[subscription].setText(
                        getCarrierString(
                                mUpdateMonitor.getTelephonyPlmn(subscription),
                                getContext().getText(R.string.lockscreen_sim_network_locked_message)));
                mScreenLocked.setText(R.string.lockscreen_instructions_when_pattern_disabled);

                // layout
                mScreenLocked.setVisibility(View.VISIBLE);
                mSelector.setVisibility(View.VISIBLE);
                mEmergencyCallText[subscription].setVisibility(View.GONE);
                break;
            case SimMissing:
                // text
                mCarrier[subscription].setText(R.string.lockscreen_missing_sim_message_short);
                mScreenLocked.setText(R.string.lockscreen_missing_sim_instructions);

                // layout
                mScreenLocked.setVisibility(View.VISIBLE);
                mSelector.setVisibility(View.VISIBLE);
                mEmergencyCallText[subscription].setVisibility(View.VISIBLE);
                // do not need to show the e-call button; user may unlock
                break;
            case SimMissingLocked:
                // text
                mCarrier[subscription].setText(
                        getCarrierString(
                                mUpdateMonitor.getTelephonyPlmn(subscription),
                                getContext().getText(R.string.lockscreen_missing_sim_message_short)));

                disableUnlockScreen(R.string.lockscreen_missing_sim_instructions, subscription);
                break;
            case SimLocked:
                // text
                mCarrier[subscription].setText(
                        getCarrierString(
                                mUpdateMonitor.getTelephonyPlmn(subscription),
                                getContext().getText(R.string.lockscreen_sim_sim_locked_message)));

                // layout
                mScreenLocked.setVisibility(View.INVISIBLE);
                mSelector.setVisibility(View.VISIBLE);
                mEmergencyCallText[subscription].setVisibility(View.GONE);
                break;
            case SimPukLocked:
                // text
                mCarrier[subscription].setText(
                        getCarrierString(
                                mUpdateMonitor.getTelephonyPlmn(subscription),
                                getContext().getText(R.string.lockscreen_sim_puk_locked_message)));

                disableUnlockScreen(R.string.lockscreen_sim_puk_locked_instructions, subscription);
                break;
            case SimIOError:
                // text
                mCarrier[subscription].setText(R.string.lockscreen_sim_error_message_short);
                mScreenLocked.setText(R.string.lockscreen_instructions_when_pattern_disabled);

                // layout
                mScreenLocked.setVisibility(View.INVISIBLE);
                mSelector.setVisibility(View.VISIBLE);
                mEmergencyCallButton.setVisibility(View.VISIBLE);
                mEmergencyCallText[subscription].setVisibility(View.VISIBLE);
                break;
            case NetworkSubsetLocked:
                //  text
                mCarrier[subscription].setText(R.string.lockscreen_sim_network_subset_locked_message);
                updateLayoutForPersoText();
                break;
            case CorporateLocked:
                //  text
                mCarrier[subscription].setText(R.string.lockscreen_sim_corporate_locked_message);
                updateLayoutForPersoText();
                break;
            case ServiceProviderLocked:
                //  text
                mCarrier[subscription].setText(R.string.lockscreen_sim_service_provider_locked_message);
                updateLayoutForPersoText();
                break;
            case SimSimLocked:
                //  text
                mCarrier[subscription].setText(R.string.lockscreen_sim_sim_locked_message);
                updateLayoutForPersoText();
                break;
            case RuimNetwork1Locked:
                //  text
                mCarrier[subscription].setText(R.string.lockscreen_ruim_network1_locked_message);
                updateLayoutForPersoText();
                break;
            case RuimNetwork2Locked:
                //  text
                mCarrier[subscription].setText(R.string.lockscreen_ruim_network2_locked_message);
                updateLayoutForPersoText();
                break;
            case RuimHrpdLocked:
                //  text
                mCarrier[subscription].setText(R.string.lockscreen_ruim_hrpd_locked_message);
                updateLayoutForPersoText();
                break;
            case RuimCorporateLocked:
                //  text
                mCarrier[subscription].setText(R.string.lockscreen_ruim_corporate_locked_message);
                updateLayoutForPersoText();
                break;
            case RuimServiceProviderLocked:
                //  text
                mCarrier[subscription].setText(R.string.lockscreen_ruim_service_provider_locked_message);
                updateLayoutForPersoText();
                break;
            case RuimRuimLocked:
                //  text
                mCarrier[subscription].setText(R.string.lockscreen_ruim_ruim_locked_message);
                updateLayoutForPersoText();
                break;
        }
    }

    private void disableUnlockScreen(int resId, int subscription) {
        // layout
        boolean disableUnlockScreen = true;
        for (int i = 0; i < TelephonyManager.getPhoneCount(); i++) {
            disableUnlockScreen = disableUnlockScreen
                    && (mStatus[i] == Status.SimMissingLocked ||
                    mStatus[i] == Status.SimPukLocked);
            if (!disableUnlockScreen) break;
        }
        if (disableUnlockScreen) {
             mScreenLocked.setText(resId);
             mSelector.setVisibility(View.GONE); // cannot unlock
             mEmergencyCallText[subscription].setVisibility(View.VISIBLE);
             mEmergencyCallButton.setVisibility(View.VISIBLE);
        } else {
             mSelector.setVisibility(View.VISIBLE);
        }
    }

    private void updateLayoutForPersoText() {
        mScreenLocked.setText(R.string.lockscreen_instructions_when_pattern_disabled);

        // layout
        mScreenLocked.setVisibility(View.VISIBLE);
        mSelector.setVisibility(View.VISIBLE);
        mEmergencyCallButton.setVisibility(View.GONE);
    }

    static CharSequence getCarrierString(CharSequence telephonyPlmn, CharSequence telephonySpn) {
        if (telephonyPlmn != null && telephonySpn == null) {
            return telephonyPlmn;
        } else if (telephonyPlmn != null && telephonySpn != null) {
            return telephonyPlmn + "|" + telephonySpn;
        } else if (telephonyPlmn == null && telephonySpn != null) {
            return telephonySpn;
        } else {
            return "";
        }
    }

    public void onSimStateChanged(IccCard.State simState, int subscription) {
        Log.d(TAG, "onSimStateChanged : simState = " + simState + ", subscription = " + subscription);
        mStatus[subscription] = getCurrentStatus(simState);
        updateLayout(mStatus[subscription], subscription);
        updateStatusLines();
    }

    void updateConfiguration() {
        Configuration newConfig = getResources().getConfiguration();
        if (newConfig.orientation != mCreationOrientation) {
            mCallback.recreateMe(newConfig);
        } else if (newConfig.hardKeyboardHidden != mKeyboardHidden) {
            mKeyboardHidden = newConfig.hardKeyboardHidden;
            final boolean isKeyboardOpen = mKeyboardHidden == Configuration.HARDKEYBOARDHIDDEN_NO;
            if (mUpdateMonitor.isKeyguardBypassEnabled() && isKeyboardOpen) {
                mCallback.goToUnlockScreen();
            }
        }
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (LockPatternKeyguardView.DEBUG_CONFIGURATION) {
            Log.v(TAG, "***** LOCK ATTACHED TO WINDOW");
            Log.v(TAG, "Cur orient=" + mCreationOrientation
                    + ", new config=" + getResources().getConfiguration());
        }
        updateConfiguration();
    }

    /** {@inheritDoc} */
    @Override
    protected void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        if (LockPatternKeyguardView.DEBUG_CONFIGURATION) {
            Log.w(TAG, "***** LOCK CONFIG CHANGING", new RuntimeException());
            Log.v(TAG, "Cur orient=" + mCreationOrientation
                    + ", new config=" + newConfig);
        }
        updateConfiguration();
    }

    /** {@inheritDoc} */
    public boolean needsInput() {
        return false;
    }

    /** {@inheritDoc} */
    public void onPause() {

    }

    /** {@inheritDoc} */
    public void onResume() {
        resetStatusInfo(mUpdateMonitor);
        mLockPatternUtils.updateEmergencyCallButtonState(mEmergencyCallButton);
    }

    /** {@inheritDoc} */
    public void cleanUp() {
        mUpdateMonitor.removeCallback(this); // this must be first
        mLockPatternUtils = null;
        mUpdateMonitor = null;
        mCallback = null;
    }

    /** {@inheritDoc} */
    public void onRingerModeChanged(int state) {
        boolean silent = AudioManager.RINGER_MODE_NORMAL != state;
        if (silent != mSilentMode) {
            mSilentMode = silent;
            updateRightTabResources();
        }
    }

    public void onPhoneStateChanged(String newState) {
        mLockPatternUtils.updateEmergencyCallButtonState(mEmergencyCallButton);
    }
}
