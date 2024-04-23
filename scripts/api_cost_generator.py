#!/usr/bin/python3 -i
#
# Copyright (c) 2016-2019, 2019 ARM Corporation
#
# Author: Parker
#
# The API cost layer works by passing custom format strings to the ApiCostGenerator. These format
# strings are C++ code, with 3-ish exceptions:
#   * Anything beginning with @ will be expanded by the ApiCostGenerator. These are used to allow
#       iteration over various items within the Vulkan spec, such as functions, enums, etc.
#   * Anything surrounded by { and } will be substituted when the ApiCostGenerator expands the @
#       directives. This gives a way to get things like data types or names for anything that can
#       be iterated over in an @ directive.
#   * Curly braces must be doubled like {{ for a single curly brace to appear in the output code.
#
# The API cost uses separate format strings for each output file, but passes them to a common
# generator. This allows greater flexibility, as changing the output codegen means just changing
# the corresponding format string.
#
# Currently, the API cost layer generates the following files from the following strings:
#   * api_cost.cpp: APICOST_CODEGEN - Provides all entrypoints for functions and dispatches the calls
#       to the proper back end
#

import os,re,sys,string
import xml.etree.ElementTree as etree
import generator as gen
from generator import *
from collections import namedtuple
from common_codegen import *

APICOST_CODEGEN = """
/*
 * (C) COPYRIGHT 2019 ARM Limited
 * ALL RIGHTS RESERVED
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     LICENSE-2.0" target="_blank" rel="nofollow">http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "api_cost.h"

//============================= API EntryPoints =============================//

// Specifically implemented functions

@foreach function where('{funcName}' == 'vkCreateInstance')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    // Get the function pointer
    VkLayerInstanceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo != 0);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    assert(fpGetInstanceProcAddr != 0);
    PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance) fpGetInstanceProcAddr(NULL, "vkCreateInstance");
    if(fpCreateInstance == NULL) {{
        return VK_ERROR_INITIALIZATION_FAILED;
    }}

    // Call the function and create the dispatch table
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    uint64_t start = ApiCostInstance::current().getCurrentTime();
    {funcReturn} result = fpCreateInstance({funcNamedParams});
    uint64_t elapse = ApiCostInstance::current().getCurrentTime() - start;

    // Output the API cost
    ApiCostInstance::current().writeFile("{funcName}",elapse);

    if(result == VK_SUCCESS) {{
        initInstanceTable(*pInstance, fpGetInstanceProcAddr);
    }}
    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkDestroyInstance')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    // Destroy the dispatch table
    dispatch_key key = get_dispatch_key({funcDispatchParam});
    uint64_t start = ApiCostInstance::current().getCurrentTime();
    instance_dispatch_table({funcDispatchParam})->DestroyInstance({funcNamedParams});
    uint64_t elapse = ApiCostInstance::current().getCurrentTime() - start;
    destroy_instance_dispatch_table(key);

    // Output the API cost
    ApiCostInstance::current().writeFile("{funcName}",elapse);
}}
@end function

@foreach function where('{funcName}' == 'vkCreateDevice')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    // Get the function pointer
    VkLayerDeviceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo != 0);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice) fpGetInstanceProcAddr(NULL, "vkCreateDevice");
    if(fpCreateDevice == nullptr) {{
        return VK_ERROR_INITIALIZATION_FAILED;
    }}

    // Call the function and create the dispatch table
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
    uint64_t start = ApiCostInstance::current().getCurrentTime();
    {funcReturn} result = fpCreateDevice({funcNamedParams});
    uint64_t elapse = ApiCostInstance::current().getCurrentTime() - start;

    // Output the API cost
    ApiCostInstance::current().writeFile("{funcName}",elapse);

    if(result == VK_SUCCESS) {{
        initDeviceTable(*pDevice, fpGetDeviceProcAddr);
    }}

    return result;
}}
@end function

@foreach function where('{funcName}' == 'vkDestroyDevice')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    // Destroy the dispatch table
    dispatch_key key = get_dispatch_key({funcDispatchParam});
    uint64_t start = ApiCostInstance::current().getCurrentTime();
    device_dispatch_table({funcDispatchParam})->DestroyDevice({funcNamedParams});
    uint64_t elapse = ApiCostInstance::current().getCurrentTime() - start;
    destroy_device_dispatch_table(key);

    // Output the API cost
    ApiCostInstance::current().writeFile("{funcName}",elapse);
}}
@end function

@foreach function where('{funcName}' == 'vkEnumerateInstanceExtensionProperties')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    return util_GetExtensionProperties(0, NULL, pPropertyCount, pProperties);
}}
@end function

@foreach function where('{funcName}' == 'vkEnumerateInstanceLayerProperties')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    static const VkLayerProperties layerProperties[] = {{
        {{
            "VK_LAYER_ARM_api_cost",
            VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION), // specVersion
            VK_MAKE_VERSION(0, 2, 0), // implementationVersion
            "layer: api_cost",
        }}
    }};

    return util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);
}}
@end function

@foreach function where('{funcName}' == 'vkEnumerateDeviceLayerProperties')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    static const VkLayerProperties layerProperties[] = {{
        {{
            "VK_LAYER_ARM_api_cost",
            VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION),
            VK_MAKE_VERSION(0, 2, 0),
            "layer: api_cost",
        }}
    }};

    return util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);
}}
@end function

@foreach function where('{funcName}' == 'vkQueuePresentKHR')
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    uint64_t start = ApiCostInstance::current().getCurrentTime();
    {funcReturn} result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    uint64_t elapse = ApiCostInstance::current().getCurrentTime() - start;

    // Output the API cost
    ApiCostInstance::current().writeFile("{funcName}",elapse);
    ApiCostInstance::current().nextFrame();
    return result;
}}
@end function

// Autogen instance functions

@foreach function where('{funcDispatchType}' == 'instance' and '{funcReturn}' != 'void' and '{funcName}' not in ['vkCreateInstance', 'vkDestroyInstance', 'vkCreateDevice', 'vkGetInstanceProcAddr', 'vkEnumerateDeviceExtensionProperties', 'vkEnumerateDeviceLayerProperties'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    uint64_t start = ApiCostInstance::current().getCurrentTime();
    {funcReturn} result = instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    uint64_t elapse = ApiCostInstance::current().getCurrentTime() - start;

    // Output the API cost
    ApiCostInstance::current().writeFile("{funcName}",elapse);
    return result;
}}
@end function

@foreach function where('{funcDispatchType}' == 'instance' and '{funcReturn}' == 'void' and '{funcName}' not in ['vkCreateInstance', 'vkDestroyInstance', 'vkCreateDevice', 'vkGetInstanceProcAddr', 'vkEnumerateDeviceExtensionProperties', 'vkEnumerateDeviceLayerProperties'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    uint64_t start = ApiCostInstance::current().getCurrentTime();
    instance_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    uint64_t elapse = ApiCostInstance::current().getCurrentTime() - start;

    // Output the API cost
    ApiCostInstance::current().writeFile("{funcName}",elapse);
}}
@end function

// Autogen device functions

@foreach function where('{funcDispatchType}' == 'device' and '{funcReturn}' != 'void' and '{funcName}' not in ['vkDestroyDevice', 'vkEnumerateInstanceExtensionProperties', 'vkEnumerateInstanceLayerProperties', 'vkQueuePresentKHR', 'vkGetDeviceProcAddr'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    uint64_t start = ApiCostInstance::current().getCurrentTime();
    {funcReturn} result = device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    uint64_t elapse = ApiCostInstance::current().getCurrentTime() - start;

    // Output the API cost
    ApiCostInstance::current().writeFile("{funcName}",elapse);
    return result;
}}
@end function

@foreach function where('{funcDispatchType}' == 'device' and '{funcReturn}' == 'void' and '{funcName}' not in ['vkDestroyDevice', 'vkEnumerateInstanceExtensionProperties', 'vkEnumerateInstanceLayerProperties', 'vkGetDeviceProcAddr'])
VK_LAYER_EXPORT VKAPI_ATTR {funcReturn} VKAPI_CALL {funcName}({funcTypedParams})
{{
    uint64_t start = ApiCostInstance::current().getCurrentTime();
    device_dispatch_table({funcDispatchParam})->{funcShortName}({funcNamedParams});
    uint64_t elapse = ApiCostInstance::current().getCurrentTime() - start;

    // Output the API cost
    ApiCostInstance::current().writeFile("{funcName}",elapse);
}}
@end function

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName)
{{
    @foreach function where('{funcType}' == 'instance'  and '{funcName}' not in [ 'vkEnumerateDeviceExtensionProperties' ])
    if(strcmp(pName, "{funcName}") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>({funcName});
    @end function

    if(instance_dispatch_table(instance)->GetInstanceProcAddr == NULL)
        return NULL;
    return instance_dispatch_table(instance)->GetInstanceProcAddr(instance, pName);
}}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName)
{{
    @foreach function where('{funcType}' == 'device')
    if(strcmp(pName, "{funcName}") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>({funcName});
    @end function

    if(device_dispatch_table(device)->GetDeviceProcAddr == NULL)
        return NULL;
    return device_dispatch_table(device)->GetDeviceProcAddr(device, pName);
}}
"""



