This vktrace build is based on lunarG's open source VulkanTools project, https://github.com/LunarG/VulkanTools.

# VkTrace Trace and Replay - Android

***This document describes the VkTrace software for tracing and replaying Vulkan API calls on Android.***

If you are looking for tracing/replaying on Android platform, please refer
to one of these other documents:
 * [VkTrace for Linux](./USAGE_linux.md)

## Index

## Tracing
The trace program is named `vktrace`. It is used to record an application's Vulkan API calls to a trace file. The call information is stored in the trace file in a compact binary format. The trace files normally have a  `.vktrace` suffix. The application can be either a local or remote application. 


After building, please follow the steps below to trace an app. Make sure the device is online in 'adb devices'.

```
    1)  adb root
    2)  adb shell setenforce 0
    3)  adb shell rm -rf /data/local/debug/vulkan
    4)  adb shell mkdir -p /data/local/debug/vulkan

    # trace 64-bit Vulkan APK
    5)  adb push android/trace_layer/arm64-v8a/libVkLayer_vktrace_layer.so /data/local/debug/vulkan/

    6)  adb shell setprop debug.vulkan.layer.1 VK_LAYER_LUNARG_vktrace
    7)  adb reverse localabstract:vktrace tcp:34201
    8)  dbuild_x64/x64/bin/vktrace -v full -o <filename>.vktrace

    9)  # Optional: Trim/Fast forward
        # Set trim frame range (trim start frame and trim end frame)
        adb shell setprop VKTRACE_TRIM_TRIGGER "frames-<start_frame>-<end_frame>"
    10) # Optional: Force using FIFO present mode in vkCreateInstance.
        adb shell setprop VKTRACE_FORCE_FIFO 1

    11) # Run the app on android to start tracing
    12) # Shutdown the app to stop tracing
        adb shell am force-stop <app's package name>

    # Acceleration Structure Related Environment Variable
    adb shell setprop VKTRACE_ENABLE_REBINDMEMORY_ALIGNEDSIZE 1
    adb shell setprop VKTRACE_AS_BUILD_RESIZE 2

    # optional for acceleration structure
    adb shell setprop VKTRACE_PAGEGUARD_ENABLE_SYNC_GPU_DATA_BACK  1
    adb shell setprop VKTRACE_DELAY_SIGNAL_FENCE_FRAMES  3

```

*NOTE*: if you could not perform adb root, you will need to copy the `libVkLayer_vktrace_layer.so` in the `/sdcard` first, then perform the copy inside the phone's shell

> You need to build x64 and android vktrace to trace an Android app.

### Replay
After building, please follow the steps below to replay a trace file.

```

    1) adb uninstall com.example.vkreplay
    2) adb install -g build-android/android/retracer/vkreplay.apk
    3) adb shell setprop debug.vulkan.layer.1 '""'
    4) adb push <filename>.vktrace /sdcard/<filename>.vktrace

    5) adb shell am start -a android.intent.action.MAIN -c android-intent.category.LAUNCH -n com.example.vkreplay/android.app.NativeActivity --es args '"-v full -o /sdcard/<filename>.vktrace"'

    6) # Optional: Take screenshots
       adb shell am start -a android.intent.action.MAIN -c android-intent.category.LAUNCH -n com.example.vkreplay/android.app.NativeActivity --es args '"-v full -o /sdcard/<filename>.vktrace -s <string>"'
       #     The <string> is one of following three options:
       #         1. comma separated list of frames (e.g. 0,1,2)
       #         2. <start_frame_index>-<count>-<interval> (e.g. 0-3-1 to take 3 screenshots for every 1 frame from frame 0)
       #         3. "all" (take screenshot for every frame)
       #     Note: Screenshots can be found in /sdcard/Android/ with name <frame index>.ppm
    7) # Optional: Force stop the replayer during retracing.
       adb shell am force-stop com.example.vkreplay
```

### Replay Options

The  `vkreplay` command-line  options are:

