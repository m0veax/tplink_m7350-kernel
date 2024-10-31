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

package android.telephony;

import android.annotation.SdkConstant;
import android.annotation.SdkConstant.SdkConstantType;
import android.content.Context;
import android.os.Bundle;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.os.SystemProperties;
import android.provider.Settings;
import android.provider.Settings.SettingNotFoundException;
import android.text.TextUtils;
import android.util.Log;

import com.android.internal.net.IPVersion;
import com.android.internal.telephony.IPhoneSubInfo;
import com.android.internal.telephony.ITelephony;
import com.android.internal.telephony.ITelephonyRegistry;
import com.android.internal.telephony.Phone;
import com.android.internal.telephony.PhoneFactory;
import com.android.internal.telephony.RILConstants;
import com.android.internal.telephony.TelephonyProperties;

import java.util.List;

/**
 * Provides access to information about the telephony services on
 * the device. Applications can use the methods in this class to
 * determine telephony services and states, as well as to access some
 * types of subscriber information. Applications can also register
 * a listener to receive notification of telephony state changes.
 * <p>
 * You do not instantiate this class directly; instead, you retrieve
 * a reference to an instance through
 * {@link android.content.Context#getSystemService
 * Context.getSystemService(Context.TELEPHONY_SERVICE)}.
 * <p>
 * Note that access to some telephony information is
 * permission-protected. Your application cannot access the protected
 * information unless it has the appropriate permissions declared in
 * its manifest file. Where permissions apply, they are noted in the
 * the methods through which you access the protected information.
 */
public class TelephonyManager {
    private static final String TAG = "TelephonyManager";

    private Context mContext;
    private ITelephonyRegistry mRegistry;

    /** @hide */
    private static int mPhoneCount = 1; // Phone count is set to 1 by default(Single Standby).
    /** @hide */
    private static final int MAX_PHONE_COUNT_DS = 2; // No. of phones for Dual Subscription.
    /** @hide */
    public static final int DEFAULT_SUB = 0;

    /** @hide */
    public TelephonyManager(Context context) {
        mContext = context;
        mRegistry = ITelephonyRegistry.Stub.asInterface(ServiceManager.getService(
                    "telephony.registry"));
    }

    /** @hide */
    private TelephonyManager() {
        if (isMultiSimEnabled()) {
            mPhoneCount = MAX_PHONE_COUNT_DS;
        }
    }

    private static TelephonyManager sInstance = new TelephonyManager();

    /** @hide */
    public static TelephonyManager getDefault() {
        return sInstance;
    }

    /**
     * Returns the number of phones available.
     * Returns 1 for Single standby mode (Single SIM functionality)
     * Returns 2 for Dual standby mode.(Dual SIM functionality)
     *
     * @hide
    */
    public static int getPhoneCount() {
         return mPhoneCount;
    }

    /**
     * returns the property value if set, otherwise initialized to false.
     * @hide
    */
     public static boolean isMultiSimEnabled() {
         //returns the property value if set, otherwise initialized to false.
         return SystemProperties.getBoolean("persist.dsds.enabled", false);
     }

    //
    // Broadcast Intent actions
    //

    /**
     * Broadcast intent action indicating that the call state (cellular)
     * on the device has changed.
     *
     * <p>
     * The {@link #EXTRA_STATE} extra indicates the new call state.
     * If the new state is RINGING, a second extra
     * {@link #EXTRA_INCOMING_NUMBER} provides the incoming phone number as
     * a String.
     *
     * <p class="note">
     * Requires the READ_PHONE_STATE permission.
     *
     * <p class="note">
     * This was a {@link android.content.Context#sendStickyBroadcast sticky}
     * broadcast in version 1.0, but it is no longer sticky.
     * Instead, use {@link #getCallState} to synchronously query the current call state.
     *
     * @see #EXTRA_STATE
     * @see #EXTRA_INCOMING_NUMBER
     * @see #getCallState
     */
    @SdkConstant(SdkConstantType.BROADCAST_INTENT_ACTION)
    public static final String ACTION_PHONE_STATE_CHANGED =
            "android.intent.action.PHONE_STATE";

    /**
     * The lookup key used with the {@link #ACTION_PHONE_STATE_CHANGED} broadcast
     * for a String containing the new call state.
     *
     * @see #EXTRA_STATE_IDLE
     * @see #EXTRA_STATE_RINGING
     * @see #EXTRA_STATE_OFFHOOK
     *
     * <p class="note">
     * Retrieve with
     * {@link android.content.Intent#getStringExtra(String)}.
     */
    public static final String EXTRA_STATE = Phone.STATE_KEY;

    /**
     * Value used with {@link #EXTRA_STATE} corresponding to
     * {@link #CALL_STATE_IDLE}.
     */
    public static final String EXTRA_STATE_IDLE = Phone.State.IDLE.toString();

    /**
     * Value used with {@link #EXTRA_STATE} corresponding to
     * {@link #CALL_STATE_RINGING}.
     */
    public static final String EXTRA_STATE_RINGING = Phone.State.RINGING.toString();

    /**
     * Value used with {@link #EXTRA_STATE} corresponding to
     * {@link #CALL_STATE_OFFHOOK}.
     */
    public static final String EXTRA_STATE_OFFHOOK = Phone.State.OFFHOOK.toString();

    /**
     * The lookup key used with the {@link #ACTION_PHONE_STATE_CHANGED} broadcast
     * for a String containing the incoming phone number.
     * Only valid when the new call state is RINGING.
     *
     * <p class="note">
     * Retrieve with
     * {@link android.content.Intent#getStringExtra(String)}.
     */
    public static final String EXTRA_INCOMING_NUMBER = "incoming_number";


    //
    //
    // Device Info
    //
    //

    /**
     * Returns the software version number for the device, for example,
     * the IMEI/SV for GSM phones. Return null if the software version is
     * not available.
     *
     * <p>Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     */
    public String getDeviceSoftwareVersion() {
        return getDeviceSoftwareVersion(getDefaultSubscription());
    }

    /**
     * Returns the software version number for the device of a subscription,
     * for example, the IMEI/SV for GSM phones. Return null if the software
     * version is not available.
     *
     * <p>Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     *
     * @param subscription for which software version has to be received
     * @hide
     */
    public String getDeviceSoftwareVersion(int subscription) {
        try {
            return getSubscriberInfo().getDeviceSvnOnSubscription(subscription);
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            return null;
        }
    }