POINTER_TYPES = ['void', 'xcb_connection_t', 'Display', 'SECURITY_ATTRIBUTES', 'ANativeWindow', 'AHardwareBuffer']

DEFINE_TYPES = ['ANativeWindow', 'AHardwareBuffer']

TRACKED_STATE = {
    'vkAllocateCommandBuffers':
        'if(result == VK_SUCCESS)\n' +
            'ApiDumpInstance::current().addCmdBuffers(\n' +
                'device,\n' +
                'pAllocateInfo->commandPool,\n' +
                'std::vector<VkCommandBuffer>(pCommandBuffers, pCommandBuffers + pAllocateInfo->commandBufferCount),\n' +
                'pAllocateInfo->level\n'
            ');',
    'vkDestroyCommandPool':
        'ApiDumpInstance::current().eraseCmdBufferPool(device, commandPool);'
    ,
    'vkFreeCommandBuffers':
        'ApiDumpInstance::current().eraseCmdBuffers(device, commandPool, std::vector<VkCommandBuffer>(pCommandBuffers, pCommandBuffers + commandBufferCount));'
    ,
}

INHERITED_STATE = {
    'VkPipelineViewportStateCreateInfo': {
        'VkGraphicsPipelineCreateInfo': [
            {
                'name': 'is_dynamic_viewport',
                'type': 'bool',
                'expr':
                    'object.pDynamicState && ' +
                    'std::count(' +
                        'object.pDynamicState->pDynamicStates, ' +
                        'object.pDynamicState->pDynamicStates + object.pDynamicState->dynamicStateCount, ' +
                        'VK_DYNAMIC_STATE_VIEWPORT' +
                    ')',
             },
             {
                'name':'is_dynamic_scissor',
                'type': 'bool',
                'expr':
                    'object.pDynamicState && ' +
                    'std::count(' +
                        'object.pDynamicState->pDynamicStates, ' +
                        'object.pDynamicState->pDynamicStates + object.pDynamicState->dynamicStateCount, ' +
                        'VK_DYNAMIC_STATE_SCISSOR' +
                    ')',
            },
        ],
    },
    'VkCommandBufferBeginInfo': {
        'vkBeginCommandBuffer': [
            {
                'name': 'cmd_buffer',
                'type': 'VkCommandBuffer',
                'expr': 'commandBuffer',
            },
        ],
    },
}

