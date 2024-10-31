/*
 * Copyright (C) 2006 The Android Open Source Project
 * Copyright (C) 2009-2011, The Linux Foundation. All rights reserved.
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

import com.android.internal.net.IPVersion;
import com.android.internal.telephony.ITelephony;
import com.android.internal.telephony.Phone;
import com.android.internal.telephony.TelephonyIntents;
import com.android.internal.telephony.QosSpec;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.DhcpInfo;
import android.net.FmcNotifier;
import android.net.IConSvcEventListener;
import android.net.IFmcEventListener;
import android.net.ILinkSocketMessageHandler;
import android.net.LinkCapabilities;
import android.net.ExtraLinkCapabilities;
import android.net.QoSTracker;
import android.net.LinkInfo;
import android.net.LinkNotifier;
import android.net.LinkProperties;
import android.net.LinkProvider;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.net.NetworkInfo;
import android.net.LinkProvider.LinkRequirement;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.provider.Settings;
import android.telephony.PhoneStateListener;
import android.telephony.ServiceState;
import android.telephony.SignalStrength;
import android.telephony.TelephonyManager;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.net.UnknownHostException;
import java.sql.Timestamp;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Collection;
import java.util.Collections;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.StringTokenizer;
import java.util.Map.Entry;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Pattern;
import android.os.INetworkManagementService;

/**
 * {@hide}
 */
class CNERequest {
    static final String LOG_TAG = "CNE";         // global logcat tag
    static final String LOCAL_TAG = "CNE_DEBUG"; // local logcat tag

    // ***** Class Variables
    static int sNextSerial = 0;
    static Object sSerialMonitor = new Object();
    private static Object sPoolSync = new Object();
    private static CNERequest sPool = null;
    private static int sPoolSize = 0;
    private static final int MAX_POOL_SIZE = 4;

    // ***** Instance Variables
    int mSerial;
    int mRequest;
    Message mResult;
    Parcel mp;
    CNERequest mNext;

    /**
     * Retrieves a new CNERequest instance from the pool.
     *
     * @param request
     *            CNE_REQUEST_*
     * @param result
     *            sent when operation completes
     * @return a CNERequest instance from the pool.
     */
    static CNERequest obtain(int request) {
        CNERequest rr = null;

        synchronized (sPoolSync) {
            if (sPool != null) {
                rr = sPool;
                sPool = rr.mNext;
                rr.mNext = null;
                sPoolSize--;
            }
        }

        if (rr == null) {
            rr = new CNERequest();
        }

        synchronized (sSerialMonitor) {
            rr.mSerial = sNextSerial++;
        }
        rr.mRequest = request;
        rr.mp = Parcel.obtain();
        // first elements in any CNE Parcel
        rr.mp.writeInt(request);
        rr.mp.writeInt(rr.mSerial);
        return rr;
    }

    /**
     * Returns a CNERequest instance to the pool.
     *
     * Note: This should only be called once per use.
     */
    void release() {
        synchronized (sPoolSync) {
            if (sPoolSize < MAX_POOL_SIZE) {
                this.mNext = sPool;
                sPool = this;
                sPoolSize++;
            }
        }
    }

    private CNERequest() {
        return;
    }

    static void resetSerial() {
        synchronized (sSerialMonitor) {
            sNextSerial = 0;
        }
    }

    String serialString() {
        // Cheesy way to do %04d
        StringBuilder sb = new StringBuilder(8);
        String sn;

        sn = Integer.toString(mSerial);

        // sb.append("J[");
        sb.append('[');
        for (int i = 0, s = sn.length(); i < 4 - s; i++) {
            sb.append('0');
        }

        sb.append(sn);
        sb.append(']');
        return sb.toString();
    }

    void onError(int error) {
        return;
    }
}

public final class CNE implements ILinkManager {
    static final String LOG_TAG = "CNE";         // global logcat tag
    static final String LOCAL_TAG = "CNE_DEBUG"; // local logcat tag
    private static final boolean DBG = true;     // enable local logging?

    // ***** Instance Variables
    LocalSocket mSocket;
    HandlerThread mSenderThread;
    CNESender mSender;
    Thread mReceiverThread;
    CNEReceiver mReceiver;
    private Context mContext;
    int mRequestMessagesPending;
    private static int mSocketId = 0;

    ArrayList<CNERequest> mRequestsList = new ArrayList<CNERequest>();
    //both Capabilities and mQosTrackers must have synchronized access
    Map<Integer, ExtraLinkCapabilities> activeCapabilities;
    Collection<QoSTracker> mQosTrackers;

    /* to do move all the constants to one file */

    // ***** Events

    static final int EVENT_SEND = 1;

    // ***** Constants

    /* CNE feature flag */
    static boolean isCndUp = false;
    // match with constant in CNE.cpp
    static final int CNE_MAX_COMMAND_BYTES = (8 * 1024);
    static final int RESPONSE_SOLICITED = 0;
    static final int RESPONSE_UNSOLICITED = 1;

    static final String SOCKET_NAME_CNE = "cnd";

    static final int SOCKET_OPEN_RETRY_MILLIS = 4 * 1000;

    /* Different requests types - corresponding to cnd_commands.h */
    static final int CNE_REQUEST_INIT = 1;
    static final int CNE_REQUEST_REG_ROLE = 2;
    static final int CNE_REQUEST_GET_COMPATIBLE_NWS = 3;
    static final int CNE_REQUEST_CONF_NW = 4;
    static final int CNE_REQUEST_DEREG_ROLE = 5;
    static final int CNE_REQUEST_REG_NOTIFICATIONS = 6;
    static final int CNE_REQUEST_UPDATE_BATTERY_INFO = 7;
    static final int CNE_REQUEST_UPDATE_WLAN_INFO = 8;
    static final int CNE_REQUEST_UPDATE_WWAN_INFO = 9;
    static final int CNE_NOTIFY_RAT_CONNECT_STATUS = 10;
    static final int CNE_NOTIFY_DEFAULT_NW_PREF = 11;
    static final int CNE_REQUEST_UPDATE_WLAN_SCAN_RESULTS = 12;
    static final int CNE_NOTIFY_SENSOR_EVENT_CMD = 13;
    static final int CNE_REQUEST_CONFIG_IPROUTE2_CMD = 14;
    static final int CNE_NOTIFY_TIMER_EXPIRED_CMD = 15;
    static final int CNE_REQUEST_START_FMC_CMD = 16;
    static final int CNE_REQUEST_STOP_FMC_CMD = 17;
    static final int CNE_REQUEST_UPDATE_WWAN_DORMANCY_INFO = 18;
    static final int CNE_REQUEST_UPDATE_DEFAULT_NETWORK_INFO = 19;

    /* UNSOL Responses - corresponding to cnd_unsol_messages.h */
    static final int CNE_RESPONSE_REG_ROLE = 1;
    static final int CNE_RESPONSE_GET_BEST_NW = 2;
    static final int CNE_RESPONSE_CONFIRM_NW = 3;
    static final int CNE_RESPONSE_DEREG_ROLE = 4;
    /* UNSOL Events */
    static final int CNE_REQUEST_BRING_RAT_DOWN = 5;
    static final int CNE_REQUEST_BRING_RAT_UP = 6;
    static final int CNE_NOTIFY_MORE_PREFERED_RAT_AVAIL = 7;
    static final int CNE_NOTIFY_RAT_LOST = 8;
    static final int CNE_REQUEST_START_SCAN_WLAN = 9;
    static final int CNE_NOTIFY_INFLIGHT_STATUS = 10;
    static final int CNE_NOTIFY_FMC_STATUS = 11;
    static final int CNE_NOTIFY_HOST_ROUTING_IP = 12;

    /* RAT type - corresponding to CneRatType */
    static final int CNE_RAT_MIN = 0;
    static final int CNE_RAT_WWAN = CNE_RAT_MIN;
    static final int CNE_RAT_WLAN = 1;
    static final int CNE_RAT_ANY = 2;
    static final int CNE_RAT_NONE = 3;
    static final int CNE_RAT_MAX = 4;
    static final int CNE_RAT_INVALID = CNE_RAT_MAX;

    /* different status codes */
    public static final int STATUS_FAILURE = 0;
    public static final int STATUS_SUCCESS = 1;

    public static final int STATUS_NOT_INFLIGHT = 0;
    public static final int STATUS_INFLIGHT = 1;

    private static NetworkInfo.State ipv6NetState = NetworkInfo.State.DISCONNECTED;

    static final int CNE_REGID_INVALID = -1;
    static final int CNE_ROLE_INVALID = -1;
    static final int CNE_DEFAULT_CON_REGID = 0;
    static final int CNE_INVALID_PID = -1;

    static final int CNE_LINK_SATISFIED = 1;
    static final int CNE_LINK_NOT_SATISFIED = 0;

    static final int CNE_MASK_ON_LINK_AVAIL_SENT = 0x0001;
    static final int CNE_MASK_ON_BETTER_LINK_AVAIL_SENT = 0x0002;

    static final int CNE_NET_SUBTYPE_WLAN_B = 20;
    static final int CNE_NET_SUBTYPE_WLAN_G = 21;

    //TODO move this down to QCNE, java should have no knowledge of this role.
    /* QoS Role range, 1000 < QoSRoleInt < 2000 */
    private static final int QOS_ROLE_MIN = 1001;
    private static final int QOS_ROLE_MAX = 1999;


    private static int mRoleRegId = 0;
    private WifiManager mWifiManager;
    private TelephonyManager mTelephonyManager;
    private ConnectivityService mService;
    private int mNetworkPreference;
    private int mDefaultNetwork = ConnectivityManager.MAX_NETWORK_TYPE;

    private SignalStrength mSignalStrength = new SignalStrength();
    ServiceState mServiceState;
    private String activeWlanIfName = null;
    private String activeWwanV4IfName = null;
    private String activeWwanV6IfName = null;
    private String activeWlanGatewayAddr = null;
    private String activeWwanV4GatewayAddr = null;
    private String hostRoutingIpAddr = null;
    private static boolean mRemoveHostEntry = false;

    private static boolean isWwanDormant = false;

    private class AddressInfo {
        String ipAddr;
        String gatewayAddr;
        String ifName;

        public AddressInfo() {
            ipAddr = null;
            gatewayAddr = null;
            ifName = null;
        }
    }

    AddressInfo getWlanAddrInfo(IPVersion version) {
        AddressInfo wlanAddrInfo = new AddressInfo();
        String ipAddr = null;
        String gatewayAddr = null;
        if (version == IPVersion.INET) {
            try {
                DhcpInfo dhcpInfo = mWifiManager.getDhcpInfo();
                int ipAddressInt = dhcpInfo.ipAddress;
                int gatewayInt = dhcpInfo.gateway;
                if (ipAddressInt != 0) {
                    ipAddr = ((ipAddressInt) & 0xff) + "."
                            + ((ipAddressInt >> 8) & 0xff) + "."
                            + ((ipAddressInt >> 16) & 0xff) + "."
                            + ((ipAddressInt >> 24) & 0xff);
                }
                if (gatewayInt != 0) {
                    gatewayAddr = ((gatewayInt) & 0xff) + "."
                            + ((gatewayInt >> 8) & 0xff) + "."
                            + ((gatewayInt >> 16) & 0xff) + "."
                            + ((gatewayInt >> 24) & 0xff);
                }
                wlanAddrInfo.ipAddr = ipAddr;
                wlanAddrInfo.gatewayAddr = gatewayAddr;
                InetAddress inetAddr = InetAddress.getByName(ipAddr);
                NetworkInterface netIface = NetworkInterface.getByInetAddress(inetAddr);
                wlanAddrInfo.ifName = netIface.getName();
            } catch (SocketException sexp) {
                logw("Could not get the netIface obj");
            } catch (NullPointerException nexp) {
                // WiFi is probably down.
                logw("DhcpInfo obj is NULL. V4 address is not yet available.");
            } catch (UnknownHostException uexp) {
                logw( "Invalid host name" + ipAddr);
            }
        } else if (version == IPVersion.INET6) {
            /*
             * currently wifistate tracker is not supporting V6
             * address, without this if V6 interface is brought up by
             * kernel, we can get the V6 address info if we have a
             * valid v4 address.
             */
            AddressInfo v4Addr = getWlanAddrInfo(IPVersion.INET);
            try {
                InetAddress inet4Addr = InetAddress.getByName(v4Addr.ipAddr);
                NetworkInterface netIface = NetworkInterface.getByInetAddress(inet4Addr);
                wlanAddrInfo.ifName = netIface.getName();
                Enumeration<InetAddress> e = netIface.getInetAddresses();
                while (e.hasMoreElements()) {
                    InetAddress inetAddr = (InetAddress) e.nextElement();
                    if (!inetAddr.equals(inet4Addr)) {
                        wlanAddrInfo.ipAddr = inetAddr.getHostAddress();
                        // TODO: get the gateway addr for v6
                    }
                }
            } catch (SocketException sexp) {
                logw("Could not get the netIface obj");
            } catch (NullPointerException nexp) {
                logw("V4Adrr is not yet available");
            } catch (UnknownHostException uexp) {
                logw("Can't get V6Addr without valid V4Addr= " + v4Addr);
            }
        } else {
            loge("Unsupported ipversion." + version);
        }
        return wlanAddrInfo;
    }

    AddressInfo getWwanAddrInfo(String apnType, IPVersion ipv) {
        AddressInfo wwanAddrInfo = new AddressInfo();
        try {
            wwanAddrInfo.ifName = mTelephonyManager.getActiveInterfaceName(apnType, ipv);
            wwanAddrInfo.ipAddr = mTelephonyManager.getActiveIpAddress(apnType, ipv);
            wwanAddrInfo.gatewayAddr = mTelephonyManager.getActiveGateway(apnType, ipv);
        } catch (NullPointerException nexp) {
            logw("mTelephonyManager is null");
        }
        return wwanAddrInfo;
    }