    /**
     * Returns the unique device ID, for example, the IMEI for GSM and the MEID
     * or ESN for CDMA phones. Return null if device ID is not available.
     *
     * <p>Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     */
    public String getDeviceId() {
        return getDeviceId(getDefaultSubscription());
    }

    /**
     * Returns the unique device ID of a subscription, for example, the IMEI for
     * GSM and the MEID for CDMA phones. Return null if device ID is not available.
     *
     * <p>Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     *
     * @param subscription of which deviceID is returned
     * @hide
     */
    public String getDeviceId(int subscription) {
        try {
            return getSubscriberInfo().getDeviceIdOnSubscription(subscription);
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            return null;
        }
    }

    /**
     * Returns the current location of the device.
     * Return null if current location is not available.
     *
     * <p>Requires Permission:
     * {@link android.Manifest.permission#ACCESS_COARSE_LOCATION ACCESS_COARSE_LOCATION} or
     * {@link android.Manifest.permission#ACCESS_COARSE_LOCATION ACCESS_FINE_LOCATION}.
     */
    public CellLocation getCellLocation() {
        return getCellLocation(getDefaultSubscription());
    }

     /**
     * Returns the current location of the devicei of a subscription.
     * Return null if current location is not available.
     *
     * <p>Requires Permission:
     * {@link android.Manifest.permission#ACCESS_COARSE_LOCATION ACCESS_COARSE_LOCATION} or
     * {@link android.Manifest.permission#ACCESS_COARSE_LOCATION ACCESS_FINE_LOCATION}.
     *
     * @param subscription of which the device current location is returned
     * @hide
     */
     public CellLocation getCellLocation(int subscription) {
        try {
            Bundle bundle = getITelephony().getCellLocationOnSubscription(subscription);
            CellLocation cl = CellLocation.newFromBundle(bundle);
            if (cl.isEmpty())
                return null;
            return cl;
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            return null;
        }
    }

    /**
     * Enables location update notifications.  {@link PhoneStateListener#onCellLocationChanged
     * PhoneStateListener.onCellLocationChanged} will be called on location updates.
     *
     * <p>Requires Permission: {@link android.Manifest.permission#CONTROL_LOCATION_UPDATES
     * CONTROL_LOCATION_UPDATES}
     *
     * @hide
     */
    public void enableLocationUpdates() {
        enableLocationUpdates(getDefaultSubscription());
    }

    /**
     * Enables location update notifications for a subscription.
     * {@link PhoneStateListener#onCellLocationChanged
     * PhoneStateListener.onCellLocationChanged} will be called on location updates.
     *
     * <p>Requires Permission: {@link android.Manifest.permission#CONTROL_LOCATION_UPDATES
     * CONTROL_LOCATION_UPDATES}
     *
     * @param subscription for which the location updates are enabled
     * @hide
     */
    public void enableLocationUpdates(int subscription) {
        try {
            getITelephony().enableLocationUpdatesOnSubscription(subscription);
        } catch (RemoteException ex) {
        } catch (NullPointerException ex) {
        }
    }

    /**
     * Disables location update notifications.  {@link PhoneStateListener#onCellLocationChanged
     * PhoneStateListener.onCellLocationChanged} will be called on location updates.
     *
     * <p>Requires Permission: {@link android.Manifest.permission#CONTROL_LOCATION_UPDATES
     * CONTROL_LOCATION_UPDATES}
     *
     * @hide
     */
    public void disableLocationUpdates() {
        disableLocationUpdates(getDefaultSubscription());
    }

    /**
     * Disables location update notifications for a subscription.
     * {@link PhoneStateListener#onCellLocationChanged
     * PhoneStateListener.onCellLocationChanged} will be called on location updates.
     *
     * <p>Requires Permission: {@link android.Manifest.permission#CONTROL_LOCATION_UPDATES
     * CONTROL_LOCATION_UPDATES}
     *
     * @param subscription for which the location updates are disabled
     * @hide
     */
    public void disableLocationUpdates(int subscription) {
        try {
            getITelephony().disableLocationUpdatesOnSubscription(subscription);
        } catch (RemoteException ex) {
        } catch (NullPointerException ex) {
        }
    }

    /**
     * Returns the neighboring cell information of the device.
     *
     * @return List of NeighboringCellInfo or null if info unavailable.
     *
     * <p>Requires Permission:
     * (@link android.Manifest.permission#ACCESS_COARSE_UPDATES}
     */
    public List<NeighboringCellInfo> getNeighboringCellInfo() {
        return getNeighboringCellInfo(getDefaultSubscription());
    }

    /**
     * Returns the neighboring cell information of the device for a subscription.
     *
     * @return List of NeighboringCellInfo or null if info unavailable.
     *
     * <p>Requires Permission:
     * (@link android.Manifest.permission#ACCESS_COARSE_UPDATES}
     *
     * @param subscription for which neighbouring cell info has to be returned
     * @hide
     */
    public List<NeighboringCellInfo> getNeighboringCellInfo(int subscription) {
        try {
            return getITelephony().getNeighboringCellInfoOnSubscription(subscription);
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            return null;
        }
    }

    /** No phone radio. */
    public static final int PHONE_TYPE_NONE = Phone.PHONE_TYPE_NONE;
    /** Phone radio is GSM. */
    public static final int PHONE_TYPE_GSM = Phone.PHONE_TYPE_GSM;
    /** Phone radio is CDMA. */
    public static final int PHONE_TYPE_CDMA = Phone.PHONE_TYPE_CDMA;

    /**
     * Returns a constant indicating the device phone type.
     *
     * @see #PHONE_TYPE_NONE
     * @see #PHONE_TYPE_GSM
     * @see #PHONE_TYPE_CDMA
     */
    public int getPhoneType() {
        return getPhoneType(getDefaultSubscription());
    }

    /**
     * Returns a constant indicating the device phone type for a subscription.
     *
     * @see #PHONE_TYPE_NONE
     * @see #PHONE_TYPE_GSM
     * @see #PHONE_TYPE_CDMA
     *
     * @param subscription for which phone type is returned
     * @hide
     */
    public int getPhoneType(int subscription) {
        try{
            ITelephony telephony = getITelephony();
            if (telephony != null) {
                return telephony.getActivePhoneTypeOnSubscription(subscription);
            } else {
                // This can happen when the ITelephony interface is not up yet.
                return getPhoneTypeFromProperty();
            }
        } catch (RemoteException ex) {
            // This shouldn't happen in the normal case, as a backup we
            // read from the system property.
            return getPhoneTypeFromProperty();
        } catch (NullPointerException ex) {
            // This shouldn't happen in the normal case, as a backup we
            // read from the system property.
            return getPhoneTypeFromProperty();
        }
    }

