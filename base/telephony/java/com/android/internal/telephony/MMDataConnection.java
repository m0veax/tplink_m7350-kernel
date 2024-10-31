/*
 * Copyright (C) 2006 The Android Open Source Project
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


import android.content.Context;
import android.content.Intent;
import android.os.AsyncResult;
import android.os.SystemProperties;
import android.util.Log;
import android.util.Patterns;

import java.util.Arrays;

import com.android.internal.telephony.DataProfile.DataProfileType;
import com.android.internal.telephony.QosSpec;

/**
 * {@hide}
 */
public class MMDataConnection extends DataConnection {

    boolean DBG = true;
    DataConnectionTracker mDct;

    /*
     * Set to true, if this instance of DC is available for use by DCT, false
     * other wise.
     */
    private boolean isAvailable = true;

    private boolean mOmhEnabled = SystemProperties.getBoolean(
            TelephonyProperties.PROPERTY_OMH_ENABLED, false);

    private static final String LOG_TAG = "DATA";

    // Send a unicast intent to ConnectivityService
    private class QosIndication {
        Intent mIntent = new Intent(TelephonyIntents.ACTION_QOS_STATE_IND);

        QosIndication() {
            // If QoS indications are to be unicast, set the class name.
            // TODO: QOS: This system property is useful when using test tools
            // to perform direct testing of QoS using Android Telephony.
            // This can be removed if/when CNE based testing tools are developed
            if (SystemProperties.getBoolean("persist.telephony.qosUnicast", false)) {
                mIntent.setClassName("com.android.server",
                        "com.android.server.LinkManager");
            }
        }

        void setIndState(int state, String error) {
            if (error != null) {
                // mIntent.putExtra(QosSpec.QosIntentKeys.QOS_ERROR, error);
                state = QosSpec.QosIndStates.REQUEST_FAILED;
            }
            mIntent.putExtra(QosSpec.QosIntentKeys.QOS_INDICATION_STATE, state);
        }

        void setQosState(int state) {
            mIntent.putExtra(QosSpec.QosIntentKeys.QOS_STATUS, state);
        }

        void setUserData(int userData) {
            mIntent.putExtra(QosSpec.QosIntentKeys.QOS_USERDATA, userData);
        }

        void setQosId(int qosId) {
            mIntent.putExtra(QosSpec.QosIntentKeys.QOS_ID, qosId);
        }

        void setQosSpec(QosSpec spec) {
            mIntent.putExtra(QosSpec.QosIntentKeys.QOS_SPEC, spec);
        }

        void sendIndication() {
            mContext.sendBroadcast(mIntent);
        }
    }

    private MMDataConnection(DataConnectionTracker dct, Context context,
            CommandsInterface ci, String name) {
        super(context, ci, name);
        this.mDct = dct;
    }

    static MMDataConnection makeDataConnection(DataConnectionTracker dct) {
        synchronized (mCountLock) {
            mCount += 1;
        }
        MMDataConnection dc = new MMDataConnection(dct, dct.mContext, dct.mCm,
                "MMDC -" + mCount);
        dc.start();
        return dc;
    }

    /**
     * Setup a data call with the specified data profile
     *
     * @param mDataProfile
     *            for this connection.
     * @param onCompleted
     *            notify success or not after down
     */
    protected void onConnect(ConnectionParams cp) {

        logi("Connecting : dataProfile = " + cp.dp.toString());

        int radioTech = cp.radioTech.isCdma() ? 0 : 1;

        /* case APN */
        if (cp.dp.getDataProfileType() == DataProfileType.PROFILE_TYPE_3GPP_APN) {
            ApnSetting apn = (ApnSetting) cp.dp;

            setHttpProxy(apn.proxy, apn.port);

            int authType = apn.authType;
            if (authType == -1) {
                authType = (apn.user != null) ? RILConstants.SETUP_DATA_AUTH_PAP_CHAP
                        : RILConstants.SETUP_DATA_AUTH_NONE;
            }
            this.mCM.setupDataCall(Integer.toString(radioTech),
                    Integer.toString(0), apn.apn, apn.user, apn.password,
                    Integer.toString(authType), cp.bearerType.toString(),
                    obtainMessage(EVENT_SETUP_DATA_CONNECTION_DONE, cp));
        } else if (cp.dp.getDataProfileType() == DataProfileType.PROFILE_TYPE_3GPP2_NAI) {
            this.mCM.setupDataCall(Integer.toString(radioTech),
                    Integer.toString(0), null, null, null,
                    Integer.toString(RILConstants.SETUP_DATA_AUTH_PAP_CHAP),
                    cp.bearerType.toString(),
                    obtainMessage(EVENT_SETUP_DATA_CONNECTION_DONE, cp));
        } else if (cp.dp.getDataProfileType() == DataProfileType.PROFILE_TYPE_3GPP2_OMH) {
            if (mOmhEnabled) {
                DataProfileOmh dp = (DataProfileOmh) cp.dp;

                // Offset by DATA_PROFILE_OEM_BASE as this is modem provided
                // profile id
                String profileId = Integer.toString(dp.getProfileId()
                        + RILConstants.DATA_PROFILE_OEM_BASE);
                logd("OMH profileId:" + profileId);
                this.mCM.setupDataCall(
                        Integer.toString(radioTech),
                        profileId,
                        null,
                        null,
                        null,
                        Integer.toString(RILConstants.SETUP_DATA_AUTH_PAP_CHAP),
                        cp.bearerType.toString(),
                        obtainMessage(EVENT_SETUP_DATA_CONNECTION_DONE, cp));
            }
        }
    }

