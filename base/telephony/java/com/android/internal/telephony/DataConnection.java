/*
 * Copyright (C) 2006, 2011 The Android Open Source Project
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

import java.util.ArrayList;

import com.android.internal.telephony.DataConnectionFailCause;
import com.android.internal.telephony.DataProfile;
import com.android.internal.telephony.EventLogTags;
import com.android.internal.telephony.CommandsInterface.RadioTechnology;
import com.android.internal.telephony.Phone.BearerType;
import com.android.internal.telephony.QosSpec;
import com.android.internal.util.HierarchicalState;
import com.android.internal.util.HierarchicalStateMachine;
import com.android.internal.net.IPVersion;
import java.net.Inet4Address;
import java.net.Inet6Address;
import java.net.InetAddress;
import java.net.UnknownHostException;

import android.content.Context;
import android.net.NetworkUtils;
import android.os.AsyncResult;
import android.os.Looper;
import android.os.Message;
import android.os.SystemProperties;
import android.util.EventLog;

import static com.android.internal.telephony.RILConstants.DEACTIVATE_REASON_NONE;

/**
 * {@hide}
 *
 * DataConnection HierarchicalStateMachine.
 *
 * This is an abstract base class for representing a single data connection.
 * Instances of this class such as <code>CdmaDataConnection</code> and
 * <code>GsmDataConnection</code>, * represent a connection via the cellular network.
 * There may be multiple data connections and all of them are managed by the
 * <code>DataConnectionTracker</code>.
 *
 * Instances are asynchronous state machines and have two primary entry points
 * <code>connect()</code> and <code>disconnect</code>. The message a parameter will be returned
 * hen the operation completes. The <code>msg.obj</code> will contain an AsyncResult
 * object and <code>AsyncResult.userObj</code> is the original <code>msg.obj</code>. if successful
 * with the <code>AsyncResult.result == null</code> and <code>AsyncResult.exception == null</code>.
 * If an error <code>AsyncResult.result = FailCause</code> and
 * <code>AsyncResult.exception = new Exception()</code>.
 *
 * The other public methods are provided for debugging.
 *
 * Below is the state machine description for this class.
 *
 * DataConnection {
 *   + mDefaultState {
 *        EVENT_RESET { clearSettings, notifiyDisconnectCompleted, >mInactiveState }.
 *        EVENT_CONNECT {  notifyConnectCompleted(FailCause.UNKNOWN) }.
 *        EVENT_DISCONNECT { notifyDisconnectCompleted }.
 *
 *        // Ignored messages
 *        EVENT_SETUP_DATA_CONNECTION_DONE,
 *        EVENT_GET_LAST_FAIL_DONE,
 *        EVENT_DEACTIVATE_DONE.
 *     }
 *   ++ # mInactiveState
 *        e(doNotifications)
 *        x(clearNotifications) {
 *            EVENT_RESET { notifiyDisconnectCompleted }.
 *            EVENT_CONNECT {startConnecting, >mActivatingState }.
 *            EVENT_DEACTIVATE_DONE { notifyDisconnectCompleted }
 *        }
 *   ++   mActivatingState {
 *            EVENT_DISCONNECT { %EVENT_DISCONNECT }.
 *            EVENT_SETUP_DATA_CONNECTION_DONE {
 *                  if (SUCCESS) { notifyConnectCompleted(FailCause.NONE), >mActiveState }.
 *                  if (ERR_BadCommand) {
 *                         notifyConnectCompleted(FailCause.UNKNOWN), >mInactiveState }.
 *                  if (ERR_BadDns) { tearDownData($DEACTIVATE_DONE), >mDisconnectingBadDnsState }.
 *                  if (ERR_Other) { getLastDataCallFailCause($EVENT_GET_LAST_FAIL_DONE) }.
 *                  if (ERR_Stale) {}.
 *            }
 *            EVENT_GET_LAST_FAIL_DONE { notifyConnectCompleted(result), >mInactive }.
 *        }
 *   ++   mActiveState {
 *            EVENT_DISCONNECT { tearDownData($EVENT_DEACTIVATE_DONE), >mDisconnecting }.
 *        }
 *   ++   mDisconnectingState {
 *            EVENT_DEACTIVATE_DONE { notifyDisconnectCompleted, >mInactiveState }.
 *        }
 *   ++   mDisconnectingBadDnsState {
 *            EVENT_DEACTIVATE_DONE { notifyConnectComplete(FailCause.UNKNOWN), >mInactiveState }.
 *        }
 *  }
 */
public abstract class DataConnection extends HierarchicalStateMachine {
    protected static final boolean DBG = true;

    protected static Object mCountLock = new Object();
    protected static int mCount;

    /**
     * Class returned by onSetupConnectionCompleted.
     */
    protected enum SetupResult {
        ERR_BadCommand,
        ERR_BadDns,
        ERR_Other,
        ERR_Stale,
        SUCCESS;

        public DataConnectionFailCause mFailCause;

        @Override
        public String toString() {
            switch (this) {
                case ERR_BadCommand: return "Bad Command";
                case ERR_BadDns: return "Bad DNS";
                case ERR_Other: return "Other error";
                case ERR_Stale: return "Stale command";
                case SUCCESS: return "SUCCESS";
                default: return "unknown";
            }
        }
    }

    /**
     * Used internally for saving connecting parameters.
     */
    protected static class ConnectionParams {
        public ConnectionParams(RadioTechnology radioTech, DataProfile dp, BearerType bearerType,
                Message onCompletedMsg) {
            this.radioTech = radioTech;
            this.dp = dp;
            this.bearerType = bearerType;
            this.onCompletedMsg = onCompletedMsg;
        }

