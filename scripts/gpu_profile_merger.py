#!/usr/bin/python3 -i
#
# Copyright (C) 2022 ARM Limited
#

import argparse
import json
import sys
from math import gcd

def merge_dev_feature2(src1, src2):
    dst = []
    for item1 in src1:
        for item2 in src2:
            if item1["extension"] == item2["extension"] and item1["name"] == item2["name"]:
                dst_item = {}
                dst_item["extension"] = item1["extension"]
                dst_item["name"] = item1["name"]
                dst_item["supported"] = False
                if (item1["supported"] and item2["supported"]):
                    dst_item["supported"] = True
                dst.append(dst_item)
    return dst

def lcm(a, b):
    return int((a * b) / gcd(a, b))

min_op = lambda x, y: str(min(int(x), int(y)))
max_op = lambda x, y: str(max(int(x), int(y)))
bool_op = lambda x, y: x and y
lcm_op = lambda x, y: str(lcm(int(x), int(y)))

def merge_shder_float_ctrls_indep(src1, src2):
    a = int(src1)
    b = int(src2)
    if (a + b <= 2):
        return min(a, b)
    else:
        return max(a, b)

dev_prop2_merge_optbl = {
    "VK_EXT_inline_uniform_block" : {
        "maxInlineUniformBlockSize" : min_op,
        "maxPerStageDescriptorInlineUniformBlocks" : min_op,
        "maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks" : min_op,
        "maxDescriptorSetInlineUniformBlocks" : min_op,
        "maxDescriptorSetUpdateAfterBindInlineUniformBlocks" : min_op,
    },
    "VK_EXT_descriptor_indexing" : {
        "maxUpdateAfterBindDescriptorsInAllPools" : min_op,
        "shaderUniformBufferArrayNonUniformIndexingNative" : bool_op,
        "shaderSampledImageArrayNonUniformIndexingNative" : bool_op,
        "shaderStorageBufferArrayNonUniformIndexingNative" : bool_op,
        "shaderStorageImageArrayNonUniformIndexingNative" : bool_op,
        "shaderInputAttachmentArrayNonUniformIndexingNative" : bool_op,
        "robustBufferAccessUpdateAfterBind" : bool_op,
        "quadDivergentImplicitLod" : bool_op,
        "maxPerStageDescriptorUpdateAfterBindSamplers" : min_op,
        "maxPerStageDescriptorUpdateAfterBindUniformBuffers" : min_op,
        "maxPerStageDescriptorUpdateAfterBindStorageBuffers" : min_op,
        "maxPerStageDescriptorUpdateAfterBindSampledImages" : min_op,
        "maxPerStageDescriptorUpdateAfterBindStorageImages" : min_op,
        "maxPerStageDescriptorUpdateAfterBindInputAttachments" : min_op,
        "maxPerStageUpdateAfterBindResources" : min_op,
        "maxDescriptorSetUpdateAfterBindSamplers" : min_op,
        "maxDescriptorSetUpdateAfterBindUniformBuffers" : min_op,
        "maxDescriptorSetUpdateAfterBindUniformBuffersDynamic" : min_op,
        "maxDescriptorSetUpdateAfterBindStorageBuffers" : min_op,
        "maxDescriptorSetUpdateAfterBindStorageBuffersDynamic" : min_op,
        "maxDescriptorSetUpdateAfterBindSampledImages" : min_op,
        "maxDescriptorSetUpdateAfterBindStorageImages" : min_op,
        "maxDescriptorSetUpdateAfterBindInputAttachments" : min_op,
    },
    "VK_KHR_multiview" : {
        "maxMultiviewViewCount" : min_op,
        "maxMultiviewInstanceIndex" : min_op,
    },
    "VK_KHR_acceleration_structure" : {
        "maxGeometryCount" : min_op,
        "maxInstanceCount" : min_op,
        "maxPrimitiveCount" : min_op,
        "maxPerStageDescriptorAccelerationStructures" : min_op,
        "maxPerStageDescriptorUpdateAfterBindAccelerationStructures" : min_op,
        "maxDescriptorSetAccelerationStructures" : min_op,
        "maxDescriptorSetUpdateAfterBindAccelerationStructures" : min_op,
        "minAccelerationStructureScratchOffsetAlignment" : lcm_op,
    },
    "VK_KHR_ray_tracing_pipeline" : {
        "shaderGroupHandleSize" : max_op,
        "maxRayRecursionDepth" : min_op,
        "maxShaderGroupStride" : min_op,
        "shaderGroupBaseAlignment" : lcm_op,
        "shaderGroupHandleCaptureReplaySize" : max_op,
        "maxRayDispatchInvocationCount" : min_op,
        "shaderGroupHandleAlignment" : lcm_op,
        "maxRayHitAttributeSize" : min_op,
    },
    "VK_KHR_shader_float_controls" : {
        "denormBehaviorIndependence" : lambda x, y: str(merge_shder_float_ctrls_indep(x, y)),
        "roundingModeIndependence" : lambda x, y: str(merge_shder_float_ctrls_indep(x, y)),
        "shaderSignedZeroInfNanPreserveFloat16" : bool_op,
        "shaderSignedZeroInfNanPreserveFloat32" : bool_op,
        "shaderSignedZeroInfNanPreserveFloat64" : bool_op,
        "shaderDenormPreserveFloat16" : bool_op,
        "shaderDenormPreserveFloat32" : bool_op,
        "shaderDenormPreserveFloat64" : bool_op,
        "shaderDenormFlushToZeroFloat16" : bool_op,
        "shaderDenormFlushToZeroFloat32" : bool_op,
        "shaderDenormFlushToZeroFloat64" : bool_op,
        "shaderRoundingModeRTEFloat16" : bool_op,
        "shaderRoundingModeRTEFloat32" : bool_op,
        "shaderRoundingModeRTEFloat64" : bool_op,
        "shaderRoundingModeRTZFloat16" : bool_op,
        "shaderRoundingModeRTZFloat32" : bool_op,
        "shaderRoundingModeRTZFloat64" : bool_op,
    }
}

