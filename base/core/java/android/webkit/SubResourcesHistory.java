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

import android.webkit.WebViewDatabase;

import android.util.Log;

import java.util.LinkedList;
import java.util.ListIterator;
import java.util.Iterator;
import java.util.HashMap;
import java.util.Set;
import android.os.SystemProperties;
import android.content.Context;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;

class SubResourcesHistory{

    // Synchronization object for controlling modifications
    // of the SubResourcesHistory data structures
    public final static Object mSubResourcesHistoryLock = new Object();

    /**
     * Static instance of a SubResourcesHistory object.
     */
    private static SubResourcesHistory sSubResourcesHistory;

    //
    private static int mReferenceCount;

    //
    private final static int MAIN_RESOURCES_MAX = 300;

    private final static int URLHISTORIES_TO_REMOVE = 30;

    //
    private final static int PERIODIC_INTERVAL = SystemProperties.getInt("http.periodic_writer.interval", 10 * 60 * 1000);

    private PeriodicWriter mPeriodicWriter;
    private static WebViewDatabase mDatabase;
    private HashMap<String, UrlHistory> mHistory;
    private boolean mIsDbUpdateNeeded;

    public static synchronized SubResourcesHistory getInstance(Context context) {
        if (null == sSubResourcesHistory) {
            sSubResourcesHistory = new SubResourcesHistory(context);
            mDatabase.getSubhostsData(sSubResourcesHistory);
        }
        mReferenceCount++;
        return sSubResourcesHistory;
    }

    private SubResourcesHistory(Context context)
    {
        mReferenceCount = 0;
        mDatabase = WebViewDatabase.getInstance(context);
        mHistory = new HashMap<String, UrlHistory>(MAIN_RESOURCES_MAX + 1, 1);
        mIsDbUpdateNeeded = false;

        mPeriodicWriter = new PeriodicWriter();
        mPeriodicWriter.start();
    }

    public synchronized void addSubHost(String mainHost, String subHost)
    {
        if ((null == mainHost)
            || (null == subHost)
            || mainHost.equals("")
            || subHost.equals("")) {

            return;
        }

        // history is updated, need to write changes to db in next run of periodic thread
        if (!mIsDbUpdateNeeded) {
            setIsDbUpdateNeeded(true);
        }

        if (mHistory.containsKey(mainHost)) {
            synchronized (mSubResourcesHistoryLock) {
                (mHistory.get(mainHost)).addSubhost(subHost);
                return;
            }
        }

        if (MAIN_RESOURCES_MAX == mHistory.size()) {
            removeLowWeightMainUrls();
        }

        // url was not found in history, add it
        UrlHistory newHistory = new UrlHistory(mainHost);
        newHistory.addSubhost(subHost);
        synchronized (mSubResourcesHistoryLock) {
            mHistory.put(mainHost, newHistory);
        }

        return;
    }

    // this function is used when getting history from database
    public synchronized void addSubHost(String mainHost, Subhost subHost)
    {
        if ((null == mainHost)
            || (null == subHost)
            || mainHost.equals("")) {

            return;
        }

        if (mHistory.containsKey(mainHost)) {
            synchronized (mSubResourcesHistoryLock) {
                (mHistory.get(mainHost)).addSubhost(subHost);
                return;
            }
        }

        // url was not found in history, insert it
        UrlHistory newHistory = new UrlHistory(mainHost);
        newHistory.addSubhost(subHost);
        synchronized (mSubResourcesHistoryLock) {
            mHistory.put(mainHost, newHistory);
        }

        return;
    }

    public LinkedList<Subhost> getSubHosts(String mainHost)
    {
        if ((null == mainHost)) {
            return null;
        }

        if (mHistory.containsKey(mainHost)) {
            return (mHistory.get(mainHost)).getSubhosts();
        }

        return null;
    }

    public LinkedList<Subhost> getSubhostsToConnect(String mainHost)
    {
        if ((null == mainHost)) {
            return null;
        }

        if (mHistory.containsKey(mainHost)) {
            return (mHistory.get(mainHost)).getSubhostsToConnect();
        }

        return null;
    }

