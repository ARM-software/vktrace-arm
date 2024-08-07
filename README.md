## Please note this is an Arm fork with some enhancements listed below.

1. Preload trace file to memory for performance test.
2. Arm linux build with headless support.
3. Enhanced device simulation layer.
4. Some replay performance optimization.

*** The Windows build is NOT tested and may be broken.**

And it is prefered to use the script `build_vktrace.sh` to do the build.

# Vulkan Ecosystem Components

This project provides vktrace capture/replay tool, the Layer Factory, and other layer tools and driver tests.

## CI Build Status
| Platform | Build Status |
|:--------:|:------------:|
| Linux/Android | [![Build Status](https://travis-ci.org/LunarG/VulkanTools.svg?branch=master)](https://travis-ci.org/LunarG/VulkanTools) |
| Windows | [![Build status](https://ci.appveyor.com/api/projects/status/2ncmy766ufb2hnh2/branch/master?svg=true)](https://ci.appveyor.com/project/karl-lunarg/vulkantools/branch/master) |

## Introduction

Branches within this repository include the Vulkan loader, validation layers, header files, and associated tests.  These pieces are mirrored from this Github repository:
https://github.com/KhronosGroup/Vulkan-ValidationLayers
These pieces are required to enable this repository to be built standalone; that is without having to clone the Vulkan-ValidationLayers repository.

The following components are available in this repository over and above what is mirrored from Vulkan-ValidationLayers repository
- Api_dump, screenshot, device_simulation, and example layers (layersvt/)
- Starter_layer and demo_layer (layer_factory/)
- tests for the vktrace and vkreplay (tests/)
- vktrace and vkreplay, API capture and replay  (vktrace/)

## Contributing

If you intend to contribute, the preferred work flow is for you to develop your contribution
in a fork of this repo in your GitHub account and then submit a pull request.
Please see the [CONTRIBUTING](CONTRIBUTING.md) file in this repository for more details

## How to Build and Run

[BUILD.md](BUILD.md)
includes directions for building all the components, running the tests and running the demo applications.

Information on how to enable the various layers is in
[layersvt/README.md](layersvt/README.md).

## Version Tagging Scheme

Updates to the `LunarG-VulkanTools` repository which correspond to a new Vulkan specification release are tagged using the following format: `v<`_`version`_`>` (e.g., `v1.1.96`).

**Note**: Marked version releases have undergone thorough testing but do not imply the same quality level as SDK tags. SDK tags follow the `sdk-<`_`version`_`>.<`_`patch`_`>` format (e.g., `sdk-1.1.92.0`).

This scheme was adopted following the 1.1.96 Vulkan specification release.

## License
This work is released as open source under a Apache-style license from Khronos including a Khronos copyright.

See COPYRIGHT.txt for a full list of licenses used in this repository.

## Acknowledgements
While this project has been developed primarily by LunarG, Inc., there are many other
companies and individuals making this possible: Valve Corporation, funding
project development; Google providing significant contributions to the validation layers;
Khronos providing oversight and hosting of the project.

## Repository Dependencies
See the [External Dependencies README.md](external/README.md) for details.