        public int tag;
        public RadioTechnology radioTech;
        public DataProfile dp;
        public BearerType bearerType;
        public Message onCompletedMsg;
    }

    /**
     * An instance used for notification of blockingReset. TODO: Remove when
     * blockingReset is removed.
     */
    class ResetSynchronouslyLock {
    }

    /**
     * Used internally for saving disconnecting parameters.
     */
    protected static class DisconnectParams {
        public DisconnectParams(Message onCompletedMsg) {
            this.onCompletedMsg = onCompletedMsg;
        }

        public DisconnectParams(ResetSynchronouslyLock lockObj) {
            this.lockObj = lockObj;
        }

        public int tag;
        public Message onCompletedMsg;
        public ResetSynchronouslyLock lockObj;
    }

    // ***** Event codes for driving the state machine
    protected static final int EVENT_RESET = 1;
    protected static final int EVENT_CONNECT = 2;
    protected static final int EVENT_SETUP_DATA_CONNECTION_DONE = 3;
    protected static final int EVENT_GET_LAST_FAIL_DONE = 4;
    protected static final int EVENT_DEACTIVATE_DONE = 5;
    protected static final int EVENT_DISCONNECT = 6;

    protected static final int EVENT_QOS_ENABLE = 7;
    protected static final int EVENT_QOS_ENABLE_DONE = 8;
    protected static final int EVENT_QOS_DISABLE = 9;
    protected static final int EVENT_QOS_DISABLE_DONE = 10;
    protected static final int EVENT_QOS_MODIFY = 11;
    protected static final int EVENT_QOS_MODIFY_DONE = 12;
    protected static final int EVENT_QOS_SUSPEND = 13;
    protected static final int EVENT_QOS_SUSPEND_DONE = 14;
    protected static final int EVENT_QOS_RESUME = 15;
    protected static final int EVENT_QOS_RESUME_DONE = 16;
    protected static final int EVENT_QOS_GET_STATUS = 17;
    protected static final int EVENT_QOS_GET_STATUS_DONE = 18;
    protected static final int EVENT_QOS_IND = 19;

    // ***** Tag IDs for EventLog
    protected static final int EVENT_LOG_BAD_DNS_ADDRESS = 50100;

    // ***** Member Variables
    protected int mTag;
    protected Context mContext;
    protected CommandsInterface mCM;
    protected int cid;
    protected DataProfile mDataProfile;
    protected BearerType mBearerType;
    protected String interfaceName;
    protected String[] mIpAddresses;
    protected String[] mGatewayAddress;
    protected String[] dnsServers;
    protected ArrayList<Integer> mQosFlowIds = new ArrayList<Integer>();
    protected long createTime;
    protected long lastFailTime;
    protected DataConnectionFailCause lastFailCause;
    protected static final String NULL_IP = "0.0.0.0";
    Object userData;

    /**
     * IPV4 address change status flags
     */
    public final static int IPV4_ADDR_ADDED    =  1 << 0;
    public final static int IPV4_ADDR_UPDATED  =  1 << 1;
    public final static int IPV4_ADDR_REMOVED  =  1 << 2;

    /**
     * IPV6 address change status flags
     */
    public final static int IPV6_ADDR_ADDED    =  1 << 3;
    public final static int IPV6_ADDR_UPDATED  =  1 << 4;
    public final static int IPV6_ADDR_REMOVED  =  1 << 5;

    public final static int IPV_ADDR_NOCHANGE = 0;

    //***** Abstract methods
    public abstract String toString();

    protected abstract void onConnect(ConnectionParams cp);

    protected abstract void onQosSetup(QosSpec qosSpec);

    protected abstract void onQosRelease(int qosId);

    protected abstract void onQosSuspend(int qosId);

    protected abstract void onQosResume(int qosId);

    protected abstract void onQosGetStatus(int qosId);

    protected abstract void onQosSetupDone(int userData, String[] responses, String error);

    protected abstract void onQosReleaseDone(int qosId, String error);

    protected abstract void onQosSuspendDone(int qosId, String error);

    protected abstract void onQosResumeDone(int qosId, String error);

    protected abstract void onQosGetStatusDone(int qosId, AsyncResult ar, String error);

    protected abstract void onQosStateChangedInd(AsyncResult ar);

    protected abstract boolean isDnsOk(String[] domainNameServers);

    protected abstract void log(String s);


   //***** Constructor
    protected DataConnection(Context context, CommandsInterface ci, String name) {
        super(name, Looper.getMainLooper());
        if (DBG) log("DataConnection constructor E");
        this.mCM = ci;
        this.mContext = context;
        this.cid = -1;
        this.dnsServers = new String[2];
        this.mIpAddresses = new String[2];
        this.mGatewayAddress = new String[2];

        clearSettings();

        setDbg(false);
        addState(mDefaultState);
            addState(mInactiveState, mDefaultState);
            addState(mActivatingState, mDefaultState);
            addState(mActiveState, mDefaultState);
                addState(mQosActiveState, mActiveState);
            addState(mDisconnectingState, mDefaultState);
            addState(mDisconnectingBadDnsState, mDefaultState);
        setInitialState(mInactiveState);
        if (DBG) log("DataConnection constructor X");
    }