    /**
     * Initiate QoS Setup with the given parameters
     *
     * @param qosSpec
     *            QoS Spec
     */
    protected void onQosSetup(QosSpec qosSpec) {
        logd("Requesting QoS Setup. QosSpec:" + qosSpec.toString());
        this.mCM.setupQosReq(cid, qosSpec.getRilQosSpec(),
                obtainMessage(EVENT_QOS_ENABLE_DONE, qosSpec.getUserData()));
    }

    /**
     * Initiate QoS Release for the given QoS ID
     *
     * @param qosId
     *            QoS ID
     */
    protected void onQosRelease(int qosId) {
        logd("Requesting QoS Release, qosId" + qosId);

        this.mCM.releaseQos(qosId, obtainMessage(EVENT_QOS_DISABLE_DONE, qosId));
    }

    /**
     * Initiate QoS Suspend for the given QoS ID
     *
     * @param qosId
     *            QoS ID
     */
    protected void onQosSuspend(int qosId) {
        logd("Requesting QoS Suspend, qosId" + qosId);

        this.mCM.suspendQos(qosId, obtainMessage(EVENT_QOS_SUSPEND_DONE, qosId));
    }

    /**
     * Initiate QoS Resume for the given QoS ID
     *
     * @param qosId
     *            QoS ID
     */
    protected void onQosResume(int qosId) {
        logd("Requesting QoS Resume, qosId" + qosId);

        this.mCM.resumeQos(qosId, obtainMessage(EVENT_QOS_RESUME_DONE, qosId));
    }

    /**
     * Get QoS status and parameters for a given QoS ID
     *
     * @param qosId
     *            QoS ID
     */
    protected void onQosGetStatus(int qosId) {
        logd("Get QoS Status, qosId:" + qosId);

        this.mCM.getQosStatus(qosId, obtainMessage(EVENT_QOS_GET_STATUS_DONE, qosId));
    }

    /**
     * QoS Setup is complete. Notify upper layers
     *
     * @param userData
     *            User Data recieved in the asynchronous response
     * @param responses
     *            Fields from the QoS Setup Response
     * @param error
     *            error string
     */
    protected void onQosSetupDone(int userData, String[] responses, String error) {
        boolean failure = false;
        int state = QosSpec.QosIndStates.REQUEST_FAILED;

        QosIndication ind = new QosIndication();
        ind.setUserData(userData);

        try {
            // non zero response is a failure
            if (responses[0].equals("0")) {
                ind.setQosId(Integer.parseInt(responses[1]));
                mQosFlowIds.add(Integer.parseInt(responses[1]));
                logd("Added QosId:" + Integer.parseInt(responses[1])
                        + " to DC:" + cid + " QoS Flow Count:" + mQosFlowIds.size());
            }
            else
                failure = true;
        } catch (Exception e) {
            // The responses are parsed for expected responses/strings
            // Any null parameter would be a failure.
            loge("onQosSetupDone: Exception" + e);
            failure = true;
        }

        if (!failure && error == null)
            state = QosSpec.QosIndStates.INITIATED;

        ind.setIndState(state, error);
        ind.sendIndication();

        logd("onQosSetupDone Complete, userData:" + userData + " error:" + error);
    }