    /**
     * Returns Default subscription.
     * Returns default value 0, if default subscription is not available
     *
     * @hide
     */
    public static int getDefaultSubscription() {
        ITelephony iTelephony = null;
        try {
            iTelephony = ITelephony.Stub.asInterface(ServiceManager.getService(Context.TELEPHONY_SERVICE));
            return iTelephony.getDefaultSubscription();
        } catch (RemoteException ex) {
            return DEFAULT_SUB;
        } catch (NullPointerException ex) {
            return DEFAULT_SUB;
        }
    }

    /**
     * Returns the designated data subscription.
     *
     * @hide
     */
    public static int getPreferredDataSubscription() {
        ITelephony iTelephony = null;
        try {
            iTelephony = ITelephony.Stub.asInterface(ServiceManager.getService(Context.TELEPHONY_SERVICE));
            return iTelephony.getPreferredDataSubscription();
        } catch (RemoteException ex) {
            return DEFAULT_SUB;
        } catch (NullPointerException ex) {
            return DEFAULT_SUB;
        }
    }

    /**
     * Sets the designated data subscription.
     *
     * @hide
     */
    public static boolean setPreferredDataSubscription(int subscription) {
        ITelephony iTelephony = null;
        try {
            iTelephony = ITelephony.Stub.asInterface(ServiceManager.getService(Context.TELEPHONY_SERVICE));
            return iTelephony.setPreferredDataSubscription(subscription);
        } catch (RemoteException ex) {
            return false;
        } catch (NullPointerException ex) {
            return false;
        }
    }

    /**
     * Returns the preferred voice subscription.
     *
     * @hide
     */
    public static int getPreferredVoiceSubscription() {
        ITelephony iTelephony = null;
        try {
            iTelephony = ITelephony.Stub.asInterface(ServiceManager.getService(Context.TELEPHONY_SERVICE));
            return iTelephony.getPreferredVoiceSubscription();
        } catch (RemoteException ex) {
            return DEFAULT_SUB;
        } catch (NullPointerException ex) {
            return DEFAULT_SUB;
        }
    }

    private int getPhoneTypeFromProperty() {
        return getPhoneTypeFromProperty(getDefaultSubscription());
    }

    private int getPhoneTypeFromProperty(int subscription) {
        String type =
            getTelephonyProperty
                (TelephonyProperties.CURRENT_ACTIVE_PHONE, subscription, null);
        if (type != null) {
            return (Integer.parseInt(type));
        } else {
            return getPhoneTypeFromNetworkType();
        }
    }

    private int getPhoneTypeFromNetworkType() {
        // When the system property CURRENT_ACTIVE_PHONE, has not been set,
        // use the system property for default network type.
        // This is a fail safe, and can only happen at first boot.
        int mode = SystemProperties.getInt("ro.telephony.default_network", -1);
        if (mode == -1)
            return PHONE_TYPE_NONE;
        return PhoneFactory.getPhoneType(mode);
    }
    //
    //
    // Current Network
    //
    //

    /**
     * Returns the alphabetic name of current registered operator.
     * <p>
     * Availability: Only when user is registered to a network. Result may be
     * unreliable on CDMA networks (use {@link #getPhoneType()} to determine if
     * on a CDMA network).
     */
    public String getNetworkOperatorName() {
        return getNetworkOperatorName(getDefaultSubscription());
    }

    /**
     * Returns the alphabetic name of current registered operator
     * for a particular subscription.
     * <p>
     * Availability: Only when user is registered to a network. Result may be
     * unreliable on CDMA networks (use {@link #getPhoneType()} to determine if
     * on a CDMA network).
     *
     * @hide
     */
    public String getNetworkOperatorName(int subscription) {
        return
            getTelephonyProperty(TelephonyProperties.PROPERTY_OPERATOR_ALPHA, subscription, "");
    }

    /**
     * Returns the numeric name (MCC+MNC) of current registered operator.
     * <p>
     * Availability: Only when user is registered to a network. Result may be
     * unreliable on CDMA networks (use {@link #getPhoneType()} to determine if
     * on a CDMA network).
     */
    public String getNetworkOperator() {
        return getNetworkOperator(getDefaultSubscription());
    }

    /**
     * Returns the numeric name (MCC+MNC) of current registered operator
     * for a particular subscription.
     * <p>
     * Availability: Only when user is registered to a network. Result may be
     * unreliable on CDMA networks (use {@link #getPhoneType()} to determine if
     * on a CDMA network).
     *
     * @hide
     */
     public String getNetworkOperator(int subscription) {
         return
             getTelephonyProperty(TelephonyProperties.PROPERTY_OPERATOR_NUMERIC, subscription, "");
     }

    /**
     * Returns true if the device is considered roaming on the current
     * network, for GSM purposes.
     * <p>
     * Availability: Only when user registered to a network.
     */
    public boolean isNetworkRoaming() {
        return isNetworkRoaming(getDefaultSubscription());
    }

    /**
     * Returns true if the device is considered roaming on the current
     * network for a subscription.
     * <p>
     * Availability: Only when user registered to a network.
     *
     * @hide
     */
    public boolean isNetworkRoaming(int subscription) {
        return
            "true".equals
            (getTelephonyProperty(TelephonyProperties.PROPERTY_OPERATOR_ISROAMING, subscription, null));
    }
    /**
     * Returns the ISO country code equivalent of the current registered
     * operator's MCC (Mobile Country Code).
     * <p>
     * Availability: Only when user is registered to a network. Result may be
     * unreliable on CDMA networks (use {@link #getPhoneType()} to determine if
     * on a CDMA network).
     */
    public String getNetworkCountryIso() {
        return getNetworkCountryIso(getDefaultSubscription());
    }

    /**
     * Returns the ISO country code equivalent of the current registered
     * operator's MCC (Mobile Country Code) of a subscription.
     * <p>
     * Availability: Only when user is registered to a network. Result may be
     * unreliable on CDMA networks (use {@link #getPhoneType()} to determine if
     * on a CDMA network).
     *
     * @hide
     */
    public String getNetworkCountryIso(int subscription) {
        return
            getTelephonyProperty(TelephonyProperties.PROPERTY_OPERATOR_ISO_COUNTRY, subscription, "");
    }

