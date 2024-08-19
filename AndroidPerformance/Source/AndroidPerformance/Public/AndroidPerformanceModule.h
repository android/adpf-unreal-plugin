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

#pragma once

#include "Modules/ModuleManager.h"

class FAndroidPerformanceModule final : public IModuleInterface
{
public:
    // IModuleInterface implementation
    void StartupModule() override;
    void ShutdownModule() override;

    void Tick(UWorld* world, ELevelTick tick_type, float delta_time);

    float GetThermalHeadroom();

    int32_t GetThermalStatus();
};
