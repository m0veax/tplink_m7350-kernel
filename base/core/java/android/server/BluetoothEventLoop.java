/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010, 2011 The Linux Foundation. All rights reserved.
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

package android.server;

import android.bluetooth.BluetoothA2dp;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothUuid;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Message;
import android.os.ParcelUuid;
import android.util.Log;
import android.content.IntentFilter;
import android.content.BroadcastReceiver;

import java.util.HashMap;
import java.util.Set;

/**
 * TODO: Move this to
 * java/services/com/android/server/BluetoothEventLoop.java
 * and make the constructor package private again.
 *
 * @hide
 */
class BluetoothEventLoop {
    private static final String TAG = "BluetoothEventLoop";
    private static final boolean DBG = true;

    private int mNativeData;
    private Thread mThread;
    private boolean mStarted;
    private boolean mInterrupted;

    private final HashMap<String, Integer> mPasskeyAgentRequestData;
    private final HashMap<String, Integer> mAuthorizationRequestData;
    private final HashMap<String, Integer> mAuthorizationAgentRequestData;
    private final BluetoothService mBluetoothService;
    private final BluetoothAdapter mAdapter;
    private final Context mContext;

    private static final int EVENT_RESTART_BLUETOOTH = 1;
    private static final int EVENT_PAIRING_CONSENT_DELAYED_ACCEPT = 2;
    private static final int EVENT_AGENT_CANCEL = 3;
    private static final int EVENT_PAIRING_TIMEOUT = 4;

    private static final int CREATE_DEVICE_ALREADY_EXISTS = 1;
    private static final int CREATE_DEVICE_SUCCESS = 0;
    private static final int CREATE_DEVICE_FAILED = -1;

    // The 1.2/2.0 incoming pairing request would timeout in 30 seconds at LMP since LMP
    // Response time out is 30 seconds. Setting the INCOMING_PAIRING_TIMEOUT to 26 seconds
    // to make sure that the pairing clean up happens from the HOST side.
    private static final int INCOMING_PAIRING_TIMEOUT = 26000;

    private static final String BLUETOOTH_ADMIN_PERM = android.Manifest.permission.BLUETOOTH_ADMIN;
    private static final String BLUETOOTH_PERM = android.Manifest.permission.BLUETOOTH;