    /**
     * TearDown the data connection.
     *
     * @param o will be returned in AsyncResult.userObj
     *          and is either a DisconnectParams or ConnectionParams.
     */
    private void tearDownData(Object o) {
        int dcReason = DEACTIVATE_REASON_NONE;
        if (o instanceof DisconnectParams) {
            dcReason = ((DisconnectParams)o).onCompletedMsg.arg1; // reason
        }

        if (mCM.getRadioState().isOn()) {
            if (DBG) log("tearDownData radio is on, call deactivateDataCall");
            mCM.deactivateDataCall(cid,
                        dcReason,
                        obtainMessage(EVENT_DEACTIVATE_DONE, o));
        } else {
            if (DBG) log("tearDownData radio is off sendMessage EVENT_DEACTIVATE_DONE immediately");
            AsyncResult ar = new AsyncResult(o, null, null);
            sendMessage(obtainMessage(EVENT_DEACTIVATE_DONE, ar));
        }
    }

    /**
     * Tear down all the QoS flows that has been setup
     */
    private void tearDownQos() {
        for (int id: (Integer[])mQosFlowIds.toArray(new Integer[0])) {
            qosRelease(id);
        }
    }

    /**
     * Send the connectionCompletedMsg.
     *
     * @param cp is the ConnectionParams
     * @param cause
     */
    private void notifyConnectCompleted(ConnectionParams cp, DataConnectionFailCause cause) {
        Message connectionCompletedMsg = cp.onCompletedMsg;
        if (connectionCompletedMsg == null) {
            return;
        }

        long timeStamp = System.currentTimeMillis();
        connectionCompletedMsg.arg1 = cid;

        if (cause == DataConnectionFailCause.NONE) {
            createTime = timeStamp;
            AsyncResult.forMessage(connectionCompletedMsg);
        } else if (cause.isPartialFailure()) {
            createTime = timeStamp;
            AsyncResult.forMessage(connectionCompletedMsg, cause, null);
        } else {
            lastFailCause = cause;
            lastFailTime = timeStamp;
            AsyncResult.forMessage(connectionCompletedMsg, cause, new Exception());
        }
        if (DBG) log("notifyConnection at " + timeStamp + " cause=" + cause);

        connectionCompletedMsg.sendToTarget();
    }

    /**
     * Send ar.userObj if its a message, which is should be back to originator.
     *
     * @param dp is the DisconnectParams.
     */
    private void notifyDisconnectCompleted(DisconnectParams dp) {
        if (DBG) log("NotifyDisconnectCompleted");

        if (dp.onCompletedMsg != null) {
            Message msg = dp.onCompletedMsg;
            log(String.format("msg.what=%d msg.obj=%s",
                    msg.what, ((msg.obj instanceof String) ? (String) msg.obj : "<no-reason>")));
            AsyncResult.forMessage(msg);
            msg.sendToTarget();
        }
        if (dp.lockObj != null) {
            synchronized(dp.lockObj) {
                dp.lockObj.notify();
            }
        }

        clearSettings();
    }

    /**
     * Clear all settings called when entering mInactiveState.
     */
    protected void clearSettings() {
        if (DBG) log("clearSettings");

        this.createTime = -1;
        this.lastFailTime = -1;
        this.lastFailCause = DataConnectionFailCause.NONE;

        mDataProfile = null;
        mBearerType = null;
        interfaceName = null;
        mIpAddresses[0] = null;
        mIpAddresses[1] = null;
        mGatewayAddress[0] = null;
        mGatewayAddress[1] = null;
        dnsServers[0] = null;
        dnsServers[1] = null;
    }

    /**
     * Process setup completion.
     *
     * @param ar is the result
     * @return SetupResult.
     */
    private SetupResult onSetupConnectionCompleted(AsyncResult ar) {
        SetupResult result;

        if (ar.exception != null) {
            if (DBG) log("DataConnection Init failed " + ar.exception);

            if (ar.exception instanceof CommandException
                    && ((CommandException) (ar.exception)).getCommandError()
                    == CommandException.Error.RADIO_NOT_AVAILABLE) {
                result = SetupResult.ERR_BadCommand;
                result.mFailCause = DataConnectionFailCause.RADIO_NOT_AVAILABLE;
            } else {
                result = SetupResult.ERR_Other;
                result.mFailCause = DataConnectionFailCause.UNKNOWN;
            }
        } else {
            DataCallState response = ((ArrayList<DataCallState>)ar.result).get(0);
            ConnectionParams cp = (ConnectionParams) ar.userObj;

            DataConnectionFailCause f =
                DataConnectionFailCause.getDataCallSetupFailCause(response.status);

            if (cp.tag != mTag) {
                if (DBG) {
                    log("BUG: onSetupConnectionCompleted is stale cp.tag=" + cp.tag + ", mtag=" + mTag);
                }
                result = SetupResult.ERR_Stale;
            } else if (f != DataConnectionFailCause.NONE && f.isPartialFailure() == false) {
                /* Complete failure case */
                result = SetupResult.ERR_Other;
                result.mFailCause = f;
            } else { /* Success case or partial failure case */

//            log("onSetupConnectionCompleted received " + response.length + " response strings:");
//            for (int i = 0; i < response.length; i++) {
//                log("  response[" + i + "]='" + response[i] + "'");
//            }

                cid = response.cid;
                interfaceName = response.ifname;
                // connection is successful, so associate this dc with
                // ipversion and data profile we used to setup this
                // dataconnection with.
                mBearerType = cp.bearerType;

                /* We requested for the IPV4V6 bearer type, but we have partial failure cases
                 * to handle.
                 */
                if (mBearerType == BearerType.IPV4V6 && f.isPartialFailure() == true) {
                    if (f == DataConnectionFailCause.ONLY_IPV4_ALLOWED) {
                        mBearerType = BearerType.IP;
                    } else if (f == DataConnectionFailCause.ONLY_IPV6_ALLOWED) {
                        mBearerType = BearerType.IPV6;
                    } else if (f == DataConnectionFailCause.ONLY_SINGLE_BEARER_ALLOWED) {
                        try {
                            mBearerType = Enum.valueOf(BearerType.class, response.type);
                        } catch (Exception e) {
                            /*
                             * TODO : bearer type could be determined from the
                             * ip address, but that is more work.
                             */
                            throw new RuntimeException("unrecognized bearer type - "
                                    + response.type);
                        }
                    }
                }

                mDataProfile = cp.dp;
                if (response.addresses != null) {
                    updateIPAddresses(response.addresses);
                    updateGatewayAddresses(response.gateways);
                    if (response.dnses != null) {
                        dnsServers[0] = response.dnses.split(" ")[0];
                        if (response.dnses.split(" ").length > 1) {
                            dnsServers[1] = response.dnses.split(" ")[1];
                        }
                    }
                    if (DBG) {
                        log("interface=" + interfaceName + " ipAddress=" + response.addresses
                            + " gateway=" + response.gateways + " DNS1=" + dnsServers[0]
                            + " DNS2=" + dnsServers[1]);
                    }
                    if (isDnsOk(dnsServers)) {
                        result = SetupResult.SUCCESS;
                        result.mFailCause = f; /* report back partial failure */
                    } else {
                        result = SetupResult.ERR_BadDns;
                    }
                } else {
                    result = SetupResult.SUCCESS;
                }
            }
        }

        if (DBG) log("DataConnection setup result='" + result + "' on cid=" + cid);
        return result;
    }