    /** Network type is unknown */
    public static final int NETWORK_TYPE_UNKNOWN = 0;
    /** Current network is GPRS */
    public static final int NETWORK_TYPE_GPRS = 1;
    /** Current network is EDGE */
    public static final int NETWORK_TYPE_EDGE = 2;
    /** Current network is UMTS */
    public static final int NETWORK_TYPE_UMTS = 3;
    /** Current network is CDMA: Either IS95A or IS95B*/
    public static final int NETWORK_TYPE_CDMA = 4;
    /** Current network is EVDO revision 0*/
    public static final int NETWORK_TYPE_EVDO_0 = 5;
    /** Current network is EVDO revision A*/
    public static final int NETWORK_TYPE_EVDO_A = 6;
    /** Current network is 1xRTT*/
    public static final int NETWORK_TYPE_1xRTT = 7;
    /** Current network is HSDPA */
    public static final int NETWORK_TYPE_HSDPA = 8;
    /** Current network is HSUPA */
    public static final int NETWORK_TYPE_HSUPA = 9;
    /** Current network is HSPA */
    public static final int NETWORK_TYPE_HSPA = 10;
    /** Current network is iDen */
    public static final int NETWORK_TYPE_IDEN = 11;
    /** Current network is EVDO revision B*/
    public static final int NETWORK_TYPE_EVDO_B = 12;
    /** @hide */
    public static final int NETWORK_TYPE_LTE = 13;
    /** @hide */
    public static final int NETWORK_TYPE_EHRPD = 14;

    /**
     * Returns a constant indicating the radio technology (network type)
     * currently in use on the device.
     * @return the network type
     *
     * @see #NETWORK_TYPE_UNKNOWN
     * @see #NETWORK_TYPE_GPRS
     * @see #NETWORK_TYPE_EDGE
     * @see #NETWORK_TYPE_UMTS
     * @see #NETWORK_TYPE_HSDPA
     * @see #NETWORK_TYPE_HSUPA
     * @see #NETWORK_TYPE_HSPA
     * @see #NETWORK_TYPE_CDMA
     * @see #NETWORK_TYPE_EVDO_0
     * @see #NETWORK_TYPE_EVDO_A
     * @see #NETWORK_TYPE_EVDO_B
     * @see #NETWORK_TYPE_1xRTT
     * @see #NETWORK_TYPE_EHRPD
     * @see #NETWORK_TYPE_LTE
     */
    public int getNetworkType() {
        return getNetworkType(getPreferredDataSubscription());
    }

    /**
     * Returns a constant indicating the radio technology (network type)
     * currently in use on the device for a subscription.
     * @return the network type
     *
     * @param subscription for which network type is returned
     * @hide
     *
     * @see #NETWORK_TYPE_UNKNOWN
     * @see #NETWORK_TYPE_GPRS
     * @see #NETWORK_TYPE_EDGE
     * @see #NETWORK_TYPE_UMTS
     * @see #NETWORK_TYPE_HSDPA
     * @see #NETWORK_TYPE_HSUPA
     * @see #NETWORK_TYPE_HSPA
     * @see #NETWORK_TYPE_CDMA
     * @see #NETWORK_TYPE_EVDO_0
     * @see #NETWORK_TYPE_EVDO_A
     * @see #NETWORK_TYPE_EVDO_B
     * @see #NETWORK_TYPE_1xRTT
     * @see #NETWORK_TYPE_EHRPD
     * @see #NETWORK_TYPE_LTE
     */
    public int getNetworkType(int subscription) {
        try{
            ITelephony telephony = getITelephony();
            if (telephony != null) {
                return telephony.getNetworkTypeOnSubscription(subscription);
            } else {
                // This can happen when the ITelephony interface is not up yet.
                return NETWORK_TYPE_UNKNOWN;
            }
        } catch(RemoteException ex) {
            // This shouldn't happen in the normal case
            return NETWORK_TYPE_UNKNOWN;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            return NETWORK_TYPE_UNKNOWN;
        }
    }

    /**
     * Returns a string representation of the radio technology (network type)
     * currently in use on the device.
     * @return the name of the radio technology
     *
     * @hide pending API council review
     */
    public String getNetworkTypeName() {
        return getNetworkTypeName(getPreferredDataSubscription());
    }

    /**
     * Returns a string representation of the radio technology (network type)
     * currently in use on the device.
     * @param subscription for which network type is returned
     * @return the name of the radio technology
     *
     * @hide pending API council review
     */
    public String getNetworkTypeName(int subscription) {
        switch (getNetworkType(subscription)) {
            case NETWORK_TYPE_GPRS:
                return "GPRS";
            case NETWORK_TYPE_EDGE:
                return "EDGE";
            case NETWORK_TYPE_UMTS:
                return "UMTS";
            case NETWORK_TYPE_HSDPA:
                return "HSDPA";
            case NETWORK_TYPE_HSUPA:
                return "HSUPA";
            case NETWORK_TYPE_HSPA:
                return "HSPA";
            case NETWORK_TYPE_CDMA:
                return "CDMA";
            case NETWORK_TYPE_EVDO_0:
                return "CDMA - EvDo rev. 0";
            case NETWORK_TYPE_EVDO_A:
                return "CDMA - EvDo rev. A";
            case NETWORK_TYPE_EVDO_B:
                return "CDMA - EvDo rev. B";
            case NETWORK_TYPE_1xRTT:
                return "CDMA - 1xRTT";
            case NETWORK_TYPE_EHRPD:
                return "CDMA - EHRPD";
            case NETWORK_TYPE_LTE:
                return "LTE";
            default:
                return "UNKNOWN";
        }
    }

    //
    //
    // SIM Card
    //
    //