VALIDITY_CHECKS = {
    'VkBufferCreateInfo': {
        'pQueueFamilyIndices': 'object.sharingMode == VK_SHARING_MODE_CONCURRENT',
    },
    'VkCommandBufferBeginInfo': {
        # Tracked state ApiDumpInstance, and inherited cmd_buffer
        'pInheritanceInfo': 'ApiDumpInstance::current().getCmdBufferLevel(cmd_buffer) == VK_COMMAND_BUFFER_LEVEL_SECONDARY',
    },
    'VkDescriptorSetLayoutBinding': {
        'pImmutableSamplers':
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) || ' +
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)',
    },
    'VkImageCreateInfo': {
        'pQueueFamilyIndices': 'object.sharingMode == VK_SHARING_MODE_CONCURRENT',
    },
    'VkPipelineViewportStateCreateInfo': {
        'pViewports': '!is_dynamic_viewport', # Inherited state variable is_dynamic_viewport
        'pScissors': '!is_dynamic_scissor',   # Inherited state variable is_dynamic_scissor
    },
    'VkSwapchainCreateInfoKHR': {
        'pQueueFamilyIndices': 'object.imageSharingMode == VK_SHARING_MODE_CONCURRENT',
    },
    'VkWriteDescriptorSet': {
        'pImageInfo':
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) || ' +
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) || ' +
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) || ' +
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) || ' +
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)',
        'pBufferInfo':
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) || ' +
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) || ' +
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) || ' +
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)',
        'pTexelBufferView':
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) || ' +
            '(object.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)',
    },
}

