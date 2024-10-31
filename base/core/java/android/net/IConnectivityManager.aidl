/**
 * Copyright (c) 2008, The Android Open Source Project
 * Copyright (C) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.net;

import android.net.LinkCapabilities;
import android.net.LinkInfo;
import android.net.NetworkInfo;
import android.os.Bundle;
import android.os.IBinder;

/**
 * Interface that answers queries about, and allows changing, the
 * state of network connectivity.
 */
/** {@hide} */
interface IConnectivityManager
{
    void setNetworkPreference(int pref);

    int getNetworkPreference();

    NetworkInfo getActiveNetworkInfo();

    NetworkInfo getNetworkInfo(int networkType);

    NetworkInfo[] getAllNetworkInfo();

    boolean setRadios(boolean onOff);

    boolean setRadio(int networkType, boolean turnOn);

    int startUsingNetworkFeature(int networkType, in String feature,
            in IBinder binder);

    int stopUsingNetworkFeature(int networkType, in String feature);

    boolean requestRouteToHost(int networkType, int hostAddress);

    boolean requestRouteToHostAddress(int networkType, in String hostAddress);

    boolean getBackgroundDataSetting();

    void setBackgroundDataSetting(boolean allowBackgroundData);

    boolean getMobileDataEnabled();

    void setMobileDataEnabled(boolean enabled);

    int tether(String iface);

    int untether(String iface);

    int getLastTetherError(String iface);

    boolean isTetheringSupported();

    String[] getTetherableIfaces();

    String[] getTetheredIfaces();

    String[] getTetheringErroredIfaces();

    String[] getTetherableUsbRegexs();

    String[] getTetherableWifiRegexs();

    void reportInetCondition(int networkType, int percentage);
    /* LinkSocket */

    int requestLink(in LinkCapabilities capabilities, IBinder callback);

    void releaseLink(int id);

    LinkCapabilities requestCapabilities(int id, in int[] capability_keys);

    boolean requestQoS(int id, int localPort, String localAddress);

    void setTrackedCapabilities(int id, in int[] capabilities);

    /* LinkProvider */

    boolean getLink_LP(int role, in Map linkReqs, int mPid, IBinder listener);

    boolean reportLinkSatisfaction_LP(int role, int mPid, in LinkInfo info, boolean isSatisfied,
            boolean isNotifyBetterCon);

    boolean releaseLink_LP(int role,int mPid);

    boolean switchLink_LP(int role, int mPid, in LinkInfo info, boolean isSwitch);

    boolean rejectSwitch_LP(int role, int mPid, in LinkInfo info, boolean isSwitch);

    /* FMC */

    boolean startFmc(IBinder listener);

    boolean stopFmc(IBinder listener);

    boolean getFmcStatus(IBinder listener);
}
