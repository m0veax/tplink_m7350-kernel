/*
 * Copyright (C) 2011, The Linux Foundation. All rights reserved.
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

package android.net;

import com.android.internal.net.IPVersion;
import com.android.internal.telephony.ITelephony;
import com.android.internal.telephony.Phone;
import com.android.internal.telephony.TelephonyIntents;
import com.android.internal.telephony.QosSpec;
import android.net.ILinkSocketMessageHandler;
import android.net.LinkCapabilities;
import android.net.ExtraLinkCapabilities;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.util.Log;

/** @hide */
public class QoSTracker {

    private static final String LOG_TAG = "QoSTracker";
    private static final String LOCAL_TAG = "QoSTracker_DEBUG";
    private final boolean DBG = true;

    private int mId;
    private int mQosId;
    private ExtraLinkCapabilities myCap = null;
    private QosSpec mQosSpec;
    private ILinkSocketMessageHandler mNotifier;
    private boolean mSetupRequested;
    private boolean mTeardownRequested;
    private int mDetailedState; //Detail QoS State obtained from lower layers
    private String mState; //QoS State notified to the APP.
    private String lastState; //last state notified to the app.
    private boolean notifyQosToSocket; //flag to track if the socket needs to be notified
    private boolean isWaitingForSpecUpdate; //flag to track qosspec request/response
    private final String QOS_STATE_FAILED = "failed";
    private final String QOS_STATE_ACTIVE = "active";
    private final String QOS_STATE_INACTIVE = "inactive";
    private final String QOS_STATE_SUSPENDED = "suspended";

    private final int[] capRoKeys = new int[] {
        LinkCapabilities.Key.RO_MIN_AVAILABLE_FWD_BW,
        LinkCapabilities.Key.RO_MAX_AVAILABLE_FWD_BW,
        LinkCapabilities.Key.RO_MIN_AVAILABLE_REV_BW,
        LinkCapabilities.Key.RO_MAX_AVAILABLE_REV_BW,
        LinkCapabilities.Key.RO_CURRENT_FWD_LATENCY,
        LinkCapabilities.Key.RO_CURRENT_REV_LATENCY,
        LinkCapabilities.Key.RO_BOUND_INTERFACE,
        LinkCapabilities.Key.RO_NETWORK_TYPE,
        LinkCapabilities.Key.RO_PHYSICAL_INTERFACE,
        LinkCapabilities.Key.RO_CARRIER_ROLE,
        LinkCapabilities.Key.RO_QOS_STATE
    };

    //Constructor
    public QoSTracker (int id, ILinkSocketMessageHandler lsmh, ExtraLinkCapabilities eCap) {
        dlogd("socket id: " + id + " QoSTracker EX");
        mId = id;
        mNotifier = lsmh;
        myCap = eCap;
        mQosId = -1;
        mDetailedState = -1;
        mState = QOS_STATE_INACTIVE;
        lastState = QOS_STATE_INACTIVE;
        mSetupRequested = false;
        mTeardownRequested = false;
        myCap.put(LinkCapabilities.Key.RO_QOS_STATE, mState);
        isWaitingForSpecUpdate = false;
    }

    public int getSocketId() {
        return mId;
    }

    public int getQosId() {
        return mQosId;
    }

    public LinkCapabilities getQosCapabilities () {
        return myCap;
    }

    public void startQosTransaction(QosSpec spec) {
        if (spec == null) return;
        mQosSpec = spec;
        mQosSpec.setUserData(mId);
        dlogi("startQosTransaction got called for socket: " + mId);

        if (!mSetupRequested) {
            //TODO Have CNE/App decide on the APN type for QoS.
            //FIXME Hardcoding APN type for now.
            if (enableQoS(mQosSpec, Phone.APN_TYPE_DEFAULT)) {
                mSetupRequested = true;
            } else {
                //TODO implement error handlers for calls to telephony
                mSetupRequested = false;
            }
        } else {
            //TODO do modify qos
        }
    }

    public void stopQosTransaction() {
        dlogd("stopQosTransaction got called for sid: " + mId);
        if (!mTeardownRequested) {
            disableQos(mQosId);
            mTeardownRequested = true;
        }
    }

