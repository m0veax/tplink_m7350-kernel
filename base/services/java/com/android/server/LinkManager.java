/*
 * Copyright (C) 2010, 2011, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of The Linux Foundation nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

package com.android.server;

import com.android.internal.net.IPVersion;
import com.android.internal.telephony.Phone;
import com.android.internal.telephony.TelephonyIntents;
import com.android.internal.telephony.Phone.DataState;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.DhcpInfo;
import android.net.ILinkSocketMessageHandler;
import android.net.LinkCapabilities;
import android.net.LinkInfo;
import android.net.LinkProperties;
import android.net.LinkSocket;
import android.net.NetworkInfo;
import android.net.LinkProvider.LinkRequirement;
import android.net.wifi.WifiManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.os.RemoteException;
import android.os.SystemProperties;
import android.telephony.TelephonyManager;
import android.util.Log;

import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Public implementation of the ILinkManager interface.
 * Provides basic LinkManager functionality.
 */
public class LinkManager implements ILinkManager {
    private static final boolean DBG = true;
    private static final String TAG = "LinkManager";

    private CarrierProfile mCarrierProfile;
    private Context mContext;
    private ConnectivityService mConnectivityService;
    private WifiManager mWifiManager;
    private TelephonyManager mTelephonyManager;
    private Map<Integer,LinkSocketInfo> mActiveLinks; // SocketID -> LinkSocketInfo
    private HandlerThread mHandlerThread;
    private LMHandler mHandler;
    private int mDefaultNetwork;


    /**
     * Storage for LinkSocket-related data.
     */
    private class LinkSocketInfo {
        public int id; // Socket ID
        public LinkCapabilities capabilities;
        public ILinkSocketMessageHandler callback;
        public int assignedNetwork; // ConnectivityManager.TYPE_WIFI or TYPE_MOBILE
        public int status;

        // State: unassigned, assigned, lost?
        static final int UNASSIGNED = 0;
        static final int ASSIGNED = 1;
        static final int LOST = 2;

        static final int NETWORK_UNKNOWN = -1;
    }

    public LinkManager(Context context, ConnectivityService cs) {
        if (DBG) Log.v(TAG, "LinkManager constructor");

        mContext = context;
        mConnectivityService = cs;
        mCarrierProfile = new CarrierProfile();
        String carrierPolicyFilename = SystemProperties.get("persist.cne.loc.policy.op");
        try {
            mCarrierProfile.parse(new FileInputStream(carrierPolicyFilename));
        } catch (FileNotFoundException e) {
            Log.e(TAG, "Carrier Policy file not found: " + carrierPolicyFilename);
        }

        mDefaultNetwork = mCarrierProfile.getPreferredNetworks(0).get(0);
        if (DBG) Log.v(TAG, "Default network = " + networkIntToString(mDefaultNetwork));
        mConnectivityService.setDefaultRoute(mDefaultNetwork);

        mHandlerThread = new HandlerThread("LMHandler");
        mHandlerThread.start();
        mHandler = new LMHandler(mHandlerThread.getLooper());

        mActiveLinks = new ConcurrentHashMap<Integer, LinkSocketInfo>();

        mWifiManager = (WifiManager) mContext.getSystemService(Context.WIFI_SERVICE);
        mTelephonyManager = (TelephonyManager) mContext.getSystemService(Context.TELEPHONY_SERVICE);

        IntentFilter filter = new IntentFilter();
        filter.addAction(WifiManager.NETWORK_STATE_CHANGED_ACTION);
        filter.addAction(TelephonyIntents.ACTION_ANY_DATA_CONNECTION_STATE_CHANGED);
        context.registerReceiver(mIntentReceiver, filter);
    }

