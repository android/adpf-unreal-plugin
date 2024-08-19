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
        :   initialized_performance_hint_manager(false),
            thermal_headroom_(0.f),
            obj_perfhint_service_(nullptr),
            obj_perfhint_game_session_(nullptr),
            obj_perfhint_render_session_(nullptr),
            report_actual_game_work_duration_(0),
            report_actual_render_work_duration_(0),
            update_target_game_work_duration_(0),
            update_target_render_work_duration_(0),
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
        if (obj_perfhint_service_ != nullptr) {
            env->DeleteGlobalRef(obj_perfhint_service_);
        }
        if (obj_perfhint_game_session_ != nullptr) {
            env->DeleteGlobalRef(obj_perfhint_game_session_);
        }
        if (obj_perfhint_render_session_ != nullptr) {
            env->DeleteGlobalRef(obj_perfhint_render_session_);
        }
    }
#endif
}

bool ADPFManager::Init() {
    provider_ = CreateThermalProvider();
    UE_LOG(LogAndroidPerformance, Log, TEXT("Created Thermal Provider - %s"), *FString(provider_->GetName()));
    return provider_ != nullptr;
}

bool ADPFManager::Deinit() {
    // Release all utilized Thermal APIs and clean resources
    provider_.Reset();

    return true;
}