    /**
     * QoS Release Done. Notify upper layers.
     *
     * @param error
     */
    protected void onQosReleaseDone(int qosId, String error) {

        if (mQosFlowIds.contains(qosId)) {
            QosIndication ind = new QosIndication();
            ind.setIndState(QosSpec.QosIndStates.RELEASING, error);
            ind.setQosId(qosId);
            ind.sendIndication();

            mQosFlowIds.remove(mQosFlowIds.indexOf(qosId));

            logd("onQosReleaseDone Complete, qosId:" + qosId
                    + " error:" + error + " QoS Flow Count:" + mQosFlowIds.size());
        }
        else
            logd("onQosReleaseDone Invalid qosId:" + qosId + " error:" + error);
    }

    /**
     * QoS Suspend Done. Notify upper layers.
     *
     * @param error
     */
    protected void onQosSuspendDone(int qosId, String error) {

        if (mQosFlowIds.contains(qosId)) {
            QosIndication ind = new QosIndication();
            ind.setIndState(QosSpec.QosIndStates.SUSPENDING, error);
            ind.setQosId(qosId);
            ind.sendIndication();

            logd("onQosSuspendDone Complete, qosId:" + qosId
                    + " error:" + error);
        }
        else
            logd("onQosSuspendDone Invalid qosId:" + qosId + " error:" + error);
    }

    /**
     * QoS Resume Done. Notify upper layers.
     *
     * @param error
     */
    protected void onQosResumeDone(int qosId, String error) {

        if (mQosFlowIds.contains(qosId)) {
            QosIndication ind = new QosIndication();
            ind.setIndState(QosSpec.QosIndStates.RESUMING, error);
            ind.setQosId(qosId);
            ind.sendIndication();

            logd("onQosResumeDone Complete, qosId:" + qosId
                    + " error:" + error);
        }
        else
            logd("onQosResumeDone Invalid qosId:" + qosId + " error:" + error);
    }

    /**
     * QoS Get Status Done. Notify upper layers.
     *
     * @param ar
     */
    protected void onQosGetStatusDone(int qosId, AsyncResult ar, String error) {
        String qosStatusResp[] = (String[])ar.result;
        QosSpec spec = null;
        int qosStatus = QosSpec.QosStatus.NONE;
        int status = QosSpec.QosIndStates.REQUEST_FAILED;

        if (qosStatusResp != null && qosStatusResp.length >= 2) {
            logd("Entire Status Msg:" + Arrays.toString(qosStatusResp));

            // Process status for valid QoS status and QoS ID
            if (isValidQos(qosId) && (qosStatusResp[1] != null)) {
                qosStatus = Integer.parseInt(qosStatusResp[1]);

                switch (qosStatus) {
                    case QosSpec.QosStatus.NONE:
                        status = QosSpec.QosIndStates.NONE;
                        break;
                    case QosSpec.QosStatus.ACTIVATED:
                    case QosSpec.QosStatus.SUSPENDED:
                        status = QosSpec.QosIndStates.ACTIVATED;
                        break;
                    default:
                        loge("Invalid qosStatus:" + qosStatus);
                        break;
                }

                if (qosStatusResp.length > 2) {
                    // There are QoS flow/filter specs, create QoS Spec object
                    spec = new QosSpec();

                    for (int i = 2; i < qosStatusResp.length; i++)
                        spec.createPipe(qosStatusResp[i]);

                    logd("QoS Spec for upper layers:" + spec.toString());
                }
            }
        } else {
            loge("Invalid Qos Status message, too few arguments");
        }

        // send an indication
        QosIndication ind = new QosIndication();
        ind.setQosId(qosId);
        ind.setIndState(status, error);
        ind.setQosState(qosStatus);
        ind.setQosSpec(spec);
        ind.sendIndication();
    }

