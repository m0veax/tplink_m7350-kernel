/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2010, 2011 The Linux Foundation. All rights reserved.
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

/**
 * TODO: Move this to
 * java/services/com/android/server/BluetoothService.java
 * and make the contructor package private again.
 *
 * @hide
 */

package android.server;

import android.bluetooth.BluetoothA2dp;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothDeviceProfileState;
import android.bluetooth.BluetoothHeadset;
import android.bluetooth.BluetoothProfileState;
import android.bluetooth.BluetoothSocket;
import android.bluetooth.BluetoothUuid;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.IBluetooth;
import android.bluetooth.IBluetoothCallback;
import android.bluetooth.IBluetoothGattService;
import android.content.BroadcastReceiver;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.ParcelUuid;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.os.SystemService;
import android.os.SystemProperties;
import android.provider.Settings;
import android.util.Log;
import android.util.Pair;

import com.android.internal.app.IBatteryStats;

import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.DataInputStream;
import java.io.File;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.io.RandomAccessFile;
import java.io.UnsupportedEncodingException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.SortedMap;
import java.util.TreeMap;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.ListIterator;
import java.util.Map;
import java.util.NoSuchElementException;

public class BluetoothService extends IBluetooth.Stub {
    private static final String TAG = "BluetoothService";
    private static final boolean DBG = true;

    private int mNativeData;
    private BluetoothEventLoop mEventLoop;
    private boolean mIsAirplaneSensitive;
    private boolean mIsAirplaneToggleable;
    private int mBluetoothState;
    private boolean mRestart = false;  // need to call enable() after disable()
    private boolean mIsDiscovering;

    private BluetoothAdapter mAdapter;  // constant after init()
    private final BondState mBondState = new BondState();  // local cache of bondings
    private final IBatteryStats mBatteryStats;
    private final Context mContext;

    private static final String BLUETOOTH_ADMIN_PERM = android.Manifest.permission.BLUETOOTH_ADMIN;
    private static final String BLUETOOTH_PERM = android.Manifest.permission.BLUETOOTH;

    private static final String DOCK_ADDRESS_PATH = "/sys/class/switch/dock/bt_addr";
    private static final String DOCK_PIN_PATH = "/sys/class/switch/dock/bt_pin";

    private static final String SHARED_PREFERENCE_DOCK_ADDRESS = "dock_bluetooth_address";
    private static final String SHARED_PREFERENCES_NAME = "bluetooth_service_settings";

    private static final int MESSAGE_REGISTER_SDP_RECORDS = 1;
    private static final int MESSAGE_FINISH_DISABLE = 2;
    private static final int MESSAGE_UUID_INTENT = 3;
    private static final int MESSAGE_DISCOVERABLE_TIMEOUT = 4;
    private static final int MESSAGE_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY = 5;
    private static final int MESSAGE_START_DUN_SERVER = 6;
    private static final int MESSAGE_GATT_INTENT = 7;
    private static final int MESSAGE_START_SAP_SERVER = 8;
    private static final int MESSAGE_DISCOVERABLE_CHANGE_TIMEOUT = 10;

    // The time (in millisecs) to delay the pairing attempt after the first
    // auto pairing attempt fails. We use an exponential delay with
    // INIT_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY as the initial value and
    // MAX_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY as the max value.
    private static final long INIT_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY = 3000;
    private static final long MAX_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY = 12000;

    private static final int SDP_ATTR_PROTO_DESC_LIST = 0x0004;
    private static final int SDP_ATTR_GOEP_L2CAP_PSM = 0x0200;

    // The timeout used to sent the UUIDs Intent
    // This timeout should be greater than the page timeout
    // Some of devices takes more than 15 secons for SDP response,
    // Therefore, it is better set 30 seconds timeout instead of 6 seconds.
    private static final int UUID_INTENT_DELAY = 30000;

    // The timeout used to sent the GATT services Intent
    // This timeout should be greater than the page timeout
    private static final int GATT_INTENT_DELAY = 30000;

    public static final String SAP_AUTHORIZE_INTENT =
            "com.android.bluetooth.sap.accessrequest";
    public static final String SAP_STATECHANGE_INTENT =
            "com.android.bluetooth.sap.statechanged";
    public static final String SAP_ACCESS_ALLOWED_ACTION =
            "com.android.bluetooth.sap.accessallowed";
    public static final String SAP_EXTRA_ALWAYS_ALLOWED =
            "com.android.bluetooth.sap.alwaysallowed";
    public static final String SAP_ACCESS_DISALLOWED_ACTION =
            "com.android.bluetooth.sap.accessdisallowed";

    /** Always retrieve RFCOMM channel for these SDP UUIDs */
    private static final ParcelUuid[] RFCOMM_UUIDS = {
            BluetoothUuid.Handsfree,
            BluetoothUuid.HSP,
            BluetoothUuid.ObexObjectPush,
            BluetoothUuid.MessageNotificationServer,
            BluetoothUuid.DirectPrinting,
            BluetoothUuid.ReferencePrinting,
            BluetoothUuid.PrintingStatus };

    // TODO(): Optimize all these string handling
    private final Map<String, String> mAdapterProperties;
    private final HashMap<String, Map<String, String>> mDeviceProperties;

    private final HashMap<String, Map<String, String>> mGattProperties;

    private final HashMap<String, Map<ParcelUuid, Integer>> mDeviceServiceChannelCache;
    private final HashMap<String, Map<ParcelUuid, Integer>> mDeviceL2capPsmCache;
    private final HashMap<String, Map<String, String>> mDeviceFeatureCache;

    private final ArrayList<String> mUuidIntentTracker;
    private final HashMap<RemoteService, IBluetoothCallback> mUuidCallbackTracker;

    private final HashMap<String, ArrayList<ParcelUuid>> mGattIntentTracker;
    private final HashMap<String, IBluetoothGattService> mGattServiceTracker;
    private final HashMap<String, IBluetoothGattService> mGattWatcherTracker;

    private final SortedMap<String, Integer> mGattServices;

    private final HashMap<Integer, Integer> mServiceRecordToPid;

    private final HashMap<String, BluetoothDeviceProfileState> mDeviceProfileState;
    private final BluetoothProfileState mA2dpProfileState;
    private final BluetoothProfileState mHfpProfileState;

    private BluetoothA2dpService mA2dpService;
    private final HashMap<String, Pair<byte[], byte[]>> mDeviceOobData;

    private static String mDockAddress;
    private String mDockPin;
    private boolean mDUNenable = false;
    private boolean mSAPenable = false;
    private boolean mFTPenable = false;

    private static final String INCOMING_CONNECTION_FILE =
      "/data/misc/bluetooth/incoming_connection.conf";
    private HashMap<String, Pair<Integer, String>> mIncomingConnections;


    private static class RemoteService {
        public String address;
        public ParcelUuid uuid;
        public RemoteService(String address, ParcelUuid uuid) {
            this.address = address;
            this.uuid = uuid;
        }
        @Override
        public boolean equals(Object o) {
            if (o instanceof RemoteService) {
                RemoteService service = (RemoteService)o;
                return address.equals(service.address) && uuid.equals(service.uuid);
            }
            return false;
        }

        @Override
        public int hashCode() {
            int hash = 1;
            hash = hash * 31 + (address == null ? 0 : address.hashCode());
            hash = hash * 31 + (uuid == null ? 0 : uuid.hashCode());
            return hash;
        }
    }

    static {
        classInitNative();
    }

    public BluetoothService(Context context) {
        mContext = context;

        // Need to do this in place of:
        // mBatteryStats = BatteryStatsService.getService();
        // Since we can not import BatteryStatsService from here. This class really needs to be
        // moved to java/services/com/android/server/
        mBatteryStats = IBatteryStats.Stub.asInterface(ServiceManager.getService("batteryinfo"));

        initializeNativeDataNative();

        if (isEnabledNative() == 1) {
            Log.w(TAG, "Bluetooth daemons already running - runtime restart? ");
            disableNative();
        }

        mBluetoothState = BluetoothAdapter.STATE_OFF;
        mIsDiscovering = false;
        mAdapterProperties = new HashMap<String, String>();
        mDeviceProperties = new HashMap<String, Map<String,String>>();

        mGattProperties = new HashMap<String, Map<String,String>>();

        mDeviceServiceChannelCache = new HashMap<String, Map<ParcelUuid, Integer>>();
        mDeviceFeatureCache = new HashMap<String, Map<String, String>>();
        mDeviceOobData = new HashMap<String, Pair<byte[], byte[]>>();
        mDeviceL2capPsmCache = new HashMap<String, Map<ParcelUuid, Integer>>();
        mUuidIntentTracker = new ArrayList<String>();
        mUuidCallbackTracker = new HashMap<RemoteService, IBluetoothCallback>();
        mGattIntentTracker = new HashMap<String, ArrayList<ParcelUuid>>();
        mGattServiceTracker = new HashMap<String, IBluetoothGattService>();
        mGattWatcherTracker = new HashMap<String, IBluetoothGattService>();
        mGattServices = new TreeMap<String, Integer>();
        mServiceRecordToPid = new HashMap<Integer, Integer>();
        mDeviceProfileState = new HashMap<String, BluetoothDeviceProfileState>();
        mA2dpProfileState = new BluetoothProfileState(mContext, BluetoothProfileState.A2DP);
        mHfpProfileState = new BluetoothProfileState(mContext, BluetoothProfileState.HFP);

        mHfpProfileState.start();
        mA2dpProfileState.start();

        mConnectionManager = new ConnectionManager();

        IntentFilter filter = new IntentFilter();
        registerForAirplaneMode(filter);

        // Register for connection-oriented notifications
        filter.addAction(BluetoothHeadset.ACTION_AUDIO_STATE_CHANGED);
        filter.addAction(BluetoothA2dp.ACTION_SINK_STATE_CHANGED);

        filter.addAction(Intent.ACTION_DOCK_EVENT);
        filter.addAction(SAP_ACCESS_ALLOWED_ACTION);
        filter.addAction(SAP_ACCESS_DISALLOWED_ACTION);
        mContext.registerReceiver(mReceiver, filter);
        mIncomingConnections = new HashMap<String, Pair<Integer, String>>();
    }

    public static synchronized String readDockBluetoothAddress() {
        if (mDockAddress != null) return mDockAddress;

        BufferedInputStream file = null;
        String dockAddress;
        try {
            file = new BufferedInputStream(new FileInputStream(DOCK_ADDRESS_PATH));
            byte[] address = new byte[17];
            file.read(address);
            dockAddress = new String(address);
            dockAddress = dockAddress.toUpperCase();
            if (BluetoothAdapter.checkBluetoothAddress(dockAddress)) {
                mDockAddress = dockAddress;
                return mDockAddress;
            } else {
                log("CheckBluetoothAddress failed for car dock address:" + dockAddress);
            }
        } catch (FileNotFoundException e) {
            log("FileNotFoundException while trying to read dock address");
        } catch (IOException e) {
            log("IOException while trying to read dock address");
        } finally {
            if (file != null) {
                try {
                    file.close();
                } catch (IOException e) {
                    // Ignore
                }
            }
        }
        mDockAddress = null;
        return null;
    }

    private synchronized boolean writeDockPin() {
        BufferedWriter out = null;
        try {
            out = new BufferedWriter(new FileWriter(DOCK_PIN_PATH));

            // Generate a random 4 digit pin between 0000 and 9999
            // This is not truly random but good enough for our purposes.
            int pin = (int) Math.floor(Math.random() * 10000);

            mDockPin = String.format("%04d", pin);
            out.write(mDockPin);
            return true;
        } catch (FileNotFoundException e) {
            log("FileNotFoundException while trying to write dock pairing pin");
        } catch (IOException e) {
            log("IOException while while trying to write dock pairing pin");
        } finally {
            if (out != null) {
                try {
                    out.close();
                } catch (IOException e) {
                    // Ignore
                }
            }
        }
        mDockPin = null;
        return false;
    }

    /*package*/ synchronized String getDockPin() {
        return mDockPin;
    }

    public synchronized void initAfterRegistration() {
        mAdapter = BluetoothAdapter.getDefaultAdapter();
        mEventLoop = new BluetoothEventLoop(mContext, mAdapter, this);
    }

    @Override
    protected void finalize() throws Throwable {
        mContext.unregisterReceiver(mReceiver);
        try {
            cleanupNativeDataNative();
        } finally {
            super.finalize();
        }
    }