    /**
     * Returns IPVersion (INET / INET6) based on the address type.
     * If invalid address, returns null
     *
     * @param addrString is the String that is either
     *  a V4 or V6.
     */
    private IPVersion getAddrType(String addrString) {
        IPVersion versiontype = null;

        if(addrString == null || addrString.length() == 0) {
            return versiontype;
        }
        try {
            InetAddress inetAddress =
                NetworkUtils.ipAddrStringToInetAddress(addrString);
            if (inetAddress instanceof Inet6Address) {
                // Ipv6 address.
                versiontype = IPVersion.INET6;
            } else if (inetAddress instanceof Inet4Address) {
                // Ipv4 address.
                versiontype = IPVersion.INET;
            }
        } catch (UnknownHostException e) {
            log(" getAddrType " + addrString + " : Exception " + e);
        }
        return versiontype;
    }

    /**
     * Updates the internal array of 'mIpAddresses' based on the
     * input string.
     * mIpAddresses[0] - IPV4 addr
     * mIpAddresses[1] - IPV6 addr
     *
     * @param addresses is the String that is sent as part of
     * UNSOL_DATA_CALL_LIST_CHANGED evt
     */
    public void updateIPAddresses(String addresses) {
        mIpAddresses = parseAddresses(addresses);
    }

    /**
     * Updates the internal array of 'mGatewayAddress' based on the
     * input string.
     * mGatewayAddress[0] - IPV4 addr
     * mGatewayAddress[1] - IPV6 addr
     *
     * @param addresses is the String that is sent as part of
     * UNSOL_DATA_CALL_LIST_CHANGED evt
     */
    public void updateGatewayAddresses(String addresses) {
        mGatewayAddress = parseAddresses(addresses);
    }

    /**
     * @param addresses space delimited list of addresses
     * @return array of addresses with val @ index 0 containing v4 address
     * and val @ index 1 containing v6 address
     *
     */
    private String[] parseAddresses(String addresses) {
        String[] addr = new String[2];
        addr[0] = addr[1] = null;

        if (addresses == null || addresses.length() == 0) {
            return addr;
        }
        String[] prefixAddr = addresses.split(" ");
        for (int i = 0; i < prefixAddr.length; i++) {
            String address = prefixAddr[i].split("/")[0];
            IPVersion ipv = getAddrType(address);
            if(null != ipv) {
                // Set the addr only if it is valid
                if(ipv == IPVersion.INET)
                    addr[0] = address;
                else if(ipv == IPVersion.INET6)
                    addr[1] = address;
            }
        }

        return addr;
    }

    /**
     * logs IP addresses.
     */
    public String ipAddrtoString() {
        return "IPV4  : " + mIpAddresses[0] + " , IPV6  : " + mIpAddresses[1];
    }

    /**
     * logs GW addresses.
     */
    public String gwAddrtoString() {
        return "IPV4  : " + mGatewayAddress[0] + " , IPV6  : " + mGatewayAddress[1];
    }

    /**
     * Returns the compare result (bit masks) of IP Addresses
     *
     * @param addresses is the new String that needs a compare with the
     * old ip addresses
     */
    public int compareAddresses(String addresses) {
        return compare(mIpAddresses, addresses);
    }

    /**
     * Returns the compare result (bit masks) of Gateway Addresses
     *
     * @param addresses is the new String that needs a compare with the
     * old GW addresses
     */
    public int compareGwAddresses(String addresses) {
        return compare(mGatewayAddress, addresses);
    }