    BroadcastReceiver mIntentReceiver = new BroadcastReceiver() {
        public void onReceive(Context context, Intent intent) {
            final String action = intent.getAction();

            if (action.equals(WifiManager.NETWORK_STATE_CHANGED_ACTION)) {
                // Turn WiFi Intents into LinkManager Handler events (connected/disconnected)
                NetworkInfo networkInfo = (NetworkInfo) intent.getParcelableExtra(WifiManager.EXTRA_NETWORK_INFO);
                NetworkInfo.State state = (networkInfo == null ? NetworkInfo.State.UNKNOWN : networkInfo.getState());
                if (state == NetworkInfo.State.CONNECTED) {
                    Message msg = mHandler.obtainMessage(LMHandler.NETWORK_CONNECTED, ConnectivityManager.TYPE_WIFI, 0);
                    msg.sendToTarget();
                } else if (state == NetworkInfo.State.DISCONNECTED) {
                    Message msg = mHandler.obtainMessage(LMHandler.NETWORK_DISCONNECTED, ConnectivityManager.TYPE_WIFI, 0);
                    msg.sendToTarget();
                }
            } else if (action.equals(TelephonyIntents.ACTION_ANY_DATA_CONNECTION_STATE_CHANGED)) {
                // Turn Telephony Intents into LinkManager Handler events (connected/disconnected)
                String apnType = intent.getStringExtra(Phone.DATA_APN_TYPES_KEY);
                if (apnType.equals(Phone.APN_TYPE_DEFAULT)) {
                    DataState dataState = Enum.valueOf(Phone.DataState.class, intent.getStringExtra(Phone.DATA_APN_TYPE_STATE));
                    if (dataState == DataState.CONNECTED) {
                        Message msg = mHandler.obtainMessage(LMHandler.NETWORK_CONNECTED, ConnectivityManager.TYPE_MOBILE, 0);
                        msg.sendToTarget();
                    } else if (dataState == DataState.DISCONNECTED) {
                        Message msg = mHandler.obtainMessage(LMHandler.NETWORK_DISCONNECTED, ConnectivityManager.TYPE_MOBILE, 0);
                        msg.sendToTarget();
                    }
                }
            } else {
                if (DBG) Log.d(TAG, "Received unexpected action: " + action);
            }
        }
    };

    /**
     * LinkManager event handler class.
     * The majority of the LinkManager logic resides here.
     */
    class LMHandler extends Handler {
        // Message types
        static final int REQUEST_LINK = 1;
        static final int RELEASE_LINK = 2;
        static final int NETWORK_CONNECTED = 3;
        static final int NETWORK_DISCONNECTED = 4;
        static final int CONNECTION_TIMED_OUT = 5;

        // Constants
        static final int CONNECTION_TIMEOUT = 10000; // ms

        public LMHandler(Looper looper) {
            super(looper);
        }

        public void handleMessage(Message msg) {
            switch (msg.what) {
                case REQUEST_LINK:
                    handleRequestLink(msg);
                    break;
                case RELEASE_LINK:
                    handleReleaseLink(msg);
                    break;
                case NETWORK_CONNECTED:
                    handleNetworkConnected(msg);
                    break;
                case NETWORK_DISCONNECTED:
                    handleNetworkDisconnected(msg);
                    break;
                case CONNECTION_TIMED_OUT:
                    handleConnectionTimedOut(msg);
                    break;
                default:
                    if (DBG) Log.d(TAG, "LMHandler handleMessage: unknown message");
                    break;
            }
        }

        private void handleRequestLink(Message msg) {
            int id = msg.arg1;
            LinkSocketInfo lsInfo = mActiveLinks.get(id);
            LinkCapabilities capabilities = lsInfo.capabilities; // needed capabilities
            int role = roleStringToInt(capabilities.get(LinkCapabilities.Key.RW_ROLE));
            List<Integer> preferredNetworks = mCarrierProfile.getPreferredNetworks(role);
            applyCapabilitiesToNetworks(capabilities, preferredNetworks);

            if (DBG) Log.v(TAG, "handleRequestLink id: " + id + " role: " + role);

            // assign to the first active network
            for (int network : preferredNetworks) {
                if (isNetworkConnected(network)) {
                    getMinAvailableForwardBandwidth(network); // Stub for bandwidth estimation
                    // TODO: add a timer for bandwidth estimate changed.
                    if (DBG) Log.v(TAG, "assigning id " + lsInfo.id + " to network " + networkIntToString(network));
                    lsInfo.assignedNetwork = network;
                    lsInfo.status = LinkSocketInfo.ASSIGNED;
                    break;
                }
            }

            if (lsInfo.status == LinkSocketInfo.ASSIGNED) {
                // no changes are necessary, send the onLinkAvail notification
                try {
                    lsInfo.callback.onLinkAvail(propertiesForNetwork(lsInfo.assignedNetwork));
                } catch (RemoteException e) {
                    if (DBG) Log.d(TAG, "RemoteException sending onLinkAvail()");
                }
            } else {
                // No active networks are acceptable - bring up the first preference
                int network = preferredNetworks.get(0);
                lsInfo.assignedNetwork = network;
                if (DBG) Log.d(TAG, "cannot assign to available networks - bringing up " + networkIntToString(network));
                mConnectivityService.reconnect(network);
                // Start timer for connection
                Message timeoutMsg = mHandler.obtainMessage(CONNECTION_TIMED_OUT, id, network);
                mHandler.sendMessageDelayed(timeoutMsg, CONNECTION_TIMEOUT);
            }
        }

