#!/bin/bash

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

UPDATE_EXTERNAL="false"
BUILD_TYPE="debug"
RELEASE_TYPE="dev"
TARGET="android"
RELEASE_TYPE_OPTION=""
BUILD_TYPE_OPTION=""
BINARY_SUFFIX=""
EXTRA_CMAKE_OPTION=""
NEED_PACKAGE="true"

print_help() {
    echo "Supported parameters are:"
    echo "    --target          <android|x86|x64|arm_32|arm_64> (optional, default is android)"
    echo "    --release-type    <dev|release>                   (optional, default is dev)"
    echo "    --build-type      <debug|release>                 (optional, default is debug)"
    echo "    --update-external <true|false>                    (optional, set to true to force update external directories, default is false)"
    echo "    --package         <true|false>                    (optional, set to true to package build outputs to vktrace_<TARGET>_<BUILD_TYPE>.tgz, default is true)"
}

while [[ $# -gt 0 ]]
do
    case $1 in
        --target)
            TARGET=$2
            shift 2
            ;;
        --release-type)
            RELEASE_TYPE=$2
            shift 2
            ;;
        --build-type)
            BUILD_TYPE=$2
            shift 2
            ;;
        --update-external)
            UPDATE_EXTERNAL=$2
            shift 2
            ;;
        --package)
            NEED_PACKAGE=$2
            shift 2
            ;;
        *)
            # unknown option
            echo Unknown option: $1
            print_help
            exit 1
            ;;
    esac
done

TARGET_LIST="android x86 x64 arm_32 arm_64"
if ! [[ ${TARGET_LIST} =~ (^|[[:space:]])${TARGET}($|[[:space:]]) ]]; then
    echo "Unsupported target option ${TARGET}!"
    exit 1
fi

RELEASE_TYPE_LIST="dev release"
if ! [[ ${RELEASE_TYPE_LIST} =~ (^|[[:space:]])${RELEASE_TYPE}($|[[:space:]]) ]]; then
    echo "Unsupported --release-type option ${RELEASE_TYPE}!"
    exit 1
fi

BUILD_TYPE_LIST="debug release"
if ! [[ ${BUILD_TYPE_LIST} =~ (^|[[:space:]])${BUILD_TYPE}($|[[:space:]]) ]]; then
    echo "Unsupported --build-type option ${BUILD_TYPE}!"
    exit 1
fi

BOOL_LIST="true false"
if ! [[ ${BOOL_LIST} =~ (^|[[:space:]])${UPDATE_EXTERNAL}($|[[:space:]]) ]]; then
    echo "Unsupported --update-external option ${UPDATE_EXTERNAL}!"
    exit 1
fi

if ! [[ ${BOOL_LIST} =~ (^|[[:space:]])${NEED_PACKAGE}($|[[:space:]]) ]]; then
    echo "Unsupported --package option ${NEED_PACKAGE}!"
    exit 1
fi

if [ ${TARGET} == "android" ]; then
    if [ ${RELEASE_TYPE} == "release" ]; then
        RELEASE_TYPE_OPTION="--release"
    fi
    if [ ${BUILD_TYPE} == "release" ]; then
        BUILD_TYPE_OPTION="--buildtype release"
    fi
    if [ ! -f ~/.android/debug.keystore ]; then
        keytool -genkey -v -keystore ~/.android/debug.keystore -storepass android -alias androiddebugkey -keypass android -dname "CN=Android Debug,O=Android,C=US"
    fi
    rm -rf ${dir}/build-android/generated
    rm -rf ${dir}/build-android/obj
    rm -rf ${dir}/build-android/libs
    rm -rf ${dir}/build-android/vkreplay/bin
    rm -rf ${dir}/build-android/vkreplay/libs
else
    if [ ${RELEASE_TYPE} == "release" ]; then
        RELEASE_TYPE_OPTION="-DVKTRACE_RELEASE=TRUE"
    fi
    BUILD_TYPE_OPTION="Debug"
    if [ ${BUILD_TYPE} == "release" ]; then
        BUILD_TYPE_OPTION="Release"
    fi

    if [ ${TARGET} == "x86" ]; then
        export ASFLAGS=--32
        export CFLAGS="-m32"
        export CXXFLAGS="-m32"
        export PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu
        BINARY_SUFFIX="32"
        EXTRA_CMAKE_OPTION="-DBUILD_X64=Off"
    elif [ ${TARGET} == "arm_32" ]; then
        BINARY_SUFFIX="32"
        EXTRA_CMAKE_OPTION="-DCMAKE_TOOLCHAIN_FILE=cmake/linux_arm.cmake"
    elif [ ${TARGET} == "arm_64" ]; then
        EXTRA_CMAKE_OPTION="-DCMAKE_TOOLCHAIN_FILE=cmake/linux_aarch64.cmake"
    fi

    if [ ${UPDATE_EXTERNAL} == "true" ]; then
        rm -rf ${dir}/external
    fi
    rm -rf ${dir}/dbuild_${TARGET}
