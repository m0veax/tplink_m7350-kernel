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

package android.net.http;

import android.content.Context;
import android.os.SystemClock;

import android.util.Log;

import java.util.LinkedList;
import java.util.ListIterator;
import java.nio.channels.SocketChannel;
import java.net.InetSocketAddress;
import org.apache.http.params.CoreConnectionPNames;
import org.apache.http.params.HttpConnectionParams;
import org.apache.http.params.HttpParams;
import org.apache.http.params.BasicHttpParams;
import java.net.Socket;
import android.net.WebAddress;
import android.os.SystemClock;

import org.apache.http.HttpHost;

import java.lang.Exception;
import java.io.IOException;
import java.lang.InterruptedException;
import java.nio.channels.UnresolvedAddressException;
import java.nio.channels.UnsupportedAddressTypeException;
import java.lang.SecurityException;

import android.webkit.Subhost;
import android.webkit.PreConnectionManager;

import java.lang.Thread;
import java.lang.Double;

class PreConnectionThread extends Thread{

    class SocketEntry {
        String mSubhost;
        SocketChannel mSocketChannel;
    };

    private final static double CONNECTION_WEIGHT = 0.8;
    private final static int MAX_CONNECTION_TIME = 500;
    private final static int SLEEP_TIME = 100;

    private final static int FIRST_LEVEL_PRE_CONNECT = 10;
    private final static int SECOND_LEVEL_PRE_CONNECT = 20;
    private final static int FIRST_LEVEL_NUM_CONNECTIONS = 1;
    private final static int SECOND_LEVEL_NUM_CONNECTIONS = 2;

    // Max number of connections to be opened is bounded by 25
    private final static int MAX_PENDING_CONNECTONS = 25;

    private boolean mStopped;
    private Context mContext;
    private RequestFeeder mRequestFeeder;
    private LinkedList<Subhost> mSubhosts;
    private LinkedList<SocketEntry> mSocketChannels;
    private RequestQueue.ConnectionManager mConnectionManager;
    private PreConnectionManager mPreConnectionMgr;
    private int mPendingConnections;
    private int mOpenedConnections;

    public PreConnectionThread(LinkedList<Subhost> subhosts,
                               RequestQueue.ConnectionManager connectionManager,
                               Context context,
                               RequestFeeder requestFeeder,
                               PreConnectionManager preConnectionMgr)
    {
        super();
        mStopped = false;
        mContext = context;
        mRequestFeeder = requestFeeder;
        mPreConnectionMgr = preConnectionMgr;
        mConnectionManager = connectionManager;
        mSubhosts = subhosts;
        mPendingConnections = 0;
        mOpenedConnections = 0;
        mSocketChannels = new LinkedList<SocketEntry>();

        setName("tcp pre-connection");
    }

    public void Stop()
    {
        mStopped = true;
    }

    public void run()
    {
        // If the page load was cancelled
        if (mStopped) {
            stopConnecting();
            return;
        }

        // initiate opening connections in non blocking mode
        initiateConnectionsFirstLevel();

        if (mStopped) {
            stopConnecting();
            return;
        }

        // store successfuly opened connection to IdleCache
        storeConnectionsFirstLevel();

        if (mStopped) {
            stopConnecting();
            return;
        }

        // initiate opening connections in non blocking mode
        initiateConnectionsExtraLevel();

        if (mStopped) {
            stopConnecting();
            return;
        }

         // store successfuly opened connection to IdleCache
        storeConnectionsExtraLevel();

        stopConnecting();

        return;
    }

    private void initiateConnectionsFirstLevel()
    {
        Log.v("http","TCP pre-connection: initiate connect first level ");

        ListIterator iter = mSubhosts.listIterator();
        while (iter.hasNext()) {
            if (mStopped) {
                stopConnecting();
                return;
            }

            Subhost subhost = (Subhost)iter.next();
            int res = Double.compare(subhost.getWeight(), CONNECTION_WEIGHT);
            if (0 <= res) {
                try {
                    SocketChannel sChannel = SocketChannel.open();
                    sChannel.configureBlocking(false);
                    try {
                        sChannel.connect(new InetSocketAddress(subhost.getHost(), 80));

                        SocketEntry sEntry = new SocketEntry();
                        sEntry.mSubhost = subhost.getHost();
                        sEntry.mSocketChannel = sChannel;

                        mSocketChannels.add(sEntry);
                        mPendingConnections++;
                    } catch (Exception e) {
                        try {
                             sChannel.close();
                        } catch (IOException ee) {
                        }
                    }
                } catch (IOException e) {
                } catch (UnresolvedAddressException e) {
                } catch (UnsupportedAddressTypeException e) {
                } catch (SecurityException e) {
                }
            }
        }

    return;
    }