    /**
     * Compares the String 'addresses' that is passed as an argument
     * and returns a flag (bit mask) indicating the new statuses of
     * IPV4 and IPV6 addresses
     *
     */
    private int compare(String[] addr, String addresses) {
        int resultCode = IPV_ADDR_NOCHANGE;

        if(addr.length == 0) {
            /* This happens when a data call (IPV4 or IPV6) is set up
             * successfully for the first time. The check here ensures that
             * it is not treated as a 'ip address change scenario' only
             * for the first time.
             */
            if (DBG) log("compare: No addresses");
            return resultCode;
        }

        // create a temp array of addresses and compare it against 'addr'
        String[] tempAddresses = parseAddresses(addresses);
        if (DBG) log("--- IPV4  : " + tempAddresses[0] + " , IPV6  : " + tempAddresses[1] + " ---");

        for (int i = 0; i < IPVersion.values().length; i++) {
            if(tempAddresses[i] != null && addr[i] != null) {
                if(tempAddresses[i].equals(addr[i])) {
                    if (DBG) log("compare: MATCHED : " + i);
                } else {
                    if (DBG) log("compare: UPDATED : " + i);
                    resultCode |= (i == 0) ? IPV4_ADDR_UPDATED : IPV6_ADDR_UPDATED;
                }
            } else if (tempAddresses[i] != null && addr[i] == null) {
                if (DBG) log("compare: ADDED : " + i);
                resultCode |= (i == 0) ? IPV4_ADDR_ADDED : IPV6_ADDR_ADDED;
            } else if (tempAddresses[i] == null && addr[i] != null) {
                if (DBG) log("compare: REMOVED : " + i);
                resultCode |= (i == 0) ? IPV4_ADDR_REMOVED : IPV6_ADDR_REMOVED;
            } else {
                // None --> No action needed
                if (DBG) log("compare: No action : " + i);
            }
        }
        return resultCode;
    }

    /**
     * The parent state for all other states.
     */
    private class DcDefaultState extends HierarchicalState {
        @Override
        protected boolean processMessage(Message msg) {
            AsyncResult ar;

            switch (msg.what) {
                case EVENT_RESET:
                    if (DBG) log("DcDefaultState: msg.what=EVENT_RESET");
                    clearSettings();
                    if (msg.obj != null) {
                        // Transition to inactive but send notifications after
                        // we've entered the mInactive state.
                        mInactiveState.setEnterNotificationParams((DisconnectParams) msg.obj);
                    }
                    transitionTo(mInactiveState);
                    break;

                case EVENT_CONNECT:
                    if (DBG) log("DcDefaultState: msg.what=EVENT_CONNECT, fail not expected");
                    ConnectionParams cp = (ConnectionParams) msg.obj;
                    notifyConnectCompleted(cp, DataConnectionFailCause.UNKNOWN);
                    break;

                case EVENT_DISCONNECT:
                    if (DBG) log("DcDefaultState: msg.what=EVENT_DISCONNECT");
                    notifyDisconnectCompleted((DisconnectParams) msg.obj);
                    break;

                default:
                    if (DBG) {
                        log("DcDefaultState: shouldn't happen but ignore msg.what=" + msg.what);
                    }
                    break;
            }

            return true;
        }
    }
    private DcDefaultState mDefaultState = new DcDefaultState();

    /**
     * The state machine is inactive and expects a EVENT_CONNECT.
     */
    private class DcInactiveState extends HierarchicalState {
        private ConnectionParams mConnectionParams = null;
        private DataConnectionFailCause mFailCause = null;
        private DisconnectParams mDisconnectParams = null;

        public void setEnterNotificationParams(ConnectionParams cp, DataConnectionFailCause cause) {
            log("DcInactiveState: setEnterNoticationParams cp,cause");
            mConnectionParams = cp;
            mFailCause = cause;
        }

        public void setEnterNotificationParams(DisconnectParams dp) {
          log("DcInactiveState: setEnterNoticationParams dp");
            mDisconnectParams = dp;
        }

        @Override protected void enter() {
            mTag += 1;

            /**
             * Now that we've transitioned to Inactive state we
             * can send notifications. Previously we sent the
             * notifications in the processMessage handler but
             * that caused a race condition because the synchronous
             * call to isInactive.
             */
            if ((mConnectionParams != null) && (mFailCause != null)) {
                log("DcInactiveState: enter notifyConnectCompleted");
                notifyConnectCompleted(mConnectionParams, mFailCause);
            }
            if (mDisconnectParams != null) {
              log("DcInactiveState: enter notifyDisconnectCompleted");
                notifyDisconnectCompleted(mDisconnectParams);
            }
        }

        @Override protected void exit() {
            // clear notifications
            mConnectionParams = null;
            mFailCause = null;
            mDisconnectParams = null;
        }

        @Override protected boolean processMessage(Message msg) {
            boolean retVal;

            switch (msg.what) {
                case EVENT_RESET:
                case EVENT_DEACTIVATE_DONE:
                    if (DBG) {
                        log("DcInactiveState: already reset, notify again. msg.what= " + msg.what);
                    }
                    if (msg.obj != null) {
                        notifyDisconnectCompleted((DisconnectParams) msg.obj);
                    }
                    retVal = true;
                    break;

                case EVENT_CONNECT:
                    if (DBG) log("DcInactiveState msg.what=EVENT_CONNECT");
                    ConnectionParams cp = (ConnectionParams) msg.obj;
                    cp.tag = mTag;
                    onConnect(cp);
                    transitionTo(mActivatingState);
                    retVal = true;
                    break;

                default:
                    if (DBG) log("DcInactiveState nothandled msg.what=" + msg.what);
                    retVal = false;
                    break;
            }
            return retVal;
        }
    }
    private DcInactiveState mInactiveState = new DcInactiveState();

