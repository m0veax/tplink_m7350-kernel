/*
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

import static com.android.internal.telephony.RILConstants.*;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;


import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.net.NetworkInfo;
import android.net.wifi.WifiManager;
import android.net.IConnectivityManager;
import android.os.AsyncResult;
import android.os.Handler;
import android.os.Message;
import android.os.Registrant;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.os.IBinder;
import android.os.ServiceManager;
import android.os.RegistrantList;
import android.os.RemoteException;
import android.preference.PreferenceManager;
import android.provider.Settings;
import android.provider.Settings.SettingNotFoundException;
import android.telephony.CellLocation;
import android.telephony.ServiceState;
import android.telephony.TelephonyManager;
import android.telephony.cdma.CdmaCellLocation;
import android.telephony.gsm.GsmCellLocation;
import android.text.TextUtils;
import android.util.EventLog;
import android.util.Log;

import com.android.internal.net.IPVersion;
import com.android.internal.telephony.EventLogTags;
import com.android.internal.telephony.CommandsInterface.RadioTechnology;
import com.android.internal.telephony.DataProfile.DataProfileType;
import com.android.internal.telephony.Phone.BearerType;
import com.android.internal.telephony.Phone.DataActivityState;
import com.android.internal.telephony.cdma.CdmaSubscriptionSourceManager;
import com.android.internal.telephony.ProxyManager.Subscription;
import com.android.internal.telephony.QosSpec;

/*
 * Definitions:
 * - DataProfile(dp) :Information required to setup a connection (ex. ApnSetting)
 * - DataService(ds) : A particular feature requested by connectivity service, (MMS, GPS) etc.
 *                     also called APN Type
 * - DataConnection(dc) : Underlying PDP connection, associated with a dp.
 * - DataProfileTracker(dpt): Keeps track of services enabled, active, and dc that supports them
 * - DataServiceStateTracker(dsst) : Keeps track of network registration states, radio access tech
 *                                   roaming indications etc.
 *
 * What we know:
 * - A set of service types that needs to be enabled
 * - Data profiles needed to establish the service type
 * - Each Data profile will also tell us whether IPv4/IPv6 is possible with that data profile
 * - Priorities of services. (this can be used if MPDP is not supported, or limited # of pdp)
 * - DataServiceStateTracker will tell us the network state and preferred data radio technology
 * - dsst also keeps track of sim/ruim loaded status
 * - Desired power state
 * - For each service type, it is possible that same APN can handle ipv4 and ipv6. It is
 *   also possible that there are different APNs. This is handled.
 *
 * What we don't know:
 * - We don't know if the underlying network will support IPV6 or not.
 * - We don't know if the underlying network will support MPDP or not (even in 3GPP)
 * - If nw does support mpdp, we dont know how many pdp sessions it can handle
 * - We don't know how many PDP sessions/interfaces modem can handle
 * - We don't know if modem can disconnect existing calls in favor of new ones
 *   based on some profile priority.
 * - We don't know if IP continuity is possible or not possible across technologies.
 * - It may not pe possible to determine whether network is EVDO or EHRPD by looking at the
 *   data registration state messages. So, if this is an EHRPD capable device, then we will have
 *   to use APN if available, or fall back to NAI.
 *
 * What we assume:
 * - Modem will not tear down the data call if IP continuity is possible.
 * - A separate dataConnection exists for IPV4 and IPV6, even
 *   though it is possible that both use the same underlying rmnet
 *   interface and pdp connection as is the case in dual stack v4/v6.
 * - If modem is aware of service priority, then these priorities are in sync
 *   with what is mentioned here, or we might end up in an infinite setup/disconnect
 *   cycle!
 *
 *
 * State Handling:
 * - states are associated with <service type, ip version> tuple.
 * - this is to handle scenario such as follows,
 *   default data might be connected on ipv4,  but we might be scanning different
 *   apns for default data on ipv6
 */

public class MMDataConnectionTracker extends DataConnectionTracker {

    private static final String LOG_TAG = "DATA";

    private static final int DATA_CONNECTION_POOL_SIZE = 8;

    private static final String INTENT_RECONNECT_ALARM = "com.android.internal.telephony.gprs-reconnect";
    private static final String INTENT_RECONNECT_ALARM_EXTRA_REASON = "reason";
    private static final String INTENT_RECONNECT_ALARM_SERVICE_TYPE = "ds";
    private static final String INTENT_RECONNECT_ALARM_IP_VERSION = "ipv";

    /**
     * Constants for the data connection activity:
     * physical link down/up
     */
     private static final int DATA_CONNECTION_ACTIVE_PH_LINK_INACTIVE = 0;
     private static final int DATA_CONNECTION_ACTIVE_PH_LINK_DOWN = 1;
     private static final int DATA_CONNECTION_ACTIVE_PH_LINK_UP = 2;

    /**
     * Partial Status codes for activeDCs
     */
     private static final int PARTIAL_STATUS_ADDR_ADDED = 0;
     private static final int PARTIAL_STATUS_ADDR_REMOVED = 1;
     private static final int PARTIAL_STATUS_ADDR_UPDATED = 2;

    // ServiceStateTracker to keep track of network service state
    DataServiceStateTracker mDsst;

    boolean isDctActive = true;

    // keeps track of data statistics activity
    private DataNetStatistics mPollNetStat;

    // keeps track of wifi status - TODO: WHY?
    private boolean mIsWifiConnected = false;

    // Intent sent when the reconnect alarm fires.
    private PendingIntent mReconnectIntent = null;

    //following flags are used in isReadyForData()
    private boolean mNoAutoAttach = false;
    private boolean mIsPsRestricted = false;
    private boolean mDesiredPowerState = true;

    /* list of messages that are waiting to be posted, when data call disconnect
     * is complete
     */
    ArrayList <Message> mDisconnectAllCompleteMsgList = new ArrayList<Message>();

    // if this property is set to false, android is not going to issue ANY ipv4 data calls.
    private static final boolean SUPPORT_IPV4 = SystemProperties.getBoolean(
            "persist.telephony.support_ipv4", true);

    // if this property is set to false, android is not going to issue ANY ipv6 data calls.
    private static final boolean SUPPORT_IPV6 = SystemProperties.getBoolean(
            "persist.telephony.support_ipv6", true);

    // if this property is set to false, android is not going to issue ANY ipv4v6 data calls.
    private static final boolean SUPPORT_IPV4V6 = SystemProperties.getBoolean(
            "persist.telephony.support_dual", false);

    /*
     * If this property is set to true then android assumes that multiple PDN is
     * going to be supported in modem/nw. However if second PDN requests fails,
     * then behavior is going to be determined by the
     * SUPPORT_SERVICE_ARBITRATION property below. If MPDN is set to false, then
     * android will ensure that the higher priority service is active. Low
     * priority data calls may be pro-actively torn down to ensure this.
     */
    private static final boolean SUPPORT_MPDN =
        SystemProperties.getBoolean("persist.telephony.mpdn", true);

    /*
     * if this property is set to true, and if multi PDN support is enabled
     * (above), then android is going to disconnect data calls that support low
     * priority services if setup data call requests for high priority services
     * fails. Low priority services will be re-attempted after high priority
     * services come up. Property should have no effect if MPDN is not enabled.
     */
    private static final boolean SUPPORT_MPDN_SERVICE_ARBITRATION =
        SystemProperties.getBoolean("persist.telephony.ds.arbit", false);

    //used for NV+CDMA
    String mCdmaHomeOperatorNumeric = null;

    private int mDisconnectPendingCount = 0;
    private boolean mDataCallSetupPending = false;

    private CdmaSubscriptionSourceManager mCdmaSSM = null;

    /*
     * context to make sure the onUpdateDataConnections doesn't get executed
     * over and over again unnecessarily.
     */
    int mUpdateDataConnectionsContext = 0;

    /**
     * mDataCallList holds all the Data connection,
     */
    private ArrayList<DataConnection> mDataConnectionList;

    private RegistrantList mAllDataDisconnectedRegistrants = new RegistrantList();

