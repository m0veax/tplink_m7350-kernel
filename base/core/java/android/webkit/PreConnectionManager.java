/*
 * Copyright (C) 2011, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

package android.webkit;

import android.util.Log;
import android.content.Context;
import java.util.LinkedList;
import android.os.SystemProperties;
import android.net.http.*;

public class PreConnectionManager{

    public final static int MAX_PRE_CONNECTION_THREADS = 5;
    public final static int PRE_CONNECTION_THREAD_UNDEFINED = -1;

    // System property,
    // true : feature on
    // false: feature off
    private boolean TCP_PRE_CONNECT;

    private String mCurrentMainURL;
    private final Network mNetwork;
    private RequestQueue mRequestQueue;
    private SubResourcesHistory mSubResorcesHistory;

    // This Id is needed in case that there are multiple windows open
    // each window has it's own PreConnectionManager and PreConnectionThread
    private int mPreConnectionThreadId;

    public PreConnectionManager(Context context)
    {
        TCP_PRE_CONNECT = SystemProperties.getBoolean("net.http.preconnect", true);

        mCurrentMainURL = new String();
        mNetwork = Network.getInstance(context);
        mSubResorcesHistory = SubResourcesHistory.getInstance(context);
        mPreConnectionThreadId = PRE_CONNECTION_THREAD_UNDEFINED;
    }

    public void onLoadStarted(String mainUrl)
    {
        if (TCP_PRE_CONNECT) {
            if (!mNetwork.isValidProxySet()) {
                if ((mPreConnectionThreadId != PRE_CONNECTION_THREAD_UNDEFINED) && !mainUrl.equals(mCurrentMainURL)) {
                    mNetwork.stopNetworkConnections(mPreConnectionThreadId);
                    mCurrentMainURL = mainUrl;
                    preConnect();
                    mSubResorcesHistory.updateUrlHistory(mCurrentMainURL);
                    return;
                }

                if ((mPreConnectionThreadId == PRE_CONNECTION_THREAD_UNDEFINED) && !mainUrl.equals(mCurrentMainURL)) {
                    mCurrentMainURL = mainUrl;
                    preConnect();
                    mSubResorcesHistory.updateUrlHistory(mCurrentMainURL);
                }
            }
        }

        return;
    }

    public void onLoadFinished()
    {
        if (TCP_PRE_CONNECT) {
            mSubResorcesHistory.onLoadFinished(mCurrentMainURL);

            if (-1 != mPreConnectionThreadId) {
                mNetwork.stopNetworkConnections(mPreConnectionThreadId);
            }

            mPreConnectionThreadId = PRE_CONNECTION_THREAD_UNDEFINED;
            mCurrentMainURL = null;
        }

        return;
    }

    public void onResourceLoad(String subHost)
    {
        if (TCP_PRE_CONNECT) {
            if (null != mCurrentMainURL) {
                if (subHost.equals("")) {
                    subHost = mCurrentMainURL;
                }
                mSubResorcesHistory.addSubHost(mCurrentMainURL, subHost);
            }
        }
        return;
    }

    private void preConnect()
    {
        if (TCP_PRE_CONNECT) {
            LinkedList<Subhost> subhosts = null;
            if (null != mSubResorcesHistory.getSubhostsToConnect(mCurrentMainURL)) {
                synchronized (SubResourcesHistory.mSubResourcesHistoryLock) {
                    subhosts = new LinkedList<Subhost>(mSubResorcesHistory.getSubhostsToConnect(mCurrentMainURL));
                }
            }

            if ((null != subhosts) && (0 != subhosts.size())) {
                mPreConnectionThreadId = mNetwork.requestNetworkConnections(this, subhosts);
            } else {
                if (null != subhosts) {
                    if (0 != subhosts.size()) {
                        // TODO: remove main url
                        Log.v("webkit","TCP pre-connection: preConnect() subhost is zero " + mCurrentMainURL);
                    }
                }
            }
        }

        return;
    }

    // This function is used by PreConnectionThread to inform PreConnectionManager that preconnection is finished
    public void setPreConnectionThreadId(int id)
    {
        if(PRE_CONNECTION_THREAD_UNDEFINED == id) {
            if (PRE_CONNECTION_THREAD_UNDEFINED != mPreConnectionThreadId) {
                mNetwork.cleanPreConnectionThreadEntry(mPreConnectionThreadId);
            }

        }

        if ((PRE_CONNECTION_THREAD_UNDEFINED == id) || (0 <= id && id < MAX_PRE_CONNECTION_THREADS)) {
            mPreConnectionThreadId = id;
        }

        return;
    }

    public void onDestroy()
    {
        if (TCP_PRE_CONNECT) {
            mSubResorcesHistory.decrementReferenceCount();
        }
        return;
    }
}