    /**
     * The state machine is activating a connection.
     */
    private class DcActivatingState extends HierarchicalState {
        @Override protected boolean processMessage(Message msg) {
            boolean retVal;
            AsyncResult ar;
            ConnectionParams cp;

            switch (msg.what) {
                case EVENT_DISCONNECT:
                    if (DBG) log("DcActivatingState deferring msg.what=EVENT_DISCONNECT");
                    deferMessage(msg);
                    retVal = true;
                    break;

                case EVENT_SETUP_DATA_CONNECTION_DONE:
                    if (DBG) log("DcActivatingState msg.what=EVENT_SETUP_DATA_CONNECTION_DONE");

                    ar = (AsyncResult) msg.obj;
                    cp = (ConnectionParams) ar.userObj;

                    SetupResult result = onSetupConnectionCompleted(ar);
                    switch (result) {
                        case SUCCESS:
                            // All is well, with possible partial failure.
                            mActiveState.setEnterNotificationParams(cp, result.mFailCause);
                            transitionTo(mActiveState);
                            break;
                        case ERR_BadCommand:
                            // Vendor ril rejected the command and didn't connect.
                            // Transition to inactive but send notifications after
                            // we've entered the mInactive state.
                            mInactiveState.setEnterNotificationParams(cp, result.mFailCause);
                            transitionTo(mInactiveState);
                            break;
                        case ERR_BadDns:
                            // Connection succeeded but DNS info is bad so disconnect
                            EventLog.writeEvent(EventLogTags.PDP_BAD_DNS_ADDRESS, dnsServers[0]);
                            tearDownData(cp);
                            transitionTo(mDisconnectingBadDnsState);
                            break;
                        case ERR_Other:
                            mInactiveState.setEnterNotificationParams(cp, result.mFailCause);
                            transitionTo(mInactiveState);
                            break;
                        case ERR_Stale:
                            // Request is stale, ignore.
                            break;
                        default:
                            throw new RuntimeException("Unkown SetupResult, should not happen");
                    }
                    retVal = true;
                    break;

                default:
                    if (DBG) log("DcActivatingState not handled msg.what=" + msg.what);
                    retVal = false;
                    break;
            }
            return retVal;
        }
    }
    private DcActivatingState mActivatingState = new DcActivatingState();

    /**
     * The state machine is connected, expecting an EVENT_DISCONNECT.
     */
    private class DcActiveState extends HierarchicalState {
        private ConnectionParams mConnectionParams = null;
        private DataConnectionFailCause mFailCause = null;

        public void setEnterNotificationParams(ConnectionParams cp, DataConnectionFailCause cause) {
            log("DcInactiveState: setEnterNoticationParams cp,cause");
            mConnectionParams = cp;
            mFailCause = cause;
        }

        @Override public void enter() {
            /**
             * Now that we've transitioned to Active state we
             * can send notifications. Previously we sent the
             * notifications in the processMessage handler but
             * that caused a race condition because the synchronous
             * call to isActive.
             */
            if ((mConnectionParams != null) && (mFailCause != null)) {
                log("DcActiveState: enter notifyConnectCompleted");
                notifyConnectCompleted(mConnectionParams, mFailCause);
            }
        }

        @Override protected void exit() {
            // clear notifications
            mConnectionParams = null;
            mFailCause = null;
        }

        @Override protected boolean processMessage(Message msg) {
            boolean retVal;

            switch (msg.what) {
                case EVENT_QOS_ENABLE:
                case EVENT_QOS_DISABLE:
                case EVENT_QOS_GET_STATUS:
                    if (DBG) log("DcActiveState moving to DcQosActiveState msg.what="
                                + msg.what);
                    deferMessage(msg);
                    transitionTo(mQosActiveState);
                    retVal = true;
                    break;
                case EVENT_DISCONNECT:
                    if (DBG) log("DcActiveState msg.what=EVENT_DISCONNECT");
                    DisconnectParams dp = (DisconnectParams) msg.obj;
                    dp.tag = mTag;
                    tearDownData(dp);
                    transitionTo(mDisconnectingState);
                    retVal = true;
                    break;

                default:
                    if (DBG) log("DcActiveState nothandled msg.what=" + msg.what);
                    retVal = false;
                    break;
            }
            return retVal;
        }
    }
    private DcActiveState mActiveState = new DcActiveState();
    /**
     * The state machine is connected, expecting QoS requests
     */
    private class DcQosActiveState extends HierarchicalState {
        private QosSpec qosSpec = null;

        @Override
        public void enter() {
        }

        @Override
        protected void exit() {
            // clear QosSpec
        }

