/*
 * Copyright (C) 2007 The Android Open Source Project
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

package com.android.internal.telephony;

import android.os.Bundle;
import java.util.List;
import android.telephony.NeighboringCellInfo;
import com.android.internal.telephony.QosSpec;


/**
 * Interface used to interact with the phone.  Mostly this is used by the
 * TelephonyManager class.  A few places are still using this directly.
 * Please clean them up if possible and use TelephonyManager insteadl.
 *
 * {@hide}
 */
interface ITelephony {

    /**
     * Dial a number. This doesn't place the call. It displays
     * the Dialer screen.
     * @param number the number to be dialed. If null, this
     * would display the Dialer screen with no number pre-filled.
     */
    void dial(String number);

    /**
     * Dial a number. This doesn't place the call. It displays
     * the Dialer screen for that subscription.
     * @param number the number to be dialed. If null, this
     * would display the Dialer screen with no number pre-filled.
     * @param subscription user preferred subscription.
     */
    void dialOnSubscription(String number, int subscription);

    /**
     * Place a call to the specified number.
     * @param number the number to be called.
     */
    void call(String number);

    /**
     * Place a call to the specified number on particular subscription.
     * @param number the number to be called.
     * @param subscription user preferred subscription.
     */
    void callOnSubscription(String number, int subscription);

    /**
     * If there is currently a call in progress, show the call screen.
     * The DTMF dialpad may or may not be visible initially, depending on
     * whether it was up when the user last exited the InCallScreen.
     *
     * @return true if the call screen was shown.
     */
    boolean showCallScreen();

    /**
     * Variation of showCallScreen() that also specifies whether the
     * DTMF dialpad should be initially visible when the InCallScreen
     * comes up.
     *
     * @param showDialpad if true, make the dialpad visible initially,
     *                    otherwise hide the dialpad initially.
     * @return true if the call screen was shown.
     *
     * @see showCallScreen
     */
    boolean showCallScreenWithDialpad(boolean showDialpad);

    /**
     * End call if there is a call in progress, otherwise does nothing.
     *
     * @return whether it hung up
     */
    boolean endCall();

    /**
     * End call on particular subscription or go to the Home screen
     * @param subscription user preferred subscription.
     * @return whether it hung up
     */
    boolean endCallOnSubscription(int subscription);

    /**
     * Answer the currently-ringing call.
     *
     * If there's already a current active call, that call will be
     * automatically put on hold.  If both lines are currently in use, the
     * current active call will be ended.
     *
     * TODO: provide a flag to let the caller specify what policy to use
     * if both lines are in use.  (The current behavior is hardwired to
     * "answer incoming, end ongoing", which is how the CALL button
     * is specced to behave.)
     *
     * TODO: this should be a oneway call (especially since it's called
     * directly from the key queue thread).
     */
    void answerRingingCall();

    /**
     * Answer the currently-ringing call on particular subscription.
     *
     * If there's already a current active call, that call will be
     * automatically put on hold.  If both lines are currently in use, the
     * current active call will be ended.
     *
     * TODO: provide a flag to let the caller specify what policy to use
     * if both lines are in use.  (The current behavior is hardwired to
     * "answer incoming, end ongoing", which is how the CALL button
     * is specced to behave.)
     *
     * TODO: this should be a oneway call (especially since it's called
     * directly from the key queue thread).
     *
     * @param subscription user preferred subscription.
     */
    void answerRingingCallOnSubscription(int subscription);

    /**
     * Silence the ringer if an incoming call is currently ringing.
     * (If vibrating, stop the vibrator also.)
     *
     * It's safe to call this if the ringer has already been silenced, or
     * even if there's no incoming call.  (If so, this method will do nothing.)
     *
     * TODO: this should be a oneway call too (see above).
     *       (Actually *all* the methods here that return void can
     *       probably be oneway.)
     */
    void silenceRinger();

    /**
     * Check if we are in either an active or holding call
     * @return true if the phone state is OFFHOOK.
     */
    boolean isOffhook();

    /**
     * Check if a particular subscription has an active or holding call
     *
     * @param subscription user preferred subscription.
     * @return true if the phone state is OFFHOOK.
     */
    boolean isOffhookOnSubscription(int subscription);