    /** SIM card state: Unknown. Signifies that the SIM is in transition
     *  between states. For example, when the user inputs the SIM pin
     *  under PIN_REQUIRED state, a query for sim status returns
     *  this state before turning to SIM_STATE_READY. */
    public static final int SIM_STATE_UNKNOWN = 0;
    /** SIM card state: no SIM card is available in the device */
    public static final int SIM_STATE_ABSENT = 1;
    /** SIM card state: Locked: requires the user's SIM PIN to unlock */
    public static final int SIM_STATE_PIN_REQUIRED = 2;
    /** SIM card state: Locked: requires the user's SIM PUK to unlock */
    public static final int SIM_STATE_PUK_REQUIRED = 3;
    /** SIM card state: Locked: requries a network PIN to unlock */
    public static final int SIM_STATE_NETWORK_LOCKED = 4;
    /** SIM card state: Ready */
    public static final int SIM_STATE_READY = 5;
    /** SIM card state: SIM Card Error, Sim Card is present but faulty
     *@hide
     */
    public static final int SIM_STATE_CARD_IO_ERROR = 6;
    /** SIM card state: Locked: requries a network subset PIN to unlock
     * @hide
     */
    public static final int ICC_STATE_SIM_NETWORK_SUBSET_LOCKED = 7;
    /** ICC card state: Locked: requries a SIM corporate PIN to unlock
     * @hide
     */
    public static final int ICC_STATE_SIM_CORPORATE_LOCKED = 8;
    /** ICC card state: Locked: requries a SIM service provider PIN to unlock
     * @hide
     */
    public static final int ICC_STATE_SIM_SERVICE_PROVIDER_LOCKED = 9;
    /** ICC card state: Locked: requries a SIM SIM PIN to unlock
     * @hide
     */
    public static final int ICC_STATE_SIM_SIM_LOCKED = 10;
    /** ICC card state: Locked: requries a RUIM network1 PIN to unlock
     * @hide
     */
    public static final int ICC_STATE_RUIM_NETWORK1_LOCKED = 11;
    /** ICC card state: Locked: requries a RUIM network2 PIN to unlock
     * @hide
     */
    public static final int ICC_STATE_RUIM_NETWORK2_LOCKED = 12;
    /** ICC card state: Locked: requries a RUIM hrpd PIN to unlock
     * @hide
     */
    public static final int ICC_STATE_RUIM_HRPD_LOCKED = 13;
    /** ICC card state: Locked: requries a RUIM corporate PIN to unlock
     * @hide
     */
    public static final int ICC_STATE_RUIM_CORPORATE_LOCKED = 14;
    /** ICC card state: Locked: requries a RUIM service provider PIN to unlock
     * @hide
     */
    public static final int ICC_STATE_RUIM_SERVICE_PROVIDER_LOCKED = 15;
    /** ICC card state: Locked: requries a RUIM RUIM PIN to unlock
     * @hide
     */
    public static final int ICC_STATE_RUIM_RUIM_LOCKED = 16;

    /**
     * @return true if a ICC card is present
     */
    public boolean hasIccCard() {
        return hasIccCard(getDefaultSubscription());
    }

    /**
     * @return true if a ICC card is present for a subscription
     *
     * @param subscription for which icc card presence is checked
     * @hide
     */
    public boolean hasIccCard(int subscription) {
        try {
            return getITelephony().hasIccCardOnSubscription(subscription);
        } catch (RemoteException ex) {
            // Assume no ICC card if remote exception which shouldn't happen
            return false;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            return false;
        }
    }

     /**
     * Returns a constant indicating the state of the
     * device SIM card present in a slot.
     *
     * @see #SIM_STATE_UNKNOWN
     * @see #SIM_STATE_ABSENT
     * @see #SIM_STATE_PIN_REQUIRED
     * @see #SIM_STATE_PUK_REQUIRED
     * @see #SIM_STATE_NETWORK_LOCKED
     * @see #SIM_STATE_READY
     * @see #SIM_STATE_CARD_IO_ERROR
     * @see #ICC_STATE_SIM_NETWORK_SUBSET_LOCKED
     * @see #ICC_STATE_SIM_CORPORATE_LOCKED
     * @see #ICC_STATE_SIM_SERVICE_PROVIDER_LOCKED
     * @see #ICC_STATE_SIM_SIM_LOCKED
     * @see #ICC_STATE_RUIM_NETWORK1_LOCKED
     * @see #ICC_STATE_RUIM_NETWORK2_LOCKED
     * @see #ICC_STATE_RUIM_HRPD_LOCKED
     * @see #ICC_STATE_RUIM_CORPORATE_LOCKED
     * @see #ICC_STATE_RUIM_SERVICE_PROVIDER_LOCKED
     * @see #ICC_STATE_RUIM_RUIM_LOCKED
     */
    public int getSimState() {
        return getSimState(getDefaultSubscription());
    }


    /** {@hide} */
    public String getActiveInterfaceName(String apnType, IPVersion ipv) {
        try{
            ITelephony telephony = getITelephony();
            if (telephony != null) {
                String ifName = telephony.getActiveInterfaceName(apnType, ipv.toString());
                if(TextUtils.isEmpty(ifName)){
                    Log.i(TAG,"interface name is null or empty");
                    return null;
                } else {
                    return ifName;
                }
            } else {
                // This can happen when the ITelephony interface is not up yet.
                return null;
            }
        } catch(RemoteException ex) {
            // This shouldn't happen in the normal case
            return null;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            Log.i(TAG,"null pointer access");
            return null;
        }
    }


    /** {@hide} */
    public String getActiveIpAddress(String apnType, IPVersion ipv) {
        try{
            ITelephony telephony = getITelephony();
            if (telephony != null) {
                String ipAddr = telephony.getActiveIpAddress(apnType, ipv.toString());
                if(TextUtils.isEmpty(ipAddr)){
                    Log.i(TAG,"ipAddr is null or empty");
                    return null;
                } else {
                    return ipAddr;
                }
            } else {
                // This can happen when the ITelephony interface is not up yet.
                return null;
            }
        } catch(RemoteException ex) {
            // This shouldn't happen in the normal case
            return null;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            Log.i(TAG,"null pointer access");
            return null;
        }
    }

    /** {@hide} */
    public String getActiveGateway(String apnType, IPVersion ipv) {
        try{
            ITelephony telephony = getITelephony();
            if (telephony != null) {
                String gatewayAddr = telephony.getActiveGateway(apnType, ipv.toString());
                if(TextUtils.isEmpty(gatewayAddr)){
                    Log.i(TAG,"gatewayAddr is null or empty");
                    return null;
                } else {
                    return gatewayAddr;
                }
            } else {
                // This can happen when the ITelephony interface is not up yet.
                return null;
            }
        } catch(RemoteException ex) {
            // This shouldn't happen in the normal case
            return null;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            Log.i(TAG,"null pointer access");
            return null;
        }
    }