        @Override
        protected boolean processMessage(Message msg) {
            boolean retVal = false;
            int qosId;
            String error;

            switch (msg.what) {
            case EVENT_QOS_ENABLE:
                if (DBG)
                    log("DcQosActiveState msg.what=EVENT_QOS_ENABLE");
                // Send out qosRequest
                qosSpec = (QosSpec) msg.obj;
                onQosSetup(qosSpec);
                retVal = true;
                break;
            case EVENT_QOS_DISABLE:
                if (DBG)
                    log("DcQosActiveState msg.what=EVENT_QOS_DISABLE");
                // Send out qosRequest
                qosId = msg.arg1;
                onQosRelease(qosId);
                retVal = true;
                break;
            case EVENT_QOS_SUSPEND:
                if (DBG)
                    log("DcQosActiveState msg.what=EVENT_QOS_SUSPEND");
                // Send out qosSuspend
                qosId = msg.arg1;
                onQosSuspend(qosId);
                retVal = true;
                break;
            case EVENT_QOS_RESUME:
                if (DBG)
                    log("DcQosActiveState msg.what=EVENT_QOS_RESUME");
                // Send out qosResume
                qosId = msg.arg1;
                onQosResume(qosId);
                retVal = true;
                break;
            case EVENT_QOS_GET_STATUS:
                if (DBG)
                    log("DcQosActiveState msg.what=EVENT_QOS_GET_STATUS");
                // Send out qosRequest
                qosId = msg.arg1;
                onQosGetStatus(qosId);
                retVal = true;
                break;
            case EVENT_QOS_IND:
                log("DcQosActiveState msg.what=EVENT_QOS_IND");
                onQosStateChangedInd((AsyncResult)msg.obj);
                // If all QosSpecs are empty, go back to active.
                if (mQosFlowIds.size() == 0) {
                    transitionTo(mActiveState);
                }
                retVal = true;
                break;

            case EVENT_QOS_ENABLE_DONE:
                if (DBG)
                    log("DcQosActiveState msg.what=EVENT_QOS_ENABLE_DONE");

                error = getAsyncException(msg);
                AsyncResult ar = (AsyncResult) msg.obj;

                String responses[] = (String[])ar.result;
                int userData = (Integer) ar.userObj;
                onQosSetupDone(userData, responses, error);
                retVal = true;
                break;

            case EVENT_QOS_DISABLE_DONE:
                if (DBG)
                    log("DcQosActiveState msg.what=EVENT_QOS_DISABLE_DONE");

                error = getAsyncException(msg);

                qosId = (Integer) ((AsyncResult)msg.obj).userObj;
                onQosReleaseDone(qosId, error);
                retVal = true;
                break;

            case EVENT_QOS_SUSPEND_DONE:
                if (DBG)
                    log("DcQosActiveState msg.what=EVENT_QOS_SUSPEND_DONE");

                error = getAsyncException(msg);

                qosId = (Integer) ((AsyncResult)msg.obj).userObj;
                onQosSuspendDone(qosId, error);
                retVal = true;
                break;

            case EVENT_QOS_RESUME_DONE:
                if (DBG)
                    log("DcQosActiveState msg.what=EVENT_QOS_RESUME_DONE");

                error = getAsyncException(msg);

                qosId = (Integer) ((AsyncResult)msg.obj).userObj;
                onQosResumeDone(qosId, error);
                retVal = true;
                break;

            case EVENT_QOS_GET_STATUS_DONE:
                if (DBG)
                    log("DcQosActiveState msg.what=EVENT_QOS_GET_STATUS_DONE");

                error = getAsyncException(msg);
                qosId = (Integer) ((AsyncResult)msg.obj).userObj;
                onQosGetStatusDone(qosId, (AsyncResult)msg.obj, error);
                // If all QosSpecs are empty, go back to active.
                if (mQosFlowIds.size() == 0) {
                    transitionTo(mActiveState);
                }
                retVal = true;
                break;
            case EVENT_QOS_MODIFY_DONE:
                break;

            case EVENT_DISCONNECT:
                if (DBG) log("DcQosActiveState msg.what=EVENT_DISCONNECT");
                //Release QoS for all flows
                tearDownQos();
                deferMessage(msg);
                retVal = true;
                break;

            default:
                if (DBG)
                    log("DcQosActiveState nothandled msg.what=" + msg.what);
                retVal = false;
                break;
            }
            return retVal;
        }

        private String getAsyncException(Message msg) {
            AsyncResult ar = (AsyncResult) msg.obj;
            String ex = null;

            if (ar.exception != null) {
                if (DBG)
                    log("Error in response" + ar.result);
                ex = ar.result == null ? null : (String)ar.result;
            }
            return ex;
        }
    }
    private DcQosActiveState mQosActiveState = new DcQosActiveState();

    /**
     * The state machine is disconnecting.
     */
    private class DcDisconnectingState extends HierarchicalState {
        @Override protected boolean processMessage(Message msg) {
            boolean retVal;

            switch (msg.what) {
                case EVENT_DEACTIVATE_DONE:
                    if (DBG) log("DcDisconnectingState msg.what=EVENT_DEACTIVATE_DONE");
                    AsyncResult ar = (AsyncResult) msg.obj;
                    DisconnectParams dp = (DisconnectParams) ar.userObj;
                    if (dp.tag == mTag) {
                        // Transition to inactive but send notifications after
                        // we've entered the mInactive state.
                        mInactiveState.setEnterNotificationParams((DisconnectParams) ar.userObj);
                        transitionTo(mInactiveState);
                    } else {
                        if (DBG) log("DcDisconnectState EVENT_DEACTIVATE_DONE stale dp.tag="
                                + dp.tag + " mTag=" + mTag);
                    }
                    retVal = true;
                    break;

                default:
                    if (DBG) log("DcDisconnectingState not handled msg.what=" + msg.what);
                    retVal = false;
                    break;
            }
            return retVal;
        }
    }
    private DcDisconnectingState mDisconnectingState = new DcDisconnectingState();

    /**
     * The state machine is disconnecting after a bad dns setup
     * was found in mInactivatingState.
     */
    private class DcDisconnectingBadDnsState extends HierarchicalState {
        @Override protected boolean processMessage(Message msg) {
            boolean retVal;

            switch (msg.what) {
                case EVENT_DEACTIVATE_DONE:
                    AsyncResult ar = (AsyncResult) msg.obj;
                    ConnectionParams cp = (ConnectionParams) ar.userObj;
                    if (cp.tag == mTag) {
                        if (DBG) log("DcDisconnectingBadDnsState msg.what=EVENT_DEACTIVATE_DONE");
                        // Transition to inactive but send notifications after
                        // we've entered the mInactive state.
                        mInactiveState.setEnterNotificationParams(cp, DataConnectionFailCause.UNKNOWN);
                        transitionTo(mInactiveState);
                    } else {
                        if (DBG) log("DcDisconnectingBadDnsState EVENT_DEACTIVE_DONE stale dp.tag="
                                + cp.tag + ", mTag=" + mTag);
                    }
                    retVal = true;
                    break;

                default:
                    if (DBG) log("DcDisconnectingBadDnsState not handled msg.what=" + msg.what);
                    retVal = false;
                    break;
            }
            return retVal;
        }
    }
    private DcDisconnectingBadDnsState mDisconnectingBadDnsState = new DcDisconnectingBadDnsState();

