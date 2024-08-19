#ifndef ADPF_NATIVE_PROVIDER_H_
#define ADPF_NATIVE_PROVIDER_H_

#ifdef ADPF_COMPILE_ANDROID_NATIVE_PROVIDER
#include <android/thermal.h>

class IProvider;

class AndroidNativeProvider : public IProvider {
public:
    AndroidNativeProvider();

    virtual const char*   GetName() const override { return "AndroidNativeProvider"; }

    virtual ThermalStatus GetThermalStatus() const override;
    virtual float         GetThermalHeadroom(int forecast_seconds) const override;

    virtual bool          RegisterCallback  (ThermalCallback callback) override;
    virtual void          UnregisterCallback(ThermalCallback callback) override;

    virtual ~AndroidNativeProvider() override;
private:
    bool Init();

    static void ThermalStatusCallback(void *data, ThermalStatus status);

    AThermalManager* thermal_manager_;
    ThermalStatus    thermal_status_;
};

#endif // ADPF_COMPILE_ANDROID_NATIVE_PROVIDER
#endif // ADPF_NATIVE_PROVIDER_H_