    /**
     * Check if an incoming phone call is ringing or call waiting.
     * @return true if the phone state is RINGING.
     */
    boolean isRinging();

    /**
     * Check if an incoming phone call is ringing or call waiting
     * on a particular subscription.
     *
     * @param subscription user preferred subscription.
     * @return true if the phone state is RINGING.
     */
    boolean isRingingOnSubscription(int subscription);

    /**
     * Check if the phone is idle.
     * @return true if the phone state is IDLE.
     */
    boolean isIdle();

    /**
     * Check if the phone is idle on a particular subscription.
     *
     * @param subscription user preferred subscription.
     * @return true if the phone state is IDLE.
     */
    boolean isIdleOnSubscription(int subscription);

    /**
     * Check to see if the radio is on or not.
     * @return returns true if the radio is on.
     */
    boolean isRadioOn();

    /**
     * Check to see if the radio is on or not on particular subscription.
     * @param subscription user preferred subscription.
     * @return returns true if the radio is on.
     */
    boolean isRadioOnOnSubscription(int subscription);

    /**
     * Check if the SIM pin lock is enabled.
     * @return true if the SIM pin lock is enabled.
     */
    boolean isSimPinEnabled();

    /**
     * Check if the SIM pin lock is enable
     * for particular subscription.
     * @param subscription user preferred subscription.
     * @return true if the SIM pin lock is enabled.
     */
    boolean isSimPinEnabledOnSubscription(int subscription);

    /**
     * Cancels the missed calls notification.
     */
    void cancelMissedCallsNotification();

    /**
     * Cancels the missed calls notification on particular subscription.
     * @param subscription user preferred subscription.
     */
    void cancelMissedCallsNotificationOnSubscription(int subscription);

    /**
     * Supply a pin to unlock the SIM.  Blocks until a result is determined.
     * @param pin The pin to check.
     * @return whether the operation was a success.
     */
    boolean supplyPin(String pin);

    /**
     * Supply a pin to unlock the SIM for particular subscription.Blocks until a result is determined.
     * @param pin The pin to check.
     * @param subscription user preferred subscription.
     * @return whether the operation was a success.
     */
    boolean supplyPinOnSubscription(String pin, int subscription);

    /**
     * Handles PIN MMI commands (PIN/PIN2/PUK/PUK2), which are initiated
     * without SEND (so <code>dial</code> is not appropriate).
     *
     * @param dialString the MMI command to be executed.
     * @return true if MMI command is executed.
     */
    boolean handlePinMmi(String dialString);

    /**
     * Handles PIN MMI commands (PIN/PIN2/PUK/PUK2), which are initiated
     * without SEND (so <code>dial</code> is not appropriate) for
     * a particular subscription.
     * @param dialString the MMI command to be executed.
     * @param subscription user preferred subscription.
     * @return true if MMI command is executed.
     */
    boolean handlePinMmiOnSubscription(String dialString, int subscription);

    /**
     * Toggles the radio on or off.
     */
    void toggleRadioOnOff();

    /**
     * Toggles the radio on or off on particular subscription.
     * @param subscription user preferred subscription.
     */
    void toggleRadioOnOffOnSubscription(int subscription);

    /**
     * Set the radio to on or off
     */
    boolean setRadio(boolean turnOn);

    /**
     * Set the radio to on or off on particular subscription.
     * @param subscription user preferred subscription.
     */
    boolean setRadioOnSubscription(boolean turnOn, int subscription);

    /**
     * Sets the ril power off
     */
    void setRilPowerOff();

    /**
     * Request to update location information in service state
     */
    void updateServiceLocation();

    /**
     * Request to update location information for a subscrition in service state
     * @param subscription user preferred subscription.
     */
    void updateServiceLocationOnSubscription(int subscription);

    /**
     * Enable location update notifications.
     */
    void enableLocationUpdates();

    /**
     * Enable location update notifications for a particular subscription.
     * @param subscription user preferred subscription.
     */
    void enableLocationUpdatesOnSubscription(int subscription);

    /**
     * Disable location update notifications.
     */
    void disableLocationUpdates();

