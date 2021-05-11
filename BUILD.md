# Build Instructions
This document contains the instructions for building this repository on Linux and Windows.

This repository contains Vulkan development tools, such as additional layers and VkTrace trace/replay utilities,
supplementing the loader and validation layer core components found at https://github.com/KhronosGroup.

## Get Additional Components

The public repository for the the LunarG VulkanTools is hosted at https://github.com/LunarG.

If you intend to contribute, the preferred work flow is to fork the repo,
create a branch in your forked repo, do the work,
and create a pull request on GitHub to integrate that work back into the repo.

## Linux System Requirements
These additional packages are needed for building the components in this repo.

### Ubuntu

Ubuntu 16.04 LTS and 18.04 have been tested with this repo.

[CMake 3.10.2](https://cmake.org/files/v3.10/cmake-3.10.2-Linux-x86_64.tar.gz) is recommended.

```
# Dependencies from included submodule components
sudo apt-get install git build-essential bison libx11-xcb-dev libxkbcommon-dev libwayland-dev libxrandr-dev libxcb-randr0-dev
# Additional dependencies for this repo:
sudo apt-get install wget autotools-dev libxcb-keysyms1 libxcb-keysyms1-dev libxcb-ewmh-dev
# If performing 32-bit builds, you will also need:
sudo apt-get install libc6-dev-i386 g++-multilib
```

On Ubuntu 18.04 LTS or newer, you may build VkConfig only if you also install several
additional Qt dependencies:

```
# Qt Dependencies for building VkConfig (not required if you don't want VkConfig)
sudo apt-get install qt5-default qtwebengine5-dev
```

### Fedora Core

Fedora Core 28 and 29 were tested with this repo.

[CMake 3.10.2](https://cmake.org/files/v3.10/cmake-3.10.2-Linux-x86_64.tar.gz) is recommended.

Additional package dependencies include:

```
# Dependencies from included submodule components
sudo dnf install git @development-tools glm-devel \
                 libpng-devel wayland-devel libpciaccess-devel \
                 libX11-devel libXpresent libxcb xcb-util libxcb-devel libXrandr-devel \
                 xcb-util-keysyms-devel xcb-util-wm-devel
# Qt Dependencies for building VkConfig (not required if you don't want VkConfig)
sudo dnf install qt qt5-qtwebengine-devel
```

## Download the repository

To create your local git repository of VulkanTools:
```
cd YOUR_DEV_DIRECTORY

# Clone the VulkanTools repo
git clone --recurse-submodules git@github.com:LunarG/VulkanTools.git

# Enter the folder containing the cloned source
cd VulkanTools

# This will perform some initialization and ensure subcomponents are built:
./update_external_sources.sh    # linux
./update_external_sources.bat   # windows
```

## Updating the Repository After a Pull

The VulkanTools repository contains a submodule named jsoncpp. You may occasionally have to update the source in that submodules.
You will know this needs to be performed when you perform a pull, and you check the status of your tree with `git status` and something similar to the following shows:

```
(master *)] $ git status
On branch master
Your branch is up-to-date with 'origin/master'.

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git checkout -- <file>..." to discard changes in working directory)

	modified:   submodules/jsoncpp (new commits)

no changes added to commit (use "git add" and/or "git commit -a")
```

To resolve this, simply update the sub-module using:

```
git submodule update --recursive
```

Then, update the external sources as before:

```
# This will perform required subcomponent operations.
./update_external_sources.sh    # linux
./update_external_sources.bat   # windows
```

Now, you should be able to continue building as normal.


## Repository Dependencies
This repository attempts to resolve some of its dependencies by using
components found from the following places, in this order:

1. CMake or Environment variable overrides (e.g., -DVULKAN_HEADERS_INSTALL_DIR)
1. LunarG Vulkan SDK, located by the `VULKAN_SDK` environment variable
1. System-installed packages, mostly applicable on Linux

Dependencies that cannot be resolved by the SDK or installed packages must be
resolved with the "install directory" override and are listed below. The
"install directory" override can also be used to force the use of a specific
version of that dependency.

### Vulkan-Headers

This repository has a required dependency on the
[Vulkan Headers repository](https://github.com/KhronosGroup/Vulkan-Headers).
You must clone the headers repository and build its `install` target before
building this repository. The Vulkan-Headers repository is required because it
contains the Vulkan API definition files (registry) that are required to build
the validation layers. You must also take note of the headers' install
directory and pass it on the CMake command line for building this repository,
as described below.

### Vulkan-Loader

The tools in this repository depend on the Vulkan loader.

A loader can be used from an installed LunarG SDK, an installed Linux package,
or from a driver installation on Windows.

If a loader is not available from any of these methods and/or it is important
to use a loader built from a repository, then you must build the
[Vulkan-Loader repository](https://github.com/KhronosGroup/Vulkan-Loader.git)
with its install target. Take note of its install directory location and pass
it on the CMake command line for building this repository, as described below.

### Vulkan-ValidationLayers
The tools in this repository depend on the Vulkan validation layers.

Validation layers can be used from an installed LunarG SDK, an installed Linux
package, or from a driver installation on Windows.

If the validation layers are not available from any of these methods and/or
it is important to use the validation layers built from a repository, then you
must build the
[Vulkan-ValidationLayers repository](https://github.com/KhronosGroup/Vulkan-ValidationLayers.git)
with its install target. Take note of its install directory location and pass
it on the CMake command line for building this repository, as described below.

### Vulkan-Tools

The tests in this repository depend on the Vulkan-Tools repository, which is
hosted under Khronos' GitHub account and differentiated in name by a hyphen.
The tests use Vulkan Info and the mock ICD from Vulkan-Tools.

You may build the
[Vulkan-Tools repository](https://github.com/KhronosGroup/Vulkan-Tools.git)
with its install target. Take note of its build directory location and set
the VULKAN\_TOOLS\_BUILD\_DIR environment variable to the appropriate path.

If you do not intend to run the tests, you do not need Vulkan-Tools.

### Build and Install Directories

A common convention is to place the build directory in the top directory of
the repository with a name of `build` and place the install directory as a
child of the build directory with the name `install`. The remainder of these
instructions follow this convention, although you can use any name for these
directories and place them in any location.

### Building Dependent Repositories with Known-Good Revisions

There is a Python utility script, `scripts/update_deps.py`, that you can use to
gather and build the dependent repositories mentioned above. This script uses
information stored in the `scripts/known_good.json` file to check out dependent
repository revisions that are known to be compatible with the revision of this
repository that you currently have checked out. As such, this script is useful
as a quick-start tool for common use cases and default configurations.

For all platforms, start with:

    git clone --recurse-submodules git@github.com:LunarG/VulkanTools.git
    cd VulkanTools
    mkdir build

For 64-bit Linux and MacOS, continue with:

    ./update_external_sources.sh
    cd build
    ../scripts/update_deps.py
    cmake -C helper.cmake ..
    cmake --build .

For 64-bit Windows, continue with:

    .\update_external_sources.bat
    cd build
    ..\scripts\update_deps.py --arch x64
    cmake -A x64 -C helper.cmake ..
    cmake --build .

For 32-bit Windows, continue with:

    .\update_external_sources.bat
    cd build
    ..\scripts\update_deps.py --arch Win32
    cmake -A Win32 -C helper.cmake ..
    cmake --build .

Please see the more detailed build information later in this file if you have
specific requirements for configuring and building these components.

#### Notes

- You may need to adjust some of the CMake options based on your platform. See
  the platform-specific sections later in this document.
- The `update_deps.py` script fetches and builds the dependent repositories in
  the current directory when it is invoked. In this case, they are built in
  the `build` directory.
- The `build` directory is also being used to build this
  (Vulkan-Tools) repository. But there shouldn't be any conflicts
  inside the `build` directory between the dependent repositories and the
  build files for this repository.
- The `--dir` option for `update_deps.py` can be used to relocate the
  dependent repositories to another arbitrary directory using an absolute or
  relative path.
- The `update_deps.py` script generates a file named `helper.cmake` and places
  it in the same directory as the dependent repositories (`build` in this
  case). This file contains CMake commands to set the CMake `*_INSTALL_DIR`
  variables that are used to point to the install artifacts of the dependent
  repositories. You can use this file with the `cmake -C` option to set these
  variables when you generate your build files with CMake. This lets you avoid
  entering several `*_INSTALL_DIR` variable settings on the CMake command line.
- If using "MINGW" (Git For Windows), you may wish to run
  `winpty update_deps.py` in order to avoid buffering all of the script's
  "print" output until the end and to retain the ability to interrupt script
  execution.
- Please use `update_deps.py --help` to list additional options and read the
  internal documentation in `update_deps.py` for further information.


## VkConfig

VkConfig has additional requirements beyond the rest of the source in this
repository.
Because of that, if one or more of those dependencies is not properly installed,
you may get a warning during CMake generation like the following:

```
WARNING: vkconfig will be excluded because Qt5 was not found.
-- Configuring done
-- Generating done
```

This is usually caused by missing Qt dependencies.
Make sure the following items are installed for proper building of VkConfig:

* Qt5
* Qt5 Web Engine Widgets

Please note that not building VkConfig is purely fine as well and will not
impact the generation of any other targets.

## Build Script
There is a build script **build_vktrace.sh** in the root of the repo.

It has following arguments:

	--target [android|x86|x64|arm_32|arm_64] (optional, default is android)
	--release-type [dev|release] (optional, default is dev)
	--build-type [debug|release] (optional, default is debug)
	--update-external [true|false] (optional, set to true to force update external directories, default is false)
	--package [true|false] (optional, set to true to package build outputs to vktrace_<TARGET>_<BUILD_TYPE>.tgz, default is true)

## Linux Build

This build process builds all items in the VulkanTools repository

Build with script:
```
./build_vktrace.sh --target [x86|x64|arm_32|arm_64] --update-external true
# Or if you do not want to update the external folder:
./build_vktrace.sh --target [x86|x64|arm_32|arm_64]
```
Or build manually:
```
# Make sure the external folder which contains glslang, Vulkan-Headers, and Vulkan-Loader is cleaned up.
rm -rf external
# Start to build
./update_external_sources.sh
cmake -H. -Bdbuild_x64 -DCMAKE_BUILD_TYPE=Debug
cd dbuild_x64
make -j $(nproc)
```

## Android Build
Install the required tools for Linux and Windows covered above, then add the
following.

### Android Studio
- Install 2.1 or later version of [Android Studio](http://tools.android.com/download/studio/stable)
- From the "Welcome to Android Studio" splash screen, add the following components using Configure > SDK Manager:
  - SDK Tools > Android NDK

#### Add NDK to path

On Linux:
```
export PATH=$HOME/Android/Sdk/ndk-bundle:$PATH
```
On Windows:
```
set PATH=%LOCALAPPDATA%\Android\sdk\ndk-bundle;%PATH%
```
On OSX:
```
export PATH=$HOME/Library/Android/sdk/ndk-bundle:$PATH
```

### Build steps for Android
Use the following to ensure the Android build works.
#### Linux

Build with script:
```
./build_vktrace.sh --target android --update-external true
# Or if you do not want to update the build-android/thirdparty folder:
./build_vktrace.sh --target android
```

Or build manually:
```
cd build-android
export ANDROID_SDK_HOME=<path/to/android/sdk>
# aapt can be found in android_sdk/build-tools/version/ e.g. android_sdk/build-tools/23.0.3/
export PATH=<path/to/the/directory/of/aapt>:${PATH}
export PATH=<path/to/android/ndk-bundle>:${PATH}
./update_external_sources_android.sh
./android-generate.sh
ndk-build -j $(nproc)
# Generate a debug.keystore if there is no such file under ~/.android/
keytool -genkey -v -keystore ~/.android/debug.keystore -storepass android -alias androiddebugkey -keypass android -dname "CN=Android Debug,O=Android,C=US"
./build_vkreplay.sh
# This will build two APKs:
# 1. vkreplay.apk which has package name "com.example.vkreplay" and can be installed as 64-bit or 32-bit (via "--abi armeabi-v7a" option in adb install command)
# 2. vkreplay32.apk which has package name "com.example.vkreplay32" and is a 32-bit only APK to make it possible for user to have both 32-bit and 64-bit version of vkreplay installed on Android.
```

## Android usage
This documentation is preliminary and needs to be beefed up.

See the [vktracereplay.sh](https://github.com/LunarG/VulkanTools/blob/master/build-android/vktracereplay.sh) file for a working example of how to use vktrace/vkreplay and screenshot layers.

Two additional scripts have been added to facilitate tracing and replaying any APK.  Note that these two scripts do not install anything for you, so make sure your target APK, vktrace, vktrace_layer, and vkreplay all use the same ABI.
```
./create_trace.sh --serial 0123456789 --abi armeabi-v7a --package com.example.CubeWithLayers  --activity android.app.NativeActivity
adb install --abi armeabi-v7a
./replay_trace.sh --serial 0123456789 --tracefile com.example.CubeWithLayers0.vktrace
```
An example of using the scripts on Linux and macOS:
```
./build_vktracereplay.sh
./vktracereplay.sh \
 --serial 12345678 \
 --abi armeabi-v7a \
 --apk ../demos/android/cube-with-layers/bin/cube-with-layers.apk \
 --package com.example.CubeWithLayers \
 --frame 50
```
And on Windows:
```
build_vktracereplay.bat ^
vktracereplay.bat ^
 --serial 12345678 ^
 --abi armeabi-v7a ^
 --apk ..\demos\android\cube-with-layers\bin\NativeActivity-debug.apk ^
 --package com.example.CubeWithLayers ^
 --frame 50
```
### vkjson_info
Currently vkjson_info is only available as an executable for devices with root access.

To use, simply push it to the device and run it.  The resulting json file will be found in:
```
/sdcard/Android/<output>.json
```
A working example can be found in [devsim_layer_test_anroid.sh](https://github.com/LunarG/VulkanTools/blob/master/build-android/devsim_layer_test_android.sh)
