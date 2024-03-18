# ADPF(Android Dynamic Performance Framework) Unreal Plugin

This repository is for using [ADPF](https://developer.android.com/games/optimize/adpf) in Unreal Engine.
It has two main features thermal state and CPU performance hints. The thermal state monitor the thermal state of a device and then proactively adjust performance before it becomes unsustainable. CPU performance hints provide performance hints that let Android choose the right CPU clocks and core types instead of Android choosing based on previous workloads.

## Setup
1. Copy AndroidPerformance folder to ‘GameProjectFolder/Plugins’.
2. Open the game project.
3. Enter Edit → Plugins.
4. Find ‘Android Performance’ and check ‘Enabled’. The Unreal Engine suggests restarting the project after checking it.
5. Restart the game project, and trigger a build.

## Graphics quality levels
Graphics quality is set by Unreal Scalability’s SetQualityLevels(), and this changes view distance, anti aliasing, shadow, post processing, texture and effects, foliage, and shading quality levels. 

For more details about these graphics quality, you can read the [Unreal Scalability reference](https://docs.unrealengine.com/4.27/en-US/TestingAndOptimization/PerformanceAndProfiling/Scalability/ScalabilityReference/). The Unreal plugin changes these graphics quality from 0 (lowest) to 3 (highest) levels based on the thermal state. It is recommended to customize these graphics quality levels 0-3 based on the needs of your game environment.

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