    /**
     * Disable location update notifications for a particular subscription.
     * @param subscription user preferred subscription.
     */
    void disableLocationUpdatesOnSubscription(int subscription);

    /**
     * Enable a specific APN type.
     */
    int enableApnType(String type);

    /**
     * Disable a specific APN type.
     */
    int disableApnType(String type);

    /**
     * Enable QoS
     * @param qosSpec QoS spec containing various QoS parameters
     * @param type Type of data connection (any of Phone.APN_TYPE_*)
     * @param ipv IPVersion of the connection QoS is requested on
     * @return Phone.QOS_REQUEST_SUCCESS - success, else failure
     * {@hide}
     */
    int enableQos(in QosSpec qosSpec, String type, String ipv);

    /**
     * Disable QoS
     * @param qosId QoS Identifier
     * @return Phone.QOS_REQUEST_SUCCESS - success, else failure
     * {@hide}
     */
    int disableQos(int qosId);

    /**
     * Modify QoS
     * @param qosId QoS Identifier
     * @return Phone.QOS_REQUEST_SUCCESS - success, else failure
     * {@hide}
     */
    int modifyQos(int qosId, in QosSpec qosSpec);

    /**
     * Suspend QoS
     * @param qosId QoS Identifier
     * @return Phone.QOS_REQUEST_SUCCESS - success, else failure
     * {@hide}
     */
    int suspendQos(int qosId);

    /**
     * Resume QoS
     * @param qosId QoS Identifier
     * @return Phone.QOS_REQUEST_SUCCESS - success, else failure
     * {@hide}
     */
    int resumeQos(int qosId);

    /**
     * Get QoS
     * @param qosId QoS Identifier
     * @return Phone.QOS_REQUEST_SUCCESS - success, else failure
     * {@hide}
     */
    int getQosStatus(int qosId);

    /**
     * Allow mobile data connections.
     */
    boolean enableDataConnectivity();

    /**
     * Disallow mobile data connections.
     */
    boolean disableDataConnectivity();

    /**
     * Report whether data connectivity is possible.
     */
    boolean isDataConnectivityPossible();

    Bundle getCellLocation();

    Bundle getCellLocationOnSubscription(int subscription);

    /**
     * Returns the neighboring cell information of the device.
     */
    List<NeighboringCellInfo> getNeighboringCellInfo();

    /**
     * Returns the neighboring cell information of the device on particular subscription.
     * @param subscription user preferred subscription.
     */
    List<NeighboringCellInfo> getNeighboringCellInfoOnSubscription(int subscription);

    /**
     * Returns the call state.
     */
     int getCallState();

    /**
     * Returns the call state for a subscription.
     */
     int getCallStateOnSubscription(int subscription);

     int getDataActivity();

     int getDataState();

    /**
     * Returns the current active phone type as integer.
     * Returns TelephonyManager.PHONE_TYPE_CDMA if RILConstants.CDMA_PHONE
     * and TelephonyManager.PHONE_TYPE_GSM if RILConstants.GSM_PHONE
     */
    int getActivePhoneType();

    /**
     * Returns the current active phone type as integer for particular subscription.
     * Returns TelephonyManager.PHONE_TYPE_CDMA if RILConstants.CDMA_PHONE
     * and TelephonyManager.PHONE_TYPE_GSM if RILConstants.GSM_PHONE
     * @param subscription user preferred subscription.
     */
    int getActivePhoneTypeOnSubscription(int subscription);

    /**
     * Sends a OEM request to the RIL and returns the response back to the
     * Caller. The returnValue is negative on failure. 0 or length of response on SUCCESS
     */
    int sendOemRilRequestRaw(in byte[] request, out byte[] response);

    /**
     * Returns the CDMA ERI icon index to display
     */
    int getCdmaEriIconIndex();

    /**
     * Returns the CDMA ERI icon index to display on particular subscription.
     * @param subscription user preferred subscription.
     */
    int getCdmaEriIconIndexOnSubscription(int subscription);

    /**
     * Returns the CDMA ERI icon mode,
     * 0 - ON
     * 1 - FLASHING
     */
    int getCdmaEriIconMode();

