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

import android.app.PendingIntent;
import android.util.Log;
import android.os.ServiceManager;

import java.util.ArrayList;
import java.util.List;

/**
 * MSimIccSmsInterfaceManager to provide an inter-process communication to
 * access Sms in Icc.
 */
public class MSimIccSmsInterfaceManager extends ISms.Stub {
    static final String LOG_TAG = "RIL_MSimIccSms";

    protected Phone[] mPhone;

    protected MSimIccSmsInterfaceManager(Phone[] phone){
        mPhone = phone;

        if (ServiceManager.getService("isms") == null) {
            ServiceManager.addService("isms", this);
        }
    }

    protected void enforceReceiveAndSend(String message) {
        enforceReceiveAndSendOnSubscription(message, getPreferredSmsSubscription());
    }

    protected void enforceReceiveAndSendOnSubscription(String message, int subscription) {
        IccSmsInterfaceManager iccSmsIntMgr = getIccSmsInterfaceManager(subscription);
        if (iccSmsIntMgr != null) {
            iccSmsIntMgr.enforceReceiveAndSend(message);
        } else {
            Log.e(LOG_TAG,"enforceReceiveAndSendOnSubscription iccSmsIntMgr is null" +
                          " for Subscription:"+subscription);
        }
    }

    public boolean
    updateMessageOnIccEf(int index, int status, byte[] pdu) {
        return updateMessageOnIccEfOnSubscription(index, status, pdu, getPreferredSmsSubscription());
    }

    public boolean
    updateMessageOnIccEfOnSubscription(int index, int status, byte[] pdu, int subscription) {
        IccSmsInterfaceManager iccSmsIntMgr = getIccSmsInterfaceManager(subscription);
        if (iccSmsIntMgr != null) {
            return iccSmsIntMgr.updateMessageOnIccEf(index, status, pdu);
        } else {
            Log.e(LOG_TAG,"updateMessageOnIccEfOnSubscription iccSmsIntMgr is null" +
                          " for Subscription:"+subscription);
            return false;
        }
    }

    public boolean copyMessageToIccEf(int status, byte[] pdu, byte[] smsc) {
        return copyMessageToIccEfOnSubscription(status, pdu, smsc, getPreferredSmsSubscription());
    }

    public boolean copyMessageToIccEfOnSubscription(int status, byte[] pdu, byte[] smsc, int subscription) {
        IccSmsInterfaceManager iccSmsIntMgr = getIccSmsInterfaceManager(subscription);
        if (iccSmsIntMgr != null) {
            return iccSmsIntMgr.copyMessageToIccEf(status, pdu, smsc);
        } else {
            Log.e(LOG_TAG,"copyMessageToIccEfOnSubscription iccSmsIntMgr is null" +
                          " for Subscription:"+subscription);
            return false;
        }
    }

    public List<SmsRawData> getAllMessagesFromIccEf() {
        return getAllMessagesFromIccEfOnSubscription(getPreferredSmsSubscription());
    }

    public List<SmsRawData> getAllMessagesFromIccEfOnSubscription(int subscription) {
        IccSmsInterfaceManager iccSmsIntMgr = getIccSmsInterfaceManager(subscription);
        if (iccSmsIntMgr != null) {
            return iccSmsIntMgr.getAllMessagesFromIccEf();
        } else {
            Log.e(LOG_TAG,"getAllMessagesFromIccEfOnSubscription iccSmsIntMgr is" +
                          " null for Subscription:"+subscription);
            return null;
        }
    }

    public void sendData(String destAddr, String scAddr, int destPort,
            byte[] data, PendingIntent sentIntent, PendingIntent deliveryIntent) {
        sendDataOnSubscription(destAddr, scAddr, destPort, data, sentIntent,
            deliveryIntent, getPreferredSmsSubscription());
    }

    public void sendDataOnSubscription(String destAddr, String scAddr, int destPort,
            byte[] data, PendingIntent sentIntent, PendingIntent deliveryIntent, int subscription) {
        IccSmsInterfaceManager iccSmsIntMgr = getIccSmsInterfaceManager(subscription);
        if (iccSmsIntMgr != null) {
            iccSmsIntMgr.sendData(destAddr, scAddr, destPort, data, sentIntent, deliveryIntent);
        } else {
            Log.e(LOG_TAG,"sendDataOnSubscription iccSmsIntMgr is null for Subscription:"+subscription);
        }
    }

    public void sendText(String destAddr, String scAddr,
            String text, PendingIntent sentIntent, PendingIntent deliveryIntent) {
        sendTextOnSubscription(destAddr, scAddr, text, sentIntent, deliveryIntent,
            getPreferredSmsSubscription());
    }
    public void sendTextOnSubscription(String destAddr, String scAddr,
            String text, PendingIntent sentIntent, PendingIntent deliveryIntent, int subscription) {
        IccSmsInterfaceManager iccSmsIntMgr = getIccSmsInterfaceManager(subscription);
        if (iccSmsIntMgr != null) {
            iccSmsIntMgr.sendText(destAddr, scAddr, text, sentIntent, deliveryIntent);
        } else {
            Log.e(LOG_TAG,"sendTextOnSubscription iccSmsIntMgr is null for" +
                          " Subscription:"+subscription);
        }
    }