        private void handleReleaseLink(Message msg) {
            LinkSocketInfo released = (LinkSocketInfo) msg.obj;
            int releasedNetwork = released.assignedNetwork;
            if (DBG) Log.v(TAG, "handleReleaseLink net: " + releasedNetwork);

            // Trigger teardown if no other links are using the network
            // (but don't tear down the last network or the default network)
            if (anotherNetworkIsAvailable(releasedNetwork) && releasedNetwork != mDefaultNetwork) {
                boolean shouldBringDownNetwork = true;
                for (LinkSocketInfo lsInfo : mActiveLinks.values()) {
                    if (lsInfo.assignedNetwork == releasedNetwork) {
                        shouldBringDownNetwork = false;
                        break;
                    }
                }
                if (shouldBringDownNetwork) {
                    if (DBG) Log.v(TAG, "bringing down network: " + releasedNetwork);
                    mConnectivityService.teardown(releasedNetwork);
                }
            }
        }

        private void handleNetworkConnected(Message msg) {
            int network = msg.arg1;
            if (DBG) Log.v(TAG, "handleNetworkConnected: " + networkIntToString(network));

            // check for waiting connections
            for (LinkSocketInfo lsInfo : mActiveLinks.values()) {
                if (lsInfo.status == LinkSocketInfo.UNASSIGNED && lsInfo.assignedNetwork == network) {
                    lsInfo.status = LinkSocketInfo.ASSIGNED;
                    try {
                        lsInfo.callback.onLinkAvail(propertiesForNetwork(lsInfo.assignedNetwork));
                    } catch (RemoteException e) {
                        if (DBG) Log.d(TAG, "RemoteException sending onLinkAvail()");
                    }
                }
            }

            bringDownUnusedNetworks();
        }

        private void handleNetworkDisconnected(Message msg) {
            int network = msg.arg1;
            if (DBG) Log.v(TAG, "handleNetworkDisconnected: " + networkIntToString(network));

            // send notifications to all affected links
            for (LinkSocketInfo lsInfo : mActiveLinks.values()) {
                if (lsInfo.assignedNetwork == network) {
                    lsInfo.status = LinkSocketInfo.LOST;
                    try {
                        lsInfo.callback.onLinkLost();
                    } catch (RemoteException e) {
                        if (DBG) Log.d(TAG, "RemoteException sending onLinkLost()");
                    }

                    if (anotherNetworkIsAvailable(network)) {
                        try {
                            lsInfo.callback.onBetterLinkAvail();
                        } catch (RemoteException e) {
                            if (DBG) Log.d(TAG, "RemoteException sending onBetterLinkAvail()");
                        }
                    }
                }
            }
        }

        private void handleConnectionTimedOut(Message msg) {
            int id = msg.arg1;
            int network = msg.arg2;
            if (DBG) Log.v(TAG, "handleConnectionTimedOut (possibly) id: " + id + " net: " + network);

            // check if the connection actually timed out
            if (mActiveLinks.get(id).status == LinkSocketInfo.ASSIGNED) return;
            if (DBG) Log.v(TAG, "handleConnectionTimedOut actual timeout id: " + id + " net: " + network);

            // bring down the failing network
            if (network != mDefaultNetwork) {
                mConnectivityService.teardown(network);
            }

            // timed out. send failure.
            try {
                mActiveLinks.get(id).callback.onGetLinkFailure(LinkSocket.ERROR_ALL_NETWORKS_DOWN);
            } catch (RemoteException e) {
                if (DBG) Log.d(TAG, "RemoteException sending onGetLinkFailure()");
            }
        }

        private boolean anotherNetworkIsAvailable(int network) {
            int otherNetwork = LinkSocketInfo.NETWORK_UNKNOWN;

            if (network == ConnectivityManager.TYPE_MOBILE) otherNetwork = ConnectivityManager.TYPE_WIFI;
            else otherNetwork = ConnectivityManager.TYPE_MOBILE;

            return isNetworkConnected(otherNetwork);
        }