    /**
     * Returns the CDMA ERI icon mode on particular subscription,
     * 0 - ON
     * 1 - FLASHING
     * @param subscription user preferred subscription.
     */
    int getCdmaEriIconModeOnSubscription(int subscription);

    /**
     * Returns the CDMA ERI text,
     */
    String getCdmaEriText();

    /**
     * Returns the CDMA ERI text for particular subscription,
     * @param subscription user preferred subscription.
     */
    String getCdmaEriTextOnSubscription(int subscription);

    /**
     * Returns true if CDMA provisioning needs to run.
     */
    boolean getCdmaNeedsProvisioning();

    /**
     * Returns true if CDMA provisioning needs to run
     * on particular subscription.
     * @param subscription user preferred subscription.
     */
    boolean getCdmaNeedsProvisioningOnSubscription(int subscription);

    /**
      * Returns the unread count of voicemails
      */
    int getVoiceMessageCount();

    /**
     * Returns the unread count of voicemails for a subscription.
     * @param subscription user preferred subscription.
     * Returns the unread count of voicemails
     */
    int getVoiceMessageCountOnSubscription(int subscription);

    /**
      * Returns the network type
      */
    int getNetworkType();

    /**
     * Returns the network type of a subscription.
     * @param subscription user preferred subscription.
     * Returns the network type
     */
    int getNetworkTypeOnSubscription(int subscription);

    /**
     * Checks whether the modem is in power save mode
     * {@hide}
     */
    boolean isModemPowerSave();

    /**
     * Checks whether the modem is in power save mode
     * for a particular subsciption.
     * @param subscription user preferred subscription.
     * {@hide}
     */
    boolean isModemPowerSaveOnSubscription(int subscription);

    /**
     * Return true if an ICC card is present
     */
    boolean hasIccCard();

   /**
     * Returns Interface Name used by specified apnType on the specified ip version.
     * apnType and ipv strings should match the strings defined in the phone interface.
     */
    String getActiveInterfaceName(String apnType, String ipv);

    /**
     * Returns Ip address used by specified apnType on the specified ip version.
     * apnType and ipv strings should match the strings defined in the phone interface.
     */
    String getActiveIpAddress(String apnType, String ipv);

    /**
     * Returns Gateway address used by specified apnType on the specified ip version.
     * apnType and ipv strings should match the strings defined in the phone interface.
     */
    String getActiveGateway(String apnType, String ipv);

    /**
     * Return true if an ICC card is present for a subscription.
     * @param subscription user preferred subscription.
     * Return true if an ICC card is present
     */
    boolean hasIccCardOnSubscription(int subscription);

    /**
     * Gets the number of attempts remaining for PIN1/PUK1 unlock.
     */
    int getIccPin1RetryCount();

    /**
     * Gets the number of attempts remaining for PIN1/PUK1 unlock
     * for a subscription.
     * @param subscription user preferred subscription.
     * Gets the number of attempts remaining for PIN1/PUK1 unlock.
     */
    int getIccPin1RetryCountOnSubscription(int subscription);

    boolean isSimPukLockedOnSubscription(int subscription);

    /**
     * get default subscription
     * @return subscription id
     */
    int getDefaultSubscription();

    /**
     * get user prefered voice subscription
     * @return subscription id
     */
    int getPreferredVoiceSubscription();

    /**
     * get user prefered data subscription
     * @return subscription id
     */
    int getPreferredDataSubscription();

    /*
     * Set user prefered data subscription
     * @return true if success
     */
    boolean setPreferredDataSubscription(int subscription);

    /**
     * Modify data readiness checks performed during data call setup
     *
     * @param checkConnectivity - check for network state in service,
                                  roaming and data in roaming enabled.
     * @param checkSubscription - check for icc/nv ready and icc records loaded.
     * @param tryDataCalls - set to true to attempt data calls if data call is not already active.
     *
     */
    void setDataReadinessChecks(
            boolean checkConnectivity, boolean checkSubscription, boolean tryDataCalls);

    /**
     * Sets the transmit power
     *
     * @param power - Specifies the max transmit power that is allowed
     */
    void setTransmitPower(int powerLevel);
}

