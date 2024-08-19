#include "AndroidJNIProvider.h"

#ifdef ADPF_COMPILE_ANDROID_JNI_PROVIDER
#include "AndroidPerformanceLog.h"

void AndroidJNIProvider::ThermalStatusCallback(void *data, ThermalStatus status) {
    UE_LOG(LogAndroidPerformance, Log, TEXT("Thermal callback, thermal status %d!"), status);
    // @FIX do proper conversion
    static_cast<AndroidJNIProvider*>(data)->thermal_status_ = (ThermalStatus)status;
}

AndroidJNIProvider::AndroidJNIProvider() {
    is_available_ = Init();
}

bool AndroidJNIProvider::Init() {
    thermal_status_ = ThermalStatus::NONE;

    if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
        // Retrieve class information
        jclass context = env->FindClass("android/content/Context");

        // Get the value of a constant
        jfieldID fid =
                env->GetStaticFieldID(context, "POWER_SERVICE", "Ljava/lang/String;");
        jobject str_svc = env->GetStaticObjectField(context, fid);

        // Get the method 'getSystemService' and call it
        extern struct android_app* GNativeAndroidApp;
        jmethodID mid_getss = env->GetMethodID(
                context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jobject obj_power_service = env->CallObjectMethod(
                GNativeAndroidApp->activity->clazz, mid_getss, str_svc);

        // Add global reference to the power service object.
        obj_power_service_ = env->NewGlobalRef(obj_power_service);

        jclass cls_power_service = env->GetObjectClass(obj_power_service_);
        get_thermal_headroom_ =
                env->GetMethodID(cls_power_service, "getThermalHeadroom", "(I)F");

        if (get_thermal_headroom_ == 0) {
            // The API is not supported in the platform version.
            UE_LOG(LogAndroidPerformance, Error, TEXT("AndroidJNIProvider: GetThermalHeadroom function is null"));
            return false;
        }

        // Free references
        env->DeleteLocalRef(cls_power_service);
        env->DeleteLocalRef(obj_power_service);
        env->DeleteLocalRef(str_svc);
        env->DeleteLocalRef(context);

        // Remove exception
        if(env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    }

    thermal_manager_ = AThermal_acquireManager(); 

    if (!thermal_manager_) {
        UE_LOG(LogAndroidPerformance, Error, TEXT("AndroidJNIProvider: AThermal_acquireManager() failed"));
        return false;
    }

    if (!RegisterCallback(ThermalStatusCallback)) {
        UE_LOG(LogAndroidPerformance, Error, TEXT("AndroidJNIProvider: Failed to register thermal status callback"));
        return false;
    }

    return true;
}

ThermalStatus AndroidJNIProvider::GetThermalStatus() const {
    return thermal_status_;
}

float AndroidJNIProvider::GetThermalHeadroom(int forecast_seconds) const {
    if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
        return env->CallFloatMethod(obj_power_service_, get_thermal_headroom_,
                                                        forecast_seconds);
    }
    return 0.0f;
}

bool AndroidJNIProvider::RegisterCallback(ThermalCallback callback) {
    auto ret = AThermal_registerThermalStatusListener(
            thermal_manager_, reinterpret_cast<void (*)(void *, AThermalStatus)>(callback), this);

    if (!ret) {
        UE_LOG(LogAndroidPerformance, Log, TEXT("Thermal status callback registered"));
    } else {
        UE_LOG(LogAndroidPerformance, Error, TEXT("Failed to register thermal status callback with return value: %d"), ret);
    }

    return ret == 0;
}

void AndroidJNIProvider::UnregisterCallback(ThermalCallback callback) {
    auto ret = AThermal_unregisterThermalStatusListener(
            thermal_manager_, reinterpret_cast<void (*)(void *, AThermalStatus)>(callback), this);
    UE_LOG(LogAndroidPerformance, Log, TEXT("Thermal Status callback unregisterred:%d"), ret);
}

AndroidJNIProvider::~AndroidJNIProvider() {
    if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
        if (obj_power_service_ != nullptr) {
            env->DeleteGlobalRef(obj_power_service_);
        }
    }

    if (thermal_manager_) {
        UnregisterCallback(ThermalStatusCallback);
        AThermal_releaseManager(thermal_manager_);
    }
}

#endif // ADPF_COMPILE_ANDROID_JNI_PROVIDER
