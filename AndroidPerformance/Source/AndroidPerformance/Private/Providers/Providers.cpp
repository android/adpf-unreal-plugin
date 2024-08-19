#include "Providers.h"

#include "AndroidPerformanceLog.h"

#include "AndroidNativeProvider.h"
#include "AndroidJNIProvider.h"
#include "SamsungProvider.h"

TUniquePtr<IProvider> CreateThermalProvider() {
    // Priority 1: Use Android Native Thermal API 
    // Available in Android API level 31 and higher
    // https://developer.android.com/ndk/reference/group/thermal
    #ifdef ADPF_COMPILE_ANDROID_NATIVE_PROVIDER
    if (android_get_device_api_level() >= 31) {
        auto provider = MakeUnique<AndroidNativeProvider>();
        if (provider->IsAvailable()) return provider;
    }
    #endif

    // Priority 2: Use Android Java PowerManager API 
    // Available in Android API level 30 and higher
    // https://developer.android.com/reference/android/os/PowerManager
    #ifdef ADPF_COMPILE_ANDROID_JNI_PROVIDER
    if (android_get_device_api_level() >= 30) {
        auto provider = MakeUnique<AndroidJNIProvider>();
        if (provider->IsAvailable()) return provider;
    }
    #endif

    #ifdef ADPF_COMPILE_SAMSUNG_PROVIDER
    // Priority 3: Use Samsung GameSDK API 
    // Available in Android API level 28 and higher
    // No public documentation exists, sorry...
    if (android_get_device_api_level() >= 28) {
        auto provider = MakeUnique<SamsungProvider>();
        if (provider->IsAvailable()) return provider;
    }
    #endif

    // If nothing is supported return null as a sing of fail
    return nullptr;
}
