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

import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;

import android.net.ConnectivityManager;
import android.util.Log;

import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;

public class CarrierProfile {
    private static final boolean DBG = true;
    private static final String TAG = "CarrierProfile";

    class PolicyTable {
        public Map<Integer, RolePolicy> rolePolicyList;
        Map<String, Integer> batteryConstraints;
        public List<Integer> mss;
        public Map<Integer, String> ratBringDownPref;

        public PolicyTable() {
            super();
            rolePolicyList = new HashMap<Integer, RolePolicy>();
            batteryConstraints = new HashMap<String, Integer>();
            mss = new ArrayList<Integer>();
            ratBringDownPref = new TreeMap<Integer, String>();
        }

        @Override
        public String toString() {
            return "policy list: " + rolePolicyList.toString() + "\n" +
                "battery constraints: " + batteryConstraints.toString() + "\n" +
                "mss: " + mss.toString() + "\n" +
                "rat bringdown pref: " + ratBringDownPref.toString();
        }
    }

    class RolePolicy {
        public int id;
        public int priority;
        public String description;
        public List<OpMode> opModes;

        public RolePolicy() {
            super();
            opModes = new ArrayList<OpMode>();
        }

        @Override
        public String toString() {
            return "id " + id + "\n"
                + "priority " + priority + "\n"
                + "description " + description +"\n"
                + opModes;
        }
    }

    class OpMode {
        // TODO: are conditions still planned? (not implemented in CnE now)
        public Map<Integer, String> ratPrefOrder;

        public OpMode() {
            super();
            ratPrefOrder = new TreeMap<Integer, String>();
        }

        @Override
        public String toString() {
            return ratPrefOrder.toString();
        }
    }

    class RatName {
        // TODO: Should this be an enum/string map?
        static final String WWAN = "WWAN";
        static final String WLAN = "WLAN";
        static final String ANY = "ANY";
    }

    // XML entity definitions
    static final String OPERATOR_POLICY = "operatorPolicy";
    static final String ROLES_LIST = "rolesList";
    static final String ROLE = "role";
    static final String ROLE_PRIORITY = "rolePriority";
    static final String ROLE_DESCRIPTION = "roleDescription";
    static final String OP_MODE = "OpMode";
    static final String CONDITIONS = "conditions";
    static final String TIME_OF_DAY_CONDITION = "TimeOfDayCondition";
    static final String START_TIME = "startTime";
    static final String END_TIME = "endTime";
    static final String MONTH_DAY_CONDITION = "MonthDayCondition";
    static final String START_DATE = "startDate";
    static final String END_DATE = "endDate";
    static final String RAT_PREFERENCE = "RATPreference";
    static final String RAT = "RAT";
    static final String MSS = "MSS";
    static final String ROLE_ID = "roleId";
    static final String ID = "id";
    static final String RAT_PRI = "Pri";
    static final String CONSTRAINTS = "constraints";
    static final String BATTERY_CONSTRAINT = "batteryConstraint";
    static final String BATTERY_LEVEL = "BatteryLevel";
    static final String MAX_ON_RATS = "MaxONRATs";
    static final String RAT_BRING_DOWN_PREF = "RATBringDownPref";
    static final String USER_POLICY = "userPolicy";

    // Members
    private PolicyTable mPolicyTable;

    public CarrierProfile() {
        super();
        mPolicyTable = new PolicyTable();
    }