fi

cd submodules/snappy
git clean -fdx
if [ ${TARGET} == "android" ]; then
NDK=`which ndk-build`
NDK=${NDK}/../
cmake -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=armeabi-v7a -DANDROID_NATIVE_API_LEVEL=27 .
else
cmake .
fi
cd -

if [ ${TARGET} == "android" ]; then
    cd ${dir}/build-android
    if [ ${UPDATE_EXTERNAL} == "true" ] || [ ! -d ${dir}/build-android/third_party ]; then
        rm -rf third_party
        ./update_external_sources_android.sh
    fi
    ./android-generate.sh ${RELEASE_TYPE_OPTION} ${BUILD_TYPE_OPTION}
    ndk-build -j $(nproc)
    if [ -d $dir/submodules/libcollector ]; then
        cd $dir/submodules/libcollector/android/gradle
        git clean -fdx
        ANDROID_HOME=${ANDROID_SDK_HOME} ./gradlew assembleRelease
        cd -
    fi
    ./build_vkreplay.sh
else
    if [ ${UPDATE_EXTERNAL} == "true" ] || [ ! -d ${dir}/external ]; then
        ./update_external_sources.sh --build-type ${BUILD_TYPE} --target ${TARGET#rhe6_}
    fi
    cmake -H. -Bdbuild_${TARGET} -DCMAKE_BUILD_TYPE=${BUILD_TYPE_OPTION} ${RELEASE_TYPE_OPTION} ${EXTRA_CMAKE_OPTION}
    cd ${dir}/dbuild_${TARGET}
    make clean
    make -j $(nproc)
fi

if [ ${NEED_PACKAGE} == "true" ]; then
    # Archive build outputs
    rm -rf ${TARGET}
    if [ ${TARGET} == "android" ]; then
        mkdir -p ${TARGET}/retracer
        mkdir -p ${TARGET}/trace_layer
        mkdir -p ${TARGET}/trace_layer/armeabi-v7a
        mkdir -p ${TARGET}/trace_layer/arm64-v8a
        cp -a vkreplay/bin/vkreplay.apk ${TARGET}/retracer/
        cp -a vkreplay/bin/vkreplay32.apk ${TARGET}/retracer/
        cp -a libs/armeabi-v7a/libVkLayer_screenshot.so ${TARGET}/trace_layer/armeabi-v7a/
        cp -a libs/armeabi-v7a/libVkLayer_vktrace_layer.so ${TARGET}/trace_layer/armeabi-v7a/
        cp -a libs/armeabi-v7a/libVkLayer_device_simulation.so ${TARGET}/trace_layer/armeabi-v7a/
        cp -a libs/armeabi-v7a/libVkLayer_offscreenrender.so ${TARGET}/trace_layer/armeabi-v7a/
        cp -a libs/arm64-v8a/libVkLayer_screenshot.so ${TARGET}/trace_layer/arm64-v8a/
        cp -a libs/arm64-v8a/libVkLayer_vktrace_layer.so ${TARGET}/trace_layer/arm64-v8a/
        cp -a libs/arm64-v8a/libVkLayer_device_simulation.so ${TARGET}/trace_layer/arm64-v8a/
        cp -a libs/arm64-v8a/libVkLayer_offscreenrender.so ${TARGET}/trace_layer/arm64-v8a/
    else
        mkdir -p ${TARGET}/bin
        cp -a vktrace/vktracedump${BINARY_SUFFIX} ${TARGET}/bin/vktracedump
        cp -a vktrace/vkreplay${BINARY_SUFFIX} ${TARGET}/bin/vkreplay
        cp -a vktrace/vktrace${BINARY_SUFFIX} ${TARGET}/bin/vktrace
        cp -a vktrace/vktrace_layer/staging-json/VkLayer_vktrace_layer.json ${TARGET}/bin/
        cp -a layersvt/staging-json/VkLayer_screenshot.json ${TARGET}/bin/
        cp -a layersvt/libVkLayer_screenshot.so ${TARGET}/bin/
        cp -a vktrace/libVkLayer_vktrace_layer${BINARY_SUFFIX}.so ${TARGET}/bin/libVkLayer_vktrace_layer.so
        if [ ${TARGET} != "arm_64" ] && [ ${TARGET} != "arm_32" ]; then
            cp -a vktrace/libvkdisplay_wayland${BINARY_SUFFIX}.so ${TARGET}/bin/libvkdisplay_wayland.so
            cp -a vktrace/libvkdisplay_xcb${BINARY_SUFFIX}.so ${TARGET}/bin/libvkdisplay_xcb.so
        fi
    fi

    tar czf vktrace_${TARGET}_${BUILD_TYPE}.tgz ${TARGET}
fi