    public Set getMainUrls()
    {
        return mHistory.keySet();
    }

    public int getUseCount(String mainHost)
    {
        if (null == mainHost) {
            return 0;
        }

        if (mHistory.containsKey(mainHost)) {
           return (mHistory.get(mainHost)).getUseCount();
        }

        return 0;
    }

    public synchronized void setUseCount(String mainHost, int useCount)
    {
        if (mHistory.containsKey(mainHost)) {
           (mHistory.get(mainHost)).setUseCount(useCount);
        }

        return;
    }

    public synchronized void onLoadFinished(String mainHost)
    {
        incrementMainUrlUseCount(mainHost);
        if (mHistory.containsKey(mainHost)) {
            (mHistory.get(mainHost)).updateSubhostsReferences();
            (mHistory.get(mainHost)).updateSubhostsToConnect();
        }
        return;
    }

    private void incrementMainUrlUseCount(String mainHost)
    {
        if (null == mainHost) {
            return;
        }

        if (mHistory.containsKey(mainHost)) {
            (mHistory.get(mainHost)).incrementUseCount();
        }

        return;
    }

    public synchronized void updateUrlHistory(String mainHost)
    {
        if (null == mainHost) {
            return;
        }

        if (mHistory.containsKey(mainHost)) {
            (mHistory.get(mainHost)).resetSubhostsWeight();
        }

        return;
    }

    public synchronized void decrementReferenceCount()
    {
        if (1 == mReferenceCount) {
            mDatabase.setSubhostsData(sSubResourcesHistory);
            sSubResourcesHistory = null;
            mPeriodicWriter.stopRequest();
        }

        mReferenceCount--;
    }

    public synchronized void updateSubhostsToConnect()
    {
        synchronized (mSubResourcesHistoryLock) {
            Set keys = mHistory.keySet();
            Iterator iter = keys.iterator();
            while (iter.hasNext()) {
                String url = (String)(iter.next());
                (mHistory.get(url)).updateSubhostsToConnect();
            }
        }

        return;
    }

    private void removeLowWeightMainUrls()
    {
        LinkedList<UrlHistory> urlHistories = new LinkedList<UrlHistory>(mHistory.values());
        Collections.sort(urlHistories, new UrlHistoryComparator());

        int removedHistories = 0;

        synchronized (mSubResourcesHistoryLock) {
            while (removedHistories < URLHISTORIES_TO_REMOVE) {
                ++removedHistories;
                mHistory.remove((urlHistories.getLast()).getMainHost());
                urlHistories.removeLast();
            }
        }

        return;
    }

    private synchronized void setIsDbUpdateNeeded(boolean updateNeeded){
        mIsDbUpdateNeeded = updateNeeded;
    }

    private class PeriodicWriter extends Thread {

        private boolean mStoped = false;

        public void stopRequest()
        {
            mStoped = true;
        }

        public void run() {
            setName("PeriodicWriter");
            android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_BACKGROUND);
            while (true) {
                if (mStoped) {
                    mPeriodicWriter = null;
                    return;
                }

                try {
                    Thread.currentThread().sleep(PERIODIC_INTERVAL);
                } catch(InterruptedException e){
                }

                if (mStoped) {
                    mPeriodicWriter = null;
                    return;
                }

                if (null != sSubResourcesHistory && 0 != mReferenceCount) {
                    Log.v("webkit","TCP pre-connection: periodic writer");
                    if (mIsDbUpdateNeeded) {
                        mDatabase.setSubhostsData(sSubResourcesHistory);
                        setIsDbUpdateNeeded(false);
                    }
                }else {
                    mPeriodicWriter = null;
                    return;
                }
            }
        }
    }

    class UrlHistoryComparator implements Comparator<UrlHistory> {

        public int compare(UrlHistory historyA, UrlHistory historyB) {
            if (historyA.getUseCount() == historyB.getUseCount()) {
                return 0;
            }

            if (historyA.getUseCount() < historyB.getUseCount()) {
                return 1;
            }else {
                return -1;
            }
        }
    }
}