def merge_dev_properties2(src1, src2):
    dst = []
    for item1 in src1:
        for item2 in src2:
            if item1["extension"] == item2["extension"] and item1["name"] == item2["name"]:
                if item1["extension"] in dev_prop2_merge_optbl and item1["name"] in dev_prop2_merge_optbl[item1["extension"]]:
                    merged_item = {}
                    merged_item["extension"] = item1["extension"]
                    merged_item["name"] = item1["name"]
                    merged_item["value"] = dev_prop2_merge_optbl[item1["extension"]][item1["name"]](item1["value"], item2["value"])
                    dst.append(merged_item)
                # else:
                #     print("Common extension property " + item1["extension"] + "." + item1["name"] + " is NOT in the table !")
    return dst

def merge_pdev_features(src1, src2):
    dst = {}
    for feat_key in src1.keys():
        dst[feat_key] = src1[feat_key] and src2[feat_key]
    return dst

def merge_pdev_properties(src1, src2):
    dst = {}
    dst["apiVersion"] = min(src1["apiVersion"], src2["apiVersion"])
    dst["deviceID"] = 2449604624
    dst["deviceName"] = "dev_sim_mixed"
    dst["deviceType"] = 1
    dst["displayName"] = "dev_sim_mixed"
    dst["driverVersion"] = 0

    limitation = {}
    src1_limit = src1["limits"]
    src2_limit = src2["limits"]
    limitation["bufferImageGranularity"] = max(src1_limit["bufferImageGranularity"], src2_limit["bufferImageGranularity"])
    limitation["discreteQueuePriorities"] = min(src1_limit["discreteQueuePriorities"], src2_limit["discreteQueuePriorities"])
    limitation["framebufferColorSampleCounts"] = min(src1_limit["framebufferColorSampleCounts"], src2_limit["framebufferColorSampleCounts"])
    limitation["framebufferDepthSampleCounts"] = min(src1_limit["framebufferDepthSampleCounts"], src2_limit["framebufferDepthSampleCounts"])
    limitation["framebufferNoAttachmentsSampleCounts"] = min(src1_limit["framebufferNoAttachmentsSampleCounts"], src2_limit["framebufferNoAttachmentsSampleCounts"])
    limitation["framebufferStencilSampleCounts"] = max(src1_limit["framebufferStencilSampleCounts"], src2_limit["framebufferStencilSampleCounts"])
    limitation["lineWidthGranularity"] = max(src1_limit["lineWidthGranularity"], src2_limit["lineWidthGranularity"])
    limitation["lineWidthRange"] = src1_limit["lineWidthRange"]
    limitation["lineWidthRange"][0] = max(limitation["lineWidthRange"][0], src2_limit["lineWidthRange"][0])
    limitation["lineWidthRange"][1] = min(limitation["lineWidthRange"][1], src2_limit["lineWidthRange"][1])
    limitation["maxBoundDescriptorSets"] = min(src1_limit["maxBoundDescriptorSets"], src2_limit["maxBoundDescriptorSets"])
    limitation["maxClipDistances"] = min(src1_limit["maxClipDistances"], src2_limit["maxClipDistances"])
    limitation["maxColorAttachments"] = min(src1_limit["maxColorAttachments"], src2_limit["maxColorAttachments"])
    limitation["maxCombinedClipAndCullDistances"] = min(src1_limit["maxCombinedClipAndCullDistances"], src2_limit["maxCombinedClipAndCullDistances"])
    limitation["maxComputeSharedMemorySize"] = min(src1_limit["maxComputeSharedMemorySize"], src2_limit["maxComputeSharedMemorySize"])
    limitation["maxComputeWorkGroupCount"] = src1_limit["maxComputeWorkGroupCount"]
    limitation["maxComputeWorkGroupCount"][0] = min(limitation["maxComputeWorkGroupCount"][0], src2_limit["maxComputeWorkGroupCount"][0])
    limitation["maxComputeWorkGroupCount"][1] = min(limitation["maxComputeWorkGroupCount"][1], src2_limit["maxComputeWorkGroupCount"][1])
    limitation["maxComputeWorkGroupCount"][2] = min(limitation["maxComputeWorkGroupCount"][2], src2_limit["maxComputeWorkGroupCount"][2])
    limitation["maxComputeWorkGroupInvocations"] = min(src1_limit["maxComputeWorkGroupInvocations"], src2_limit["maxComputeWorkGroupInvocations"])
    limitation["maxComputeWorkGroupSize"] = src1_limit["maxComputeWorkGroupSize"]
    limitation["maxComputeWorkGroupSize"][0] = min(limitation["maxComputeWorkGroupSize"][0], src2_limit["maxComputeWorkGroupSize"][0])
    limitation["maxComputeWorkGroupSize"][1] = min(limitation["maxComputeWorkGroupSize"][1], src2_limit["maxComputeWorkGroupSize"][1])
    limitation["maxComputeWorkGroupSize"][2] = min(limitation["maxComputeWorkGroupSize"][2], src2_limit["maxComputeWorkGroupSize"][2])
    limitation["maxCullDistances"] = min(src1_limit["maxCullDistances"], src2_limit["maxCullDistances"])
    limitation["maxDescriptorSetInputAttachments"] = min(src1_limit["maxDescriptorSetInputAttachments"], src2_limit["maxDescriptorSetInputAttachments"])
    limitation["maxDescriptorSetSampledImages"] = min(src1_limit["maxDescriptorSetSampledImages"], src2_limit["maxDescriptorSetSampledImages"])
    limitation["maxDescriptorSetSamplers"] = min(src1_limit["maxDescriptorSetSamplers"], src2_limit["maxDescriptorSetSamplers"])
    limitation["maxDescriptorSetStorageBuffers"] = min(src1_limit["maxDescriptorSetStorageBuffers"], src2_limit["maxDescriptorSetStorageBuffers"])
    limitation["maxDescriptorSetStorageBuffersDynamic"] = min(src1_limit["maxDescriptorSetStorageBuffersDynamic"], src2_limit["maxDescriptorSetStorageBuffersDynamic"])
    limitation["maxDescriptorSetStorageImages"] = min(src1_limit["maxDescriptorSetStorageImages"], src2_limit["maxDescriptorSetStorageImages"])
    limitation["maxDescriptorSetUniformBuffers"] = min(src1_limit["maxDescriptorSetUniformBuffers"], src2_limit["maxDescriptorSetUniformBuffers"])
    limitation["maxDescriptorSetUniformBuffersDynamic"] = min(src1_limit["maxDescriptorSetUniformBuffersDynamic"], src2_limit["maxDescriptorSetUniformBuffersDynamic"])
    limitation["maxDrawIndexedIndexValue"] = min(src1_limit["maxDrawIndexedIndexValue"], src2_limit["maxDrawIndexedIndexValue"])
    limitation["maxDrawIndirectCount"] = min(src1_limit["maxDrawIndirectCount"], src2_limit["maxDrawIndirectCount"])
    limitation["maxFragmentCombinedOutputResources"] = min(src1_limit["maxFragmentCombinedOutputResources"], src2_limit["maxFragmentCombinedOutputResources"])
    limitation["maxFragmentDualSrcAttachments"] = min(src1_limit["maxFragmentDualSrcAttachments"], src2_limit["maxFragmentDualSrcAttachments"])
    limitation["maxFragmentInputComponents"] = min(src1_limit["maxFragmentInputComponents"], src2_limit["maxFragmentInputComponents"])
    limitation["maxFragmentOutputAttachments"] = min(src1_limit["maxFragmentOutputAttachments"], src2_limit["maxFragmentOutputAttachments"])
    limitation["maxFramebufferHeight"] = min(src1_limit["maxFramebufferHeight"], src2_limit["maxFramebufferHeight"])
    limitation["maxFramebufferLayers"] = min(src1_limit["maxFramebufferLayers"], src2_limit["maxFramebufferLayers"])
    limitation["maxFramebufferWidth"] = min(src1_limit["maxFramebufferWidth"], src2_limit["maxFramebufferWidth"])
    limitation["maxGeometryInputComponents"] = min(src1_limit["maxGeometryInputComponents"], src2_limit["maxGeometryInputComponents"])
    limitation["maxGeometryOutputComponents"] = min(src1_limit["maxGeometryOutputComponents"], src2_limit["maxGeometryOutputComponents"])
    limitation["maxGeometryOutputVertices"] = min(src1_limit["maxGeometryOutputVertices"], src2_limit["maxGeometryOutputVertices"])
    limitation["maxGeometryShaderInvocations"] = min(src1_limit["maxGeometryShaderInvocations"], src2_limit["maxGeometryShaderInvocations"])
    limitation["maxGeometryTotalOutputComponents"] = min(src1_limit["maxGeometryTotalOutputComponents"], src2_limit["maxGeometryTotalOutputComponents"])
    limitation["maxImageArrayLayers"] = min(src1_limit["maxImageArrayLayers"], src2_limit["maxImageArrayLayers"])
    limitation["maxImageDimension1D"] = min(src1_limit["maxImageDimension1D"], src2_limit["maxImageDimension1D"])
    limitation["maxImageDimension2D"] = min(src1_limit["maxImageDimension2D"], src2_limit["maxImageDimension2D"])
    limitation["maxImageDimension3D"] = min(src1_limit["maxImageDimension3D"], src2_limit["maxImageDimension3D"])
    limitation["maxImageDimensionCube"] = min(src1_limit["maxImageDimensionCube"], src2_limit["maxImageDimensionCube"])
    limitation["maxInterpolationOffset"] = min(src1_limit["maxInterpolationOffset"], src2_limit["maxInterpolationOffset"])
    limitation["maxMemoryAllocationCount"] = min(src1_limit["maxMemoryAllocationCount"], src2_limit["maxMemoryAllocationCount"])
    limitation["maxPerStageDescriptorInputAttachments"] = min(src1_limit["maxPerStageDescriptorInputAttachments"], src2_limit["maxPerStageDescriptorInputAttachments"])
    limitation["maxPerStageDescriptorSampledImages"] = min(src1_limit["maxPerStageDescriptorSampledImages"], src2_limit["maxPerStageDescriptorSampledImages"])
    limitation["maxPerStageDescriptorSamplers"] = min(src1_limit["maxPerStageDescriptorSamplers"], src2_limit["maxPerStageDescriptorSamplers"])
    limitation["maxPerStageDescriptorStorageBuffers"] = min(src1_limit["maxPerStageDescriptorStorageBuffers"], src2_limit["maxPerStageDescriptorStorageBuffers"])
    limitation["maxPerStageDescriptorStorageImages"] = min(src1_limit["maxPerStageDescriptorStorageImages"], src2_limit["maxPerStageDescriptorStorageImages"])
    limitation["maxPerStageDescriptorUniformBuffers"] = min(src1_limit["maxPerStageDescriptorUniformBuffers"], src2_limit["maxPerStageDescriptorUniformBuffers"])
    limitation["maxPerStageResources"] = min(src1_limit["maxPerStageResources"], src2_limit["maxPerStageResources"])
    limitation["maxPushConstantsSize"] = min(src1_limit["maxPushConstantsSize"], src2_limit["maxPushConstantsSize"])
    limitation["maxSampleMaskWords"] = min(src1_limit["maxSampleMaskWords"], src2_limit["maxSampleMaskWords"])
    limitation["maxSamplerAllocationCount"] = min(src1_limit["maxSamplerAllocationCount"], src2_limit["maxSamplerAllocationCount"])
    limitation["maxSamplerAnisotropy"] = min(src1_limit["maxSamplerAnisotropy"], src2_limit["maxSamplerAnisotropy"])
    limitation["maxSamplerLodBias"] = min(src1_limit["maxSamplerLodBias"], src2_limit["maxSamplerLodBias"])
    limitation["maxStorageBufferRange"] = min(src1_limit["maxStorageBufferRange"], src2_limit["maxStorageBufferRange"])
    limitation["maxTessellationControlPerPatchOutputComponents"] = min(src1_limit["maxTessellationControlPerPatchOutputComponents"], src2_limit["maxTessellationControlPerPatchOutputComponents"])
    limitation["maxTessellationControlPerVertexInputComponents"] = min(src1_limit["maxTessellationControlPerVertexInputComponents"], src2_limit["maxTessellationControlPerVertexInputComponents"])
    limitation["maxTessellationControlPerVertexOutputComponents"] = min(src1_limit["maxTessellationControlPerVertexOutputComponents"], src2_limit["maxTessellationControlPerVertexOutputComponents"])
    limitation["maxTessellationControlTotalOutputComponents"] = min(src1_limit["maxTessellationControlTotalOutputComponents"], src2_limit["maxTessellationControlTotalOutputComponents"])
    limitation["maxTessellationEvaluationInputComponents"] = min(src1_limit["maxTessellationEvaluationInputComponents"], src2_limit["maxTessellationEvaluationInputComponents"])
    limitation["maxTessellationEvaluationOutputComponents"] = min(src1_limit["maxTessellationEvaluationOutputComponents"], src2_limit["maxTessellationEvaluationOutputComponents"])
    limitation["maxTessellationGenerationLevel"] = min(src1_limit["maxTessellationGenerationLevel"], src2_limit["maxTessellationGenerationLevel"])
    limitation["maxTessellationPatchSize"] = min(src1_limit["maxTessellationPatchSize"], src2_limit["maxTessellationPatchSize"])
    limitation["maxTexelBufferElements"] = min(src1_limit["maxTexelBufferElements"], src2_limit["maxTexelBufferElements"])
    limitation["maxTexelGatherOffset"] = min(src1_limit["maxTexelGatherOffset"], src2_limit["maxTexelGatherOffset"])
    limitation["maxTexelOffset"] = min(src1_limit["maxTexelOffset"], src2_limit["maxTexelOffset"])
    limitation["maxUniformBufferRange"] = min(src1_limit["maxUniformBufferRange"], src2_limit["maxUniformBufferRange"])
    limitation["maxVertexInputAttributeOffset"] = min(src1_limit["maxVertexInputAttributeOffset"], src2_limit["maxVertexInputAttributeOffset"])
    limitation["maxVertexInputAttributes"] = min(src1_limit["maxVertexInputAttributes"], src2_limit["maxVertexInputAttributes"])
    limitation["maxVertexInputBindingStride"] = min(src1_limit["maxVertexInputBindingStride"], src2_limit["maxVertexInputBindingStride"])
    limitation["maxVertexInputBindings"] = min(src1_limit["maxVertexInputBindings"], src2_limit["maxVertexInputBindings"])
    limitation["maxVertexOutputComponents"] = min(src1_limit["maxVertexOutputComponents"], src2_limit["maxVertexOutputComponents"])
    limitation["maxViewportDimensions"] = src1_limit["maxViewportDimensions"]
    limitation["maxViewportDimensions"][0] = min(limitation["maxViewportDimensions"][0], src2_limit["maxViewportDimensions"][0])
    limitation["maxViewportDimensions"][1] = min(limitation["maxViewportDimensions"][1], src2_limit["maxViewportDimensions"][1])
    limitation["maxViewports"] = min(src1_limit["maxViewports"], src2_limit["maxViewports"])
    limitation["minInterpolationOffset"] = max(src1_limit["minInterpolationOffset"], src2_limit["minInterpolationOffset"])
    limitation["minMemoryMapAlignment"] = lcm(src1_limit["minMemoryMapAlignment"], src2_limit["minMemoryMapAlignment"])
    limitation["minStorageBufferOffsetAlignment"] = lcm(src1_limit["minStorageBufferOffsetAlignment"], src2_limit["minStorageBufferOffsetAlignment"])
    limitation["minTexelBufferOffsetAlignment"] = lcm(src1_limit["minTexelBufferOffsetAlignment"], src2_limit["minTexelBufferOffsetAlignment"])
    limitation["minTexelGatherOffset"] = max(src1_limit["minTexelGatherOffset"], src2_limit["minTexelGatherOffset"])
    limitation["minTexelOffset"] = max(src1_limit["minTexelGatherOffset"], src2_limit["minTexelGatherOffset"])
    limitation["minUniformBufferOffsetAlignment"] = lcm(src1_limit["minUniformBufferOffsetAlignment"], src2_limit["minUniformBufferOffsetAlignment"])
    limitation["mipmapPrecisionBits"] = min(src1_limit["mipmapPrecisionBits"], src2_limit["mipmapPrecisionBits"])
    limitation["nonCoherentAtomSize"] = max(src1_limit["nonCoherentAtomSize"], src2_limit["nonCoherentAtomSize"])
    limitation["optimalBufferCopyOffsetAlignment"] = lcm(src1_limit["optimalBufferCopyOffsetAlignment"], src2_limit["optimalBufferCopyOffsetAlignment"])
    limitation["optimalBufferCopyRowPitchAlignment"] = lcm(src1_limit["optimalBufferCopyRowPitchAlignment"], src2_limit["optimalBufferCopyRowPitchAlignment"])
    limitation["pointSizeGranularity"] = max(src1_limit["pointSizeGranularity"], src2_limit["pointSizeGranularity"])
    limitation["pointSizeRange"] = src1_limit["pointSizeRange"]
    limitation["pointSizeRange"][0] = max(limitation["pointSizeRange"][0], src2_limit["pointSizeRange"][0])
    limitation["pointSizeRange"][1] = min(limitation["pointSizeRange"][1], src2_limit["pointSizeRange"][1])
    limitation["sampledImageColorSampleCounts"] = min(src1_limit["sampledImageColorSampleCounts"], src2_limit["sampledImageColorSampleCounts"])
    limitation["sampledImageDepthSampleCounts"] = min(src1_limit["sampledImageDepthSampleCounts"], src2_limit["sampledImageDepthSampleCounts"])
    limitation["sampledImageIntegerSampleCounts"] = min(src1_limit["sampledImageIntegerSampleCounts"], src2_limit["sampledImageIntegerSampleCounts"])
    limitation["sampledImageStencilSampleCounts"] = min(src1_limit["sampledImageStencilSampleCounts"], src2_limit["sampledImageStencilSampleCounts"])
    limitation["sparseAddressSpaceSize"] = min(int(src1_limit["sparseAddressSpaceSize"]), int(src2_limit["sparseAddressSpaceSize"]))
    limitation["standardSampleLocations"] = src1_limit["standardSampleLocations"] and src2_limit["standardSampleLocations"]
    limitation["storageImageSampleCounts"] = min(src1_limit["storageImageSampleCounts"], src2_limit["storageImageSampleCounts"])
    limitation["strictLines"] = src1_limit["strictLines"] and src2_limit["strictLines"]
    limitation["subPixelInterpolationOffsetBits"] = min(src1_limit["subPixelInterpolationOffsetBits"], src2_limit["subPixelInterpolationOffsetBits"])
    limitation["subPixelPrecisionBits"] = min(src1_limit["subPixelPrecisionBits"], src2_limit["subPixelPrecisionBits"])
    limitation["subTexelPrecisionBits"] = min(src1_limit["subTexelPrecisionBits"], src2_limit["subTexelPrecisionBits"])
    limitation["timestampComputeAndGraphics"] = src1_limit["timestampComputeAndGraphics"] and src2_limit["timestampComputeAndGraphics"]
    limitation["timestampPeriod"] = max(src1_limit["timestampPeriod"], src2_limit["timestampPeriod"])
    limitation["viewportBoundsRange"] = src1_limit["viewportBoundsRange"]
    limitation["viewportBoundsRange"][0] = max(limitation["viewportBoundsRange"][0], src2_limit["viewportBoundsRange"][0])
    limitation["viewportBoundsRange"][1] = min(limitation["viewportBoundsRange"][1], src2_limit["viewportBoundsRange"][1])
    limitation["viewportSubPixelBits"] = min(src1_limit["viewportSubPixelBits"], src2_limit["viewportSubPixelBits"])

    dst["limits"] = limitation
    dst["pipelineCacheUUID"] = src1["pipelineCacheUUID"]
    dst["sparseProperties"] = {}
    dst["sparseProperties"]["residencyAlignedMipSize"] = src1["sparseProperties"]["residencyAlignedMipSize"] and src2["sparseProperties"]["residencyAlignedMipSize"]
    dst["sparseProperties"]["residencyNonResidentStrict"] = src1["sparseProperties"]["residencyNonResidentStrict"] and src2["sparseProperties"]["residencyNonResidentStrict"]
    dst["sparseProperties"]["residencyStandard2DBlockShape"] = src1["sparseProperties"]["residencyStandard2DBlockShape"] and src2["sparseProperties"]["residencyStandard2DBlockShape"]
    dst["sparseProperties"]["residencyStandard2DMultisampleBlockShape"] = src1["sparseProperties"]["residencyStandard2DMultisampleBlockShape"] and src2["sparseProperties"]["residencyStandard2DMultisampleBlockShape"]
    dst["sparseProperties"]["residencyStandard3DBlockShape"] = src1["sparseProperties"]["residencyStandard3DBlockShape"] and src2["sparseProperties"]["residencyStandard3DBlockShape"]

    dst["subgroupProperties"] = {}
    dst["subgroupProperties"]["quadOperationsInAllStages"] = src1["subgroupProperties"]["quadOperationsInAllStages"] and src2["subgroupProperties"]["quadOperationsInAllStages"]
    dst["subgroupProperties"]["subgroupSize"] = min(src1["subgroupProperties"]["subgroupSize"], src2["subgroupProperties"]["subgroupSize"])
    dst["subgroupProperties"]["supportedOperations"] = src1["subgroupProperties"]["supportedOperations"] & src2["subgroupProperties"]["supportedOperations"]
    dst["subgroupProperties"]["supportedStages"] = src1["subgroupProperties"]["supportedStages"] & src2["subgroupProperties"]["supportedStages"]

    dst["vendorID"] = src1["vendorID"]
    return dst