    /**
     * Handler for all QoS indications
     */
    protected void onQosStateChangedInd(AsyncResult ar) {
        String qosInd[] = (String[])ar.result;
        int qosIndState = QosSpec.QosIndStates.REQUEST_FAILED;

        if (qosInd == null || qosInd.length != 2) {
            // Invalid QoS Indication, ignore it
            loge("Invalid Qos State Changed Ind:" + ar.result);
            return;
        }

        logd("onQosStateChangedInd: qosId:" + qosInd[0] + ":" + qosInd[1]);

        QosIndication ind = new QosIndication();

        try {
            ind.setQosId(Integer.parseInt(qosInd[0]));

            // Converting RIL's definition of QoS state into the one defined in QosSpec
            int qosState = Integer.parseInt(qosInd[1]);

            switch(qosState) {
                case RILConstants.RIL_QosIndStates.RIL_QOS_ACTIVATED:
                    qosIndState = QosSpec.QosIndStates.ACTIVATED;
                    break;
                case RILConstants.RIL_QosIndStates.RIL_QOS_USER_RELEASE:
                    qosIndState = QosSpec.QosIndStates.RELEASED;
                    break;
                case RILConstants.RIL_QosIndStates.RIL_QOS_NETWORK_RELEASE:
                    qosIndState = QosSpec.QosIndStates.RELEASED_NETWORK;
                    break;
                case RILConstants.RIL_QosIndStates.RIL_QOS_SUSPENDED:
                    qosIndState = QosSpec.QosIndStates.SUSPENDED;
                    break;
                case RILConstants.RIL_QosIndStates.RIL_QOS_MODIFIED:
                    qosIndState = QosSpec.QosIndStates.MODIFIED;
                    break;
                default:
                    loge("Invalid Qos State, ignoring indication!");
                    break;
            }
        } catch (Exception e) {
            logd("Exception processing indication:" + e);
        }

        ind.setIndState(qosIndState, null);
        ind.sendIndication();
    }

    private void setHttpProxy(String httpProxy, String httpPort) {
        if (httpProxy == null || httpProxy.length() == 0) {
            SystemProperties.set("net.gprs.http-proxy", null);
            return;
        }

        if (httpPort == null || httpPort.length() == 0) {
            httpPort = "8080"; // Default to port 8080
        }

        SystemProperties.set("net.gprs.http-proxy", "http://" + httpProxy + ":"
                + httpPort + "/");
    }

    @Override
    protected boolean isDnsOk(String[] domainNameServers) {
        if (NULL_IP.equals(dnsServers[0]) && NULL_IP.equals(dnsServers[1])
                && !mDct.isDnsCheckDisabled()) {
            // Work around a race condition where QMI does not fill in DNS:
            // Deactivate PDP and let DataConnectionTracker retry.
            // Do not apply the race condition workaround for MMS APN
            // if Proxy is an IP-address.
            // Otherwise, the default APN will not be restored anymore.
            if (!(mDataProfile.getDataProfileType() == DataProfileType.PROFILE_TYPE_3GPP_APN
                    && mDataProfile
                            .canHandleServiceType(DataServiceType.SERVICE_TYPE_MMS)
                    && isIpAddress(((ApnSetting) mDataProfile).mmsProxy))) {
                return false;
            }
        }
        return true;
    }

    /* TODO: Fix this function - also add support for IPV6 */
    private boolean isIpAddress(String address) {
        if (address == null)
            return false;

        return Patterns.IP_ADDRESS
                .matcher(((ApnSetting) mDataProfile).mmsProxy).matches();
    }

    /*
     * Set to true to mark the DC as in Use and set to false to mark the dc as
     * available for use
     */
    void setAvailable(boolean b) {
        isAvailable = b;
    }

    boolean isAvailable() {
        return isAvailable;
    }

    public void update(CommandsInterface ci) {
        // Update the commands interface object.
        this.mCM = ci;
        // Call reset to make sure those are in inactive state.
        reset(null);
    }

    void logd(String logString) {
        if (DBG) {
            Log.d(LOG_TAG, "[DC cid = " + cid + "]" + logString);
        }
    }

    void logv(String logString) {
        if (DBG) {
            Log.d(LOG_TAG, "[DC cid = " + cid + "]" + logString);
        }
    }

    void logi(String logString) {
        Log.i(LOG_TAG, "[DC cid = " + cid + "]" + logString);
    }

    void loge(String logString) {
        Log.e(LOG_TAG, "[DC cid = " + cid + "]" + logString);
    }

    public String toString() {
        return "Cid=" + cid + ", State=" + getStateAsString() + ", bearerType="
                + mBearerType + ", create=" + createTime + ", lastFail="
                + lastFailTime + ", lastFailCause=" + lastFailCause + ", dp="
                + mDataProfile;
    }

    @Override
    protected void log(String s) {
        logv(s);
    }
}
