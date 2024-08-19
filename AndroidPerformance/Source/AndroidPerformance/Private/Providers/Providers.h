#ifndef ADPF_PROVIDERS_H_
#define ADPF_PROVIDERS_H_

#if (__ANDROID_API__ >= 31)
#define ADPF_COMPILE_ANDROID_NATIVE_PROVIDER
#endif

#if (USE_ANDROID_JNI && __ANDROID_API__ >= 30)
#define ADPF_COMPILE_ANDROID_JNI_PROVIDER
#endif

#if (USE_ANDROID_JNI && __ANDROID_API__ >= 28)
#define ADPF_COMPILE_SAMSUNG_PROVIDER
#endif

enum class ThermalStatus {
    ERROR     = -1,
    NONE      = 0,
    LIGHT     = 1,
    MODERATE  = 2,
    SEVERE    = 3,
    CRITICAL  = 4,
    EMERGENCY = 5,
    SHUTDOWN  = 6,
};

typedef void ThermalCallback(void*, ThermalStatus); 

class IProvider {
public:
    IProvider() = default;

    bool IsAvailable() { return is_available_; }

    IProvider(const IProvider &) = delete;
    void operator=(const IProvider&) = delete;

    virtual const char*   GetName() const = 0;

    virtual ThermalStatus GetThermalStatus()   const = 0;
    virtual float         GetThermalHeadroom(int forecast_seconds) const = 0;

    virtual bool          RegisterCallback  (ThermalCallback callback) = 0;
    virtual void          UnregisterCallback(ThermalCallback callback) = 0;

    virtual ~IProvider() = default;

protected:
    bool             is_available_;
};

TUniquePtr<IProvider> CreateThermalProvider();

#endif // ADPF_PROVIDERS_H_
