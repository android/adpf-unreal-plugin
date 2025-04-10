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
    TEXT(" 1: Settings are adjusted according to the thermal headroom\n"),
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
        : thermal_manager_(nullptr),
            initialized_performance_hint_manager(false),
            support_performance_hint_manager(true),
            thermal_headroom_(0.f),
            obj_perfhint_game_session_(nullptr),
            obj_perfhint_render_session_(nullptr),
            obj_perfhint_rhi_session_(nullptr),
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
    destroy();
}

bool ADPFManager::initialize() {
#if __ANDROID_API__ < 31
    #error "Android API level is less than 31, the thermal API is not supported. Recommend to increase 'NDK API Level' to 'android-33' or 'use-jni' branch in github."
#endif
#if __ANDROID_API__ < 33
    #error "Android API level is less than 33, the performance hint APIs are not supported. Recommend to increase 'NDK API Level' to 'android-33' or 'use-jni' branch in github."
#endif

#if defined(PLATFORM_ANDROID) && __ANDROID_API__ >= 31
    // Initialize PowerManager reference.
    if(android_get_device_api_level() < 31) {
        // The device might not support thermal APIs, it will not initialized.
        UE_LOG(LogAndroidPerformance, Log, TEXT("Device API level is less than 31, the ADPF plugin will not work in this device."));
        return false;
    }
    else if(InitializePowerManager() == false) {
        // The device might not support thermal APIs, it will not initialized.
        UE_LOG(LogAndroidPerformance, Log, TEXT("Initialize PowerManager failed, the ADPF plugin will not work in this device."));
        return false;
    }

    // Initialize PerformanceHintManager reference.
    auto manager = GetThermalManager();
    if (manager != nullptr) {
        UE_LOG(LogAndroidPerformance, Log, TEXT("ADPFManager is initialized, the ADPF plugin will work in this device."));
        return true;
    }
#endif

    // The device might not support thermal APIs, it will not initialized.
    UE_LOG(LogAndroidPerformance, Log, TEXT("ADPFManager is not initialized, the ADPF plugin will not work in this device."));
    return false;
}

void ADPFManager::destroy() {
#if defined(PLATFORM_ANDROID) && __ANDROID_API__ >= 33
    // Destroy the performance hint sessions and release the thermal manager.
    UE_LOG(LogAndroidPerformance, Log, TEXT("Destroying ADPFManager."));
    DestroyPerformanceHintManager();
    if (thermal_manager_ != nullptr) {
        AThermal_releaseManager(thermal_manager_);
    }
#endif
}

// Invoke the method periodically (once a frame) to monitor
// the device's thermal throttling status.
void ADPFManager::Monitor() {
#if defined(PLATFORM_ANDROID) && __ANDROID_API__ >= 31
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
        UE_LOG(LogAndroidPerformance, Log, TEXT("Headroom %.3f FPS %.2f temp %.2f"), thermal_headroom_,
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
                        obj_perfhint_game_session_);
            }
            else {
                prev_max_fps = -1.0f;
            }
            UpdatePerfHintSession(static_cast<jlong>(GRenderThreadTime * 1000), target_duration_nano, update_target_duration,
                    obj_perfhint_render_session_);
            UpdatePerfHintSession(static_cast<jlong>(GRHIThreadTime * 1000), target_duration_nano, update_target_duration,
                    obj_perfhint_rhi_session_);
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

// Initialize JNI calls for the powermanager.
bool ADPFManager::InitializePowerManager() {
#if defined(PLATFORM_ANDROID) && __ANDROID_API__ >= 31
    if (android_get_device_api_level() >= 31) {
        // Use NDK API to retrieve thermal headroom.
        thermal_manager_ = AThermal_acquireManager();
    } else {
        return false;
    }
#endif

    if(FMath::IsNaN(UpdateThermalStatusHeadRoom())) {
        // If thermal headroom is NaN, this device is not support thermal API.
        return false;
    }

    return true;
}

// Retrieve current thermal headroom using JNI call.
float ADPFManager::UpdateThermalStatusHeadRoom() {
#if defined(PLATFORM_ANDROID) && __ANDROID_API__ >= 31
    // Get the current thermal headroom.
    if(thermal_manager_) {
        thermal_headroom_ = AThermal_getThermalHeadroom(
                thermal_manager_, kThermalHeadroomForecastSeconds);
        return thermal_headroom_;
    }
#endif
    return thermal_headroom_;
}

