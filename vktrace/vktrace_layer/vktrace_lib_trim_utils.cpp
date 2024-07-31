/*
 *
 * Copyright (C) 2016-2024 ARM Limited
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
 */

#include "vulkan/vulkan.h"
#include "vk_layer_extension_utils.h"
#include "vk_layer_utils.h"
#include "vktrace_lib_trim_utils.h"

using namespace std;

namespace trim {
uint32_t GetFormatComponentCount(VkFormat format)
{
    return FormatComponentCount(format);
}

uint32_t GetFormatElementSize(VkFormat format)
{
    return FormatElementSize(format);
}

VkExtent3D GetFormatTexelBlockExtent(VkFormat format)
{
    return FormatTexelBlockExtent(format);
}
}  // namespace trim