// Invoke the method periodically (once a frame) to monitor
// the device's thermal throttling status.
void ADPFManager::Monitor() {
#if PLATFORM_ANDROID
    if (CVarAndroidPerformanceEnabled.GetValueOnAnyThread() == 0) {
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
        UE_LOG(LogAndroidPerformance, Log, TEXT("Headroom %.3f %d FPS %.2f temp %.2f"), thermal_headroom_, provider_->GetThermalStatus(),
                fps_total / (float)fps_count, FAndroidMisc::GetDeviceTemperatureLevel());
        fps_total = 0.0f;
        fps_count = 0;

        const int32 quality_mode = CVarAndroidPerformanceChangeQualites.GetValueOnAnyThread();
        if (quality_mode != 0) {
            if (quality_mode == 1) {
                saveQualityLevel(thermal_headroom_);
            }
            else {
                saveQualityLevel(max_quality_count - (int32_t)provider_->GetThermalStatus() - 1);
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
    if (CVarAndroidPerformanceHintEnabled.GetValueOnAnyThread() != 0) {
        // Initialize PowerHintManager reference on here, when
        // StartupModule is called render thread id is changed.
        if (initialized_performance_hint_manager == false) {
            initialized_performance_hint_manager = true;
            InitializePerformanceHintManager();
        }

        bool update_target_duration = false;
        // Check max fps is changed, and caluate nanosec duration
        if(prev_max_fps != GEngine->GetMaxFPS()) {
            prev_max_fps = GEngine->GetMaxFPS();
            update_target_duration = true;
            prev_max_fps_nano = prev_max_fps == 0.0f ? 16666666 : fpsToNanosec(prev_max_fps);
        }

        // Update hint session.
        if(GGameThreadTime > 0) {
            UpdatePerfHintGameSession(static_cast<jlong>(GGameThreadTime * 1000), prev_max_fps_nano, update_target_duration);
        } else {
            prev_max_fps = -1.0f;
        }
        UpdatePerfHintRenderSession(findLongestNanosec(GRenderThreadTime, GRHIThreadTime), prev_max_fps_nano, update_target_duration);
    }
#endif
}

// Retrieve current thermal headroom using JNI call.
float ADPFManager::UpdateThermalStatusHeadRoom() {
    thermal_headroom_ = provider_->GetThermalHeadroom(kThermalHeadroomUpdateThreshold);

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

        {
            // Create int array which contain game thread ID.
            jintArray array = env->NewIntArray(1);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
            int32_t tids[1] = { static_cast<int32_t>(GGameThreadId)};
PRAGMA_ENABLE_DEPRECATION_WARNINGS
            env->SetIntArrayRegion(array, 0, 1, tids);
            const jlong DEFAULT_TARGET_NS = 16666666;

            // Create Hint session for game thread.
            jobject obj_hintsession = env->CallObjectMethod(
                    obj_perfhint_service_, mid_createhintsession, array, DEFAULT_TARGET_NS);
            if (obj_hintsession == nullptr) {
                UE_LOG(LogAndroidPerformance, Log, TEXT("Failed to create a perf hint session."));
            } else {
                obj_perfhint_game_session_ = env->NewGlobalRef(obj_hintsession);
                preferred_update_rate_ =
                        env->CallLongMethod(obj_perfhint_service_, mid_preferedupdaterate);

                // Retrieve mid of Session APIs.
                jclass cls_perfhint_session = env->GetObjectClass(obj_perfhint_game_session_);
                report_actual_game_work_duration_ = env->GetMethodID(
                        cls_perfhint_session, "reportActualWorkDuration", "(J)V");
                update_target_game_work_duration_ = env->GetMethodID(
                        cls_perfhint_session, "updateTargetWorkDuration", "(J)V");
            }

            // Free local references
            env->DeleteLocalRef(obj_hintsession);
            env->DeleteLocalRef(array);
        }

        {
            // Create int array which contain render and RHI thread IDs.
            jintArray array = env->NewIntArray(2);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
            int32_t tids[2] = { static_cast<int32_t>(GRenderThreadId), static_cast<int32_t>(GRHIThreadId) };
PRAGMA_ENABLE_DEPRECATION_WARNINGS
            env->SetIntArrayRegion(array, 0, 2, tids);
            const jlong DEFAULT_TARGET_NS = 16666666;

            // Create Hint session for render threads.
            jobject obj_hintsession = env->CallObjectMethod(
                    obj_perfhint_service_, mid_createhintsession, array, DEFAULT_TARGET_NS);
            if (obj_hintsession == nullptr) {
                UE_LOG(LogAndroidPerformance, Log, TEXT("Failed to create a perf hint session."));
            } else {
                obj_perfhint_render_session_ = env->NewGlobalRef(obj_hintsession);
                preferred_update_rate_ =
                        env->CallLongMethod(obj_perfhint_service_, mid_preferedupdaterate);

                // Retrieve mid of Session APIs.
                jclass cls_perfhint_session = env->GetObjectClass(obj_perfhint_render_session_);
                report_actual_render_work_duration_ = env->GetMethodID(
                        cls_perfhint_session, "reportActualWorkDuration", "(J)V");
                update_target_render_work_duration_ = env->GetMethodID(
                        cls_perfhint_session, "updateTargetWorkDuration", "(J)V");
            }

            // Free local references
            env->DeleteLocalRef(obj_hintsession);
            env->DeleteLocalRef(array);
        }

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

    if (report_actual_game_work_duration_ == 0 || update_target_game_work_duration_ == 0) {
        // The API is not supported in the platform version.
        return false;
    }if (report_actual_render_work_duration_ == 0 || update_target_render_work_duration_ == 0) {
        // The API is not supported in the platform version.
        return false;
    }
    return true;
}

// Indicates the start and end of the performance intensive task.
// The methods call performance hint API to tell the performance
// hint to the system.
void ADPFManager::UpdatePerfHintGameSession(jlong duration_ns, jlong target_duration_ns, bool update_target_duration) {
    if (obj_perfhint_game_session_) {
        if(duration_ns > target_duration_ns) {
            UE_LOG(LogAndroidPerformance, Log, TEXT("Game threads will be boosted, duration_ns %lld, target_duration_ns %lld"),
                    duration_ns, target_duration_ns);
        }

#if PLATFORM_ANDROID
        // Report and update the target work duration using JNI calls.
        if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
            env->CallVoidMethod(obj_perfhint_game_session_, report_actual_game_work_duration_,
                                duration_ns);
            if (update_target_duration) {
                env->CallVoidMethod(obj_perfhint_game_session_, update_target_game_work_duration_,
                                    target_duration_ns);
            }
        }
#endif
    }
}

void ADPFManager::UpdatePerfHintRenderSession(jlong duration_ns, jlong target_duration_ns, bool update_target_duration) {
    if (obj_perfhint_render_session_) {
        if(duration_ns > target_duration_ns) {
            UE_LOG(LogAndroidPerformance, Log, TEXT("Render threads will be boosted, duration_ns %lld, target_duration_ns %lld"),
                    duration_ns, target_duration_ns);
        }

#if PLATFORM_ANDROID
        // Report and update the target work duration using JNI calls.
        if (JNIEnv* env = FAndroidApplication::GetJavaEnv()) {
            env->CallVoidMethod(obj_perfhint_render_session_, report_actual_render_work_duration_,
                                duration_ns);
            if (update_target_duration) {
                env->CallVoidMethod(obj_perfhint_render_session_, update_target_render_work_duration_,
                                    target_duration_ns);
            }
        }
#endif
    }
}

jlong ADPFManager::fpsToNanosec(const float maxFPS) {
    return static_cast<jlong>(1000000000.0f / maxFPS);
}

jlong ADPFManager::findLongestNanosec(const uint32_t a, const uint32_t b) {
    return static_cast<jlong>(std::max(a, b) * 1000);
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
