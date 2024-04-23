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
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES:
            {
                VkPhysicalDeviceMultiviewFeatures& dst = *reinterpret_cast<VkPhysicalDeviceMultiviewFeatures*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceMultiviewFeatures& src = features.at(pDummy->sType).multiview_feature;
                    CLAMP_BOOL_VALUE(multiview);
                    CLAMP_BOOL_VALUE(multiviewGeometryShader);
                    CLAMP_BOOL_VALUE(multiviewTessellationShader);
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES:
            {
                VkPhysicalDeviceSamplerYcbcrConversionFeatures& dst = *reinterpret_cast<VkPhysicalDeviceSamplerYcbcrConversionFeatures*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceSamplerYcbcrConversionFeatures& src = features.at(pDummy->sType).sampler_ycbcr_conv_feature;
                    CLAMP_BOOL_VALUE(samplerYcbcrConversion);
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR:
            {
                VkPhysicalDeviceAccelerationStructureFeaturesKHR& dst = *reinterpret_cast<VkPhysicalDeviceAccelerationStructureFeaturesKHR*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceAccelerationStructureFeaturesKHR& src = features.at(pDummy->sType).acceleration_structure_feature;
                    CLAMP_BOOL_VALUE(accelerationStructure)
                    CLAMP_BOOL_VALUE(accelerationStructureCaptureReplay)
                    CLAMP_BOOL_VALUE(accelerationStructureIndirectBuild)
                    CLAMP_BOOL_VALUE(accelerationStructureHostCommands)
                    CLAMP_BOOL_VALUE(descriptorBindingAccelerationStructureUpdateAfterBind)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR:
            {
                VkPhysicalDeviceRayTracingPipelineFeaturesKHR& dst = *reinterpret_cast<VkPhysicalDeviceRayTracingPipelineFeaturesKHR*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceRayTracingPipelineFeaturesKHR& src = features.at(pDummy->sType).ray_tracing_pipeline_feature;
                    CLAMP_BOOL_VALUE(rayTracingPipeline)
                    CLAMP_BOOL_VALUE(rayTracingPipelineShaderGroupHandleCaptureReplay)
                    CLAMP_BOOL_VALUE(rayTracingPipelineShaderGroupHandleCaptureReplayMixed)
                    CLAMP_BOOL_VALUE(rayTracingPipelineTraceRaysIndirect)
                    CLAMP_BOOL_VALUE(rayTraversalPrimitiveCulling)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR:
            {
                VkPhysicalDeviceRayQueryFeaturesKHR& dst = *reinterpret_cast<VkPhysicalDeviceRayQueryFeaturesKHR*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceRayQueryFeaturesKHR& src = features.at(pDummy->sType).ray_query_feature;
                    CLAMP_BOOL_VALUE(rayQuery)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
            {
                VkPhysicalDeviceBufferDeviceAddressFeatures& dst = *reinterpret_cast<VkPhysicalDeviceBufferDeviceAddressFeatures*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceBufferDeviceAddressFeatures& src = features.at(pDummy->sType).buffer_device_address_feature;
                    CLAMP_BOOL_VALUE(bufferDeviceAddress)
                    CLAMP_BOOL_VALUE(bufferDeviceAddressCaptureReplay)
                    CLAMP_BOOL_VALUE(bufferDeviceAddressMultiDevice)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT:
            {
                VkPhysicalDeviceFragmentDensityMapFeaturesEXT& dst = *reinterpret_cast<VkPhysicalDeviceFragmentDensityMapFeaturesEXT*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceFragmentDensityMapFeaturesEXT& src = features.at(pDummy->sType).fragment_density_map_feature;
                    CLAMP_BOOL_VALUE(fragmentDensityMap)
                    CLAMP_BOOL_VALUE(fragmentDensityMapDynamic)
                    CLAMP_BOOL_VALUE(fragmentDensityMapNonSubsampledImages)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_FEATURES_EXT:
            {
                VkPhysicalDeviceFragmentDensityMap2FeaturesEXT& dst = *reinterpret_cast<VkPhysicalDeviceFragmentDensityMap2FeaturesEXT*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceFragmentDensityMap2FeaturesEXT& src = features.at(pDummy->sType).fragment_density_map2_feature;
                    CLAMP_BOOL_VALUE(fragmentDensityMapDeferred)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES:
            {
                VkPhysicalDeviceImagelessFramebufferFeatures& dst = *reinterpret_cast<VkPhysicalDeviceImagelessFramebufferFeatures*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceImagelessFramebufferFeatures& src = features.at(pDummy->sType).imageless_framebuffer_feature;
                    CLAMP_BOOL_VALUE(imagelessFramebuffer)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT:
            {
                VkPhysicalDeviceTransformFeedbackFeaturesEXT& dst = *reinterpret_cast<VkPhysicalDeviceTransformFeedbackFeaturesEXT*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceTransformFeedbackFeaturesEXT& src = features.at(pDummy->sType).transform_feedback_feature;
                    CLAMP_BOOL_VALUE(transformFeedback)
                    CLAMP_BOOL_VALUE(geometryStreams)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT:
            {
                VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT& dst = *reinterpret_cast<VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT& src = features.at(pDummy->sType).vertex_attribute_divisor_feature;
                    CLAMP_BOOL_VALUE(vertexAttributeInstanceRateDivisor)
                    CLAMP_BOOL_VALUE(vertexAttributeInstanceRateZeroDivisor)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR:
            {
                VkPhysicalDeviceTimelineSemaphoreFeaturesKHR& dst = *reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeaturesKHR*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceTimelineSemaphoreFeaturesKHR& src = features.at(pDummy->sType).timeline_semaphore_feature;
                    CLAMP_BOOL_VALUE(timelineSemaphore)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES:
            {
                VkPhysicalDeviceTextureCompressionASTCHDRFeatures& dst = *reinterpret_cast<VkPhysicalDeviceTextureCompressionASTCHDRFeatures*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceTextureCompressionASTCHDRFeatures& src = features.at(pDummy->sType).texture_compression_astc_hdr_feature;
                    CLAMP_BOOL_VALUE(textureCompressionASTC_HDR)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES:
            {
                VkPhysicalDeviceDescriptorIndexingFeatures& dst = *reinterpret_cast<VkPhysicalDeviceDescriptorIndexingFeatures*>(pDummy);
                if (features.find(pDummy->sType) != features.end()) {
                    const VkPhysicalDeviceDescriptorIndexingFeatures& src = features.at(pDummy->sType).descriptor_indexing_feature;
                    CLAMP_BOOL_VALUE(shaderInputAttachmentArrayDynamicIndexing)
                    CLAMP_BOOL_VALUE(shaderUniformTexelBufferArrayDynamicIndexing)
                    CLAMP_BOOL_VALUE(shaderStorageTexelBufferArrayDynamicIndexing)
                    CLAMP_BOOL_VALUE(shaderUniformBufferArrayNonUniformIndexing)
                    CLAMP_BOOL_VALUE(shaderSampledImageArrayNonUniformIndexing)
                    CLAMP_BOOL_VALUE(shaderStorageBufferArrayNonUniformIndexing)
                    CLAMP_BOOL_VALUE(shaderStorageImageArrayNonUniformIndexing)
                    CLAMP_BOOL_VALUE(shaderInputAttachmentArrayNonUniformIndexing)
                    CLAMP_BOOL_VALUE(shaderUniformTexelBufferArrayNonUniformIndexing)
                    CLAMP_BOOL_VALUE(shaderStorageTexelBufferArrayNonUniformIndexing)
                    CLAMP_BOOL_VALUE(descriptorBindingUniformBufferUpdateAfterBind)
                    CLAMP_BOOL_VALUE(descriptorBindingSampledImageUpdateAfterBind)
                    CLAMP_BOOL_VALUE(descriptorBindingStorageImageUpdateAfterBind)
                    CLAMP_BOOL_VALUE(descriptorBindingStorageBufferUpdateAfterBind)
                    CLAMP_BOOL_VALUE(descriptorBindingUniformTexelBufferUpdateAfterBind)
                    CLAMP_BOOL_VALUE(descriptorBindingStorageTexelBufferUpdateAfterBind)
                    CLAMP_BOOL_VALUE(descriptorBindingUpdateUnusedWhilePending)
                    CLAMP_BOOL_VALUE(descriptorBindingPartiallyBound)
                    CLAMP_BOOL_VALUE(descriptorBindingVariableDescriptorCount)
                    CLAMP_BOOL_VALUE(runtimeDescriptorArray)
                }
            }
            break;
        default:
            ErrorPrintf("Unhandled extension feature %d\n", pDummy->sType);
            break;
        }
        pNext = const_cast<void*>(pDummy->pNext);
    }
}

void ClampExtendedDevProperties(const std::unordered_map<uint32_t, ExtendedProperty>& properties, void* pNext) {
    while(pNext) {
        VkApplicationInfo* pDummy = (VkApplicationInfo*)pNext;
        switch (pDummy->sType)
        {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR:
            {
                VkPhysicalDeviceAccelerationStructurePropertiesKHR& dst = *reinterpret_cast<VkPhysicalDeviceAccelerationStructurePropertiesKHR*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceAccelerationStructurePropertiesKHR& src = properties.at(pDummy->sType).acceleration_structure_prop;
                    CLAMP_TO_SMALL_VALUE(maxGeometryCount);
                    CLAMP_TO_SMALL_VALUE(maxInstanceCount);
                    CLAMP_TO_SMALL_VALUE(maxPrimitiveCount);
                    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorAccelerationStructures);
                    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorUpdateAfterBindAccelerationStructures);
                    CLAMP_TO_SMALL_VALUE(maxDescriptorSetAccelerationStructures);
                    CLAMP_TO_SMALL_VALUE(maxDescriptorSetUpdateAfterBindAccelerationStructures);
                    CLAMP_TO_LARGE_VALUE(minAccelerationStructureScratchOffsetAlignment);
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT:
            {
                VkPhysicalDeviceExternalMemoryHostPropertiesEXT& dst = *reinterpret_cast<VkPhysicalDeviceExternalMemoryHostPropertiesEXT*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceExternalMemoryHostPropertiesEXT& src = properties.at(pDummy->sType).external_memory_host_prop;
                    CLAMP_TO_LCM_VALUE(minImportedHostPointerAlignment);
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT:
            {
                VkPhysicalDeviceTransformFeedbackPropertiesEXT& dst = *reinterpret_cast<VkPhysicalDeviceTransformFeedbackPropertiesEXT*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceTransformFeedbackPropertiesEXT& src = properties.at(pDummy->sType).transform_feedback_prop;
                    CLAMP_TO_SMALL_VALUE(maxTransformFeedbackStreams);
                    CLAMP_TO_SMALL_VALUE(maxTransformFeedbackBuffers);
                    CLAMP_TO_SMALL_VALUE(maxTransformFeedbackBufferSize);
                    CLAMP_TO_SMALL_VALUE(maxTransformFeedbackStreamDataSize);
                    CLAMP_TO_SMALL_VALUE(maxTransformFeedbackBufferDataSize);
                    CLAMP_TO_SMALL_VALUE(maxTransformFeedbackBufferDataStride);
                    CLAMP_BOOL_VALUE(transformFeedbackQueries);
                    CLAMP_BOOL_VALUE(transformFeedbackStreamsLinesTriangles);
                    CLAMP_BOOL_VALUE(transformFeedbackRasterizationStreamSelect);
                    CLAMP_BOOL_VALUE(transformFeedbackDraw);
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES:
            {
                VkPhysicalDeviceSubgroupProperties& dst = *reinterpret_cast<VkPhysicalDeviceSubgroupProperties*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceSubgroupProperties& src = properties.at(pDummy->sType).physical_device_subgroup_prop;
                    CLAMP_BOOL_VALUE(quadOperationsInAllStages);
                    CLAMP_TO_SMALL_VALUE(subgroupSize);
                    CLAMP_TO_SMALL_VALUE(supportedOperations);
                    CLAMP_TO_SMALL_VALUE(supportedStages);
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES:
            // ignore
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR:
            {
                VkPhysicalDeviceRayTracingPipelinePropertiesKHR& dst = *reinterpret_cast<VkPhysicalDeviceRayTracingPipelinePropertiesKHR*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& src = properties.at(pDummy->sType).ray_tracing_pipeline_prop;
                    CLAMP_TO_LARGE_VALUE(shaderGroupHandleSize)
                    CLAMP_TO_SMALL_VALUE(maxRayRecursionDepth)
                    CLAMP_TO_LCM_VALUE(shaderGroupBaseAlignment)
                    CLAMP_TO_LARGE_VALUE(shaderGroupHandleCaptureReplaySize)
                    CLAMP_TO_SMALL_VALUE(maxRayDispatchInvocationCount)
                    CLAMP_TO_LCM_VALUE(shaderGroupHandleAlignment)
                    CLAMP_TO_SMALL_VALUE(maxRayHitAttributeSize)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT:
            {
                VkPhysicalDeviceFragmentDensityMapPropertiesEXT& dst = *reinterpret_cast<VkPhysicalDeviceFragmentDensityMapPropertiesEXT*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceFragmentDensityMapPropertiesEXT& src = properties.at(pDummy->sType).fragment_density_map_prop;
                    CLAMP_TO_LARGE_VALUE(minFragmentDensityTexelSize.width)
                    CLAMP_TO_LARGE_VALUE(minFragmentDensityTexelSize.height)
                    CLAMP_TO_SMALL_VALUE(maxFragmentDensityTexelSize.width)
                    CLAMP_TO_SMALL_VALUE(maxFragmentDensityTexelSize.height)
                    CLAMP_BOOL_VALUE(fragmentDensityInvocations)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_PROPERTIES_EXT:
            {
                VkPhysicalDeviceFragmentDensityMap2PropertiesEXT& dst = *reinterpret_cast<VkPhysicalDeviceFragmentDensityMap2PropertiesEXT*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceFragmentDensityMap2PropertiesEXT& src = properties.at(pDummy->sType).fragment_density_map2_prop;
                    CLAMP_BOOL_VALUE(subsampledLoads)
                    CLAMP_BOOL_VALUE(subsampledCoarseReconstructionEarlyAccess)
                    CLAMP_TO_SMALL_VALUE(maxSubsampledArrayLayers)
                    CLAMP_TO_SMALL_VALUE(maxDescriptorSetSubsampledSamplers)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES:
            {
                VkPhysicalDeviceMultiviewProperties& dst = *reinterpret_cast<VkPhysicalDeviceMultiviewProperties*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceMultiviewProperties& src = properties.at(pDummy->sType).multiview_prop;
                    CLAMP_TO_SMALL_VALUE(maxMultiviewViewCount)
                    CLAMP_TO_SMALL_VALUE(maxMultiviewInstanceIndex)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT:
            {
                VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT& dst = *reinterpret_cast<VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT& src = properties.at(pDummy->sType).vertex_attribute_divisor_prop;
                    CLAMP_TO_SMALL_VALUE(maxVertexAttribDivisor)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES_KHR:
            {
                VkPhysicalDeviceTimelineSemaphorePropertiesKHR& dst = *reinterpret_cast<VkPhysicalDeviceTimelineSemaphorePropertiesKHR*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceTimelineSemaphorePropertiesKHR& src = properties.at(pDummy->sType).timeline_semaphore_prop;
                    CLAMP_TO_SMALL_VALUE(maxTimelineSemaphoreValueDifference)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES:
            {
                VkPhysicalDeviceDepthStencilResolveProperties& dst = *reinterpret_cast<VkPhysicalDeviceDepthStencilResolveProperties*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceDepthStencilResolveProperties& src = properties.at(pDummy->sType).depth_stencil_resolve_prop;
                    CLAMP_TO_SMALL_VALUE(supportedDepthResolveModes)
                    CLAMP_TO_SMALL_VALUE(supportedStencilResolveModes)
                    CLAMP_BOOL_VALUE(independentResolveNone)
                    CLAMP_BOOL_VALUE(independentResolve)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR:
            {
                VkPhysicalDeviceFloatControlsPropertiesKHR& dst = *reinterpret_cast<VkPhysicalDeviceFloatControlsPropertiesKHR*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceFloatControlsPropertiesKHR& src = properties.at(pDummy->sType).float_controls_prop;
                    CLAMP_TO_SMALL_VALUE(denormBehaviorIndependence)
                    CLAMP_TO_SMALL_VALUE(roundingModeIndependence)
                    CLAMP_BOOL_VALUE(shaderSignedZeroInfNanPreserveFloat16)
                    CLAMP_BOOL_VALUE(shaderSignedZeroInfNanPreserveFloat32)
                    CLAMP_BOOL_VALUE(shaderSignedZeroInfNanPreserveFloat64)
                    CLAMP_BOOL_VALUE(shaderDenormPreserveFloat16)
                    CLAMP_BOOL_VALUE(shaderDenormPreserveFloat32)
                    CLAMP_BOOL_VALUE(shaderDenormPreserveFloat64)
                    CLAMP_BOOL_VALUE(shaderDenormFlushToZeroFloat16)
                    CLAMP_BOOL_VALUE(shaderDenormFlushToZeroFloat32)
                    CLAMP_BOOL_VALUE(shaderDenormFlushToZeroFloat64)
                    CLAMP_BOOL_VALUE(shaderRoundingModeRTEFloat16)
                    CLAMP_BOOL_VALUE(shaderRoundingModeRTEFloat32)
                    CLAMP_BOOL_VALUE(shaderRoundingModeRTEFloat64)
                    CLAMP_BOOL_VALUE(shaderRoundingModeRTZFloat16)
                    CLAMP_BOOL_VALUE(shaderRoundingModeRTZFloat32)
                    CLAMP_BOOL_VALUE(shaderRoundingModeRTZFloat64)
                }
            }
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES:
            {
                VkPhysicalDeviceDescriptorIndexingProperties& dst = *reinterpret_cast<VkPhysicalDeviceDescriptorIndexingProperties*>(pDummy);
                if (properties.find(pDummy->sType) != properties.end()) {
                    const VkPhysicalDeviceDescriptorIndexingProperties& src = properties.at(pDummy->sType).descriptor_indexing_prop;
                    CLAMP_TO_SMALL_VALUE(maxUpdateAfterBindDescriptorsInAllPools)
                    CLAMP_BOOL_VALUE(shaderUniformBufferArrayNonUniformIndexingNative)
                    CLAMP_BOOL_VALUE(shaderSampledImageArrayNonUniformIndexingNative)
                    CLAMP_BOOL_VALUE(shaderStorageBufferArrayNonUniformIndexingNative)
                    CLAMP_BOOL_VALUE(shaderStorageImageArrayNonUniformIndexingNative)
                    CLAMP_BOOL_VALUE(shaderInputAttachmentArrayNonUniformIndexingNative)
                    CLAMP_BOOL_VALUE(robustBufferAccessUpdateAfterBind)
                    CLAMP_BOOL_VALUE(quadDivergentImplicitLod)
                    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorUpdateAfterBindSamplers)
                    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorUpdateAfterBindUniformBuffers)
                    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorUpdateAfterBindStorageBuffers)
                    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorUpdateAfterBindSampledImages)
                    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorUpdateAfterBindStorageImages)
                    CLAMP_TO_SMALL_VALUE(maxPerStageDescriptorUpdateAfterBindInputAttachments)
                    CLAMP_TO_SMALL_VALUE(maxPerStageUpdateAfterBindResources)
                    CLAMP_TO_SMALL_VALUE(maxDescriptorSetUpdateAfterBindSamplers)
                    CLAMP_TO_SMALL_VALUE(maxDescriptorSetUpdateAfterBindUniformBuffers)
                    CLAMP_TO_SMALL_VALUE(maxDescriptorSetUpdateAfterBindUniformBuffersDynamic)
                    CLAMP_TO_SMALL_VALUE(maxDescriptorSetUpdateAfterBindStorageBuffers)
                    CLAMP_TO_SMALL_VALUE(maxDescriptorSetUpdateAfterBindStorageBuffersDynamic)
                    CLAMP_TO_SMALL_VALUE(maxDescriptorSetUpdateAfterBindSampledImages)
                    CLAMP_TO_SMALL_VALUE(maxDescriptorSetUpdateAfterBindStorageImages)
                    CLAMP_TO_SMALL_VALUE(maxDescriptorSetUpdateAfterBindInputAttachments)
                }
            }
            break;
        default:
            ErrorPrintf("Unhandled extension property %d\n", pDummy->sType);
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
