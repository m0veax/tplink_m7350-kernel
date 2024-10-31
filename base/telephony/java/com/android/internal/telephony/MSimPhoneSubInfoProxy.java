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

import android.os.ServiceManager;
import android.util.Log;
import java.lang.NullPointerException;
import java.lang.ArrayIndexOutOfBoundsException;

public class MSimPhoneSubInfoProxy extends IPhoneSubInfo.Stub {
    private static final String TAG = "MSimPhoneSubInfoProxy";
    private Phone[] mPhone;

    public MSimPhoneSubInfoProxy(Phone[] phone) {
        mPhone = phone;
        if (ServiceManager.getService("iphonesubinfo") == null) {
            ServiceManager.addService("iphonesubinfo", this);
        }
    }

    public String getDeviceId() {
        return getDeviceIdOnSubscription(getDefaultSubscription());
    }

    public String getDeviceIdOnSubscription(int subscription) {
        PhoneSubInfoProxy phoneSubInfoProxy = getPhoneSubInfoProxy(subscription);
        if (phoneSubInfoProxy != null) {
            return getPhoneSubInfoProxy(subscription).getDeviceId();
        } else {
            Log.e(TAG,"getDeviceIdOnSubscription phoneSubInfoProxy is null" +
                      " for Subscription:"+subscription);
            return null;
        }
    }

    public String getDeviceSvn() {
        return getDeviceSvnOnSubscription(getDefaultSubscription());
    }

    public String getDeviceSvnOnSubscription(int subscription) {
        PhoneSubInfoProxy phoneSubInfoProxy = getPhoneSubInfoProxy(subscription);
        if (phoneSubInfoProxy != null) {
            return getPhoneSubInfoProxy(subscription).getDeviceSvn();
        } else {
            Log.e(TAG,"getDeviceSvnOnSubscription phoneSubInfoProxy is null" +
                      " for Subscription:"+subscription);
            return null;
        }
    }

    public String getSubscriberId() {
        return getSubscriberIdOnSubscription(getDefaultSubscription());
    }

    public String getSubscriberIdOnSubscription(int subscription) {
        PhoneSubInfoProxy phoneSubInfoProxy = getPhoneSubInfoProxy(subscription);
        if (phoneSubInfoProxy != null) {
            return getPhoneSubInfoProxy(subscription).getSubscriberId();
        } else {
            Log.e(TAG,"getSubscriberIdOnSubscription phoneSubInfoProxy is" +
                      " null for Subscription:"+subscription);
            return null;
        }
    }

    public String getIccSerialNumber() {
        return getIccSerialNumberOnSubscription(getDefaultSubscription());
    }

    public String getIccSerialNumberOnSubscription(int subscription) {
        PhoneSubInfoProxy phoneSubInfoProxy = getPhoneSubInfoProxy(subscription);
        if (phoneSubInfoProxy != null) {
            return getPhoneSubInfoProxy(subscription).getIccSerialNumber();
        } else {
            Log.e(TAG,"getIccSerialNumberOnSubscription phoneSubInfoProxy is" +
                      " null for Subscription:"+subscription);
            return null;
        }
    }

    public String getLine1Number() {
        return getLine1NumberOnSubscription(getDefaultSubscription());
    }

    public String getLine1NumberOnSubscription(int subscription) {
        PhoneSubInfoProxy phoneSubInfoProxy = getPhoneSubInfoProxy(subscription);
        if (phoneSubInfoProxy != null) {
            return getPhoneSubInfoProxy(subscription).getLine1Number();
        } else {
            Log.e(TAG,"getLine1NumberOnSubscription phoneSubInfoProxy is" +
                      " null for Subscription:"+subscription);
            return null;
        }
    }

    public String getLine1AlphaTag() {
        return getLine1AlphaTagOnSubscription(getDefaultSubscription());
    }

    public String getLine1AlphaTagOnSubscription(int subscription) {
        PhoneSubInfoProxy phoneSubInfoProxy = getPhoneSubInfoProxy(subscription);
        if (phoneSubInfoProxy != null) {
            return getPhoneSubInfoProxy(subscription).getLine1AlphaTag();
        } else {
            Log.e(TAG,"getLine1AlphaTagOnSubscription phoneSubInfoProxy is" +
                      " null for Subscription:"+subscription);
            return null;
        }
    }

    public String getVoiceMailNumber() {
        return getVoiceMailNumberOnSubscription(getDefaultSubscription());
    }

    public String getVoiceMailNumberOnSubscription(int subscription) {
        PhoneSubInfoProxy phoneSubInfoProxy = getPhoneSubInfoProxy(subscription);
        if (phoneSubInfoProxy != null) {
            return getPhoneSubInfoProxy(subscription).getVoiceMailNumber();
        } else {
            Log.e(TAG,"getVoiceMailNumberOnSubscription phoneSubInfoProxy is" +
                      " null for Subscription:"+subscription);
            return null;
        }
    }

    public String getCompleteVoiceMailNumber() {
        return getCompleteVoiceMailNumberOnSubscription(getDefaultSubscription());
    }

    public String getCompleteVoiceMailNumberOnSubscription(int subscription) {
        PhoneSubInfoProxy phoneSubInfoProxy = getPhoneSubInfoProxy(subscription);
        if (phoneSubInfoProxy != null) {
            return getPhoneSubInfoProxy(subscription).getCompleteVoiceMailNumber();
        } else {
            Log.e(TAG,"getCompleteVoiceMailNumberOnSubscription phoneSubInfoProxy" +
                      " is null for Subscription:"+subscription);
            return null;
        }
    }

    public String getVoiceMailAlphaTag() {
        return getVoiceMailAlphaTagOnSubscription(getDefaultSubscription());
    }

    public String getVoiceMailAlphaTagOnSubscription(int subscription) {
        PhoneSubInfoProxy phoneSubInfoProxy = getPhoneSubInfoProxy(subscription);
        if (phoneSubInfoProxy != null) {
            return getPhoneSubInfoProxy(subscription).getVoiceMailAlphaTag();
        } else {
            Log.e(TAG,"getVoiceMailAlphaTagOnSubscription phoneSubInfoProxy is" +
                      " null for Subscription:"+subscription);
            return null;
        }
    }

    /**
     * get Phone sub info proxy object based on subscription.
     **/
    private PhoneSubInfoProxy getPhoneSubInfoProxy(int subscription) {
        try {
            return ((PhoneProxy)mPhone[subscription]).getPhoneSubInfoProxy();
        } catch (NullPointerException e) {
            Log.e(TAG, "Exception is :"+e.toString()+" For subscription :"+subscription );
            e.printStackTrace();
            return null;
        } catch (ArrayIndexOutOfBoundsException e) {
            Log.e(TAG, "Exception is :"+e.toString()+" For subscription :"+subscription );
            e.printStackTrace();
            return null;
        }
    }

    private int getDefaultSubscription() {
        return PhoneFactory.getDefaultSubscription();
    }
}