def merge_extension_array(src1, src2):
    dst = []
    for item1 in src1:
        for item2 in src2:
            if item1["extensionName"] == item2["extensionName"]:
                dst_item = {}
                dst_item["extensionName"] = item1["extensionName"]
                dst_item["specVersion"] = min(item1["specVersion"], item2["specVersion"])
                dst.append(dst_item)
    return dst

def merge_queue_family_prop(src1, src2):
    dst = []
    array_len = min(len(src1), len(src2))
    for i in range(array_len):
        dst_item = src1[i]
        dst_item["minImageTransferGranularity"]["depth"] = max(dst_item["minImageTransferGranularity"]["depth"], src2[i]["minImageTransferGranularity"]["depth"])
        dst_item["minImageTransferGranularity"]["height"] = max(dst_item["minImageTransferGranularity"]["height"], src2[i]["minImageTransferGranularity"]["height"])
        dst_item["minImageTransferGranularity"]["width"] = max(dst_item["minImageTransferGranularity"]["width"], src2[i]["minImageTransferGranularity"]["width"])
        dst_item["queueCount"] = min(dst_item["queueCount"], src2[i]["queueCount"])
        dst_item["queueFlags"] = dst_item["queueFlags"] & src2[i]["queueFlags"]
        dst_item["timestampValidBits"] = min(dst_item["timestampValidBits"], src2[i]["timestampValidBits"])
        dst.append(dst_item)
    return dst