    private void initiateConnectionsExtraLevel()
    {
        Log.v("http","TCP pre-connection: initiate connect second level ");
        ListIterator iter = mSubhosts.listIterator();
        while (iter.hasNext()) {
            if (mStopped) {
                stopConnecting();
                return;
            }

            Subhost subhost = (Subhost)iter.next();
            int res = Double.compare(subhost.getWeight(), CONNECTION_WEIGHT);
            if (0 <= res) {
                int numOfConnections = 0;
                if (SECOND_LEVEL_PRE_CONNECT < subhost.getNumberOfReferences()) {
                    numOfConnections = SECOND_LEVEL_NUM_CONNECTIONS;
                }else {
                    if (FIRST_LEVEL_PRE_CONNECT < subhost.getNumberOfReferences()) {
                            numOfConnections = FIRST_LEVEL_NUM_CONNECTIONS;
                    }
                }

                for(int i = 0; i < numOfConnections; ++i) {
                    try {
                        SocketChannel sChannel = SocketChannel.open();
                        sChannel.configureBlocking(false);
                        try {
                            sChannel.connect(new InetSocketAddress(subhost.getHost(), 80));

                            SocketEntry sEntry = new SocketEntry();
                            sEntry.mSubhost = subhost.getHost();
                            sEntry.mSocketChannel = sChannel;

                            mSocketChannels.add(sEntry);

                            mPendingConnections++;
                            if (mPendingConnections == MAX_PENDING_CONNECTONS) {
                                // Stop initiating connections if max bound was reached
                                return;
                            }
                        } catch (Exception e) {
                            try {
                                sChannel.close();
                            } catch (IOException ee) {
                            }
                        }

                    } catch (IOException e){
                    } catch (UnresolvedAddressException e) {
                    } catch (UnsupportedAddressTypeException e) {
                    } catch (SecurityException e) {
                    }
                }
            }
        }

        return;
    }

    private void storeConnectionsFirstLevel()
    {
        Log.v("http","TCP pre-connection: finish connect first level ");
        long startTime = SystemClock.uptimeMillis();

        ListIterator sChannelIter = mSocketChannels.listIterator();

        while (sChannelIter.hasNext()) {

            if (mStopped) {
                stopConnecting();
                return;
            }

            SocketEntry sEntry = (SocketEntry)sChannelIter.next();
            WebAddress uri = new WebAddress(sEntry.mSubhost);
            HttpHost httpHost = new HttpHost(uri.mHost, uri.mPort, uri.mScheme);
            try{
                if ((sEntry.mSocketChannel).finishConnect()) {
                    AndroidHttpClientConnection conn = new AndroidHttpClientConnection();
                    BasicHttpParams params = new BasicHttpParams();
                    Socket sock = (sEntry.mSocketChannel).socket();
                    params.setIntParameter(HttpConnectionParams.SOCKET_BUFFER_SIZE, 8192);
                    conn.bind(sock, params);

                    (sEntry.mSocketChannel).configureBlocking(true);

                    Connection connection = Connection.getConnection(mContext, httpHost, null, mRequestFeeder);
                    connection.mHttpClientConnection = conn;

                    // IdleReaper can't close this connection before loadFinished.
                    // When load is finished the flag will be changed to false, then IdleReaper can close it.
                    connection.setTcpPreConnect(true);

                    // if recycling failed, i.e. IdleCache is full, stop preconnecting
                    if (!mConnectionManager.recycleConnection(connection)) {
                        stopConnecting();
                        return;
                    }

                    sChannelIter.remove();
                    mOpenedConnections++;
                }
            } catch (IOException e){
            }
        }
    }

    private void storeConnectionsExtraLevel()
    {
        Log.v("http","TCP pre-connection: finish connect second level ");
        long startTime = SystemClock.uptimeMillis();

        while (true) {

            ListIterator sChannelIter = mSocketChannels.listIterator();

            while (sChannelIter.hasNext()) {

                if (mStopped) {
                    stopConnecting();
                    return;
                }

                SocketEntry sEntry = (SocketEntry)sChannelIter.next();
                WebAddress uri = new WebAddress(sEntry.mSubhost);
                HttpHost httpHost = new HttpHost(uri.mHost, uri.mPort, uri.mScheme);
                try{
                    if ((sEntry.mSocketChannel).finishConnect()) {
                        AndroidHttpClientConnection conn = new AndroidHttpClientConnection();
                        BasicHttpParams params = new BasicHttpParams();
                        Socket sock = (sEntry.mSocketChannel).socket();
                        params.setIntParameter(HttpConnectionParams.SOCKET_BUFFER_SIZE, 8192);
                        conn.bind(sock, params);

                        (sEntry.mSocketChannel).configureBlocking(true);

                        Connection connection = Connection.getConnection(mContext, httpHost, null, mRequestFeeder);
                        connection.mHttpClientConnection = conn;

                        // IdleReaper can't close this connection before loadFinished.
                        // When load is finished the flag will be changed to false, then IdleReaper can close it.
                        connection.setTcpPreConnect(true);

                        // if recycling failed, i.e. IdleCache is full, stop preconnecting
                        if (!mConnectionManager.recycleConnection(connection)) {
                            stopConnecting();
                            return;
                        }

                        sChannelIter.remove();
                        mOpenedConnections++;
                    }
                } catch (IOException e){
                }
            }

            long time = SystemClock.uptimeMillis();

            if ((0 != mSocketChannels.size()) && ((time-startTime) < MAX_CONNECTION_TIME)) {
                try{
                    long passedTime = time-startTime;
                    Thread.currentThread().sleep(SLEEP_TIME);
                } catch(InterruptedException e){
                }
            } else {
                 long passedTime = time-startTime;
                 if (0 != mSocketChannels.size()) {
                     Log.v("http","TCP pre-connection: " + mSocketChannels.size() + " connections not opened");
                     // Clear unopened connections
                     stopConnecting();
                 } else {
                     Log.v("http","TCP pre-connection: total number of opened connections: " + mOpenedConnections);
                 }
                return;
            }
        }
    }

    private void stopConnecting()
    {
        // clear connections that was not opened
        ListIterator sChannelIter = mSocketChannels.listIterator();
        while (sChannelIter.hasNext()) {
            try {
                (((SocketEntry)sChannelIter.next()).mSocketChannel).close();
            } catch (IOException e) {
            }
        }

        mStopped = true;
        mPreConnectionMgr.setPreConnectionThreadId(PreConnectionManager.PRE_CONNECTION_THREAD_UNDEFINED);

        return;
    }
}