        private void bringDownUnusedNetworks() {
            if (DBG) Log.v(TAG, "bringDownUnusedNetworks() EX");

            int[] allNetworks = {ConnectivityManager.TYPE_MOBILE, ConnectivityManager.TYPE_WIFI};

            for (int i = 0; i < allNetworks.length; i++) {
                int network = allNetworks[i];
                if (network == mDefaultNetwork) continue; // Don't bring down default network
                if (!anotherNetworkIsAvailable(network)) continue; // Don't bring down the only active network

                boolean networkInUse = false;
                for (LinkSocketInfo lsInfo : mActiveLinks.values()) {
                    if (lsInfo.assignedNetwork == network) {
                        networkInUse = true;
                        break;
                    }
                }

                if (!networkInUse) {
                    if (DBG) Log.v(TAG, "bringing down unused network: " + networkIntToString(network));
                    mConnectivityService.teardown(network);
                }
            }
        }

        /**
         * Modifies preferredNetworks list according to keys in capabilities.
         *
         * @param capabilities The needed LinkCapabilities.
         * @param preferredNetworks The list of networks to modify.
         */
        private void applyCapabilitiesToNetworks(LinkCapabilities capabilities,
                List<Integer> preferredNetworks) {
            if (DBG) Log.v(TAG, "applyCapabilitiesToNetworks: caps" + capabilities + "nets" + preferredNetworks);

            // Apply "allowed networks" rule
            if (capabilities.containsKey(LinkCapabilities.Key.RW_ALLOWED_NETWORKS)) {
                String allowed = capabilities.get(LinkCapabilities.Key.RW_ALLOWED_NETWORKS);
                List<Integer> allowedNetworks = csNetworkStringToList(allowed);

                // remove all networks that aren't allowed
                preferredNetworks.retainAll(allowedNetworks);
            }

            // Apply "prohibited networks" rule
            if (capabilities.containsKey(LinkCapabilities.Key.RW_PROHIBITED_NETWORKS)) {
                String prohibited = capabilities.get(LinkCapabilities.Key.RW_PROHIBITED_NETWORKS);
                List<Integer> prohibitedNetworks = csNetworkStringToList(prohibited);

                preferredNetworks.removeAll(prohibitedNetworks);
            }

            if (DBG) Log.v(TAG, "applyCapabilitiesToNetworks resulting nets: " + preferredNetworks);
        }

        /**
         * Converts a comma-separated list of networks (as used in
         * LinkCapabilities) into a list of network ID's, as defined by
         * ConnectivityManager.
         *
         * @param networks Comma-separated network string, e.g. "wifi, mobile"
         * @return List of network ID's, e.g. {
         *         ConnectivityManager.NETWORK_WIFI,
         *         ConnectivityManager.NETWORK_MOBILE }
         */
        private List<Integer> csNetworkStringToList(String networks) {
            String[] netNames = networks.split(",\\s?");

            List<Integer> netList = new ArrayList<Integer>(netNames.length);
            for (String name : netNames) {
                int network = networkStringToInt(name);
                if (network != LinkSocketInfo.NETWORK_UNKNOWN) {
                    netList.add(network);
                }
            }

            return netList;
        }
    }

    public int requestLink(LinkCapabilities capabilities, IBinder binder) {
        int id = getNextSocketId();
        if (DBG) Log.v(TAG, "requestLink capabilities: " + capabilities);

        LinkSocketInfo info = new LinkSocketInfo();
        info.id = id;
        info.capabilities = capabilities;
        info.callback = ILinkSocketMessageHandler.Stub.asInterface(binder);
        info.assignedNetwork = LinkSocketInfo.NETWORK_UNKNOWN;
        info.status = LinkSocketInfo.UNASSIGNED;
        mActiveLinks.put(info.id, info);

        Message msg = mHandler.obtainMessage(LMHandler.REQUEST_LINK, id, 0);
        msg.sendToTarget();

        return id;
    }

    public void releaseLink(int id) {
        if (DBG) Log.v(TAG, "releaseLink id: " + id);
        LinkSocketInfo removed = mActiveLinks.remove(id);

        Message msg = mHandler.obtainMessage(LMHandler.RELEASE_LINK, removed);
        msg.sendToTarget();
    }

