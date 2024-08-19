#include "AndroidNativeProvider.h"

#ifdef ADPF_COMPILE_ANDROID_NATIVE_PROVIDER

#include <android/thermal.h>

#include "AndroidPerformanceLog.h"

void AndroidNativeProvider::ThermalStatusCallback(void *data, ThermalStatus status) {
    UE_LOG(LogAndroidPerformance, Log, TEXT("Thermal callback, thermal status %d!"), status);
    // @FIX do proper conversion
    static_cast<AndroidNativeProvider*>(data)->thermal_status_ = (ThermalStatus)status;
}

AndroidNativeProvider::AndroidNativeProvider() {
    is_available_ = Init();
}

bool AndroidNativeProvider::Init() {
    thermal_status_  = ThermalStatus::NONE;
    thermal_manager_ = AThermal_acquireManager();

    if (!thermal_manager_) {
        UE_LOG(LogAndroidPerformance, Error, TEXT("AndroidNativeProvider: AThermal_acquireManager() failed"));
        return false;
    }
    if (FMath::IsNaN(GetThermalHeadroom(0))) {
        UE_LOG(LogAndroidPerformance, Error, TEXT("AndroidNativeProvider: Thermal Headroom returned NaN"));
        return false;
    }
    if (!RegisterCallback(ThermalStatusCallback)) {
        UE_LOG(LogAndroidPerformance, Error, TEXT("AndroidNativeProvider: Failed to register thermal status callback"));
        return false;
    }

    UE_LOG(LogAndroidPerformance, Log, TEXT("AndroidNativeProvider: Initialized successfully"));
    return true;
}

ThermalStatus AndroidNativeProvider::GetThermalStatus() const {
    return thermal_status_;
}

float AndroidNativeProvider::GetThermalHeadroom(int forecast_seconds) const {
    return AThermal_getThermalHeadroom(thermal_manager_, forecast_seconds);
}

bool AndroidNativeProvider::RegisterCallback(ThermalCallback callback) {
    auto ret = AThermal_registerThermalStatusListener(
                    thermal_manager_, reinterpret_cast<void (*)(void *, AThermalStatus)>(callback), this);

    if (!ret) {
        UE_LOG(LogAndroidPerformance, Log, TEXT("Thermal status callback registered"));
    } else {
        UE_LOG(LogAndroidPerformance, Error, TEXT("Failed to register thermal status callback with return value: %d"), ret);
    }

    return ret == 0;
}

void AndroidNativeProvider::UnregisterCallback(ThermalCallback callback) {
    auto ret = AThermal_unregisterThermalStatusListener(
                    thermal_manager_, reinterpret_cast<void (*)(void *, AThermalStatus)>(callback), this);
    UE_LOG(LogAndroidPerformance, Log, TEXT("Thermal Status callback unregisterred:%d"), ret);
}

AndroidNativeProvider::~AndroidNativeProvider() {
    if (thermal_manager_) {
        UnregisterCallback(ThermalStatusCallback);
        AThermal_releaseManager(thermal_manager_);
    }
}

#endif // ADPF_COMPILE_ANDROID_NATIVE_PROVIDER