    public void handleQosEvent(int qosId, int qosIndState, int qosState, QosSpec spec) {
        mQosId = qosId;
        if (myCap == null) {
            dlogw("handleQosEvent failed due to null capabilities... aborting");
            return;
        }

        if (qosState == -1) { // event is an unsolicited indication
            handleQosIndEvent(qosIndState);
            //do not go further if a spec update is needed.
            if (isWaitingForSpecUpdate) return;
        } else { //event is a response to getQosStatus
            if (spec == null) {
                //remove current flow spec entries from the capabilities if spec is null
                myCap.remove(LinkCapabilities.Key.RO_MIN_AVAILABLE_REV_BW);
                myCap.remove(LinkCapabilities.Key.RO_MAX_AVAILABLE_REV_BW);
                myCap.remove(LinkCapabilities.Key.RO_MIN_AVAILABLE_FWD_BW);
                myCap.remove(LinkCapabilities.Key.RO_MAX_AVAILABLE_FWD_BW);
                myCap.remove(LinkCapabilities.Key.RO_CURRENT_REV_LATENCY);
                myCap.remove(LinkCapabilities.Key.RO_CURRENT_FWD_LATENCY);
            } else {
                updateCapabilitiesFromSpec(spec);
            }
            isWaitingForSpecUpdate = false; //received spec update so reset the flag
        }

        if (notifyQosToSocket) {
            ExtraLinkCapabilities sendCap = new ExtraLinkCapabilities();
            for (int roKey : capRoKeys) {
                if (myCap.containsKey(roKey))
                    sendCap.put(roKey, myCap.get(roKey));
            }
            try {
                dlogi("notifying socket of updated capabilities: " + sendCap);
                mNotifier.onCapabilitiesChanged(sendCap);
                notifyQosToSocket = false;
            } catch (RemoteException re) {
                dlogd(" oncapabilitieschanged failed for sid: "
                    + mId + " with exception: " + re);
            } catch (NullPointerException npe) {
                dlogd(" onCapabilitiesChgd got null notifier " + npe);
            }
        }
    }

    private void handleQosIndEvent(int qosIndState) {
        mDetailedState = qosIndState;

        /*
         * Convert detailed state to coarse state that will be conveyed to
         * the socket per the following table
         * ==================================================
         *  Detailed State          |       State
         * --------------------------------------------------
         *  REQUEST_FAILED          |   QOS_STATE_FAILED
         * --------------------------------------------------
         *  INITIATED, RELEASED,    |
         *  RELEASED_NETWORK,       |   QOS_STATE_INACTIVE
         *  NONE                    |
         * -------------------------|------------------------
         *  ACTIVATED,              |
         *  MODIFYING, MODIFIED,    |
         *  MODIFIED_NETWORK,       |   QOS_STATE_ACTIVE
         *  RELEASING,              |
         *  RESUMED_NETWORK,        |
         *  SUSPENDING              |
         * -------------------------|------------------------
         *  SUSPENDED,              |   QOS_STATE_SUSPENDED
         * --------------------------------------------------
         */
        switch (mDetailedState) {
            case QosSpec.QosIndStates.REQUEST_FAILED:
                mSetupRequested = false;
                notifyQosToSocket = true;
                mState = QOS_STATE_FAILED;
                break;
            case QosSpec.QosIndStates.RELEASED_NETWORK:
            case QosSpec.QosIndStates.RELEASED:
            case QosSpec.QosIndStates.NONE:
                mSetupRequested = false;
                mState = QOS_STATE_INACTIVE;
                //sometimes we need to update even if the coarse QOS_STATE does not change
                notifyQosToSocket = true;
                break;
            case QosSpec.QosIndStates.INITIATED:
                mSetupRequested = true;
                mState = QOS_STATE_INACTIVE;
                break;
            case QosSpec.QosIndStates.ACTIVATED:
            case QosSpec.QosIndStates.MODIFIED:
            case QosSpec.QosIndStates.MODIFIED_NETWORK:
            case QosSpec.QosIndStates.RESUMED_NETWORK:
                //sometimes we need to update even if the coarse QOS_STATE does not change
                notifyQosToSocket = true;
            case QosSpec.QosIndStates.MODIFYING:
            case QosSpec.QosIndStates.SUSPENDING:
            case QosSpec.QosIndStates.RELEASING:
                mState = QOS_STATE_ACTIVE;
                break;
            case QosSpec.QosIndStates.SUSPENDED:
                mState = QOS_STATE_SUSPENDED;
                break;
            default:
                dlogd("CnE got invalid qos indication: " + mDetailedState);
        }
        myCap.put(LinkCapabilities.Key.RO_QOS_STATE, mState);
        //FIXME Querying for a spec for every indication for now.
        //TODO find out for which indications the spec is expected to change
        //and query the spec for those indications only.
        isWaitingForSpecUpdate = getQos(mQosId);

        if (!mState.equals(lastState)) {
            notifyQosToSocket = true;
            lastState = mState;
        }
    }

