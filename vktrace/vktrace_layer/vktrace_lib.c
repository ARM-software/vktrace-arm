/*
 * Copyright 2015-2016 Valve Corporation
 * Copyright (C) 2015-2016 LunarG, Inc.
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
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Peter Lohrmann <peterl@valvesoftware.com>
 */

#include "vktrace_common.h"
#include "vktrace_filelike.h"
#include "vktrace_interconnect.h"
#include "vktrace_vk_vk.h"
#include "vktrace_lib_trim.h"
#include "vktrace_lib_helpers.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Environment variables
// These are needed because Windows may not have getenv available.
// See the Windows man page for getenv to find out why.

#if defined(_WIN32)
static inline char *vktrace_layer_getenv(const char *name) {
    char *retVal;
    DWORD valSize;
    valSize = GetEnvironmentVariableA(name, NULL, 0);
    // valSize DOES include the null terminator, so for any set variable
    // will always be at least 1. If it's 0, the variable wasn't set.
    if (valSize == 0) return NULL;
    retVal = (char *)malloc(valSize);
    GetEnvironmentVariableA(name, retVal, valSize);
    return retVal;
}

static inline void vktrace_layer_free_getenv(const char *val) { free((void *)val); }
#else
static inline char* vktrace_layer_getenv(const char* name) { return getenv(name); }

static inline void vktrace_layer_free_getenv(const char* val) {}
#endif

VKTRACER_EXIT TrapExit(void) { vktrace_LogVerbose("vktrace_lib TrapExit."); }

extern VKTRACER_ENTRY _Load(void) {
    // only do the hooking and networking if the tracer is NOT loaded by vktrace
    if (vktrace_is_loaded_into_vktrace() == FALSE) {
        char *verbosity;
        vktrace_LogSetCallback(loggingCallback);
        verbosity = vktrace_layer_getenv(_VKTRACE_VERBOSITY_ENV);
        if (verbosity && !strcmp(verbosity, "quiet"))
            vktrace_LogSetLevel(VKTRACE_LOG_NONE);
        else if (verbosity && !strcmp(verbosity, "warnings"))
            vktrace_LogSetLevel(VKTRACE_LOG_WARNING);
        else if (verbosity && !strcmp(verbosity, "full"))
            vktrace_LogSetLevel(VKTRACE_LOG_VERBOSE);
#if defined(_DEBUG)
        else if (verbosity && !strcmp(verbosity, "debug"))
            vktrace_LogSetLevel(VKTRACE_LOG_DEBUG);
#endif
        else if (verbosity && !strcmp(verbosity, "max"))
#if defined(_DEBUG)
            vktrace_LogSetLevel(VKTRACE_LOG_DEBUG);
#else
            vktrace_LogSetLevel(VKTRACE_LOG_VERBOSE);
#endif
        else
            // Either verbosity=="errors", or it wasn't specified
            vktrace_LogSetLevel(VKTRACE_LOG_ERROR);

        vktrace_layer_free_getenv(verbosity);

        vktrace_LogVerbose("vktrace_lib library loaded into PID %d", vktrace_get_pid());
        atexit(TrapExit);

// If you need to debug startup, build with this set to true, then attach and change it to false.
#if defined(_DEBUG)
        {
            bool debugStartup = false;
            while (debugStartup)
                ;
        }
#endif
    }
}

#if defined(WIN32)
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    hModule;
    lpReserved;

    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            _Load();
            break;
        }
        case DLL_PROCESS_DETACH: {
            _Unload();
            break;
        }
        default:
            break;
    }
    return TRUE;
}
#endif
#if defined(__cplusplus)
}
#endif