    private final Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            String address = null;
            switch (msg.what) {
            case EVENT_RESTART_BLUETOOTH:
                mBluetoothService.restart();
                break;
            case EVENT_PAIRING_CONSENT_DELAYED_ACCEPT:
                address = (String)msg.obj;
                if (address != null) {
                    mBluetoothService.setPairingConfirmation(address, true);
                }
                break;
            case EVENT_AGENT_CANCEL:
                // Set the Bond State to BOND_NONE.
                // We always have only 1 device in BONDING state.
                String[] devices = mBluetoothService.listInState(BluetoothDevice.BOND_BONDING);
                if (devices.length == 0) {
                    break;
                } else if (devices.length > 1) {
                    Log.e(TAG, " There is more than one device in the Bonding State");
                    break;
                }
                address = devices[0];
                mBluetoothService.setBondState(address,
                        BluetoothDevice.BOND_NONE,
                        BluetoothDevice.UNBOND_REASON_REMOTE_AUTH_CANCELED);
                break;
            case EVENT_PAIRING_TIMEOUT:
                address = (String) msg.obj;
                Log.d(TAG, "Cancelling bond process");
                mBluetoothService.cancelBondProcess(address);
            }
        }
    };

    static { classInitNative(); }
    private static native void classInitNative();

    /* package */ BluetoothEventLoop(Context context, BluetoothAdapter adapter,
            BluetoothService bluetoothService) {
        mBluetoothService = bluetoothService;
        mContext = context;
        mPasskeyAgentRequestData = new HashMap();
        mAuthorizationRequestData = new HashMap();
        mAuthorizationAgentRequestData = new HashMap<String, Integer>();
        mAdapter = adapter;
        initializeNativeDataNative();
    }

    protected void finalize() throws Throwable {
        try {
            cleanupNativeDataNative();
        } finally {
            super.finalize();
        }
    }

    /* package */ HashMap<String, Integer> getPasskeyAgentRequestData() {
        return mPasskeyAgentRequestData;
    }

    /* package */ HashMap<String, Integer> getAuthorizationRequestData() {
        return mAuthorizationRequestData;
    }

    /* package */ HashMap<String, Integer> getAuthorizationAgentRequestData() {
        return mAuthorizationAgentRequestData;
    }

    /* package */ void start() {

        if (!isEventLoopRunningNative()) {
            if (DBG) log("Starting Event Loop thread");
            startEventLoopNative();
        }
    }

    public void stop() {
        if (isEventLoopRunningNative()) {
            if (DBG) log("Stopping Event Loop thread");
            stopEventLoopNative();
        }
    }

    public boolean isEventLoopRunning() {
        return isEventLoopRunningNative();
    }

    private void addDevice(String address, String[] properties) {
        mBluetoothService.addRemoteDeviceProperties(address, properties);
        String rssi = mBluetoothService.getRemoteDeviceProperty(address, "RSSI");
        String classValue = mBluetoothService.getRemoteDeviceProperty(address, "Class");
        String devType = mBluetoothService.getRemoteDeviceProperty(address, "Type");
        String name = mBluetoothService.getRemoteDeviceProperty(address, "Name");
        short rssiValue;
        // For incoming connections, we don't get the RSSI value. Use a default of MIN_VALUE.
        // If we accept the pairing, we will automatically show it at the top of the list.
        if (rssi != null) {
            rssiValue = (short)Integer.valueOf(rssi).intValue();
        } else {
            rssiValue = Short.MIN_VALUE;
        }
        if (classValue != null) {
            Intent intent = new Intent(BluetoothDevice.ACTION_FOUND);
            intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
            intent.putExtra(BluetoothDevice.EXTRA_CLASS,
                    new BluetoothClass(Integer.valueOf(classValue)));
            intent.putExtra(BluetoothDevice.EXTRA_RSSI, rssiValue);
            intent.putExtra(BluetoothDevice.EXTRA_NAME, name);

            mContext.sendBroadcast(intent, BLUETOOTH_PERM);
        } else if (devType != null) {
            if (DBG) log("Device type: " + devType);
            if ("LE".equals(devType)) {
                Intent intent = new Intent(BluetoothDevice.ACTION_FOUND);
                intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
                intent.putExtra(BluetoothDevice.EXTRA_RSSI, rssiValue);
                intent.putExtra(BluetoothDevice.EXTRA_NAME, name);

                mContext.sendBroadcast(intent, BLUETOOTH_PERM);
            }
        } else {
            log ("ClassValue: " + classValue + " for remote device: " + address + " is null");
        }
    }

    private void onDeviceFound(String address, String[] properties) {
        if (properties == null) {
            Log.e(TAG, "ERROR: Remote device properties are null");
            return;
        }
        if (!mBluetoothService.isEnabled()) {
            Log.e(TAG, "Bluetooth is not on");
            return;
        }

        addDevice(address, properties);
    }

    private void onDeviceDisappeared(String address) {
        Intent intent = new Intent(BluetoothDevice.ACTION_DISAPPEARED);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
        mContext.sendBroadcast(intent, BLUETOOTH_PERM);
    }

    private void onDeviceDisconnectRequested(String deviceObjectPath) {
        String address = mBluetoothService.getAddressFromObjectPath(deviceObjectPath);
        if (address == null) {
            Log.e(TAG, "onDeviceDisconnectRequested: Address of the remote device in null");
            return;
        }
        Intent intent = new Intent(BluetoothDevice.ACTION_ACL_DISCONNECT_REQUESTED);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
        mContext.sendBroadcast(intent, BLUETOOTH_PERM);
    }

    private void onCreatePairedDeviceResult(String address, int result) {
        address = address.toUpperCase();
        mBluetoothService.onCreatePairedDeviceResult(address, result);
    }

    private void onDeviceCreated(String deviceObjectPath) {
        String address = mBluetoothService.getAddressFromObjectPath(deviceObjectPath);
        if (!mBluetoothService.isRemoteDeviceInCache(address)) {
            // Incoming connection, we haven't seen this device, add to cache.

            String[] properties = mBluetoothService.getRemoteDeviceProperties(address);
            if (properties != null) {
                addDevice(address, properties);
            }
        }
        return;
    }

    private void onDeviceRemoved(String deviceObjectPath) {
        String address = mBluetoothService.getAddressFromObjectPath(deviceObjectPath);
        if (address != null) {
            mBluetoothService.setBondState(address.toUpperCase(), BluetoothDevice.BOND_NONE,
                BluetoothDevice.UNBOND_REASON_REMOVED);
            mBluetoothService.setRemoteDeviceProperty(address, "UUIDs", null);
            mBluetoothService.setRemoteDeviceProperty(address, "Services", null);
            mBluetoothService.setRemoteDeviceProperty(address, "Trusted", "false");
            mBluetoothService.clearRemoteDeviceGattServices(address);
        }
    }

    /*package*/ void onPropertyChanged(String[] propValues) {
        if (mBluetoothService.isAdapterPropertiesEmpty()) {
            // We have got a property change before
            // we filled up our cache.
            mBluetoothService.getAllProperties();
        }
        String name = propValues[0];

        if (name.equals("Name")) {
            mBluetoothService.setProperty(name, propValues[1]);
            Intent intent = new Intent(BluetoothAdapter.ACTION_LOCAL_NAME_CHANGED);
            intent.putExtra(BluetoothAdapter.EXTRA_LOCAL_NAME, propValues[1]);
            mContext.sendBroadcast(intent, BLUETOOTH_PERM);
        } else if (name.equals("Pairable") || name.equals("Discoverable")) {
            String pairable = name.equals("Pairable") ? propValues[1] :
                mBluetoothService.getPropertyInternal("Pairable");
            String discoverable = name.equals("Discoverable") ? propValues[1] :
                mBluetoothService.getPropertyInternal("Discoverable");

            // This shouldn't happen, unless Adapter Properties are null.
            if (pairable == null || discoverable == null)
                return;

            mBluetoothService.setProperty(name, propValues[1]);
            int mode = BluetoothService.bluezStringToScanMode(
                    pairable.equals("true"),
                    discoverable.equals("true"));
            if (mode >= 0) {
                Intent intent = new Intent(BluetoothAdapter.ACTION_SCAN_MODE_CHANGED);
                intent.putExtra(BluetoothAdapter.EXTRA_SCAN_MODE, mode);
                intent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY_BEFORE_BOOT);
                mContext.sendBroadcast(intent, BLUETOOTH_PERM);
            }
        } else if (name.equals("Discovering")) {
            Intent intent;
            mBluetoothService.setProperty(name, propValues[1]);
            if (propValues[1].equals("true")) {
                mBluetoothService.setIsDiscovering(true);
                intent = new Intent(BluetoothAdapter.ACTION_DISCOVERY_STARTED);
            } else {
                // Stop the discovery.
                mBluetoothService.cancelDiscovery();
                mBluetoothService.setIsDiscovering(false);
                intent = new Intent(BluetoothAdapter.ACTION_DISCOVERY_FINISHED);
            }
            mContext.sendBroadcast(intent, BLUETOOTH_PERM);
        } else if (name.equals("Devices")) {
            String value = null;
            int len = Integer.valueOf(propValues[1]);
            if (len > 0) {
                StringBuilder str = new StringBuilder();
                for (int i = 2; i < propValues.length; i++) {
                    str.append(propValues[i]);
                    str.append(",");
                }
                value = str.toString();
            }
            mBluetoothService.setProperty(name, value);
        } else if (name.equals("Powered")) {
            // bluetoothd has restarted, re-read all our properties.
            // Note: bluez only sends this property change when it restarts.
            if (propValues[1].equals("true"))
                onRestartRequired();
        }
    }

    private void onDevicePropertyChanged(String deviceObjectPath, String[] propValues) {
        if (!mBluetoothService.isEnabled()) {
            Log.e(TAG, "Bluetooth is not enabled");
            return;
        }

        String name = propValues[0];
        String address = mBluetoothService.getAddressFromObjectPath(deviceObjectPath);
        if (address == null) {
            Log.e(TAG, "onDevicePropertyChanged: Address of the remote device in null");
            return;
        }
        if (DBG) {
            log("Device property changed:" + address + ", property: " + name);
        }
        BluetoothDevice device = mAdapter.getRemoteDevice(address);
        if (name.equals("Name")) {
            mBluetoothService.setRemoteDeviceProperty(address, name, propValues[1]);
            Intent intent = new Intent(BluetoothDevice.ACTION_NAME_CHANGED);
            intent.putExtra(BluetoothDevice.EXTRA_DEVICE, device);
            intent.putExtra(BluetoothDevice.EXTRA_NAME, propValues[1]);
            mContext.sendBroadcast(intent, BLUETOOTH_PERM);
        } else if (name.equals("Class")) {
            mBluetoothService.setRemoteDeviceProperty(address, name, propValues[1]);
            Intent intent = new Intent(BluetoothDevice.ACTION_CLASS_CHANGED);
            intent.putExtra(BluetoothDevice.EXTRA_DEVICE, device);
            intent.putExtra(BluetoothDevice.EXTRA_CLASS,
                    new BluetoothClass(Integer.valueOf(propValues[1])));
            mContext.sendBroadcast(intent, BLUETOOTH_PERM);
        } else if (name.equals("Connected")) {
            Log.d(TAG, "Device property Connected: " + propValues[1]);

            mBluetoothService.setRemoteDeviceProperty(address, name, propValues[1]);
            Intent intent = null;
            if (propValues[1].equals("true")) {
                intent = new Intent(BluetoothDevice.ACTION_ACL_CONNECTED);
                // Set the link timeout to 8000 slots (5 sec timeout)
                // for bluetooth docks.
                if (mBluetoothService.isBluetoothDock(address)) {
                    mBluetoothService.setLinkTimeout(address, 8000);
                }
            } else {
                // Check and clean-up if bonding is in progress
                if (mBluetoothService.getBondState(address) ==
                        BluetoothDevice.BOND_BONDING) {
                    mBluetoothService.setBondState(address,
                          BluetoothDevice.BOND_NONE);
                }

                intent = new Intent(BluetoothDevice.ACTION_ACL_DISCONNECTED);
            }
            intent.putExtra(BluetoothDevice.EXTRA_DEVICE, device);
            mContext.sendBroadcast(intent, BLUETOOTH_PERM);
        } else if (name.equals("UUIDs")) {
            String uuid = null;
            int len = Integer.valueOf(propValues[1]);
            if (len > 0) {
                StringBuilder str = new StringBuilder();
                for (int i = 2; i < propValues.length; i++) {
                    str.append(propValues[i]);
                    str.append(",");
                }
                uuid = str.toString();
            }
            mBluetoothService.setRemoteDeviceProperty(address, name, uuid);

            // UUIDs have changed, query remote service channel and update cache.
            mBluetoothService.updateDeviceServiceChannelCache(address);

            mBluetoothService.sendUuidIntent(address);
        } else if (name.equals("Services")) {
            String services = null;
            int len = Integer.valueOf(propValues[1]);
            if (len > 0) {
                StringBuilder str = new StringBuilder();
                for (int i = 2; i < propValues.length; i++) {
                    str.append(propValues[i]);
                    str.append(",");
                }
                services = str.toString();
            }
            mBluetoothService.setRemoteDeviceProperty(address, name, services);

            mBluetoothService.sendGattIntent(address, BluetoothDevice.GATT_RESULT_SUCCESS);

        } else if (name.equals("Paired")) {
            if (propValues[1].equals("true")) {
                // If locally initiated pairing, we will
                // not go to BOND_BONDED state until we have received a
                // successful return value in onCreatePairedDeviceResult

                // There can be one outgoing and one incoming pairing at the same time
                if (!address.equals(mBluetoothService.getPendingOutgoingBonding())) {
                    mBluetoothService.setBondState(address, BluetoothDevice.BOND_BONDED);
                }
            } else {
                mBluetoothService.setBondState(address, BluetoothDevice.BOND_NONE);
                mBluetoothService.setRemoteDeviceProperty(address, "Trusted", "false");
            }
        } else if (name.equals("Trusted")) {
            if (DBG)
                log("set trust state succeded, value is  " + propValues[1]);
            mBluetoothService.setRemoteDeviceProperty(address, name, propValues[1]);
        }
    }

    private String checkAuthorizationRequestAndGetAddress(String objectPath, int nativeData) {
        String address = mBluetoothService.getAddressFromObjectPath(objectPath);
        if (address == null) {
            Log.e(TAG, "Unable to get device address in checkAuthorizationRequestAndGetAddress, " +
                  "returning null");
            return null;
        }
        address = address.toUpperCase();
        mAuthorizationRequestData.put(address, new Integer(nativeData));

        if (mBluetoothService.getBluetoothState() == BluetoothAdapter.STATE_TURNING_OFF) {
            // shutdown path
            mBluetoothService.sapAuthorize(address, false);
            return null;
        }
        return address;
    }

    private String checkPairingRequestAndGetAddress(String objectPath, int nativeData) {
        String address = mBluetoothService.getAddressFromObjectPath(objectPath);
        if (address == null) {
            Log.e(TAG, "Unable to get device address in checkPairingRequestAndGetAddress, " +
                  "returning null");
            return null;
        }
        address = address.toUpperCase();
        mPasskeyAgentRequestData.put(address, new Integer(nativeData));

        if (mBluetoothService.getBluetoothState() == BluetoothAdapter.STATE_TURNING_OFF) {
            // shutdown path
            mBluetoothService.cancelPairingUserInput(address);
            return null;
        }
        // Set state to BONDING. For incoming connections it will be set here.
        // For outgoing connections, it gets set when we call createBond.
        // Also set it only when the state is not already Bonded, we can sometimes
        // get an authorization request from the remote end if it doesn't have the link key
        // while we still have it.
        if (mBluetoothService.getBondState(address) != BluetoothDevice.BOND_BONDED)
            mBluetoothService.setBondState(address, BluetoothDevice.BOND_BONDING);
        return address;
    }

    private void onRequestPairingConsent(String objectPath, int nativeData) {
        String address = checkPairingRequestAndGetAddress(objectPath, nativeData);
        if (address == null) return;

        /* The link key will not be stored if the incoming request has MITM
         * protection switched on. Unfortunately, some devices have MITM
         * switched on even though their capabilities are NoInputNoOutput,
         * so we may get this request many times. Also if we respond immediately,
         * the other end is unable to handle it. Delay sending the message.
         */
        if (mBluetoothService.getBondState(address) == BluetoothDevice.BOND_BONDED) {
            Message message = mHandler.obtainMessage(EVENT_PAIRING_CONSENT_DELAYED_ACCEPT);
            message.obj = address;
            mHandler.sendMessageDelayed(message, 1500);
            return;
        }

        Intent intent = new Intent(BluetoothDevice.ACTION_PAIRING_REQUEST);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
        intent.putExtra(BluetoothDevice.EXTRA_PAIRING_VARIANT,
                        BluetoothDevice.PAIRING_VARIANT_CONSENT);
        mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);
        return;
    }

    private void onRequestPasskeyConfirmation(String objectPath, int passkey, int nativeData) {
        String address = checkPairingRequestAndGetAddress(objectPath, nativeData);
        if (address == null) return;

        Intent intent = new Intent(BluetoothDevice.ACTION_PAIRING_REQUEST);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
        intent.putExtra(BluetoothDevice.EXTRA_PASSKEY, passkey);
        intent.putExtra(BluetoothDevice.EXTRA_PAIRING_VARIANT,
                BluetoothDevice.PAIRING_VARIANT_PASSKEY_CONFIRMATION);
        mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);
        return;
    }

    private void onRequestPasskey(String objectPath, int nativeData) {
        String address = checkPairingRequestAndGetAddress(objectPath, nativeData);
        if (address == null) return;

        Intent intent = new Intent(BluetoothDevice.ACTION_PAIRING_REQUEST);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
        intent.putExtra(BluetoothDevice.EXTRA_PAIRING_VARIANT,
                BluetoothDevice.PAIRING_VARIANT_PASSKEY);
        mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);
        return;
    }

    private void onSapAuthorize(String objectPath, String uuid, int nativeData) {
        Log.i(TAG, "onSapAuthorize" + objectPath + uuid);
        String address = checkAuthorizationRequestAndGetAddress(objectPath, nativeData);
        if (address == null) {
            Log.e(TAG, "address is null");
            return;
        }
        /*Get the Trust state of the device*/
        boolean trusted = mBluetoothService.getTrustState(address);
        if (trusted) {
            /*Say as authorized to lower layers without popping up to
            user*/
            mBluetoothService.sapAuthorize(address, true);
        } else {
            Intent intent = new Intent(BluetoothService.SAP_AUTHORIZE_INTENT);
            intent.putExtra("name", mBluetoothService.getRemoteName(address));
            intent.putExtra("address", address);
            mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);
        }
    }
    private void onSapStateChanged(String objectPath, String state, int nativeData) {
        Log.i(TAG, "onSapStateChanged" + objectPath + state);

        String address = mBluetoothService.getAddressFromObjectPath(objectPath);
        if (address == null) {
            Log.e(TAG, "Unable to get device address , " +
                  "returning null");
            return;
        }
        address = address.toUpperCase();

        int sapState;
        if(state.equals("Connected")) {
                sapState = 1;
        } else  {
                sapState = 0;
        }
        Intent intent = new Intent(BluetoothService.SAP_STATECHANGE_INTENT);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE,  mAdapter.getRemoteDevice(address));
        intent.putExtra("state", sapState);
        mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);
    }
    private void onRequestPinCode(String objectPath, int nativeData, boolean secure) {
        String address = checkPairingRequestAndGetAddress(objectPath, nativeData);
        if (address == null) return;
        Log.i(TAG, "Secure pairing is "+ secure);
        String pendingOutgoingAddress =
                mBluetoothService.getPendingOutgoingBonding();
        if (address.equals(pendingOutgoingAddress)) {
            // we initiated the bonding

            // Check if its a dock
            if (mBluetoothService.isBluetoothDock(address)) {
                String pin = mBluetoothService.getDockPin();
                mBluetoothService.setPin(address, BluetoothDevice.convertPinToBytes(pin));
                return;
            }

            BluetoothClass btClass = new BluetoothClass(mBluetoothService.getRemoteClass(address));

            // try 0000 once if the device looks dumb
            switch (btClass.getDeviceClass()) {
            case BluetoothClass.Device.AUDIO_VIDEO_WEARABLE_HEADSET:
            case BluetoothClass.Device.AUDIO_VIDEO_HANDSFREE:
            case BluetoothClass.Device.AUDIO_VIDEO_HEADPHONES:
            case BluetoothClass.Device.AUDIO_VIDEO_PORTABLE_AUDIO:
            case BluetoothClass.Device.AUDIO_VIDEO_CAR_AUDIO:
            case BluetoothClass.Device.AUDIO_VIDEO_HIFI_AUDIO:
                if (mBluetoothService.attemptAutoPair(address)) return;
           }
        }
        Intent intent = new Intent(BluetoothDevice.ACTION_PAIRING_REQUEST);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
        intent.putExtra(BluetoothDevice.EXTRA_PAIRING_VARIANT, BluetoothDevice.PAIRING_VARIANT_PIN);
        intent.putExtra(BluetoothDevice.EXTRA_SECURE_PAIRING, secure);
        mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);
        if (mHandler.hasMessages(EVENT_PAIRING_TIMEOUT)) {
            Log.d(TAG, "Ther is EVENT_PAIRING_TIMEOUT message");
            mHandler.removeMessages(EVENT_PAIRING_TIMEOUT);
        }
        if (!(address.equals(pendingOutgoingAddress))) {
            Message msg = mHandler.obtainMessage(EVENT_PAIRING_TIMEOUT);
            msg.obj = address;
            mHandler.sendMessageDelayed(msg, INCOMING_PAIRING_TIMEOUT);
            Log.d(TAG, "Queuing INCOMING_PAIRING_TIMEOUT msg");
        }

        return;
    }

    private void onDisplayPasskey(String objectPath, int passkey, int nativeData) {
        String address = checkPairingRequestAndGetAddress(objectPath, nativeData);
        if (address == null) return;

        Intent intent = new Intent(BluetoothDevice.ACTION_PAIRING_REQUEST);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
        intent.putExtra(BluetoothDevice.EXTRA_PASSKEY, passkey);
        intent.putExtra(BluetoothDevice.EXTRA_PAIRING_VARIANT,
                        BluetoothDevice.PAIRING_VARIANT_DISPLAY_PASSKEY);
        mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);
    }

    private void onRequestOobData(String objectPath , int nativeData) {
        String address = checkPairingRequestAndGetAddress(objectPath, nativeData);
        if (address == null) return;

        Intent intent = new Intent(BluetoothDevice.ACTION_PAIRING_REQUEST);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
        intent.putExtra(BluetoothDevice.EXTRA_PAIRING_VARIANT,
                BluetoothDevice.PAIRING_VARIANT_OOB_CONSENT);
        mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);
    }

    private void  onAgentAuthorize(String objectPath, String deviceUuid, int nativeData) {
        String address = mBluetoothService.getAddressFromObjectPath(objectPath);
        if (address == null) {
            Log.e(TAG, "Unable to get device address in onAuthAgentAuthorize");
            return;
        }

        boolean authorized = false;
        ParcelUuid uuid = ParcelUuid.fromString(deviceUuid);
        BluetoothA2dp a2dp = new BluetoothA2dp(mContext);

        BluetoothDevice device = mAdapter.getRemoteDevice(address);
        mAuthorizationAgentRequestData.put(address, new Integer(nativeData));

        // Bluez sends the UUID of the local service being accessed, _not_ the
        // remote service
        if (mBluetoothService.isEnabled() &&
                (BluetoothUuid.isAudioSource(uuid) || BluetoothUuid.isAvrcpTarget(uuid)
                        || BluetoothUuid.isAdvAudioDist(uuid)) &&
                        !isOtherSinkInNonDisconnectingState(address)) {
            authorized = a2dp.getSinkPriority(device) > BluetoothA2dp.PRIORITY_OFF;
            if (authorized) {
                Log.i(TAG, "First check pass for incoming A2DP / AVRCP connection from " + address);
                // Some headsets try to connect AVCTP before AVDTP - against the recommendation
                // If AVCTP connection fails, we get stuck in IncomingA2DP state in the state
                // machine.  We don't handle AVCTP signals currently. We only send
                // intents for AVDTP state changes. We need to handle both of them in
                // some cases. For now, just don't move to incoming state in this case.
                if (!BluetoothUuid.isAvrcpTarget(uuid)) {
                    mBluetoothService.notifyIncomingA2dpConnection(address);
                } else {
                // The below change will make sure A2DP connection is
                // established. If the remote device sends AVRCP connection
                // first DUT will accept the AVRCP connection and initiates
                // A2DP connection from handset.
                    a2dp.allowIncomingConnect(device, authorized);
                    Log.d(TAG,"AVRCP target connection");
                    mBluetoothService.notifyConnectA2dp(address);
                    return;
                }
            } else {
                Log.i(TAG, "Rejecting incoming A2DP / AVRCP connection from " + address);
            }
        } else {
            Log.i(TAG, "Rejecting incoming " + deviceUuid + " connection from " + address);
        }
        log("onAgentAuthorize(" + objectPath + ", " + deviceUuid + ") = " + authorized);
        if (!authorized) a2dp.allowIncomingConnect(device, authorized);
    }

    private boolean onAgentOutOfBandDataAvailable(String objectPath) {
        if (!mBluetoothService.isEnabled()) return false;

        String address = mBluetoothService.getAddressFromObjectPath(objectPath);
        if (address == null) return false;

        if (mBluetoothService.getDeviceOutOfBandData(
            mAdapter.getRemoteDevice(address)) != null) {
            return true;
        }
        return false;

    }

    private boolean isOtherSinkInNonDisconnectingState(String address) {
        BluetoothA2dp a2dp = new BluetoothA2dp(mContext);
        Set<BluetoothDevice> devices = a2dp.getNonDisconnectedSinks();
        if (devices.size() == 0) return false;
        for(BluetoothDevice dev: devices) {
            if (!dev.getAddress().equals(address)) return true;
        }
        return false;
    }

    private void onAgentCancel() {
        Intent intent = new Intent(BluetoothDevice.ACTION_PAIRING_CANCEL);
        mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);

        mHandler.sendMessageDelayed(mHandler.obtainMessage(EVENT_AGENT_CANCEL),
                   1500);

        return;
    }

    private void onDiscoverServicesResult(String deviceObjectPath, boolean result) {
        if (!mBluetoothService.isEnabled()) {
            log("Bluetooth is not on");
            return;
        }

        String address = mBluetoothService.getAddressFromObjectPath(deviceObjectPath);
        // We don't parse the xml here, instead just query Bluez for the properties.
        if (address == null) {
            Log.e(TAG, "Unexpected error! address is null");
            return;
        }
        if (result) {
            mBluetoothService.updateRemoteDevicePropertiesCache(address);
        }
        mBluetoothService.sendUuidIntent(address);
        mBluetoothService.makeServiceChannelCallbacks(address);
    }

    private void onCreateDeviceResult(String address, int result) {
        if (DBG) log("Result of onCreateDeviceResult:" + result);

        switch (result) {
        case CREATE_DEVICE_ALREADY_EXISTS:
            String path = mBluetoothService.getObjectPathFromAddress(address);
            if (path != null) {
                mBluetoothService.discoverServicesNative(path, "");
                break;
            }
            Log.w(TAG, "Device exists, but we dont have the bluez path, failing");
            // fall-through
        case CREATE_DEVICE_FAILED:
            mBluetoothService.sendUuidIntent(address);
            mBluetoothService.sendGattIntent(address, BluetoothDevice.GATT_RESULT_FAIL);
            mBluetoothService.makeServiceChannelCallbacks(address);
            break;
        case CREATE_DEVICE_SUCCESS:
            // nothing to do, UUID intent's will be sent via property changed

        }
    }

    private void onRestartRequired() {
        if (mBluetoothService.isEnabled()) {
            Log.e(TAG, "*** A serious error occurred (did bluetoothd crash?) - " +
                       "restarting Bluetooth ***");
            mHandler.sendEmptyMessage(EVENT_RESTART_BLUETOOTH);
        }
    }

    private void onDiscoverCharacteristicsResult(String serviceObjectPath, boolean result) {

        Log.d(TAG, "onDiscoverCharacteristicsResult: " + result);

        if (result) {
            mBluetoothService.updateGattServicePropertiesCache(serviceObjectPath);
        }
        mBluetoothService.makeDiscoverCharacteristicsCallback(serviceObjectPath, result);
    }

    private void onSetCharacteristicPropertyResult(String path, String property, boolean result) {

        Log.d(TAG, "onSetCharPropResult path " + path + " property = " + property);
        Log.d(TAG, "Result = " + result);
        mBluetoothService.makeSetCharacteristicPropertyCallback(path, property, result);
    }

    private void onWatcherValueChanged(String characteristicPath, String value) {
        // TODO: Send this to upper layer
        mBluetoothService.makeWatcherValueChangedCallback(characteristicPath, value);

    }

    private void onUpdateCharacteristicValueResult(String charObjectPath, boolean result) {

        mBluetoothService.makeUpdateCharacteristicValueCallback(charObjectPath, result);
    }

    private static void log(String msg) {
        Log.d(TAG, msg);
    }

    private native void initializeNativeDataNative();
    private native void startEventLoopNative();
    private native void stopEventLoopNative();
    private native boolean isEventLoopRunningNative();
    private native void cleanupNativeDataNative();
}