    public boolean isEnabled() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        return isEnabledInternal();
    }

    private boolean isEnabledInternal() {
        return mBluetoothState == BluetoothAdapter.STATE_ON;
    }

    public int getBluetoothState() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        return mBluetoothState;
    }


    /**
     * Bring down bluetooth and disable BT in settings. Returns true on success.
     */
    public boolean disable() {
        return disable(true);
    }

    /**
     * Bring down bluetooth. Returns true on success.
     *
     * @param saveSetting If true, persist the new setting
     */
    public synchronized boolean disable(boolean saveSetting) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM, "Need BLUETOOTH_ADMIN permission");
        int disconnDelay = 3000;
        int conn = 0;

        switch (mBluetoothState) {
        case BluetoothAdapter.STATE_OFF:
            return true;
        case BluetoothAdapter.STATE_ON:
            break;
        default:
            return false;
        }
        if (mEnableThread != null && mEnableThread.isAlive()) {
            return false;
        }
        setBluetoothState(BluetoothAdapter.STATE_TURNING_OFF);
        mHandler.removeMessages(MESSAGE_REGISTER_SDP_RECORDS);
        mHandler.removeMessages(MESSAGE_START_DUN_SERVER);
        mHandler.removeMessages(MESSAGE_DISCOVERABLE_TIMEOUT);

        // Allow 3 second for profiles to gracefully disconnect
        // TODO: Introduce a callback mechanism so that each profile can notify
        // BluetoothService when it is done shutting down

        conn = listConnectionNative();
        Log.d(TAG,"conn = "+conn);
        if(conn == 0)
            disconnDelay = 2000;
        mHandler.sendMessageDelayed(
                mHandler.obtainMessage(MESSAGE_FINISH_DISABLE, saveSetting ? 1 : 0, 0),disconnDelay);
        return true;
    }


    private synchronized void finishDisable(boolean saveSetting) {
        if (mBluetoothState != BluetoothAdapter.STATE_TURNING_OFF) {
            return;
        }

        if (mDUNenable == true) {
            Log.d(TAG, "Stopping BT-DUN server");
            SystemService.stop("bt-dun");
        }

        if (mSAPenable == true) {
            Log.d(TAG, "Stopping BT SAP server");
            SystemService.stop("bt-sap");
        }
        mEventLoop.stop();
        tearDownNativeDataNative();
        disableNative();

        // mark in progress bondings as cancelled
        for (String address : mBondState.listInState(BluetoothDevice.BOND_BONDING)) {
            mBondState.setBondState(address, BluetoothDevice.BOND_NONE,
                                    BluetoothDevice.UNBOND_REASON_AUTH_CANCELED);
        }

        // update mode
        Intent intent = new Intent(BluetoothAdapter.ACTION_SCAN_MODE_CHANGED);
        intent.putExtra(BluetoothAdapter.EXTRA_SCAN_MODE, BluetoothAdapter.SCAN_MODE_NONE);
        mContext.sendBroadcast(intent, BLUETOOTH_PERM);

        mIsDiscovering = false;
        mAdapterProperties.clear();
        mServiceRecordToPid.clear();

        if (saveSetting) {
            persistBluetoothOnSetting(false);
        }

        setBluetoothState(BluetoothAdapter.STATE_OFF);

        // Log bluetooth off to battery stats.
        long ident = Binder.clearCallingIdentity();
        try {
            mBatteryStats.noteBluetoothOff();
        } catch (RemoteException e) {
        } finally {
            Binder.restoreCallingIdentity(ident);
        }

        if (mRestart) {
            mRestart = false;
            enable();
        }
    }

    /** Bring up BT and persist BT on in settings */
    public boolean enable() {
        return enable(true);
    }

    /**
     * Enable this Bluetooth device, asynchronously.
     * This turns on/off the underlying hardware.
     *
     * @param saveSetting If true, persist the new state of BT in settings
     * @return True on success (so far)
     */
    public synchronized boolean enable(boolean saveSetting) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");

        // Airplane mode can prevent Bluetooth radio from being turned on.
        if (mIsAirplaneSensitive && isAirplaneModeOn() && !mIsAirplaneToggleable) {
            return false;
        }
        if (mBluetoothState != BluetoothAdapter.STATE_OFF) {
            return false;
        }
        if (mEnableThread != null && mEnableThread.isAlive()) {
            return false;
        }
        if (SystemProperties.getBoolean("ro.qualcomm.bluetooth.dun", false)) {
            mDUNenable = true;
        }
        if (SystemProperties.getBoolean("ro.qualcomm.bluetooth.ftp", false)) {
            mFTPenable = true;
        }
        if (SystemProperties.getBoolean("ro.qualcomm.bluetooth.sap", false)) {
            mSAPenable = true;
        }
        setBluetoothState(BluetoothAdapter.STATE_TURNING_ON);
        mEnableThread = new EnableThread(saveSetting);
        mEnableThread.start();
        return true;
    }

    /** Forcibly restart Bluetooth if it is on */
    /* package */ synchronized void restart() {
        if (mBluetoothState != BluetoothAdapter.STATE_ON) {
            return;
        }
        mRestart = true;
        if (!disable(false)) {
            mRestart = false;
        }
    }

    private synchronized void setBluetoothState(int state) {
        if (state == mBluetoothState) {
            return;
        }

        if (DBG) log("Bluetooth state " + mBluetoothState + " -> " + state);

        Intent intent = new Intent(BluetoothAdapter.ACTION_STATE_CHANGED);
        intent.putExtra(BluetoothAdapter.EXTRA_PREVIOUS_STATE, mBluetoothState);
        intent.putExtra(BluetoothAdapter.EXTRA_STATE, state);
        intent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY_BEFORE_BOOT);

        mBluetoothState = state;

        mContext.sendBroadcast(intent, BLUETOOTH_PERM);
    }

    private final Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
            case MESSAGE_REGISTER_SDP_RECORDS:
                if (!isEnabledInternal()) {
                    return;
                }
                // SystemService.start() forks sdptool to register service
                // records. It can fail to register some records if it is
                // forked multiple times in a row, probably because there is
                // some race in sdptool or bluez when operated in parallel.
                // As a workaround, delay 500ms between each fork of sdptool.
                // TODO: Don't fork sdptool in order to register service
                // records, use a DBUS call instead.
                switch (msg.arg1) {
                case 1:
                    Log.d(TAG, "Registering hfag record");
                    SystemService.start("hfag");
                    mHandler.sendMessageDelayed(
                            mHandler.obtainMessage(MESSAGE_REGISTER_SDP_RECORDS, 2, -1), 500);
                    break;
                case 2:
                    Log.d(TAG, "Registering hsag record");
                    SystemService.start("hsag");
                    mHandler.sendMessageDelayed(
                            mHandler.obtainMessage(MESSAGE_REGISTER_SDP_RECORDS, 3, -1), 500);
                    break;
                case 3:
                    Log.d(TAG, "Registering opush record");
                    SystemService.start("opush");
                    mHandler.sendMessageDelayed(
                            mHandler.obtainMessage(MESSAGE_REGISTER_SDP_RECORDS, 4, -1), 500);
                    break;
                case 4:
                    Log.d(TAG, "Registering pbap record");
                    SystemService.start("pbap");
                    mHandler.sendMessageDelayed(
                            mHandler.obtainMessage(MESSAGE_REGISTER_SDP_RECORDS, 5, -1), 500);
                    break;
                case 5:
                    Log.d(TAG, "Registering map record instance id 0");
                    SystemService.start("map0");
                    mHandler.sendMessageDelayed(
                            mHandler.obtainMessage(MESSAGE_REGISTER_SDP_RECORDS, 6, -1), 500);
                    break;
                case 6:
                    if (mDUNenable == true) {
                        Log.d(TAG, "Registering dun record");
                        SystemService.start("dund");
                    }
                    mHandler.sendMessageDelayed(
                           mHandler.obtainMessage(MESSAGE_REGISTER_SDP_RECORDS, 7, -1), 500);
                    break;
                case 7:
                    if (mFTPenable == true) {
                        Log.d(TAG, "Registering ftp record");
                        SystemService.start("ftp");
                    }
                    mHandler.sendMessageDelayed(
                            mHandler.obtainMessage(MESSAGE_REGISTER_SDP_RECORDS, 8, -1), 500);
                    break;
                case 8:
                    Log.d(TAG, "Registering map record instance id 1");
                    SystemService.start("map1");
                    mHandler.sendMessageDelayed(
                           mHandler.obtainMessage(MESSAGE_REGISTER_SDP_RECORDS, 9, -1), 500);
                    break;
                case 9:
                   if (mSAPenable == true) {
                       Log.d(TAG, "Registering SAP record");
                       SystemService.start("sapd");
                   }
                }
                break;
            case MESSAGE_FINISH_DISABLE:
                finishDisable(msg.arg1 != 0);
                break;
            case MESSAGE_UUID_INTENT:
                String address = (String)msg.obj;
                if (address != null) {
                    sendUuidIntent(address);
                    makeServiceChannelCallbacks(address);
                }
                break;
            case MESSAGE_DISCOVERABLE_TIMEOUT:
                int mode = msg.arg1;
                if (isEnabledInternal()) {
                    // TODO: Switch back to the previous scan mode
                    // This is ok for now, because we only use
                    // CONNECTABLE and CONNECTABLE_DISCOVERABLE
                    setScanMode(BluetoothAdapter.SCAN_MODE_CONNECTABLE, -1);
                }
                break;
            case MESSAGE_START_DUN_SERVER:
                if (mDUNenable == true) {
                    Log.d(TAG, "Starting BT-DUN server");
                    SystemService.start("bt-dun");
                }
                break;
            case MESSAGE_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY:
                address = (String)msg.obj;
                if (address != null) {
                    if (true == createBond(address)) {
                       setBondState(address,
                            BluetoothDevice.BOND_RETRY,
                            BluetoothDevice.UNBOND_REASON_AUTH_FAILED);
                    }
                }
                break;
            case MESSAGE_GATT_INTENT:
                address = (String)msg.obj;
                if (address != null && mGattIntentTracker.containsKey(address)){
                    sendGattIntent(address, BluetoothDevice.GATT_RESULT_TIMEOUT);
                }
                break;
            case MESSAGE_START_SAP_SERVER:
                if (mSAPenable == true) {
                    Log.d(TAG, "Starting BT-SAP server");
                    SystemService.start("bt-sap");
                }
                break;
            case MESSAGE_DISCOVERABLE_CHANGE_TIMEOUT:
                Log.i(TAG, "discoverable change timeout");
                if (getScanMode() != BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE) {
                    getAllProperties();
                    int newMode = getScanMode();
                    if (newMode == BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE) {
                        Intent intent = new Intent(BluetoothAdapter.ACTION_SCAN_MODE_CHANGED);
                        intent.putExtra(BluetoothAdapter.EXTRA_SCAN_MODE, newMode);
                        intent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY_BEFORE_BOOT);
                        mContext.sendBroadcast(intent, BLUETOOTH_PERM);
                    }
                }
                break;
            }
        }
    };

    private EnableThread mEnableThread;

    private class EnableThread extends Thread {
        private final boolean mSaveSetting;
        public EnableThread(boolean saveSetting) {
            mSaveSetting = saveSetting;
        }
        public void run() {
            boolean res = (enableNative() == 0);
            if (res) {
                int retryCount = 2;
                boolean running = false;
                while ((retryCount-- > 0) && !running) {
                    mEventLoop.start();
                    // it may take a momement for the other thread to do its
                    // thing.  Check periodically for a while.
                    int pollCount = 5;
                    while ((pollCount-- > 0) && !running) {
                        if (mEventLoop.isEventLoopRunning()) {
                            running = true;
                            break;
                        }
                        try {
                            Thread.sleep(100);
                        } catch (InterruptedException e) {}
                    }
                }
                if (!running) {
                    log("bt EnableThread giving up");
                    res = false;
                    disableNative();
                }
            }


            if (res) {
                if (!setupNativeDataNative()) {
                    return;
                }
                if (mSaveSetting) {
                    persistBluetoothOnSetting(true);
                }
                mIsDiscovering = false;
                mBondState.readAutoPairingData();
                mBondState.loadBondState();
                initProfileState();

                if (mDUNenable == true) {
                    mHandler.sendMessage(
                            mHandler.obtainMessage(MESSAGE_START_DUN_SERVER));
                }

                if (mSAPenable == true) {
                    mHandler.sendMessage(
                            mHandler.obtainMessage(MESSAGE_START_SAP_SERVER));
                }

                mHandler.sendMessageDelayed(
                        mHandler.obtainMessage(MESSAGE_REGISTER_SDP_RECORDS, 1, -1), 3000);

                // Log bluetooth on to battery stats.
                long ident = Binder.clearCallingIdentity();
                try {
                    mBatteryStats.noteBluetoothOn();
                } catch (RemoteException e) {
                } finally {
                    Binder.restoreCallingIdentity(ident);
                }
            }

            mEnableThread = null;

            setBluetoothState(res ?
                              BluetoothAdapter.STATE_ON :
                              BluetoothAdapter.STATE_OFF);

            if (res) {
                // Update mode
                String[] propVal = {"Pairable", getProperty("Pairable")};
                mEventLoop.onPropertyChanged(propVal);
            }

            if (mIsAirplaneSensitive && isAirplaneModeOn() && !mIsAirplaneToggleable) {
                disable(false);
            }

        }
    }

    private ConnectionManager mConnectionManager;

    // Track system Bluetooth profile connection state.  A centralized location to
    // control and enforce profile concurrency policies (e.g., do X when audio activity
    // is present)
    private class ConnectionManager {
        private LinkedList<BtPolicyCallback> mConnectionList = new LinkedList<BtPolicyCallback>();
        private boolean mScoAudioActive = false;
        private boolean mA2dpAudioActive = false;
        private boolean mUseWifiForBtTransfers = true;

        private int mBtPolicyHandle = 1;
        private LinkedList<Integer> mBtPolicyHandlesAvailable = new LinkedList<Integer>();

        private class BtPolicyCallback {
            IBluetoothCallback mCallback;
            int mDesiredAmpPolicy = BluetoothSocket.BT_AMP_POLICY_REQUIRE_BR_EDR;
            int mCurrentAmpPolicy = BluetoothSocket.BT_AMP_POLICY_REQUIRE_BR_EDR;
            int mHandle = 0;

            boolean removeHandle(int handle) {
                Log.i(TAG, "Deallocating handle " + Integer.toString(mHandle) + " to track BT policy.");
                mHandle = 0;
                return mBtPolicyHandlesAvailable.add(handle);
            }

            int getHandle() {
                return mHandle;
            }

            BtPolicyCallback(IBluetoothCallback newCallback, int newDesiredAmpPolicy) {
                this.mCallback = newCallback;
                this.mDesiredAmpPolicy = validateAmpPolicy(newDesiredAmpPolicy);
                this.mCurrentAmpPolicy = getEffectiveAmpPolicy(newDesiredAmpPolicy);

                /* Allocate a handle */
                try {
                    mHandle = mBtPolicyHandlesAvailable.getFirst();
                } catch (NoSuchElementException e) {
                    if (mBtPolicyHandle < Integer.MAX_VALUE) {
                        mHandle = ++mBtPolicyHandle;
                        Log.i(TAG, "Allocating handle " + Integer.toString(mHandle) + " to track BT policy.");
                    } else {
                        Log.e(TAG, "Trouble finding open handle to track BT policy!");
                    }
                }
            }
        }

        private int validateAmpPolicy(int policy) {
            if (policy == BluetoothSocket.BT_AMP_POLICY_PREFER_BR_EDR ||
                    policy == BluetoothSocket.BT_AMP_POLICY_PREFER_AMP) {
                return policy;
            }
            return BluetoothSocket.BT_AMP_POLICY_REQUIRE_BR_EDR;
        }

        public synchronized int getEffectiveAmpPolicy(int policy) {
            if (mScoAudioActive || mA2dpAudioActive || !mUseWifiForBtTransfers) {
                return BluetoothSocket.BT_AMP_POLICY_REQUIRE_BR_EDR;
            }
            return validateAmpPolicy(policy);
        }

        public synchronized int registerEl2capConnection(IBluetoothCallback callback, int initialPolicy) {
            BtPolicyCallback bpc = new BtPolicyCallback(callback, initialPolicy);
            mConnectionList.add(bpc);
            return bpc.getHandle();
        }

        public synchronized void deregisterEl2capConnection(int handle) {
            ListIterator<BtPolicyCallback> it = mConnectionList.listIterator();
            while (it.hasNext()) {
                BtPolicyCallback thisBtPolicyCallback = it.next();
                if (thisBtPolicyCallback.mHandle == handle) {
                    thisBtPolicyCallback.removeHandle(handle);
                    mConnectionList.remove(thisBtPolicyCallback);
                    break;
                }
            }
        }

        public synchronized boolean setDesiredAmpPolicy(int handle, int policy) {
            boolean result = true;
            ListIterator<BtPolicyCallback> it = mConnectionList.listIterator();
            policy = validateAmpPolicy(policy);
            int allowedPolicy = getEffectiveAmpPolicy(policy);
            while (it.hasNext()) {
                BtPolicyCallback thisBtPolicyCallback = it.next();
                if (thisBtPolicyCallback.mHandle == handle) {
                    if (thisBtPolicyCallback.mCurrentAmpPolicy != allowedPolicy) {
                        try {
                            thisBtPolicyCallback.mCallback.onAmpPolicyChange(allowedPolicy);
                            thisBtPolicyCallback.mCurrentAmpPolicy = allowedPolicy;
                            thisBtPolicyCallback.mDesiredAmpPolicy = policy;
                        } catch (RemoteException e) {
                            Log.e(TAG, "", e);
                            result = false;
                        }
                    }
                    break;
                }
            }
            return result;
        }

        public synchronized void setScoAudioActive(boolean active) {
            Log.d(TAG, "setScoAudioActive(): " + active);

            if (mScoAudioActive != active) {
                mScoAudioActive = active;
                updateConnectionAmpPolicies();
            }
        }

        public synchronized void setA2dpAudioActive(boolean active) {
            Log.d(TAG, "setA2dpAudioActive(): " + active);

            if (mA2dpAudioActive != active) {
                mA2dpAudioActive = active;
                updateConnectionAmpPolicies();
            }
        }

        public synchronized void setUseWifiForBtTransfers(boolean useWifi) {
            Log.d(TAG, "setUseWifiForBtTransfers(): " + useWifi);

            if (mUseWifiForBtTransfers != useWifi) {
                mUseWifiForBtTransfers = useWifi;
                updateConnectionAmpPolicies();
            }
        }

        // call when anything happens that can change an AMP policy
        private boolean updateConnectionAmpPolicies() {
            boolean result = true;
            ListIterator<BtPolicyCallback> it = mConnectionList.listIterator();
            while (it.hasNext()) {
                BtPolicyCallback thisBtPolicyCallback = it.next();
                int allowedPolicy = getEffectiveAmpPolicy(thisBtPolicyCallback.mDesiredAmpPolicy);
                if (allowedPolicy != thisBtPolicyCallback.mCurrentAmpPolicy) {
                    try {
                        thisBtPolicyCallback.mCallback.onAmpPolicyChange(allowedPolicy);
                        thisBtPolicyCallback.mCurrentAmpPolicy = allowedPolicy;
                    } catch (RemoteException e) {
                        Log.e(TAG, "", e);
                        result = false; /* This one didn't update, but keep looping */
                    }
                }
            }
            return result;
        }
    }

    private void persistBluetoothOnSetting(boolean bluetoothOn) {
        long origCallerIdentityToken = Binder.clearCallingIdentity();
        Settings.Secure.putInt(mContext.getContentResolver(), Settings.Secure.BLUETOOTH_ON,
                bluetoothOn ? 1 : 0);
        Binder.restoreCallingIdentity(origCallerIdentityToken);
    }

    /*package*/ synchronized boolean attemptAutoPair(String address) {
        if (!mBondState.hasAutoPairingFailed(address) &&
                !mBondState.isAutoPairingBlacklisted(address)) {
            mBondState.attempt(address);
            setPin(address, BluetoothDevice.convertPinToBytes("0000"));
            return true;
        }
        return false;
    }

    /*package*/ synchronized void onCreatePairedDeviceResult(String address, int result) {
        if (result == BluetoothDevice.BOND_SUCCESS) {
            setBondState(address, BluetoothDevice.BOND_BONDED);
            if (mBondState.isAutoPairingAttemptsInProgress(address)) {
                mBondState.clearPinAttempts(address);
            }
        } else {
            setBondState(address, BluetoothDevice.BOND_NONE, result);
            if (result == BluetoothDevice.UNBOND_REASON_AUTH_FAILED &&
                mBondState.getAttempt(address) == 1) {
                mBondState.addAutoPairingFailure(address);
                pairingAttempt(address, result);
            } else if (result == BluetoothDevice.UNBOND_REASON_REMOTE_DEVICE_DOWN &&
                  mBondState.isAutoPairingAttemptsInProgress(address)) {
                pairingAttempt(address, result);
            } else {
                if (mBondState.isAutoPairingAttemptsInProgress(address)) {
                   mBondState.clearPinAttempts(address);
                }
             }
        }
    }

    /*package*/ synchronized String getPendingOutgoingBonding() {
        return mBondState.getPendingOutgoingBonding();
    }

    private void pairingAttempt(String address, int result) {
        // This happens when our initial guess of "0000" as the pass key
        // fails. Try to create the bond again and display the pin dialog
        // to the user. Use back-off while posting the delayed
        // message. The initial value is
        // INIT_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY and the max value is
        // MAX_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY. If the max value is
        // reached, display an error to the user.
        int attempt = mBondState.getAttempt(address);
        if (attempt * INIT_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY >
                    MAX_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY) {
            mBondState.clearPinAttempts(address);
            setBondState(address, BluetoothDevice.BOND_NONE, result);
            return;
        }

        Message message = mHandler.obtainMessage(MESSAGE_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY);
        message.obj = address;
        boolean postResult =  mHandler.sendMessageDelayed(message,
                                        attempt * INIT_AUTO_PAIRING_FAILURE_ATTEMPT_DELAY);
        if (!postResult) {
            mBondState.clearPinAttempts(address);
            setBondState(address,
                    BluetoothDevice.BOND_NONE, result);
            return;
        }
        mBondState.attempt(address);
    }

    /** local cache of bonding state.
    /* we keep our own state to track the intermediate state BONDING, which
    /* bluez does not track.
     * All addresses must be passed in upper case.
     */
    public class BondState {
        private final HashMap<String, Integer> mState = new HashMap<String, Integer>();
        private final HashMap<String, Integer> mPinAttempt = new HashMap<String, Integer>();

        private static final String AUTO_PAIRING_BLACKLIST =
            "/etc/bluetooth/auto_pairing.conf";
        private static final String DYNAMIC_AUTO_PAIRING_BLACKLIST =
            "/data/misc/bluetooth/dynamic_auto_pairing.conf";
        private ArrayList<String>  mAutoPairingAddressBlacklist;
        private ArrayList<String> mAutoPairingExactNameBlacklist;
        private ArrayList<String> mAutoPairingPartialNameBlacklist;
        // Addresses added to blacklist dynamically based on usage.
        private ArrayList<String> mAutoPairingDynamicAddressBlacklist;


        // If this is an outgoing connection, store the address.
        // There can be only 1 pending outgoing connection at a time,
        private String mPendingOutgoingBonding;

        private synchronized void setPendingOutgoingBonding(String address) {
            mPendingOutgoingBonding = address;
        }

        public synchronized String getPendingOutgoingBonding() {
            return mPendingOutgoingBonding;
        }

        public synchronized void loadBondState() {
            if (mBluetoothState != BluetoothAdapter.STATE_TURNING_ON) {
                return;
            }
            String []bonds = null;
            String address = null;
            String val = getPropertyInternal("Devices");
            if (val != null) {
                bonds = val.split(",");
            }
            if (bonds == null) {
                return;
            }
            mState.clear();
            Log.d(TAG, "found " + bonds.length + " bonded devices");
            for (String device : bonds) {
                address = getAddressFromObjectPath(device);
                if (address == null) {
                    Log.e(TAG, "error! address is null");
                    continue;
                }
                String pairState = getUpdatedRemoteDeviceProperty(address, "Paired");
                Log.d(TAG, "The paired state of the remote device is " + pairState);
                if(pairState.equals("true")) {
                    Log.d(TAG, "The paired state of the remote device is true");
                    mState.put(address.toUpperCase(),
                        BluetoothDevice.BOND_BONDED);
                }
            }
        }

        public synchronized void setBondState(String address, int state) {
            setBondState(address, state, 0);
        }

        /** reason is ignored unless state == BOND_NOT_BONDED */
        public synchronized void setBondState(String address, int state, int reason) {
            int oldState = getBondState(address);
            if (oldState == state) {
                return;
            }

            // Check if this was an pending outgoing bonding.
            // If yes, reset the state.
            if (oldState == BluetoothDevice.BOND_BONDING) {
                if (address.equals(mPendingOutgoingBonding)) {
                    mPendingOutgoingBonding = null;
                }
            }

            if (state == BluetoothDevice.BOND_BONDED) {
                addProfileState(address);
            }

            if (DBG) log(address + " bond state " + oldState + " -> " + state + " (" +
                         reason + ")");
            Intent intent = new Intent(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
            intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
            intent.putExtra(BluetoothDevice.EXTRA_BOND_STATE, state);
            intent.putExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, oldState);
            if (state == BluetoothDevice.BOND_NONE) {
                if (reason <= 0) {
                    Log.w(TAG, "setBondState() called to unbond device, but reason code is " +
                          "invalid. Overriding reason code with BOND_RESULT_REMOVED");
                    reason = BluetoothDevice.UNBOND_REASON_REMOVED;
                }
                intent.putExtra(BluetoothDevice.EXTRA_REASON, reason);
                mState.remove(address);
            } else {
                mState.put(address, state);
            }

            mContext.sendBroadcast(intent, BLUETOOTH_PERM);
        }

        public boolean isAutoPairingBlacklisted(String address) {
            if (mAutoPairingAddressBlacklist != null) {
                for (String blacklistAddress : mAutoPairingAddressBlacklist) {
                    if (address.startsWith(blacklistAddress)) return true;
                }
            }

            if (mAutoPairingDynamicAddressBlacklist != null) {
                for (String blacklistAddress: mAutoPairingDynamicAddressBlacklist) {
                    if (address.equals(blacklistAddress)) return true;
                }
            }
            String name = getRemoteName(address);
            if (name != null) {
                if (mAutoPairingExactNameBlacklist != null) {
                    for (String blacklistName : mAutoPairingExactNameBlacklist) {
                        if (name.equals(blacklistName)) return true;
                    }
                }

                if (mAutoPairingPartialNameBlacklist != null) {
                    for (String blacklistName : mAutoPairingPartialNameBlacklist) {
                        if (name.startsWith(blacklistName)) return true;
                    }
                }
            }
            return false;
        }

        public synchronized int getBondState(String address) {
            Integer state = mState.get(address);
            if (state == null) {
                return BluetoothDevice.BOND_NONE;
            }
            return state.intValue();
        }

        /*package*/ synchronized String[] listInState(int state) {
            ArrayList<String> result = new ArrayList<String>(mState.size());
            for (Map.Entry<String, Integer> e : mState.entrySet()) {
                if (e.getValue().intValue() == state) {
                    result.add(e.getKey());
                }
            }
            return result.toArray(new String[result.size()]);
        }

        public synchronized void addAutoPairingFailure(String address) {
            if (mAutoPairingDynamicAddressBlacklist == null) {
                mAutoPairingDynamicAddressBlacklist = new ArrayList<String>();
            }

            updateAutoPairingData(address);
            mAutoPairingDynamicAddressBlacklist.add(address);
        }

        public synchronized boolean isAutoPairingAttemptsInProgress(String address) {
            return getAttempt(address) != 0;
        }

        public synchronized void clearPinAttempts(String address) {
            mPinAttempt.remove(address);
        }

        public synchronized boolean hasAutoPairingFailed(String address) {
            if (mAutoPairingDynamicAddressBlacklist == null) return false;

            return mAutoPairingDynamicAddressBlacklist.contains(address);
        }

        public synchronized int getAttempt(String address) {
            Integer attempt = mPinAttempt.get(address);
            if (attempt == null) {
                return 0;
            }
            return attempt.intValue();
        }

        public synchronized void attempt(String address) {
            Integer attempt = mPinAttempt.get(address);
            int newAttempt;
            if (attempt == null) {
                newAttempt = 1;
            } else {
                newAttempt = attempt.intValue() + 1;
            }
            mPinAttempt.put(address, new Integer(newAttempt));
        }

        private void copyAutoPairingData() {
            File file = null;
            FileInputStream in = null;
            FileOutputStream out = null;
            try {
                file = new File(DYNAMIC_AUTO_PAIRING_BLACKLIST);
                if (file.exists()) return;

                in = new FileInputStream(AUTO_PAIRING_BLACKLIST);
                out= new FileOutputStream(DYNAMIC_AUTO_PAIRING_BLACKLIST);

                byte[] buf = new byte[1024];
                int len;
                while ((len = in.read(buf)) > 0) {
                    out.write(buf, 0, len);
                }
            } catch (FileNotFoundException e) {
                log("FileNotFoundException: in copyAutoPairingData");
            } catch (IOException e) {
                log("IOException: in copyAutoPairingData");
            } finally {
                if (in != null) {
                   try {
                       in.close();
                   } catch (IOException e) {}
                }
                if (out != null) {
                    try {
                        out.close();
                    } catch (IOException e) {}
                }
            }
        }

        public void readAutoPairingData() {
            if (mAutoPairingAddressBlacklist != null) return;
            copyAutoPairingData();
            FileInputStream fstream = null;
            try {
                fstream = new FileInputStream(DYNAMIC_AUTO_PAIRING_BLACKLIST);
                DataInputStream in = new DataInputStream(fstream);
                BufferedReader file = new BufferedReader(new InputStreamReader(in));
                String line;
                while((line = file.readLine()) != null) {
                    line = line.trim();
                    if (line.length() == 0 || line.startsWith("//")) continue;
                    String[] value = line.split("=");
                    if (value != null && value.length == 2) {
                        String[] val = value[1].split(",");
                        if (value[0].equalsIgnoreCase("AddressBlacklist")) {
                            mAutoPairingAddressBlacklist =
                                new ArrayList<String>(Arrays.asList(val));
                        } else if (value[0].equalsIgnoreCase("ExactNameBlacklist")) {
                            mAutoPairingExactNameBlacklist =
                                new ArrayList<String>(Arrays.asList(val));
                        } else if (value[0].equalsIgnoreCase("PartialNameBlacklist")) {
                            mAutoPairingPartialNameBlacklist =
                                new ArrayList<String>(Arrays.asList(val));
                        } else if (value[0].equalsIgnoreCase("DynamicAddressBlacklist")) {
                            mAutoPairingDynamicAddressBlacklist =
                                new ArrayList<String>(Arrays.asList(val));
                        } else {
                            Log.e(TAG, "Error parsing Auto pairing blacklist file");
                        }
                    }
                }
            } catch (FileNotFoundException e) {
                log("FileNotFoundException: readAutoPairingData" + e.toString());
            } catch (IOException e) {
                log("IOException: readAutoPairingData" + e.toString());
            } finally {
                if (fstream != null) {
                    try {
                        fstream.close();
                    } catch (IOException e) {
                        // Ignore
                    }
                }
            }
        }

        // This function adds a bluetooth address to the auto pairing blacklist
        // file. These addresses are added to DynamicAddressBlacklistSection
        private void updateAutoPairingData(String address) {
            BufferedWriter out = null;
            try {
                out = new BufferedWriter(new FileWriter(DYNAMIC_AUTO_PAIRING_BLACKLIST, true));
                StringBuilder str = new StringBuilder();
                if (mAutoPairingDynamicAddressBlacklist.size() == 0) {
                    str.append("DynamicAddressBlacklist=");
                }
                str.append(address);
                str.append(",");
                out.write(str.toString());
            } catch (FileNotFoundException e) {
                log("FileNotFoundException: updateAutoPairingData" + e.toString());
            } catch (IOException e) {
                log("IOException: updateAutoPairingData" + e.toString());
            } finally {
                if (out != null) {
                    try {
                        out.close();
                    } catch (IOException e) {
                        // Ignore
                    }
                }
            }
        }
    }

    private static String toBondStateString(int bondState) {
        switch (bondState) {
        case BluetoothDevice.BOND_NONE:
            return "not bonded";
        case BluetoothDevice.BOND_BONDING:
            return "bonding";
        case BluetoothDevice.BOND_BONDED:
            return "bonded";
        default:
            return "??????";
        }
    }

    /*package*/ synchronized boolean isAdapterPropertiesEmpty() {
        return mAdapterProperties.isEmpty();
    }

    /*package*/synchronized void getAllProperties() {

        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        mAdapterProperties.clear();

        String properties[] = (String [])getAdapterPropertiesNative();
        // The String Array consists of key-value pairs.
        if (properties == null) {
            Log.e(TAG, "*Error*: GetAdapterProperties returned NULL");
            return;
        }

        for (int i = 0; i < properties.length; i++) {
            String name = properties[i];
            String newValue = null;
            int len;
            if (name == null) {
                Log.e(TAG, "Error:Adapter Property at index" + i + "is null");
                continue;
            }
            if (name.equals("Devices") || name.equals("UUIDs")) {
                StringBuilder str = new StringBuilder();
                len = Integer.valueOf(properties[++i]);
                for (int j = 0; j < len; j++) {
                    str.append(properties[++i]);
                    str.append(",");
                }
                if (len > 0) {
                    newValue = str.toString();
                }
            } else {
                newValue = properties[++i];
            }
            mAdapterProperties.put(name, newValue);
        }

        // Add adapter object path property.
        String adapterPath = getAdapterPathNative();
        if (adapterPath != null)
            mAdapterProperties.put("ObjectPath", adapterPath + "/dev_");
    }

    /* package */ synchronized void setProperty(String name, String value) {
        mAdapterProperties.put(name, value);
    }

    public synchronized boolean setName(String name) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (name == null) {
            return false;
        }
        return setPropertyString("Name", name);
    }

    //TODO(): setPropertyString, setPropertyInteger, setPropertyBoolean
    // Either have a single property function with Object as the parameter
    // or have a function for each property and then obfuscate in the JNI layer.
    // The following looks dirty.
    private boolean setPropertyString(String key, String value) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return false;
        return setAdapterPropertyStringNative(key, value);
    }

    private boolean setPropertyInteger(String key, int value) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return false;
        return setAdapterPropertyIntegerNative(key, value);
    }

    private boolean setPropertyBoolean(String key, boolean value) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return false;
        return setAdapterPropertyBooleanNative(key, value ? 1 : 0);
    }

    /**
     * Set the discoverability window for the device.  A timeout of zero
     * makes the device permanently discoverable (if the device is
     * discoverable).  Setting the timeout to a nonzero value does not make
     * a device discoverable; you need to call setMode() to make the device
     * explicitly discoverable.
     *
     * @param timeout The discoverable timeout in seconds.
     */
    public synchronized boolean setDiscoverableTimeout(int timeout) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        return setPropertyInteger("DiscoverableTimeout", timeout);
    }

    public synchronized boolean setScanMode(int mode, int duration) {
        mContext.enforceCallingOrSelfPermission(android.Manifest.permission.WRITE_SECURE_SETTINGS,
                                                "Need WRITE_SECURE_SETTINGS permission");
        boolean pairable = false;
        boolean discoverable = false;

        switch (mode) {
        case BluetoothAdapter.SCAN_MODE_NONE:
            pairable = false;
            discoverable = false;
            mHandler.removeMessages(MESSAGE_DISCOVERABLE_TIMEOUT);
            break;
        case BluetoothAdapter.SCAN_MODE_CONNECTABLE:
            pairable = true;
            discoverable = false;
            mHandler.removeMessages(MESSAGE_DISCOVERABLE_TIMEOUT);
            break;
        case BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE:
            setDiscoverableTimeout(duration);
            pairable = true;
            discoverable = true;
            mHandler.removeMessages(MESSAGE_DISCOVERABLE_TIMEOUT);
            if (duration > 0) {
                Message msg = mHandler.obtainMessage(MESSAGE_DISCOVERABLE_TIMEOUT);
                mHandler.sendMessageDelayed(msg, duration * 1000);
                Message msgd = mHandler.obtainMessage(MESSAGE_DISCOVERABLE_CHANGE_TIMEOUT);
                mHandler.sendMessageDelayed(msgd, 200);
            }
            if (DBG) Log.d(TAG, "BT Discoverable for " + duration + " seconds");
            break;
        default:
            Log.w(TAG, "Requested invalid scan mode " + mode);
            return false;
        }
        setPropertyBoolean("Pairable", pairable);
        setPropertyBoolean("Discoverable", discoverable);

        return true;
    }

    /*package*/ synchronized String getProperty(String name) {
        if (!isEnabledInternal()) return null;
        return getPropertyInternal(name);
    }

    /*package*/ synchronized String getPropertyInternal(String name) {
        if (!mAdapterProperties.isEmpty())
            return mAdapterProperties.get(name);
        getAllProperties();
        return mAdapterProperties.get(name);
    }

    public synchronized String getAddress() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        return getProperty("Address");
    }

    public synchronized String getName() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        return getProperty("Name");
    }

    public synchronized String getCOD() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        return getProperty("Class");
    }

    public boolean setBluetoothClass(String address, int classOfDevice) {
        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                    "Need BLUETOOTH_ADMIN permission");
            return false;
        }

        if (!isEnabledInternal()) return false;

        return setDevicePropertyIntegerNative(getObjectPathFromAddress(address), "Class",
                classOfDevice);
    }


    /**
     * Returns the user-friendly name of a remote device.  This value is
     * returned from our local cache, which is updated when onPropertyChange
     * event is received.
     * Do not expect to retrieve the updated remote name immediately after
     * changing the name on the remote device.
     *
     * @param address Bluetooth address of remote device.
     *
     * @return The user-friendly name of the specified remote device.
     */
    public synchronized String getRemoteName(String address) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return null;
        }
        return getRemoteDeviceProperty(address, "Name");
    }

    /**
     * Get the discoverability window for the device.  A timeout of zero
     * means that the device is permanently discoverable (if the device is
     * in the discoverable mode).
     *
     * @return The discoverability window of the device, in seconds.  A negative
     *         value indicates an error.
     */
    public synchronized int getDiscoverableTimeout() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        String timeout = getProperty("DiscoverableTimeout");
        if (timeout != null)
           return Integer.valueOf(timeout);
        else
            return -1;
    }

    public synchronized int getScanMode() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal())
            return BluetoothAdapter.SCAN_MODE_NONE;

        boolean pairable = getProperty("Pairable").equals("true");
        boolean discoverable = getProperty("Discoverable").equals("true");
        return bluezStringToScanMode (pairable, discoverable);
    }

    public synchronized boolean startDiscovery() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;


        log("**** startDiscovery");

        return startDiscoveryNative();
    }

    public synchronized boolean cancelDiscovery() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;

        return stopDiscoveryNative();
    }

    public synchronized boolean isDiscovering() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        return mIsDiscovering;
    }

    /* package */ void setIsDiscovering(boolean isDiscovering) {
        mIsDiscovering = isDiscovering;
    }

    private boolean isBondingFeasible(String address) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;

        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return false;
        }
        address = address.toUpperCase();

        if (mBondState.getPendingOutgoingBonding() != null) {
            log("Ignoring createBond(): another device is bonding");
            // a different device is currently bonding, fail
            return false;
        }

        // Check for bond state only if we are not performing auto
        // pairing exponential back-off attempts.
        if (!mBondState.isAutoPairingAttemptsInProgress(address) &&
                mBondState.getBondState(address) != BluetoothDevice.BOND_NONE) {
            log("Ignoring createBond(): this device is already bonding or bonded");
            return false;
        }

        if (address.equals(mDockAddress)) {
            if (!writeDockPin()) {
                log("Error while writing Pin for the dock");
                return false;
            }
        }
        return true;
    }

    public synchronized boolean createBond(String address) {
        if (!isBondingFeasible(address)) return false;

        if (!createPairedDeviceNative(address, 60000  /*1 minute*/ )) {
            return false;
        }

        mBondState.setPendingOutgoingBonding(address);
        mBondState.setBondState(address, BluetoothDevice.BOND_BONDING);

        return true;
    }

    public synchronized boolean createBondOutOfBand(String address, byte[] hash,
                                                    byte[] randomizer) {
        if (!isBondingFeasible(address)) return false;

        if (!createPairedDeviceOutOfBandNative(address, 60000 /* 1 minute */)) {
            return false;
        }

        setDeviceOutOfBandData(address, hash, randomizer);
        mBondState.setPendingOutgoingBonding(address);
        mBondState.setBondState(address, BluetoothDevice.BOND_BONDING);

        return true;
    }

    public synchronized boolean setDeviceOutOfBandData(String address, byte[] hash,
            byte[] randomizer) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;

        Pair <byte[], byte[]> value = new Pair<byte[], byte[]>(hash, randomizer);

        if (DBG) {
            log("Setting out of band data for:" + address + ":" +
              Arrays.toString(hash) + ":" + Arrays.toString(randomizer));
        }

        mDeviceOobData.put(address, value);
        return true;
    }

    Pair<byte[], byte[]> getDeviceOutOfBandData(BluetoothDevice device) {
        return mDeviceOobData.get(device.getAddress());
    }


    public synchronized byte[] readOutOfBandData() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM,
                                                "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return null;

        return readAdapterOutOfBandDataNative();
    }

    public synchronized boolean cancelBondProcess(String address) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;

        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return false;
        }
        address = address.toUpperCase();
        if (mBondState.getBondState(address) != BluetoothDevice.BOND_BONDING) {
            return false;
        }

        mBondState.setBondState(address, BluetoothDevice.BOND_NONE,
                                BluetoothDevice.UNBOND_REASON_AUTH_CANCELED);
        cancelDeviceCreationNative(address);
        return true;
    }

    public synchronized boolean removeBond(String address) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;

        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return false;
        }
        BluetoothDeviceProfileState state = mDeviceProfileState.get(address);
        if (state != null) {
            state.sendMessage(BluetoothDeviceProfileState.UNPAIR);
            return true;
        } else {
            return false;
        }
    }

    public synchronized boolean removeBondInternal(String address) {
        // Unset the trusted device state and then unpair
        setTrust(address, false);
        return removeDeviceNative(getObjectPathFromAddress(address));
    }

    public synchronized String[] listBonds() {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        return mBondState.listInState(BluetoothDevice.BOND_BONDED);
    }

    /*package*/ synchronized String[] listInState(int state) {
      return mBondState.listInState(state);
    }

    public synchronized int getBondState(String address) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return BluetoothDevice.ERROR;
        }
        return mBondState.getBondState(address.toUpperCase());
    }

    /*package*/ synchronized boolean setBondState(String address, int state) {
        return setBondState(address, state, 0);
    }

    /*package*/ synchronized boolean setBondState(String address, int state, int reason) {
        mBondState.setBondState(address.toUpperCase(), state);
        return true;
    }

    public synchronized boolean isBluetoothDock(String address) {
        SharedPreferences sp = mContext.getSharedPreferences(SHARED_PREFERENCES_NAME,
                mContext.MODE_PRIVATE);

        return sp.contains(SHARED_PREFERENCE_DOCK_ADDRESS + address);
    }

    /*package*/ boolean isRemoteDeviceInCache(String address) {
        return (mDeviceProperties.get(address) != null);
    }

    /*package*/ String[] getRemoteDeviceProperties(String address) {
        Log.d(TAG, "getRemoteDeviceProperties: " + address);
        if (!isEnabledInternal()) return null;

        String objectPath = getObjectPathFromAddress(address);
        return (String [])getDevicePropertiesNative(objectPath);
    }

    /*package*/ synchronized String getRemoteDeviceProperty(String address, String property) {
        Map<String, String> properties = mDeviceProperties.get(address);
        Log.d(TAG, "getRemoteDeviceProperty: " + property + ", " + address);
        if (properties != null) {
            return properties.get(property);
        } else {
            // Query for remote device properties, again.
            // We will need to reload the cache when we switch Bluetooth on / off
            // or if we crash.
            if (updateRemoteDevicePropertiesCache(address))
                return getRemoteDeviceProperty(address, property);
        }
        Log.e(TAG, "getRemoteDeviceProperty: " + property + "not present:" + address);
        return null;
    }

    /* package */ synchronized boolean updateRemoteDevicePropertiesCache(String address) {
        String[] propValues = getRemoteDeviceProperties(address);
        Log.d(TAG, "updateRemoteDevicePropertiesCache: " + address);
        if (propValues != null) {
            addRemoteDeviceProperties(address, propValues);
            return true;
        }
        return false;
    }

    private String getUpdatedRemoteDeviceProperty(String address,
                                       String property) {
        String objectPath = getObjectPathFromAddress(address);
        String[] propValues =  (String [])getDevicePropertiesNative(objectPath);
        if (propValues != null) {
            addRemoteDeviceProperties(address, propValues);
            return getRemoteDeviceProperty(address, property);
        }
        Log.e(TAG, "getRemoteDeviceProperty: " + property + "not present:" + address);
        return null;
    }

    /* package */ synchronized void addRemoteDeviceProperties(String address, String[] properties) {
        /*
         * We get a DeviceFound signal every time RSSI changes or name changes.
         * Don't create a new Map object every time */
        Map<String, String> propertyValues = mDeviceProperties.get(address);
        if (propertyValues == null) {
            propertyValues = new HashMap<String, String>();
        }

        for (int i = 0; i < properties.length; i++) {
            String name = properties[i];
            String newValue = null;
            int len;
            if (name == null) {
                Log.e(TAG, "Error: Remote Device Property at index" + i + "is null");
                continue;
            }
            if (name.equals("UUIDs") || name.equals("Nodes") || name.equals("Services")) {
                StringBuilder str = new StringBuilder();
                len = Integer.valueOf(properties[++i]);
                for (int j = 0; j < len; j++) {
                    str.append(properties[++i]);
                    str.append(",");
                }
                if (len > 0) {
                    newValue = str.toString();
                }
            } else {
                newValue = properties[++i];
            }
            propertyValues.put(name, newValue);
        }

        mDeviceProperties.put(address, propertyValues);

        // We have added a new remote device or updated its properties.
        // Also update the serviceChannel cache.
        updateDeviceServiceChannelCache(address);
    }

    /* package */ void removeRemoteDeviceProperties(String address) {
        mDeviceProperties.remove(address);
    }

    /* package */ synchronized void setRemoteDeviceProperty(String address, String name,
                                                              String value) {
        Map <String, String> propVal = mDeviceProperties.get(address);
        if (propVal != null) {
            propVal.put(name, value);
            mDeviceProperties.put(address, propVal);
        } else {
            Log.e(TAG, "setRemoteDeviceProperty for a device not in cache:" + address);
        }
    }

    /**
     * Sets the remote device trust state.
     *
     * @return boolean to indicate operation success or fail
     */
    public synchronized boolean setTrust(String address, boolean value) {
        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                    "Need BLUETOOTH_ADMIN permission");
            return false;
        }

        if (!isEnabledInternal()) return false;

        return setDevicePropertyBooleanNative(getObjectPathFromAddress(address), "Trusted",
                value ? 1 : 0);
    }

    /**
     * Gets the remote device trust state as boolean.
     * Note: this value may be
     * retrieved from cache if we retrieved the data before *
     *
     * @return boolean to indicate trust or untrust state
     */
    public synchronized boolean getTrustState(String address) {
        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
            return false;
        }

        String val = getRemoteDeviceProperty(address, "Trusted");
        if (val == null) {
            return false;
        } else {
            return val.equals("true") ? true : false;
        }
    }

    /**
     * Gets the remote major, minor classes encoded as a 32-bit
     * integer.
     *
     * Note: this value is retrieved from cache, because we get it during
     *       remote-device discovery.
     *
     * @return 32-bit integer encoding the remote major, minor, and service
     *         classes.
     */
    public synchronized int getRemoteClass(String address) {
        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
            return BluetoothClass.ERROR;
        }
        String val = getRemoteDeviceProperty(address, "Class");
        if (val == null)
            return BluetoothClass.ERROR;
        else {
            return Integer.valueOf(val);
        }
    }

    /**
     * Gets the UUIDs supported by the remote device
     *
     * @return array of 128bit ParcelUuids
     */
    public synchronized ParcelUuid[] getRemoteUuids(String address) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return null;
        }
        return getUuidFromCache(address);
    }

    private ParcelUuid[] getUuidFromCache(String address) {
        String value = getRemoteDeviceProperty(address, "UUIDs");
        if (value == null) return null;

        String[] uuidStrings = null;
        // The UUIDs are stored as a "," separated string.
        uuidStrings = value.split(",");
        ParcelUuid[] uuids = new ParcelUuid[uuidStrings.length];

        for (int i = 0; i < uuidStrings.length; i++) {
            uuids[i] = ParcelUuid.fromString(uuidStrings[i]);
        }
        return uuids;
    }

    /**
     * Connect and fetch new UUID's using SDP.
     * The UUID's found are broadcast as intents.
     * Optionally takes a uuid and callback to fetch the RFCOMM channel for the
     * a given uuid.
     * TODO: Don't wait UUID_INTENT_DELAY to broadcast UUID intents on success
     * TODO: Don't wait UUID_INTENT_DELAY to handle the failure case for
     * callback and broadcast intents.
     */
    public synchronized boolean fetchRemoteUuids(String address, ParcelUuid uuid,
            IBluetoothCallback callback) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return false;

        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return false;
        }

        RemoteService service = new RemoteService(address, uuid);
        if (uuid != null && mUuidCallbackTracker.get(service) != null) {
            // An SDP query for this address & uuid is already in progress
            // Do not add this callback for the uuid
            return false;
        }

        if (mUuidIntentTracker.contains(address)) {
            // An SDP query for this address is already in progress
            // Add this uuid onto the in-progress SDP query
            if (uuid != null) {
                mUuidCallbackTracker.put(new RemoteService(address, uuid), callback);
            }
            return true;
        }

        boolean ret;
        // Just do the SDP if the device is already  created and UUIDs are not
        // NULL, else create the device and then do SDP.
        if (isRemoteDeviceInCache(address) && getRemoteUuids(address) != null && findDeviceNative(address) != null) {
            String path = getObjectPathFromAddress(address);
            if (path == null) return false;

            // Use an empty string for the UUID pattern
            ret = discoverServicesNative(path, "");
        } else {
            ret = createDeviceNative(address);
        }

        mUuidIntentTracker.add(address);
        if (uuid != null) {
            mUuidCallbackTracker.put(new RemoteService(address, uuid), callback);
        }

        Message message = mHandler.obtainMessage(MESSAGE_UUID_INTENT);
        message.obj = address;
        mHandler.sendMessageDelayed(message, UUID_INTENT_DELAY);
        return ret;
    }

    /**
     * Gets the rfcomm channel associated with the UUID.
     * Pulls records from the cache only.
     *
     * @param address Address of the remote device
     * @param uuid ParcelUuid of the service attribute
     *
     * @return rfcomm channel associated with the service attribute
     *         -1 on error
     */
    public int getRemoteServiceChannel(String address, ParcelUuid uuid) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return -1;

        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return BluetoothDevice.ERROR;
        }
        // Check if we are recovering from a crash.
        if (mDeviceProperties.isEmpty()) {
            if (!updateRemoteDevicePropertiesCache(address))
                return -1;
        }

        Map<ParcelUuid, Integer> value = mDeviceServiceChannelCache.get(address);
        if (value != null && value.containsKey(uuid))
            return value.get(uuid);
        return -1;
    }


    /**
     * Gets the remote features list associated with the feature name.
     * Pulls records from the cache only.
     *
     * @param address Address of the remote device
     * @param feature the name of feature to get
     *
     * @return feature list string value
     *         null on error
     */
    public String getRemoteFeature(String address, String feature) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return null;

        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return null;
        }

        Map<String, String> value = mDeviceFeatureCache.get(address);
        if (value != null && value.containsKey(feature))
            return value.get(feature);
        return null;
    }

    /**
     * Gets the L2CAP PSM associated with the UUID.
     * Pulls records from the cache only.
     *
     * @param address Address of the remote device
     * @param uuid ParcelUuid of the service attribute
     *
     * @return L2CAP PSM associated with the service attribute
     *         -1 on error
     */
    public int getRemoteL2capPsm(String address, ParcelUuid uuid) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return -1;

        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return BluetoothDevice.ERROR;
        }
        // Check if we are recovering from a crash.
        if (mDeviceProperties.isEmpty()) {
            if (!updateRemoteDevicePropertiesCache(address)) {
                return -1;
            }
        }

        Map<ParcelUuid, Integer> value = mDeviceL2capPsmCache.get(address);
        if (value != null && value.containsKey(uuid)) {
            return value.get(uuid);
        }

        return -1;
    }

    public synchronized boolean setPin(String address, byte[] pin) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;

        if (pin == null || pin.length <= 0 || pin.length > 16 ||
            !BluetoothAdapter.checkBluetoothAddress(address)) {
            return false;
        }
        address = address.toUpperCase();
        Integer data = mEventLoop.getPasskeyAgentRequestData().remove(address);
        if (data == null) {
            Log.w(TAG, "setPin(" + address + ") called but no native data available, " +
                  "ignoring. Maybe the PasskeyAgent Request was cancelled by the remote device" +
                  " or by bluez.\n");
            return false;
        }
        // bluez API wants pin as a string
        String pinString;
        try {
            pinString = new String(pin, "UTF8");
        } catch (UnsupportedEncodingException uee) {
            Log.e(TAG, "UTF8 not supported?!?");
            return false;
        }
        return setPinNative(address, pinString, data.intValue());
    }

    public synchronized boolean sapAuthorize(String address, boolean access) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;


        address = address.toUpperCase();
        Integer data = mEventLoop.getAuthorizationRequestData().remove(address);
        if (data == null) {
            Log.w(TAG, "sapAuthorize(" + address + ") called but no native data available, " +
                  "ignoring. Maybe the PasskeyAgent Request was cancelled by the remote device" +
                  " or by bluez.\n");
            return false;
        }

        return sapAuthorizeNative(address, access, data.intValue());
    }

    public synchronized boolean setPasskey(String address, int passkey) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;

        if (passkey < 0 || passkey > 999999 || !BluetoothAdapter.checkBluetoothAddress(address)) {
            return false;
        }
        address = address.toUpperCase();
        Integer data = mEventLoop.getPasskeyAgentRequestData().remove(address);
        if (data == null) {
            Log.w(TAG, "setPasskey(" + address + ") called but no native data available, " +
                  "ignoring. Maybe the PasskeyAgent Request was cancelled by the remote device" +
                  " or by bluez.\n");
            return false;
        }
        return setPasskeyNative(address, passkey, data.intValue());
    }

    public synchronized boolean setPairingConfirmation(String address, boolean confirm) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;

        address = address.toUpperCase();
        Integer data = mEventLoop.getPasskeyAgentRequestData().remove(address);
        if (data == null) {
            Log.w(TAG, "setPasskey(" + address + ") called but no native data available, " +
                  "ignoring. Maybe the PasskeyAgent Request was cancelled by the remote device" +
                  " or by bluez.\n");
            return false;
        }
        return setPairingConfirmationNative(address, confirm, data.intValue());
    }

    public synchronized boolean setRemoteOutOfBandData(String address) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;
        address = address.toUpperCase();
        Integer data = mEventLoop.getPasskeyAgentRequestData().remove(address);
        if (data == null) {
            Log.w(TAG, "setRemoteOobData(" + address + ") called but no native data available, " +
                  "ignoring. Maybe the PasskeyAgent Request was cancelled by the remote device" +
                  " or by bluez.\n");
            return false;
        }

        Pair<byte[], byte[]> val = mDeviceOobData.get(address);
        byte[] hash, randomizer;
        if (val == null) {
            // TODO: check what should be passed in this case.
            hash = new byte[16];
            randomizer = new byte[16];
        } else {
            hash = val.first;
            randomizer = val.second;
        }
        return setRemoteOutOfBandDataNative(address, hash, randomizer, data.intValue());
    }

    public synchronized boolean cancelPairingUserInput(String address) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_ADMIN_PERM,
                                                "Need BLUETOOTH_ADMIN permission");
        if (!isEnabledInternal()) return false;

        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return false;
        }
        mBondState.setBondState(address, BluetoothDevice.BOND_NONE,
                BluetoothDevice.UNBOND_REASON_AUTH_CANCELED);
        address = address.toUpperCase();
        Integer data = mEventLoop.getPasskeyAgentRequestData().remove(address);
        if (data == null) {
            Log.w(TAG, "cancelUserInputNative(" + address + ") called but no native data " +
                "available, ignoring. Maybe the PasskeyAgent Request was already cancelled " +
                "by the remote or by bluez.\n");
            return false;
        }
        return cancelPairingUserInputNative(address, data.intValue());
    }

    /*package*/ void updateDeviceServiceChannelCache(String address) {
        if (!isEnabledInternal()) {
            log("Bluetooth is not on");
            return;
        }

        ParcelUuid[] deviceUuids = getRemoteUuids(address);
        // We are storing the RFCOMM channel numbers and L2CAP PSMs
        // only for the uuids we are interested in.
        int channel;
        int psm;
        if (DBG) log("updateDeviceServiceChannelCache(" + address + ")");

        // Remove service channel timeout handler
        mHandler.removeMessages(MESSAGE_UUID_INTENT);

        ArrayList<ParcelUuid> applicationUuids = new ArrayList();

        synchronized (this) {
            for (RemoteService service : mUuidCallbackTracker.keySet()) {
                if (service.address.equals(address)) {
                    applicationUuids.add(service.uuid);
                }
            }
        }

        Map <ParcelUuid, Integer> rfcommValue = new HashMap<ParcelUuid, Integer>();
        Map <ParcelUuid, Integer> l2capPsmValue = new HashMap<ParcelUuid, Integer>();
        Map <String, String> feature = new HashMap<String, String>();

        // Retrieve RFCOMM channel and L2CAP PSM (if available) for default uuids
        for (ParcelUuid uuid : RFCOMM_UUIDS) {
            if (BluetoothUuid.isUuidPresent(deviceUuids, uuid)) {
                psm = getDeviceServiceChannelNative(getObjectPathFromAddress(address),
                        uuid.toString(), SDP_ATTR_GOEP_L2CAP_PSM);
                l2capPsmValue.put(uuid, psm);
                if (DBG) log("\tuuid(system): " + uuid + " PSM: "+ psm );
                if(!BluetoothUuid.isPrintingStatus(uuid)){
                    channel = getDeviceServiceChannelNative(getObjectPathFromAddress(address),
                             uuid.toString(), SDP_ATTR_PROTO_DESC_LIST);
                    rfcommValue.put(uuid, channel);
                    if (DBG) log("\tuuid(system): " + uuid + " SCN: "+ channel );
                }
            }

            // Retrieve Additional RFCOMM Channel for BPP
            if (BluetoothUuid.isPrintingStatus(uuid)) {
                channel = getDeviceServiceChannelNative(getObjectPathFromAddress(address),
                         uuid.toString(), 0x000d); // find additional Channel Number
                if (DBG) log("\tuuid(system): " + uuid + " " + channel);
                rfcommValue.put(uuid, channel);
            }

            if (BluetoothUuid.isDirectPrinting(uuid)) {
                String SupportedFormats;
                SupportedFormats = getDeviceStringAttrValue(getObjectPathFromAddress(address),
                                   uuid.toString(), 0x0350); // find additional Channel Number
                if (DBG) log("\tSupportedFormats: " + uuid + " " + SupportedFormats);
                feature.put("SupportedFormats", SupportedFormats);
                mDeviceFeatureCache.put(address, feature);
            }

        }
        // Retrieve RFCOMM channel and L2CAP PSM (if available) for
        // application requested uuids
        for (ParcelUuid uuid : applicationUuids) {
            if (BluetoothUuid.isUuidPresent(deviceUuids, uuid)) {
                psm = getDeviceServiceChannelNative(getObjectPathFromAddress(address),
                        uuid.toString(), SDP_ATTR_GOEP_L2CAP_PSM);
                channel = getDeviceServiceChannelNative(getObjectPathFromAddress(address),
                        uuid.toString(), SDP_ATTR_PROTO_DESC_LIST);
                if (DBG) log("\tuuid(application): " + uuid + " SCN: " + channel +
                        " PSM: " + psm);
                rfcommValue.put(uuid, channel);
                l2capPsmValue.put(uuid, psm);
            }
        }

        synchronized (this) {
            // Make application callbacks (RFCOMM channels only)
            for (Iterator<RemoteService> iter = mUuidCallbackTracker.keySet().iterator();
                    iter.hasNext();) {
                RemoteService service = iter.next();
                if (service.address.equals(address)) {
                    channel = -1;
                    if (rfcommValue.get(service.uuid) != null) {
                        channel = rfcommValue.get(service.uuid);
                    }
                    if (channel != -1) {
                        if (DBG) log("Making callback for " + service.uuid + " with result " +
                                channel);
                        IBluetoothCallback callback = mUuidCallbackTracker.get(service);
                        if (callback != null) {
                            try {
                                callback.onRfcommChannelFound(channel);
                            } catch (RemoteException e) {Log.e(TAG, "", e);}
                        }

                        iter.remove();
                    }
                }
            }

            // Update cache
            mDeviceServiceChannelCache.put(address, rfcommValue);
            mDeviceL2capPsmCache.put(address, l2capPsmValue);
        }
    }

    /**
     * b is a handle to a Binder instance, so that this service can be notified
     * for Applications that terminate unexpectedly, to clean there service
     * records
     */
    public synchronized int addRfcommServiceRecord(String serviceName, ParcelUuid uuid,
            int channel, IBinder b) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return -1;

        if (serviceName == null || uuid == null || channel < 1 ||
                channel > BluetoothSocket.MAX_RFCOMM_CHANNEL) {
            return -1;
        }
        if (BluetoothUuid.isUuidPresent(BluetoothUuid.RESERVED_UUIDS, uuid)) {
            Log.w(TAG, "Attempted to register a reserved UUID: " + uuid);
            return -1;
        }
        int handle = addRfcommServiceRecordNative(serviceName,
                uuid.getUuid().getMostSignificantBits(), uuid.getUuid().getLeastSignificantBits(),
                (short)channel);
        if (DBG) log("new handle " + Integer.toHexString(handle));
        if (handle == -1) {
            return -1;
        }

        int pid = Binder.getCallingPid();
        mServiceRecordToPid.put(new Integer(handle), new Integer(pid));
        try {
            b.linkToDeath(new Reaper(handle, pid), 0);
        } catch (RemoteException e) {}
        return handle;
    }

    public void removeServiceRecord(int handle) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM,
                                                "Need BLUETOOTH permission");
        checkAndRemoveRecord(handle, Binder.getCallingPid());
    }

    public int registerEl2capConnection(IBluetoothCallback callback, int desiredAmpPolicy) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM,
                                                "Need BLUETOOTH permission");
        return mConnectionManager.registerEl2capConnection(callback, desiredAmpPolicy);
    }

    public void deregisterEl2capConnection(int handle) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM,
                                                "Need BLUETOOTH permission");
        mConnectionManager.deregisterEl2capConnection(handle);
    }

    public int getEffectiveAmpPolicy(int desiredPolicy) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM,
                                                "Need BLUETOOTH permission");
        return mConnectionManager.getEffectiveAmpPolicy(desiredPolicy);
    }

    public boolean setDesiredAmpPolicy(int handle, int policy) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM,
                                                "Need BLUETOOTH permission");
        return mConnectionManager.setDesiredAmpPolicy(handle, policy);
    }

    public void setUseWifiForBtTransfers(boolean useWifi) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM,
                                                "Need BLUETOOTH permission");
        mConnectionManager.setUseWifiForBtTransfers(useWifi);
    }

    private synchronized void checkAndRemoveRecord(int handle, int pid) {
        Integer handleInt = new Integer(handle);
        Integer owner = mServiceRecordToPid.get(handleInt);
        if (owner != null && pid == owner.intValue()) {
            if (DBG) log("Removing service record " + Integer.toHexString(handle) + " for pid " +
                    pid);
            mServiceRecordToPid.remove(handleInt);
            removeServiceRecordNative(handle);
        }
    }

    private class Reaper implements IBinder.DeathRecipient {
        int pid;
        int handle;
        Reaper(int handle, int pid) {
            this.pid = pid;
            this.handle = handle;
        }
        public void binderDied() {
            synchronized (BluetoothService.this) {
                if (DBG) log("Tracked app " + pid + " died");
                checkAndRemoveRecord(handle, pid);
            }
        }
    }

    private final BroadcastReceiver mReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent == null) return;

            String action = intent.getAction();
            if (action.equals(Intent.ACTION_AIRPLANE_MODE_CHANGED)) {
                ContentResolver resolver = context.getContentResolver();
                // Query the airplane mode from Settings.System just to make sure that
                // some random app is not sending this intent and disabling bluetooth
                boolean enabled = !isAirplaneModeOn();
                // If bluetooth is currently expected to be on, then enable or disable bluetooth
                if (Settings.Secure.getInt(resolver, Settings.Secure.BLUETOOTH_ON, 0) > 0) {
                    if (enabled) {
                        enable(false);
                    } else {
                        disable(false);
                    }
                }
            } else if (Intent.ACTION_DOCK_EVENT.equals(action)) {
                int state = intent.getIntExtra(Intent.EXTRA_DOCK_STATE,
                        Intent.EXTRA_DOCK_STATE_UNDOCKED);
                if (DBG) Log.v(TAG, "Received ACTION_DOCK_EVENT with State:" + state);
                if (state == Intent.EXTRA_DOCK_STATE_UNDOCKED) {
                    mDockAddress = null;
                    mDockPin = null;
                } else {
                    SharedPreferences.Editor editor =
                        mContext.getSharedPreferences(SHARED_PREFERENCES_NAME,
                                mContext.MODE_PRIVATE).edit();
                    editor.putBoolean(SHARED_PREFERENCE_DOCK_ADDRESS + mDockAddress, true);
                    editor.apply();
                }
            } else if (BluetoothHeadset.ACTION_AUDIO_STATE_CHANGED.equals(action)) {
                int audioState = intent.getIntExtra(BluetoothHeadset.EXTRA_AUDIO_STATE,
                        BluetoothHeadset.AUDIO_STATE_DISCONNECTED);
                mConnectionManager.setScoAudioActive(audioState == BluetoothHeadset.AUDIO_STATE_CONNECTED);
            } else if (BluetoothA2dp.ACTION_SINK_STATE_CHANGED.equals(action)) {
                BluetoothA2dp btA2dp = new BluetoothA2dp(context);
                if (btA2dp != null) {
                    mConnectionManager.setA2dpAudioActive(btA2dp.isPlayingSink());
                } else {
                    Log.e(TAG, "BluetoothA2dp service not available");
                }
            } else if (SAP_ACCESS_ALLOWED_ACTION.equals(action)) {
                Log.i(TAG, "Received SAP_ACCESS_ALLOWED_ACTION");
                String address = intent.getStringExtra("address");
                boolean alwaysAllow = intent.getBooleanExtra(SAP_EXTRA_ALWAYS_ALLOWED, false);
                if (address != null) {
                    sapAuthorize(address, true);
                    if (alwaysAllow) {
                        Log.i(TAG, "Setting trust state to true");
                        setTrust(address,true);
                    }
                } else {
                    Log.e(TAG, "address is null");
                }
            } else if (SAP_ACCESS_DISALLOWED_ACTION.equals(action)) {
                Log.i(TAG, "Received SAP_ACCESS_DISALLOWED_ACTION");
                String address = intent.getStringExtra("address");
                if (address != null) {
                    sapAuthorize(address, false);
                } else {
                    Log.e(TAG, "address is null");
                }
            }
        }
    };

    private void registerForAirplaneMode(IntentFilter filter) {
        final ContentResolver resolver = mContext.getContentResolver();
        final String airplaneModeRadios = Settings.System.getString(resolver,
                Settings.System.AIRPLANE_MODE_RADIOS);
        final String toggleableRadios = Settings.System.getString(resolver,
                Settings.System.AIRPLANE_MODE_TOGGLEABLE_RADIOS);

        mIsAirplaneSensitive = airplaneModeRadios == null ? true :
                airplaneModeRadios.contains(Settings.System.RADIO_BLUETOOTH);
        mIsAirplaneToggleable = toggleableRadios == null ? false :
                toggleableRadios.contains(Settings.System.RADIO_BLUETOOTH);

        if (mIsAirplaneSensitive) {
            filter.addAction(Intent.ACTION_AIRPLANE_MODE_CHANGED);
        }
    }

    /* Returns true if airplane mode is currently on */
    private final boolean isAirplaneModeOn() {
        return Settings.System.getInt(mContext.getContentResolver(),
                Settings.System.AIRPLANE_MODE_ON, 0) == 1;
    }

    /* Broadcast the Uuid intent */
    /*package*/ synchronized void sendUuidIntent(String address) {
        ParcelUuid[] uuid = getUuidFromCache(address);
        Intent intent = new Intent(BluetoothDevice.ACTION_UUID);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
        intent.putExtra(BluetoothDevice.EXTRA_UUID, uuid);
        mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);

        if (mUuidIntentTracker.contains(address))
            mUuidIntentTracker.remove(address);

    }

    /*package*/ synchronized void makeServiceChannelCallbacks(String address) {
        for (Iterator<RemoteService> iter = mUuidCallbackTracker.keySet().iterator();
                iter.hasNext();) {
            RemoteService service = iter.next();
            if (service.address.equals(address)) {
                if (DBG) log("Cleaning up failed UUID channel lookup: " + service.address +
                        " " + service.uuid);
                IBluetoothCallback callback = mUuidCallbackTracker.get(service);
                if (callback != null) {
                    try {
                        callback.onRfcommChannelFound(-1);
                    } catch (RemoteException e) {Log.e(TAG, "", e);}
                }

                iter.remove();
            }
        }
    }

    @Override
    protected void dump(FileDescriptor fd, PrintWriter pw, String[] args) {
        switch(mBluetoothState) {
        case BluetoothAdapter.STATE_OFF:
            pw.println("Bluetooth OFF\n");
            return;
        case BluetoothAdapter.STATE_TURNING_ON:
            pw.println("Bluetooth TURNING ON\n");
            return;
        case BluetoothAdapter.STATE_TURNING_OFF:
            pw.println("Bluetooth TURNING OFF\n");
            return;
        case BluetoothAdapter.STATE_ON:
            pw.println("Bluetooth ON\n");
        }

        pw.println("mIsAirplaneSensitive = " + mIsAirplaneSensitive);
        pw.println("mIsAirplaneToggleable = " + mIsAirplaneToggleable);

        pw.println("Local address = " + getAddress());
        pw.println("Local name = " + getName());
        pw.println("isDiscovering() = " + isDiscovering());

        BluetoothHeadset headset = new BluetoothHeadset(mContext, null);

        pw.println("\n--Known devices--");
        for (String address : mDeviceProperties.keySet()) {
            int bondState = mBondState.getBondState(address);
            pw.printf("%s %10s (%d) %s\n", address,
                       toBondStateString(bondState),
                       mBondState.getAttempt(address),
                       getRemoteName(address));

            Map<ParcelUuid, Integer> uuidChannels = mDeviceServiceChannelCache.get(address);
            if (uuidChannels == null) {
                pw.println("\tuuids = null");
            } else {
                for (ParcelUuid uuid : uuidChannels.keySet()) {
                    Integer channel = uuidChannels.get(uuid);
                    if (channel == null) {
                        pw.println("\t" + uuid);
                    } else {
                        pw.println("\t" + uuid + " RFCOMM channel = " + channel);
                    }
                }
            }

            Map<ParcelUuid, Integer> uuidPsms = mDeviceL2capPsmCache.get(address);
            if (uuidPsms == null) {
                pw.println("\tuuids = null");
            } else {
                for (ParcelUuid uuid : uuidPsms.keySet()) {
                    Integer psm = uuidPsms.get(uuid);
                    if (psm == null) {
                        pw.println("\t" + uuid);
                    } else {
                        pw.println("\t" + uuid + " L2CAP PSM = " + psm);
                    }
                }
            }

            for (RemoteService service : mUuidCallbackTracker.keySet()) {
                if (service.address.equals(address)) {
                    pw.println("\tPENDING CALLBACK: " + service.uuid);
                }
            }
        }

        String value = getProperty("Devices");
        String[] devicesObjectPath = null;
        if (value != null) {
            devicesObjectPath = value.split(",");
        }
        pw.println("\n--ACL connected devices--");
        if (devicesObjectPath != null) {
            for (String device : devicesObjectPath) {
                pw.println(getAddressFromObjectPath(device));
            }
        }

        // Rather not do this from here, but no-where else and I need this
        // dump
        pw.println("\n--Headset Service--");
        switch (headset.getState(headset.getCurrentHeadset())) {
        case BluetoothHeadset.STATE_DISCONNECTED:
            pw.println("getState() = STATE_DISCONNECTED");
            break;
        case BluetoothHeadset.STATE_CONNECTING:
            pw.println("getState() = STATE_CONNECTING");
            break;
        case BluetoothHeadset.STATE_CONNECTED:
            pw.println("getState() = STATE_CONNECTED");
            break;
        case BluetoothHeadset.STATE_ERROR:
            pw.println("getState() = STATE_ERROR");
            break;
        }

        pw.println("\ngetCurrentHeadset() = " + headset.getCurrentHeadset());
        pw.println("getBatteryUsageHint() = " + headset.getBatteryUsageHint());
        headset.close();
        pw.println("\n--Application Service Records--");
        for (Integer handle : mServiceRecordToPid.keySet()) {
            Integer pid = mServiceRecordToPid.get(handle);
            pw.println("\tpid " + pid + " handle " + Integer.toHexString(handle));
        }
    }

    /* package */ static int bluezStringToScanMode(boolean pairable, boolean discoverable) {
        if (pairable && discoverable)
            return BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE;
        else if (pairable && !discoverable)
            return BluetoothAdapter.SCAN_MODE_CONNECTABLE;
        else
            return BluetoothAdapter.SCAN_MODE_NONE;
    }

    /* package */ static String scanModeToBluezString(int mode) {
        switch (mode) {
        case BluetoothAdapter.SCAN_MODE_NONE:
            return "off";
        case BluetoothAdapter.SCAN_MODE_CONNECTABLE:
            return "connectable";
        case BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE:
            return "discoverable";
        }
        return null;
    }

    /*package*/ String getAddressFromObjectPath(String objectPath) {
        String adapterObjectPath = getPropertyInternal("ObjectPath");
        if (adapterObjectPath == null || objectPath == null) {
            Log.e(TAG, "getAddressFromObjectPath: AdapterObjectPath:" + adapterObjectPath +
                    "  or deviceObjectPath:" + objectPath + " is null");
            return null;
        }
        if (!objectPath.startsWith(adapterObjectPath)) {
            Log.e(TAG, "getAddressFromObjectPath: AdapterObjectPath:" + adapterObjectPath +
                    "  is not a prefix of deviceObjectPath:" + objectPath +
                    "bluetoothd crashed ?");
            return null;
        }
        String address = objectPath.substring(adapterObjectPath.length());
        if (address != null) return address.replace('_', ':');

        Log.e(TAG, "getAddressFromObjectPath: Address being returned is null");
        return null;
    }

    /*package*/ String getObjectPathFromAddress(String address) {
        String path = getPropertyInternal("ObjectPath");
        if (path == null) {
            Log.e(TAG, "Error: Object Path is null");
            return null;
        }
        path = path + address.replace(":", "_");
        return path;
    }

    /*package */ void setLinkTimeout(String address, int num_slots) {
        String path = getObjectPathFromAddress(address);
        boolean result = setLinkTimeoutNative(path, num_slots);

        if (!result) log("Set Link Timeout to:" + num_slots + " slots failed");
    }

    public boolean connectHeadset(String address) {
        BluetoothDeviceProfileState state = mDeviceProfileState.get(address);
        if (state != null) {
            Message msg = new Message();
            msg.arg1 = BluetoothDeviceProfileState.CONNECT_HFP_OUTGOING;
            msg.obj = state;
            mHfpProfileState.sendMessage(msg);
            return true;
        }
        return false;
    }

    public boolean disconnectHeadset(String address) {
        BluetoothDeviceProfileState state = mDeviceProfileState.get(address);
        if (state != null) {
            Message msg = new Message();
            msg.arg1 = BluetoothDeviceProfileState.DISCONNECT_HFP_OUTGOING;
            msg.obj = state;
            mHfpProfileState.sendMessage(msg);
            return true;
        }
        return false;
    }

    public boolean connectSink(String address) {
        BluetoothDeviceProfileState state = mDeviceProfileState.get(address);
        if (state != null) {
            Message msg = new Message();
            msg.arg1 = BluetoothDeviceProfileState.CONNECT_A2DP_OUTGOING;
            msg.obj = state;
            mA2dpProfileState.sendMessage(msg);
            return true;
        }
        return false;
    }

    public boolean disconnectSink(String address) {
        BluetoothDeviceProfileState state = mDeviceProfileState.get(address);
        if (state != null) {
            Message msg = new Message();
            msg.arg1 = BluetoothDeviceProfileState.DISCONNECT_A2DP_OUTGOING;
            msg.obj = state;
            mA2dpProfileState.sendMessage(msg);
            return true;
        }
        return false;
    }

    private BluetoothDeviceProfileState addProfileState(String address) {
        BluetoothDeviceProfileState state = mDeviceProfileState.get(address);
        if (state != null) return state;

        state = new BluetoothDeviceProfileState(mContext, address, this, mA2dpService);
        mDeviceProfileState.put(address, state);
        state.start();
        return state;
    }

    private void initProfileState() {
        String []bonds = null;
        String val = getPropertyInternal("Devices");
        if (val != null) {
            bonds = val.split(",");
        }
        if (bonds == null) {
            return;
        }

        for (String path : bonds) {
            String address = getAddressFromObjectPath(path);
            BluetoothDeviceProfileState state = addProfileState(address);
            // Allow 8 secs for SDP records to get registered.
            Message msg = new Message();
            msg.what = BluetoothDeviceProfileState.AUTO_CONNECT_PROFILES;
            state.sendMessageDelayed(msg, 8000);
        }
    }

    public boolean notifyConnectA2dp(String address) {
        BluetoothDeviceProfileState state =
             mDeviceProfileState.get(address);
        if (state != null) {
            Message msg = new Message();
            msg.what = BluetoothDeviceProfileState.CONNECT_OTHER_PROFILES;
            msg.arg1 = BluetoothDeviceProfileState.CONNECT_A2DP_OUTGOING;
            state.sendMessage(msg);
            return true;
        }
        return false;
    }

    public boolean notifyIncomingConnection(String address) {
        BluetoothDeviceProfileState state =
             mDeviceProfileState.get(address);
        if (state != null) {
            Message msg = new Message();
            msg.what = BluetoothDeviceProfileState.CONNECT_HFP_INCOMING;
            state.sendMessage(msg);
            return true;
        }
        return false;
    }

    /*package*/ boolean notifyIncomingA2dpConnection(String address) {
       BluetoothDeviceProfileState state =
            mDeviceProfileState.get(address);
       if (state != null) {
           Message msg = new Message();
           msg.what = BluetoothDeviceProfileState.CONNECT_A2DP_INCOMING;
           state.sendMessage(msg);
           return true;
       }
       return false;
    }

    /*package*/ void setA2dpService(BluetoothA2dpService a2dpService) {
        mA2dpService = a2dpService;
    }

    /*package*/ Integer getAuthorizationAgentRequestData(String address) {
        Integer data = mEventLoop.getAuthorizationAgentRequestData().remove(address);
        return data;
    }

    public void sendProfileStateMessage(int profile, int cmd) {
        Message msg = new Message();
        msg.what = cmd;
        if (profile == BluetoothProfileState.HFP) {
            mHfpProfileState.sendMessage(msg);
        } else if (profile == BluetoothProfileState.A2DP) {
            mA2dpProfileState.sendMessage(msg);
        }
    }

    private void createIncomingConnectionStateFile() {
        File f = new File(INCOMING_CONNECTION_FILE);
        if (!f.exists()) {
            try {
                f.createNewFile();
            } catch (IOException e) {
                Log.e(TAG, "IOException: cannot create file");
            }
        }
    }

    /** @hide */
    public Pair<Integer, String> getIncomingState(String address) {
        if (mIncomingConnections.isEmpty()) {
            createIncomingConnectionStateFile();
            readIncomingConnectionState();
        }
        return mIncomingConnections.get(address);
    }

    private void readIncomingConnectionState() {
        synchronized(mIncomingConnections) {
            FileInputStream fstream = null;
            try {
              fstream = new FileInputStream(INCOMING_CONNECTION_FILE);
              DataInputStream in = new DataInputStream(fstream);
              BufferedReader file = new BufferedReader(new InputStreamReader(in));
              String line;
              while((line = file.readLine()) != null) {
                  line = line.trim();
                  if (line.length() == 0) continue;
                  String[] value = line.split(",");
                  if (value != null && value.length == 3) {
                      Integer val1 = Integer.parseInt(value[1]);
                      Pair<Integer, String> val = new Pair(val1, value[2]);
                      mIncomingConnections.put(value[0], val);
                  }
              }
            } catch (FileNotFoundException e) {
                log("FileNotFoundException: readIncomingConnectionState" + e.toString());
            } catch (IOException e) {
                log("IOException: readIncomingConnectionState" + e.toString());
            } finally {
                if (fstream != null) {
                    try {
                        fstream.close();
                    } catch (IOException e) {
                        // Ignore
                    }
                }
            }
        }
    }

    private void truncateIncomingConnectionFile() {
        RandomAccessFile r = null;
        try {
            r = new RandomAccessFile(INCOMING_CONNECTION_FILE, "rw");
            r.setLength(0);
        } catch (FileNotFoundException e) {
            log("FileNotFoundException: truncateIncomingConnectionState" + e.toString());
        } catch (IOException e) {
            log("IOException: truncateIncomingConnectionState" + e.toString());
        } finally {
            if (r != null) {
                try {
                    r.close();
                } catch (IOException e) {
                    // ignore
                 }
            }
        }
    }

    /** @hide */
    public void writeIncomingConnectionState(String address, Pair<Integer, String> data) {
        synchronized(mIncomingConnections) {
            mIncomingConnections.put(address, data);

            truncateIncomingConnectionFile();
            BufferedWriter out = null;
            StringBuilder value = new StringBuilder();
            try {
                out = new BufferedWriter(new FileWriter(INCOMING_CONNECTION_FILE, true));
                for (String devAddress: mIncomingConnections.keySet()) {
                  Pair<Integer, String> val = mIncomingConnections.get(devAddress);
                  value.append(devAddress);
                  value.append(",");
                  value.append(val.first.toString());
                  value.append(",");
                  value.append(val.second);
                  value.append("\n");
                }
                out.write(value.toString());
            } catch (FileNotFoundException e) {
                log("FileNotFoundException: writeIncomingConnectionState" + e.toString());
            } catch (IOException e) {
                log("IOException: writeIncomingConnectionState" + e.toString());
            } finally {
                if (out != null) {
                    try {
                        out.close();
                    } catch (IOException e) {
                        // Ignore
                    }
                }
            }
        }
    }

    private static void log(String msg) {
        Log.d(TAG, msg);
    }


    public synchronized String getGattServiceProperty(String path, String property) {
        Map<String, String> properties = mGattProperties.get(path);
        Log.d(TAG, "getGattServiceProperty: " + property + ", path "+ path);
        if (properties != null) {
            return properties.get(property);
        } else {
            // Query for GATT service properties, again.
            if (updateGattServicePropertiesCache(path))
                return getGattServiceProperty(path, property);
        }
        Log.e(TAG, "getGattServiceProperty: " + property + " not present: " + path);
        return null;
    }

    /* package */ synchronized boolean updateGattServicePropertiesCache(String path) {
        String[] propValues = (String []) getGattServicePropertiesNative(path);
        if (propValues != null) {
            addGattServiceProperties(path, propValues);
            return true;
        }
        return false;
    }

    /* package */ synchronized void addGattServiceProperties(String path, String[] properties) {
        Map<String, String> propertyValues = mGattProperties.get(path);
        if (propertyValues == null) {
            propertyValues = new HashMap<String, String>();
        }

        for (int i = 0; i < properties.length; i++) {
            String name = properties[i];
            String newValue = null;
            int len;
            if (name == null) {
                Log.e(TAG, "Error: Gatt Service Property at index" + i + "is null");
                continue;
            }
            if (name.equals("Characteristics")) {
                StringBuilder str = new StringBuilder();
                len = Integer.valueOf(properties[++i]);
                for (int j = 0; j < len; j++) {
                    str.append(properties[++i]);
                    str.append(",");
                }
                if (len > 0) {
                    newValue = str.toString();
                }
            } else {
                newValue = properties[++i];
            }
            propertyValues.put(name, newValue);
        }

        mGattProperties.put(path, propertyValues);

    }

    /* package */ void removeGattServiceProperties(String path) {
        mGattProperties.remove(path);
    }

    private synchronized String[] getRemoteGattServices(String address) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return null;
        }
        return getGattServicesFromCache(address);
    }

   private String[] getGattServicesFromCache(String address) {

       Log.d (TAG, "getGattServicesFromCache");

       String value = getRemoteDeviceProperty(address, "Services");
        if (value == null) {
            Log.d(TAG, "getGattServicesFromCache: no services found");
            return null;
        }

        String[] path = null;
        // The paths are stored as a "," separated string.
        path = value.split(",");

        return path;
    }

    private String[] matchGattService(String address, ParcelUuid uuid)
    {
        // The properties should be cached at this point
        String value = getRemoteDeviceProperty(address, "Services");
        if (value == null) {
            Log.e(TAG, "matchGattService: No GATT based services were found on " + address);
            return null;
        } else {
            Log.d(TAG, "matchGattService: Value " + value);
        }

        String[] gattServicePaths = null;
        // The  object paths are stored as a "," separated string.
        gattServicePaths = value.split(",");

        ArrayList<String> matchList = new ArrayList<String>();
        int count = 0;
        String stringUuid;

        stringUuid = uuid.toString();

        Log.d(TAG, "Requested GATT UUID to match: " + stringUuid);

        for (int i  = 0; i < gattServicePaths.length; i++) {
            boolean match = true;
            String serviceUuid = getGattServiceProperty(gattServicePaths[i], "UUID");

            if (serviceUuid != null) {
                Log.d(TAG, "Found GATT UUID: " + serviceUuid);

                if (!serviceUuid.equalsIgnoreCase(stringUuid)){
                        Log.d(TAG,"UUID does not match");
                        match = false;
                }

                if (match) {
                    matchList.add(gattServicePaths[i]);
                    count++;
                }
            }
        }

        if (count == 0)
            return null;

        Log.d(TAG,"Found " + count+ " instances of service " + stringUuid);

        String[] ret = new String[count];

        matchList.toArray(ret);

        return ret;
    }

   /* Broadcast the GATT services intent */
    /*package*/ synchronized void sendGattIntent(String address, int result) {
        Intent intent = new Intent(BluetoothDevice.ACTION_GATT);
        ParcelUuid[] uuids = null;
        String[] gattPath;
        int count = 1;
        int i;
        boolean isScheduled = false;

        synchronized (this) {
            if (mGattIntentTracker.containsKey(address)) {

                ArrayList<ParcelUuid> serviceUuids = mGattIntentTracker.get(address);
                isScheduled = true;

                if(serviceUuids != null) {
                    uuids = new ParcelUuid[serviceUuids.size()];
                    uuids = serviceUuids.toArray(uuids);
                }

                Log.d(TAG, "Clear GATT INTENT tracker");
                mGattIntentTracker.remove(address);
            }
        }

        if (!isScheduled)
            return;

        if (uuids != null) {
            count = uuids.length;
            for (i = 0; i < count; i++) {

                gattPath = matchGattService (address, uuids[i]);

                intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
                intent.putExtra(BluetoothDevice.EXTRA_UUID, uuids[i]);
                intent.putExtra(BluetoothDevice.EXTRA_GATT, gattPath);
                intent.putExtra(BluetoothDevice.EXTRA_GATT_RESULT, result);
                mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);
            }
        } else {
            Log.d(TAG, "Send intents about all services found on the remote devices");

            String value = getRemoteDeviceProperty(address, "Services");
            if (value == null) {
                Log.e(TAG, "No GATT based services were found on " + address);
                return;
            }
            Log.d(TAG, "GattServices: " + value);

            String[] gattServicePaths = null;
            // The  object paths are stored as a "," separated string.
            gattServicePaths = value.split(",");

            //Send one intent per GATT service
            for (i  = 0; i < gattServicePaths.length; i++) {
                String serviceUuid = getGattServiceProperty(gattServicePaths[i], "UUID");
                intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mAdapter.getRemoteDevice(address));
                intent.putExtra(BluetoothDevice.EXTRA_UUID, serviceUuid);
                intent.putExtra(BluetoothDevice.EXTRA_GATT, gattServicePaths[i]);
                intent.putExtra(BluetoothDevice.EXTRA_GATT_RESULT, result);
                mContext.sendBroadcast(intent, BLUETOOTH_ADMIN_PERM);
            }
        }
    }

    private String[] getCharacteristicsFromCache(String servicePath) {
        String value = getGattServiceProperty(servicePath, "Characteristics");
        if (value == null) {
            return null;
        }

        String[] paths = null;
        // The Charateristic paths are stored as a "," separated string.
        paths = value.split(",");
        return paths;
    }

    /*package*/ synchronized void makeDiscoverCharacteristicsCallback(String servicePath, boolean result) {
        IBluetoothGattService callback = mGattServiceTracker.get(servicePath);

        Log.d(TAG, "makeDiscoverCharacteristicsCallback for service: " + servicePath);

        if (callback != null) {
            String[]  charPaths = null;
            if (result)
                charPaths = getCharacteristicsFromCache(servicePath);
            try {
                callback.onCharacteristicsDiscovered(charPaths, result);
            } catch (RemoteException e) {Log.e(TAG, "", e);}
        } else
            Log.d(TAG, "Discover Characteristics Callback for  service " + servicePath + " not queued");

    }

    /*package*/ synchronized void makeSetCharacteristicPropertyCallback(String charPath, String property, boolean result) {
        Log.d(TAG, "makeSetCharacteristicPropertyCallback for char: " + charPath);

        if (charPath == null) {
            return;
        }

        String servicePath = charPath.substring(0, charPath.indexOf("/characteristic"));

        if (servicePath == null) {
            return;
        }

        IBluetoothGattService callback = mGattServiceTracker.get(servicePath);

        if (callback != null) {
            try {
                callback.onSetCharacteristicProperty(charPath, property, result);
            } catch (RemoteException e) {Log.e(TAG, "", e);}
        } else
            Log.d(TAG, "Set Characteristics Property Callback for  service " + servicePath + " not queued");

    }

    /*package*/ synchronized void makeWatcherValueChangedCallback(String charPath, String value) {

        if (charPath == null) {
            return;
        }

        String servicePath = charPath.substring(0, charPath.indexOf("/characteristic"));

        if (servicePath == null) {
            return;
        }

        Log.d(TAG, "WatcherValueChanged : service Path = " + servicePath);

        IBluetoothGattService callback = mGattWatcherTracker.get(servicePath);

        if (callback != null) {
            try {
                callback.onValueChanged(charPath, value);
            } catch (RemoteException e) {Log.e(TAG, "", e);}
        } else {
            Log.d(TAG, "Callback for service " + servicePath + " not registered");
        }
    }

    /*package*/ synchronized void makeUpdateCharacteristicValueCallback(String charPath, boolean result) {

        String servicePath = charPath.substring(0, charPath.indexOf("/characteristic"));

        if (servicePath == null) {
            return;
        }

        IBluetoothGattService callback = mGattServiceTracker.get(servicePath);

        Log.d(TAG, "makeCharacteristicValueUpdatedCallback for service: " + charPath);

        if (callback != null) {
            try {
                callback.onCharacteristicValueUpdated(charPath, result);
            } catch (RemoteException e) {Log.e(TAG, "", e);}
        } else {
            Log.d(TAG, "Callback for service " + servicePath + " not registered");
        }
    }

   /**
     * Bluetooth: Support for GATT Update Characteristic Value
     * Returns the user-friendly name of a GATT based service. This value is
     * returned from our local cache.
     */
    public String getGattServiceName(String path) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!BluetoothAdapter.checkBluetoothAddress(getAddressFromObjectPath(path))) {
            return null;
        }

        return getGattServiceProperty(path, "Name");

    }

   /**
     * Connect and fetch object paths for GATT based services.
     * TODO: for BR/EDR use SDP mechanism
     */
    public synchronized boolean getGattServices(String address, ParcelUuid uuid) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return false;

        Log.d(TAG, "getGattServices");

        if (!BluetoothAdapter.checkBluetoothAddress(address)) {
            return false;
        }

        boolean ret = true;
        boolean delay = false;
        boolean discovering = false;

        synchronized (this) {
            discovering = mGattIntentTracker.containsKey(address);
        }

        if (!discovering) {
            if (isRemoteDeviceInCache(address) && getRemoteGattServices(address) != null && findDeviceNative(address) != null) {
                String value = getRemoteDeviceProperty(address, "Services");
                if (value == null) {
                    Log.e(TAG, "No GATT based services were found on " + address);
                    ret = false;
                }
            } else {
                Log.d(TAG, "Need to Create Remote Device" + address + " before accessing properties");
                ret = createDeviceNative(address);
                delay = true;
            }
        } else
                Log.d(TAG, "GATT service discovery for remote device " + address + "is in progress");

        if (!ret)
            return false;

        ArrayList<ParcelUuid> serviceUuids;
        synchronized (this) {
            if (mGattIntentTracker.containsKey(address)) {
                serviceUuids = mGattIntentTracker.get(address);
                mGattIntentTracker.remove(address);
            } else {
                serviceUuids = new ArrayList<ParcelUuid>();
            }

            serviceUuids.add(uuid);
            mGattIntentTracker.put(address, serviceUuids);
        }

        if (!discovering) {
            Message message = mHandler.obtainMessage(MESSAGE_GATT_INTENT);
            message.obj = address;
            if (delay)
                mHandler.sendMessageDelayed(message, GATT_INTENT_DELAY);
            else
                mHandler.sendMessage(message);
        }

        return true;
   }

    public synchronized boolean discoverCharacteristics(String path) {
       mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return false;

        Log.d(TAG, "discoverCharacteristics");

       if (!mGattServices.containsKey(path)) {
            Log.d(TAG, "Service not present " + path);
            return false;
        }

        boolean ret = discoverCharacteristicsNative(path);

        return ret;
   }

    public synchronized String[] getCharacteristicProperties(String path) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return null;

        Log.d(TAG, "getCharacteristicProperties");

        if (path == null) {
            return null;
        }

        String servicePath = path.substring(0, path.indexOf("/characteristic"));

        if (servicePath == null) {
            return null;
        }

       if (!mGattServices.containsKey(servicePath)) {
            Log.d(TAG, "Service not present " + servicePath);
            return null;
        }

        String[] propValues = (String []) getCharacteristicPropertiesNative(path);
        return propValues;
    }

    public synchronized boolean setCharacteristicProperty(String path, String key, byte[] value, boolean reliable) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return false;

        if (path == null) {
            return false;
        }

        String servicePath = path.substring(0, path.indexOf("/characteristic"));

        if (servicePath == null) {
            return false;
        }

        if (!mGattServices.containsKey(servicePath)) {
            Log.d(TAG, "Service not present " + servicePath);
            return false;
        }

        boolean ret = setCharacteristicPropertyNative(path, key, value, value.length, reliable);

        return ret;
    }

    public synchronized boolean updateCharacteristicValue(String path) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return false;

        Log.d(TAG, "updateCharacteristicValue");

        if (path == null) {
            return false;
        }

        String servicePath = path.substring(0, path.indexOf("/characteristic"));

        if (servicePath == null) {
            return false;
        }

       if (!mGattServices.containsKey(servicePath)) {
            Log.d(TAG, "Service not present " + servicePath);
            return false;
        }

        return updateCharacteristicValueNative(path);
    }

    public synchronized boolean registerCharacteristicsWatcher(String path, IBluetoothGattService gattCallback) {
       mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return false;

        Log.d(TAG, "registerCharacteristicsWatcher");

       if (!mGattServices.containsKey(path)) {
            Log.d(TAG, "Service not present " + path);
            return false;
        }

        if (mGattWatcherTracker.get(path) != null) {
            // Do not add this callback
            Log.d(TAG, "registerCharacteristicsWatcher: already registered for " + path);
            return false;
        }

        boolean ret = registerCharacteristicsWatcherNative(path);

        if (ret == true) {
            mGattWatcherTracker.put(path, gattCallback);
        }

        return ret;
   }

    public synchronized boolean deregisterCharacteristicsWatcher(String path) {
       mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return false;

       if (!mGattServices.containsKey(path)) {
            Log.d(TAG, "Service not present " + path);
            return false;
        }

        Log.d(TAG, "deregisterCharacteristicsWatcher");

        boolean ret = deregisterCharacteristicsWatcherNative(path);

        mGattWatcherTracker.remove(path);

        return ret;
    }

    public synchronized void disconnectSap() {
        Log.d(TAG, "disconnectSap");
        int res = disConnectSapNative();
        Log.d(TAG, "disconnectSap returns -" + res);
        return;
    }

    public synchronized boolean startRemoteGattService(String path, IBluetoothGattService gattCallback) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return false;

        Log.d(TAG, "startRemoteGattService(");

        if (mGattServiceTracker.get(path) != null) {
            // Do not add this callback, its already there
            Log.d(TAG, "startRemoteGattService: callback already registered " + path);
            return false;
        }

        mGattServiceTracker.put(path, gattCallback);

        if (!mGattServices.containsKey(path))
            mGattServices.put(path, 1);
        else {
            Integer refCount = mGattServices.get(path);
            refCount++;
            mGattServices.remove(path);
            mGattServices.put(path, refCount);
        }

        return true;
    }

    public synchronized void closeRemoteGattService(String path) {
        mContext.enforceCallingOrSelfPermission(BLUETOOTH_PERM, "Need BLUETOOTH permission");
        if (!isEnabledInternal()) return;

        if (!mGattServices.containsKey(path)) {
            Log.d(TAG, "removeRemoteGattService: service not found " + path);
            return;
        }

        Integer refCount = mGattServices.get(path);
        refCount--;

        Log.d(TAG, "removeRemoteGattService: refCount for " + path + " is " + refCount);

        if (refCount > 0) {
            mGattServices.remove(path);
            mGattServices.put(path, refCount);
            return;
        }

        Map<String, String> properties = mGattProperties.get(path);

        if (properties != null) {
            String chars = properties.get("Characteristics");

            if (chars != null) {
                String[] charPaths = chars.split(",");

                for (int i = 0; i < charPaths.length; i++)
                    mGattServiceTracker.remove(charPaths[i]);
            }
        }

        removeGattServiceProperties(path);
        mGattServiceTracker.remove(path);
        mGattServices.remove(path);

        //Check if we can disconnect from the remote device (LE only)
        String devicePath = path.substring(0, path.indexOf("/service"));

        if (devicePath == null)
            return;

        String address = getAddressFromObjectPath(devicePath);

        if(address == null) {
            Log.d(TAG, "adress is null????");
            return;
        }

        SortedMap subMap = mGattServices.tailMap(devicePath);

        if (!subMap.isEmpty()) {
            String nextServicePath = (String) subMap.firstKey();
            if (devicePath.equals(nextServicePath.substring(0, path.indexOf("/service")))) {
                Log.d(TAG, "removeRemoteGattService: more GATT services are running on device " + nextServicePath);
                // There are still other GATT services used on this remote device
            }
        }

        Log.d(TAG, "removeRemoteGattService: disconnect" + address);

        boolean res;
        res = disconnectGattNative(path);
        Log.d(TAG, "disconnectGatt " + res);

    }

    /*package*/ synchronized void  clearRemoteDeviceGattServices(String address) {
        Log.d(TAG, "clearRemoteDeviceGattServices");

       String value = getRemoteDeviceProperty(address, "Services");
       if (value == null) {
            return;
        }

        String[] services = null;
        services = value.split(",");

        for(int i = 0; i < services.length; i++)
            closeRemoteGattService(services[i]);

        setRemoteDeviceProperty(address, "Services", null);
    }


    private native static void classInitNative();
    private native void initializeNativeDataNative();
    private native boolean setupNativeDataNative();
    private native boolean tearDownNativeDataNative();
    private native void cleanupNativeDataNative();
    private native String getAdapterPathNative();

    private native int isEnabledNative();
    private native int enableNative();
    private native int disableNative();

    private native Object[] getAdapterPropertiesNative();
    private native Object[] getDevicePropertiesNative(String objectPath);
    private native boolean setAdapterPropertyStringNative(String key, String value);
    private native boolean setAdapterPropertyIntegerNative(String key, int value);
    private native boolean setAdapterPropertyBooleanNative(String key, int value);

    private native boolean startDiscoveryNative();
    private native boolean stopDiscoveryNative();
    private native int listConnectionNative();
    private native boolean createPairedDeviceNative(String address, int timeout_ms);
    private native boolean createPairedDeviceOutOfBandNative(String address, int timeout_ms);
    private native byte[] readAdapterOutOfBandDataNative();

    private native boolean cancelDeviceCreationNative(String address);
    private native boolean removeDeviceNative(String objectPath);
    private native int getDeviceServiceChannelNative(String objectPath, String uuid,
            int attributeId);
    private native String getDeviceStringAttrValue(String objectPath, String uuid,
            int attributeId);

    private native boolean cancelPairingUserInputNative(String address, int nativeData);
    private native boolean setPinNative(String address, String pin, int nativeData);
    private native boolean sapAuthorizeNative(String address, boolean access, int nativeData);
    private native boolean setPasskeyNative(String address, int passkey, int nativeData);
    private native boolean setPairingConfirmationNative(String address, boolean confirm,
            int nativeData);
    private native boolean setRemoteOutOfBandDataNative(String address, byte[] hash,
                                                        byte[] randomizer, int nativeData);

    private native boolean setDevicePropertyBooleanNative(String objectPath, String key,
            int value);
    private native boolean setDevicePropertyIntegerNative(String objectPath, String key,
            int value);

    private native boolean createDeviceNative(String address);
    /*package*/ native boolean discoverServicesNative(String objectPath, String pattern);

    private native int addRfcommServiceRecordNative(String name, long uuidMsb, long uuidLsb,
            short channel);
    private native boolean removeServiceRecordNative(int handle);
    private native boolean setLinkTimeoutNative(String path, int num_slots);
    native boolean setAuthorizationNative(String address, boolean value, int data);
    private native String findDeviceNative(String address);

    private native Object[] getGattServicePropertiesNative(String path);
    private native boolean discoverCharacteristicsNative(String path);
    private native Object[] getCharacteristicPropertiesNative(String path);
    private native boolean setCharacteristicPropertyNative(String path, String key, byte[] value, int length, boolean reliable);
    private native boolean updateCharacteristicValueNative(String path);
    private native boolean registerCharacteristicsWatcherNative(String path);
    private native boolean deregisterCharacteristicsWatcherNative(String path);
    private native int disConnectSapNative();
    private native boolean disconnectGattNative(String path);
}
