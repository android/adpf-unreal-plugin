/*
 * Copyright 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ADPFManager.h"
#include "AndroidPerformanceLog.h"
#include "RenderCore.h"

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#endif

#include <time.h>
#include <algorithm>

static TAutoConsoleVariable<int32> CVarAndroidPerformanceEnabled(
    TEXT("r.AndroidPerformanceEnabled"),
    1,
    TEXT("Enable/disable the Android Performance plugin in the Monitor() method.\n")
    TEXT("The plugin uses the Android adaptability API to adjust the game settings based on the thermal status of the device and will adjust the CPU as needed.\n")
    TEXT(" 0: off (disabled)\n")
    TEXT(" 1: on (enabled)"),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarAndroidPerformanceHintEnabled(
    TEXT("r.AndroidPerformanceHintEnabled"),
    1,
    TEXT("Enable/disable the performance hint manager in the Monitor() method.\n")
    TEXT("Enable this setting for optimal thread boosting on supported Android devices.\n")
    TEXT(" 0: off (disabled)\n")
    TEXT(" 1: on (enabled)"),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarAndroidPerformanceChangeQualites(
    TEXT("r.AndroidPerformanceChangeQualities"),
    1,
    TEXT("Choose how the thermal status adjusts the game's fidelity level.\n")
    TEXT(" 0: The system does not adjust any settings\n")
    TEXT(" 1: Settings are adjusted according to the thermal headroom\n")
    TEXT(" 2: Settings are adjusted according to the thermal listener"),
    ECVF_RenderThreadSafe);

// Native callback for thermal status change listener.
// The function is called from Activity implementation in Java.
void nativeThermalStatusChanged(JNIEnv *env, jclass cls, jint thermalState) {
    UE_LOG(LogAndroidPerformance, Log, TEXT("Thermal Status updated to:%d"), thermalState);
    ADPFManager::getInstance().SetThermalStatus(thermalState);
}

// Native API to register/unregiser thethermal status change listener.
// The function is called from Activity implementation in Java.
void thermal_callback(void *data, AThermalStatus status) {
    ADPFManager::getInstance().SetThermalStatus(status);
}

float Clock() {
    static struct timespec _base;
    static bool first_call = true;

    if (first_call) {
        clock_gettime(CLOCK_MONOTONIC, &_base);
        first_call = false;
    }

    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    float sec_diff = (float)(t.tv_sec - _base.tv_sec);
    float msec_diff = (float)((t.tv_nsec - _base.tv_nsec) / 1000000);
    return sec_diff + 0.001f * msec_diff;
}

ADPFManager::ADPFManager()
        : thermal_manager_(nullptr),
            initialized_performance_hint_manager(false),
            support_performance_hint_manager(true),
            thermal_status_(0),
            thermal_headroom_(0.f),
            obj_power_service_(nullptr),
            get_thermal_headroom_(0),
            obj_perfhint_service_(nullptr),
            obj_perfhint_game_session_(nullptr),
            obj_perfhint_render_session_(nullptr),
            obj_perfhint_rhi_session_(nullptr),
            report_actual_game_work_duration_(0),
            report_actual_render_work_duration_(0),
            report_actual_rhi_work_duration_(0),
            update_target_game_work_duration_(0),
            update_target_render_work_duration_(0),
            update_target_rhi_work_duration_(0),
            preferred_update_rate_(0),
            current_quality_level(max_quality_count - 1),
            target_quality_level(max_quality_count - 1),
            prev_max_fps(-1.0f),
            prev_max_fps_nano(0),
            fps_total(0.0f),
            fps_count(0){
    last_clock_ = Clock();

    // Load current quality level, and set this quality level is maximum.
    Scalability::FQualityLevels current_level = Scalability::GetQualityLevels();
    for(int32_t i = 0; i < max_quality_count; ++i) {
        quality_levels[i].SetFromSingleQualityLevel(i);

        if(quality_levels[i].ResolutionQuality > current_level.ResolutionQuality) {
            quality_levels[i].ResolutionQuality = current_level.ResolutionQuality;
        }
        if(quality_levels[i].ViewDistanceQuality > current_level.ViewDistanceQuality) {
            quality_levels[i].ViewDistanceQuality = current_level.ViewDistanceQuality;
        }
        if(quality_levels[i].AntiAliasingQuality > current_level.AntiAliasingQuality) {
            quality_levels[i].AntiAliasingQuality = current_level.AntiAliasingQuality;
        }
        if(quality_levels[i].ShadowQuality > current_level.ShadowQuality) {
            quality_levels[i].ShadowQuality = current_level.ShadowQuality;
        }
        if(quality_levels[i].PostProcessQuality > current_level.PostProcessQuality) {
            quality_levels[i].PostProcessQuality = current_level.PostProcessQuality;
        }
        if(quality_levels[i].TextureQuality > current_level.TextureQuality) {
            quality_levels[i].TextureQuality = current_level.TextureQuality;
        }
        if(quality_levels[i].EffectsQuality > current_level.EffectsQuality) {
            quality_levels[i].EffectsQuality = current_level.EffectsQuality;
        }
        if(quality_levels[i].FoliageQuality > current_level.FoliageQuality) {
            quality_levels[i].FoliageQuality = current_level.FoliageQuality;
        }
        if(quality_levels[i].ShadingQuality > current_level.ShadingQuality) {
            quality_levels[i].ShadingQuality = current_level.ShadingQuality;
        }
    }
}

ADPFManager::~ADPFManager() {
#if PLATFORM_ANDROID
    // Remove global reference.
    if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
        DestroyPerformanceHintManager();
        if (obj_power_service_ != nullptr) {
            env->DeleteGlobalRef(obj_power_service_);
        }
    }
#endif
}

bool ADPFManager::registerListener() {
#if PLATFORM_ANDROID
    // Initialize PowerManager reference.
    if(InitializePowerManager()) {
        // The device might not support thermal APIs, it will not initialized.
        return true;
    }
#endif

    // The device might not support thermal APIs, it will not initialized.
    return false;
}

bool ADPFManager::unregisterListener() {
    return true;
}

// Invoke the method periodically (once a frame) to monitor
// the device's thermal throttling status.
void ADPFManager::Monitor() {
#if PLATFORM_ANDROID
    if (CVarAndroidPerformanceEnabled.GetValueOnAnyThread() == 0) {
        // check performance hint session is created, and delete it.
        if(initialized_performance_hint_manager) {
            initialized_performance_hint_manager = false;
            DestroyPerformanceHintManager();
            UE_LOG(LogAndroidPerformance, Log, TEXT("Performance Hint Manager Destroyed because of CVar disabled."));
        }

        return;
    }

    // for debug
    extern ENGINE_API float GAverageFPS;
    fps_total += GAverageFPS;
    fps_count++;

    // Change graphic quality by theraml.
    float current_clock = Clock();
    if (current_clock - last_clock_ >= kThermalHeadroomUpdateThreshold) {
        // Update thermal headroom.
        UpdateThermalStatusHeadRoom();
        last_clock_ = current_clock;

        // for debug
        UE_LOG(LogAndroidPerformance, Log, TEXT("Headroom %.3f %d FPS %.2f temp %.2f"), thermal_headroom_, thermal_status_,
                fps_total / (float)fps_count, FAndroidMisc::GetDeviceTemperatureLevel());
        fps_total = 0.0f;
        fps_count = 0;

        const int32 quality_mode = CVarAndroidPerformanceChangeQualites.GetValueOnAnyThread();
        if (quality_mode != 0) {
            if (quality_mode == 1) {
                saveQualityLevel(thermal_headroom_);
            }

            // TODO Change the quality and FPS settings to match the game's status.
            uint32_t new_target = target_quality_level;
            if(current_quality_level != new_target) {
                if(new_target > max_quality_count) {
                    new_target = max_quality_count - 1;
                }
                current_quality_level = new_target;

                // Change Unreal scalability quality.
                // https://docs.unrealengine.com/4.27/en-US/TestingAndOptimization/PerformanceAndProfiling/Scalability/ScalabilityReference/
                UE_LOG(LogAndroidPerformance, Log, TEXT("Change quality level to %d"), new_target);
                SetQualityLevels(quality_levels[new_target], true);
            }
        }
    }

    // Hint manager logic based on current FPS and actual thread time.
    if ((CVarAndroidPerformanceHintEnabled.GetValueOnAnyThread()) != 0 && support_performance_hint_manager) {
        // Initialization, attempt only once if not already initialized.
        if (initialized_performance_hint_manager == false) {
            if(InitializePerformanceHintManager()) {
                UE_LOG(LogAndroidPerformance, Log, TEXT("Performance Hint Manager Initialized."));
                initialized_performance_hint_manager = true;

                // Force target duration calculation on the first update after initialization
                prev_max_fps = -1.0f;
            }
            else {
                // Initialization failed, disable support permanently for this run.
                support_performance_hint_manager = false;
            }
        }

        // Update Logic, proceed only if successfully initialized.
        if(initialized_performance_hint_manager) {
            // Check max fps is changed, and caluate nanosec duration
            const float current_max_fps = GEngine->GetMaxFPS();
            bool update_target_duration = false;
            long long target_duration_nano = prev_max_fps_nano; // Default to previous value
            if(prev_max_fps != current_max_fps) {
                prev_max_fps = current_max_fps;
                update_target_duration = true;

                // Use default if max FPS is 0 (unlimited) or negative, otherwise calculate based on FPS.
                target_duration_nano = (prev_max_fps <= 0.0f) ? 16666666 : fpsToNanosec(prev_max_fps);
                prev_max_fps_nano = target_duration_nano; // Store the newly calculated value for next frame
                UE_LOG(LogAndroidPerformance, Verbose, TEXT("Max FPS changed to %.2f, Target Duration set to %lld ns"), prev_max_fps, prev_max_fps_nano);
            }

            // Update the performance hint sessions with the actual thread time and target duration.
            if(GGameThreadTime > 0) {
                UpdatePerfHintSession(static_cast<jlong>(GGameThreadTime * 1000), target_duration_nano, update_target_duration,
                        obj_perfhint_game_session_, report_actual_game_work_duration_, update_target_game_work_duration_);
            }
            else {
                prev_max_fps = -1.0f;
            }
            UpdatePerfHintSession(static_cast<jlong>(GRenderThreadTime * 1000), target_duration_nano, update_target_duration,
                    obj_perfhint_render_session_, report_actual_render_work_duration_, update_target_render_work_duration_);
            UpdatePerfHintSession(static_cast<jlong>(GRHIThreadTime * 1000), target_duration_nano, update_target_duration,
                    obj_perfhint_rhi_session_, report_actual_rhi_work_duration_, update_target_rhi_work_duration_);
        }
    }
    else {
        // Cleanup Logic, ff feature was initialized but is now disabled/unsupported
        if(initialized_performance_hint_manager) {
            initialized_performance_hint_manager = false;
            prev_max_fps = -1.0f;
            DestroyPerformanceHintManager();
            UE_LOG(LogAndroidPerformance, Log, TEXT("Performance Hint Manager Destroyed because of CVar disabled."));
        }
    }
#endif
}

void ADPFManager::SetThermalStatus(int32_t i){
    thermal_status_ = i;
}

// Initialize JNI calls for the powermanager.
bool ADPFManager::InitializePowerManager() {
#if PLATFORM_ANDROID
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
#endif

    if (get_thermal_headroom_ == 0) {
        // The API is not supported in the platform version.
        return false;
    }

    if(FMath::IsNaN(UpdateThermalStatusHeadRoom())) {
        // If thermal headroom is NaN, this device is not support thermal API.
        return false;
    }

    return true;
}

// Retrieve current thermal headroom using JNI call.
float ADPFManager::UpdateThermalStatusHeadRoom() {
    if (get_thermal_headroom_ == 0) {
        return 0.f;
    }

#if PLATFORM_ANDROID
    // Get thermal headroom!
    if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
        thermal_headroom_ =
                env->CallFloatMethod(obj_power_service_, get_thermal_headroom_,
                                                        kThermalHeadroomForecastSeconds);
    }
#endif
    return thermal_headroom_;
}

// Initialize JNI calls for the PowerHintManager.
bool ADPFManager::InitializePerformanceHintManager() {
#if PLATFORM_ANDROID
    if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
        // Retrieve class information
        jclass context = env->FindClass("android/content/Context");

        // Get the value of a constant
        jfieldID fid = env->GetStaticFieldID(context, "PERFORMANCE_HINT_SERVICE",
                                             "Ljava/lang/String;");
        if(!fid) {
            // Remove exception
            if(env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
            UE_LOG(LogAndroidPerformance, Log, TEXT("Performance Hint Manager is not supported."));
            return false;
        }
        jobject str_svc = env->GetStaticObjectField(context, fid);

        // Get the method 'getSystemService' and call it
        extern struct android_app* GNativeAndroidApp;
        jmethodID mid_getss = env->GetMethodID(
                context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jobject obj_perfhint_service = env->CallObjectMethod(
                GNativeAndroidApp->activity->clazz, mid_getss, str_svc);

        // Add global reference to the power service object.
        obj_perfhint_service_ = env->NewGlobalRef(obj_perfhint_service);

        // Retrieve methods IDs for the APIs.
        jclass cls_perfhint_service = env->GetObjectClass(obj_perfhint_service_);
        jmethodID mid_createhintsession =
                env->GetMethodID(cls_perfhint_service, "createHintSession",
                                 "([IJ)Landroid/os/PerformanceHintManager$Session;");
        jmethodID mid_preferedupdaterate = env->GetMethodID(
                cls_perfhint_service, "getPreferredUpdateRateNanos", "()J");
        
        const jlong DEFAULT_TARGET_NS = 16666666;

        // Function to create and initialize a hint session
        auto CreateHintSession = [&](int32_t threadId, jobject& sessionGlobalRef, jmethodID& reportMethod, jmethodID& updateMethod) {
            jintArray array = env->NewIntArray(1);
            env->SetIntArrayRegion(array, 0, 1, &threadId); 

            jobject obj_hintsession = env->CallObjectMethod(obj_perfhint_service_, mid_createhintsession, array, DEFAULT_TARGET_NS);
            if (obj_hintsession) {
                sessionGlobalRef = env->NewGlobalRef(obj_hintsession);
                preferred_update_rate_ = env->CallLongMethod(obj_perfhint_service_, mid_preferedupdaterate);

                jclass cls_perfhint_session = env->GetObjectClass(obj_hintsession);
                reportMethod = env->GetMethodID(cls_perfhint_session, "reportActualWorkDuration", "(J)V");
                updateMethod = env->GetMethodID(cls_perfhint_session, "updateTargetWorkDuration", "(J)V");
            } else {
                UE_LOG(LogAndroidPerformance, Log, TEXT("Failed to create a perf hint session."));
            }

            env->DeleteLocalRef(obj_hintsession);
            env->DeleteLocalRef(array);
        };

PRAGMA_DISABLE_DEPRECATION_WARNINGS
        CreateHintSession(GGameThreadId, obj_perfhint_game_session_,
                report_actual_game_work_duration_, update_target_game_work_duration_);
        CreateHintSession(GRenderThreadId, obj_perfhint_render_session_,
                report_actual_render_work_duration_, update_target_render_work_duration_);
        CreateHintSession(GRHIThreadId, obj_perfhint_rhi_session_,
                report_actual_rhi_work_duration_, update_target_rhi_work_duration_);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

        // Free local references
        env->DeleteLocalRef(cls_perfhint_service);
        env->DeleteLocalRef(obj_perfhint_service);
        env->DeleteLocalRef(str_svc);
        env->DeleteLocalRef(context);

        // Remove exception
        if(env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    }
#endif

    if (report_actual_game_work_duration_ == 0 || update_target_game_work_duration_ == 0 || 
        report_actual_render_work_duration_ == 0 || update_target_render_work_duration_ == 0 ||
        report_actual_rhi_work_duration_ == 0 || update_target_rhi_work_duration_ == 0) {
        // The API is not supported well.
        DestroyPerformanceHintManager();
        UE_LOG(LogAndroidPerformance, Log, TEXT("Performance Hint Manager Initialization Failed."));
        return false;
    }
    return true;
}

void ADPFManager::DestroyPerformanceHintManager() {
#if PLATFORM_ANDROID
    prev_max_fps = -1.0f;
    if (obj_perfhint_service_) {
        if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
            if (obj_perfhint_game_session_) {
                env->DeleteGlobalRef(obj_perfhint_game_session_);
                obj_perfhint_game_session_ = nullptr;
            }
            if (obj_perfhint_render_session_) {
                env->DeleteGlobalRef(obj_perfhint_render_session_);
                obj_perfhint_render_session_ = nullptr;
            }
            if (obj_perfhint_rhi_session_) {
                env->DeleteGlobalRef(obj_perfhint_rhi_session_);
                obj_perfhint_rhi_session_ = nullptr;
            }
            env->DeleteGlobalRef(obj_perfhint_service_);
            obj_perfhint_service_ = nullptr;
        }
    }
#endif
}

// Indicates the start and end of the performance intensive task.
// The methods call performance hint API to tell the performance
// hint to the system.
void ADPFManager::UpdatePerfHintSession(jlong duration_ns, jlong target_duration_ns, bool update_target_duration,
        jobject obj_perfhint_session_, jmethodID report_actual_work_duration, jmethodID update_target_work_duration) {
#if PLATFORM_ANDROID
    if (obj_perfhint_session_) {
        // Report and update the target work duration using JNI calls.
        if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
            env->CallVoidMethod(obj_perfhint_session_, report_actual_work_duration,
                                duration_ns);
            if(update_target_duration) {
                env->CallVoidMethod(obj_perfhint_session_, update_target_work_duration,
                                    target_duration_ns);
            }
        }
    }
#endif
}

jlong ADPFManager::fpsToNanosec(const float maxFPS) {
    return static_cast<jlong>(1000000000.0f / maxFPS);
}

void ADPFManager::saveQualityLevel(const int32_t warning_level) {
    if(warning_level >= 0 && warning_level < max_quality_count) {
        target_quality_level = warning_level;
    }
}

void ADPFManager::saveQualityLevel(const float head_room) {
    if(head_room < 0.75f) {
        // 0.0 < x < 0.75
        target_quality_level = 3;
    }
    else if(head_room < 0.85f) {
        // 0.75 < x < 0.85
        target_quality_level = 2;
    }
    else if(head_room < 0.95f) {
        // 0.85 < x < 0.95
        target_quality_level = 1;
    }
    else {
        // 0.95 < x
        target_quality_level = 0;
    }
}
