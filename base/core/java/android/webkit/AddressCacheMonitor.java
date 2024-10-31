/*
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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
 */

package android.webkit;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.util.Log;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import java.net.InetAddress;
import java.lang.String;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import java.lang.SecurityException;

class AddressCacheMonitor extends BroadcastReceiver {

    static final String ADDRESS_CACHE_LOGTAG = "ADDRESS_CACHE_MONITOR";

    private Context mContext;
    private String mPreviousNetworkName;
    private static AddressCacheMonitor mAddressCacheMonitor = null;

    private AddressCacheMonitor(Context context)
    {
        mContext      = context;
        IntentFilter intentFilter = new IntentFilter();
        intentFilter.addAction(ConnectivityManager.CONNECTIVITY_ACTION);
        mContext.getApplicationContext().registerReceiver(this, intentFilter);
        Log.d(ADDRESS_CACHE_LOGTAG, "Address cache monitor created and armed");
    }

    public static synchronized AddressCacheMonitor getAddressCacheMonitor(Context context)
    {
        if(mAddressCacheMonitor == null)
        {
          mAddressCacheMonitor = new AddressCacheMonitor(context);
        }
        return mAddressCacheMonitor;
    }

    @Override
    public void onReceive(Context context, Intent intent) {

        if (intent.getAction().equals(ConnectivityManager.CONNECTIVITY_ACTION)) {

            NetworkInfo networkInfo = (NetworkInfo)
                intent.getParcelableExtra(ConnectivityManager.EXTRA_NETWORK_INFO);

            int type                    = networkInfo.getType();
            boolean isConnected         = networkInfo.isConnected();
            String networkName          = networkInfo.getTypeName();
            boolean exceptionCaught = false;

            if (type == ConnectivityManager.TYPE_WIFI) {
                try {
                    WifiManager wifiManager = (WifiManager) mContext.getSystemService(Context.WIFI_SERVICE);
                    WifiInfo wifiInfo       = wifiManager.getConnectionInfo();
                    networkName             = networkName + " " + wifiInfo.getSSID();
                }
                catch (SecurityException se) {
                    Log.i(ADDRESS_CACHE_LOGTAG,
                          "No permission to access connection info? - "  + se);
                    exceptionCaught = true;
                }
            }

            Log.d(ADDRESS_CACHE_LOGTAG, "network ("  +
                                        networkName  +
                                        ") is "      +
                                        (isConnected ? "connected" : "disconnected") );

            if ( isConnected &&
                 (exceptionCaught || !networkName.equals(mPreviousNetworkName)) ) {
                Log.d(ADDRESS_CACHE_LOGTAG, "clearing address cache" );
                mPreviousNetworkName = networkName;
                InetAddress.clearAddressCache();
            }
        }
    }
}

