/*
 * (C) COPYRIGHT 2019 ARM Limited
 * ALL RIGHTS RESERVED
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

#include <cstring>
#include <list>
#include <vector>
#include <unordered_map>

#include "vulkan/vulkan.h"
#include "dev_sim_ext_features.h"
#include "dev_sim_compatmode.h"

template<class T> T gcd(T a, T b) {
    if (!a || !b) return a > b ? a : b;
    for (T t; (t = a % b); a = b, b = t);
    return b;
}

template<class T> T lcm(T a, T b) {
    return a*b/gcd(a, b);
}

#define CLAMP_TO_SMALL_VALUE(name) if (src.name < dst.name) { \
                                       dst.name = src.name; }

#define CLAMP_TO_LARGE_VALUE(name) if (src.name > dst.name) { \
                                       dst.name = src.name; }

#define CLAMP_TO_LCM_VALUE(name) if (src.name != dst.name) { \
                                     dst.name = lcm(dst.name, src.name); }

#define CLAMP_BOOL_VALUE(name) if (!src.name || !dst.name) { \
                                   dst.name = false; }

#define CLAMP_BIT_FLAGS(name) if (src.name != dst.name) { \
                                  dst.name = dst.name & src.name; }

extern void ErrorPrintf(const char *fmt, ...);
extern void DebugPrintf(const char *fmt, ...);

void ClampPhyDevLimits(const VkPhysicalDeviceLimits& src, VkPhysicalDeviceLimits& dst) {
    CLAMP_TO_SMALL_VALUE(maxImageDimension1D);
    CLAMP_TO_SMALL_VALUE(maxImageDimension2D);
    CLAMP_TO_SMALL_VALUE(maxImageDimension3D);
    CLAMP_TO_SMALL_VALUE(maxImageDimensionCube);
    CLAMP_TO_SMALL_VALUE(maxImageArrayLayers);
    CLAMP_TO_SMALL_VALUE(maxTexelBufferElements);
    CLAMP_TO_SMALL_VALUE(maxUniformBufferRange);
    CLAMP_TO_SMALL_VALUE(maxStorageBufferRange);
    CLAMP_TO_SMALL_VALUE(maxPushConstantsSize);
    CLAMP_TO_SMALL_VALUE(maxMemoryAllocationCount);
    CLAMP_TO_SMALL_VALUE(maxSamplerAllocationCount);
    CLAMP_TO_LARGE_VALUE(bufferImageGranularity);
    CLAMP_TO_SMALL_VALUE(sparseAddressSpaceSize);
    CLAMP_TO_SMALL_VALUE(maxBoundDescriptorSets);
    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorSamplers);
    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorUniformBuffers);
    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorStorageBuffers);
    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorSampledImages);
    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorStorageImages);
    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorInputAttachments);
    CLAMP_TO_SMALL_VALUE(maxPerStageResources);
    CLAMP_TO_SMALL_VALUE(maxDescriptorSetSamplers);
    CLAMP_TO_SMALL_VALUE(maxDescriptorSetUniformBuffers);
    CLAMP_TO_SMALL_VALUE(maxDescriptorSetUniformBuffersDynamic);
    CLAMP_TO_SMALL_VALUE(maxDescriptorSetStorageBuffers);
    CLAMP_TO_SMALL_VALUE(maxDescriptorSetStorageBuffersDynamic);
    CLAMP_TO_SMALL_VALUE(maxDescriptorSetSampledImages);
    CLAMP_TO_SMALL_VALUE(maxDescriptorSetStorageImages);
    CLAMP_TO_SMALL_VALUE(maxDescriptorSetInputAttachments);
    CLAMP_TO_SMALL_VALUE(maxVertexInputAttributes);
    CLAMP_TO_SMALL_VALUE(maxVertexInputBindings);
    CLAMP_TO_SMALL_VALUE(maxVertexInputAttributeOffset);
    CLAMP_TO_SMALL_VALUE(maxVertexInputBindingStride);
    CLAMP_TO_SMALL_VALUE(maxVertexOutputComponents);
    CLAMP_TO_SMALL_VALUE(maxTessellationGenerationLevel);
    CLAMP_TO_SMALL_VALUE(maxTessellationPatchSize);
    CLAMP_TO_SMALL_VALUE(maxTessellationControlPerVertexInputComponents);
    CLAMP_TO_SMALL_VALUE(maxTessellationControlPerVertexOutputComponents);
    CLAMP_TO_SMALL_VALUE(maxTessellationControlPerPatchOutputComponents);
    CLAMP_TO_SMALL_VALUE(maxTessellationControlTotalOutputComponents);
    CLAMP_TO_SMALL_VALUE(maxTessellationEvaluationInputComponents);
    CLAMP_TO_SMALL_VALUE(maxTessellationEvaluationOutputComponents);
    CLAMP_TO_SMALL_VALUE(maxGeometryShaderInvocations);
    CLAMP_TO_SMALL_VALUE(maxGeometryInputComponents);
    CLAMP_TO_SMALL_VALUE(maxGeometryOutputComponents);
    CLAMP_TO_SMALL_VALUE(maxGeometryOutputVertices);
    CLAMP_TO_SMALL_VALUE(maxGeometryTotalOutputComponents);
    CLAMP_TO_SMALL_VALUE(maxFragmentInputComponents);
    CLAMP_TO_SMALL_VALUE(maxFragmentOutputAttachments);
    CLAMP_TO_SMALL_VALUE(maxFragmentDualSrcAttachments);
    CLAMP_TO_SMALL_VALUE(maxFragmentCombinedOutputResources);
    CLAMP_TO_SMALL_VALUE(maxComputeSharedMemorySize);
    CLAMP_TO_SMALL_VALUE(maxComputeWorkGroupCount[0]);
    CLAMP_TO_SMALL_VALUE(maxComputeWorkGroupCount[1]);
    CLAMP_TO_SMALL_VALUE(maxComputeWorkGroupCount[2]);
    CLAMP_TO_SMALL_VALUE(maxComputeWorkGroupInvocations);
    CLAMP_TO_SMALL_VALUE(maxComputeWorkGroupSize[0]);
    CLAMP_TO_SMALL_VALUE(maxComputeWorkGroupSize[1]);
    CLAMP_TO_SMALL_VALUE(maxComputeWorkGroupSize[2]);
    CLAMP_TO_SMALL_VALUE(subPixelPrecisionBits);
    CLAMP_TO_SMALL_VALUE(subTexelPrecisionBits);
    CLAMP_TO_SMALL_VALUE(mipmapPrecisionBits);
    CLAMP_TO_SMALL_VALUE(maxDrawIndexedIndexValue);
    CLAMP_TO_SMALL_VALUE(maxDrawIndirectCount);
    CLAMP_TO_SMALL_VALUE(maxSamplerLodBias);
    CLAMP_TO_SMALL_VALUE(maxSamplerAnisotropy);
    CLAMP_TO_SMALL_VALUE(maxViewports);
    CLAMP_TO_SMALL_VALUE(maxViewportDimensions[0]);
    CLAMP_TO_SMALL_VALUE(maxViewportDimensions[1]);
    CLAMP_TO_LARGE_VALUE(viewportBoundsRange[0]);
    CLAMP_TO_SMALL_VALUE(viewportBoundsRange[1]);
    CLAMP_TO_SMALL_VALUE(viewportSubPixelBits);
    CLAMP_TO_LCM_VALUE(minMemoryMapAlignment);
    CLAMP_TO_LCM_VALUE(minTexelBufferOffsetAlignment);
    CLAMP_TO_LCM_VALUE(minUniformBufferOffsetAlignment);
    CLAMP_TO_LCM_VALUE(minStorageBufferOffsetAlignment);
    CLAMP_TO_LARGE_VALUE(minTexelOffset);
    CLAMP_TO_SMALL_VALUE(maxTexelOffset);
    CLAMP_TO_LARGE_VALUE(minTexelGatherOffset);
    CLAMP_TO_SMALL_VALUE(maxTexelGatherOffset);
    CLAMP_TO_LARGE_VALUE(minInterpolationOffset);
    CLAMP_TO_SMALL_VALUE(maxInterpolationOffset);
    CLAMP_TO_SMALL_VALUE(subPixelInterpolationOffsetBits);
    CLAMP_TO_SMALL_VALUE(maxFramebufferWidth);
    CLAMP_TO_SMALL_VALUE(maxFramebufferHeight);
    CLAMP_TO_SMALL_VALUE(maxFramebufferLayers);
    CLAMP_BIT_FLAGS(framebufferColorSampleCounts);
    CLAMP_BIT_FLAGS(framebufferDepthSampleCounts);
    CLAMP_BIT_FLAGS(framebufferStencilSampleCounts);
    CLAMP_BIT_FLAGS(framebufferNoAttachmentsSampleCounts);
    CLAMP_TO_SMALL_VALUE(maxColorAttachments);
    CLAMP_BIT_FLAGS(sampledImageColorSampleCounts);
    CLAMP_BIT_FLAGS(sampledImageIntegerSampleCounts);
    CLAMP_BIT_FLAGS(sampledImageDepthSampleCounts);
    CLAMP_BIT_FLAGS(sampledImageStencilSampleCounts);
    CLAMP_BIT_FLAGS(storageImageSampleCounts);
    CLAMP_TO_SMALL_VALUE(maxSampleMaskWords);
    CLAMP_BOOL_VALUE(timestampComputeAndGraphics);
    CLAMP_TO_SMALL_VALUE(maxClipDistances);
    CLAMP_TO_SMALL_VALUE(maxCullDistances);
    CLAMP_TO_SMALL_VALUE(maxCombinedClipAndCullDistances);
    CLAMP_TO_LARGE_VALUE(pointSizeRange[0]);
    CLAMP_TO_SMALL_VALUE(pointSizeRange[1]);
    CLAMP_TO_LARGE_VALUE(lineWidthRange[0]);
    CLAMP_TO_SMALL_VALUE(lineWidthRange[1]);
    CLAMP_TO_LARGE_VALUE(pointSizeGranularity);
    CLAMP_TO_LARGE_VALUE(lineWidthGranularity);
    CLAMP_BOOL_VALUE(strictLines);
    CLAMP_BOOL_VALUE(standardSampleLocations);
    CLAMP_TO_LCM_VALUE(optimalBufferCopyOffsetAlignment);
    CLAMP_TO_LCM_VALUE(optimalBufferCopyRowPitchAlignment);
    CLAMP_TO_LCM_VALUE(nonCoherentAtomSize);
}

void ClampPhyDevFeatures(const VkPhysicalDeviceFeatures& src, VkPhysicalDeviceFeatures& dst) {
    CLAMP_BOOL_VALUE(robustBufferAccess);
    CLAMP_BOOL_VALUE(fullDrawIndexUint32);
    CLAMP_BOOL_VALUE(imageCubeArray);
    CLAMP_BOOL_VALUE(independentBlend);
    CLAMP_BOOL_VALUE(geometryShader);
    CLAMP_BOOL_VALUE(tessellationShader);
    CLAMP_BOOL_VALUE(sampleRateShading);
    CLAMP_BOOL_VALUE(dualSrcBlend);
    CLAMP_BOOL_VALUE(logicOp);
    CLAMP_BOOL_VALUE(multiDrawIndirect);
    CLAMP_BOOL_VALUE(drawIndirectFirstInstance);
    CLAMP_BOOL_VALUE(depthClamp);
    CLAMP_BOOL_VALUE(depthBiasClamp);
    CLAMP_BOOL_VALUE(fillModeNonSolid);
    CLAMP_BOOL_VALUE(depthBounds);
    CLAMP_BOOL_VALUE(wideLines);
    CLAMP_BOOL_VALUE(largePoints);
    CLAMP_BOOL_VALUE(alphaToOne);
    CLAMP_BOOL_VALUE(multiViewport);
    CLAMP_BOOL_VALUE(samplerAnisotropy);
    CLAMP_BOOL_VALUE(textureCompressionETC2);
    CLAMP_BOOL_VALUE(textureCompressionASTC_LDR);
    CLAMP_BOOL_VALUE(textureCompressionBC);
    CLAMP_BOOL_VALUE(occlusionQueryPrecise);
    CLAMP_BOOL_VALUE(pipelineStatisticsQuery);
    CLAMP_BOOL_VALUE(vertexPipelineStoresAndAtomics);
    CLAMP_BOOL_VALUE(fragmentStoresAndAtomics);
    CLAMP_BOOL_VALUE(shaderTessellationAndGeometryPointSize);
    CLAMP_BOOL_VALUE(shaderImageGatherExtended);
    CLAMP_BOOL_VALUE(shaderStorageImageExtendedFormats);
    CLAMP_BOOL_VALUE(shaderStorageImageMultisample);
    CLAMP_BOOL_VALUE(shaderStorageImageReadWithoutFormat);
    CLAMP_BOOL_VALUE(shaderStorageImageWriteWithoutFormat);
    CLAMP_BOOL_VALUE(shaderUniformBufferArrayDynamicIndexing);
    CLAMP_BOOL_VALUE(shaderSampledImageArrayDynamicIndexing);
    CLAMP_BOOL_VALUE(shaderStorageBufferArrayDynamicIndexing);
    CLAMP_BOOL_VALUE(shaderStorageImageArrayDynamicIndexing);
    CLAMP_BOOL_VALUE(shaderClipDistance);
    CLAMP_BOOL_VALUE(shaderCullDistance);
    CLAMP_BOOL_VALUE(shaderFloat64);
    CLAMP_BOOL_VALUE(shaderInt64);
    CLAMP_BOOL_VALUE(shaderInt16);
    CLAMP_BOOL_VALUE(shaderResourceResidency);
    CLAMP_BOOL_VALUE(shaderResourceMinLod);
    CLAMP_BOOL_VALUE(sparseBinding);
    CLAMP_BOOL_VALUE(sparseResidencyBuffer);
    CLAMP_BOOL_VALUE(sparseResidencyImage2D);
    CLAMP_BOOL_VALUE(sparseResidencyImage3D);
    CLAMP_BOOL_VALUE(sparseResidency2Samples);
    CLAMP_BOOL_VALUE(sparseResidency4Samples);
    CLAMP_BOOL_VALUE(sparseResidency8Samples);
    CLAMP_BOOL_VALUE(sparseResidency16Samples);
    CLAMP_BOOL_VALUE(sparseResidencyAliased);
    CLAMP_BOOL_VALUE(variableMultisampleRate);
    CLAMP_BOOL_VALUE(inheritedQueries);
}

void ClampQueueFamilyProps(const VkQueueFamilyProperties& src, VkQueueFamilyProperties& dst) {
    CLAMP_BIT_FLAGS(queueFlags);
    CLAMP_TO_SMALL_VALUE(queueCount);
    CLAMP_TO_SMALL_VALUE(timestampValidBits);
    CLAMP_TO_SMALL_VALUE(minImageTransferGranularity.width);
    CLAMP_TO_SMALL_VALUE(minImageTransferGranularity.height);
    CLAMP_TO_SMALL_VALUE(minImageTransferGranularity.depth);
}

void ClampFormatProps(const VkFormatProperties& src, VkFormatProperties& dst) {
    CLAMP_BIT_FLAGS(bufferFeatures);
    CLAMP_BIT_FLAGS(linearTilingFeatures);
    CLAMP_BIT_FLAGS(optimalTilingFeatures);
}

void ClampDevExtProps(const std::vector<VkExtensionProperties>& src, std::vector<VkExtensionProperties>& dst) {
    std::list<VkExtensionProperties> clamped;
    for (uint32_t i = 0; i < src.size(); i++) {
        for (uint32_t j = 0; j < dst.size(); j++) {
            if (strcmp(src[i].extensionName, dst[j].extensionName) == 0) {
                clamped.push_back(src[i]);
                break;
            }
        }
    }
    dst.resize(clamped.size());
    uint32_t i = 0;
    for (auto it = clamped.begin(); it != clamped.end(); it++) {
        dst[i++] = *it;
    }
}

void ClampExtendedDevFeatures(const std::unordered_map<uint32_t, ExtendedFeature>& features, void* pNext) {
    while(pNext) {
        VkApplicationInfo* pDummy = (VkApplicationInfo*)pNext;
        switch (pDummy->sType)
        {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ASTC_DECODE_FEATURES_EXT:
            {
                VkPhysicalDeviceASTCDecodeFeaturesEXT& dst = *reinterpret_cast<VkPhysicalDeviceASTCDecodeFeaturesEXT*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceASTCDecodeFeaturesEXT& src = features.at(pDummy->sType).astc_decode_feature;
                    CLAMP_BOOL_VALUE(decodeModeSharedExponent);
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES:
            {
                VkPhysicalDeviceScalarBlockLayoutFeatures& dst = *reinterpret_cast<VkPhysicalDeviceScalarBlockLayoutFeatures*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceScalarBlockLayoutFeatures& src = features.at(pDummy->sType).scalar_block_layout_feature;
                    CLAMP_BOOL_VALUE(scalarBlockLayout);
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES:
            {
                VkPhysicalDeviceShaderFloat16Int8Features& dst = *reinterpret_cast<VkPhysicalDeviceShaderFloat16Int8Features*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceShaderFloat16Int8Features& src = features.at(pDummy->sType).shader_float16_int8_feature;
                    CLAMP_BOOL_VALUE(shaderFloat16);
                    CLAMP_BOOL_VALUE(shaderInt8);
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES:
            {
                VkPhysicalDevice16BitStorageFeatures& dst = *reinterpret_cast<VkPhysicalDevice16BitStorageFeatures*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDevice16BitStorageFeatures& src = features.at(pDummy->sType).bit16_storage_feature;
                    CLAMP_BOOL_VALUE(storageBuffer16BitAccess);
                    CLAMP_BOOL_VALUE(uniformAndStorageBuffer16BitAccess);
                    CLAMP_BOOL_VALUE(storagePushConstant16);
                    CLAMP_BOOL_VALUE(storageInputOutput16);
                }
            }
            break;
        default:
            ErrorPrintf("Unhandled extension %d", pDummy->sType);
            break;
        }
        pNext = const_cast<void*>(pDummy->pNext);
    }
}

#undef CLAMP_TO_SMALL_VALUE
#undef CLAMP_TO_LARGE_VALUE
#undef CLAMP_TO_LCM_VALUE
#undef CLAMP_BOOL_VALUE
#undef CLAMP_BIT_FLAGS