class ApiCostGeneratorOptions(GeneratorOptions):

    def __init__(self,
                 input = None,
                 filename = None,
                 directory = '.',
                 apiname = None,
                 profile = None,
                 versions = '.*',
                 emitversions = '.*',
                 defaultExtensions = None,
                 addExtensions = None,
                 removeExtensions = None,
                 emitExtensions = None,
                 sortProcedure = None,
                 prefixText = "",
                 genFuncPointers = True,
                 protectFile = True,
                 protectFeature = True,
                 protectProto = None,
                 protectProtoStr = None,
                 apicall = '',
                 apientry = '',
                 apientryp = '',
                 indentFuncProto = True,
                 indentFuncPointer = False,
                 alignFuncParam = 0,
                 expandEnumerants = True,
                 ):
        GeneratorOptions.__init__(self,
                                  filename = filename,
                                  directory = directory,
                                  apiname = apiname,
                                  profile = profile,
                                  versions = versions,
                                  emitversions = emitversions,
                                  defaultExtensions = defaultExtensions,
                                  addExtensions = addExtensions,
                                  removeExtensions = removeExtensions,
                                  emitExtensions = emitExtensions,
                                  sortProcedure = sortProcedure)
        self.input           = input
        self.prefixText      = prefixText
        self.genFuncPointers = genFuncPointers
        self.protectFile     = protectFile
        self.protectFeature  = protectFeature
        self.protectProto    = protectProto
        self.protectProtoStr = protectProtoStr
        self.apicall         = apicall
        self.apientry        = apientry
        self.apientryp       = apientryp
        self.indentFuncProto = indentFuncProto
        self.indentFuncPointer = indentFuncPointer
        self.alignFuncParam  = alignFuncParam


