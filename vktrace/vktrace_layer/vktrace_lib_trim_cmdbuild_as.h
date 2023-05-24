/*
 *
 * Copyright (C) 2019 ARM Limited
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

namespace trim {
struct _scratch_size {
    VkDeviceSize buildSize;
    VkDeviceSize updateSize;
};

struct _shader_device_buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceAddress address;
};

VkResult create_staging_buffer_on_memory(VkCommandBuffer commandBuffer, VkBuffer* buffer, VkDeviceMemory memory,
                                         VkDeviceSize offset, VkDeviceSize size);

VkResult get_cmdbuildas_memory_buffer(VkDeviceAddress address, VkDeviceAddress size, VkDeviceAddress* bufOffset,
                                      VkDeviceMemory* memory, VkBuffer* buffer, VkDeviceAddress* memOffset, uint64_t* buf_gid,
                                      uint64_t* bufMem_gid);

VkResult dump_build_AS_buffer_data(VkCommandBuffer commandBuffer, trim::BuildAsBufferInfo& info);

VkResult remove_cmdbuild_by_as(VkAccelerationStructureKHR as);
VkResult remove_cmdCopyAs_by_as(VkAccelerationStructureKHR as);

VkResult generate_buildas_create_inputs(VkDevice device, uint32_t queueFamilyIndex, BuildAsInfo& buildAsInfo);
VkResult generate_buildas_destroy_inputs(VkDevice device, BuildAsInfo& buildAsInfo);

void set_scratch_size(VkDevice device, VkDeviceSize buildSize, VkDeviceSize updateSize);
_scratch_size* get_max_scratch_size(VkDevice device);
VkResult generate_shader_device_buffer(bool makeCall, VkDevice device, VkDeviceSize size, uint32_t queueFamilyIndex,
                                       VkBufferUsageFlags usage, _shader_device_buffer& scratch_buffer);
void generate_destroy_shader_device_buffer(VkDevice device, _shader_device_buffer& deviceBuffer);
}  // namespace trim