// Initialize JNI calls for the PowerHintManager.
bool ADPFManager::InitializePerformanceHintManager() {
#if defined(PLATFORM_ANDROID) && __ANDROID_API__ >= 33
    if (android_get_device_api_level() < 31) {
        // The device might not support performance hint APIs, it will not initialized.
        UE_LOG(LogAndroidPerformance, Log, TEXT("The device API level is less than 31, the ADPF performance hint APIs will not work in this device."));
        return false;
    }

    APerformanceHintManager* obj_perfhint_manager = APerformanceHint_getManager();
    if(obj_perfhint_manager == nullptr) {
        // The device might not support performance hint APIs, it will not initialized.
        UE_LOG(LogAndroidPerformance, Log, TEXT("Get performance hint manager failed, the ADPF performance hint APIs will not work in this device."));
        return false;
    }

    // Create performance hint sessions for game, render, and RHI threads.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    const int32_t kGameThreadId = GGameThreadId;
    const int32_t kRenderThreadId = GRenderThreadId;
    const int32_t kRHIThreadId = GRHIThreadId;
    obj_perfhint_game_session_ = APerformanceHint_createSession(obj_perfhint_manager,
            &kGameThreadId, 1, 166666666);
    obj_perfhint_render_session_ = APerformanceHint_createSession(obj_perfhint_manager,
            &kRenderThreadId, 1, 166666666);
    obj_perfhint_rhi_session_ = APerformanceHint_createSession(obj_perfhint_manager,
            &kRHIThreadId, 1, 166666666);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif

    // Check if the performance hint sessions are created successfully.
    // If not, the device might not support performance hint APIs.
    if(obj_perfhint_game_session_ == nullptr || obj_perfhint_render_session_ == nullptr ||
        obj_perfhint_rhi_session_ == nullptr) {
        // The API is not supported well.
        UE_LOG(LogAndroidPerformance, Log, TEXT("Creating performance hint session failed, the ADPF performance hint APIs will not work in this device."));
        DestroyPerformanceHintManager();
        return false;
    }
    return true;
}

void ADPFManager::DestroyPerformanceHintManager() {
#if defined(PLATFORM_ANDROID) && __ANDROID_API__ >= 33
    prev_max_fps = -1.0f;

    UE_LOG(LogAndroidPerformance, Log, TEXT("Destroying performance hint sessions."));
    if(obj_perfhint_game_session_) {
        APerformanceHint_closeSession(obj_perfhint_game_session_);
        obj_perfhint_game_session_ = nullptr;
    }
    if(obj_perfhint_render_session_) {
        APerformanceHint_closeSession(obj_perfhint_render_session_);
        obj_perfhint_render_session_ = nullptr;
    }
    if(obj_perfhint_rhi_session_) {
        APerformanceHint_closeSession(obj_perfhint_rhi_session_);
        obj_perfhint_rhi_session_ = nullptr;
    }
#endif
}

// Indicates the start and end of the performance intensive task.
// The methods call performance hint API to tell the performance
// hint to the system.
void ADPFManager::UpdatePerfHintSession(jlong duration_ns, jlong target_duration_ns, bool update_target_duration,
        APerformanceHintSession* obj_perfhint_session_) {
#if defined(PLATFORM_ANDROID) && __ANDROID_API__ >= 33
    if (obj_perfhint_session_) {
        // UE_LOG(LogAndroidPerformance, Log, TEXT("Update performance hint session duration %lld ns, update target duration %s and %lld ns"),
        //         duration_ns, update_target_duration ? TEXT("true") : TEXT("false"), target_duration_ns);
        APerformanceHint_reportActualWorkDuration(obj_perfhint_session_, duration_ns);
        if(update_target_duration) {
            APerformanceHint_updateTargetWorkDuration(obj_perfhint_session_, target_duration_ns);
        }
    }
#endif
}

jlong ADPFManager::fpsToNanosec(const float maxFPS) {
    return static_cast<jlong>(1000000000.0f / maxFPS);
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