    private void updateCapabilitiesFromSpec (QosSpec spec) {
        //Only extract flow information for bandiwdth and latency
        //FIXME hardcoding to two flows per spec, viz one fwd and reverse.
        //TODO extract flows as per qos role definition in the config file
        if (spec == null) return;
        dlogi("updateCapabilities got spec: " + spec);

        String temp = null;
        QosSpec.QosPipe txPipe = null;
        QosSpec.QosPipe rxPipe = null;

        for (QosSpec.QosPipe p : spec.getQosPipes()) {
            if (p.get(QosSpec.QosSpecKey.FLOW_DIRECTION).equals(
                  Integer.toString(QosSpec.QosDirection.QOS_TX))) txPipe = p;
            if (p.get(QosSpec.QosSpecKey.FLOW_DIRECTION).equals(
                  Integer.toString(QosSpec.QosDirection.QOS_TX))) rxPipe = p;
        }

        if (txPipe == null && rxPipe == null) {
            dlogw("updateCapabilities expected tx and rx pipes but did not find them");
            return;
        }

        if ((temp = txPipe.get(QosSpec.QosSpecKey.FLOW_DATA_RATE_MIN)) != null) {
            myCap.put(LinkCapabilities.Key.RO_MIN_AVAILABLE_REV_BW, temp);
        }
        if ((temp = txPipe.get(QosSpec.QosSpecKey.FLOW_DATA_RATE_MAX)) != null) {
            myCap.put(LinkCapabilities.Key.RO_MAX_AVAILABLE_REV_BW, temp);
        }
        if ((temp = rxPipe.get(QosSpec.QosSpecKey.FLOW_DATA_RATE_MIN)) != null) {
            myCap.put(LinkCapabilities.Key.RO_MIN_AVAILABLE_FWD_BW, temp);
        }
        if ((temp = rxPipe.get(QosSpec.QosSpecKey.FLOW_DATA_RATE_MAX)) != null) {
            myCap.put(LinkCapabilities.Key.RO_MAX_AVAILABLE_FWD_BW, temp);
        }
        if ((temp = txPipe.get(QosSpec.QosSpecKey.FLOW_LATENCY)) != null) {
            myCap.put(LinkCapabilities.Key.RO_CURRENT_REV_LATENCY, temp);
        }
        if ((temp = rxPipe.get(QosSpec.QosSpecKey.FLOW_LATENCY)) != null) {
            myCap.put(LinkCapabilities.Key.RO_CURRENT_FWD_LATENCY, temp);
        }
        dlogi("updated capabilities to: " + myCap);
    }

    //update Active Capabilities
    //TODO Move this out of here and do this when requestLink is received

    private boolean enableQoS (QosSpec spec, String apnType) {
        boolean res = false;

        if (spec.getUserData() < 1 || apnType == null) return res;
        if (spec == null) {
            dlogw( "qos spec is null");
            return res;
        }
        dlogi("requesting qos with spec: " + spec.toString()
                            + " for txId: " + spec.getUserData()
                            + " on apn: " + apnType);

        ITelephony mPhone = getPhone();
        if (mPhone == null) {
            logw("telephony service is unavailable");
            return res;
        }

        try {
            // Currently only IPv4 is supported
            res = (mPhone.enableQos(spec, apnType, IPVersion.INET.toString()) == Phone.QOS_REQUEST_SUCCESS);
        } catch (RemoteException re) {
            logw("remote exception while using telephony service: " + re);
        } catch (Exception e) {
            logw("exception while using telephony service: " + e);
        }

        return res;
    }