    // ******* public interface

    /**
     * Disconnect from the network.
     *
     * @param onCompletedMsg is sent with its msg.obj as an AsyncResult object.
     *        With AsyncResult.userObj set to the original msg.obj.
     */
    public void reset(Message onCompletedMsg) {
        sendMessage(obtainMessage(EVENT_RESET, new DisconnectParams(onCompletedMsg)));
    }

    /**
     * Reset the connection and wait for it to complete.
     * TODO: Remove when all callers only need the asynchronous
     * reset defined above.
     *
     * WARNING! DataConnection object now share the same looper as
     * DataConnectionTrackers. This function SHOULD NOT be called!
     */
    public void resetSynchronously() {
        ResetSynchronouslyLock lockObj = new ResetSynchronouslyLock();
        synchronized(lockObj) {
            sendMessage(obtainMessage(EVENT_RESET, new DisconnectParams(lockObj)));
            try {
                lockObj.wait();
            } catch (InterruptedException e) {
                log("blockingReset: unexpected interrupted of wait()");
            }
        }
    }

    /**
     * Connect to the data profile and return an AsyncResult in onCompletedMsg.
     *
     * @param onCompletedMsg is sent with its msg.obj as an AsyncResult object.
     *        With AsyncResult.userObj set to the original msg.obj,
     *        AsyncResult.result = FailCause and AsyncResult.exception = Exception().
     */
    public void connect(RadioTechnology radioTech, DataProfile dp, BearerType bearerType,
            Message onCompletedMsg) {
        mCM.registerForQosStateChangedInd(getHandler(), EVENT_QOS_IND, null);

        sendMessage(obtainMessage(EVENT_CONNECT, new ConnectionParams(radioTech, dp, bearerType,
                onCompletedMsg)));
    }

    /**
     * Disconnect from the network.
     *
     * @param onCompletedMsg is sent with its msg.obj as an AsyncResult object.
     *        With AsyncResult.userObj set to the original msg.obj.
     */
    public void disconnect(Message onCompletedMsg) {
        mCM.unregisterForQosStateChangedInd(getHandler());
        sendMessage(obtainMessage(EVENT_DISCONNECT, new DisconnectParams(onCompletedMsg)));
    }

    public void qosSetup(QosSpec qosSpec) {
        sendMessage(obtainMessage(EVENT_QOS_ENABLE, qosSpec));
    }

    public void qosRelease(int qosId) {
        sendMessage(getHandler().obtainMessage(EVENT_QOS_DISABLE, qosId, 0));
    }

    public void qosModify(int qosId, QosSpec qosSpec) {
        sendMessage(getHandler().obtainMessage(EVENT_QOS_MODIFY, qosId, 0,
                qosSpec));
    }

    public void qosSuspend(int qosId) {
        sendMessage(getHandler().obtainMessage(EVENT_QOS_SUSPEND, qosId, 0));
    }

    public void qosResume(int qosId) {
        sendMessage(getHandler().obtainMessage(EVENT_QOS_RESUME, qosId, 0));
    }

    public void getQosStatus(int qosId) {
        sendMessage(getHandler().obtainMessage(EVENT_QOS_GET_STATUS, qosId, 0));
    }

    // ****** The following are used for debugging.

    /**
     * TODO: This should be an asynchronous call and we wouldn't
     * have to use handle the notification in the DcInactiveState.enter.
     *
     * @return true if the state machine is in the inactive state.
     */
    public boolean isInactive() {
        boolean retVal = getCurrentState() == mInactiveState;
        return retVal;
    }

    /**
     * Check if QoS enabled for this data call
     * @return
     */
    public boolean isQosAvailable() {
        boolean retVal = getCurrentState() == mQosActiveState;
        return retVal;
    }

    /**
     * Check if the QoS ID is valid
     * @return
     */
    public boolean isValidQos(int qosId) {
        return !mQosFlowIds.isEmpty() && mQosFlowIds.contains(qosId);
    }

    /**
     * TODO: This should be an asynchronous call and we wouldn't
     * have to use handle the notification in the DcActiveState.enter.
     *
     * @return true if the state machine is in the active state.
     */
    public boolean isActive() {
        boolean retVal = getCurrentState() == mActiveState;
        return retVal;
    }

    public DataProfile getDataProfile() {
        return mDataProfile;
    }

    public BearerType getBearerType() {
        return mBearerType;
    }

    /**
     * @return the interface name as a string.
     */
    public String getInterface() {
        return interfaceName;
    }

    /**
     * @return the ip address as a string for a given IPVersion.
     */
    public String getIpAddress(IPVersion ipv) {
        return (ipv == IPVersion.INET) ? mIpAddresses[0]: mIpAddresses[1];
    }

    /**
     * @return the gateway address as a string for a given IPVersion.
     */
    public String getGatewayAddress(IPVersion ipv) {
        return (ipv == IPVersion.INET) ? mGatewayAddress[0]: mGatewayAddress[1];
    }

    /**
     * @return an array of associated DNS addresses.
     */
    public String[] getDnsServers() {
        return dnsServers;
    }

    /**
     * @return the current state as a string.
     */
    public String getStateAsString() {
        String retVal = getCurrentState().getName();
        return retVal;
    }

    /**
     * @return the time of when this connection was created.
     */
    public long getConnectionTime() {
        return createTime;
    }

    /**
     * @return the time of the last failure.
     */
    public long getLastFailTime() {
        return lastFailTime;
    }

    /**
     * @return the last cause of failure.
     */
    public DataConnectionFailCause getLastFailCause() {
        return lastFailCause;
    }
}
