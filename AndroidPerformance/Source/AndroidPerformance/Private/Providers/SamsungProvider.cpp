#include "SamsungProvider.h"

#ifdef ADPF_COMPILE_SAMSUNG_PROVIDER

#include "AndroidPerformanceLog.h"

ThermalStatus ToThermalStatus(int warning_level) {
    // Below is from GameSDKManager documentation
    /**
         * Listener interface for High Temperature notice.
         * @param warningLevel indicates the warning level(Range: 0~2) for the temperature of the device.
         * The meaning of warningLevel '0' is that the temperature of device is low. Which means that the "setLevelWithScene" API can be used.
         * The meaning of warningLevel '1' is that the temperature of device has risen above a certain temperature. This means thermal throttling will approach soon. Therefore, it is necessary to take action to lower the temperature of the device.
         * The meaning of warningLevel '2' is that it can no longer guarantee a certain  cpu/gpuLevel of performance. So, "setLevelWithScene" API will be released. App can't use "setLevelWithScene" API.
    */
    switch (warning_level) {
        // GameSDK has only 3 values, so map it in a reasonable sense to ThermalStatus enum
        case 0:    return ThermalStatus::NONE;
        case 1:    return ThermalStatus::MODERATE;
        case 2:    return ThermalStatus::SEVERE;
        case -999: return ThermalStatus::ERROR;
    };

    checkNoEntry();
    return ThermalStatus::ERROR;
}

SamsungProvider::SamsungProvider() {
    is_available_ = Init();
}

bool SamsungProvider::Init() {
    jni_gamesdk_ = MakeUnique<FJavaSamsungGameSDK>();

    if (!jni_gamesdk_->IsAvailable()) {
        UE_LOG(LogAndroidPerformance, Error, TEXT("SamsungProvider: GameSDK is not available on this device"));
        return false;
    }

    FString version = jni_gamesdk_->GetVersion();
    UE_LOG(LogAndroidPerformance, Log, TEXT("SamsungProvider: Initialized successfully (GameSDK version %s loaded)"), *version);

    return true;
}

static TArray<ThermalCallback*> g_callbacks;    // List of all callbacks to be invoked by native java callback
static TArray<void*>            g_callback_arg; // List of all void* arguments to be passed to corresponding callbacks

extern "C"
JNIEXPORT void JNICALL Java_com_samsung_android_SamsungGameSDK_nativeOnHighTempWarning(JNIEnv *env, jobject object, int warning_level) {
    ThermalStatus status = ToThermalStatus(warning_level);

    for (auto c : g_callbacks) {
        c(nullptr, status);
    }
}

ThermalStatus SamsungProvider::GetThermalStatus() const {
    return ToThermalStatus(jni_gamesdk_->GetTemperatureWarningLevel());
}

float SamsungProvider::GetThermalHeadroom(int forecast_seconds) const {
    // @TODO Do predicition here
    return jni_gamesdk_->GetHighPrecisionTemp();
}

bool SamsungProvider::RegisterCallback(ThermalCallback callback) {
    g_callbacks.Push(callback);

    UE_LOG(LogAndroidPerformance, Log, TEXT("SamsungProvider: Thermal status callback registered"));
    return true;
}

void SamsungProvider::UnregisterCallback(ThermalCallback callback) {
    size_t index = g_callbacks.Find(callback);

    if (index == INDEX_NONE) {
        UE_LOG(LogAndroidPerformance, Error, TEXT("SamsungProvider: Attempted unregistering a non registered callback."));
        checkNoEntry();
        return;
    }

    g_callbacks.RemoveAt(index);
}

SamsungProvider::~SamsungProvider() {}

#endif // ADPF_COMPILE_SAMSUNG_PROVIDER