class ApiCostOutputGenerator(OutputGenerator):

    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout,
                 registryFile = None):
        gen.OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        self.format = None

        self.constants = {}
        self.extensions = set()
        self.extFuncs = {}
        self.extTypes = {}
        self.includes = {}

        self.basetypes = set()
        self.bitmasks = set()
        self.enums = set()
        self.externalTypes = set()
        self.flags = set()
        self.funcPointers = set()
        self.functions = set()
        self.handles = set()
        self.structs = set()
        self.unions = set()
        self.aliases = {}

        self.registryFile = registryFile

        # Used to track duplications (thanks 1.1 spec)
        self.trackedTypes = []

    def beginFile(self, genOpts):
        gen.OutputGenerator.beginFile(self, genOpts)
        self.format = genOpts.input

        if self.registryFile is not None:
            root = xml.etree.ElementTree.parse(self.registryFile)
        else:
            root = self.registry.reg

        for node in root.find('extensions').findall('extension'):
            ext = VulkanExtension(node)
            self.extensions.add(ext)
            for item in ext.vktypes:
                self.extTypes[item] = ext
            for item in ext.vkfuncs:
                self.extFuncs[item] = ext

        for node in self.registry.reg.findall('enums'):
            if node.get('name') == 'API Constants':
                for item in node.findall('enum'):
                    self.constants[item.get('name')] = item.get('value')

        for node in self.registry.reg.find('types').findall('type'):
            if node.get('category') == 'include':
                self.includes[node.get('name')] = ''.join(node.itertext())


    def endFile(self):
        # Find all of the extensions that use the system types
        self.sysTypes = set()
        for node in self.registry.reg.find('types').findall('type'):
            if node.get('category') is None and node.get('requires') in self.includes and node.get('requires') != 'vk_platform' or \
                (node.find('name') is not None and node.find('name').text in DEFINE_TYPES): #Handle system types that are '#define'd in spec
                for extension in self.extTypes:
                    for structName in self.extTypes[extension].vktypes:
                        for struct in self.structs:
                            if struct.name == structName:
                                for member in struct.members:
                                    if node.get('name') == member.baseType or node.get('name') + '*' == member.baseType:
                                        sysType = VulkanSystemType(node.get('name'), self.extTypes[structName])
                                        if sysType not in self.sysTypes:
                                            self.sysTypes.add(sysType)
                    for funcName in self.extTypes[extension].vkfuncs:
                        for func in self.functions:
                            if func.name == funcName:
                                for param in func.parameters:
                                    if node.get('name') == param.baseType or node.get('name') + '*' == param.baseType:
                                        sysType = VulkanSystemType(node.get('name'), self.extFuncs[funcName])
                                        if sysType not in self.sysTypes:
                                            self.sysTypes.add(sysType)

        # Find every @foreach, @if, and @end
        forIter = re.finditer('(^\\s*\\@foreach\\s+[a-z]+(\\s+where\\(.*\\))?\\s*^)|(\\@foreach [a-z]+(\\s+where\\(.*\\))?\\b)', self.format, flags=re.MULTILINE)
        ifIter = re.finditer('(^\\s*\\@if\\(.*\\)\\s*^)|(\\@if\\(.*\\))', self.format, flags=re.MULTILINE)
        endIter = re.finditer('(^\\s*\\@end\\s+[a-z]+\\s*^)|(\\@end [a-z]+\\b)', self.format, flags=re.MULTILINE)
        try:
            nextFor = next(forIter)
        except StopIteration:
            nextFor = None
        try:
            nextIf = next(ifIter)
        except StopIteration:
            nextIf = None
        try:
            nextEnd = next(endIter)
        except StopIteration:
            nextEnd = None

        # Match the beginnings to the ends
        loops = []
        unassignedControls = []
        depth = 0
        while nextFor is not None or nextFor is not None or nextEnd is not None:
            # If this is a @foreach
            if nextFor is not None and ((nextIf is None or nextFor.start() < nextIf.start()) and nextFor.start() < nextEnd.start()):
                depth += 1
                forType = re.search('(?<=\\s)[a-z]+', self.format[nextFor.start():nextFor.end()])
                text = self.format[forType.start()+nextFor.start():forType.end()+nextFor.start()]
                whereMatch = re.search('(?<=where\\().*(?=\\))', self.format[nextFor.start():nextFor.end()])
                condition = None if whereMatch is None else self.format[whereMatch.start()+nextFor.start():whereMatch.end()+nextFor.start()]
                unassignedControls.append((nextFor.start(), nextFor.end(), text, condition))

                try:
                    nextFor = next(forIter)
                except StopIteration:
                    nextFor = None

            # If this is an @if
            elif nextIf is not None and nextIf.start() < nextEnd.start():
                depth += 1
                condMatch = re.search('(?<=if\\().*(?=\\))', self.format[nextIf.start():nextIf.end()])
                condition = None if condMatch is None else self.format[condMatch.start()+nextIf.start():condMatch.end()+nextIf.start()]
                unassignedControls.append((nextIf.start(), nextIf.end(), 'if', condition))

                try:
                    nextIf = next(ifIter)
                except StopIteration:
                    nextIf = None

            # Else this is an @end
            else:
                depth -= 1
                endType = re.search('(?<=\\s)[a-z]+', self.format[nextEnd.start():nextEnd.end()])
                text = self.format[endType.start()+nextEnd.start():endType.end()+nextEnd.start()]

                start = unassignedControls.pop(-1)
                assert(start[2] == text)

                item = Control(self.format, start[0:2], (nextEnd.start(), nextEnd.end()), text, start[3])
                if len(loops) < 1 or depth < loops[-1][0]:
                    while len(loops) > 0 and depth < loops[-1][0]:
                        item.children.insert(0, loops.pop(-1)[1])
                    loops.append((depth, item))
                else:
                    loops.append((depth, item))

                try:
                    nextEnd = next(endIter)
                except StopIteration:
                    nextEnd = None

        # Expand each loop into its full form
        lastIndex = 0
        for _, loop in loops:
            gen.write(self.format[lastIndex:loop.startPos[0]].format(**{}), file=self.outFile)
            gen.write(self.expand(loop), file=self.outFile)
            lastIndex = loop.endPos[1]
        gen.write(self.format[lastIndex:-1].format(**{}), file=self.outFile)

        gen.OutputGenerator.endFile(self)

    def genCmd(self, cmd, name, alias):
        gen.OutputGenerator.genCmd(self, cmd, name, alias)

        if name == "vkApiCostOutputGeneratorEnumerateInstanceVersion": return # TODO: Create exclusion list or metadata to indicate this

        self.functions.add(VulkanFunction(cmd.elem, self.constants, self.aliases, self.extFuncs))

    # These are actually constants
    def genEnum(self, enuminfo, name, alias):
        gen.OutputGenerator.genEnum(self, enuminfo, name, alias)

    # These are actually enums
    def genGroup(self, groupinfo, groupName, alias):
        gen.OutputGenerator.genGroup(self, groupinfo, groupName, alias)

        if alias is not None:
            trackedName = alias
        else:
            trackedName = groupName
        if trackedName in self.trackedTypes:
            return
        self.trackedTypes.append(trackedName)

        if groupinfo.elem.get('type') == 'bitmask':
            self.bitmasks.add(VulkanBitmask(groupinfo.elem, self.extensions))
        elif groupinfo.elem.get('type') == 'enum':
            self.enums.add(VulkanEnum(groupinfo.elem, self.extensions))

    def genType(self, typeinfo, name, alias):
        gen.OutputGenerator.genType(self, typeinfo, name, alias)

        if alias is not None:
            trackedName = alias
            if typeinfo.elem.get('category') == 'struct':
                self.aliases[name] = alias
        else:
            trackedName = name
        if trackedName in self.trackedTypes:
            return
        self.trackedTypes.append(trackedName)

        if typeinfo.elem.get('category') == 'struct':
            self.structs.add(VulkanStruct(typeinfo.elem, self.constants))
        elif typeinfo.elem.get('category') == 'basetype':
            self.basetypes.add(VulkanBasetype(typeinfo.elem))
        elif typeinfo.elem.get('category') is None and typeinfo.elem.get('requires') == 'vk_platform':
            self.externalTypes.add(VulkanExternalType(typeinfo.elem))
        elif typeinfo.elem.get('category') == 'handle':
            self.handles.add(VulkanHandle(typeinfo.elem))
        elif typeinfo.elem.get('category') == 'union':
            self.unions.add(VulkanUnion(typeinfo.elem, self.constants))
        elif typeinfo.elem.get('category') == 'bitmask':
            self.flags.add(VulkanFlags(typeinfo.elem))
        elif typeinfo.elem.get('category') == 'funcpointer':
            self.funcPointers.add(VulkanFunctionPointer(typeinfo.elem))

    def expand(self, loop, parents=[]):
        # Figure out what we're dealing with
        if loop.text == 'if':
            subjects = [ Control.IfDummy() ]
        elif loop.text == 'basetype':
            subjects = self.basetypes
        elif loop.text == 'bitmask':
            subjects = self.bitmasks
        elif loop.text == 'choice':
            subjects = self.findByType([VulkanUnion], parents).choices
        elif loop.text == 'enum':
            subjects = self.enums
        elif loop.text == 'extension':
            subjects = self.extensions
        elif loop.text == 'flag':
            subjects = self.flags
        elif loop.text == 'funcpointer':
            subjects = self.funcPointers
        elif loop.text == 'function':
            subjects = self.functions
        elif loop.text == 'handle':
            subjects = self.handles
        elif loop.text == 'option':
            subjects = self.findByType([VulkanEnum, VulkanBitmask], parents).options
        elif loop.text == 'member':
            subjects = self.findByType([VulkanStruct], parents).members
        elif loop.text == 'parameter':
            subjects = self.findByType([VulkanFunction], parents).parameters
        elif loop.text == 'struct':
            subjects = self.structs
        elif loop.text == 'systype':
            subjects = self.sysTypes
        elif loop.text == 'type':
            subjects = self.externalTypes
        elif loop.text == 'union':
            subjects = self.unions
        else:
            assert(False)

        # Generate the output string
        out = ''
        for item in subjects:

            # Merge the values and the parent values
            values = item.values().copy()
            for parent in parents:
                values.update(parent.values())

            # Check if the condition is met
            if loop.condition is not None:
                cond = eval(loop.condition.format(**values))
                assert(cond == True or cond == False)
                if not cond:
                    continue

            # Check if an ifdef is needed
            if item.name in self.extFuncs:
                ext = self.extFuncs[item.name]
            elif item.name in self.extTypes:
                ext = self.extTypes[item.name]
            elif item in self.sysTypes:
                ext = item.ext
            else:
                ext = None
            if ext is not None and ext.guard is not None:
                out += '#if defined({})\n'.format(ext.guard)

            # Format the string
            lastIndex = loop.startPos[1]
            for child in loop.children:
                out += loop.fullString[lastIndex:child.startPos[0]].format(**values)
                out += self.expand(child, parents=[item]+parents)
                lastIndex = child.endPos[1]
            out += loop.fullString[lastIndex:loop.endPos[0]].format(**values)

            # Close the ifdef
            if ext is not None and ext.guard is not None:
                out += '#endif // {}\n'.format(ext.guard)

        return out

    def findByType(self, types, objects):
        value = None
        for item in objects:
            for ty in types:
                if isinstance(item, ty):
                    value = item
                    break
        assert(value is not None)
        return value
