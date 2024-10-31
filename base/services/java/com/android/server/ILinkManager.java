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

import android.net.LinkCapabilities;
import android.net.LinkInfo;
import android.net.LinkProvider.LinkRequirement;
import android.os.IBinder;

import java.util.Map;

interface ILinkManager {

    /* CNE feature flag */
    static final String UseCne = "persist.cne.UseCne";

    /* LinkSocket */
    int requestLink(LinkCapabilities capabilities, IBinder binder);

    void releaseLink(int id);

    String getMinAvailableForwardBandwidth(int id);

    String getMaxAvailableForwardBandwidth(int id);

    String getMinAvailableReverseBandwidth(int id);

    String getMaxAvailableReverseBandwidth(int id);

    String getCurrentFwdLatency(int id);

    String getCurrentRevLatency(int id);

    int getNetworkType(int id);

    String getQosState(int id);

    boolean requestQoS(int id, int localPort, String localAddress);

    /* ConnectivityService */
    void setDefaultConnectionNwPref(int preference);

    void sendDefaultNwPref2Cne(int mNetworkPreference);

    /* LinkProvider */
    boolean reportLinkSatisfaction_LP(int role, int mPid, LinkInfo info, boolean isSatisfied,
            boolean isNotifyBetterCon);

    boolean getLink_LP(int role, Map<LinkRequirement, String> linkReqs, int mPid, IBinder listener);

    boolean switchLink_LP(int role, int mPid, LinkInfo info, boolean isNotifyBetterLink);

    boolean rejectSwitch_LP(int role, int mPid, LinkInfo info, boolean isNotifyBetterLink);

    boolean releaseLink_LP(int role, int mPid);

    /* FMC */
    boolean startFmc(IBinder listener);

    boolean stopFmc(IBinder listener);

    boolean getFmcStatus(IBinder listener);
}
