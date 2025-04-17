# Android Dynamic Performance Framework (ADPF) Unreal plugin

This repository enables you to use the [ADPF](https://developer.android.com/games/optimize/adpf) plugin in Unreal Engine.

The plugin has two main features: thermal state and CPU performance hints. The plugin monitors the thermal state of a device and proactively adjusts performance before the level of performance becomes unsustainable. CPU performance hints let Android choose the right CPU clocks and core types instead of Android choosing based on previous workloads.

## Setup
1. Copy the AndroidPerformance folder to `GameProjectFolder/Plugins`.
2. Open the game project.
3. Enter **Edit → Plugins**.
4. Find **Android Performance** and check **Enabled**. The Unreal Engine suggests restarting the project.
5. Restart the game project, and trigger a build.

If the game use 'NDK API Level' to 'android-33' or higher, use the main branch. Otherwise use 'use-jni' branch.

## Graphics quality levels
Graphics quality is set by the Unreal Scalability `SetQualityLevels()` function, which changes view distance, anti-aliasing, shadow, post-processing, texture and effects, foliage, and shading quality levels.

For more details about these graphics qualities, see the [Unreal Scalability reference](https://docs.unrealengine.com/4.27/en-US/TestingAndOptimization/PerformanceAndProfiling/Scalability/ScalabilityReference/). The Unreal plugin changes graphics quality level from 0 (lowest) to 3 (highest) based on the thermal state. Customize the graphics quality levels 0-3 based on the needs of your game environment.

The game **must customize ADPF graphic quality logic** to use in-game graphic quality logic.

## License

Copyright 2024 The Android Open Source Project

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