    public boolean requestQoS(int id, int localPort, String localAddress) {
        if (DBG) Log.e(TAG, "requestQoS is not supported");
        return false;
    }

    /**
     * Minimum available forward link (download) bandwidth for the socket. This value is
     * in kilobits per second (kbps).
     */
    public String getMinAvailableForwardBandwidth(int id) {
        // Implementation left to OEMs
        return null;
    }

    /**
     * Maximum available forward link (download) bandwidth for the socket. This value is
     * in kilobits per second (kbps).
     */
    public String getMaxAvailableForwardBandwidth(int id) {
        // Implementation left to OEMs
        return null;
    }

    /**
     * Minimum available reverse link (upload) bandwidth for the socket. This value is
     * in kilobits per second (kbps).
     */
    public String getMinAvailableReverseBandwidth(int id) {
        // Implementation left to OEMs
        return null;
    }

    /**
     * Maximum available reverse link (upload) bandwidth for the socket. This value is
     * in kilobits per second (kbps).
     */
    public String getMaxAvailableReverseBandwidth(int id) {
        // Implementation left to OEMs
        return null;
    }

    /**
     * Current estimated downlink latency of the socket, in milliseconds.
     */
    public String getCurrentFwdLatency(int id) {
        // Implementation left to OEMs
        return null;
    }

    /**
     * Current estimated uplink latency of the socket, in milliseconds.
     */
    public String getCurrentRevLatency(int id) {
        // Implementation left to OEMs
        return null;
    }

    /**
     * Current Quality of Service state of the socket
     */
    public String getQosState(int id) {
        // Implementation left to OEMs
        return "inactive";
    }

    /**
     * An integer representing the network type.
     * @param id LinkSocket ID
     * @see ConnectivityManager
     */
    public int getNetworkType(int id) {
        int netType = -1;
        LinkSocketInfo info = mActiveLinks.get(id);
        if (info != null) {
            netType = mActiveLinks.get(id).assignedNetwork;
        } else {
            if (DBG) Log.d(TAG, "getNetworkType called with invalid id: " + id);
        }
        return netType;
    }

    /**
     * Converts a role string to a role ID (as used in OperatorPolicy.xml)
     * @param string Role
     * @return Role ID
     */
    private int roleStringToInt(String roleString) {
        int roleInt = 0;
        if (roleString == null) roleInt = 0;
        else if (roleString.equals(LinkCapabilities.Role.DEFAULT)) roleInt = 0;
        else if (roleString.equals(LinkCapabilities.Role.VIDEO_STREAMING_1080P)) roleInt = 1;
        else if (roleString.equals(LinkCapabilities.Role.WEB_BROWSER)) roleInt = 2;
        else Log.d(TAG, roleString + " is not a known role.");
        return roleInt;
    }

    /**
     * Converts network names (as used in Carrier Profile, e.g. "WWAN"; or in
     * LinkCapabilities, e.g. "mobile") to ConnectivityManager constants.
     *
     * @param network Name of network
     * @return The corresponding network constant from ConnectivityManager.
     */
    private int networkStringToInt(String network) {
        int networkInt = LinkSocketInfo.NETWORK_UNKNOWN;
        if (network == null) {
            if (DBG) Log.d(TAG, "networkStringToInt: null string");
        } else if (network.equals("WLAN") || network.equals("wifi")) {
            networkInt = ConnectivityManager.TYPE_WIFI;
        } else if (network.equals("WWAN") || network.equals("mobile")) {
            networkInt = ConnectivityManager.TYPE_MOBILE;
        } else {
            if (DBG) Log.d(TAG, network + " is not a known network name.");
        }

        if (DBG) Log.v(TAG, "networkStringToInt: " + network + ":" + networkInt);

        return networkInt;
    }

    private String networkIntToString(int network) {
        switch (network) {
            case ConnectivityManager.TYPE_WIFI:
                return "WiFi";
            case ConnectivityManager.TYPE_MOBILE:
                return "Mobile";
            default:
                // fall through
        }

        return "unknown network " + network;
    }

    private boolean isNetworkConnected(int networkType) {
        if (DBG) Log.v(TAG, "isNetworkConnected: " + networkIntToString(networkType));
        NetworkInfo networkInfo = mConnectivityService.getNetworkInfo(networkType);
        return networkInfo.isConnected();
    }

