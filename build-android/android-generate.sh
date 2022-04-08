#!/bin/bash

# Copyright 2015 The Android Open Source Project
# Copyright (C) 2015 Valve Corporation
# Copyright (C) 2019 ARM Limited

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#      http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

dir=$(cd -P -- "$(dirname -- "$0")" && pwd -P)
cd $dir

VKTRACE_RELEASE_VERSION=`cat ${dir}/../vktrace_version`
VKTRACE_RELEASE_TYPE="dev unofficial"
VERSION="${VKTRACE_RELEASE_VERSION} ${VKTRACE_RELEASE_TYPE}"
BUILD_TYPE="debug"

while [[ $# -gt 0 ]]
do
    case $1 in
        --release)
            VERSION="${VKTRACE_RELEASE_VERSION}"
            shift 1
            ;;
        --buildtype)
            BUILD_TYPE=$2
            shift 2
            ;;
        *)
            # unknown option
            echo Unknown option: $1
            echo "Supported parameters are:"
            echo "    --release (optional)"
            echo "    --buildtype [debug|release] (optional, default is debug)"
            exit 1
            ;;
    esac
done

BUILD_TYPE_LIST="debug release"
if ! [[ ${BUILD_TYPE_LIST} =~ (^|[[:space:]])${BUILD_TYPE}($|[[:space:]]) ]]; then
    echo "Unsupported build type ${BUILD_TYPE}!"
    exit 1
fi

if [ $BUILD_TYPE == "release" ]; then
    DBG_FLAG=
else
    DBG_FLAG=-D_DEBUG
fi

sed -i "s/versionName=\"1.0\"/versionName=\"${VERSION}\"/" ${dir}/vkreplay/AndroidManifest.xml
echo "APP_CFLAGS += ${DBG_FLAG} -DVKTRACE_VERSION=\"\\\"${VERSION}\\\"\"" >> ${dir}/jni/Application.mk
echo "APP_CPPFLAGS += ${DBG_FLAG} -DVKTRACE_VERSION=\"\\\"${VERSION}\\\"\"" >> ${dir}/jni/Application.mk

rm -rf generated
mkdir -p generated/include generated/common

LVL_BASE=$dir/third_party/Vulkan-ValidationLayers
LVL_SCRIPTS=${LVL_BASE}/scripts
LVL_GENERATED=${LVL_BASE}/layers/generated
VT_SCRIPTS=../../../scripts
REGISTRY_PATH=$dir/third_party/Vulkan-Headers/registry
REGISTRY=${REGISTRY_PATH}/vk.xml

# Check for python 3.6 or greater
PYTHON_EXECUTABLE=python3
PYTHON_MINOR_VERSION=$($PYTHON_EXECUTABLE --version | cut -d. -f2)
if [ $PYTHON_MINOR_VERSION -lt "6" ]; then
    for MINOR_TEST in {6..20}
    do
        TEST_EXECUTABLE=$PYTHON_EXECUTABLE.$MINOR_TEST
        if which $TEST_EXECUTABLE; then
            PYTHON_EXECUTABLE=$TEST_EXECUTABLE
            break
        fi
    done
fi
echo "Using python: $(which $PYTHON_EXECUTABLE)"

( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vk_safe_struct.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vk_safe_struct.cpp )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vk_enum_string_helper.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vk_object_types.h -removeExtensions VK_KHR_video_queue -removeExtensions VK_KHR_video_decode_queue -removeExtensions VK_KHR_video_encode_queue -removeExtensions VK_EXT_video_decode_h264 -removeExtensions VK_EXT_video_decode_h265 -removeExtensions VK_EXT_video_encode_h264 -removeExtensions VK_EXT_video_encode_h265)
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vk_dispatch_table_helper.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} parameter_validation.cpp )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vk_layer_dispatch_table.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vk_extension_helper.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vk_typemap_helper.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} object_tracker.cpp )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} object_tracker.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} layer_chassis_dispatch.cpp )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} layer_chassis_dispatch.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} chassis.cpp )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/lvl_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} chassis.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${LVL_SCRIPTS}/external_revision_generator.py --git_dir ../../third_party/shaderc/third_party/spirv-tools -s SPIRV_TOOLS_COMMIT_ID -o spirv_tools_commit_id.h )

# layer factory
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} layer_factory.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} layer_factory.cpp )
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vlf_makefile_generator.py ../../../layer_factory ${REGISTRY_PATH}/../include)

# apidump
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} api_dump.cpp -removeExtensions VK_KHR_video_queue -removeExtensions VK_KHR_video_decode_queue -removeExtensions VK_KHR_video_encode_queue -removeExtensions VK_EXT_video_decode_h264 -removeExtensions VK_EXT_video_decode_h265 -removeExtensions VK_EXT_video_encode_h264 -removeExtensions VK_EXT_video_encode_h265)
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} api_dump_text.h -removeExtensions VK_KHR_video_queue -removeExtensions VK_KHR_video_decode_queue -removeExtensions VK_KHR_video_encode_queue -removeExtensions VK_EXT_video_decode_h264 -removeExtensions VK_EXT_video_decode_h265 -removeExtensions VK_EXT_video_encode_h264 -removeExtensions VK_EXT_video_encode_h265)
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} api_dump_html.h -removeExtensions VK_KHR_video_queue -removeExtensions VK_KHR_video_decode_queue -removeExtensions VK_KHR_video_encode_queue -removeExtensions VK_EXT_video_decode_h264 -removeExtensions VK_EXT_video_decode_h265 -removeExtensions VK_EXT_video_encode_h264 -removeExtensions VK_EXT_video_encode_h265)
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} api_dump_json.h -removeExtensions VK_KHR_video_queue -removeExtensions VK_KHR_video_decode_queue -removeExtensions VK_KHR_video_encode_queue -removeExtensions VK_EXT_video_decode_h264 -removeExtensions VK_EXT_video_decode_h265 -removeExtensions VK_EXT_video_encode_h264 -removeExtensions VK_EXT_video_encode_h265)

# apicost
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} api_cost.cpp )

# systrace
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vktrace_systrace.cpp )

# emptydriver
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vktrace_emptydriver.cpp )

# vktrace
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vktrace_vk_vk.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vktrace_vk_vk.cpp )
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vktrace_vk_vk_packets.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vktrace_vk_packet_id.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vk_struct_size_helper.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vk_struct_size_helper.c )

# vkreplay
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vkreplay_vk_func_ptrs.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vkreplay_vk_replay_gen.cpp )
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vkreplay_vk_objmapper.h )
( cd generated/include; ${PYTHON_EXECUTABLE} ${VT_SCRIPTS}/vt_genvk.py -registry ${REGISTRY} -scripts ${REGISTRY_PATH} vk_struct_member.h )

( pushd ${LVL_BASE}/build-android; rm -rf generated; mkdir -p generated/include generated/common; popd )
( cd generated/include; cp -rf * ${LVL_BASE}/build-android/generated/include )
( cp ${LVL_BASE}/layers/generated/vk_validation_error_messages.h generated/include )

exit 0
