/*
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
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

public class Subhost{

    private final static int HOST_MAX_REFERENCES = Integer.MAX_VALUE;
    private final static double INCREMENTATION_WEIGHT = 0.33;
    private final static double DECREMENTATION_WEIGHT = 0.67;

    private String mHost;
    private double mWeight;

    // number of resources fetched from the subhost during the current load of the website
    private int mNumberOfReferences;

    // number of resources fetched from the subhost during the last load of the website
    private int mOldNumberOfReferences;

    public Subhost(String host)
    {
        if (null != host) {
            mWeight = 1;
            mNumberOfReferences = 1;
            mHost = new String(host);

            // this is the first time the subhost is created
            mOldNumberOfReferences = -1;
        }
    }

    public Subhost(String host, int numOfReferences, double weight)
    {
        if (null != host) {
            mHost = new String(host);

            if (0 < numOfReferences) {
                mNumberOfReferences = numOfReferences;
            }else {
                mNumberOfReferences = 0;
            }

            if (0 < weight) {
                mWeight = weight;
            }else {
                mWeight = 0;
            }

            mOldNumberOfReferences = mNumberOfReferences;

            return;
        }
    }

    public int getNumberOfReferences()
    {
        return mNumberOfReferences;
    }

    public void incrementNumberOfReferences()
    {
        if (mNumberOfReferences < HOST_MAX_REFERENCES) {
            mNumberOfReferences++;
        }

        return;
    }

    public String getHost()
    {
        return mHost;
    }

    public double getWeight()
    {
        return mWeight;
    }

    public void incrementWeight()
    {
        mWeight += INCREMENTATION_WEIGHT;
        return;
    }

    public void decrementWeight()
    {
        mWeight *= DECREMENTATION_WEIGHT;
        return;
    }

    public void resetNumberOfReferences()
    {
        mOldNumberOfReferences = mNumberOfReferences;
        mNumberOfReferences = 0;

        return;
    }

    public void updateNumberOfReferences()
    {
        if (-1 == mOldNumberOfReferences) {
            // this is the first website this subhost is being used to fetch resources
            mOldNumberOfReferences = mNumberOfReferences;
            return;
        }

        // new number of references is the average of the current and old
        mNumberOfReferences = (mNumberOfReferences + mOldNumberOfReferences) / 2;
        return;
    }

}