    private boolean disableQos (int qosId) {
        boolean res = false;

        dlogi( "disabling qos for id: " + qosId);
        ITelephony mPhone = getPhone();
        if (mPhone == null) {
            logw("telephony service is unavailable");
            return res;
        }

        try {
            res = (mPhone.disableQos(qosId) == Phone.QOS_REQUEST_SUCCESS);
        } catch (RemoteException re) {
            logw("remote exception while using telephony service: " + re);
        } catch (Exception e) {
            logw("exception while using telephony service: " + e);
        }

        return res;
    }

    private boolean getQos (int qosId) {
        boolean res = false;

        dlogi( "requesting qos spec for id: " + qosId);
        ITelephony mPhone = getPhone();
        if (mPhone == null) {
            logw("telephony service is unavailable");
            return res;
        }

        try {
            res = (mPhone.getQosStatus(qosId) == Phone.QOS_REQUEST_SUCCESS);
        } catch (RemoteException re) {
            logw("remote exception while using telephony service: " + re);
        } catch (Exception e) {
            logw("exception while using telephony service: " + e);
        }

        dlogi("getQoS returned: " + res);
        return res;
    }

    private boolean modifyQos (int qosId, QosSpec spec) {
        boolean res = false;

        if (spec == null) {
            dlogw( "qos spec is null");
            return res;
        }
        dlogi( "modifying qos spec for id: " + qosId);
        ITelephony mPhone = getPhone();
        if (mPhone == null) {
            logw("telephony service is unavailable");
            return res;
        }

        try {
            res = (mPhone.modifyQos(qosId, spec) == Phone.QOS_REQUEST_SUCCESS);
        } catch (RemoteException re) {
            logw("remote exception while using telephony service: " + re);
        } catch (Exception e) {
            logw("exception while using telephony service: " + e);
        }

        return res;
    }

    private boolean suspendQos (int qosId) {
        boolean res = false;

        dlogi( "suspending qos for id: " + qosId);
        ITelephony mPhone = getPhone();
        if (mPhone == null) {
            logw("telephony service is unavailable");
            return res;
        }

        try {
            res = (mPhone.suspendQos(qosId) == Phone.QOS_REQUEST_SUCCESS);
        } catch (RemoteException re) {
            logw("remote exception while using telephony service: " + re);
        } catch (Exception e) {
            logw("exception while using telephony service: " + e);
        }

        return res;
    }

    private boolean resumeQos (int qosId) {
        boolean res = false;

        dlogi( "resuming qos for id: " + qosId);
        ITelephony mPhone = getPhone();
        if (mPhone == null) {
            logw("telephony service is unavailable");
            return res;
        }

        try {
            res = (mPhone.resumeQos(qosId) == Phone.QOS_REQUEST_SUCCESS);
        } catch (RemoteException re) {
            logw("remote exception while using telephony service: " + re);
        } catch (Exception e) {
            logw("exception while using telephony service: " + e);
        }

        return res;
    }

    private ITelephony getPhone() {
      return ITelephony.Stub.asInterface(ServiceManager.getService("phone"));
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

    /* logging macros */
    private void logd (String s) {
        Log.d(LOG_TAG,s);
    }
    private void loge (String s) {
        Log.e(LOG_TAG,s);
    }
    private void logw (String s) {
        Log.w(LOG_TAG,s);
    }
    private void logv (String s) {
        Log.v(LOG_TAG,s);
    }
    private void logi (String s) {
        Log.i(LOG_TAG,s);
    }

    private void dlogd (String s) {
        if (DBG) Log.d(LOCAL_TAG,s);
    }
    private void dloge (String s) {
        if (DBG) Log.e(LOCAL_TAG,s);
    }
    private void dlogw (String s) {
        if (DBG) Log.w(LOCAL_TAG,s);
    }
    private void dlogv (String s) {
        if (DBG) Log.v(LOCAL_TAG,s);
    }
    private void dlogi (String s) {
        if (DBG) Log.i(LOCAL_TAG,s);
    }
}