    /**
     * Returns a constant indicating the state of the
     * device SIM card.
     *
     * @see #SIM_STATE_UNKNOWN
     * @see #SIM_STATE_ABSENT
     * @see #SIM_STATE_PIN_REQUIRED
     * @see #SIM_STATE_PUK_REQUIRED
     * @see #SIM_STATE_NETWORK_LOCKED
     * @see #SIM_STATE_READY
     * @see #SIM_STATE_CARD_IO_ERROR
     * @see #ICC_STATE_SIM_NETWORK_SUBSET_LOCKED
     * @see #ICC_STATE_SIM_CORPORATE_LOCKED
     * @see #ICC_STATE_SIM_SERVICE_PROVIDER_LOCKED
     * @see #ICC_STATE_SIM_SIM_LOCKED
     * @see #ICC_STATE_RUIM_NETWORK1_LOCKED
     * @see #ICC_STATE_RUIM_NETWORK2_LOCKED
     * @see #ICC_STATE_RUIM_HRPD_LOCKED
     * @see #ICC_STATE_RUIM_CORPORATE_LOCKED
     * @see #ICC_STATE_RUIM_SERVICE_PROVIDER_LOCKED
     * @see #ICC_STATE_RUIM_RUIM_LOCKED
     * @hide
     */
    public int getSimState(int slotId) {
        String prop =
            getTelephonyProperty(TelephonyProperties.PROPERTY_SIM_STATE, slotId, "");
        if ("ABSENT".equals(prop)) {
            return SIM_STATE_ABSENT;
        } else if ("PIN_REQUIRED".equals(prop)) {
            return SIM_STATE_PIN_REQUIRED;
        } else if ("PUK_REQUIRED".equals(prop)) {
            return SIM_STATE_PUK_REQUIRED;
        } else if ("NETWORK_LOCKED".equals(prop)) {
            return SIM_STATE_NETWORK_LOCKED;
        } else if ("READY".equals(prop)) {
            return SIM_STATE_READY;
        } else if ("CARD_IO_ERROR".equals(prop)) {
            return SIM_STATE_CARD_IO_ERROR;
        } else if ("SIM_NETWORK_SUBSET_LOCKED".equals(prop)) {
            return ICC_STATE_SIM_NETWORK_SUBSET_LOCKED;
        } else if ("SIM_CORPORATE_LOCKED".equals(prop)) {
            return ICC_STATE_SIM_CORPORATE_LOCKED;
        } else if ("SIM_SERVICE_PROVIDER_LOCKED".equals(prop)) {
            return ICC_STATE_SIM_SERVICE_PROVIDER_LOCKED;
        } else if ("SIM_SIM_LOCKED".equals(prop)) {
            return ICC_STATE_SIM_SIM_LOCKED;
        } else if ("RUIM_NETWORK1_LOCKED".equals(prop)) {
            return ICC_STATE_RUIM_NETWORK1_LOCKED;
        } else if ("RUIM_NETWORK2_LOCKED".equals(prop)) {
            return ICC_STATE_RUIM_NETWORK2_LOCKED;
        } else if ("RUIM_HRPD_LOCKED".equals(prop)) {
            return ICC_STATE_RUIM_HRPD_LOCKED;
        } else if ("RUIM_CORPORATE_LOCKED".equals(prop)) {
            return ICC_STATE_RUIM_CORPORATE_LOCKED;
        } else if ("RUIM_SERVICE_PROVIDER_LOCKED".equals(prop)) {
            return ICC_STATE_RUIM_SERVICE_PROVIDER_LOCKED;
        } else if ("RUIM_RUIM_LOCKED".equals(prop)) {
            return ICC_STATE_RUIM_RUIM_LOCKED;
        } else {
            return SIM_STATE_UNKNOWN;
        }
    }

    /**
     * Returns the MCC+MNC (mobile country code + mobile network code) of the
     * provider of the SIM. 5 or 6 decimal digits.
     * <p>
     * Availability: SIM state must be {@link #SIM_STATE_READY}
     *
     * @see #getSimState
     */
    public String getSimOperator() {
        return getSimOperator(getDefaultSubscription());
    }

    /**
     * Returns the MCC+MNC (mobile country code + mobile network code) of the
     * provider of the SIM for a particular subscription. 5 or 6 decimal digits.
     * <p>
     * Availability: SIM state must be {@link #SIM_STATE_READY}
     *
     * @see #getSimState
     *
     * @param subscription for which provider's MCC+MNC is returned
     * @hide
     */
    public String getSimOperator(int subscription) {
        return getTelephonyProperty
            (TelephonyProperties.PROPERTY_ICC_OPERATOR_NUMERIC, subscription, "");
    }

    /**
     * Returns the Service Provider Name (SPN).
     * <p>
     * Availability: SIM state must be {@link #SIM_STATE_READY}
     *
     * @see #getSimState
     */
    public String getSimOperatorName() {
        return getSimOperatorName(getDefaultSubscription());
    }

    /**
     * Returns the Service Provider Name (SPN) of a subscription.
     * <p>
     * Availability: SIM state must be {@link #SIM_STATE_READY}
     *
     * @see #getSimState
     *
     * @hide
     */
    public String getSimOperatorName(int subscription) {
        return
            getTelephonyProperty(TelephonyProperties.PROPERTY_ICC_OPERATOR_ALPHA, subscription, "");
    }

    /**
     * Returns the ISO country code equivalent for the SIM provider's country code.
     */
    public String getSimCountryIso() {
        return getSimCountryIso(getDefaultSubscription());
    }

    /**
     * Returns the ISO country code equivalent for the SIM provider's country code
     * of a subscription.
     *
     * @hide
     */
     public String getSimCountryIso(int subscription) {
         return getTelephonyProperty
             (TelephonyProperties.PROPERTY_ICC_OPERATOR_ISO_COUNTRY, subscription, "");
     }

    /**
     * Returns the serial number of the SIM, if applicable. Return null if it is
     * unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     */
    public String getSimSerialNumber() {
        return getSimSerialNumber(getDefaultSubscription());
    }

    /**
     * Returns the serial number of the SIM, if applicable for a subscription.
     * Return null if it is unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     *
     * @param subscription for which Sim Serial number is returned
     * @hide
     */
    public String getSimSerialNumber(int subscription) {
        try {
            return getSubscriberInfo().getIccSerialNumberOnSubscription(subscription);
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            return null;
        }
    }

    //
    //
    // Subscriber Info
    //
    //

    /**
     * Returns the unique subscriber ID, for example, the IMSI for a GSM phone.
     * Return null if it is unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     */
    public String getSubscriberId() {
        return getSubscriberId(getDefaultSubscription());
    }

