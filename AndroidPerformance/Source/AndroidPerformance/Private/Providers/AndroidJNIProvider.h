#ifndef ADPF_JNI_PROVIDER_H_
#define ADPF_JNI_PROVIDER_H_

#ifdef ADPF_COMPILE_ANDROID_JNI_PROVIDER
#include <jni.h>
#include <android_native_app_glue.h>
#include <android/thermal.h>

#include "Android/AndroidApplication.h"

class IProvider;

class AndroidJNIProvider : public IProvider {
public:
    AndroidJNIProvider();

    virtual const char*   GetName() const override { return "AndroidJNIProvider"; }

    virtual ThermalStatus GetThermalStatus() const override;
    virtual float         GetThermalHeadroom(int forecast_seconds) const override;

    virtual bool          RegisterCallback  (ThermalCallback callback) override;
    virtual void          UnregisterCallback(ThermalCallback callback) override;

    virtual ~AndroidJNIProvider() override;
private:
    bool Init();

    static void ThermalStatusCallback(void *data, ThermalStatus status);

    jobject obj_power_service_;
    jmethodID get_thermal_headroom_;

    AThermalManager *thermal_manager_;
    ThermalStatus thermal_status_;
};

#endif // ADPF_COMPILE_ANDROID_JNI_PROVIDER
#endif // ADPF_JNI_PROVIDER_H_
