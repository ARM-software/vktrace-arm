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
#pragma once
#include "vktrace_lib_trim_statetracker.h"
#include "vktrace_trace_packet_identifiers.h"
#include "vulkan/vulkan.h"
#include "vulkan/vk_layer.h"


namespace trim {

struct _shader_host_buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void* hostAddress;
};

VkResult dump_build_AS_buffer_data(VkDevice device, trim::BuildAsBufferInfo& info, const void* pData);
VkResult dump_build_MP_buffer_data(VkDevice device, trim::BuildAsBufferInfo& info, const void* pData);

VkResult remove_build_by_as(VkDevice device, VkAccelerationStructureKHR as);
VkResult remove_CopyAs_by_as(VkAccelerationStructureKHR as);

VkResult remove_build_by_mp(VkDevice device, VkMicromapEXT mp);
VkResult remove_CopyMp_by_mp(VkMicromapEXT mp);
VkResult remove_WriteMp_by_mp(VkMicromapEXT mp);

VkResult generate_buildas_create_inputs(VkDevice device, uint32_t queueFamilyIndex, BuildAsInfo& buildAsInfo);
VkResult generate_buildas_destroy_inputs(VkDevice device, BuildAsInfo& buildAsInfo);
VkResult only_buildas_destroy_inputs(VkDevice device, BuildAsInfo& buildAsInfo);

VkResult generate_buildmp_create_inputs(VkDevice device, uint32_t queueFamilyIndex, BuildMpInfo& buildMpInfo);
VkResult generate_buildmp_destroy_inputs(VkDevice device, BuildMpInfo& buildMpInfo);
VkResult only_buildmp_destroy_inputs(VkDevice device, BuildMpInfo& buildMpInfo);

VkResult generate_shader_host_buffer(bool makeCall, VkDevice device, VkDeviceSize size, uint32_t queueFamilyIndex,
                                       VkBufferUsageFlags usage, _shader_host_buffer& scratch_buffer);
void generate_destroy_shader_host_buffer(VkDevice device, _shader_host_buffer& deviceBuffer);
}  // namespace trim