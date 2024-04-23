# Build Instructions
This document contains the instructions for building this repository on Linux.

This repository contains Vulkan development tools, such as additional layers and VkTrace trace/replay utilities,
supplementing the loader and validation layer core components found at https://github.com/KhronosGroup.

## Get Additional Components

The public repository for the the LunarG VulkanTools is hosted at https://github.com/LunarG.

If you intend to contribute, the preferred work flow is to fork the repo,
create a branch in your forked repo, do the work,
and create a pull request on GitHub to integrate that work back into the repo.

## Index

1. [Repository Set-Up](#repository-set-up)
2. [Linux Build](#building-for-linux)
3. [Android Build](#building-for-android)

## Repository Set-Up

### Clone the Repository

Use the following Git command to create a local copy of the VkTrace
repository:

```bash
git clone https://github.com/ARM-software/vktrace-arm.git
```

### Repository Dependencies

Required dependencies are included with the VkTrace repository as Git
submodules. After cloning the repository, execute the following command
from within the VkTrace project folder to initialize the submodules:

```bash
cd vktrace-arm
git submodule update --init --recursive
```

This repository also has a required dependency on [Vulkan Headers](https://github.com/KhronosGroup/Vulkan-Headers), [Vulkan-Loader](https://github.com/KhronosGroup/Vulkan-Loader.git), [Vulkan-ValidationLayers](https://github.com/KhronosGroup/Vulkan-ValidationLayers.git), [Vulkan-Tools](https://github.com/KhronosGroup/Vulkan-Tools.git).

There is a Python utility script, `scripts/update_deps.py`, that you can use to
gather and build the dependent repositories mentioned above. This script uses
information stored in the `scripts/known_good.json` file to check out dependent
repository revisions that are known to be compatible with the revision of this
repository that you currently have checked out. As such, this script is useful
as a quick-start tool for common use cases and default configurations.

These Vulkan repository dependencies can be resolved by using build option `--update-external true` when building. 

## Build Script
There is a build script **build_vktrace.sh** in the root of the repo.

It has following arguments:

    --target [android|x86|x64|arm_32|arm_64] (optional, default is android)
    --release-type [dev|release] (optional, default is dev)
    --build-type [debug|release] (optional, default is debug)
    --update-external [true|false] (optional, set to true to force update external directories, default is false)
    --package [true|false] (optional, set to true to package build outputs to vktrace_<TARGET>_<BUILD_TYPE>.tgz, default is true)

## Building for Linux

This build process builds all items in the repository

### Required Package List
Building on Linux requires the installation of the following packages:
- A C++ compiler with C++17 support
- Git
- CMake >= 3.17.2
- Python >= 3.7.1
- X11 + XCB and/or Wayland development libraries

#### Ubuntu
VkTrace is built and tested for SDK development using Ubuntu 20.04. Other Ubuntu releases may not be unsupported.

For Ubuntu, the required packages can be installed with the following command:
```
For 64-bit builds:
sudo apt-get install git cmake build-essential bison libx11-xcb-dev libxkbcommon-dev libwayland-dev libxrandr-dev libxcb-randr0-dev libxcb-keysyms1 libxcb-keysyms1-dev libxcb-ewmh-dev libxcb-keysyms1 libxcb-keysyms1-dev libxcb-ewmh-dev

For 32-bit builds:
sudo apt-get install libx11-xcb-dev:i386 libxcb-keysyms1-dev:i386 libwayland-dev:i386 libxrandr-dev:i386

For arm64 builds (cross compilation):
sudo apt-get install g++-aarch64-linux-gnu
sudo apt-get install gcc-aarch64-linux-gnu
```
### Linux Build


Build with script:
```
./build_vktrace.sh --target [x86|x64|arm_32|arm_64] --build-type release --update-external true
# Or if you do not want to update the external folder:
./build_vktrace.sh --target [x86|x64|arm_32|arm_64] --build-type release 

# for example: x86-64 build
./build_vktrace.sh --target x64 --build-type release --update-external true

# for examole: arm64 build
./build_vktrace.sh --target arm_64 --build-type release --update-external true
```

After building, the binaries and libraries will be in `dbuild_<target>/<target>/bin`.

## Building for Android


### Android Development Requirements
- The [Android Platform tools](https://developer.android.com/studio/releases/platform-tools) for your specific platform
- [Android SDK](https://guides.codepath.com/android/installing-android-sdk-tools) 29 (10 Quince Tart) or newer
- [Android NDK](https://developer.android.com/ndk/guides/)  20~21 (21.4.7075529 is recommended)
- CMake 3.10.2.4988404
- Java JDK 1.8.0

These can be configured using [Android Studio](https://developer.android.com/studio)

For Android Platform tools: ` (Toolbar) Tools -> SDK Manager -> SDK Platforms`

For SDK, NDK and CMake:
` (Toolbar) Tools -> SDK Manager -> SDK Tools` and check the option `Show Packages Details` (in the bottom right corner) to select specific versions


Or install manually (not recommanded):

`sdkmanager` can be downloaded from [Android Studio command line tools](https://developer.android.com/studio).

Note: when using sdkmanager, please switch to Java 17 and set `--sdk_root=<SDK_ROOT>` 
```
# For example:
# set SDK_ROOT as ANDROID_SDK_HOME
# Install SDK:
sdkmanager --sdk_root=$ANDROID_SDK_HOME "platform-tools" "platforms;android-29" \
              "build-tools;30.0.3" "cmake;3.10.2.4988404"
# Install NDK:
sdkmanager --sdk_root=$ANDROID_SDK_HOME --install "ndk;21.4.7075529"
```


### Set Environment Variables

Environment variables `ANDROID_SDK_HOME` and `ANDROID_NDK_HOME` are needed
```
# For example:
export ANDROID_SDK_HOME=$HOME/Android/Sdk
export ANDROID_NDK_HOME=${ANDROID_SDK_HOME}/ndk/21.4.7075529

export PATH=$PATH:$ANDROID_NDK_HOME
export PATH=$PATH:${ANDROID_SDK_HOME}/platform-tools
export PATH=$PATH:${ANDROID_SDK_HOME}/build-tools/30.0.3
```

`ANDROID_SDK_HOME` can be found in Android Studio ` (Toolbar) Tools -> SDK Manager -> Android SDK Location`

### Android Build

Build with script:
```
./build_vktrace.sh --target android --build-type release --update-external true
# Or if you do not want to update the build-android/thirdparty folder:
./build_vktrace.sh --target android --build-type release 
```


There might be some issues about the keytool:
```
# To fix the keytool error "The -keyalg option must be specified.":
keytool -genkey -v -keystore ~/.android/debug.keystore -storepass android -alias androiddebugkey -keypass android -dname "CN=Android Debug,O=Android,C=US" -keyalg RSA
```


After building, the packages and layers will be in `build-android/android`.