def merge_format_prop(src1, src2):
    dst = []
    for item1 in src1:
        for item2 in src2:
            if item1["formatID"] == item2["formatID"]:
                dst_item = {}
                dst_item["formatID"] = item1["formatID"]
                dst_item["linearTilingFeatures"] = item1["linearTilingFeatures"] & item2["linearTilingFeatures"]
                dst_item["optimalTilingFeatures"] = item1["optimalTilingFeatures"] & item2["optimalTilingFeatures"]
                dst_item["bufferFeatures"] = item1["bufferFeatures"] & item2["bufferFeatures"]
                dst.append(dst_item)
    return dst

def merge(src1, src2):
    dst = {}
    if (src1["$schema"] != src2["$schema"]):
        print("Both profiles are not in same schema !")
        return dst

    if (src1["$schema"] != "https://schema.khronos.org/vulkan/devsim_1_0_0.json#"):
        print("Wrong profile 1 schema " + src1["$schema"])

    if (src2["$schema"] != "https://schema.khronos.org/vulkan/devsim_1_0_0.json#"):
        print("Wrong profile 2 schema " + src2["$schema"])

    dst["$schema"] = src1["$schema"]
    dst["comments"] = { "info": "Generated by gpu_profile_merger.py", "desc": "" }
    dst["environment"] = src1["environment"]
    dst["extended"] = {}
    dst["extended"]["devicefeatures2"] = merge_dev_feature2(src1["extended"]["devicefeatures2"], src2["extended"]["devicefeatures2"])
    dst["extended"]["deviceproperties2"] = merge_dev_properties2(src1["extended"]["deviceproperties2"], src2["extended"]["deviceproperties2"])
    dst["VkPhysicalDeviceFeatures"] = merge_pdev_features(src1["VkPhysicalDeviceFeatures"], src2["VkPhysicalDeviceFeatures"])
    dst["VkPhysicalDeviceProperties"] = merge_pdev_properties(src1["VkPhysicalDeviceProperties"], src2["VkPhysicalDeviceProperties"])
    dst["ArrayOfVkExtensionProperties"] = merge_extension_array(src1["ArrayOfVkExtensionProperties"], src2["ArrayOfVkExtensionProperties"])
    dst["ArrayOfVkLayerProperties"] = []
    dst["ArrayOfVkQueueFamilyProperties"] = merge_queue_family_prop(src1["ArrayOfVkQueueFamilyProperties"], src2["ArrayOfVkQueueFamilyProperties"])
    dst["ArrayOfVkFormatProperties"] = merge_format_prop(src1["ArrayOfVkFormatProperties"], src2["ArrayOfVkFormatProperties"])
    return dst

def main():
    description = ('A simple script for merging two gpu profile json files.')
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument('-src1', help='Input profile json 1.')
    parser.add_argument('-src2', help='Input profile json 2.')
    parser.add_argument('-dst', help='Output profile json.')
    options = parser.parse_args()

    in_src1 = open(options.src1, 'r')
    in_src2 = open(options.src2, 'r')
    out_dst = open(options.dst, 'w')

    src1_json_obj = json.load(in_src1)
    src2_json_obj = json.load(in_src2)

    dst_json_obj = merge(src1_json_obj, src2_json_obj)

    json.dump(dst_json_obj, out_dst, indent=4)

    print(dst_json_obj["$schema"])


if __name__ == '__main__':
    try:
        main()
    except BrokenPipeError as exc:
        sys.exit(exc.errno)