    BroadcastReceiver mIntentReceiver = new BroadcastReceiver() {
        public void onReceive(Context context, Intent intent) {
            final String action = intent.getAction();

            if (action.equals(Intent.ACTION_BATTERY_CHANGED)) {
                dlogi( "CNE received action: " + action);
                double level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, 0);
                double scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, 1);
                int pluginType = intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0);
                int status = intent.getIntExtra(BatteryManager.EXTRA_STATUS, 0);
                int normalizedLevel = (int)((level/scale) * 100);
                updateBatteryStatus(status, pluginType, normalizedLevel);

            } else if (action.equals(Intent.ACTION_SHUTDOWN)) {
                    dlogi("CNE received action: " + action);
                    stopFmc(null);

            } else if (action.equals(WifiManager.RSSI_CHANGED_ACTION)) {
                dlogi( "CNE received action RSSI Changed events: " + action);
                if (mWifiManager != null) {
                    String ipV4Addr = null;
                    String iface = null;
                    ConnectivityManager cm = (ConnectivityManager) mContext
                            .getSystemService(Context.CONNECTIVITY_SERVICE);
                    NetworkInfo networkInfo = cm.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
                    NetworkInfo.State networkState = (networkInfo
                            == null ? NetworkInfo.State.UNKNOWN : networkInfo.getState());
                    if (networkState == NetworkInfo.State.CONNECTED) {
                        AddressInfo aInfo = getWlanAddrInfo(IPVersion.INET);
                        ipV4Addr = aInfo.ipAddr;
                        iface = aInfo.ifName;
                    }
                    WifiInfo wifiInfo = mWifiManager.getConnectionInfo();
                    String ssid = wifiInfo.getSSID();
                    int rssi = wifiInfo.getRssi();
                    Timestamp ts = new Timestamp(System.currentTimeMillis());
                    String tsStr = ts.toString();
                    logi("CNE received action RSSI Changed events -"
                            + " networkState: " + networkState
                            + " ssid= " + ssid
                            + " rssi=" + rssi
                            + " ipV4Addr=" + ipV4Addr
                            + " timeStamp:" + tsStr
                            + " networkState: " + networkState);
                    updateWlanStatus(CNE_NET_SUBTYPE_WLAN_G, NetworkStateToInt(networkState), rssi,
                            ssid, ipV4Addr, iface, tsStr);
                } else {
                    logw("CNE received action RSSI Changed events, null WifiManager");
                }

            } else if ((action.equals(WifiManager.NETWORK_STATE_CHANGED_ACTION)) ||
                       (action.equals(WifiManager.WIFI_STATE_CHANGED_ACTION)) ||
                       (action.equals(WifiManager.NO_MORE_WIFI_LOCKS))) {

                dlogi("CNE received action Network/Wifi State Changed: " + action);

                if (mWifiManager != null) {
                    AddressInfo wlanV4Addr = getWlanAddrInfo(IPVersion.INET);
                    NetworkInfo.State networkState = NetworkInfo.State.UNKNOWN;
                    if(action.equals(WifiManager.NETWORK_STATE_CHANGED_ACTION) ||
                       action.equals(WifiManager.NO_MORE_WIFI_LOCKS)) {
                        NetworkInfo networkInfo = (NetworkInfo) intent
                                .getParcelableExtra(WifiManager.EXTRA_NETWORK_INFO);
                        networkState = (networkInfo == null ? NetworkInfo.State.UNKNOWN
                                : networkInfo.getState());
                    }
                    if (action.equals(WifiManager.WIFI_STATE_CHANGED_ACTION)) {
                        final boolean enabled = intent.getIntExtra(WifiManager.EXTRA_WIFI_STATE,
                                WifiManager.WIFI_STATE_UNKNOWN) == WifiManager.WIFI_STATE_ENABLED;
                        if (!enabled) {
                            networkState = NetworkInfo.State.DISCONNECTED;
                        } else {
                            networkState = NetworkInfo.State.UNKNOWN;
                        }
                    }
                    dlogi("CNE received action Network/Wifi State Changed"
                                + " networkState: " + networkState);
                    if (networkState == NetworkInfo.State.CONNECTED) {
                            activeWlanIfName = wlanV4Addr.ifName;
                            activeWlanGatewayAddr = wlanV4Addr.gatewayAddr;
                    }
                    WifiInfo wifiInfo = mWifiManager.getConnectionInfo();
                    String ssid = wifiInfo.getSSID();
                    int rssi = wifiInfo.getRssi();
                    Timestamp ts = new Timestamp(System.currentTimeMillis());
                    String tsStr = ts.toString();
                    dlogi("CNE received action Network/Wifi State Changed -"
                                + " ssid=" + ssid
                                + " rssi=" + rssi
                                + " networkState=" + networkState
                                + " ifName=" + wlanV4Addr.ifName
                                + " ipV4Addr=" + wlanV4Addr.ipAddr
                                + " gateway=" + wlanV4Addr.gatewayAddr
                                + " timeStamp=" + tsStr);
                    updateWlanStatus(CNE_NET_SUBTYPE_WLAN_G, NetworkStateToInt(networkState), rssi,
                            ssid, wlanV4Addr.ipAddr, wlanV4Addr.ifName, tsStr);
                    notifyRatConnectStatus(CNE_RAT_WLAN, NetworkStateToInt(networkState),
                            wlanV4Addr.ipAddr);
                } else {
                    logw("CNE received action Network State Changed, null WifiManager");
                }
            } else if (action.equals(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION)) {
                dlogi( "CNE received action: " + action);
                if (mWifiManager != null) {
                    //List<ScanResult> results = mWifiManager.getScanResults();
                    // updateWlanScanResults(results);
                }
            } else if (action.equals(TelephonyIntents.ACTION_ANY_DATA_CONNECTION_STATE_CHANGED)) {
                String apnType = intent.getStringExtra(Phone.DATA_APN_TYPES_KEY);
                if (apnType.equals(Phone.APN_TYPE_DEFAULT)) {
                    AddressInfo wwanV4AddrInfo = getWwanAddrInfo(Phone.APN_TYPE_DEFAULT,
                            IPVersion.INET);
                    IPVersion ipv = IPVersion.INET;
                    String str = intent.getStringExtra(Phone.DATA_IPVERSION_KEY);
                    if (str != null) {
                        ipv = Enum.valueOf(IPVersion.class, str);
                    }

                    str = intent.getStringExtra(Phone.DATA_APN_TYPE_STATE);
                    NetworkInfo.State netState = NetworkInfo.State.DISCONNECTED;
                    if (str != null) {
                        netState = convertToNetworkState(Enum.valueOf(Phone.DataState.class, str));
                    }
                    if (netState == NetworkInfo.State.CONNECTED) {
                        if (ipv == IPVersion.INET) {
                            activeWwanV4IfName = wwanV4AddrInfo.ifName;
                            activeWwanV4GatewayAddr = wwanV4AddrInfo.gatewayAddr;
                        } else {
                            if (ipv6NetState != netState) {
                                ipv6NetState = netState;
                                logd("Adding IPV6 default route for mobile");
                                mService.setDefaultRoute(ConnectivityManager.TYPE_MOBILE, ipv);
                            }
                        }
                    } else {
                        if (ipv == IPVersion.INET6) {
                            ipv6NetState = netState;
                        }
                    }

                    AddressInfo wwanAddrInfo;
                    if (ipv == IPVersion.INET) {
                        wwanAddrInfo = wwanV4AddrInfo;
                    } else {
                        Log.w(LOG_TAG, "IPV6 is not supported by CNE");
                        return;
                    }

                    int roaming = (int) (mTelephonyManager.isNetworkRoaming() ? 1 : 0);
                    int networkType = mTelephonyManager.getNetworkType();
                    int signalStrength = getSignalStrength(networkType);
                    Timestamp ts = new Timestamp(System.currentTimeMillis());
                    String tsStr = ts.toString();
                    dlogi("onDataConnectionStateChanged -"
                                + " networkType= " + networkType
                                + " networkState=" + netState
                                + " signalStr=" + signalStrength
                                + " roaming=" + roaming
                                + " ifName=" + wwanAddrInfo.ifName
                                + " ipAddr=" + wwanAddrInfo.ipAddr
                                + " gateway=" + wwanAddrInfo.gatewayAddr
                                + " timeStamp=" + tsStr);
                    updateWwanStatus(networkType, NetworkStateToInt(netState), signalStrength,
                            roaming, wwanV4AddrInfo.ipAddr, wwanAddrInfo.ifName, tsStr);
                    notifyRatConnectStatus(CNE_RAT_WWAN, NetworkStateToInt(netState),
                            wwanV4AddrInfo.ipAddr);
                } else {
                    loge("CNE currently does not support this apnType=" + apnType);
                }
            } else if (action.equals(TelephonyIntents.ACTION_QOS_STATE_IND)) {
                int qosIndState = intent.getIntExtra(QosSpec.QosIntentKeys.QOS_INDICATION_STATE, -1);
                int qosState = intent.getIntExtra(QosSpec.QosIntentKeys.QOS_STATUS, -1);
                int qosId = intent.getIntExtra(QosSpec.QosIntentKeys.QOS_ID, -1);
                int txId = intent.getIntExtra(QosSpec.QosIntentKeys.QOS_USERDATA, -1);
                QosSpec myQos = new QosSpec();
                myQos = (QosSpec) intent.getExtras().getParcelable(QosSpec.QosIntentKeys.QOS_SPEC);
                updateQosStatus(txId, qosId, qosIndState, qosState, myQos);
            } else {
                logw("CNE received unexpected action: " + action);
            }
        }
    };

    private void updateQosStatus (int txId, int qosId, int qosIndState, int qosState, QosSpec myQos) {
        // lookup using txid only when status is initiated since we dont have a qosid yet.
        QoSTracker qt = (qosIndState == QosSpec.QosIndStates.INITIATED) ?
            findQosBySocketId(txId) : findQosByQosId(qosId);
        dlogi("updateQosStatus got indication: " + qosIndState
                                 + " qosState: " + qosState
                                 + " txId: " + txId
                                 + " qosId: " + qosId);
        if (qt != null) qt.handleQosEvent(qosId, qosIndState, qosState, myQos);
        else logd("updateQosStatus did not find a handle to sid: " + txId + " qid: " + qosId);
    }

    private static enum RatTriedStatus {
        RAT_STATUS_TRIED, RAT_STATUS_NOT_TRIED
    }

    private class RatInfo {
        int rat;
        RatTriedStatus status;

        /* other rat related info passed by the callback */
        public RatInfo() {
            rat = CNE_RAT_INVALID;
        }

        public boolean equals(Object o) {
            if (o instanceof RatInfo) {
                RatInfo ratInfo = (RatInfo) o;
                if (this.rat == ratInfo.rat) {
                    return true;
                }
            }
            return false;
        }
    }

    private class CallbackInfo {
        IConSvcEventListener listener;
        boolean isNotifBetterRat;
    }

    private class RegInfo implements IBinder.DeathRecipient {
        private int role;
        private int regId;
        private int pid;
        private IBinder mBinder;
        private int notificationsSent;
        /*
         * do we want to have the copy of the Link Reqs?? here? if yes
         * which one
         */
        ArrayList<RatInfo> compatibleRatsList;
        int activeRat;
        int betterRat;
        /* pid and callbackInfo list */
        private CallbackInfo cbInfo;

        /* constructor */
        public RegInfo(IBinder binder) {
            role = CNE_ROLE_INVALID;
            regId = mRoleRegId++;
            pid = CNE_INVALID_PID;
            compatibleRatsList = new ArrayList<RatInfo>();
            activeRat = CNE_RAT_INVALID;
            betterRat = CNE_RAT_INVALID;
            cbInfo = new CallbackInfo();
            mBinder = binder;
            if (mBinder != null) {
                try {
                    dlogi( "CNE linking binder to death");
                    mBinder.linkToDeath(this, 0);
                } catch (RemoteException e) {
                    binderDied();
                }
            }
        }

        public void binderDied() {
            dlogi( "CNE binder died role=" + role + "regId =" + regId);
            releaseLink_LP(role, pid);
        }

        public void dump() {
            dlogi("     Role: " + role);
            dlogi("    RegId: " + regId);
            dlogi("      Pid: " + pid);
            dlogi("ActiveRat: " + activeRat);
            dlogi("BetterRat: " + betterRat);
            for (int index = 0; index < compatibleRatsList.size(); index++) {
                RatInfo ratInfo = (RatInfo) compatibleRatsList.get(index);
                dlogi("compatibleRat[" + index + "]=" + ratInfo.rat + " ratState = "
                        + ratInfo.status);
            }
        }
    }

    /* regId, RegInfo map */
    private ConcurrentHashMap<Integer, RegInfo> activeRegsList;

    /*
     * DefaultConnection Class: this class registers for default role
     * and keeps one connection up always if available ...so that the
     * device is always data connected and all the legacy apps that
     * are not using cne api have connectivity
     */
    class DefaultConnection {
        Map<LinkRequirement, String> mLinkReqs;
        MyLinkNotifier mLinkNotifier;

        /* Default constructor initializing linkrequirements to null */
        public DefaultConnection() {
            mLinkReqs = null;
        }

        /* constructor initializing the linkRequirements */
        public DefaultConnection(Map<LinkRequirement, String> linkReqs) {
            mLinkReqs = linkReqs;
        }

        /*
         * This function will start the connection with a default
         * role.
         */
        public void startConnection() {
            dlogi( "DefaultConnection startConnection called");
            mLinkNotifier = new MyLinkNotifier();
            getLink_LP(LinkProvider.ROLE_DEFAULT,
                       mLinkReqs,
                       mLinkNotifier.getCallingPid(),
                       mLinkNotifier);

        }

        /* Ends the previously started connection */
        public void endConnection() {
            dlogi( "DefaultConnection endConnection called");
            releaseLink_LP(LinkProvider.ROLE_DEFAULT,mLinkNotifier.getCallingPid());
        }

        /*
         * Implementation of the LinkNotifier interface to pass to the
         * LinkProvider class used to start connection
         */
        private class MyLinkNotifier extends IConSvcEventListener.Stub {
            /* default constructor */
            public MyLinkNotifier() {
                return;
            }

            /*
             * on the linkavailability notification we have to notify
             * it it to the iproute to set the default routing table
             */
            public void onLinkAvail(LinkInfo info) {
                dlogi( "DefaultConnection onLinkAvail called");
                reportLinkSatisfaction_LP(LinkProvider.ROLE_DEFAULT,
                                          getCallingPid(),
                                          info,
                                          true,
                                          true);
                /* notify to the default network */
                notifyDefaultNwChange(info.getNwId());
            }

            /*
             * Link Acquisition/startConnection as failed retry after
             * some time.
             */
            public void onGetLinkFailure(int reason) {
                dlogi( "DefaultConnection onGetLinkFailed reason=" + reason);
                /*
                 * we will not release the link here. we might get a
                 * better link avail call later.
                 */
            }

            /* a better link is available accept it. */
            public void onBetterLinkAvail(LinkInfo info) {
                dlogi( "DefaultConnection onBetterLinkAvail called");
                logi("onBetterLinkAvail pid= " + getCallingPid());
                switchLink_LP(LinkProvider.ROLE_DEFAULT,getCallingPid(),info, true);
                /* notify to the default network */
                notifyDefaultNwChange(info.getNwId());
            }

            /* current connection is lost. */
            public void onLinkLost(LinkInfo info) {
                dlogi( "DefaultConnection Link is lost");
            }
        }
    }

    private DefaultConnection mDefaultConn;

    class CNESender extends Handler implements Runnable {
        public CNESender(Looper looper) {
            super(looper);
        }

        // Only allocated once
        byte[] dataLength = new byte[4];

        // ***** Runnable implementation
        public void run() {
            // setup if needed
        }

        // ***** Handler implemementation
        public void handleMessage(Message msg) {
            CNERequest rr = (CNERequest) (msg.obj);
            CNERequest req = null;

            switch (msg.what) {
                case EVENT_SEND:
                    /**
                     * mRequestMessagePending++ already happened for
                     * every EVENT_SEND, thus we must make sure
                     * mRequestMessagePending-- happens once and only
                     * once
                     */
                    boolean alreadySubtracted = false;
                    try {
                        LocalSocket s;
                        s = mSocket;
                        if (s == null) {
                            rr.release();
                            mRequestMessagesPending--;
                            alreadySubtracted = true;
                            return;
                        }
                        synchronized (mRequestsList) {
                            mRequestsList.add(rr);
                        }
                        mRequestMessagesPending--;
                        alreadySubtracted = true;
                        byte[] data;
                        data = rr.mp.marshall();
                        rr.mp.recycle();
                        rr.mp = null;
                        if (data.length > CNE_MAX_COMMAND_BYTES) {
                            throw new RuntimeException("Parcel larger than max bytes allowed! "
                                    + data.length);
                        }

                        // parcel length in big endian
                        dataLength[0] = dataLength[1] = 0;
                        dataLength[2] = (byte) ((data.length >> 8) & 0xff);
                        dataLength[3] = (byte) ((data.length) & 0xff);
                        // dlogi( "writing packet: " + data.length + " bytes");
                        s.getOutputStream().write(dataLength);
                        s.getOutputStream().write(data);
                    } catch (IOException ex) {
                        logw("IOException " + ex);
                        req = findAndRemoveRequestFromList(rr.mSerial);
                        // make sure this request has not already been
                        // handled, eg, if CNEReceiver cleared the list.
                        if (req != null || !alreadySubtracted) {
                            rr.release();
                        }
                    } catch (RuntimeException exc) {
                        logw("Uncaught exception " + exc);
                        req = findAndRemoveRequestFromList(rr.mSerial);
                        // make sure this request has not already been
                        // handled, eg, if CNEReceiver cleared the list.
                        if (req != null || !alreadySubtracted) {
                            // rr.onError(GENERIC_FAILURE);
                            rr.release();
                        }
                    }

                    if (!alreadySubtracted) {
                        mRequestMessagesPending--;
                    }
                    break;
            }
        }
    }

    /**
     * Reads in a single CNE message off the wire. A CNE message
     * consists of a 4-byte little-endian length and a subsequent
     * series of bytes. The final message (length header omitted) is
     * read into <code>buffer</code> and the length of the final
     * message (less header) is returned. A return value of -1
     * indicates end-of-stream.
     *
     * @param is
     *            non-null; Stream to read from
     * @param buffer
     *            Buffer to fill in. Must be as large as maximum
     *            message size, or an ArrayOutOfBounds exception will
     *            be thrown.
     * @return Length of message less header, or -1 on end of stream.
     * @throws IOException
     */
    private static int readCneMessage(InputStream is, byte[] buffer) throws IOException {
        int countRead;
        int offset;
        int remaining;
        int messageLength;

        // First, read in the length of the message
        offset = 0;
        remaining = 4;
        do {
            countRead = is.read(buffer, offset, remaining);

            if (countRead < 0) {
                logw("Hit EOS reading message length");
                return -1;
            }

            offset += countRead;
            remaining -= countRead;
        } while (remaining > 0);

        messageLength = ((buffer[0] & 0xff) << 24)
                | ((buffer[1] & 0xff) << 16)
                | ((buffer[2] & 0xff) << 8)
                | (buffer[3] & 0xff);
        // Then, re-use the buffer and read in the message itself
        offset = 0;
        remaining = messageLength;
        do {
            countRead = is.read(buffer, offset, remaining);

            if (countRead < 0) {
                logw("Hit EOS reading message.  messageLength=" + messageLength
                        + " remaining=" + remaining);
                return -1;
            }

            offset += countRead;
            remaining -= countRead;
        } while (remaining > 0);

        return messageLength;
    }

    class CNEReceiver implements Runnable {
        byte[] buffer;

        CNEReceiver() {
            buffer = new byte[CNE_MAX_COMMAND_BYTES];
        }

        public void run() {
            int retryCount = 0;

            try {
                for (;;) {
                    LocalSocket s = null;
                    LocalSocketAddress l;

                    try {
                        dlogi( "CNE creating socket");
                        s = new LocalSocket();
                        l = new LocalSocketAddress(SOCKET_NAME_CNE,
                                LocalSocketAddress.Namespace.RESERVED);
                        s.connect(l);
                    } catch (IOException ex) {
                        try {
                            if (s != null) {
                                s.close();
                            }
                        } catch (IOException ex2) {
                            // ignore failure to close after failure to connect
                        }

                        // don't print an error message after the the
                        // first time or after the 8th time
                        if (retryCount == 8) {
                            logw("Couldn't find '" + SOCKET_NAME_CNE + "' socket after "
                                    + retryCount + " times, continuing to retry silently");
                        } else if (retryCount > 0 && retryCount < 8) {
                            dlogi("Couldn't find '" + SOCKET_NAME_CNE
                                        + "' socket; retrying after timeout");
                        }

                        try {
                            Thread.sleep(SOCKET_OPEN_RETRY_MILLIS);
                        } catch (InterruptedException er) {
                            dlogi("cnd socket open retry timer was interrupted");
                        }
                        retryCount++;
                        continue;
                    }
                    retryCount = 0;
                    mSocket = s;
                    dlogi( "Connected to '" + SOCKET_NAME_CNE + "' socket");
                    isCndUp = true;
                    // send the init request to lower layer cne
                    sendInitReq();

                    /* wait until CNE gets created */
                    synchronized (mService) {
                        /* send DefaultNwPref */
                        sendDefaultNwPref(mNetworkPreference);

                        /* start the default connection now */
                        try {
                            mDefaultConn.startConnection();
                        } catch (NullPointerException e) {
                            logw("Exception mDefaultConn is null" + e);
                        }
                    }
                    int length = 0;
                    try {
                        InputStream is = mSocket.getInputStream();

                        for (;;) {
                            Parcel p;

                            length = readCneMessage(is, buffer);

                            if (length < 0) {
                                // End-of-stream reached
                                break;
                            }
                            p = Parcel.obtain();
                            p.unmarshall(buffer, 0, length);
                            p.setDataPosition(0);
                            // dlogi( "Read packet: " + length + " bytes");
                            processResponse(p);
                            p.recycle();
                        }
                    } catch (java.io.IOException ex) {
                        dlogi( "'" + SOCKET_NAME_CNE + "' socket closed" + ex);
                    } catch (Throwable tr) {
                        logw("Uncaught exception read length=" + length + "Exception:"
                                + tr.toString());
                    }
                    dlogi("Disconnected from '" + SOCKET_NAME_CNE + "' socket");
                    /*
                     * end the default connection it will get started
                     * again when connection gets established
                     */
                    try {
                        mDefaultConn.endConnection();
                    } catch (NullPointerException e) {
                        logw("Exception mDefaultConn is null" + e);
                    }
                    isCndUp = false;
                    try {
                        mSocket.close();
                    } catch (IOException ex) {
                    }

                    mSocket = null;
                    CNERequest.resetSerial();

                    // Clear request list on close
                    synchronized (mRequestsList) {
                        for (int i = 0, sz = mRequestsList.size(); i < sz; i++) {
                            CNERequest rr = mRequestsList.get(i);
                            // rr.onError(RADIO_NOT_AVAILABLE);
                            rr.release();
                        }

                        mRequestsList.clear();
                    }
                }
            } catch (Throwable tr) {
                logw("Uncaught exception " + tr);
            }
        }
    }

    private ITelephony getPhone() {
      return ITelephony.Stub.asInterface(ServiceManager.getService("phone"));
    }

    // ***** Constructor
    public CNE(Context context, ConnectivityService conn) {
        mRequestMessagesPending = 0;
        mContext = context;
        mService = conn;
        mSenderThread = new HandlerThread("CNESender");
        mSenderThread.start();

        Looper looper = mSenderThread.getLooper();
        mSender = new CNESender(looper);

        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_BATTERY_CHANGED);
        filter.addAction(WifiManager.WIFI_STATE_CHANGED_ACTION);
        filter.addAction(WifiManager.RSSI_CHANGED_ACTION);
        filter.addAction(WifiManager.NETWORK_STATE_CHANGED_ACTION);
        filter.addAction(WifiManager.NO_MORE_WIFI_LOCKS);
        filter.addAction(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION);
        filter.addAction(TelephonyIntents.ACTION_ANY_DATA_CONNECTION_STATE_CHANGED);
        //QOS intents
        filter.addAction(TelephonyIntents.ACTION_QOS_STATE_IND);
        filter.addAction(Intent.ACTION_SHUTDOWN);

        context.registerReceiver(mIntentReceiver, filter);
        activeRegsList = new ConcurrentHashMap<Integer, RegInfo>();
        //Initialize a map to save all registration needs and capabilities.
        activeCapabilities = Collections.synchronizedMap(new HashMap<Integer, ExtraLinkCapabilities>());
        //Maintain separate container to track QoS specific registrations. Does
        //not distinguish between linksocket and linkdatagramsockets.
        mQosTrackers = Collections.synchronizedCollection(new ArrayList<QoSTracker>());

        /* create the default connection no metatdata right now */
        Map<LinkRequirement, String> linkReqs = null;
        mDefaultConn = new DefaultConnection(linkReqs);
        mWifiManager = (WifiManager) mContext.getSystemService(Context.WIFI_SERVICE);
        mTelephonyManager = (TelephonyManager) mContext.getSystemService(Context.TELEPHONY_SERVICE);

        // register for phone state notifications.
        mTelephonyManager.listen(mPhoneStateListener, PhoneStateListener.LISTEN_SIGNAL_STRENGTHS
                + PhoneStateListener.LISTEN_DATA_ACTIVITY);

        mReceiver = new CNEReceiver();
        mReceiverThread = new Thread(mReceiver, "CNEReceiver");
        mReceiverThread.start();
    }

    private void send(CNERequest rr) {
        Message msg = mSender.obtainMessage(EVENT_SEND, rr);
        // acquireWakeLock();
        msg.sendToTarget();
    }

    private void processResponse(Parcel p) {
        int type;
        type = p.readInt();
        if (type == RESPONSE_UNSOLICITED) {
            processUnsolicited(p);
        } else if (type == RESPONSE_SOLICITED) {
            processSolicited(p);
        }
    }

    private CNERequest findAndRemoveRequestFromList(int serial) {
        synchronized (mRequestsList) {
            for (int i = 0, s = mRequestsList.size(); i < s; i++) {
                CNERequest rr = mRequestsList.get(i);
                if (rr.mSerial == serial) {
                    mRequestsList.remove(i);
                    return rr;
                }
            }
        }
        return null;
    }

    private void processSolicited(Parcel p) {
        int serial, error;
        serial = p.readInt();
        error = p.readInt();
        CNERequest rr;
        rr = findAndRemoveRequestFromList(serial);
        if (rr == null) {
            logw("Unexpected solicited response! sn: " + serial + " error: " + error);
            return;
        }
        if (error != 0) {
            rr.onError(error);
            rr.release();
            return;
        }
        rr.release();
    }

    private void processUnsolicited(Parcel p) {
        dlogi( "processUnsolicited called");
        int response;

        response = p.readInt();
        switch (response) {
            case CNE_RESPONSE_REG_ROLE: {
                handleRegRoleRsp(p);
                break;
            }
            case CNE_RESPONSE_GET_BEST_NW: {
                handleGetCompatibleNwsRsp(p);
                break;
            }
            case CNE_RESPONSE_CONFIRM_NW: {
                handleConfNwRsp(p);
                break;
            }
            case CNE_RESPONSE_DEREG_ROLE: {
                handleDeRegRoleRsp(p);
                break;
            }
            case CNE_REQUEST_BRING_RAT_DOWN: {
                handleRatDownMsg(p);
                break;
            }
            case CNE_REQUEST_BRING_RAT_UP: {
                handleRatUpMsg(p);
                break;
            }
            case CNE_NOTIFY_MORE_PREFERED_RAT_AVAIL: {
                handleMorePrefNwAvailEvent(p);
                break;
            }
            case CNE_NOTIFY_RAT_LOST: {
                handleRatLostEvent(p);
                break;
            }
            case CNE_REQUEST_START_SCAN_WLAN: {
                handleStartScanWlanMsg(p);
                break;
            }
            case CNE_NOTIFY_INFLIGHT_STATUS: {
                handleNotifyInFlightStatusMsg(p);
                break;
            }
            case CNE_NOTIFY_FMC_STATUS: {
                handleFmcStatusMsg(p);
                break;
            }
            case CNE_NOTIFY_HOST_ROUTING_IP: {
                handleHostRoutingIpMsg(p);
                break;
            }
            default: {
                logw("UNKOWN Unsolicited Event " + response);
            }
        }
        return;
    }

    private int NetworkStateToInt(NetworkInfo.State state) {
        switch (state) {
            case CONNECTING: {
                return 0;
            }
            case CONNECTED: {
                return 1;
            }
            case SUSPENDED: {
                return 2;
            }
            case DISCONNECTING: {
                return 3;
            }
            case DISCONNECTED: {
                return 4;
            }
            case UNKNOWN: {
                return 5;
            }
        }
        return -1;
    }

    private PhoneStateListener mPhoneStateListener = new PhoneStateListener() {
        @Override
        public void onSignalStrengthsChanged(SignalStrength signalStrength) {
            mSignalStrength = signalStrength;
            if (mTelephonyManager != null) {
                ConnectivityManager cm = (ConnectivityManager) mContext
                        .getSystemService(Context.CONNECTIVITY_SERVICE);
                NetworkInfo networkInfo = cm.getNetworkInfo(ConnectivityManager.TYPE_MOBILE);
                NetworkInfo.State networkState = (networkInfo == null ? NetworkInfo.State.UNKNOWN
                        : networkInfo.getState());
                AddressInfo wwanV4AddrInfo = getWwanAddrInfo(Phone.APN_TYPE_DEFAULT,
                        IPVersion.INET);
                int roaming = (int) (mTelephonyManager.isNetworkRoaming() ? 1 : 0);
                int type = networkInfo.getSubtype();
                int rssi = getSignalStrength(type);
                Timestamp ts = new Timestamp(System.currentTimeMillis());
                String tsStr = ts.toString();
                String logmsg = "CNE onSignalStrengthsChanged -"
                            + " type:" + type
                            + " strength:" + rssi
                            + " ipV4Addr:" + wwanV4AddrInfo.ipAddr
                            + " roaming:" + roaming
                            + " iface:" + wwanV4AddrInfo.ifName
                            + " timeStamp:" + tsStr;
                dlogi(logmsg);
                updateWwanStatus(type, NetworkStateToInt(networkState), rssi, roaming,
                        wwanV4AddrInfo.ipAddr, wwanV4AddrInfo.ifName, tsStr);
            } else {
                logw("onSignalStrengthsChanged null TelephonyManager");
            }
        }

        @Override
        public void onDataActivity(int activity) {
            // update dormancy status if necessary
            if (activity == TelephonyManager.DATA_ACTIVITY_DORMANT) {
                // only update if needed, isWwanDormant should be true
                if (isWwanDormant == false) {
                    isWwanDormant = true;
                    updateWwanDormancyStatus();
                }
            } else {
                // only update if needed, isWwanDormant should be false
                if (isWwanDormant == true) {
                    isWwanDormant = false;
                    updateWwanDormancyStatus();
                }
            }
        }
    };

    private NetworkInfo.State convertToNetworkState(Phone.DataState dataState) {
        switch (dataState) {
            case DISCONNECTED:
                return NetworkInfo.State.DISCONNECTED;
            case CONNECTING:
                return NetworkInfo.State.CONNECTING;
            case CONNECTED:
                return NetworkInfo.State.CONNECTED;
            case SUSPENDED:
                return NetworkInfo.State.SUSPENDED;
            default:
                return NetworkInfo.State.UNKNOWN;
        }
    }

    private int getSignalStrength(int networkType) {
        if (mSignalStrength == null) {
            logw("getSignalStrength mSignalStrength in null");
            return -1;
        }
        dlogi( "getSignalStrength networkType= " + networkType);
        switch (networkType) {
            case TelephonyManager.NETWORK_TYPE_GPRS:
            case TelephonyManager.NETWORK_TYPE_EDGE:
            case TelephonyManager.NETWORK_TYPE_UMTS:
            case TelephonyManager.NETWORK_TYPE_HSDPA:
            case TelephonyManager.NETWORK_TYPE_HSUPA:
            case TelephonyManager.NETWORK_TYPE_HSPA:
                // dBm = -113 + 2*asu
                return (-113 + 2 * (mSignalStrength.getGsmSignalStrength()));
            case TelephonyManager.NETWORK_TYPE_1xRTT:
            case TelephonyManager.NETWORK_TYPE_CDMA:
                return (mSignalStrength.getCdmaDbm());
                // return(mSignalStrength.getCdmaEcio());
            case TelephonyManager.NETWORK_TYPE_EVDO_0:
            case TelephonyManager.NETWORK_TYPE_EVDO_A:
                return (mSignalStrength.getEvdoDbm());
                // return(mSignalStrength.getEvdoSnr());
        }
        return -1;
    }

    /* API functions */
    public boolean updateBatteryStatus(int status, int pluginType, int level) {

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_UPDATE_BATTERY_INFO);
        if (rr == null) {
            logw("updateBatteryStatus: rr=NULL");
            return false;
        }
        dlogi("UpdateBatteryStatus status=" + status + " pluginType=" + pluginType
                    + " level=" + level);
        rr.mp.writeInt(3); // num of ints that are getting written
        rr.mp.writeInt(status);
        rr.mp.writeInt(pluginType);
        rr.mp.writeInt(level);
        send(rr);
        return true;
    }

    public boolean updateWlanStatus(int type, int state, int rssi, String ssid, String ipAddr,
            String iface, String timeStamp) {

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_UPDATE_WLAN_INFO);
        if (rr == null) {
            logw("updateWlanStatus: rr=NULL - no updated");
            return false;
        }

        dlogi("UpdateWlanStatus type=" + type
                    + " state=" + state
                    + " rssi=" + rssi
                    + " ssid=" + ssid
                    + " ipAddr=" + ipAddr
                    + " iface=" + iface
                    + " timeStamp=" + timeStamp);

        rr.mp.writeInt(type);
        rr.mp.writeInt(state);
        rr.mp.writeInt(rssi);
        rr.mp.writeString(ssid);
        rr.mp.writeString(ipAddr);
        rr.mp.writeString(iface);
        rr.mp.writeString(timeStamp);
        send(rr);
        return true;
    }

    public boolean updateWlanScanResults(List<ScanResult> scanResults) {

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_UPDATE_WLAN_SCAN_RESULTS);
        if (rr == null) {
            logw("updateWlanScanResults: rr=NULL");
            return false;
        }

        if (scanResults != null) {
            dlogi("CNE- updateWlanScanResults: scanResults size = " + scanResults.size());
            rr.mp.writeInt(scanResults.size()); // write number of elements
            for (int i = scanResults.size() - 1; i >= 0; i--) {
                ScanResult scanResult = scanResults.get(i);
                rr.mp.writeInt(scanResult.level);
                rr.mp.writeInt(scanResult.frequency);
                rr.mp.writeString(scanResult.SSID);
                rr.mp.writeString(scanResult.BSSID);
                rr.mp.writeString(scanResult.capabilities);

                // Need to check for empty string before writeString?
                // if (TextUtils.isEmpty(scanResult.capabilities))
            }
        }
        send(rr);
        return true;
    }

    /** {@hide} */
    public boolean updateWwanStatus(int type, int state, int rssi, int roaming, String ipV4Addr,
            String iface, String timeStamp) {

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_UPDATE_WWAN_INFO);
        if (rr == null) {
            logw("updateWwanStatus: rr=NULL - no updated");
            return false;
        }
        dlogi("UpdateWwanStatus type=" + type
                    + " state=" + state
                    + " rssi=" + rssi
                    + " roaming=" + roaming
                    + " ipV4Addr=" + ipV4Addr
                    + " iface=" + iface
                    + " timeStamp=" + timeStamp);
        rr.mp.writeInt(type);
        rr.mp.writeInt(state);
        rr.mp.writeInt(rssi);
        rr.mp.writeInt(roaming);
        rr.mp.writeString(ipV4Addr);
        rr.mp.writeString(iface);
        rr.mp.writeString(timeStamp);
        send(rr);
        return true;
    }

    /** {@hide} */
    private boolean updateWwanDormancyStatus() {
        // send isWwanDormant to cnd

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_UPDATE_WWAN_DORMANCY_INFO);
        if (rr == null) {
            logw("updateWwanDormancyStatus: rr=NULL - not updated");
            return false;
        }
        dlogi("updateWwanDormancyStatus dormancy=" + isWwanDormant);

        rr.mp.writeInt(isWwanDormant ? 1 : 0);
        send(rr);
        return true;
    }

    /** {@hide} */
    private boolean updateDefaultNetwork() {
        // notify cnd which network is the default

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_UPDATE_DEFAULT_NETWORK_INFO);
        if (rr == null) {
            logw("updateDefaultNetwork: rr=NULL - not updated");
            return false;
        }
        dlogi("updateDefaultNetwork default = " + mDefaultNetwork);

        rr.mp.writeInt(mDefaultNetwork);
        send(rr);
        return true;
    }

    /** {@hide} */
    public boolean notifyRatConnectStatus(int type, int status, String ipV4Addr) {

        CNERequest rr = CNERequest.obtain(CNE_NOTIFY_RAT_CONNECT_STATUS);

        if (rr == null) {
            logw("notifyRatConnectStatus: rr=NULL");
            return false;
        }
        dlogi("notifyRatConnectStatus ratType=" + type
                    + " status=" + status
                    + " ipV4Addr=" + ipV4Addr);
        rr.mp.writeInt(type);
        rr.mp.writeInt(status);
        rr.mp.writeString(ipV4Addr);
        send(rr);
        return true;
    }

    /** {@hide} */
    public void sendDefaultNwPref2Cne(int preference) {
        mNetworkPreference = preference;
    }

    /** {@hide} */
    public void sendDefaultNwPref(int preference) {
        CNERequest rr = CNERequest.obtain(CNE_NOTIFY_DEFAULT_NW_PREF);
        if (rr != null) {
            rr.mp.writeInt(1); // num of ints that are getting written
            int rat = CNE_RAT_WLAN;
            if (preference == ConnectivityManager.TYPE_MOBILE) {
                rat = CNE_RAT_WWAN;
            }
            rr.mp.writeInt(rat);
            send(rr);
        } else {
            logw("sendDefaultNwPref2Cne: rr=NULL");
        }
        return;
    }

    private boolean sendRegRoleReq(int role, int roleRegId, int fwLinkBw, int revLinkBw) {
        dlogw( "sendRegRoleReq(role=" + role + ", roleRegId=" + roleRegId +
                ", fwLinkBw=" + fwLinkBw + ", revLinkBwBw=" + revLinkBw + ") EX");

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_REG_ROLE);
        if (rr == null) {
            logw("sendRegRoleReq: rr=NULL");
            return false;
        }

        rr.mp.writeInt(4); // num of ints that are getting written
        rr.mp.writeInt(role);
        rr.mp.writeInt(roleRegId);
        rr.mp.writeInt(fwLinkBw);
        rr.mp.writeInt(revLinkBw);
        send(rr);

        return true;
    }

    private boolean sendInitReq() {
        CNERequest rr = CNERequest.obtain(CNE_REQUEST_INIT);
        if (rr == null) {
            logw("sendinitReq: rr=NULL");
            return false;
        }
        send(rr);
        return true;
    }

    private boolean sendGetCompatibleNwsReq(int roleRegId) {
        CNERequest rr = CNERequest.obtain(CNE_REQUEST_GET_COMPATIBLE_NWS);
        if (rr == null) {
            logw("sendGetCompatibleNwsReq: rr=NULL");
            return false;
        }
        rr.mp.writeInt(1); // num of ints that are getting written
        rr.mp.writeInt(roleRegId);
        dlogi( "Sending GetCompatibleNwsReq roleRegId=" + roleRegId);
        send(rr);
        return true;
    }

    // change name and params one is added
    private boolean sendConfirmNwReq(int roleRegId, int ifaceId, int confirmation,
            int notifyIfBetterNwAvail, int newIfaceId) {
        CNERequest rr = CNERequest.obtain(CNE_REQUEST_CONF_NW);
        if (rr == null) {
            logw("sendConfirmNwReq: rr=NULL");
            return false;
        }
        rr.mp.writeInt(5); // num of ints that are getting written
        rr.mp.writeInt(roleRegId);
        rr.mp.writeInt(ifaceId);
        rr.mp.writeInt(confirmation);
        rr.mp.writeInt(notifyIfBetterNwAvail);
        rr.mp.writeInt(newIfaceId);

        send(rr);
        return true;

    }

    private boolean sendDeregRoleReq(int roleRegId) {

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_DEREG_ROLE);
        if (rr == null) {
            logw("sendDeregRoleReq: rr=NULL");
            return false;
        }
        rr.mp.writeInt(1); // num of ints that are getting written
        rr.mp.writeInt(roleRegId);
        dlogi( "sendDeregRoleReq:");

        send(rr);
        return true;
    }

    private int getRegId(int pid, int role) {
        int regId = CNE_REGID_INVALID;
        Iterator<Entry<Integer, RegInfo>> activeRegsIter = activeRegsList.entrySet().iterator();
        while (activeRegsIter.hasNext()) {
            RegInfo regInfo = activeRegsIter.next().getValue();
            if (regInfo.role == role && regInfo.pid == pid) {
                regId = regInfo.regId;
                break;
            }
        }
        return regId;
    }

    private void handleRegRoleRsp(Parcel p) {
        int numInts = p.readInt();
        int roleRegId = p.readInt();
        int evtStatus = p.readInt();
        dlogi("handleRegRoleRsp called with numInts = " + numInts + " RoleRegId = "
                    + roleRegId + " evtStatus = " + evtStatus);
        /* does this role already exists? */
        RegInfo regInfo = activeRegsList.get(roleRegId);
        if (regInfo != null) {
            if (evtStatus == STATUS_SUCCESS) {
                /*
                 * register role was success so get the compatible
                 * networks
                 */
                sendGetCompatibleNwsReq(roleRegId);
                return;
            } else {
                IConSvcEventListener listener = regInfo.cbInfo.listener;
                if (listener != null) {
                    try {
                        listener.onGetLinkFailure(LinkNotifier.FAILURE_GENERAL);
                    } catch (RemoteException e) {
                        logw("handleRegRoleRsp listener is null");
                    }
                }
            }
        } else {
            dlogw( "handleRegRoleRsp regId=" + roleRegId + " does not exists");
            return;
        }
    }

    private void handleGetCompatibleNwsRsp(Parcel p) {
        int roleRegId = p.readInt();
        int evtStatus = p.readInt();
        dlogi("handleGetCompatibleNwsRsp called for roleRegId = " + roleRegId
                    + " evtStatus = " + evtStatus);
        /* does this role already exists? */
        RegInfo regInfo = activeRegsList.get(roleRegId);
        if (regInfo != null) {
            //now handle CnE/LinkSocket notifications.
            if (evtStatus == STATUS_SUCCESS) {
                /* save the returned info */
                regInfo.activeRat = p.readInt();
                /* save the oldCompatibleRatsList */
                ArrayList<RatInfo> prevCompatibleRatsList = regInfo.compatibleRatsList;
                ArrayList<RatInfo> newCompatibleRatsList = new ArrayList<RatInfo>();
                for (int i = 0; i < CNE_RAT_MAX; i++) {
                    int nextRat = p.readInt();
                    if (nextRat != CNE_RAT_INVALID && nextRat != CNE_RAT_NONE) {
                        RatInfo ratInfo = new RatInfo();
                        ratInfo.rat = nextRat;
                        if (nextRat == regInfo.activeRat) {
                            ratInfo.status = RatTriedStatus.RAT_STATUS_TRIED;
                        } else {
                            /* try to preserve the old rat status */
                            int index = prevCompatibleRatsList.indexOf(ratInfo);
                            if (index != -1) {
                                RatInfo oldRatInfo = prevCompatibleRatsList.get(index);
                                ratInfo.status = oldRatInfo.status;
                            } else {
                                ratInfo.status = RatTriedStatus.RAT_STATUS_NOT_TRIED;
                            }
                        }
                        newCompatibleRatsList.add(ratInfo);
                    }
                }
                String ipAddr = new String(p.readString());
                int flBwEst = p.readInt();
                int rlBwEst = p.readInt();
                dlogi("ipV4Addr = " + ipAddr + " flBwEst = " + flBwEst
                            + " rlBwEst = " + rlBwEst);
                prevCompatibleRatsList.clear();
                regInfo.compatibleRatsList = newCompatibleRatsList;
                regInfo.dump();
                /* call the call backs */
                /*
                 * over here the implementation is to use what ever
                 * the connectivityEngine has passed as the best rat,
                 * if AndroidConSvc is not satisifed or wants to do
                 * give a different nw then, it can confirm the
                 * bestrat given by Cne negative, and do not call the
                 * onLinkAvail callback yet and request a new one
                 */
                IConSvcEventListener listener = regInfo.cbInfo.listener;
                if (listener != null) {
                    try {
                        LinkInfo linkInfo = new LinkInfo(ipAddr, flBwEst, rlBwEst,
                                regInfo.activeRat);
                        if (DBG) {
                            Log.d(LOCAL_TAG,"DEBUG:: created LinkInfo with ip "
                                + linkInfo.getIpAddr().getHostAddress());
                        }
                        regInfo.notificationsSent = regInfo.notificationsSent
                                | CNE_MASK_ON_LINK_AVAIL_SENT;
                        listener.onLinkAvail(linkInfo);
                    } catch (RemoteException e) {
                        logw("handleGetCompNwsRsp listener is null");
                    }
                }
                return;
            } else {
                IConSvcEventListener listener = regInfo.cbInfo.listener;
                if (listener != null) {
                    try {
                        listener.onGetLinkFailure(LinkNotifier.FAILURE_NO_LINKS);
                    } catch (RemoteException e) {
                        logw("handleGetCompNwsRsp listener is null");
                    }
                }
            }
        } else {
            dlogw( "handleGetCompatibleNwsRsp role does not exists");
            return;
        }

    }

    private void handleConfNwRsp(Parcel p) {
        int numInts = p.readInt();
        int roleRegId = p.readInt();
        int evtStatus = p.readInt();
        dlogi("handleConfNwRsp called with numInts = " + numInts
                    + " regRoleId = " + roleRegId + " evtStatus = " + evtStatus);
    }

    private void handleDeRegRoleRsp(Parcel p) {
        int numInts = p.readInt();
        int roleRegId = p.readInt();
        int evtStatus = p.readInt();
        dlogi("handleDeRegRoleRsp called with numInts = " + numInts
                    + " roleRegId = " + roleRegId + " evtStatus = " + evtStatus);
        /* clean up */
        /* does this role already exists? */
        RegInfo regInfo = activeRegsList.get(roleRegId);
        if (regInfo != null) {
            regInfo.compatibleRatsList.clear();
            activeRegsList.remove(roleRegId);
        } else {
            logw("handleDeRegRoleRsp role does not exists");
            return;
        }
    }

    private void handleMorePrefNwAvailEvent(Parcel p) {
        int roleRegId = p.readInt();
        int betterRat = p.readInt();
        String ipAddr = new String(p.readString());
        int flBwEst = p.readInt();
        int rlBwEst = p.readInt();
        dlogi("handleMorePrefNwAvailEvent called for roleRegId = " + roleRegId
                    + " betterRat = " + betterRat
                    + " ipAddr = " + ipAddr
                    + " flBwEst = " + flBwEst
                    + " rlBwEst = " + rlBwEst);
        /* does this role already exists? */
        RegInfo regInfo = activeRegsList.get(roleRegId);
        if (regInfo != null) {
            /* save the betterRat info */
            regInfo.betterRat = betterRat;
            /* call the call backs */
            IConSvcEventListener listener = regInfo.cbInfo.listener;
            if (listener != null) {
                try {
                    LinkInfo linkInfo = new LinkInfo(ipAddr, flBwEst, rlBwEst, betterRat);
                    regInfo.notificationsSent
                            = regInfo.notificationsSent | CNE_MASK_ON_BETTER_LINK_AVAIL_SENT;
                    listener.onBetterLinkAvail(linkInfo);
                } catch (RemoteException e) {
                    logw("handleMorePrefNwAvailEvt listener is null");
                }
            }
        } else {
            logw("handleMorePrefNwAvailEvent role does not exists");
            return;
        }
    }

    private void handleRatLostEvent(Parcel p) {
        int numInts = p.readInt();
        int roleRegId = p.readInt();
        int rat = p.readInt();
        dlogi("handleRatLostEvent called with numInts = " + numInts
                    + " roleRegId = " + roleRegId + " rat = " + rat);
        /* does this role already exists? */
        RegInfo regInfo = activeRegsList.get(roleRegId);
        if (regInfo != null) {
            if (regInfo.activeRat == rat) {
                IConSvcEventListener listener = regInfo.cbInfo.listener;
                if (listener != null) {
                    try {
                        LinkInfo linkInfo = new LinkInfo(null, LinkInfo.INF_UNSPECIFIED,
                                LinkInfo.INF_UNSPECIFIED, rat);
                        listener.onLinkLost(linkInfo);
                    } catch (RemoteException e) {
                        logw("handleRatLostEvent listener is null");
                    }
                }
            } else {
                dlogw("Rat lost is not for the active rat=" + regInfo.activeRat);
            }
        } else {
            dlogw( "handleRatLostEvent role does not exists");
            return;
        }
    }

    private void handleRatDownMsg(Parcel p) {
        int ratType = p.readInt();
        dlogi( "handleRatDownMsg called ratType = " + ratType);
        if (mService != null) {
            mService.bringDownRat(ratType);
        }
        return;
    }

    private void handleRatUpMsg(Parcel p) {
        int ratType = p.readInt();
        dlogi( "handleRatUpMsg called ratType = " + ratType);
        switch (ratType) {
            case CNE_RAT_WLAN:
                handleWlanBringUp();
                break;
            case CNE_RAT_WWAN:
                handleWwanBringUp();
                break;
            default:
                dlogw( "UnHandled Rat Type: " + ratType);
        }
        return;
    }

    private void handleWlanBringUp() {
        try {
            ConnectivityManager cm = (ConnectivityManager) mContext
                    .getSystemService(Context.CONNECTIVITY_SERVICE);
            NetworkInfo networkInfo = cm.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
            NetworkInfo.State networkState = networkInfo.getState();
            if (networkState == NetworkInfo.State.CONNECTED) {
                AddressInfo wlanV4AddrInfo = getWlanAddrInfo(IPVersion.INET);
                notifyRatConnectStatus(CNE_RAT_WLAN, NetworkStateToInt(networkState),
                        wlanV4AddrInfo.ipAddr);

            } else {
                mService.bringUpRat(CNE_RAT_WLAN);
            }
        } catch (NullPointerException e) {
            logw("handleWlanBringUp " + e);
            e.printStackTrace();
        }
    }

    private void handleWwanBringUp() {
        try {
            ConnectivityManager cm = (ConnectivityManager) mContext
                    .getSystemService(Context.CONNECTIVITY_SERVICE);
            NetworkInfo networkInfo = cm.getNetworkInfo(ConnectivityManager.TYPE_MOBILE);
            NetworkInfo.State networkState = networkInfo.getState();
            if (networkState == NetworkInfo.State.CONNECTED) {
                AddressInfo wwanV4AddrInfo = getWwanAddrInfo(Phone.APN_TYPE_DEFAULT,
                        IPVersion.INET);
                notifyRatConnectStatus(CNE_RAT_WWAN, NetworkStateToInt(networkState),
                        wwanV4AddrInfo.ipAddr);
            } else {
                // tell telephony service to bypass all network
                // checking
                if (getFmcObj() != null) {
                    if (getFmcObj().dsAvail == true) {
                        try {
                            // bypass connectivity and subscription
                            // checking, and bring up data call
                            ITelephony mPhone = getPhone();
                            if (mPhone == null)
                                logw("handle wwan bring up found null telephony object");
                            else
                              mPhone.setDataReadinessChecks(false, false, true);
                        } catch (RemoteException e) {
                            logw("remoteException while calling setDataReadinessChecks");
                        }
                    }
                }
                mService.bringUpRat(CNE_RAT_WWAN);
            }
        } catch (NullPointerException e) {
            logw("handleWwanBringUp " + e);
            e.printStackTrace();
        }
    }

    private void handleStartScanWlanMsg(Parcel p) {
        dlogi( "handleStartScanWlanMsg called");
        if (!mWifiManager.isWifiEnabled()) return;
        mWifiManager.startScanActive();
    }

    private void handleNotifyInFlightStatusMsg(Parcel p) {
        dlogi( "handleNotifyInFlightStatusMsg called");
        boolean on;
        p.readInt(); // numInts - unused
        int status = p.readInt();
        if (status == STATUS_INFLIGHT) {
            on = true;
        } else {
            on = false;
        }
        Settings.System.putInt(mContext.getContentResolver(), Settings.System.AIRPLANE_MODE_ON,
                on ? 1 : 0);
        Intent intent = new Intent(Intent.ACTION_AIRPLANE_MODE_CHANGED);
        intent.putExtra("state", on);
        mContext.sendBroadcast(intent);

    }

    private void handleFmcStatusMsg(Parcel p) {
        FmcRegInfo rInfo = getFmcObj();
        p.readInt(); // numInts - unused
        int status = p.readInt();
        if (rInfo != null) {
            dlogi("handleFmcStatusMsg fmc_status="
                        + FmcNotifier.FMC_STATUS_STR[status]);
            if (status == FmcNotifier.FMC_STATUS_ENABLED) {
                AddressInfo wwanV4AddrInfo = getWwanAddrInfo(Phone.APN_TYPE_DEFAULT,
                        IPVersion.INET);
                dlogi("handleFmcStatusMsg fmc_status=" + FmcNotifier.FMC_STATUS_STR[status] +
                    ",hostIp=" + hostRoutingIpAddr + ",interface=" + activeWlanIfName);
                IBinder b = ServiceManager.getService(Context.NETWORKMANAGEMENT_SERVICE);
                INetworkManagementService nms = INetworkManagementService.Stub.asInterface(b);
                try {
                    nms.addDstRoute(activeWlanIfName, hostRoutingIpAddr, activeWlanGatewayAddr);
                    if (mService != null) {
                        mService.setDefaultRoute(ConnectivityManager.TYPE_MOBILE, IPVersion.INET);
                    }
                } catch (Exception e) {
                    logw("Error adding host routing");
                }
                rInfo.enabled = true;
            } else if (status == FmcNotifier.FMC_STATUS_REGISTRATION_SUCCESS) {
                dlogi("handleFmcStatusMsg REGISTRATION_SUCCESS - fmcEnabled="
                      + rInfo.enabled );
                if (rInfo.enabled == true) {
                    IBinder b = ServiceManager.getService(Context.NETWORKMANAGEMENT_SERVICE);
                    INetworkManagementService nms = INetworkManagementService.Stub.asInterface(b);
                    try {
                        nms.replaceV4DefaultRoute(activeWlanIfName, activeWlanGatewayAddr);
                        nms.delDstRoute(hostRoutingIpAddr);
                    } catch (Exception e) {
                        logw("Error adding host routing");
                    }
                    rInfo.enabled = false;
                }
            } else {
                if ((status == FmcNotifier.FMC_STATUS_CLOSED)
                        || (status == FmcNotifier.FMC_STATUS_FAILURE)
                        || (status == FmcNotifier.FMC_STATUS_DS_NOT_AVAIL)
                        || (status == FmcNotifier.FMC_STATUS_RETRIED)) {
                    rInfo.enabled = false;
                    rInfo.dsAvail = false;
                }
            }
            if (status == FmcNotifier.FMC_STATUS_CLOSED) {
                if (rInfo.actionStop) {
                    onFmcStatus(status);
                    setFmcObj(null);
                } else {
                    onFmcStatus(FmcNotifier.FMC_STATUS_FAILURE);
                }
            } else {
                onFmcStatus(status);
            }
        }
    }

    private void handleHostRoutingIpMsg(Parcel p) {
        String ipAddr = new String(p.readString());
        dlogi( "handleHostRoutingIpMsg ip=" + ipAddr);
        hostRoutingIpAddr = ipAddr;
        if (getFmcObj() != null) {
            getFmcObj().dsAvail = true;
        }
        return;
    }

    /** {@hide} */
    public void setDefaultConnectionNwPref(int preference) {
        dlogi( "setDefaultConnectionNwPref called with pref = " + preference);
        /* does this role already exists? */
        RegInfo regInfo = activeRegsList.get(CNE_DEFAULT_CON_REGID);
        if (regInfo != null) {
            /*
             * preference can be either wlan or wwan send the
             * confirmNw with the next best wwan or wlan rat in the
             * compatiblerats list for this role
             */
            if (preference != regInfo.activeRat) {
                sendConfirmNwReq(regInfo.regId, regInfo.activeRat, 0, // not satisfied with this
                        regInfo.cbInfo.isNotifBetterRat ? 1 : 0, preference);
            }
        } else {
            logw("Default Registration does not exists");
        }
        return;
    }

    /*
     * the dlogic of the function will decide what is the next rat to
     * try. here we follow the order provided by the connectivity
     * engine. AndroidConSvc can change this dlogic what ever it wants
     */
    private int getNextRatToTry(ArrayList<RatInfo> ratList) {
        int candidateRat = CNE_RAT_INVALID;
        /* index 0 is the active rat */
        for (int index = 1; index < ratList.size(); index++) {
            RatInfo ratInfo = (RatInfo) ratList.get(index);
            if (ratInfo.status == RatTriedStatus.RAT_STATUS_NOT_TRIED) {
                candidateRat = ratInfo.rat;
                ratInfo.status = RatTriedStatus.RAT_STATUS_TRIED;
                break;
            }
        }
        dlogi( "getNextRatToTry called NextRatToTry= " + candidateRat);
        return candidateRat;
    }

    /**
     * Convert a data rate string such as "2Mbps" to an integer
     * representing the data rate in bps. If no data rate is given
     * then bps will be assumed.
     * @param rate a data rate string, such as "100kbps" or "2Mbps"
     * @return the data rate in bps as an integer
     */
    private static int parseBwString(String rate) {
        if (rate == null) return 0;

        int rateMultiple = 1; // defaults to bps
        if (rate.toLowerCase().endsWith("kbps") || rate.endsWith("kbit/s") || rate.endsWith("kb/s")) {
            rateMultiple = 1000; // 1,000 bps per 1 Mbps
        } else if (rate.toLowerCase().endsWith("mbps") || rate.endsWith("Mbit/s") || rate.endsWith("Mb/s")) {
            rateMultiple = 1000000; // 1,000,000 bps per 1 Mbps
        } else if (rate.toLowerCase().endsWith("gbps") || rate.endsWith("Gbit/s") || rate.endsWith("Gb/s")) {
            rateMultiple = 1000000000; // 1,000,000,000 bps per 1 Gbps
        }

        // find first non-numeric character, and trim
        int trimPosition = rate.length();
        for (int i = 0; i < rate.length(); i++) {
            if (rate.charAt(i) <= '0' || rate.charAt(i) >= '9') {
                trimPosition = i;
                break;
            }
        }
        rate = rate.substring(0, trimPosition);
        if (rate.length() == 0) rate = "0";

        return (Integer.parseInt(rate) * rateMultiple);
    }

    // generate SocketId's
    // TODO find a better way to assign unique ids per socket/registration since
    // the regid may overlap with default connection when num registrations
    // exceed max integer value.
    synchronized private static int getNextSocketId() {
        if (mRoleRegId == Integer.MAX_VALUE) mRoleRegId = 0;
        // Using the LP roleregid as the socket id since both LP and LS APIs aren't
        // supposed to coexist concurrently.
        return mRoleRegId;
    }

    /** {@hide} */
    public boolean getLink_LP(int role, Map<LinkRequirement, String> linkReqs, int pid,
            IBinder listener) {
        dlogi( "getLink called for role = " + role);
        /* did the app(pid) register for this role already? */
        if (getRegId(pid, role) != CNE_REGID_INVALID) {
            logw("Multpl same role reg's not allowed by single app");
            return false;
        } else {
            /* new role registration for this app (pid) */
            RegInfo regInfo = new RegInfo(listener);
            regInfo.role = role;
            regInfo.pid = pid;
            IConSvcEventListener evtListener =
                    (IConSvcEventListener) IConSvcEventListener.Stub.asInterface(listener);
            regInfo.cbInfo.listener = evtListener;
            regInfo.cbInfo.isNotifBetterRat = false;
            dlogi( "activeRegsList.size before = " + activeRegsList.size());
            activeRegsList.put(regInfo.regId, regInfo);
            dlogi( "activeRolesList.size after = " + activeRegsList.size());

            int fwLinkBwReq = 0;
            int revLinkBwReq = 0;
            if (linkReqs != null) {
                for (Map.Entry<LinkRequirement, String> e : linkReqs.entrySet()) {
                    LinkRequirement key = e.getKey();
                    String value = e.getValue();
                    if (value == null) continue;

                    switch (key) {
                        case FW_LINK_BW:
                            fwLinkBwReq = parseBwString(value);
                            if (fwLinkBwReq == -1) {
                                logw("Invalid bandwidth req. value: " + value);
                                return false;
                            }
                            break;
                        case REV_LINK_BW:
                            revLinkBwReq = parseBwString(value);
                            if (revLinkBwReq == -1) {
                                logw("Invalid bandwidth req. value: " + value);
                                return false;
                            }
                            break;
                    }
                }
            }
            sendRegRoleReq(role, regInfo.regId, fwLinkBwReq, revLinkBwReq);
            return true;
        }
    }

    /** {@hide} */
    public boolean reportLinkSatisfaction_LP(int role, int pid, LinkInfo info, boolean isSatisfied,
            boolean isNotifyBetterLink) {
        dlogi("reporting connection satisfaction role = " + role + "isSatisfied = "
                    + isSatisfied + "isNotifyBetterLink" + isNotifyBetterLink);
        int regId = getRegId(pid, role);
        RegInfo regInfo = activeRegsList.get(regId);
        if (regInfo != null) {
            if ((regInfo.notificationsSent
                    & CNE_MASK_ON_LINK_AVAIL_SENT) == CNE_MASK_ON_LINK_AVAIL_SENT) {
                regInfo.cbInfo.isNotifBetterRat = isNotifyBetterLink;
                int ratToTry = CNE_RAT_NONE;
                if (!isSatisfied) {
                    ratToTry = getNextRatToTry(regInfo.compatibleRatsList);
                }
                sendConfirmNwReq(regInfo.regId, info == null ? regInfo.activeRat : info.getNwId(),
                        isSatisfied ? 1 : 0, isNotifyBetterLink ? 1 : 0, ratToTry);
                /*
                 * if the app was not satisfied and ratToTry was
                 * CNE_RAT_INVALID then call back the app saying
                 * connection not possible
                 */
                if (!isSatisfied && ratToTry == CNE_RAT_INVALID) {
                    // call the callback
                    try {
                        IConSvcEventListener evtListener = regInfo.cbInfo.listener;
                        if (evtListener != null) {
                            evtListener.onGetLinkFailure(LinkNotifier.FAILURE_NO_LINKS);
                        }
                        return true;
                    } catch (RemoteException e) {
                        logw("remoteException while calling onConnectionComplete");
                        return false;
                    }
                }
                return true;
            } else {
                logw("OnLinkAvail notification was not sent yet.");
                return false;
            }
        } else {
            logw("App did not register for this role");
            return false;
        }
    }

    /** {@hide} */
    public boolean releaseLink_LP(int role, int pid) {
        int regId;
        if (role < 0) { // linksocket releasing link
            dlogi( "releasing link for socket = " + pid);
            regId = pid;
        } else { // linkprovider releasing link
            dlogi( "releasing link for role = " + role);
            regId = getRegId(pid, role);
        }

        //activeRegsList is the superset, check if QoS regestration is present inside
        //of this superset and remove it from that subset as well.
        RegInfo regInfo = activeRegsList.get(regId);
        if (regInfo != null) {
            sendDeregRoleReq(regInfo.regId);
            /* clean up */
            try {
                regInfo.mBinder.unlinkToDeath(regInfo, 0);
            } catch (NoSuchElementException exp) {
                logw("bindrLinkToDeatch was not registered");
            }
            regInfo.compatibleRatsList.clear();
            activeRegsList.remove(regId);
            // clean up QoS handler
            QoSTracker qt = findQosBySocketId(regId);
            if (qt != null) {
                qt.stopQosTransaction(); //release associated QoS
                synchronized(mQosTrackers) {
                    try {
                        mQosTrackers.remove(qt);
                        dlogd("stopped tracking qos for socket: " + regId);
                    } catch (Exception e) {
                        dlogd("release link, error while remove qos tracker: " + e);
                    }
                }
            }
            // clean up needs for this registration.
            synchronized(activeCapabilities) {
                if (activeCapabilities.containsKey(regId)) {
                    activeCapabilities.remove(regId);
                } else {
                    dlogi("registration was not a link socket");
                }
            }
            return true;
        } else {
            logw("App did not register for this role");
            return false;
        }
    }

    /** {@hide} */
    public boolean switchLink_LP(int role, int pid, LinkInfo info, boolean isNotifyBetterLink) {
        dlogi( "switch link for role = " + role);
        int regId = getRegId(pid, role);
        RegInfo regInfo = activeRegsList.get(regId);
        if (regInfo != null) {
            if ((regInfo.notificationsSent & CNE_MASK_ON_BETTER_LINK_AVAIL_SENT)
                    == CNE_MASK_ON_BETTER_LINK_AVAIL_SENT) {
                regInfo.activeRat = info == null ? regInfo.betterRat : info.getNwId();
                sendConfirmNwReq(regInfo.regId, regInfo.activeRat, CNE_LINK_SATISFIED,
                        isNotifyBetterLink ? 1 : 0, CNE_RAT_NONE);
                return true;
            } else {
                logw("OnBetterLinkAvail notification was not sent yet.");
                return false;
            }
        } else {
            logw("App did not register for this role");
            return false;
        }
    }

    /** {@hide} */
    public boolean rejectSwitch_LP(int role, int pid, LinkInfo info, boolean isNotifyBetterLink) {
        dlogi( "rejectSwitch for role = " + role);
        int regId = getRegId(pid, role);
        RegInfo regInfo = activeRegsList.get(regId);
        if (regInfo != null) {
            if ((regInfo.notificationsSent & CNE_MASK_ON_BETTER_LINK_AVAIL_SENT)
                    == CNE_MASK_ON_BETTER_LINK_AVAIL_SENT) {
                sendConfirmNwReq(regInfo.regId, info == null ? regInfo.betterRat : info.getNwId(),
                        CNE_LINK_NOT_SATISFIED, isNotifyBetterLink ? 1 : 0, CNE_RAT_NONE);
                return true;
            } else {
                logw("OnBetterLinkAvail notification was not sent yet.");
                return false;
            }
        } else {
            logw("App did not register for this role");
            return false;
        }
    }

    /** {@hide} */
    public static boolean configureSsid(String newStr) {
        try {
            boolean strMatched = false;
            File file = new File("/data/ssidconfig.txt");

            // Read file to buffer
            BufferedReader reader = new BufferedReader(new FileReader(file));
            String line = "";
            String oldtext = "";
            String oldStr = "";
            // Get token of new string
            StringTokenizer newst = new StringTokenizer(newStr, ":");
            String newToken = newst.nextToken();
            dlogi( "configureSsid: newToken: " + newToken);
            while ((line = reader.readLine()) != null) {
                oldtext += line + "\r\n";
                StringTokenizer oldst = new StringTokenizer(line, ":");
                while (oldst.hasMoreTokens()) {
                    String oldToken = oldst.nextToken();
                    dlogi( "configureSsid: oldToken: " + oldToken);
                    if (newToken.equals(oldToken)) {
                        dlogi( "configSsid entry matched");
                        // Save string to be replaced
                        oldStr = line;
                        strMatched = true;
                    }
                }
            }
            if (!strMatched) {
                dlogi( "configSsid entry not matched");
                return false;
            }
            // To replace new text in file
            String newtext = oldtext.replaceAll(oldStr, newStr);
            reader.close();
            FileWriter writer = new FileWriter("/data/ssidconfig.txt");
            writer.write(newtext);
            writer.close();
            return true;
        } catch (IOException ioe) {
            ioe.printStackTrace();
        }
        return true;
    }

    /** {@hide} */
    public void notifyDefaultNwChange(int nwId) {
        dlogi("notifyDefaultNwChange - nwId: " + nwId
                    + " activeWwanV4Ifname:" + activeWwanV4IfName
                    + " activeWwanV6Ifname:" + activeWwanV6IfName
                    + " activeWlanIfname:" + activeWlanIfName);
        mDefaultNetwork = nwId;
        // Change default network interface in main table to new one
        if ((nwId == ConnectivityManager.TYPE_WIFI) && (activeWlanIfName != null)) {
            // Need to delete the host entry
            if (mRemoveHostEntry) {
                mRemoveHostEntry = false;
                IBinder b = ServiceManager.getService(Context.NETWORKMANAGEMENT_SERVICE);
                INetworkManagementService nms = INetworkManagementService.Stub.asInterface(b);
                try {
                    nms.delDstRoute(hostRoutingIpAddr);
                } catch (Exception e) {
                    logw("Error deleting host routing");
                }
            }
        }
        if (mService != null) {
            mService.setDefaultRoute(nwId, IPVersion.INET);
        } else {
            dlogw( "notifyDefaultNwChange: mService in NULL");
        }
        updateDefaultNetwork();
        return;
    }

    /* Fmc Related */
    private FmcRegInfo fmcRegInfo = null;

    /** {@hide} */
    private class FmcRegInfo implements IBinder.DeathRecipient {
        protected boolean enabled; // true when FMC is enabled
        protected boolean actionStop; // when stop got pushed
        protected boolean dsAvail; // DS is available
        protected int lastSendStatus; // last send status to OEM
        protected String ssid; // CT AP
        private IBinder mBinder;
        IFmcEventListener mListener;

        /* constructor */
        public FmcRegInfo(IBinder binder) {
            mBinder = binder;
            mListener = null;
            enabled = false;
            actionStop = false;
            dsAvail = false;
            ssid = "";
            lastSendStatus = -1;
            dlogi( "FmcRegInfo - binder=" + mBinder);
            if (mBinder != null) {
                mListener = (IFmcEventListener) IFmcEventListener.Stub.asInterface(binder);
                try {
                    dlogi( "FmcRegInfo: linking binder to death");
                    mBinder.linkToDeath(this, 0);
                } catch (RemoteException e) {
                    binderDied();
                    e.printStackTrace();
                }
            }
        }

        public void binderDied() {
            mListener = null;
            dlogi( "CNE@FmcRegInfo: RemoteException.");
        }
    }

    /**
     * {@hide} return: true when send out status successfully
     */
    private void onFmcStatus(int status) {
        FmcRegInfo rInfo;
        rInfo = getFmcObj();
        if (rInfo != null) {
            rInfo.lastSendStatus = status;
            if ((rInfo.lastSendStatus == FmcNotifier.FMC_STATUS_CLOSED)
                    || (rInfo.lastSendStatus == FmcNotifier.FMC_STATUS_FAILURE)
                    || (rInfo.lastSendStatus == FmcNotifier.FMC_STATUS_DS_NOT_AVAIL)
                    || (rInfo.lastSendStatus == FmcNotifier.FMC_STATUS_RETRIED)) {
                mRemoveHostEntry = true;
                try {
                    // tell telephony service to do all necessary
                    // network checking, no data call
                    ITelephony mPhone = getPhone();
                    if (mPhone == null)
                        logw("handle wwan bring up found null telephony object");
                    else
                        mPhone.setDataReadinessChecks(true, true, false);
                } catch (RemoteException e) {
                    logw("remoteException while calling setDataReadinessChecks");
                }
            }
            dlogi( "onFmcStatus: mRemoveHostEntry=" + mRemoveHostEntry);
            if (rInfo.mListener != null) {
                try {
                    // Check status and send to OEM
                    dlogi("onFmcStatus: fmc_status=" + FmcNotifier.FMC_STATUS_STR[status]);
                    rInfo.mListener.onFmcStatus(status);
                } catch (RemoteException e) {
                    logw("onFmcStatus: exception onFmcStatus");
                }
            }
        } else {
            logw("onFmcStatus: regInfo = null");
        }
    }

    /** {@hide} */
    private FmcRegInfo getFmcObj() {
        return fmcRegInfo;
    }

    /** {@hide} */
    private void setFmcObj(FmcRegInfo obj) {
        fmcRegInfo = obj;
    }

    /** {@hide} */
    private FmcRegInfo reestablishBinder(FmcRegInfo rSrc, IBinder binder) {
        FmcRegInfo r = null;
        if (binder != rSrc.mBinder) {
            dlogi( "reestablishBinder: new binder");
            r = new FmcRegInfo(binder);
            r.ssid = rSrc.ssid;
            r.enabled = rSrc.enabled;
            r.dsAvail = rSrc.dsAvail;
            r.lastSendStatus = rSrc.lastSendStatus;
            setFmcObj(r);
        } else {
            dlogi( "reestablishBinder: existing binder");
            r = rSrc;
        }
        return r;
    }

    /** {@hide} */
    public boolean startFmc(IBinder binder) {
        boolean ok = true;
        boolean reqToStart = true;
        FmcRegInfo rInfo = getFmcObj();
        dlogi( "startFmc: ");
        if (rInfo != null) {
            rInfo = reestablishBinder(rInfo, binder);
            if (rInfo.enabled) { // already enabled
                onFmcStatus(FmcNotifier.FMC_STATUS_ENABLED);
                reqToStart = false;
            } else if (rInfo.dsAvail) {
                onFmcStatus(FmcNotifier.FMC_STATUS_REGISTRATION_SUCCESS);
                reqToStart = false;
            }
        } else { /* new OEM registration for this app */
            setFmcObj(new FmcRegInfo(binder));
        }
        if (reqToStart) {

            ConnectivityManager cm = (ConnectivityManager) mContext
                    .getSystemService(Context.CONNECTIVITY_SERVICE);
            NetworkInfo networkInfo = cm.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
            NetworkInfo.State networkState = (networkInfo
                    == null ? NetworkInfo.State.UNKNOWN : networkInfo.getState());
            dlogi("CNE@startFmc: wifi state=" + networkState);
            if (networkState != NetworkInfo.State.CONNECTED) {
                dlogi("CNE@startFmc: wifi not ready");
                onFmcStatus(FmcNotifier.FMC_STATUS_FAILURE);
                return ok;
            }
            onFmcStatus(FmcNotifier.FMC_STATUS_INITIALIZED);
            // send enableFMC to Cnd
            CNERequest rr = CNERequest.obtain(CNE_REQUEST_START_FMC_CMD);
            if (rr == null) {
                ok = false;
                logw("startFmc: rr=NULL.");
                onFmcStatus(FmcNotifier.FMC_STATUS_FAILURE);
            } else {
                WifiInfo wifiInfo = mWifiManager.getConnectionInfo();
                String ssid = wifiInfo.getSSID();

                rr.mp.writeString(ssid);
                send(rr);
            }
        }
        return ok;
    }

    /** {@hide} */
    public boolean stopFmc(IBinder binder) {
        boolean ok = true;
        FmcRegInfo rInfo = getFmcObj();
        dlogi( "stopFmc");
        if (rInfo != null) {
            rInfo.actionStop = true;
            onFmcStatus(FmcNotifier.FMC_STATUS_SHUTTING_DOWN);
            // Send stopFMC to cnd
            CNERequest rr = CNERequest.obtain(CNE_REQUEST_STOP_FMC_CMD);
            if (rr == null) {
                logw("stopFmc: rr=NULL.");
                ok = false;
            } else {
                send(rr);
            }
        } else {
            onFmcStatus(FmcNotifier.FMC_STATUS_NOT_YET_STARTED);
        }
        return ok;
    }

    /** {@hide} */
    public boolean getFmcStatus(IBinder binder) {
        boolean ok = false;
        FmcRegInfo rInfo = getFmcObj();
        dlogi( "getFmcStatus: ");
        if (rInfo != null) {
            rInfo = reestablishBinder(rInfo, binder);
            if (rInfo.lastSendStatus != -1) {
                onFmcStatus(rInfo.lastSendStatus); // re-send last status
                ok = true;
            }
        }
        return ok;
    }

    /* LinkSocket API Implementations */

    /**
     * Translate a LinkSocket requestLink to a CNE getLink
     * @param cap
     * @param binder
     * @return socket id
     */
    public int requestLink(LinkCapabilities cap, IBinder binder) {
        ILinkSocketMessageHandler callback = ILinkSocketMessageHandler.Stub.asInterface(binder);

        // translating role strings to integers
        // hard coded until we figure out what the new XML file is going to look like in QCNE
        int roleInt = 0;
        String roleString = cap.get(LinkCapabilities.Key.RW_ROLE);
        dlogv( "Converting Role: " + roleString);
        if (roleString == null)  roleInt = 0;
        else if (roleString.equals(LinkCapabilities.Role.DEFAULT)) roleInt = 0;
        else if (roleString.equals(LinkCapabilities.Role.VIDEO_STREAMING_1080P)) roleInt = 1;
        else if (roleString.equals(LinkCapabilities.Role.WEB_BROWSER)) roleInt = 2;
        //QOS roles start upwards of 1000; hardcoding until policy file includes
        //QOS params.
        else if (roleString.equals(LinkCapabilities.Role.QOS_VIDEO_TELEPHONY)) roleInt = 1001;
        else logd(roleString + " is not a known role.");

        // translate LinkCapabilities into a HashMap
        dlogv( "Converting Bandwidth Req's");
        Map<LinkRequirement, String> reqs = new HashMap<LinkRequirement, String>();
        //FIXME pass all requirements to QCNE when it understands QOS roles.
        //For now, do not pass bandwidth requirements of a QoS role to CnE
        if (roleInt < QOS_ROLE_MIN || roleInt > QOS_ROLE_MAX) {
            reqs.put(LinkRequirement.FW_LINK_BW,
                    cap.get(LinkCapabilities.Key.RW_REQUIRED_FWD_BW) + "kbps");
            reqs.put(LinkRequirement.REV_LINK_BW,
                    cap.get(LinkCapabilities.Key.RW_REQUIRED_REV_BW) + "kbps");
        }

        // generate a socket id, use it in place of a PID
        dlogv( "Generating a SocketID");
        int id = getNextSocketId();

        // maintain every socket's needs. Do this for QoS and non-QoS sockets
        ExtraLinkCapabilities mcap = new ExtraLinkCapabilities();
        // initialize QoS state for all sockets
        mcap.put(LinkCapabilities.Key.RO_QOS_STATE, "inactive");
        dlogv("Populating capabilities locally for: " + cap);
        for(Entry<Integer, String> need : cap.entrySet()) {
            if (need == null) continue;
            mcap.put(need.getKey(), need.getValue());
        }
        dlogv("Populated capabilities locally as: " + mcap);
        // Synchronous access to activeCapabilities as they are updated by
        // multiple threads
        synchronized(activeCapabilities) {
            if (activeCapabilities.containsKey(id)) {
                logw("discarding needs for duplicate socket id");
            } else {
                activeCapabilities.put(id, mcap);
            }
        }
        if (roleInt >= QOS_ROLE_MIN && roleInt <= QOS_ROLE_MAX) {
            if (findQosBySocketId(id) == null) {
                synchronized(mQosTrackers) {
                    try {
                        if (!mQosTrackers.add(new QoSTracker(id, callback, mcap)))
                            dlogd("failed to track qos request for socket: " + id);
                    } catch (Exception e) {
                        dlogd("error while tracking qos request from the socket:" + id + " " + e);
                    }
                }
            } else {
                dlogd("ignoring duplicate request link of qos role for socket: " + id);
            }
        }
        // translate the call
        dlogv( "Making the call to getLink");
        this.getLink_LP(roleInt, reqs, id, new CneCallbackAdapter(id, callback));

        return id;
    }

    /**
     * Translate a LinkSocket releaseLink to a CNE releaseLink
     * @param id
     */
    public void releaseLink(int id) {
        //socket does not use mapping from a role and pid to regid.
        //setting role to -1 always.
        this.releaseLink_LP(-1, id);
    }

    /** @hide
     * Minimum available forward link (download) bandwidth for the socket.
     * This value is in kilobits per second (kbps).
     */
    public String getMinAvailableForwardBandwidth(int id) {
        QoSTracker qt = findQosBySocketId(id);
        LinkCapabilities cap = (qt == null) ? getSocketXCapabilities(id) : qt.getQosCapabilities();
        if (cap == null) return null;
        dlogd("getMinAvailableForwardBandiwdth ("+id+")");
        return cap.get(LinkCapabilities.Key.RO_MIN_AVAILABLE_FWD_BW);
    }

    /** @hide
     * Maximum available forward link (download) bandwidth for the socket.
     * This value is in kilobits per second (kbps).
     */
    public String getMaxAvailableForwardBandwidth(int id) {
        QoSTracker qt = findQosBySocketId(id);
        LinkCapabilities cap = (qt == null) ? getSocketXCapabilities(id) : qt.getQosCapabilities();
        if (cap == null) return null;
        dlogd("getMaxAvailableForwardBandiwdth ("+id+")");
        return cap.get(LinkCapabilities.Key.RO_MAX_AVAILABLE_FWD_BW);
    }

    /** @hide
     * Minimum available reverse link (upload) bandwidth for the socket.
     * This value is in kilobits per second (kbps).
     */
    public String getMinAvailableReverseBandwidth(int id) {
        QoSTracker qt = findQosBySocketId(id);
        LinkCapabilities cap = (qt == null) ? getSocketXCapabilities(id) : qt.getQosCapabilities();
        if (cap == null) return null;
        dlogd("getMinAvailableReverseBandiwdth ("+id+")");
        return cap.get(LinkCapabilities.Key.RO_MIN_AVAILABLE_REV_BW);
    }

    /** @hide
     * Maximum available reverse link (upload) bandwidth for the socket.
     * This value is in kilobits per second (kbps).
     */
    public String getMaxAvailableReverseBandwidth(int id) {
        QoSTracker qt = findQosBySocketId(id);
        LinkCapabilities cap = (qt == null) ? getSocketXCapabilities(id) : qt.getQosCapabilities();
        if (cap == null) return null;
        dlogd("getMaxAvailableReverseBandiwdth ("+id+")");
        return cap.get(LinkCapabilities.Key.RO_MAX_AVAILABLE_REV_BW);
    }

    /** @hide
     * Current estimated downlink latency of the socket, in milliseconds.
     */
    public String getCurrentFwdLatency(int id) {
        QoSTracker qt = findQosBySocketId(id);
        LinkCapabilities cap = (qt == null) ? getSocketXCapabilities(id) : qt.getQosCapabilities();
        if (cap == null) return null;
        dlogd("getCurrentForwardLatency ("+id+")");
        return cap.get(LinkCapabilities.Key.RO_CURRENT_FWD_LATENCY);
    }

    /** @hide
     * Current estimated uplink latency of the socket, in milliseconds.
     */
    public String getCurrentRevLatency(int id) {
        QoSTracker qt = findQosBySocketId(id);
        LinkCapabilities cap = (qt == null) ? getSocketXCapabilities(id) : qt.getQosCapabilities();
        if (cap == null) return null;
        dlogd("getCurrentReverseLatency ("+id+")");
        return cap.get(LinkCapabilities.Key.RO_CURRENT_REV_LATENCY);
    }

    /** @hide
     * Current state for Quality of Service for this socket
     */
    public String getQosState(int id) {
        QoSTracker qt = findQosBySocketId(id);
        LinkCapabilities cap = (qt == null) ? getSocketXCapabilities(id) : qt.getQosCapabilities();
        if (cap == null) return null;
        dlogd("getQosState ("+id+")");
        return cap.get(LinkCapabilities.Key.RO_QOS_STATE);
    }

    /** @hide
     * An integer representing the network type.
     * @see ConnectivityManager
     */
    public int getNetworkType(int id) {
        return -1;
    }

    /** @hide
     * start qos transaction request for the specified socket id and port
     */
    public boolean requestQoS(int id, int aport, String localAddress) {
        QoSTracker qt = findQosBySocketId(id);
        LinkCapabilities cap = getSocketXCapabilities(id);
        if (qt == null || cap == null) return false;
        QosSpec mySpec = prepareQosSpec(aport, localAddress, cap);
        if (mySpec == null) {
            dlogw("requestQos failed to prepare qos spec for socket: " + id);
            return false;
        }
        qt.startQosTransaction(mySpec);
        return true;
    }

    /** @hide
     * Adapter class to translates CNE callbacks to LinkSocket callbacks.
     */
    public class CneCallbackAdapter extends IConSvcEventListener.Stub {

        protected int mId = 0;
        protected ILinkSocketMessageHandler mLsmh;

        public CneCallbackAdapter(int id, ILinkSocketMessageHandler lsmh) {
            mId = id;
            mLsmh = lsmh;
        }

        /**
         * Translate a CNE onLinkAvail to a LinkSocket onLinkAvail.
         *
         * A reportLinkSatisfaction event is sent as soon as an
         * onLinkAvail callback is received because LinkSocket just
         * assumes that the application is satisfied.
         */
        public void onLinkAvail(LinkInfo info) throws RemoteException {
            android.util.Log.v("CneCallbackAdapter", "onLinkAvail EX");
            reportLinkSatisfaction_LP(0, mId, info, true, true);
            LinkProperties prop = new LinkProperties();
            prop.addAddress(info.getIpAddr());
            mLsmh.onLinkAvail(prop);
        }

        /**
         * Translate a CNE onBetterLinkAvail to a LinkSocket onBetterLinkAvail.
         *
         * A rejectSwitch is sent as soon as a onBetterLinkAvail is received
         * because LinkSocket doesn't have the same concept of a link switch
         * as CNE.
         */
        public void onBetterLinkAvail(LinkInfo info) throws RemoteException {
            android.util.Log.v("CneCallbackAdapter", "onBetterLinkAvail EX");
            rejectSwitch_LP(0, mId, info, true);
            mLsmh.onBetterLinkAvail();
        }

        /**
         * Translate a CNE onLinkLost to a LinkSocket onLinkLost.
         */
        public void onLinkLost(LinkInfo info) throws RemoteException {
            android.util.Log.v("CneCallbackAdapter", "onLinkLost EX");
            mLsmh.onLinkLost();
        }

        /**
         * Translate a CNE onGetLinkFailure to a LinkSocket onGetLinkFailure.
         */
        public void onGetLinkFailure(int reason) throws RemoteException {
            android.util.Log.v("CneCallbackAdapter", "onGetLinkFailure EX");
            mLsmh.onGetLinkFailure(reason);
        }
    }

    /**
     * QOS
     */
    // Creates QosSpec for a give role type
    private QosSpec prepareQosSpec(int localPort, String localAddress, LinkCapabilities myCap) {
        /*
         * prepare QoSSpec as per the role defined.
         * TODO retrieve flow information from the carrier policy xml file
         * based on the role type.
         * TODO retrieve technology type from CNE and use applicable 3GPP
         * or 3GPP2 params from the config file.

         * FIXME For now we are hardcoding this flow information based on
         * videoe_telephony role type. for qos_video_telephony role on a
         * 3GPP radio there are two flows, incoming and outgoing with both
         * of their classification as STREAMING. Latency and bit rates
         * will be provided by the application via link capabilities.
         */
         // Some parameters in the qos spec will always be the same no matter what,
         // declare them here
        final String TOS_MASK = "255";
        final String SRC_PORT_RANGE = "0";
        final String defaultLatency = "100";
        final String maxFwdBw = "16000000";
        final String maxRevBw = "5000000";
        final String minBw = "64000";

        dlogd("preparing qosspec");

        QosSpec mQosSpec = new QosSpec();
        //for umts, create two flows per spec, viz tx and rx.
        QosSpec.QosPipe txPipe = mQosSpec.createPipe();
        QosSpec.QosPipe rxPipe = mQosSpec.createPipe();
        txPipe.put(QosSpec.QosSpecKey.SPEC_INDEX, "0");
        rxPipe.put(QosSpec.QosSpecKey.SPEC_INDEX, "0");
        txPipe.put(QosSpec.QosSpecKey.FLOW_DIRECTION,
                Integer.toString(QosSpec.QosDirection.QOS_TX));
        rxPipe.put(QosSpec.QosSpecKey.FLOW_DIRECTION,
                Integer.toString(QosSpec.QosDirection.QOS_RX));
        txPipe.put(QosSpec.QosSpecKey.FLOW_TRAFFIC_CLASS,
                Integer.toString(QosSpec.QosClass.STREAMING));
        rxPipe.put(QosSpec.QosSpecKey.FLOW_TRAFFIC_CLASS,
                Integer.toString(QosSpec.QosClass.STREAMING));

        /*
         * lookup needs and translate to QosSpec
         * The Flow and filter information is translated from capabilities
         * to QoS Spec as follows:
         * ------------------------------------------------------------------------------------
         * Capabilities key                     QosSpec direction and key combo
         * ------------------------------------------------------------------------------------
         * RW_DESIRED_FWD_BW            ==>     QOS_RX, FLOW_DATA_RATE_MAX
         * RW_REQUIRED_FWD_BW           ==>     QOS_RX, FLOW_DATA_RATE_MIN
         * RW_DESIRED_REV_BW            ==>     QOS_TX, FLOW_DATA_RATE_MAX
         * RW_REQUIRED_REV_BW           ==>     QOS_TX, FLOW_DATA_RATE_MIN
         * RW_MAX_ALLOWED_FWD_LATENCY    ==>     QOS_RX, FLOW_LATENCY
         * RW_MAX_ALLOWED_REV_LATENCY   ==>     QOS_TX, FLOW_LATENCY
         * RO_TRANSPORT_PROTO_TYPE,
         *     RW_DEST_PORTS       ==>     QOS_TX,
         *                                          FILTER_[PROTO]_DESTINATION_PORT_START,
         *                                          FILTER_[PROTO]_DESTINATION_PORT_RANGE
         *     localPort (local)        ==>         FILTER_[PROTO]_SOURCE_PORT_START,
         *     SOURCE_PORT_RANGE(local) ==>         FILTER_[PROTO]_SOURCE_PORT_RANGE
         *
         * RW_DEST_IP_ADDRESSES         ==>     QOS_TX, FILTER_IPV4_DESTINATION_ADDR
         * localAddress (local)         ==>     QOS_TX, FILTER_IPV4_SOURCE_ADDR,
         *                              ==>     QOS_RX, FILTER_IPV4_DESTINATION_ADDR
         * RW_FILTERSPEC_IP_TOS         ==>     QOS_TX, FILTER_IPV4_TOS
         * TOS_MASK (local)             ==>     QOS_TX, FILTER_IPV4_TOS_MASK
         * ------------------------------------------------------------------------------------
         *
         * *******************************************************************
         *  IMPORTANT: if the capability is not defined then do not populate
         *  the spec with null values, the modem will reject it.
         * *******************************************************************
         */
        if (myCap == null) {
            dlogw("prepareQosSpec failed because needs is null");
            return null;
        }

        String temp = null;
        // apply flow spec, convert unit for capabilities kbps to qosspec bps
        if ((temp = myCap.get(LinkCapabilities.Key.RW_DESIRED_REV_BW)) != null) {
            txPipe.put(QosSpec.QosSpecKey.FLOW_DATA_RATE_MAX, temp);
        } else {
            txPipe.put(QosSpec.QosSpecKey.FLOW_DATA_RATE_MAX, maxRevBw);
        }
        if ((temp = myCap.get(LinkCapabilities.Key.RW_REQUIRED_REV_BW)) != null) {
            txPipe.put(QosSpec.QosSpecKey.FLOW_DATA_RATE_MIN, temp);
        } else {
            txPipe.put(QosSpec.QosSpecKey.FLOW_DATA_RATE_MIN, minBw);
        }
        if ((temp = myCap.get(LinkCapabilities.Key.RW_MAX_ALLOWED_REV_LATENCY)) != null) {
            txPipe.put(QosSpec.QosSpecKey.FLOW_LATENCY, temp);
        } else {
            txPipe.put(QosSpec.QosSpecKey.FLOW_LATENCY, defaultLatency);
        }
        if ((temp = myCap.get(LinkCapabilities.Key.RW_DESIRED_FWD_BW)) != null) {
            rxPipe.put(QosSpec.QosSpecKey.FLOW_DATA_RATE_MAX, temp);
        } else {
            rxPipe.put(QosSpec.QosSpecKey.FLOW_DATA_RATE_MAX, maxFwdBw);
        }
        if ((temp = myCap.get(LinkCapabilities.Key.RW_REQUIRED_FWD_BW)) != null) {
            rxPipe.put(QosSpec.QosSpecKey.FLOW_DATA_RATE_MIN, temp);
        } else {
            rxPipe.put(QosSpec.QosSpecKey.FLOW_DATA_RATE_MIN, minBw);
        }
        if ((temp = myCap.get(LinkCapabilities.Key.RW_MAX_ALLOWED_FWD_LATENCY)) != null) {
            rxPipe.put(QosSpec.QosSpecKey.FLOW_LATENCY, temp);
        } else {
            rxPipe.put(QosSpec.QosSpecKey.FLOW_LATENCY, defaultLatency);
        }

        //create transport specific port keys, and use them to apply
        //the filter spec for ports
        int srcPortStartKey, srcPortRangeKey;
        int dstPortStartKey, dstPortRangeKey, dstPortStartVal, dstPortRangeVal;

        if ((temp = myCap.get(LinkCapabilities.Key.RO_TRANSPORT_PROTO_TYPE)) == null) {
            logw("prepareQosSpec failed - transport protocol is not set");
            return null;
        }
        if (temp.equals("udp")) {
            srcPortStartKey = QosSpec.QosSpecKey.FILTER_UDP_SOURCE_PORT_START;
            srcPortRangeKey = QosSpec.QosSpecKey.FILTER_UDP_SOURCE_PORT_RANGE;
            dstPortStartKey = QosSpec.QosSpecKey.FILTER_UDP_DESTINATION_PORT_START;
            dstPortRangeKey = QosSpec.QosSpecKey.FILTER_UDP_DESTINATION_PORT_RANGE;
        } else if (temp.equals("tcp")) {
            srcPortStartKey = QosSpec.QosSpecKey.FILTER_TCP_SOURCE_PORT_START;
            srcPortRangeKey = QosSpec.QosSpecKey.FILTER_TCP_SOURCE_PORT_RANGE;
            dstPortStartKey = QosSpec.QosSpecKey.FILTER_TCP_DESTINATION_PORT_START;
            dstPortRangeKey = QosSpec.QosSpecKey.FILTER_TCP_DESTINATION_PORT_RANGE;
        } else {
            logw("prepareQosSpec failed - unrecognized transport: " + temp);
            return null;
        }

        //Passed by linkSocket after it is bound to a suitable port.
        //dlogi("using localport for source filter: " + localPort);
        //txPipe.put(srcPortStartKey, Integer.toString(localPort)); // disable for trials
        //txPipe.put(srcPortRangeKey, SRC_PORT_RANGE);
        if ((temp = myCap.get(LinkCapabilities.Key.RW_DEST_PORTS)) != null) {
            //validate if destination port is formatted correctly.
            if (temp.contains("-")) {
                String []tok = temp.split("-");
                if ((tok.length != 2) ||
                        ((dstPortStartVal = Integer.parseInt(tok[0])) >
                        (dstPortRangeVal = Integer.parseInt(tok[1])))) {
                    dlogw("startQos failed due to invalid dst port format: "
                            + temp);
                    return null;
                }
                dstPortRangeVal = dstPortRangeVal - dstPortStartVal;
            } else {
                dstPortStartVal = Integer.parseInt(temp);
                dstPortRangeVal = 0;
            }
            txPipe.put(dstPortStartKey, Integer.toString(dstPortStartVal));
            txPipe.put(dstPortRangeKey, Integer.toString(dstPortRangeVal));
            rxPipe.put(srcPortStartKey, Integer.toString(dstPortStartVal));
            rxPipe.put(srcPortRangeKey, Integer.toString(dstPortRangeVal));
        }
        // apply local address to the filter spec as source for outgoing
        //txPipe.put(QosSpec.QosSpecKey.FILTER_IPV4_SOURCE_ADDR, localAddress); //not allowed in rel 8
        // apply local port to the filter spec as dest port for incoming flow
        //rxPipe.put(dstPortStartKey, Integer.toString(localPort)); // disable for trials
        //rxPipe.put(dstPortRangeKey, SRC_PORT_RANGE);
        // apply local address to the filter spec as destination for the incoming flow
        //rxPipe.put(QosSpec.QosSpecKey.FILTER_IPV4_DESTINATION_ADDR, localAddress); //not allowed in rel 8
        // apply destination ip filter spec as dst for tx and src for rx flow
        if ((temp = myCap.get(LinkCapabilities.Key.RW_DEST_IP_ADDRESSES)) != null) {
            txPipe.put(QosSpec.QosSpecKey.FILTER_IPV4_DESTINATION_ADDR, temp);
            rxPipe.put(QosSpec.QosSpecKey.FILTER_IPV4_SOURCE_ADDR, temp);
        }
        // apply TOS
        if ((temp = myCap.get(LinkCapabilities.Key.RW_FILTERSPEC_IP_TOS)) != null) {
            txPipe.put(QosSpec.QosSpecKey.FILTER_IPV4_TOS, temp);
            txPipe.put(QosSpec.QosSpecKey.FILTER_IPV4_TOS_MASK, TOS_MASK);
        }
        dlogi("prepared qos spec: " + mQosSpec);
        return mQosSpec;
    }

    private QoSTracker findQosBySocketId(int id) {
        QoSTracker tracker = null;
        synchronized(mQosTrackers) {
            for (QoSTracker qt : mQosTrackers) {
                if (qt == null) continue;
                if (id == qt.getSocketId()) {
                    tracker = qt;
                    break;
                }
            }
        }
        return tracker;
    }

    private QoSTracker findQosByQosId(int id) {
        QoSTracker tracker = null;
        synchronized(mQosTrackers) {
            for (QoSTracker qt : mQosTrackers) {
                if (qt == null) continue;
                if (id == qt.getQosId()) {
                    tracker = qt;
                    break;
                }
            }
        }
        return tracker;
    }

    private ExtraLinkCapabilities getSocketXCapabilities(int id) {
        return activeCapabilities.get(id);
    }

    private static void logd (String s) {
        Log.d(LOG_TAG,s);
    }
    private static void loge (String s) {
        Log.e(LOG_TAG,s);
    }
    private static void logw (String s) {
        Log.w(LOG_TAG,s);
    }
    private static void logv (String s) {
        Log.v(LOG_TAG,s);
    }
    private static void logi (String s) {
        Log.i(LOG_TAG,s);
    }

    private static void dlogd (String s) {
        if (DBG) Log.d(LOCAL_TAG,s);
    }
    private static void dloge (String s) {
        if (DBG) Log.e(LOCAL_TAG,s);
    }
    private static void dlogw (String s) {
        if (DBG) Log.w(LOCAL_TAG,s);
    }
    private static void dlogv (String s) {
        if (DBG) Log.v(LOCAL_TAG,s);
    }
    private static void dlogi (String s) {
        if (DBG) Log.i(LOCAL_TAG,s);
    }
}
