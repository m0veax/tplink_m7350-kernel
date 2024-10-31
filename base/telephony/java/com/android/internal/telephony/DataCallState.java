/*
 * Copyright (C) 2009-11 Qualcomm Innovation Center, Inc.  All Rights Reserved.
 * Copyright (C) 2009 The Android Open Source Project
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


public class DataCallState {
    public int status;
    public int suggestedRetryTime;
    public int cid;
    public int active;
    public String type;
    public String ifname;
    public String addresses;
    public String dnses;
    public String gateways;

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();
        sb.append("DataCallState: {");
        sb.append("status:").append(status);
        sb.append(",suggestedRetryTime: ").append(suggestedRetryTime);
        sb.append(",cid: ").append(cid);
        sb.append(",active: ").append(active);
        sb.append(",type: ").append(type);
        sb.append(",ifname: ").append(ifname);
        sb.append(",addresses: ").append(addresses);
        sb.append(",dnses: ").append(dnses);
        sb.append(",gateways: ").append(gateways);
        sb.append("}");
        return sb.toString();
    }
}