    /**
     * Parses an XML carrier profile into an internal data structure.
     * @param xmlStream The carrier profile.
     * @return boolean: success
     */
    public boolean parse(InputStream xmlStream) {
        DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();

        try {
            DocumentBuilder builder = factory.newDocumentBuilder();
            Document dom = builder.parse(xmlStream);
            Element root = dom.getDocumentElement();
            NodeList roles = root.getElementsByTagName(ROLE);
            for (int i = 0; i < roles.getLength(); i++) {
                RolePolicy rolePolicy = new RolePolicy();
                Node nRole = roles.item(i);
                rolePolicy.id = Integer.parseInt(nRole.getAttributes().getNamedItem(ID).getNodeValue());
                NodeList children = nRole.getChildNodes();
                for (int j = 0; j < children.getLength(); j++) {
                    Node child = children.item(j);
                    String name = child.getNodeName();
                    if (name.equalsIgnoreCase(ROLE_PRIORITY)){
                        rolePolicy.priority = Integer.parseInt(child.getFirstChild().getNodeValue());
                    } else if (name.equalsIgnoreCase(OP_MODE)) {
                        Node nRatPreference = ((Element) child).getElementsByTagName(RAT_PREFERENCE).item(0);
                        NodeList ratList = nRatPreference.getChildNodes();
                        OpMode opMode = new OpMode();
                        for (int k = 0; k < ratList.getLength(); k++) {
                            if (ratList.item(k).getNodeName().equalsIgnoreCase(RAT)) {
                                Node nRat = ratList.item(k);
                                int priority = Integer.parseInt(nRat.getAttributes().getNamedItem(RAT_PRI).getNodeValue());
                                opMode.ratPrefOrder.put(priority, nRat.getFirstChild().getNodeValue());
                            }
                        }
                        rolePolicy.opModes.add(opMode);
                    }
                }
                mPolicyTable.rolePolicyList.put(new Integer(rolePolicy.id), rolePolicy);
            }

            parseConstraints((Element) root.getElementsByTagName(CONSTRAINTS).item(0));
            parseMss((Element) root.getElementsByTagName(MSS).item(0));
            parseRatBringDownPref((Element) root.getElementsByTagName(RAT_BRING_DOWN_PREF).item(0));

        } catch (Exception e) {
            e.printStackTrace();
            return false;
        }

        if (DBG) Log.v(TAG, "CarrierProfile parsed results:\n" + mPolicyTable);
        return true;
    }

    private void parseConstraints(Element nConstraints) {
        NodeList battConstraintsList = nConstraints.getElementsByTagName(BATTERY_CONSTRAINT);
        for (int i = 0; i < battConstraintsList.getLength(); i++) {
            Element battConstraint = (Element) battConstraintsList.item(i);
            String level = battConstraint.getElementsByTagName(BATTERY_LEVEL).item(0).getFirstChild().getNodeValue();
            int maxOnRats = Integer.parseInt(battConstraint.getElementsByTagName(MAX_ON_RATS).item(0).getFirstChild().getNodeValue());
            mPolicyTable.batteryConstraints.put(level, maxOnRats);
        }
    }

    private void parseMss(Element nMss) {
        NodeList roleIdList = nMss.getElementsByTagName(ROLE_ID);
        for (int i = 0; i < roleIdList.getLength(); i++) {
            Node nRoleId = roleIdList.item(i);
            int roleId = Integer.parseInt(nRoleId.getFirstChild().getNodeValue());
            mPolicyTable.mss.add(roleId);
        }
    }

    private void parseRatBringDownPref(Element nRatBringDownPref) {
        NodeList ratList = nRatBringDownPref.getElementsByTagName(RAT);
        for (int i = 0; i < ratList.getLength(); i++) {
            Node nRat = ratList.item(i);
            int priority = Integer.parseInt(nRat.getAttributes().getNamedItem(RAT_PRI).getNodeValue());
            mPolicyTable.ratBringDownPref.put(priority, nRat.getFirstChild().getNodeValue());
        }
    }

    /**
     * Returns a sorted list of preferred networks, based on current state.
     * e.g. { ConnectivityManager.TYPE_WIFI, ConnectivityManager.TYPE_MOBILE }
     */
    public List<Integer> getPreferredNetworks(int roleId) {
        RolePolicy rolePolicy = mPolicyTable.rolePolicyList.get(new Integer(roleId));
        if (rolePolicy == null) {return null;}

        // TODO: find the correct opMode
        List<String> networkList = new ArrayList<String>(rolePolicy.opModes.get(0).ratPrefOrder.values());

        List<Integer> retList = new ArrayList<Integer>(networkList.size());
        for (int i = 0; i < networkList.size(); i++) {
            int networkInt = networkStringToInt(networkList.get(i));
            if (networkInt != -1) {
                // only add valid networks
                retList.add(networkInt);
            }
        }

        return retList;
    }

    /**
     * Converts network names (as used in Carrier Profile, e.g. "WWAN") to
     * ConnectivityManager constants.
     *
     * @param network Name of network
     * @return The corresponding network constant from ConnectivityManager.
     */
    private int networkStringToInt(String network) {
        int networkInt = -1;

        if (network == null) {
            if (DBG) Log.d(TAG, "networkStringToInt: null string");
        } else if (network.equals("WLAN")) {
            networkInt = ConnectivityManager.TYPE_WIFI;
        } else if (network.equals("WWAN")) {
            networkInt = ConnectivityManager.TYPE_MOBILE;
        } else {
            if (DBG) Log.d(TAG, network + " is not a known network name.");
        }

        if (DBG) Log.v(TAG, "networkStringToInt: " + network + ":" + networkInt);

        return networkInt;
    }
}
