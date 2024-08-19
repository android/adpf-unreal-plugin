#ifndef ADPF_SAMSUNG_PROVIDER_H_
#define ADPF_SAMSUNG_PROVIDER_H_

#ifdef ADPF_COMPILE_SAMSUNG_PROVIDER
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#include "Android/AndroidJava.h"

#include "AndroidPerformanceLog.h"

class IProvider;

// Wrapper for com/epicgames/ue4/SamsungGameSDK.java
class FJavaSamsungGameSDK : public FJavaClassObject {
public:
    FJavaSamsungGameSDK()
        : FJavaClassObject("com/samsung/android/SamsungGameSDK", "()V")
        , IsAvailableMethod(GetClassMethod("IsAvailable", "()Z")) // boolean IsAvailable();
        , GetVersionMethod(GetClassMethod("GetVersion", "()Ljava/lang/String;")) // string GetVersion();
        , GetTemperatureLevelMethod(GetClassMethod("GetTemperatureLevel", "()I")) // int GetTemperatureLevel();
        , GetTemperatureWarningLevelMethod(GetClassMethod("GetTemperatureWarningLevel", "()I")) // int GetTemperatureWarningLevel();
        , GetSkinTempLevelMethod(GetClassMethod("GetSkinTempLevel", "()I")) // int GetSkinTempLevel()
        , GetCpuJTLevelMethod(GetClassMethod("GetCpuJTLevel", "()I")) // int GetCpuJTLevel();
        , GetGpuJTLevelMethod(GetClassMethod("GetGpuJTLevel", "()I")) // int GetGpuJTLevel();
    {
        GetHighPrecisionTempMethod = nullptr;
        if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
            GetHighPrecisionTempMethod = env->GetMethodID(Class, "GetHighPrecisionTemp", "()D");
        }
    }

    bool IsAvailable() {
        return CallMethod<bool>(IsAvailableMethod);
    }

    FString GetVersion() {
        return CallMethod<FString>(GetVersionMethod);
    }

    int GetTemperatureLevel() {
        return CallMethod<int>(GetTemperatureLevelMethod);
    }

    bool SetFrequencyLevel (int cpuLevel, int gpuLevel) {
        return CallMethod<bool>(SetFrequencyLevelMethod, cpuLevel, gpuLevel);
    }

    double GetGPUFrameTime() {
        return CallMethod<double>(GetGpuFrameTimeMethod);
    }

    double GetHighPrecisionTemp() {
        if (GetHighPrecisionTempMethod == 0) {
            UE_LOG(LogAndroidPerformance, Log, TEXT("Calling GetHighPrecisionTemp, but no matching Java method exists."));
            return 0.0;
        }

        if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
            // Returns a value in [0, 10] range
            double ret = env->CallDoubleMethod(Object, GetHighPrecisionTempMethod);
            ret /= 10.0;  // Normalize by the max value
            VerifyException();
            // We expect a value in range of [0, 1] here
            // Everything else is considered to be an error
            // And gets turned into a safe default zero
            if (ret >= 1 || ret < 0) ret = 0;  
            return ret;
        }

        checkNoEntry();
        return 0.0;
    }

    int GetTemperatureWarningLevel() {
        return CallMethod<int>(GetTemperatureWarningLevelMethod);
    }

    int GetSkinTempLevel() {
        return CallMethod<int>(GetSkinTempLevelMethod);
    }

    int GetCpuJTLevel() {
        return CallMethod<int>(GetCpuJTLevelMethod);
    }

    int GetGpuJTLevel() {
        return CallMethod<int>(GetGpuJTLevelMethod);
    }

    int GetCpuLevelMax() {
        return CallMethod<int>(GetCpuLevelMaxMethod);
    }

    int GetGpuLevelMax() {
        return CallMethod<int>(GetGpuLevelMaxMethod);
    }

    virtual ~FJavaSamsungGameSDK() {}
private:
    // GameSDK v1.6
    FJavaClassMethod IsAvailableMethod; // boolean isAvailable();
    FJavaClassMethod GetVersionMethod; // int GetVersion();
    FJavaClassMethod GetTemperatureLevelMethod; // int GetTemperatureLevel();
    FJavaClassMethod SetFrequencyLevelMethod; // boolean SetFrequencyLevel(int cpuLevel, int gpuLevel);
    FJavaClassMethod GetGpuFrameTimeMethod; // double GetGPUFrameTime();
    FJavaClassMethod GetTemperatureWarningLevelMethod; // int GetTemperatureWarningLevel();
    FJavaClassMethod GetSkinTempLevelMethod; // int GetSkinTempLevel()
    FJavaClassMethod GetCpuJTLevelMethod; // int GetCpuJTLevel();
    FJavaClassMethod GetGpuJTLevelMethod; // int GetGpuJTLevel();

    // GameSDK v3.0
    FJavaClassMethod GetCpuLevelMaxMethod; // int GetCpuLevelMax();
    FJavaClassMethod GetGpuLevelMaxMethod; // int GetGpuLevelMax();

    jmethodID GetHighPrecisionTempMethod; // double GetHighPrecisionTempMethod();
};

class SamsungProvider : public IProvider {
public:
    SamsungProvider();

    virtual const char*   GetName() const override { return "SamsungProvider"; }

    virtual ThermalStatus GetThermalStatus() const override;
    virtual float         GetThermalHeadroom(int forecast_seconds) const override;

    virtual bool          RegisterCallback  (ThermalCallback callback) override;
    virtual void          UnregisterCallback(ThermalCallback callback) override;

    virtual               ~SamsungProvider() override;
private:
    bool Init();

    TUniquePtr<FJavaSamsungGameSDK> jni_gamesdk_;
};

#endif // ADPF_COMPILE_SAMSUNG_PROVIDER
#endif // ADPF_SAMSUNG_PROVIDER_H_
