#!/bin/bash

# Copyright 2017 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if [ -z "${ANDROID_SDK_HOME}" ];
then echo "Please set ANDROID_SDK_HOME, exiting"; exit 1;
else echo "ANDROID_SDK_HOME is ${ANDROID_SDK_HOME}";
fi

function findtool() {
    if [[ ! $(type -t $1) ]]; then
        echo Command $1 not found, see ../BUILD.md;
        exit 1;
    fi
}

# Check for dependencies
findtool aapt
findtool zipalign
findtool jarsigner

set -ev

LAYER_BUILD_DIR=$PWD
echo LAYER_BUILD_DIR="${LAYER_BUILD_DIR}"

# libcollector path
LIBCOLLECTOR_LAYER_DIR=${LAYER_BUILD_DIR}/../submodules/libcollector

function create_APK() {
    aapt package -f -M AndroidManifest.xml -I "$ANDROID_SDK_HOME/platforms/android-23/android.jar" -S res -F bin/$1-unaligned.apk bin/libs
    # update this logic to detect if key is already there.  If so, use it, otherwise create it.
    jarsigner -verbose -keystore ~/.android/debug.keystore -storepass android -keypass android  bin/$1-unaligned.apk androiddebugkey
    zipalign -f 4 bin/$1-unaligned.apk bin/$1.apk
}

#
# create vkreplay APK
#
(
pushd $LAYER_BUILD_DIR/vkreplay
mkdir -p bin/libs/lib
for abi in armeabi-v7a arm64-v8a
do
    mkdir -p $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}
    cp $LAYER_BUILD_DIR/libs/${abi}/libvkreplay.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libvkreplay.so
    cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_screenshot.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_screenshot.so
    cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_monitor.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_monitor.so
    cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_offscreenrender.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_offscreenrender.so
    cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_api_cost.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_api_cost.so
    cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_api_dump.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_api_dump.so
    cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_systrace.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_systrace.so
    cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_emptydriver.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_emptydriver.so
    cp $LIBCOLLECTOR_LAYER_DIR/android/gradle/layer_android/build/intermediates/transforms/stripDebugSymbol/release/0/lib/${abi}/libVkLayer_libcollector.so \
            $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_libcollector.so
done
create_APK vkreplay
popd
)

#
# create vkreplay32 APK
{
pushd $LAYER_BUILD_DIR/vkreplay
rm -rf bin/libs
mkdir -p bin/libs/lib
abi='armeabi-v7a'
mkdir -p $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}
cp $LAYER_BUILD_DIR/libs/${abi}/libvkreplay.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libvkreplay.so
cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_screenshot.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_screenshot.so
cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_monitor.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_monitor.so
cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_offscreenrender.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_offscreenrender.so
cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_api_cost.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_api_cost.so
cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_api_dump.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_api_dump.so
cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_systrace.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_systrace.so
cp $LAYER_BUILD_DIR/libs/${abi}/libVkLayer_emptydriver.so $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_emptydriver.so
cp $LIBCOLLECTOR_LAYER_DIR/android/gradle/layer_android/build/intermediates/transforms/stripDebugSymbol/release/0/lib/${abi}/libVkLayer_libcollector.so \
        $LAYER_BUILD_DIR/vkreplay/bin/libs/lib/${abi}/libVkLayer_libcollector.so
sed -i 's/com.example.vkreplay/com.example.vkreplay32/' AndroidManifest.xml
create_APK vkreplay32
popd
}

git checkout -- .

exit 0
