/*
 * Copyright (c) 2010, The Linux Foundation. All rights reserved.
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

import com.android.internal.telephony.Phone.BearerType;


class CdmaNAI extends DataProfile {

    /* TODO: This class is a TODO! */

    /* ID of the profile in the modem */
    private int mProfileId = 0;

    @Override
    boolean canHandleServiceType(DataServiceType type) {
        return true;
    }

    @Override
    BearerType getBearerType() {
        return BearerType.IPV4V6;
    }

    @Override
    DataProfileType getDataProfileType() {
        return DataProfileType.PROFILE_TYPE_3GPP2_NAI;
    }

    public int getProfileId() {
        return mProfileId;
    }

    @Override
    public String toShortString() {
        return "CDMA NAI";
    }

    @Override
    String toHash() {
        return this.toString();
    }

}
