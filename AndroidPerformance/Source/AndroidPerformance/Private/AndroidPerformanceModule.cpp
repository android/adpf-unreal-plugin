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

#include "AndroidPerformanceModule.h"

#define LOGTEXT_NAMESPACE "AndroidPerformance"

#include "AndroidPerformanceLog.h"
#include "ADPFManager.h"

IMPLEMENT_MODULE(FAndroidPerformanceModule, AndroidPerformance)

DEFINE_LOG_CATEGORY(LogAndroidPerformance);

void FAndroidPerformanceModule::StartupModule()
{
#if PLATFORM_ANDROID
    UE_LOG(LogAndroidPerformance, Log, TEXT("Android Performance Module Started"));

    bool isInitialized = ADPFManager::getInstance().initialize();

    // registration tick
    if(isInitialized)
    {
        FWorldDelegates::OnWorldTickStart.AddRaw(this, &FAndroidPerformanceModule::Tick);
    }
    else
    {
        UE_LOG(LogAndroidPerformance, Log, TEXT("Android Performance is not initialized because of not supporint device"));
    }
#endif
}

void FAndroidPerformanceModule::ShutdownModule()
{
#if PLATFORM_ANDROID
    UE_LOG(LogAndroidPerformance, Log, TEXT("Android Performance Module Shutdown"));

    // unregistration tick
    FWorldDelegates::OnWorldTickStart.RemoveAll(this);

    ADPFManager::getInstance().destroy();
#endif
}

void FAndroidPerformanceModule::Tick(UWorld* world, ELevelTick tick_type, float delta_time)
{
#if PLATFORM_ANDROID
    ADPFManager::getInstance().Monitor();
#endif
}

#undef LOGTEXT_NAMESPACE
