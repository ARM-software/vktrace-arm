/**************************************************************************
 *
 * Copyright 2014-2018 Valve Corporation, Inc.
 * Copyright (C) 2014-2018 LunarG, Inc.
 * Copyright (C) 2019 ARM Limited.
 * All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Peter Lohrmann <peterl@valvesoftware.com>
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: David Pinedo <david@lunarg.com>
 **************************************************************************/
#include "vulkan/vk_layer.h"

#include "vkreplay_settings.h"
// declared as extern in header
vkreplayer_settings g_vkReplaySettings;

static vkreplayer_settings s_defaultVkReplaySettings = {NULL, 1, 0, UINT_MAX, true, false, NULL, NULL, NULL, NULL, NULL, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, 50, FALSE, FALSE, NULL, FALSE};

vktrace_SettingInfo g_vk_settings_info[] = {
    {"o",
     "Open",
     VKTRACE_SETTING_STRING,
     {&g_vkReplaySettings.pTraceFilePath},
     {&s_defaultVkReplaySettings.pTraceFilePath},
     TRUE,
     "The trace file to open and replay."},
    {"t",
     "TraceFile",
     VKTRACE_SETTING_STRING,
     {&g_vkReplaySettings.pTraceFilePath},
     {&s_defaultVkReplaySettings.pTraceFilePath},
     TRUE,
     "(Deprecated, use -o or --Open instead) The trace file to replay."},
    {"pltf",
     "PreloadTraceFile",
     VKTRACE_SETTING_BOOL,
     {&g_vkReplaySettings.preloadTraceFile},
     {&s_defaultVkReplaySettings.preloadTraceFile},
     TRUE,
     "Preload tracefile to memory before replay."},
#if !defined(ANDROID) && defined(PLATFORM_LINUX)
    {"headless",
     "Headless",
     VKTRACE_SETTING_BOOL,
     {&g_vkReplaySettings.headless},
     {&s_defaultVkReplaySettings.headless},
     TRUE,
     "Replay in headless mode via VK_EXT_headless_surface or VK_ARMX_headless_surface."},
#else
    {"vsyncoff",
     "VsyncOff",
     VKTRACE_SETTING_BOOL,
     {&g_vkReplaySettings.vsyncOff},
     {&s_defaultVkReplaySettings.vsyncOff},
     TRUE,
     "Turn off vsync to avoid replay being vsync-limited."},
#endif
    {"l",
     "NumLoops",
     VKTRACE_SETTING_UINT,
     {&g_vkReplaySettings.numLoops},
     {&s_defaultVkReplaySettings.numLoops},
     TRUE,
     "The number of times to replay the trace file or loop range."},
    {"lsf",
     "LoopStartFrame",
     VKTRACE_SETTING_UINT,
     {&g_vkReplaySettings.loopStartFrame},
     {&s_defaultVkReplaySettings.loopStartFrame},
     TRUE,
     "The start frame number of the loop range."},
    {"lef",
     "LoopEndFrame",
     VKTRACE_SETTING_UINT,
     {&g_vkReplaySettings.loopEndFrame},
     {&s_defaultVkReplaySettings.loopEndFrame},
     TRUE,
     "The end frame number of the loop range."},
    {"c",
     "CompatibilityMode",
     VKTRACE_SETTING_BOOL,
     {&g_vkReplaySettings.compatibilityMode},
     {&s_defaultVkReplaySettings.compatibilityMode},
     TRUE,
     "Use compatibiltiy mode, i.e. convert memory indices to replay device indices, default is TRUE."},
    {"s",
     "Screenshot",
     VKTRACE_SETTING_STRING,
     {&g_vkReplaySettings.screenshotList},
     {&s_defaultVkReplaySettings.screenshotList},
     TRUE,
     "Comma separated list of frames to take a take snapshots of"},
    {"sf",
     "ScreenshotFormat",
     VKTRACE_SETTING_STRING,
     {&g_vkReplaySettings.screenshotColorFormat},
     {&s_defaultVkReplaySettings.screenshotColorFormat},
     TRUE,
     "Color Space format of screenshot files. Formats are UNORM, SNORM, USCALED, SSCALED, UINT, SINT, SRGB"},
    {"x",
     "ExitOnAnyError",
     VKTRACE_SETTING_BOOL,
     {&g_vkReplaySettings.exitOnAnyError},
     {&s_defaultVkReplaySettings.exitOnAnyError},
     TRUE,
     "Exit if an error occurs during replay, default is FALSE"},
    {"sp",
     "ScreenshotPrefix",
     VKTRACE_SETTING_STRING,
     {&g_vkReplaySettings.screenshotPrefix},
     {&s_defaultVkReplaySettings.screenshotPrefix},
     TRUE,
     "/path/to/snapshots/prefix- Must contain full path and a prefix, resulting screenshots will be named prefix-framenumber.ppm"},
    {"pt",
     "EnablePortabilityTableSupport",
     VKTRACE_SETTING_BOOL,
     {&g_vkReplaySettings.enablePortabilityTable},
     {&s_defaultVkReplaySettings.enablePortabilityTable},
     TRUE,
     "Read portability table if it exists."},
    {"mma",
     "SelfManageMemoryAllocation",
     VKTRACE_SETTING_BOOL,
     {&g_vkReplaySettings.selfManageMemAllocation},
     {&s_defaultVkReplaySettings.selfManageMemAllocation},
     TRUE,
     "Manage OPTIMAL image's memory allocation by vkreplay. (Deprecated)"},
    {"fsw",
     "ForceSingleWindow",
     VKTRACE_SETTING_BOOL,
     {&g_vkReplaySettings.forceSingleWindow},
     {&s_defaultVkReplaySettings.forceSingleWindow},
     TRUE,
     "Force single window rendering."},
    {"pmp",
     "memoryPercentage",
     VKTRACE_SETTING_UINT,
     {&g_vkReplaySettings.memoryPercentage},
     {&s_defaultVkReplaySettings.memoryPercentage},
     TRUE,
     "Preload vktrace file block occupancy system memory percentage,the default is 50%"},
    {"prm",
     "premapping",
     VKTRACE_SETTING_BOOL,
     {&g_vkReplaySettings.premapping},
     {&s_defaultVkReplaySettings.premapping},
     TRUE,
     "Premap resources in several vulkan APIs when preloading."},
    {"epc",
     "enablePipelineCache",
     VKTRACE_SETTING_BOOL,
     {&g_vkReplaySettings.enablePipelineCache},
     {&s_defaultVkReplaySettings.enablePipelineCache},
     TRUE,
     "Write pipeline cache to the disk and use the cache data for the next replay."},
    {"pcp",
     "pipelineCachePath",
     VKTRACE_SETTING_STRING,
     {&g_vkReplaySettings.pipelineCachePath},
     {&s_defaultVkReplaySettings.pipelineCachePath},
     TRUE,
     "Set the path for saving the pipeline cache data for the replay."},
    {"fsii",
     "forceSyncImgIdx",
     VKTRACE_SETTING_BOOL,
     {&g_vkReplaySettings.forceSyncImgIdx},
     {&s_defaultVkReplaySettings.forceSyncImgIdx},
     TRUE,
     "Force sync the acquire next image index."}

};

vktrace_SettingGroup g_vkReplaySettingGroup = {"vkreplay_vk", sizeof(g_vk_settings_info) / sizeof(g_vk_settings_info[0]),
                                               &g_vk_settings_info[0]};

void apply_layerSettings_overrides() {
#if 0
    setLayerOptionEnum("DrawStateReportFlags", g_vkReplaySettings.drawStateReportFlags);
    setLayerOptionEnum("DrawStateDebugAction", g_vkReplaySettings.drawStateDebugAction);
    setLayerOptionEnum("MemTrackerReportFlags", g_vkReplaySettings.memTrackerReportFlags);
    setLayerOptionEnum("MemTrackerDebugAction", g_vkReplaySettings.memTrackerDebugAction);
    setLayerOptionEnum("ObjectTrackerReportFlags", g_vkReplaySettings.objectTrackerReportFlags);
    setLayerOptionEnum("ObjectTrackerDebugAction", g_vkReplaySettings.objectTrackerDebugAction);
#endif
}
