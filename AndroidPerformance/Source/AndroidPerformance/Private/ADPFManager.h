/*
 * Copyright 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ADPF_MANAGER_H_
#define ADPF_MANAGER_H_

#include <android/log.h>
#include <android/thermal.h>
#include <jni.h>
#include <android_native_app_glue.h>

#include "Scalability.h"

/*
 * ADPFManager class anages the ADPF APIs.
 */
class ADPFManager {
public:
    // Singleton function.
    static ADPFManager& getInstance() {
        static ADPFManager instance;
        return instance;
    }
    // Dtor.
    ~ADPFManager();
    // Delete copy constructor since the class is used as a singleton.
    ADPFManager(ADPFManager const&) = delete;
    void operator=(ADPFManager const&) = delete;

    bool initialize();
    void destroy();

    // Invoke the method periodically (once a frame) to monitor
    // the device's thermal throttling status.
    void Monitor();

    // Method to retrieve thermal manager. The API is used to register/unregister
    // callbacks from C API.
    AThermalManager* GetThermalManager() { return thermal_manager_; }

private:
    inline jlong fpsToNanosec(const float maxFPS);
    void saveQualityLevel(const float head_room);

    // Update thermal headroom every 15 seconds.
    static constexpr int32_t kThermalHeadroomUpdateThreshold = 15;

    // Get current thermal headroom.
    static constexpr int32_t kThermalHeadroomForecastSeconds = 0;

    // Ctor. It's private since the class is designed as a singleton.
    ADPFManager();

    // Functions to initialize ADPF API's calls.
    bool InitializePowerManager();
    float UpdateThermalStatusHeadRoom();
    bool InitializePerformanceHintManager();
    void DestroyPerformanceHintManager();

    // Get current thermal status and headroom.
    float GetThermalHeadroom() { return thermal_headroom_; }

    // Indicates the start and end of the performance intensive task.
    // The methods call performance hint API to tell the performance
    // hint to the system.
    void UpdatePerfHintSession(jlong duration_ns, jlong target_duration_ns, bool update_target_duration,
            jobject obj_perfhint_session_, jmethodID report_actual_work_duration, jmethodID update_target_work_duration);

    AThermalManager* thermal_manager_;
    bool initialized_performance_hint_manager;
    bool support_performance_hint_manager;
    float thermal_headroom_;
    float last_clock_;
    jobject obj_power_service_;
    jmethodID get_thermal_headroom_;

    jobject obj_perfhint_service_;
    jobject obj_perfhint_game_session_;
    jobject obj_perfhint_render_session_;
    jobject obj_perfhint_rhi_session_;
    jmethodID report_actual_game_work_duration_;
    jmethodID report_actual_render_work_duration_;
    jmethodID report_actual_rhi_work_duration_;
    jmethodID update_target_game_work_duration_;
    jmethodID update_target_render_work_duration_;
    jmethodID update_target_rhi_work_duration_;
    jlong preferred_update_rate_;

    static const int32_t max_quality_count = 4;
    Scalability::FQualityLevels quality_levels[max_quality_count];
    int32_t current_quality_level;
    int32_t target_quality_level;

    float prev_max_fps;
    jlong prev_max_fps_nano;

    // for debug
    float fps_total;
    int fps_count;
};

#endif    // ADPF_MANAGER_H_