    // TODO: Find out if there's an easier way to get network addresses. Aren't they saved anywhere already?
    private LinkProperties propertiesForNetwork(int assignedNetwork) {
        if (assignedNetwork == ConnectivityManager.TYPE_MOBILE) {
            return getWwanProperties();
        } else if (assignedNetwork == ConnectivityManager.TYPE_WIFI) {
            return getWlanProperties();
        }
        return null;
    }

    private LinkProperties getWwanProperties() {
        LinkProperties props = new LinkProperties();
        if (mTelephonyManager == null) {
            if (DBG) Log.d(TAG, "mTelephonyManager is null");
            return props;
        }

        String interfaceName = mTelephonyManager.getActiveInterfaceName(Phone.APN_TYPE_DEFAULT, IPVersion.INET);
        String addressString = mTelephonyManager.getActiveIpAddress(Phone.APN_TYPE_DEFAULT, IPVersion.INET);

        try {
            NetworkInterface networkInterface = NetworkInterface.getByName(interfaceName);
            props.setInterface(networkInterface);

            InetAddress address = InetAddress.getByName(addressString);
            props.addAddress(address);
        } catch (UnknownHostException e) {
            if (DBG) Log.d(TAG, "getWwanProperties: Invalid host name " + addressString);
        } catch (SocketException e) {
            if (DBG) Log.d(TAG, "could not get network interface for " + interfaceName);
        }

        return props;
    }

    private LinkProperties getWlanProperties() {
        LinkProperties props = new LinkProperties();
        String ipAddr = null;

        DhcpInfo dhcpInfo = mWifiManager.getDhcpInfo();
        if (dhcpInfo == null) {
            if (DBG) Log.d(TAG, "DhcpInfo is null. V4 address is not yet available.");
            return props;
        }

        int ipAddressInt = dhcpInfo.ipAddress;
        if (ipAddressInt != 0) {
            ipAddr = ((ipAddressInt) & 0xff) + "."
                    + ((ipAddressInt >> 8) & 0xff) + "."
                    + ((ipAddressInt >> 16) & 0xff) + "."
                    + ((ipAddressInt >> 24) & 0xff);
        }

        try {
            InetAddress inetAddr = InetAddress.getByName(ipAddr);
            NetworkInterface netIface = NetworkInterface.getByInetAddress(inetAddr);

            props.addAddress(inetAddr);
            props.setInterface(netIface);
        } catch (UnknownHostException e) {
            if (DBG) Log.d(TAG, "getWlanProperties: Invalid host name " + ipAddr);
        } catch (SocketException e) {
            if (DBG) Log.d(TAG, "could not get network interface for " + ipAddr);
        }

        return props;
    }

    private static int mSocketId = 0;
    synchronized private static int getNextSocketId() {
        if (mSocketId == Integer.MAX_VALUE) mSocketId = 0;
        return mSocketId++;
    }

    /**
     * The default connection is specified via Carrier Policy. This API is ignored.
     */
    public void setDefaultConnectionNwPref(int preference) {
        if (DBG) Log.v(TAG, "setDefaultConnectionNwPref " + preference + " ignored");
    }

    /**
     * This seems redundant. We should probably remove this API.
     */
    public void sendDefaultNwPref2Cne(int preference) {
        setDefaultConnectionNwPref(preference);
    }

    /* FMC API stubs */
    public boolean startFmc(IBinder listener) {
        return false;
    }

    public boolean stopFmc(IBinder listener) {
        return false;
    }

    public boolean getFmcStatus(IBinder listener) {
        return false;
    }

    /* Old LinkProvider API stubs */
    public boolean switchLink_LP(int role, int mPid, LinkInfo info, boolean isNotifyBetterLink) {
        return false;
    }

    public boolean reportLinkSatisfaction_LP(int role, int mPid, LinkInfo info,
            boolean isSatisfied, boolean isNotifyBetterCon) {
        return false;
    }

    public boolean getLink_LP(int role, Map<LinkRequirement, String> linkReqs, int mPid,
            IBinder listener) {
        return false;
    }

    public boolean rejectSwitch_LP(int role, int mPid, LinkInfo info, boolean isNotifyBetterLink) {
        return false;
    }

    public boolean releaseLink_LP(int role, int mPid) {
        return false;
    }
}