    public void sendMultipartText(String destAddr, String scAddr, List<String> parts,
            List<PendingIntent> sentIntents, List<PendingIntent> deliveryIntents) {
        sendMultipartTextOnSubscription(destAddr, scAddr, (ArrayList<String>) parts,
            (ArrayList<PendingIntent>) sentIntents, (ArrayList<PendingIntent>) deliveryIntents,
                getPreferredSmsSubscription());
    }
    public void sendMultipartTextOnSubscription(String destAddr, String scAddr, List<String> parts,
            List<PendingIntent> sentIntents, List<PendingIntent> deliveryIntents, int subscription) {
        IccSmsInterfaceManager iccSmsIntMgr = getIccSmsInterfaceManager(subscription);
        if (iccSmsIntMgr != null ) {
            iccSmsIntMgr.sendMultipartText(destAddr, scAddr, parts, sentIntents, deliveryIntents);
        } else {
            Log.e(LOG_TAG,"sendMultipartTextOnSubscription iccSmsIntMgr is null for" +
                          " Subscription:"+subscription);
        }
    }

    public boolean enableCellBroadcast(int messageIdentifier) {
        return enableCellBroadcastOnSubscription(messageIdentifier, getPreferredSmsSubscription());
    }

    public boolean enableCellBroadcastRangeOnSubscription(int startMessageId, int endMessageId, int subscription) {
        IccSmsInterfaceManager iccSmsIntMgr = getIccSmsInterfaceManager(subscription);
        if (iccSmsIntMgr != null ) {
            return iccSmsIntMgr.enableCellBroadcastRange(startMessageId, endMessageId);
        } else {
            Log.e(LOG_TAG,"enableCellBroadcast iccSmsIntMgr is null for" +
                          " Subscription:"+subscription);
        }
        return false;
    }

    public boolean enableCellBroadcastRange(int startMessageId, int endMessageId) {
        return enableCellBroadcastRangeOnSubscription(startMessageId, endMessageId, getPreferredSmsSubscription());
    }

    public boolean enableCellBroadcastOnSubscription(int messageIdentifier, int subscription) {
        return enableCellBroadcastRangeOnSubscription(messageIdentifier, messageIdentifier, subscription);
    }

    public boolean disableCellBroadcast(int messageIdentifier) {
        return disableCellBroadcastOnSubscription(messageIdentifier, getPreferredSmsSubscription());
    }

    public boolean disableCellBroadcastOnSubscription(int messageIdentifier, int subscription) {
        return disableCellBroadcastRangeOnSubscription(messageIdentifier, messageIdentifier, subscription);
    }

    public boolean disableCellBroadcastRange(int startMessageId, int endMessageId) {
        return disableCellBroadcastRangeOnSubscription(startMessageId, endMessageId, getPreferredSmsSubscription());
    }

    public boolean disableCellBroadcastRangeOnSubscription(int startMessageId, int endMessageId, int subscription) {
        IccSmsInterfaceManager iccSmsIntMgr = getIccSmsInterfaceManager(subscription);
        if (iccSmsIntMgr != null ) {
            return iccSmsIntMgr.disableCellBroadcastRange(startMessageId, endMessageId);
        } else {
            Log.e(LOG_TAG,"disableCellBroadcast iccSmsIntMgr is null for" +
                          " Subscription:"+subscription);
        }
       return false;
    }

    public boolean enableCdmaBroadcast(int messageIdentifier) {
        return enableCdmaBroadcast(messageIdentifier, getPreferredSmsSubscription());
    }

    public boolean enableCdmaBroadcast(int messageIdentifier, int subscription) {
        IccSmsInterfaceManager iccSmsIntMgr = getIccSmsInterfaceManager(subscription);
        if (iccSmsIntMgr != null ) {
            return iccSmsIntMgr.enableCdmaBroadcast(messageIdentifier);
        } else {
            Log.e(LOG_TAG,"enableCdmaBroadcast iccSmsIntMgr is null for" +
                          " Subscription:"+subscription);
        }
        return false;
    }

    public boolean disableCdmaBroadcast(int messageIdentifier) {
        return disableCdmaBroadcast(messageIdentifier, getPreferredSmsSubscription());
    }

    public boolean disableCdmaBroadcast(int messageIdentifier, int subscription) {
        IccSmsInterfaceManager iccSmsIntMgr = getIccSmsInterfaceManager(subscription);
        if (iccSmsIntMgr != null ) {
            return iccSmsIntMgr.disableCdmaBroadcast(messageIdentifier);
        } else {
            Log.e(LOG_TAG,"disableCdmaBroadcast iccSmsIntMgr is null for" +
                          " Subscription:"+subscription);
        }
       return false;
    }

    /**
     * get sms interface manager object based on subscription.
     **/
    private IccSmsInterfaceManager getIccSmsInterfaceManager(int subscription) {
        try {
            return ((PhoneProxy)mPhone[subscription]).getIccSmsInterfaceManager();
        } catch (NullPointerException e) {
            Log.e(LOG_TAG, "Exception is :"+e.toString()+" For subscription :"+subscription );
            e.printStackTrace(); //This will print stact trace
            return null;
        } catch (ArrayIndexOutOfBoundsException e) {
            Log.e(LOG_TAG, "Exception is :"+e.toString()+" For subscription :"+subscription );
            e.printStackTrace(); //This will print stack trace
            return null;
        }
    }

    /**
       Gets User preferred SMS subscription */
    public int getPreferredSmsSubscription() {
        return PhoneFactory.getSMSSubscription();
    }
}