| Replay Option         | Description | Required |Default |
| --------------------- | ----------- | ------- |------- |
| -o&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;Open&nbsp;&lt;string&gt; | Name of trace file to open and replay | Yes | N/A |
| -l&nbsp;&lt;int&gt;<br>&#x2011;&#x2011;NumLoops&nbsp;&lt;int&gt; | Number of times to replay the trace file  | No |1 |
| -lsf&nbsp;&lt;int&gt;<br>&#x2011;&#x2011;LoopStartFrame&nbsp;&lt;int&gt; | The start frame number of the loop range | No | 0 |
| -lef&nbsp;&lt;int&gt;<br>&#x2011;&#x2011;LoopEndFrame&nbsp;&lt;int&gt; | The end frame number of the loop range | No | No | the last frame in the tracefile |
| -c&nbsp;&lt;bool&gt;<br>&#x2011;&#x2011;CompatibilityMode&nbsp;&lt;bool&gt; | Enable compatibility mode - modify api calls as needed when replaying trace file created on different platform than replay platform. For example: Convert trace file memory indices to replay device memory indices. | No | true |
| -s&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;Screenshot&nbsp;&lt;string&gt; | Comma-separated list of frame numbers of which to take screen shots  | No | no screenshots |
| -sf&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;ScreenshotFormat&nbsp;&lt;string&gt; | Color Space format of screenshot files. Formats are UNORM, SNORM, USCALED, SSCALED, UINT, SINT, SRGB  | No | Format of swapchain image |
| -x&nbsp;&lt;bool&gt;<br>&#x2011;&#x2011;ExitOnAnyError&nbsp;&lt;bool&gt; | Exit if an error occurs during replay | No | false |
| -v&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;Verbosity&nbsp;&lt;string&gt; | Verbosity mode - "quiet", "errors", "warnings", or "full" | No | errors |
|-fdaf&nbsp;&lt;bool&gt;<br>&#x2011;&#x2011;forceDisableAF&nbsp;&lt;bool&gt; | Force to disable anisotropic filter.| No |false|
|-fbw&nbsp;&lt;bool&gt;<br>&#x2011;&#x2011;finishBeforeSwap&nbsp;&lt;bool&gt; | Inject the vkDeviceWaitIdle function before vkQueuePresent.| No |false|
|-epc&nbsp;&lt;bool&gt;<br>&#x2011;&#x2011;enablePipelineCache&nbsp;&lt;bool&gt; | Write/load the pipeline cache to the disk. The tool will detect if the pipeline cache files have already been written, in this case, the pipeline cache will be loaded instead of being saved.| No |false|
|-pcp&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;pipelineCachePath&nbsp;&lt;string&gt; | Set the path for saving/loading the pipeline cache data.| No |"/sdcard/Android/data/com.example.vkreplay/pipelinecache" for Android ;<br>"./pipelinecache" for Linux|
|-fsii&nbsp;&lt;bool&gt;<br>&#x2011;&#x2011;forceSyncImgIdx&nbsp;&lt;bool&gt; | Force sync the acquire next image index.| No |false|
|-vsyncoff&nbsp;&lt;bool&gt;<br>&#x2011;&#x2011;VsyncOff&nbsp;&lt;bool&gt; | Turn off vsync to avoid replay being vsync-limited| No |false|
|-drcr&nbsp;&lt;uint&gt;<br>&#x2011;&#x2011;disableRQAndRTPCaptureReplay&nbsp;&lt;uint&gt; | Disable ray query and ray tracing capture replay feature,these three values can be combined to use: accelerationStructureCaptureReplay :0x01, bufferDeviceAddressCaptureReplay :0x02, rayTracingPipelineShaderGroupHandleCaptureReplay :0x04.| No |0|
|-tsf&nbsp;&lt;uint32&gt;<br>&#x2011;&#x2011;TriggerScriptOnFrame&nbsp;&lt;uint32&gt; | Set the frame index to trigger the script. This parameter must be paired with parameter tsp.| No |UINT64_MAX|
|-tsp&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;scriptPath&nbsp;&lt;string&gt; | Set the script path which will be trigger. This parameter must be paired with parameter tsf.| No |""|
|-pc&nbsp;&lt;uint&gt;<br>&#x2011;&#x2011;printCurrentPacketIndex&nbsp;&lt;uint&gt; |  Print current replayed packet index: 0 - off, 1 - only print all frames, 2 - print all calls and frames, > 10 print every N calls and frames.| No |0|
|-evsc&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;enableVirtualSwapchain&nbsp;&lt;string&gt; |Enable the virtual swapchain.| No |false|
|-vscpm&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;enableVscPerfMode&nbsp;&lt;string&gt; | Set the script path which will be trigger. This parameter must be paired with parameter tsf.| No |""|

#### Acceleration Structure Related Replay Options
Replaying ray query/ray tracing traces need add special parameters.


```
# Replaying with ray tracing capture replay feature:
adb shell am start -a android.intent.action.MAIN -c android-intent.category.LAUNCH -n com.example.vkreplay/android.app.NativeActivity --es args '"-v full -o /sdcard/<filename>.vktrace -drcr 0"'
# the default value of drcr is 0, except drcr is written to vktrace header

# Replaying without ray tracing capture replay feature:
adb shell am start -a android.intent.action.MAIN -c android-intent.category.LAUNCH -n com.example.vkreplay/android.app.NativeActivity --es args '"-v full -o /sdcard/<filename>.vktrace -drcr 7"'
# the drcr option can be used with combined values

# If the buffer device addresses in the memory of buffer need be remapped:
adb shell am start -a android.intent.action.MAIN -c android-intent.category.LAUNCH -n com.example.vkreplay/android.app.NativeActivity --es args '"-v full -o /sdcard/<filename>.vktrace -drcr 7 -spc 1"'
# the drcr option can be used with combined values
```