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

import java.util.List;
import java.util.ArrayList;
import java.util.LinkedList;
import java.util.Iterator;
import java.util.ListIterator;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.Set;
import java.lang.System;
import java.lang.Integer;
import android.os.SystemProperties;

class UrlHistory {

    // The maximal number of subhosts saved per main resource
    private final static int SUBHOSTS_MAX = 30;

    // The number of subhosts to remove when new subhost is to be inserted
    // and the hash is full
    private final static int SUBHOSTS_TO_REMOVE = 4;

    // The maximal number of subhosts used for pre connection
    private final static int SUBHOSTS_TO_CONNECT_MAX = 15;
    private final static int MAX_USE_COUNT = Integer.MAX_VALUE;

    private int mUseCount;
    private String mMainHost;
    private HashMap<String, Subhost> mSubhosts;
    private LinkedList<Subhost> mSubhostsToConnect;

    public UrlHistory(String mainHost)
    {
        mUseCount = 0;
        mMainHost = new String(mainHost);
        mSubhostsToConnect = null;
        mSubhosts = new HashMap<String, Subhost>(SUBHOSTS_MAX + 1, 1);
    }

    public int getUseCount()
    {
        return mUseCount;
    }

    public String getMainHost()
    {
        return mMainHost;
    }

    public void setUseCount(int useCount)
    {
        if (0 <= useCount) {
            mUseCount = useCount;
        }else {
            mUseCount = 0;
        }

        return;
    }

    public void incrementUseCount()
    {
        if (mUseCount < MAX_USE_COUNT) {
            mUseCount++;
        }

        return;
    }

    public void addSubhost(String subHost)
    {
        if (null != subHost) {

            if (mSubhosts.containsKey(subHost)) {
                (mSubhosts.get(subHost)).incrementNumberOfReferences();
                (mSubhosts.get(subHost)).incrementWeight();
                return;
            }

            if (SUBHOSTS_MAX == mSubhosts.size()) {
                removeLowWeightSubhosts();
            }

            Subhost newSubHost = new Subhost(subHost);
            mSubhosts.put(subHost, newSubHost);
        }

        return;
    }

    // this function is used when getting history from database
    public void addSubhost(Subhost subHost)
    {
        if (null != subHost) {
            if (!mSubhosts.containsKey(subHost.getHost())) {
                mSubhosts.put(subHost.getHost(), subHost);
            }
        }

        return;
    }

    public LinkedList<Subhost> getSubhosts()
    {
        LinkedList<Subhost> subhosts = new LinkedList<Subhost>(mSubhosts.values());
        return subhosts;
    }

    public LinkedList<Subhost> getSubhostsToConnect()
    {
        return mSubhostsToConnect;
    }

    // This function is called onLoadStarted
    public void resetSubhostsWeight()
    {
        Set keys = mSubhosts.keySet();
        Iterator iter = keys.iterator();

        while (iter.hasNext()) {
            Subhost curSubHost = (Subhost)mSubhosts.get((String)(iter.next()));
            curSubHost.decrementWeight();
            curSubHost.resetNumberOfReferences();
        }

        return;
    }

    // This function is called onLoadFinished
    public void updateSubhostsReferences()
    {
        Set keys = mSubhosts.keySet();
        Iterator iter = keys.iterator();

        while (iter.hasNext()) {
            Subhost curSubHost = (Subhost)mSubhosts.get((String)(iter.next()));
            curSubHost.updateNumberOfReferences();
        }

        return;
    }

    public void updateSubhostsToConnect()
    {
        if (null != mSubhostsToConnect) {
            mSubhostsToConnect.clear();
        }else {
            mSubhostsToConnect = new LinkedList<Subhost>();
        }

        Set keys = mSubhosts.keySet();
        Iterator iter = keys.iterator();

        while (iter.hasNext()) {
            String url = (String)(iter.next());
            Subhost newSubHost = new Subhost(url, (mSubhosts.get(url)).getNumberOfReferences(), (mSubhosts.get(url)).getWeight());
            mSubhostsToConnect.add(newSubHost);
        }

        Collections.sort(mSubhostsToConnect, new SubhostComparator());

        int subhostsToRemove = mSubhostsToConnect.size() - SUBHOSTS_TO_CONNECT_MAX;
        while (0 < subhostsToRemove) {
            subhostsToRemove--;
            mSubhostsToConnect.removeLast();
        }

       return ;
    }

    private void removeLowWeightSubhosts()
    {
        LinkedList<Subhost> subhosts = new LinkedList<Subhost>(mSubhosts.values());
        Collections.sort(subhosts, new SubhostComparator());

        int removedSubhost = 0;
        while (removedSubhost < SUBHOSTS_TO_REMOVE) {
            ++removedSubhost;
            mSubhosts.remove((subhosts.getLast()).getHost());
            subhosts.removeLast();
        }

        return;
    }

    class SubhostComparator implements Comparator<Subhost> {

        public int compare(Subhost subA, Subhost subB) {
            if (subA.getWeight() == subB.getWeight()) {
                return 0;
            }

            if (subA.getWeight() < subB.getWeight()) {
                return 1;
            }else {
                return -1;
            }
        }
    }

}
