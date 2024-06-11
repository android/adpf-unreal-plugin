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

// Forward declarations of functions that need to be in C decl.
extern "C" {
void nativeThermalStatusChanged(JNIEnv* env, jclass cls, int32_t thermalState);
}

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

    bool registerListener();
    bool unregisterListener();

    // Invoke the method periodically (once a frame) to monitor
    // the device's thermal throttling status.
    void Monitor();

    // Method to set thermal status. Need to be public since the method
    // is called from C native listener.
    void SetThermalStatus(const int32_t i);

    // Method to retrieve thermal manager. The API is used to register/unregister
    // callbacks from C API.
    AThermalManager* GetThermalManager() { return thermal_manager_; }

 private:
    inline jlong fpsToNanosec(const float maxFPS);
    inline jlong findLongestNanosec(const uint32_t a, const uint32_t b);
    void saveQualityLevel(const int32_t warning_level);
    void saveQualityLevel(const float head_room);

    // Update thermal headroom each sec.
    static constexpr int32_t kThermalHeadroomUpdateThreshold = 1;

    // Ctor. It's private since the class is designed as a singleton.
    ADPFManager();

    // Functions to initialize ADPF API's calls.
    bool InitializePowerManager();
    float UpdateThermalStatusHeadRoom();
    bool InitializePerformanceHintManager();

    // Get current thermal status and headroom.
    int32_t GetThermalStatus() { return thermal_status_; }
    float GetThermalHeadroom() { return thermal_headroom_; }

    // Indicates the start and end of the performance intensive task.
    // The methods call performance hint API to tell the performance
    // hint to the system.
    void UpdatePerfHintGameSession(jlong duration_ns, jlong target_duration_ns);
    void UpdatePerfHintRenderSession(jlong duration_ns, jlong target_duration_ns);

    AThermalManager* thermal_manager_;
    bool initialized_performance_hint_manager;
    int32_t thermal_status_; // enum for AThermalStatus
    float thermal_headroom_; // 0.0f ~ 1.0f, can be over 1.0f but it means THERMAL_STATUS_SEVERE 
    float last_clock_;
    jobject obj_power_service_;
    jmethodID get_thermal_headroom_;

    jobject obj_perfhint_service_;
    jobject obj_perfhint_game_session_;
    jobject obj_perfhint_render_session_;
    jmethodID report_actual_game_work_duration_;
    jmethodID report_actual_render_work_duration_;
    jmethodID update_target_game_work_duration_;
    jmethodID update_target_render_work_duration_;
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