    BroadcastReceiver mIntentReceiver = new BroadcastReceiver() {
        @Override
        public synchronized void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            logv("intent received :" + action);
            if (action.equals(Intent.ACTION_SCREEN_ON)) {
                mPollNetStat.notifyScreenState(true);
                stopNetStatPoll();
                startNetStatPoll();

            } else if (action.equals(Intent.ACTION_SCREEN_OFF)) {
                mPollNetStat.notifyScreenState(false);
                stopNetStatPoll();
                startNetStatPoll();

            } else if (action.startsWith((INTENT_RECONNECT_ALARM))) {
                String reason = intent.getStringExtra(INTENT_RECONNECT_ALARM_EXTRA_REASON);
                DataServiceType ds = DataServiceType.valueOf(intent.getStringExtra(INTENT_RECONNECT_ALARM_SERVICE_TYPE));
                IPVersion ipv = IPVersion.valueOf(intent.getStringExtra(INTENT_RECONNECT_ALARM_IP_VERSION));
                /* set state as scanning so that updateDataConnections will process the data call */
                if (mDpt.getState(ds, ipv)==State.WAITING_ALARM)
                    mDpt.setState(State.SCANNING, ds, ipv);
                updateDataConnections(reason);
            } else if (action.equals(WifiManager.NETWORK_STATE_CHANGED_ACTION)) {
                final android.net.NetworkInfo networkInfo = (NetworkInfo) intent
                        .getParcelableExtra(WifiManager.EXTRA_NETWORK_INFO);
                mIsWifiConnected = (networkInfo != null && networkInfo.isConnected());

            } else if (action.equals(WifiManager.WIFI_STATE_CHANGED_ACTION)) {
                final boolean enabled = intent.getIntExtra(WifiManager.EXTRA_WIFI_STATE,
                        WifiManager.WIFI_STATE_UNKNOWN) == WifiManager.WIFI_STATE_ENABLED;
                if (!enabled) {
                    // when WIFI got disabled, the NETWORK_STATE_CHANGED_ACTION
                    // quit and wont report disconnected till next enabling.
                    mIsWifiConnected = false;
                }

            } else if (action.equals(TelephonyIntents.ACTION_VOICE_CALL_STARTED)) {
                sendMessage(obtainMessage(EVENT_VOICE_CALL_STARTED));

            } else if (action.equals(TelephonyIntents.ACTION_VOICE_CALL_ENDED)) {
                sendMessage(obtainMessage(EVENT_VOICE_CALL_ENDED));
            }
        }
    };

    protected MMDataConnectionTracker(Context context, PhoneNotifier notifier, CommandsInterface ci) {
        super(context, notifier, ci);

        mDsst = new DataServiceStateTracker(this, context, notifier, ci);
        mPollNetStat = new DataNetStatistics(this);

        // register for events.
        mCm.registerForOn(this, EVENT_RADIO_ON, null);
        mCm.registerForOffOrNotAvailable(this, EVENT_RADIO_OFF_OR_NOT_AVAILABLE, null);
        mCm.registerForDataStateChanged(this, EVENT_DATA_CALL_LIST_CHANGED, null);
        mCm.registerForTetheredModeStateChanged(this, EVENT_TETHERED_MODE_STATE_CHANGED, null);

        mDsst.registerForDataConnectionAttached(this, EVENT_DATA_CONNECTION_ATTACHED, null);
        mDsst.registerForDataConnectionDetached(this, EVENT_DATA_CONNECTION_DETACHED, null);
        mDsst.registerForRadioTechnologyChanged(this, EVENT_RADIO_TECHNOLOGY_CHANGED, null);

        mDsst.registerForDataRoamingOn(this, EVENT_ROAMING_ON, null);
        mDsst.registerForDataRoamingOff(this, EVENT_ROAMING_OFF, null);

        /* CDMA only */
        mCm.registerForCdmaOtaProvision(this, EVENT_CDMA_OTA_PROVISION, null);
        mCdmaSSM = CdmaSubscriptionSourceManager.getInstance(context, ci, new Registrant(this,
                EVENT_CDMA_SUBSCRIPTION_SOURCE_CHANGED, null));

        /* GSM only */
        mDsst.registerForPsRestrictedEnabled(this, EVENT_PS_RESTRICT_ENABLED, null);
        mDsst.registerForPsRestrictedDisabled(this, EVENT_PS_RESTRICT_DISABLED, null);

        /*
         * We let DSST worry about SIM/RUIM records to make life a little
         * simpler for us
         */
        mDsst.registerForRecordsLoaded(this, EVENT_RECORDS_LOADED, null);

        mDpt.registerForDataProfileDbChanged(this, EVENT_DATA_PROFILE_DB_CHANGED, null);

        IntentFilter filter = new IntentFilter();
        for (DataServiceType ds : DataServiceType.values()) {
            filter.addAction(getAlarmIntentName(ds, IPVersion.INET));
            filter.addAction(getAlarmIntentName(ds, IPVersion.INET6));
        }
        filter.addAction(Intent.ACTION_SCREEN_ON);
        filter.addAction(Intent.ACTION_SCREEN_OFF);
        filter.addAction(WifiManager.NETWORK_STATE_CHANGED_ACTION);
        filter.addAction(WifiManager.WIFI_STATE_CHANGED_ACTION);
        filter.addAction(TelephonyIntents.ACTION_VOICE_CALL_STARTED);
        filter.addAction(TelephonyIntents.ACTION_VOICE_CALL_ENDED);

        mContext.registerReceiver(mIntentReceiver, filter, null, this);

        createDataConnectionList();

        // This preference tells us 1) initial condition for "dataEnabled",
        // and 2) whether the RIL will setup the baseband to auto-PS
        // attach.
        SharedPreferences sp = PreferenceManager.getDefaultSharedPreferences(mContext);
        boolean dataDisabledOnBoot = sp.getBoolean(Phone.DATA_DISABLED_ON_BOOT_KEY, false);
        mDpt.setServiceTypeEnabled(DataServiceType.SERVICE_TYPE_DEFAULT, !dataDisabledOnBoot);
        mNoAutoAttach = dataDisabledOnBoot;

        /* On startup, check with ConnectivityService(CS) if mobile data has
         * been disabled from the phone settings.  CS processes this setting on
         * startup and disables all service types via the
         * PhoneInterfaceManager. In some cases the PhoneInterfaceManager is
         * not started in time for CS to disable the service types, so, double
         * checking here.
         */
        IBinder b = ServiceManager.getService(Context.CONNECTIVITY_SERVICE);
        IConnectivityManager service = IConnectivityManager.Stub.asInterface(b);

        try {
            if (service.getMobileDataEnabled() == false) {
                // Disable all data profiles
                for (DataServiceType ds : DataServiceType.values()) {
                    if (mDpt.isServiceTypeEnabled(ds)) {
                        mDpt.setServiceTypeEnabled(ds, false);
                        logd("Disabling ds" + ds);
                    }
                }
            }
        } catch (RemoteException e) {
            // Could not find ConnectivityService, nothing to do, continue.
            logw("Could not access Connectivity Service." + e);
        }

        //used in CDMA+NV case.
        mCdmaHomeOperatorNumeric = SystemProperties.get("ro.cdma.home.operator.numeric");

        logv("SUPPORT_IPV4 = " + SUPPORT_IPV4);
        logv("SUPPORT_IPV6 = " + SUPPORT_IPV6);
        logv("SUPPORT_IPV4V6 = " + SUPPORT_IPV4V6);
        logv("SUPPORT_MPDN = " + SUPPORT_MPDN);
        logv("SUPPORT_MPDN_SERVICE_ARBITRATION = " + SUPPORT_MPDN_SERVICE_ARBITRATION);
        logv("SUPPORT_OMH = " + mDpt.isOmhEnabled());
        logv("PROPERTY_SUPPORT_EHRPD = "
                + SystemProperties.getBoolean(TelephonyProperties.PROPERTY_SUPPORT_EHRPD, false));
        logv("PROPERTY_SUPPORT_SVLTE1X = "
                + SystemProperties.getBoolean(TelephonyProperties.PROPERTY_SUPPORT_SVLTE1X, false));
        logv("SUPPORT_OOS_IS_DISCONNECT = " + mOosIsDisconnect);
    }

    public void update(CommandsInterface ci, Subscription subData) {
        if (subData == null) {
            loge("update(): Supplied subscription info is null");
            return;
        }

        int currentDds = PhoneFactory.getDataSubscription();

        logd("update(): subData.subId = " + subData.subId + " currentDds = " + currentDds);

        // Update only if DDS
        if (subData.subId == currentDds) {
            // 1. Update the subscription data
            mSubscriptionData = subData;

            // 2. Reset Data Connections List
            if (mDataConnectionList != null) {
                for (DataConnection dc: mDataConnectionList) {
                    ((MMDataConnection)dc).update(ci);
                }
            }

            // 3. Unregister for the events on Commands Interface.
            mCm.unregisterForOn(this);
            mCm.unregisterForOffOrNotAvailable(this);
            mCm.unregisterForDataStateChanged(this);
            mCm.unregisterForCdmaOtaProvision(this);

            // 4. Re-register for the events on new Commands Interface.
            mCm = ci;
            mCm.registerForOn(this, EVENT_RADIO_ON, null);
            mCm.registerForOffOrNotAvailable(this, EVENT_RADIO_OFF_OR_NOT_AVAILABLE, null);
            mCm.registerForDataStateChanged(this, EVENT_DATA_CALL_LIST_CHANGED, null);
            mCm.registerForCdmaOtaProvision(this, EVENT_CDMA_OTA_PROVISION, null);

            // 5. Update all the elments
            mDpt.update(ci, subData.subId);
            mDsst.update(ci);

            // 6. Restart NetStat Poll
            stopNetStatPoll();
            startNetStatPoll();
        }
    }

    public void dispose() {

        // mark DCT as disposed
        isDctActive = false;

        mCm.unregisterForAvailable(this);
        mCm.unregisterForOn(this);
        mCm.unregisterForOffOrNotAvailable(this);
        mCm.unregisterForDataStateChanged(this);
        mCm.unregisterForTetheredModeStateChanged(this);

        mCm.unregisterForCdmaOtaProvision(this);

        mDsst.unregisterForDataConnectionAttached(this);
        mDsst.unregisterForDataConnectionDetached(this);
        mDsst.unRegisterForRadioTechnologyChanged(this);

        mDsst.unregisterForRecordsLoaded(this);

        mDsst.unregisterForDataRoamingOn(this);
        mDsst.unregisterForDataRoamingOff(this);
        mDsst.unregisterForPsRestrictedEnabled(this);
        mDsst.unregisterForPsRestrictedDisabled(this);
        mCdmaSSM.dispose(this);

        mDpt.unregisterForDataProfileDbChanged(this);

        mDsst.dispose();
        mDsst = null;

        destroyDataConnectionList();

        mContext.unregisterReceiver(this.mIntentReceiver);

        super.dispose();
    }

    public void handleMessage(Message msg) {

        if (isDctActive == false) {
            logw("Ignoring handler messages, DCT marked as disposed.");
            return;
        }

        switch (msg.what) {
            case EVENT_UPDATE_DATA_CONNECTIONS:
                onUpdateDataConnections(((String)msg.obj), (int)msg.arg1);
                break;

            case EVENT_RECORDS_LOADED:
                onRecordsLoaded();
                break;

            case EVENT_DATA_CONNECTION_ATTACHED:
                onDataConnectionAttached();
                break;

            case EVENT_DATA_CONNECTION_DETACHED:
                onDataConnectionDetached();
                break;

            case EVENT_RADIO_TECHNOLOGY_CHANGED:
                onRadioTechnologyChanged((AsyncResult)msg.obj);
                break;

            case EVENT_DATA_CALL_LIST_CHANGED:
                // unsolicited
                onDataCallListChanged((AsyncResult) msg.obj);
                break;

            case EVENT_DATA_PROFILE_DB_CHANGED:
                onDataProfileListChanged((AsyncResult) msg.obj);
                break;

            case EVENT_CDMA_OTA_PROVISION:
                onCdmaOtaProvision((AsyncResult) msg.obj);
                break;

            case EVENT_CDMA_SUBSCRIPTION_SOURCE_CHANGED:
                if (mCdmaSSM.getCdmaSubscriptionSource() == Phone.CDMA_SUBSCRIPTION_NV) {
                    mDpt.updateOperatorNumeric(mCdmaHomeOperatorNumeric,
                            REASON_CDMA_SUBSCRIPTION_SOURCE_CHANGED);
                }
                updateDataConnections(REASON_CDMA_SUBSCRIPTION_SOURCE_CHANGED);
                break;

            case EVENT_PS_RESTRICT_ENABLED:
                logi("PS restrict enabled.");
                /**
                 * We don't need to explicitly to tear down the PDP context when
                 * PS restricted is enabled. The base band will deactive PDP
                 * context and notify us with PDP_CONTEXT_CHANGED. But we should
                 * stop the network polling and prevent reset PDP.
                 */
                stopNetStatPoll();
                mIsPsRestricted = true;
                break;

            case EVENT_PS_RESTRICT_DISABLED:
                logi("PS restrict disable.");
                /**
                 * When PS restrict is removed, we need setup PDP connection if
                 * PDP connection is down.
                 */
                mIsPsRestricted = false;
                updateDataConnections(REASON_PS_RESTRICT_DISABLED);
                break;

            case EVENT_TETHERED_MODE_STATE_CHANGED:
                onTetheredModeStateChanged((AsyncResult) msg.obj);
                break;

            case EVENT_DATA_CALL_DROPPED:
                onDataCallDropped((AsyncResult)msg.obj);
                break;

            default:
                super.handleMessage(msg);
                break;
        }
    }

    protected void updateDataConnections(String reason) {
        mUpdateDataConnectionsContext++;
        Message msg = obtainMessage(EVENT_UPDATE_DATA_CONNECTIONS, //what
                mUpdateDataConnectionsContext, //arg1
                0, //arg2
                reason); //userObj
        sendMessage(msg);
    }

    private void onCdmaOtaProvision(AsyncResult ar) {
        if (ar.exception != null) {
            int[] otaProvision = (int[]) ar.result;
            if ((otaProvision != null) && (otaProvision.length > 1)) {
                switch (otaProvision[0]) {
                    case Phone.CDMA_OTA_PROVISION_STATUS_COMMITTED:
                    case Phone.CDMA_OTA_PROVISION_STATUS_OTAPA_STOPPED:
                        mDpt.resetAllProfilesAsWorking();
                        mDpt.resetAllServiceStates();
                        updateDataConnections(REASON_CDMA_OTA_PROVISION);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    private void onDataProfileListChanged(AsyncResult ar) {
        String reason = (String) ((AsyncResult) ar).result;

        mDpt.resetAllProfilesAsWorking();
        mDpt.resetAllServiceStates();

        /*
         * if there was something disconnected, updateDataConnections() will be
         * called after disconnect is done, else we call updateDataConnections()
         * to see if there is some call to be brought up as a result of apn change.
         */
        if (disconnectAllConnections(reason, null) == false) {
            updateDataConnections(reason);
        }
    }

    protected void onRecordsLoaded() {

        if (mDsst.mRuimRecords != null) {
            /* read the modem profiles */
            mDpt.readDataprofilesFromModem();
        }

        updateOperatorNumericInDpt(REASON_ICC_RECORDS_LOADED);
        updateDataConnections(REASON_ICC_RECORDS_LOADED);
    }

    /*
     * returns true if data profile list was changed as a result of this
     * operator numeric update
     */
    private boolean updateOperatorNumericInDpt(String reason) {

        //TODO: enable technology/subscription based operator numeric update

        /*
         * GSM+EHRPD requires MCC/MNC be used from SIMRecords. So for now, just
         * use simrecords if it is available.
         */

        if (mDsst.mSimRecords != null) {
            mDpt.updateOperatorNumeric(mDsst.mSimRecords.getSIMOperatorNumeric(), reason);
        } else if (mDsst.mRuimRecords != null
                && mCdmaSSM.getCdmaSubscriptionSource() == Phone.CDMA_SUBSCRIPTION_RUIM_SIM) {
            mDpt.updateOperatorNumeric(mDsst.mRuimRecords.getRUIMOperatorNumeric(), reason);
        } else {
            loge("records are loaded, but both mSimrecords & mRuimRecords are null.");
        }
        return false;
    }

    protected void onDataConnectionAttached() {
        // reset all profiles as working so as to give
        // all data profiles fair chance again.
        mDpt.resetAllProfilesAsWorking();
        mDpt.resetAllServiceStates();

        /*
         * send a data connection notification update, with latest states, it is
         * possible data went out of service and came back in service without
         * data calls being disconnected
         */
        notifyAllDataServiceTypes(REASON_DATA_NETWORK_ATTACH);

        updateDataConnections(REASON_DATA_NETWORK_ATTACH);
    }

    protected void onDataConnectionDetached() {
        /*
         * Ideally, nothing needs to be done, data connections will disconnected
         * one by one, and update data connections will be done then. But that
         * might not happen, or might take time. So still need to trigger a data
         * connection state update, because data was detached and packets are
         * not going to flow anyway.
         */
          notifyAllDataServiceTypes(REASON_DATA_NETWORK_DETACH);
    }

    protected void onRadioTechnologyChanged(AsyncResult asyncResult) {
        /*
         * notify radio technology changes.
         */
        notifyAllDataServiceTypes(REASON_RADIO_TECHNOLOGY_CHANGED);

        boolean shouldRetryDataCall = (Boolean)asyncResult.result;
        /*
         * Reset all service states when IRAT hand over happens. Data
         * profiles not working on previous radio technologies might start
         * working now.
         */
        if (shouldRetryDataCall) {
            logd("Resetting all profiles and service states!");
            mDpt.resetAllProfilesAsWorking();
            mDpt.resetAllServiceStates();
        }
        updateDataConnections(REASON_RADIO_TECHNOLOGY_CHANGED);
    }

    @Override
    protected void onRadioOn() {
        mDpt.resetAllProfilesAsWorking();
        mDpt.resetAllServiceStates();
        updateDataConnections(REASON_RADIO_ON);
    }

    @Override
    protected void onRadioOff() {
        //cleanup for next time, probably not required.
        mDpt.resetAllProfilesAsWorking();
        mDpt.resetAllServiceStates();
    }

    @Override
    protected void onRoamingOff() {
        mDpt.resetAllProfilesAsWorking();
        mDpt.resetAllServiceStates();
        updateDataConnections(REASON_ROAMING_OFF);
    }

    @Override
    protected void onRoamingOn() {
        if (getDataOnRoamingEnabled() == false) {
            disconnectAllConnections(REASON_ROAMING_ON, null);
        }
        updateDataConnections(REASON_ROAMING_ON);
    }

    @Override
    protected void onVoiceCallEnded() {
        mDpt.resetAllProfilesAsWorking();
        mDpt.resetAllServiceStates();
        updateDataConnections(REASON_VOICE_CALL_ENDED);
        notifyAllDataServiceTypes(REASON_VOICE_CALL_ENDED);
    }

    @Override
    protected void onVoiceCallStarted() {
        updateDataConnections(REASON_VOICE_CALL_STARTED);
        notifyAllDataServiceTypes(REASON_VOICE_CALL_STARTED);
    }

    @Override
    protected void onMasterDataDisabled(Message onCompleteMsg) {
        disconnectAllConnections(REASON_MASTER_DATA_DISABLED, onCompleteMsg);
    }

    @Override
    protected void onMasterDataEnabled() {
        mDpt.resetAllProfilesAsWorking();
        mDpt.resetAllServiceStates();
        updateDataConnections(REASON_MASTER_DATA_ENABLED);
    }

    @Override
    protected void onServiceTypeDisabled(DataServiceType ds) {

        /* if the dc corresponding to the ds we are disabling is not in use by any other ds;
         * bring it down. Do this for each ip type.
         */
        for (IPVersion ipv : IPVersion.values()) {

            if (mDpt.isServiceTypeActive(ds, ipv) == false) {
                // reset the service state for this service type,
                // clears out any connection (v4/v6) that was being retried.
                mDpt.resetServiceState(ds);
                continue;
            }

            DataConnection dc = mDpt.getActiveDataConnection(ds, ipv);
            boolean tearDownNeeded = true;
            for (DataServiceType t : DataServiceType.values()) {
                if (t != ds && mDpt.isServiceTypeEnabled(t)
                        && mDpt.getActiveDataConnection(t, ipv) == dc) {
                    tearDownNeeded = false; //dc used by somebody else.
                }
            }
            if (tearDownNeeded) {
                tryDisconnectDataCall(dc, REASON_SERVICE_TYPE_DISABLED);
            }
        }

        //check for something else to do
        updateDataConnections(REASON_SERVICE_TYPE_DISABLED);
    }

    @Override
    protected void onServiceTypeEnabled(DataServiceType type) {
        mDpt.resetAllProfilesAsWorking();
        mDpt.resetServiceState(type);
        updateDataConnections(REASON_SERVICE_TYPE_ENABLED);
    }

    /**
     * @param explicitPoll if true, indicates that *we* polled for this update
     *            while state == CONNECTED rather than having it delivered via
     *            an unsolicited response (which could have happened at any
     *            previous state
     */
    @SuppressWarnings("unchecked")
    protected void onDataCallListChanged(AsyncResult ar) {

        ArrayList<DataCallState> dcStates;
        dcStates = (ArrayList<DataCallState>) (ar.result);

        if (ar.exception != null) {
            // This is probably "radio not available" or something
            // of that sort. If so, the whole connection is going
            // to come down soon anyway
            return;
        }

        logv("onDataCallListChanged:");
        logv("---dc state list---");
        for (DataCallState d : dcStates) {
            if (d != null && d.active != DATA_CONNECTION_ACTIVE_PH_LINK_INACTIVE)
            logv(d.toString());
        }
        dumpDataCalls();

        boolean isDataDormant = true; // will be set to false, if atleast one
                                      // data connection is not dormant.

        int activeDataCallCount = 0;

        int ipResultCode = 0;

        int gwResultCode = 0;

        for (DataConnection dc: mDataConnectionList) {

            if (dc.isActive() == false) {
                continue;
            } else {
                activeDataCallCount++;
            }

            DataCallState activeDC = getDataCallStateByCid(dcStates, dc.cid);
            if (activeDC == null) {
                logi("DC has disappeared from list : dc = " + dc);

                CallbackData c = new CallbackData();
                c.dc = dc;
                c.reason = REASON_NETWORK_DISCONNECT;
                //onUpdateDataConnections will be called when async reset returns.
                dc.reset(obtainMessage(EVENT_DATA_CALL_DROPPED, c));

                // Active call disconnected
                activeDataCallCount--;
            } else if (activeDC.active == DATA_CONNECTION_ACTIVE_PH_LINK_INACTIVE) {
                DataConnectionFailCause failCause = DataConnectionFailCause
                        .getDataConnectionDisconnectCause(activeDC.status);

                logi("DC is inactive : dc = " + dc);
                logi("   inactive cause = " + failCause);

                CallbackData c = new CallbackData();
                c.dc = dc;
                c.reason = REASON_NETWORK_DISCONNECT;
                //onUpdateDataConnections will be called when async reset returns.
                dc.reset(obtainMessage(EVENT_DATA_CALL_DROPPED, c));

                // Active call disconnected
                activeDataCallCount--;
            } else if (((ipResultCode = dc.compareAddresses(activeDC.addresses))
                    != dc.IPV_ADDR_NOCHANGE) ||
                    ((gwResultCode = dc.compareGwAddresses(activeDC.gateways))
                            != dc.IPV_ADDR_NOCHANGE)) {
                handleIPAddressChange(dc, activeDC, ipResultCode);
                handleGwAddressChange(dc, activeDC, gwResultCode);

                if (((ipResultCode & dc.IPV4_ADDR_REMOVED) != 0) ||
                        ((ipResultCode & dc.IPV6_ADDR_REMOVED) != 0)) {
                    /*
                     *  If either v4 or v6 address has been  removed , then updateDataConnections()
                     *  will take care of retrying / setting up call again on appropriate ipv.
                     */
                    updateDataConnections(REASON_DATA_CONN_PROP_CHANGED);
                }
            } else {
                switch (activeDC.active) {
                    /*
                     * TODO: fusion - the following code will show dormancy
                     * indications for both cdma/gsm. Is this a good thing?
                     */
                    case DATA_CONNECTION_ACTIVE_PH_LINK_UP:
                        isDataDormant = false;
                        break;

                    case DATA_CONNECTION_ACTIVE_PH_LINK_DOWN:
                        // do nothing
                        break;

                    default:
                        loge("dc.cid = " + dc.cid + ", unexpected DataCallState.active="
                                + activeDC.active);
                }
            }
        }

        if (isDataDormant) {
            mPollNetStat.setActivity(Activity.DORMANT);
            stopNetStatPoll();
        } else {
            mPollNetStat.setActivity(Activity.NONE);
            startNetStatPoll();
        }
        notifyDataActivity();

        logv("onDataCallListChanged: - activeDataCallCount = " + activeDataCallCount);
        if (activeDataCallCount == 0) {
            logv("onDataCallListChanged: - Notify all data disconnect from modem.");
            notifyAllDataDisconnected();
        }
    }

    public void notifyAllDataDisconnected() {
        mAllDataDisconnectedRegistrants.notifyRegistrants();
    }

    public void registerForAllDataDisconnected(Handler h, int what, Object obj) {
        mAllDataDisconnectedRegistrants.addUnique(h, what, obj);

        boolean anyDcActive = false;
        for (DataConnection dc: mDataConnectionList) {
            logd("DC = " + dc);
            if (dc.isActive()) {
                anyDcActive = true;
            }
        }

        logd("anyDcActive = " + anyDcActive);
        if (!anyDcActive) {
            logd("notify All Data Disconnected");
            mAllDataDisconnectedRegistrants.notifyRegistrants();
        }
    }

    public void unregisterForAllDataDisconnected(Handler h) {
        mAllDataDisconnectedRegistrants.remove(h);
    }

    void onTetheredModeStateChanged(AsyncResult ar) {
        int[] ret = (int[])ar.result;

        if (ret == null || ret.length != 1) {
            loge("Error: Invalid Tethered mode received");
            return;
        }

        int mode = ret[0];
        logd("onTetheredModeStateChanged: mode:" + mode);

        switch (mode) {
            case RIL_TETHERED_MODE_ON:
                /* Indicates that an internal data call was created in the
                 * modem. Do nothing, just information for now
                 */
                logd("Unsol Indication: RIL_TETHERED_MODE_ON");
            break;
            case RIL_TETHERED_MODE_OFF:
                logd("Unsol Indication: RIL_TETHERED_MODE_OFF");
                /* This indicates that an internal modem data call (e.g. tethered)
                 * had ended. Reset the retry count for all DataService types since
                 * all have become unblocked and stand a chance of initiating a call
                 * again.
                 */
                mDpt.resetAllServiceStates();
                updateDataConnections(REASON_TETHERED_MODE_STATE_CHANGED);
            break;
            default:
                loge("Error: Invalid Tethered mode:" + mode);
        }
    }


    private DataCallState getDataCallStateByCid(ArrayList<DataCallState> states, int cid) {
        for (int i = 0, s = states.size(); i < s; i++) {
            if (states.get(i).cid == cid)
                return states.get(i);
        }
        return null;
    }

    private void handleIPAddressChange(DataConnection dc, DataCallState activeDC, int flags ) {

        logi("handleIPAddressChange:  " + activeDC.addresses + " flags " + flags);

        if(flags == dc.IPV_ADDR_NOCHANGE) {
            return;
        }

        if (((flags & dc.IPV4_ADDR_UPDATED) != 0) ||
                ((flags & dc.IPV6_ADDR_UPDATED) != 0)) {
            // One of the IP addresses or both have got modified, disconnect
            logi("tryDisconnectDataCall...");
            tryDisconnectDataCall(dc, REASON_DATA_CONN_PROP_CHANGED);
            return;
        }

        // Update the addresses before notifying upper layers of ip change
        dc.updateIPAddresses(activeDC.addresses);
        if ((flags & dc.IPV4_ADDR_ADDED) != 0) {
            notifyPartialDcStatus(dc,IPVersion.INET,
                    REASON_DATA_NETWORK_ATTACH,PARTIAL_STATUS_ADDR_ADDED);
        }
        if ((flags & dc.IPV6_ADDR_ADDED) != 0) {
            notifyPartialDcStatus(dc,IPVersion.INET6,
                    REASON_DATA_NETWORK_ATTACH,PARTIAL_STATUS_ADDR_ADDED);
        }
        if ((flags & dc.IPV4_ADDR_REMOVED) != 0) {
            notifyPartialDcStatus(dc,IPVersion.INET,
                    REASON_NETWORK_DISCONNECT,PARTIAL_STATUS_ADDR_REMOVED);
        }
        if ((flags & dc.IPV6_ADDR_REMOVED) != 0) {
            notifyPartialDcStatus(dc,IPVersion.INET6,
                    REASON_NETWORK_DISCONNECT,PARTIAL_STATUS_ADDR_REMOVED);
        }
        return;
    }

    private void handleGwAddressChange(DataConnection dc, DataCallState activeDC, int flags ) {
        logi("handleGwAddressChange:  " + activeDC.gateways + " flags " + flags);

        if(flags == dc.IPV_ADDR_NOCHANGE) {
            return;
        }

        if (((flags & dc.IPV4_ADDR_UPDATED) != 0) ||
                ((flags & dc.IPV6_ADDR_UPDATED) != 0)) {
            // One of the Gw addresses or both have got modified, disconnect
            /*
             *  TODO : Need more graceful way to perform route updates,
             *  than doing disconnect and re-connect
             */
            logi("tryDisconnectDataCall...");
            tryDisconnectDataCall(dc, REASON_DATA_CONN_PROP_CHANGED);
            return;
        }

        // Update the addresses before notifying upper layers of GW change
        dc.updateGatewayAddresses(activeDC.gateways);
        if ((flags & dc.IPV4_ADDR_ADDED) != 0) {
            notifyPartialDcStatus(dc,IPVersion.INET,
                    REASON_DATA_NETWORK_ATTACH,PARTIAL_STATUS_ADDR_ADDED);
        }
        if ((flags & dc.IPV6_ADDR_ADDED) != 0) {
            notifyPartialDcStatus(dc,IPVersion.INET6,
                    REASON_DATA_NETWORK_ATTACH,PARTIAL_STATUS_ADDR_ADDED);
        }
        if ((flags & dc.IPV4_ADDR_REMOVED) != 0) {
            notifyPartialDcStatus(dc,IPVersion.INET,
                    REASON_NETWORK_DISCONNECT,PARTIAL_STATUS_ADDR_REMOVED);
        }
        if ((flags & dc.IPV6_ADDR_REMOVED) != 0) {
            notifyPartialDcStatus(dc,IPVersion.INET6,
                    REASON_NETWORK_DISCONNECT,PARTIAL_STATUS_ADDR_REMOVED);
        }
    }

    @Override
    protected void onConnectDone(AsyncResult ar) {

        mDataCallSetupPending = false;

        CallbackData c = (CallbackData) ar.userObj;

        DataConnectionFailCause cause = DataConnectionFailCause.NONE;

        /*
         * If setup is successful,  ar.result will contain the MMDataConnection instance
         *     with possible partial failure cause.
         * if setup failure, ar.result will contain the failure reason.
         */
        if (ar.exception == null) { /* connection is up!! */

            logi("--------------------------");
            logi("Data call setup : SUCCESS");
            logi("  data profile  : " + c.dp.toShortString());
            logi("  service type  : " + c.ds);
            logi("  data call id  : " + c.dc.cid);
            logi("  bearer type   : " + c.dc.getBearerType());
            logi("  ip address/gw : " + c.dc.ipAddrtoString()+"/"+c.dc.gwAddrtoString());
            logi("  dns           : " + Arrays.toString(c.dc.getDnsServers()));
            if (ar.result != null) {
                cause = (DataConnectionFailCause) (ar.result);
                if (cause != null && cause.isPartialFailure()) {
                    logi("  partial fail cause    : " + cause);
                }
            }
            logi("--------------------------");

            handleConnectedDc(c.dc, c.reason);

            handlePartialConnectedDc(c, cause);

            //we might have other things to do, so call update updateDataConnections() again.
            updateDataConnections(c.reason);

            return; //done.
        }

        //ASSERT: Data call setup has failed.

        freeDataConnection((MMDataConnection)c.dc);

        //Set data call state as FAILED, state might move
        //to WAITING_FOR_ALARM if we have retries left.

        mDpt.setState(State.FAILED, c.ds, c.ipv);
        notifyDataConnection(c.ds, c.ipv, c.reason);

        logi("--------------------------");
        logi("Data call setup : FAILED");
        logi("  data profile  : " + c.dp.toShortString());
        logi("  service type  : " + c.ds);
        logi("  bearer type   : " + c.dc.getBearerType());
        logi("  fail cause    : " + cause);
        logi("--------------------------");

        boolean needDataConnectionUpdate = true;

        /*
         * look at the error code and determine what is the best thing to do :
         * there is no guarantee that modem/network is capable of reporting the
         * correct failure reason, so we do our best to get all requested
         * services up, but somehow making sure we don't retry endlessly.
         */

        if (cause == DataConnectionFailCause.IP_VERSION_NOT_SUPPORTED) {
            /*
             * it might not be possible for us to know if its the network that
             * doesn't support IPV6 in general, or if its the profile we tried
             * that doesn't support IPV6!
             */
            logv("Disabling data profile. dp=" + c.dp.toShortString() + ", ipv=" + c.ipv);
            c.dp.setWorking(false, c.ipv);
            // set state to scanning because can try on other data
            // profiles that might work with this ds+ipv.
            mDpt.setState(State.SCANNING, c.ds, c.ipv);
        } else if (cause.isDataProfileFailure()) {
            /*
             * this profile doesn't work, mark it as not working, so that we
             * have other profiles to try with. It is possible that
             * modem/network didn't report IP_VERSION_NOT_SUPPORTED, but profile
             * might still work with other IPV.
             */
            logv("Disabling data profile. dp=" + c.dp.toShortString() + ", ipv=" + c.ipv);
            c.dp.setWorking(false, c.ipv);
            // set state to scanning because can try on other data
            // profiles that might work with this ds+ipv.
            mDpt.setState(State.SCANNING, c.ds, c.ipv);
        } else if (mDpt.isServiceTypeActive(c.ds) == false &&
                cause.isPdpAvailabilityFailure()) {
            /*
             * not every modem, or network might be able to report this but if
             * we know this is the failure reason, we know exactly what to do!
             * check if low priority services are active, if yes tear it down!
             * But do not bother de-activating low priority calls if the same service
             * is already active on other ip versions.
             */
            if (SUPPORT_MPDN_SERVICE_ARBITRATION && disconnectOneLowPriorityDataCall(c.ds, c.reason)) {
                logv("Disconnected low priority data call [pdp availability failure.]");
                needDataConnectionUpdate = false;
                // will be called, when disconnect is complete.
            }
            // set state to scanning because can try on other data
            // profiles that might work with this ds+ipv.
            mDpt.setState(State.SCANNING, c.ds, c.ipv);
        } else if (SUPPORT_MPDN_SERVICE_ARBITRATION && mDpt.isServiceTypeActive(c.ds) == false
                && disconnectOneLowPriorityDataCall(c.ds, c.reason)) {
            logv("Disconnected low priority data call [pdp availability failure.]");
            /*
             * We do this because there is no way to know if the failure was
             * caused because of network resources not being available! But do
             * not bother de-activating low priority calls if the same service
             * is already active on other ip versions.
             */
            needDataConnectionUpdate = false;
            // set state to scanning because can try on other data
            // profiles that might work with this ds+ipv.
            mDpt.setState(State.SCANNING, c.ds, c.ipv);
        } else if (cause.isPermanentFail()) {
            /*
             * even though modem reports permanent failure, it is not clear
             * if failure is related to data profile, ip version, mpdp etc.
             * its safer to try and exhaust all data profiles.
             */
            logv("Permanent failure. Disabling data profile. dp=" +
                    c.dp.toShortString() + ", ipv="+ c.ipv);
            c.dp.setWorking(false, c.ipv);
            // set state to scanning because can try on other data
            // profiles that might work with this ds+ipv.
            mDpt.setState(State.SCANNING, c.ds, c.ipv);
        } else {
            logv("Data call setup failure cause unknown / temporary failure.");
            /*
             * If we reach here, then it is a temporary failure and we are trying
             * to setup data call on the highest priority service that is enabled.
             * 1. Retry if possible
             * 2. If no more retries possible, disable the data profile.
             * 3. If no more valid data profiles, mark service as disabled and set state
             *    to failed, notify.
             * 4. if default is the highest priority service left enabled,
             *    it will be retried forever!
             */

            RetryManager retryManager = mDpt.getRetryManager(c.ds, c.ipv);

            boolean scheduleAlarm = false;
            long nextReconnectDelay = 0; /* if scheduleAlarm == true */

            if (retryManager.isRetryNeeded()) {
                /* 1 : we have retries left. so Retry! */
                scheduleAlarm  = true;
                nextReconnectDelay = retryManager.getRetryTimer();
                retryManager.increaseRetryCount();
                // set state to scanning because can try on other data
                // profiles that might work with this ds+ipv.
                mDpt.setState(State.WAITING_ALARM, c.ds, c.ipv);
                notifyDataConnection(c.ds, c.ipv, c.reason);
            } else {
                /* 2 : enough of retries. disable the data profile */
                logv("No retries left, disabling data profile. dp=" +
                        c.dp.toShortString() + ", ipv = "+ c.ipv);
                c.dp.setWorking(false, c.ipv);
                if (mDpt.getNextWorkingDataProfile(c.ds, getDataProfileTypeToUse(), c.ipv) != null) {
                    // set state to scanning because can try on other data
                    // profiles that might work with this ds+ipv.
                    mDpt.setState(State.SCANNING, c.ds, c.ipv);
                } else {
                    if (c.ds != DataServiceType.SERVICE_TYPE_DEFAULT) {
                        /*
                         * No more valid data profiles, mark service as disabled
                         * and set state to failed, notify.
                         */
                        // but make sure service is not active on different IPV!
                        if (mDpt.isServiceTypeActive(c.ds) == false) {
                            logv("No data profiles left to try, disabling service  " + c.ds);
                            mDpt.setServiceTypeEnabled(c.ds, false);
                        }
                        mDpt.setState(State.FAILED, c.ds, c.ipv);
                        notifyDataConnection(c.ds, c.ipv, c.reason);
                    } else {
                        /* 4 */
                        /* we don't have any higher priority services
                         * enabled and we ran out of other profiles to try.
                         * So retry forever with the last profile we have.
                         */
                        logv("Retry forever using last disabled data profile. dp=" +
                                c.dp.toShortString() + ", ipv = " + c.ipv);
                        c.dp.setWorking(true, c.ipv);
                        mDpt.setState(State.WAITING_ALARM, c.ds, c.ipv);
                        notifyDataConnection(c.ds, c.ipv, c.reason);
                        notifyDataConnectionFail(c.reason);

                        retryManager.retryForeverUsingLastTimeout();
                        scheduleAlarm = true;
                        nextReconnectDelay = retryManager.getRetryTimer();
                        retryManager.increaseRetryCount();
                    }
                }
            }

            if (scheduleAlarm) {
                logd("Scheduling next attempt on " + c.ds + " for " + (nextReconnectDelay / 1000)
                        + "s. Retry count = " + retryManager.getRetryCount());

                AlarmManager am = (AlarmManager) mContext.getSystemService(Context.ALARM_SERVICE);

                Intent intent = new Intent(getAlarmIntentName(c.ds, c.ipv));
                intent.putExtra(INTENT_RECONNECT_ALARM_EXTRA_REASON, c.reason);
                intent.putExtra(INTENT_RECONNECT_ALARM_SERVICE_TYPE, c.ds.toString());
                intent.putExtra(INTENT_RECONNECT_ALARM_IP_VERSION, c.ipv.toString());

                mReconnectIntent = PendingIntent.getBroadcast(mContext, 0, intent, 0);
                // cancel any pending wakeup - TODO: does this work?
                am.cancel(mReconnectIntent);
                am.set(AlarmManager.ELAPSED_REALTIME_WAKEUP, SystemClock.elapsedRealtime()
                        + nextReconnectDelay, mReconnectIntent);
                needDataConnectionUpdate = true;
            }
        }

        if (needDataConnectionUpdate) {
            updateDataConnections(c.reason);
        }

        logDataConnectionFailure(c.ds, c.dp, c.ipv, cause);
    }

    private String getAlarmIntentName(DataServiceType ds, IPVersion ipv) {
        return (INTENT_RECONNECT_ALARM + "." + ds + "." + ipv);
    }

    private void logDataConnectionFailure(DataServiceType ds, DataProfile dp, IPVersion ipv,
            DataConnectionFailCause cause) {
        if (cause.isEventLoggable()) {
            CellLocation loc = TelephonyManager.getDefault().getCellLocation();
            int id = -1;
            if (loc != null) {
                if (loc instanceof GsmCellLocation)
                    id = ((GsmCellLocation) loc).getCid();
                else
                    id = ((CdmaCellLocation) loc).getBaseStationId();
            }

            if (getRadioTechnology().isGsm()
                    || getRadioTechnology() == RadioTechnology.RADIO_TECH_EHRPD) {
                EventLog.writeEvent(EventLogTags.PDP_NETWORK_DROP,
                        id, getRadioTechnology().toString());
            } else {
                EventLog.writeEvent(EventLogTags.CDMA_DATA_DROP,
                        id, getRadioTechnology().toString());
            }
        }
    }

    /* disconnect exactly one data call whose priority is lower than serviceType
     */
    private boolean disconnectOneLowPriorityDataCall(DataServiceType serviceType, String reason) {
        for (DataServiceType ds : DataServiceType.values()) {
            if (ds.isLowerPriorityThan(serviceType) && mDpt.isServiceTypeEnabled(ds)
                    && mDpt.isServiceTypeActive(ds)) {
                // we are clueless as to whether IPV4/IPV6 are on same network PDP or
                // different, so disconnect both.
                boolean disconnectDone = false;
                DataConnection dc;
                dc = mDpt.getActiveDataConnection(ds, IPVersion.INET);
                if (dc != null) {
                    tryDisconnectDataCall(dc, reason);
                    disconnectDone = true;
                }
                dc = mDpt.getActiveDataConnection(ds, IPVersion.INET6);
                if (dc != null) {
                    tryDisconnectDataCall(dc, reason);
                    disconnectDone = true;
                }
                if (disconnectDone) {
                    return true;
                }
            }
        }
        return false;
    }

    /*
     * Check if higher priority data call is active whose priority is greater
     * than the specified service type.
     */
    private boolean isHigherPriorityDataCallActive(DataServiceType serviceType) {
        for (DataServiceType ds : DataServiceType.values()) {
            if (ds.isHigherPriorityThan(serviceType) && mDpt.isServiceTypeEnabled(ds)
                    && mDpt.isServiceTypeActive(ds)) {
                return true;
            }
        }
        return false;
    }

    protected void onDataCallDropped(AsyncResult ar) {

        CallbackData c = (CallbackData) ar.userObj;

        logv("onDataCallDropped: dc=" + c.dc + ", reason=" + c.reason);

        handleDisconnectedDc(c.dc, c.reason);

        updateDataConnections(c.reason); //check for something else to do.
    }

    protected synchronized void onDisconnectDone(AsyncResult ar) {

        CallbackData c = (CallbackData) ar.userObj;

        logv("onDisconnectDone: reason=" + c.reason);

        handleDisconnectedDc(c.dc, c.reason);

        if (mDisconnectPendingCount > 0)
            mDisconnectPendingCount--;

        if (mDisconnectPendingCount == 0) {
            for (Message m: mDisconnectAllCompleteMsgList) {
                m.sendToTarget();
            }
            mDisconnectAllCompleteMsgList.clear();
        }

        updateDataConnections(c.reason); //check for something else to do.
    }

    /*
     * returns true, if disconnect is in progress. returns false if nothing to
     * disconnect.
     */
    private synchronized boolean disconnectAllConnections(String reason,
            Message disconnectAllCompleteMsg) {

        if (disconnectAllCompleteMsg != null) {
            mDisconnectAllCompleteMsgList.add(disconnectAllCompleteMsg);
        }

        if (mDisconnectPendingCount != 0) {
            logv("disconnect all data connections in progress. queuing.");
            return true;
        }

        mDisconnectPendingCount = 0;
        for (DataConnection dc : mDataConnectionList) {
            if (dc.isInactive() == false ) {
                tryDisconnectDataCall(dc, reason);
                mDisconnectPendingCount++;
            }
        }

        // clear any service type that is pending/retried.
        mDpt.resetAllServiceStates();

        if (mDisconnectPendingCount == 0) {
            for (Message m: mDisconnectAllCompleteMsgList) {
                m.sendToTarget();
            }
            mDisconnectAllCompleteMsgList.clear();
            return false;
        }
        return true;
    }

    synchronized protected void onUpdateDataConnections(String reason, int context) {
        if (context != mUpdateDataConnectionsContext) {
            //we have other EVENT_UPDATE_DATA_CONNECTIONS on the way.
            logv("onUpdateDataConnections [ignored] : reason=" + reason);
            return;
        }

        logv("onUpdateDataConnections: reason=" + reason);
        dumpDataCalls();
        dumpDataServiceTypes();

        // Check for data readiness!
        boolean isReadyForData = isReadyForData()
                                    && getDesiredPowerState()
                                    && mCm.getRadioState().isOn();

        if (isReadyForData == false) {
            logi("***** NOT Ready for data :");
            logi("   " + "getDesiredPowerState() = " + getDesiredPowerState());
            logi("   " + "mCm.getRadioState() = " + mCm.getRadioState());
            logi("   " + dumpDataReadinessinfo());
            return; // we will be called, when some event, triggers us back into
                    // readiness.
        } else {
            logi("Ready for data : ");
            logi("   " + "getDesiredPowerState() = " + getDesiredPowerState());
            logi("   " + "mCm.getRadioState() = " + mCm.getRadioState());
            logi("   " + dumpDataReadinessinfo());
        }

        /*
         * If we had issued a data call setup before, then wait for it to complete before
         * trying any new calls.
         */
        if (mDataCallSetupPending == true) {
            logi("Data Call setup pending. Not trying to bring up any new data connections.");
            return;
        }

        if (mDisconnectPendingCount > 0) {
            logi("Data Call disconnect request pending."
                    + "Not trying to bring up any new data connections.");
            return;
        }

        /*
         * We bring up data calls strictly in order of service priority when
         * 1. MPDN is disabled OR
         * 2. MPDN is enabled and MPDN service arbitration is also enabled OR
         * 3. We are in OMH mode.
         *
         * We can try data calls ignoring service priority on other cases.
         */

        boolean dataCallsInOrderOfPriority = SUPPORT_MPDN == false
                || (SUPPORT_MPDN && SUPPORT_MPDN_SERVICE_ARBITRATION)
                || mDpt.isAnyDataProfileAvailable(DataProfileType.PROFILE_TYPE_3GPP2_OMH);

        /*
         * If a service in WAITING_ALARM state, then we can try other services
         * only if dataCallsInOrderOfPriority is false.
         */

        for (DataServiceType ds : DataServiceType.getPrioritySortedValues()) {
            if (mDpt.isServiceTypeEnabled(ds) == true) {
                // IPV4
                if (SUPPORT_IPV4) {
                    if (checkAndBringUpDs(ds, IPVersion.INET, reason, dataCallsInOrderOfPriority))
                        return;
                }

                // IPV6
                if (SUPPORT_IPV6) {
                    if (checkAndBringUpDs(ds, IPVersion.INET6, reason, dataCallsInOrderOfPriority))
                        return;
                }
            } // if (mDpt.isServiceTypeEnabled(ds) == true)
        }
    }

    private boolean checkAndBringUpDs(DataServiceType ds, IPVersion ipv, String reason,
            boolean dataCallsInOrderOfPriority) {

        if (mDpt.isServiceTypeActive(ds, ipv) == false) {
            if (mDpt.getState(ds, ipv) != State.WAITING_ALARM) {
                boolean setupDone = trySetupDataCall(ds, ipv, reason);
                if (setupDone)
                    return true;
            } else if (dataCallsInOrderOfPriority == true) {
                // higher priority service is waiting for alarm to ring, so
                // skip all lower priority calls.
                logv("Skipping bringing up of low pri ds due to pending high pri ds");
                return true;
            }
        }

        return false;
    }

    private void handleDisconnectedDc(DataConnection dc, String reason) {

        logv("handleDisconnectedDc() : " + dc);

        freeDataConnection((MMDataConnection)dc);

        for (DataServiceType ds : DataServiceType.values()) {
            boolean needUpdate = false;

            if (mDpt.getActiveDataConnection(ds, IPVersion.INET) == dc) {
                mDpt.setServiceTypeAsInactive(ds, IPVersion.INET);
                needUpdate = true;
            }
            if (mDpt.getActiveDataConnection(ds, IPVersion.INET6) == dc) {
                mDpt.setServiceTypeAsInactive(ds, IPVersion.INET6);
                needUpdate = true;
            }
            if (needUpdate) {
                notifyDataConnection(ds, IPVersion.INET, reason);
                notifyDataConnection(ds, IPVersion.INET6, reason);
                if (ds == DataServiceType.SERVICE_TYPE_DEFAULT
                    && mDpt.isServiceTypeActive(DataServiceType.SERVICE_TYPE_DEFAULT) == false) {
                    SystemProperties.set("gsm.defaultpdpcontext.active", "false");
                }
            }
        }
    }

    private void notifyPartialDcStatus(DataConnection dc, IPVersion ipv,
            String reason, int  statusCode) {
        logv("notifyPartialDcStatus() : " + dc + ", IPVersion :" +
                ipv + " reason :" + reason + " statusCode = " + statusCode);

        for (DataServiceType ds : DataServiceType.values()) {
            if (mDpt.getActiveDataConnection(ds, ipv) == dc) {
                switch (statusCode) {
                    case PARTIAL_STATUS_ADDR_ADDED:
                        if (dc.getDataProfile().canHandleServiceType(ds))
                            mDpt.setServiceTypeAsActive(ds, dc, ipv);
                        break;
                    case PARTIAL_STATUS_ADDR_REMOVED:
                        mDpt.setServiceTypeAsInactive(ds, ipv);
                        break;
                    case PARTIAL_STATUS_ADDR_UPDATED:
                        break;
                    default:
                          break;
                }
                notifyDataConnection(ds, ipv, reason);
            }
        }
    }

    private void handlePartialConnectedDc(CallbackData c, DataConnectionFailCause cause) {
            if (cause == DataConnectionFailCause.ONLY_IPV4_ALLOWED) {
                /*
                 * Since only IPV4 call is active and partial fail
                 * cause code is 'only ipv4 allowed', do not attempt
                 * ipv6 call again on this APN on this n/w technology
                 */
                disableDataProfile(c.dp, IPVersion.INET6);
            } else if (cause == DataConnectionFailCause.ONLY_IPV6_ALLOWED) {
                /*
                 * Since only IPV6 call is active and partial fail
                 * cause code is 'only ipv6 allowed' do not attempt
                 * ipv4 call again on this APN on this n/w technology
                 */
                disableDataProfile(c.dp, IPVersion.INET);
            } else if (cause == DataConnectionFailCause.ONLY_SINGLE_BEARER_ALLOWED) {
                /*
                 * TODO: Say, if ipv4 call is active and partial fail
                 * cause code is 'single bearer only allowed',
                 * attempt ipv6 call again on this APN
                 * and vice versa
                 */
            }
    }

    private void disableDataProfile(DataProfile dp, IPVersion ipv) {
        dp.setWorking(false, ipv);
        for (DataServiceType ds : DataServiceType.values()) {
            mDpt.setServiceTypeAsInactive(ds, ipv);
            mDpt.setState(State.FAILED, ds, ipv);
        }
    }

    private void handleConnectedDc(DataConnection dc, String reason) {

        logv("handleConnectedDc() : " + dc);

        boolean needsDisconnect = true;
        BearerType b = dc.getBearerType();

        for (IPVersion ipv : IPVersion.values()) {

            if (b.supportsIpVersion(ipv) == false)
                continue; //bearer doesn't support ipv.

            for (DataServiceType ds : DataServiceType.values()) {
                if (mDpt.isServiceTypeActive(ds, ipv) == false
                        && dc.getDataProfile().canHandleServiceType(ds)) {

                    mDpt.setServiceTypeAsActive(ds, dc, ipv);
                    notifyDataConnection(ds, ipv, reason);

                    /*
                     * of all the services that just got active, at least one of them
                     * should have be enabled or else this dc is staying up for no
                     * reason. This can happen for example when the service type
                     * was disabled by the time setup_data_call response came up.
                     */
                    if (mDpt.isServiceTypeEnabled(ds) == true) {
                        needsDisconnect = false;
                    }

                    if (ds == DataServiceType.SERVICE_TYPE_DEFAULT) {
                        SystemProperties.set("gsm.defaultpdpcontext.active", "true");
                    }
                }
            } //for ds
        } //for ipv

        if (needsDisconnect == true) {
            tryDisconnectDataCall(dc, REASON_SERVICE_TYPE_DISABLED);
        }
    }

    private boolean getDesiredPowerState() {
        return mDesiredPowerState;
    }

    @Override
    public synchronized void setDataConnectionAsDesired(boolean desiredPowerState,
            Message onCompleteMsg) {

        mDesiredPowerState = desiredPowerState;

        if (mDesiredPowerState == false) {
            disconnectAllConnections(Phone.REASON_RADIO_TURNED_OFF, onCompleteMsg);
            return;
        }

        if (onCompleteMsg != null) {
            onCompleteMsg.sendToTarget();
        }
    }

    private boolean isReadyForData() {

        //TODO: Check voice call state, emergency call back info
        boolean isReadyForData = isDataConnectivityEnabled();

        RadioTechnology r = getRadioTechnology();
        if (mCheckForConnectivity) {

            boolean roaming = mDsst.getDataServiceState().getRoaming();
            isReadyForData = isReadyForData && (!roaming || getDataOnRoamingEnabled());

            int dataRegState = this.mDsst.getDataServiceState().getState();

            isReadyForData = isReadyForData
                && ((dataRegState == ServiceState.STATE_IN_SERVICE
                            && r != RadioTechnology.RADIO_TECH_UNKNOWN)
                        || mNoAutoAttach);
        }

        if (mCheckForSubscription) {
            if (r.isGsm()
                    || (r == RadioTechnology.RADIO_TECH_EHRPD
                        && mDsst.mCdmaSubscriptionSource != Phone.CDMA_SUBSCRIPTION_NV)
                    || (r.isUnknown() && mNoAutoAttach)) {
                isReadyForData = isReadyForData && mDsst.mSimRecords != null
                    && mDsst.mSimRecords.getRecordsLoaded() && !mIsPsRestricted;
            }

            if (r.isCdma()) {
                isReadyForData = isReadyForData
                    && (mDsst.mCdmaSubscriptionSource == Phone.CDMA_SUBSCRIPTION_NV
                            || (mDsst.mRuimRecords != null
                                && mDsst.mRuimRecords.getRecordsLoaded()));
            }
        }
        return isReadyForData;
    }

    /**
     * The only circumstances under which we report that data connectivity is not
     * possible are
     * <ul>
     * <li>Data roaming is disallowed and we are roaming.</li>
     * <li>The current data state is {@code DISCONNECTED} for a reason other than
     * having explicitly disabled connectivity. In other words, data is not available
     * because the phone is out of coverage or some like reason.</li>
     * </ul>
     * @return {@code true} if data connectivity is possible, {@code false} otherwise.
     */
    public boolean isDataConnectivityPossible() {
        //TODO: is there any difference from isReadyForData()?
        return isReadyForData();
    }

    private RadioTechnology getRadioTechnology() {
        if (mCheckForConnectivity) {
            return RadioTechnology.getRadioTechFromInt(mDsst.getDataServiceState()
                    .getRadioTechnology());
        } else {
            // Workaround to choose a technology for SETUP_DATA_CALL when
            // mCheckForConnectivity is set to false. We need a technology for
            // SETUP_DATA_CALL.
            return RadioTechnology.RADIO_TECH_1xRTT;
        }
    }

    public String dumpDataReadinessinfo() {
        StringBuilder sb = new StringBuilder();
        sb.append("[DataRadioTech = ").append(getRadioTechnology());
        sb.append(", data network state = ").append(mDsst.getDataServiceState().getState());
        sb.append(", mMasterDataEnabled = ").append(mMasterDataEnabled);
        sb.append(", is Roaming = ").append(mDsst.getDataServiceState().getRoaming());
        sb.append(", dataOnRoamingEnable = ").append(getDataOnRoamingEnabled());
        sb.append(", isPsRestricted = ").append(mIsPsRestricted);
        sb.append(", desiredPowerState  = ").append(getDesiredPowerState());
        sb.append(", mSIMRecords = ");
        if (mDsst.mSimRecords != null)
            sb.append(mDsst.mSimRecords.getRecordsLoaded())
                    .append("/"+mDsst.mSimRecords.getSIMOperatorNumeric());
        sb.append(", cdmaSubSource = ").append(mDsst.mCdmaSubscriptionSource);
        if (mDsst.mCdmaSubscriptionSource == Phone.CDMA_SUBSCRIPTION_NV)
            sb.append("/"+mCdmaHomeOperatorNumeric);
        sb.append(", mRuimRecords = ");
        if (mDsst.mRuimRecords != null)
            sb.append(mDsst.mRuimRecords.getRecordsLoaded())
                .append("/"+mDsst.mRuimRecords.getRUIMOperatorNumeric());
        sb.append(", checks = ").append(mCheckForConnectivity).append("/")
            .append(mCheckForSubscription);
        sb.append("]");
        return sb.toString();
    }

    void dumpDataCalls() {
        logv("---dc list---");
        for (DataConnection dc: mDataConnectionList) {
            if (dc.isInactive() == false) {
                StringBuilder sb = new StringBuilder();
                sb.append("cid = " + dc.cid);
                sb.append(", state = "+dc.getStateAsString());
                sb.append(", bearerType = "+dc.getBearerType());
                sb.append(", ipaddress = "+dc.ipAddrtoString());
                sb.append(", gw="+dc.gwAddrtoString());
                sb.append(", dns="+ Arrays.toString(dc.getDnsServers()));
                logv(sb.toString());
            }
        }
    }

    void dumpDataServiceTypes() {
        logv("---ds list---");
        for (DataServiceType ds: DataServiceType.values()) {
            StringBuilder sb = new StringBuilder();
            sb.append("ds= " + ds);
            sb.append(", enabled = "+mDpt.isServiceTypeEnabled(ds));
            sb.append(", active = v4:")
            .append(mDpt.getState(ds, IPVersion.INET));
            if (mDpt.isServiceTypeActive(ds, IPVersion.INET)) {
                sb.append("("+mDpt.getActiveDataConnection(ds, IPVersion.INET).cid+")");
            }
            sb.append(" v6:")
            .append(mDpt.getState(ds, IPVersion.INET6));
            if (mDpt.isServiceTypeActive(ds, IPVersion.INET6)) {
                sb.append("("+mDpt.getActiveDataConnection(ds, IPVersion.INET6).cid+")");
            }
            logv(sb.toString());
        }
    }

    class CallbackData {
        DataConnection dc;
        DataProfile dp;
        IPVersion ipv;
        String reason;
        DataServiceType ds;
    }

    private boolean tryDisconnectDataCall(DataConnection dc, String reason) {
        logv("tryDisconnectDataCall : dc=" + dc + ", reason=" + reason);

        CallbackData c = new CallbackData();
        c.dc = dc;
        c.reason = reason;

        int dcReason = 0;
        // Set the reason. Currently indicating explicit powerdown reasons only
        if (Phone.REASON_RADIO_TURNED_OFF.equals(reason))
            dcReason = DEACTIVATE_REASON_RADIO_OFF;
        else
            dcReason = DEACTIVATE_REASON_NONE;

        dc.disconnect(obtainMessage(EVENT_DISCONNECT_DONE, dcReason, 0, c));
        return true;
    }

    private boolean trySetupDataCall(DataServiceType ds, IPVersion ipv, String reason) {
        logv("trySetupDataCall : ds=" + ds + ", ipv=" + ipv + ", reason=" + reason);
        DataProfile dp = mDpt.getNextWorkingDataProfile(ds, getDataProfileTypeToUse(), ipv);
        if (dp == null) {
            logw("no working data profile available to establish service type " + ds + "on " + ipv);
            mDpt.setState(State.FAILED, ds, ipv);
            notifyDataConnection(ds, ipv, reason);
            return false;
        }

        /* If MPDN is disabled, we ensure that only one PDN of highest priority is active.
         *
         * Also, for OMH arbitration, check if there is an existing OMH profile.
         * If there is at least one, then we assume that the device is working in the
         * OMH enabled mode and has one or more OMH profile(s) and needs arbitration.
         * If not, we allow the device to operate using other non OMH profiles.
         */
        if(SUPPORT_MPDN == false ||
                mDpt.isAnyDataProfileAvailable(DataProfileType.PROFILE_TYPE_3GPP2_OMH)) {
            // If the call indeed got disconnected return, otherwise pass through
            if (disconnectOneLowPriorityDataCall(ds, reason)) {
                logw("Lower/Equal priority call disconnected.");
                return true;
            }
            if(isHigherPriorityDataCallActive(ds)) {
                logw("Higher priority call active. Ignoring setup data call request.");
                return false;
            }
        }

        DataConnection dc = getFreeDataConnection();
        if (dc == null) {
            // if this happens, it probably means that our data call list is not
            // big enough!
            boolean ret = SUPPORT_MPDN_SERVICE_ARBITRATION && disconnectOneLowPriorityDataCall(ds, reason);
            // irrespective of ret, we should return true here
            // - if a call was indeed disconnected, then updateDataConnections()
            //   will take care of setting up call again
            // - if no calls were disconnected, then updateDataConnections will fail for every
            //   service type anyway.
            return true;
        }

        mDpt.setState(State.CONNECTING, ds, ipv);
        notifyDataConnection(ds, ipv, reason);

        mDataCallSetupPending = true;

        //Assertion: dc!=null && dp!=null
        CallbackData c = new CallbackData();
        c.dc = dc;
        c.dp = dp;
        c.ds = ds;
        c.ipv = ipv;
        c.reason = reason;

        /*
         * Decide the bearer type to use..criterion is as follows:
         *
         * 1. if IPV4 & IPV6 are both not active and if we have a data profile
         * that supports dual bearer then set bearer type as dual bearer.
         * 2. else just use the bearer type corresponding to ip version that
         * was requested.
         */

        BearerType b = ipv == IPVersion.INET ? BearerType.IP : BearerType.IPV6;
        if (mDpt.isServiceTypeActive(ds, IPVersion.INET) == false
                && mDpt.isServiceTypeActive(ds, IPVersion.INET6) == false
                && dp.getBearerType() == BearerType.IPV4V6
                && SUPPORT_IPV4V6) {
            b = BearerType.IPV4V6;
        }

        dc.connect(getRadioTechnology(), dp, b, obtainMessage(EVENT_CONNECT_DONE, c));
        return true;
    }

    private DataProfileType getDataProfileTypeToUse() {
        DataProfileType type = null;
        RadioTechnology r = getRadioTechnology();
        if (r == RadioTechnology.RADIO_TECH_UNKNOWN || r == null) {
            type = null;
        } else if (r.isGsm() || r == RadioTechnology.RADIO_TECH_EHRPD) {
            type = DataProfileType.PROFILE_TYPE_3GPP_APN;
        } else {
            if (mDpt.isOmhEnabled()) {
                type = DataProfileType.PROFILE_TYPE_3GPP2_OMH;
            } else {
                type = DataProfileType.PROFILE_TYPE_3GPP2_NAI;
            }
        }
        return type;
    }

    private void createDataConnectionList() {
        mDataConnectionList = new ArrayList<DataConnection>();
        DataConnection dc;

        for (int i = 0; i < DATA_CONNECTION_POOL_SIZE; i++) {
            dc = MMDataConnection.makeDataConnection(this);
            ((MMDataConnection)dc).setAvailable(true);
            mDataConnectionList.add(dc);
        }
    }

    private void destroyDataConnectionList() {
        if (mDataConnectionList != null) {
            mDataConnectionList.removeAll(mDataConnectionList);
        }
    }

    private MMDataConnection getFreeDataConnection() {
        for (DataConnection conn : mDataConnectionList) {
            MMDataConnection dc = (MMDataConnection) conn;
            if (dc.isAvailable() && dc.isInactive()) {
                dc.setAvailable(false);
                return dc;
            }
        }
        return null;
    }

    private void freeDataConnection(MMDataConnection dc) {
        if (dc.isInactive() == false) {
            loge("Assertion failed : Freeing inActive data call!");
        }
        if (dc.isAvailable() == true) {
            loge("Assertion failed : freeDataCall when isAvailable() is already true!");
        }
        dc.setAvailable(true);
    }

    protected void startNetStatPoll() {
        if (mPollNetStat.isEnablePoll() == false) {
            mPollNetStat.resetPollStats();
            mPollNetStat.setEnablePoll(true);
            mPollNetStat.run();
        }
    }

    protected void stopNetStatPoll() {
        mPollNetStat.setEnablePoll(false);
        removeCallbacks(mPollNetStat);
    }

    // Retrieve the data roaming setting from the shared preferences.
    public boolean getDataOnRoamingEnabled() {
        try {
            return Settings.Secure.getInt(mContext.getContentResolver(),
                    Settings.Secure.DATA_ROAMING) > 0;
        } catch (SettingNotFoundException snfe) {
            return false;
        }
    }

    public ServiceState getDataServiceState() {
        return mDsst.getDataServiceState();
    }

    @Override
    protected boolean isConcurrentVoiceAndData() {
        return mDsst.isConcurrentVoiceAndData();
    }

    public void setDataReadinessChecks(
            boolean checkConnectivity, boolean checkSubscription, boolean tryDataCalls) {
        mCheckForConnectivity = checkConnectivity;
        mCheckForSubscription = checkSubscription;
        if (tryDataCalls) {
            updateDataConnections(REASON_DATA_READINESS_CHECKS_MODIFIED);
        }
    }

    public DataActivityState getDataActivityState() {
        DataActivityState ret = DataActivityState.NONE;
        if (getDataServiceState().getState() == ServiceState.STATE_IN_SERVICE) {
            switch (mPollNetStat.getActivity()) {
                case DATAIN:
                    ret = DataActivityState.DATAIN;
                    break;
                case DATAOUT:
                    ret = DataActivityState.DATAOUT;
                    break;
                case DATAINANDOUT:
                    ret = DataActivityState.DATAINANDOUT;
                    break;
                case DORMANT:
                    ret = DataActivityState.DORMANT;
                    break;
            }
        }
        return ret;
    }

    @SuppressWarnings("unchecked")
    public List<DataConnection> getCurrentDataConnectionList() {
        ArrayList<DataConnection> dcs = (ArrayList<DataConnection>) mDataConnectionList.clone();
        return dcs;
    }

    public void registerForDataServiceStateChanged(Handler h, int what, Object obj) {
        mDsst.registerForServiceStateChanged(h, what, obj);
    }

    public void unregisterForDataServiceStateChanged(Handler h) {
        mDsst.unregisterForServiceStateChanged(h);
    }

    public void setSubscriptionInfo(Subscription subData) {
        // Update the subscription info only if this is the DDS.
        if (subData.subId == PhoneFactory.getDataSubscription()) {
            mSubscriptionData = subData;
            mDsst.updateIccAvailability();
            mDpt.setSubscription(mSubscriptionData.subId);
        }
    }

    public Subscription getSubscriptionInfo() {
        return mSubscriptionData;
    }

    public int getSubscription() {
        if (mSubscriptionData != null) {
           return mSubscriptionData.subId;
        }

        return TelephonyManager.DEFAULT_SUB;
    }

    @Override
    public int enableQos(QosSpec qosSpec, String type, IPVersion ipv) {
        int result = Phone.QOS_REQUEST_FAILURE;

        DataServiceType serviceType = DataServiceType.apnTypeStringToServiceType(type);

        if (serviceType != null) {
            Log.d(LOG_TAG, "enableQos: serviceType:" + serviceType +
                                        " userData:" + qosSpec.getUserData());

            DataConnection dc = mDpt.getActiveDataConnection(serviceType, ipv);
            if (dc != null) {
                dc.qosSetup(qosSpec);
                result = Phone.QOS_REQUEST_SUCCESS;
            }
            else
                Log.e(LOG_TAG, "enableQos: Did not find a data connection!");
        }

        return result;
    }

    @Override
    public int disableQos(int qosId) {
        int result = Phone.QOS_REQUEST_FAILURE;
        Log.d(LOG_TAG, "disableQos:" + qosId);

        for (DataConnection dc: mDataConnectionList) {
            if (dc.isValidQos(qosId)) {
                dc.qosRelease(qosId);
                result = Phone.QOS_REQUEST_SUCCESS;
                break;
            }
        }

        return result;
    }

    @Override
    public int modifyQos(int qosId, QosSpec qosSpec) {
        int result = Phone.QOS_REQUEST_FAILURE;
        Log.d(LOG_TAG, "modifyQos:" + qosId);

        for (DataConnection dc: mDataConnectionList) {
            if (dc.isValidQos(qosId)) {
                dc.qosModify(qosId, qosSpec);
                result = Phone.QOS_REQUEST_SUCCESS;
                break;
            }
        }

        return result;
    }

    @Override
    public int suspendQos(int qosId) {
        int result = Phone.QOS_REQUEST_FAILURE;
        Log.d(LOG_TAG, "suspendQos:" + qosId);

        for (DataConnection dc: mDataConnectionList) {
            if (dc.isValidQos(qosId)) {
                dc.qosSuspend(qosId);
                result = Phone.QOS_REQUEST_SUCCESS;
                break;
            }
        }

        return result;
    }

    @Override
    public int resumeQos(int qosId) {
        int result = Phone.QOS_REQUEST_FAILURE;
        Log.d(LOG_TAG, "resumeQos:" + qosId);

        for (DataConnection dc: mDataConnectionList) {
            if (dc.isValidQos(qosId)) {
                dc.qosResume(qosId);
                result = Phone.QOS_REQUEST_SUCCESS;
                break;
            }
        }

        return result;
    }

    @Override
    public int getQosStatus(int qosId) {
        int result = Phone.QOS_REQUEST_FAILURE;
        Log.d(LOG_TAG, "getQosStatus:" + qosId);

        for (DataConnection dc: mDataConnectionList) {
            if (dc.isValidQos(qosId)) {
                dc.getQosStatus(qosId);
                result = Phone.QOS_REQUEST_SUCCESS;
                break;
            }
        }

        return result;
    }

    void loge(String string) {
        Log.e(LOG_TAG, "[DCT" + (mSubscriptionData!=null ? "("+mSubscriptionData.subId+")" : "")
                              + " ] " + string);
    }

    void logw(String string) {
        Log.w(LOG_TAG, "[DCT" + (mSubscriptionData!=null ? "("+mSubscriptionData.subId+")" : "")
                              + " ] " + string);
    }

    void logd(String string) {
        Log.d(LOG_TAG, "[DCT" + (mSubscriptionData!=null ? "("+mSubscriptionData.subId+")" : "")
                              + " ] " + string);
    }

    void logv(String string) {
        Log.v(LOG_TAG, "[DCT" + (mSubscriptionData!=null ? "("+mSubscriptionData.subId+")" : "")
                              + " ] " + string);
    }

    void logi(String string) {
        Log.i(LOG_TAG, "[DCT" + (mSubscriptionData!=null ? "("+mSubscriptionData.subId+")" : "")
                              + " ] " + string);
    }
}
