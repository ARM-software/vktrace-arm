#!/bin/bash

# Copyright (C) 2019-2023 ARM Limited

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#      http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

VULKAN_HEADER="external/Vulkan-Headers/build/install/include/vulkan/vulkan_core.h"

# check CFLAGS contains -m32 to make sure this script only works with x86 build
if ! [[ $CFLAGS =~ "-m32" ]]
then
    echo "Skip $0 since the script is only needed when building x86 vktrace."
    exit
fi

# check file exists
if [ ! -f $1 ]
then
    echo "File $1 not exist!"
    exit -1
fi

if [ -z $1 ]
then
    echo "Vulkan header path not specified, use default ${VULKAN_HEADER}!"
else
    VULKAN_HEADER=$1
    echo "Adding aligned(8) to ${VULKAN_HEADER}!"
fi

sed -i -r -e 's/(^    uint64_t [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
# VkDeviceSize is defined by "typedef uint64_t VkDeviceSize;"
sed -i -r -e 's/(^    VkDeviceSize [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}

# handles defined by VK_DEFINE_NON_DISPATCHABLE_HANDLE are 64-bit handles
if [ $(basename ${VULKAN_HEADER}) == "vulkan_core.h" ]
then
    HANDLE_LIST_64BIT=`grep "^VK_DEFINE_NON_DISPATCHABLE_HANDLE(.*)$" ${VULKAN_HEADER} | sed -e "s/VK_DEFINE_NON_DISPATCHABLE_HANDLE(//g" -e "s/)//g"`
    for HANDLE in ${HANDLE_LIST_64BIT}
    do
        sed -i -r -e "s/(^    ${HANDLE} [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/" ${VULKAN_HEADER}
    done
else
    sed -i -r -e 's/(^    VkSemaphore [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkFence [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkDeviceMemory [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkBuffer [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkImage [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkEvent [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkQueryPool [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkBufferView [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkImageView [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkShaderModule [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkPipelineCache [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkPipelineLayout [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkRenderPass [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkPipeline [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkDescriptorSetLayout [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkSampler [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkDescriptorPool [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkDescriptorSet [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkFramebuffer [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkCommandPool [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkSamplerYcbcrConversion [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkDescriptorUpdateTemplate [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkSurfaceKHR [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkSwapchainKHR [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkDisplayKHR [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkDisplayModeKHR [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkDebugReportCallbackEXT [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkObjectTableNVX [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkIndirectCommandsLayoutNVX [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkDebugUtilsMessengerEXT [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
    sed -i -r -e 's/(^    VkValidationCacheEXT [ a-zA-Z]+);$/\1 __attribute__((aligned(8)));/' ${VULKAN_HEADER}
fi