    /**
     * Returns the unique subscriber ID, for example, the IMSI for a GSM phone
     * for a subscription.
     * Return null if it is unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     *
     * @param subscription whose subscriber id is returned
     * @hide
     */
    public String getSubscriberId(int subscription) {
        try {
            return getSubscriberInfo().getSubscriberIdOnSubscription(subscription);
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            return null;
        }
    }

    /**
     * Returns the phone number string for line 1, for example, the MSISDN
     * for a GSM phone. Return null if it is unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     */
    public String getLine1Number() {
        return getLine1Number(getDefaultSubscription());
    }

    /**
     * Returns the phone number string for line 1, for example, the MSISDN
     * for a GSM phone for a particular subscription. Return null if it is unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     *
     * @param subscription whose phone number for line 1 is returned
     * @hide
     */
    public String getLine1Number(int subscription) {
        try {
            return getSubscriberInfo().getLine1NumberOnSubscription(subscription);
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            return null;
        }
    }

    /**
     * Returns the alphabetic identifier associated with the line 1 number.
     * Return null if it is unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     * @hide
     * nobody seems to call this.
     */
    public String getLine1AlphaTag() {
        return getLine1AlphaTag(getDefaultSubscription());
    }

    /**
     * Returns the alphabetic identifier associated with the line 1 number
     * for a subscription.
     * Return null if it is unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     * @param subscription whose alphabetic identifier associated with line 1 is returned
     * @hide
     * nobody seems to call this.
     */
    public String getLine1AlphaTag(int subscription) {
        try {
            return getSubscriberInfo().getLine1AlphaTagOnSubscription(subscription);
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            return null;
        }
    }

    /**
     * Returns the voice mail number. Return null if it is unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     */
    public String getVoiceMailNumber() {
        return getVoiceMailNumber(getDefaultSubscription());
    }

    /**
     * Returns the voice mail number for a subscription.
     * Return null if it is unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     * @param subscription whose voice mail number is returned
     * @hide
     */
    public String getVoiceMailNumber(int subscription) {
        try {
            return getSubscriberInfo().getVoiceMailNumberOnSubscription(subscription);
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            return null;
        }
    }

    /**
     * Returns the complete voice mail number. Return null if it is unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#CALL_PRIVILEGED CALL_PRIVILEGED}
     *
     * @hide
     */
    public String getCompleteVoiceMailNumber() {
        try {
            return getSubscriberInfo().getCompleteVoiceMailNumber();
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            return null;
        }
    }

    /**
     * Returns the complete voice mail number. Return null if it is unavailable.
     * <p>
     * Requires Permission:
     * {@link android.Manifest.permission#CALL_PRIVILEGED CALL_PRIVILEGED}
     *
     *@hide
     */
    public String getCompleteVoiceMailNumber(int subscription) {
        try {
            return getSubscriberInfo().getCompleteVoiceMailNumberOnSubscription(subscription);
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            return null;
            }
        }

    /**
     * Returns the voice mail count. Return 0 if unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     * @param subscription whose voice mail count is returned
     * @hide
     */
    public int getVoiceMessageCount() {
        return getVoiceMessageCount(getDefaultSubscription());
    }

    /**
     * Returns the voice mail count for a subscription. Return 0 if unavailable.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     * @param subscription whose voice message count is returned
     * @hide
     */
    public int getVoiceMessageCount(int subscription) {
        try {
            return getITelephony().getVoiceMessageCountOnSubscription(subscription);
        } catch (RemoteException ex) {
            return 0;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            return 0;
        }
    }

    /**
     * Retrieves the alphabetic identifier associated with the voice
     * mail number.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     */
    public String getVoiceMailAlphaTag() {
        return getVoiceMailAlphaTag(getDefaultSubscription());
    }

    /**
     * Retrieves the alphabetic identifier associated with the voice
     * mail number for a subscription.
     * <p>
     * Requires Permission:
     *   {@link android.Manifest.permission#READ_PHONE_STATE READ_PHONE_STATE}
     * @param subscription whose alphabetic identifier associated with the voice mail number is returned
     * @hide
     */
    public String getVoiceMailAlphaTag(int subscription) {
        try {
            return getSubscriberInfo().getVoiceMailAlphaTagOnSubscription(subscription);
        } catch (RemoteException ex) {
            return null;
        } catch (NullPointerException ex) {
            // This could happen before phone restarts due to crashing
            return null;
        }
    }

    private IPhoneSubInfo getSubscriberInfo() {
        // get it each time because that process crashes a lot
        return IPhoneSubInfo.Stub.asInterface(ServiceManager.getService("iphonesubinfo"));
    }


    /** Device call state: No activity. */
    public static final int CALL_STATE_IDLE = 0;
    /** Device call state: Ringing. A new call arrived and is
     *  ringing or waiting. In the latter case, another call is
     *  already active. */
    public static final int CALL_STATE_RINGING = 1;
    /** Device call state: Off-hook. At least one call exists
      * that is dialing, active, or on hold, and no calls are ringing
      * or waiting. */
    public static final int CALL_STATE_OFFHOOK = 2;

    /**
     * Returns a constant indicating the call state (cellular) on the device.
     */
    public int getCallState() {
        return getCallState(getDefaultSubscription());
    }

    /**
     * Returns a constant indicating the call state (cellular) on the device
     * for a subscription.
     *
     * @param subscription whose call state is returned
     * @hide
     */
    public int getCallState(int subscription) {
        try {
            return getITelephony().getCallStateOnSubscription(subscription);
        } catch (RemoteException ex) {
            // the phone process is restarting.
            return CALL_STATE_IDLE;
        } catch (NullPointerException ex) {
          // the phone process is restarting.
          return CALL_STATE_IDLE;
      }
    }

    /** Data connection activity: No traffic. */
    public static final int DATA_ACTIVITY_NONE = 0x00000000;
    /** Data connection activity: Currently receiving IP PPP traffic. */
    public static final int DATA_ACTIVITY_IN = 0x00000001;
    /** Data connection activity: Currently sending IP PPP traffic. */
    public static final int DATA_ACTIVITY_OUT = 0x00000002;
    /** Data connection activity: Currently both sending and receiving
     *  IP PPP traffic. */
    public static final int DATA_ACTIVITY_INOUT = DATA_ACTIVITY_IN | DATA_ACTIVITY_OUT;
    /**
     * Data connection is active, but physical link is down
     */
    public static final int DATA_ACTIVITY_DORMANT = 0x00000004;

