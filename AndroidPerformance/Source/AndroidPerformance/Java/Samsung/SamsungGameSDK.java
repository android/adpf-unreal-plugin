package com.samsung.android;

import android.util.Log;
import com.samsung.android.gamesdk.GameSDKManager;

public class SamsungGameSDK implements GameSDKManager.Listener
{
    public native void nativeOnHighTempWarning(int warningLevel);

    public SamsungGameSDK() {
        mManager = null;
        if (!init()) {
            mManager = null;
        }
    }
    
    boolean init() {
        Log.e("SamsungGameSDK", "Initializing SamsungGameSDK");
        mManager = new GameSDKManager();

        if (!mManager.isAvailable()) {
            Log.e("SamsungGameSDK", "GameSDKManager is not available");
            return false;
        }
        if (!mManager.initialize("3.2")) {
            Log.e("SamsungGameSDK", "GameSDKManager failed to initialize");
            return false;
        }
        mVersion = Float.parseFloat(mManager.getVersion());

        if(mVersion < 1.5f) {
            Log.e("SamsungGameSDK", "GameSDK version is below 1.5, we don't support that");
            return false;
        }
        if (!mManager.setListener(this)) {
            Log.e("SamsungGameSDK", "Failed to attach thermal listeners");
            return false;
        }

        return true;
    }

    @Override
    public void onHighTempWarning(int warningLevel) {
        synchronized(mTemperatureWarningLevel) {
            mTemperatureWarningLevel = warningLevel;
        }
        nativeOnHighTempWarning(warningLevel);
    }

    @Override
    public void onRefreshRateChanged() {}

    @Override
    public void onReleasedByTimeout() {}
        
    @Override
    public void onReleasedCpuBoost() {}

    @Override
    public void onReleasedGpuBoost() {}

    public boolean IsAvailable() {
        return mManager != null;
    }

    public String GetVersion() {
        return mManager == null ? "(none)" : mManager.getVersion();
    }

    // returns temperature level in range [0-7], 7 meaning the highest level
    public int GetTemperatureLevel() {
        return mManager.getTempLevel();
    }

    public int GetTemperatureWarningLevel() {
        synchronized(mTemperatureWarningLevel) {
            return mTemperatureWarningLevel;
        }
    }

    // returns temperature level in range [0-7], 7 meaning the highest level
    public int GetSkinTempLevel() {
        if (mVersion >= 1.6) {
            return mManager.getSkinTempLevel();
        }
        return mManager.getTempLevel();
    }

    // returns temperature level in range [0-6], 7 meaning the highest level
    public int GetCpuJTLevel() {
        return mManager.getCpuJTLevel();
    }

    // returns temperature level in range [0-6], 7 meaning the highest level
    public int GetGpuJTLevel() {
        return mManager.getGpuJTLevel();
    }

    public double GetHighPrecisionTemp() {
        if (mVersion >= 3.0f) {
            Log.e("SamsungGameSDK", "Logging getHighPrecisionSkinTempLevel()");
            return mManager.getHighPrecisionSkinTempLevel();
        } else {
            return GetSkinTempLevel() / 7.0; // Remap from [0, 7] to the [0, 10]
        }
    }

    private GameSDKManager mManager = null;
    private Integer mTemperatureWarningLevel = 0;
    private Float mVersion = 0.0f; // @TODO Replace with int
}