    /**
     * Returns a constant indicating the type of activity on a data connection
     * (cellular).
     *
     * @see #DATA_ACTIVITY_NONE
     * @see #DATA_ACTIVITY_IN
     * @see #DATA_ACTIVITY_OUT
     * @see #DATA_ACTIVITY_INOUT
     * @see #DATA_ACTIVITY_DORMANT
     */
    public int getDataActivity() {
        try {
            return getITelephony().getDataActivity();
        } catch (RemoteException ex) {
            // the phone process is restarting.
            return DATA_ACTIVITY_NONE;
        } catch (NullPointerException ex) {
          // the phone process is restarting.
          return DATA_ACTIVITY_NONE;
      }
    }

    /** Data connection state: Disconnected. IP traffic not available. */
    public static final int DATA_DISCONNECTED   = 0;
    /** Data connection state: Currently setting up a data connection. */
    public static final int DATA_CONNECTING     = 1;
    /** Data connection state: Connected. IP traffic should be available. */
    public static final int DATA_CONNECTED      = 2;
    /** Data connection state: Suspended. The connection is up, but IP
     * traffic is temporarily unavailable. For example, in a 2G network,
     * data activity may be suspended when a voice call arrives. */
    public static final int DATA_SUSPENDED      = 3;

    /**
     * Returns a constant indicating the current data connection state
     * (cellular).
     *
     * @see #DATA_DISCONNECTED
     * @see #DATA_CONNECTING
     * @see #DATA_CONNECTED
     * @see #DATA_SUSPENDED
     */
    public int getDataState() {
        try {
            return getITelephony().getDataState();
        } catch (RemoteException ex) {
            // the phone process is restarting.
            return DATA_DISCONNECTED;
        } catch (NullPointerException ex) {
            return DATA_DISCONNECTED;
        }
    }

    private ITelephony getITelephony() {
        return ITelephony.Stub.asInterface(ServiceManager.getService(Context.TELEPHONY_SERVICE));
    }

    //
    //
    // PhoneStateListener
    //
    //

    /**
     * Registers a listener object to receive notification of changes
     * in specified telephony states.
     * <p>
     * To register a listener, pass a {@link PhoneStateListener}
     * and specify at least one telephony state of interest in
     * the events argument.
     *
     * At registration, and when a specified telephony state
     * changes, the telephony manager invokes the appropriate
     * callback method on the listener object and passes the
     * current (udpated) values.
     * <p>
     * To unregister a listener, pass the listener object and set the
     * events argument to
     * {@link PhoneStateListener#LISTEN_NONE LISTEN_NONE} (0).
     *
     * @param listener The {@link PhoneStateListener} object to register
     *                 (or unregister)
     * @param events The telephony state(s) of interest to the listener,
     *               as a bitwise-OR combination of {@link PhoneStateListener}
     *               LISTEN_ flags.
     */
    public void listen(PhoneStateListener listener, int events) {
        String pkgForDebug = mContext != null ? mContext.getPackageName() : "<unknown>";
        try {
            Boolean notifyNow = (getITelephony() != null);
            mRegistry.listenOnSubscription(pkgForDebug, listener.callback, events, notifyNow,
                                           listener.mSubscription);
        } catch (RemoteException ex) {
            // system process dead
        } catch (NullPointerException ex) {
            // system process dead
        }
    }

    /**
     * Returns the CDMA ERI icon index to display
     *
     * @hide
     */
    public int getCdmaEriIconIndex() {
        return getCdmaEriIconIndex(getDefaultSubscription());
    }

    /**
     * Returns the CDMA ERI icon index to display for a subscription
     *
     * @hide
     */
    public int getCdmaEriIconIndex(int subscription) {
        try {
            return getITelephony().getCdmaEriIconIndexOnSubscription(subscription);
        } catch (RemoteException ex) {
            // the phone process is restarting.
            return -1;
        } catch (NullPointerException ex) {
            return -1;
        }
    }

    /**
     * Returns the CDMA ERI icon mode,
     * 0 - ON
     * 1 - FLASHING
     *
     * @hide
     */
    public int getCdmaEriIconMode() {
        return getCdmaEriIconMode(getDefaultSubscription());
    }

    /**
     * Returns the CDMA ERI icon mode for a subscription.
     * 0 - ON
     * 1 - FLASHING
     *
     * @hide
     */
    public int getCdmaEriIconMode(int subscription) {
        try {
            return getITelephony().getCdmaEriIconModeOnSubscription(subscription);
        } catch (RemoteException ex) {
            // the phone process is restarting.
            return -1;
        } catch (NullPointerException ex) {
            return -1;
        }
    }

    /**
     * Returns the CDMA ERI text,
     *
     * @hide
     */
    public String getCdmaEriText() {
        return getCdmaEriText(getDefaultSubscription());
    }

    /**
     * Returns the CDMA ERI text, of a subscription
     *
     * @hide
     */
    public String getCdmaEriText(int subscription) {
        try {
            return getITelephony().getCdmaEriTextOnSubscription(subscription);
        } catch (RemoteException ex) {
            // the phone process is restarting.
            return null;
        } catch (NullPointerException ex) {
            return null;
        }
    }

    /**
     * Sets the telephony property with the value specified.
     *
     * @hide
     */
    public static void setTelephonyProperty(String property, int index, String value) {
        String propVal = "";
        String p[] = null;
        String prop = SystemProperties.get(property);

        if (prop != null) {
            p = prop.split(",");
        }

        for (int i = 0; i < index; i++) {
            String str = "";
            if ((p != null) && (i < p.length)) {
                str = p[i];
            }
            propVal = propVal + str + ",";
        }

        propVal = propVal + value;
        if (p != null) {
            for (int i = index+1; i < p.length; i++) {
                propVal = propVal + "," + p[i];
            }
        }
        try {
            SystemProperties.set(property, propVal);
        } catch (IllegalArgumentException e) {
            Log.e(TAG,"Propval length is greaterthan 91");
        }
    }

    /**
     * Gets the telephony property.
     *
     * @hide
     */
    public static String getTelephonyProperty(String property, int index, String defaultVal) {
        String propVal = null;
        String prop = SystemProperties.get(property);

        if ((prop != null) && (prop.length() > 0)) {
            String values[] = prop.split(",");
            if ((index >= 0) && (index < values.length) && (values[index] != null)) {
                propVal = values[index];
            }
        }
        return propVal == null ? defaultVal : propVal;
    }
}

