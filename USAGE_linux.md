This vktrace build is based on lunarG's open source VulkanTools project, https://github.com/LunarG/VulkanTools.

# VkTrace Trace and Replay - Linux

***This document describes the VkTrace software for tracing and replaying Vulkan API calls on Linux.***

If you are looking for tracing/replaying on Android platform, please refer
to one of these other documents:
 * [VkTrace for Android](./USAGE_android.md)

## Index

## Tracing
The trace program is named `vktrace`. It is used to record an application's Vulkan API calls to a trace file. The call information is stored in the trace file in a compact binary format. The trace files normally have a  `.vktrace` suffix. The application can be either a local or remote application. 


### Trace Layer
The vktrace trace layer is a Vulkan layer that intercepts Vulkan API calls and logs them to a vktrace file.


After building, please follow the steps below to set the layer. The settings are the same for arm_linux and x64_linux.

> 64-bit applications need to be traced using the 64-bit (arm64-v8a for Arm platform) trace layer and any 64-bit vktrace.

#### Set Environment Variable
```
export VKTRACE_PATH=${vktrace_binaries_path}
export LD_LIBRARY_PATH=${driver_path}:$VKTRACE_PATH
export VK_LAYER_PATH=$VK_LAYER_PATH:$VKTRACE_PATH

# Optional:  Trim/Fast forward
# set trim frame range (trim start frame and trim end frame)
export VKTRACE_TRIM_TRIGGER="frames-<start_frame>-<end_frame>"
```

`vktrace_binaries_path` is the path to the layer's `VkLayer_vktrace_layer.json` file and corresponding `libVkLayer_vktrace_layer.so` library. It must be added to `VK_LAYER_PATH` environment variable for the Vulkan loader to find the layer. The layer and library are in `dbuild_<target>/<target>/bin` after building.

#### Enable Vulkan Trace Layer
```
export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_vktrace
```

####  Acceleration Structure Related Environment Variable
For those apps/programs using [acceleration structures](https://docs.vulkan.org/spec/latest/chapters/accelstructures.html), there are some additional environment variables.
```
# aligned size of get*MemoryRequirements with system page size
export VKTRACE_ENABLE_REBINDMEMORY_ALIGNEDSIZE=1

# specifies a multiplier of accelerationStructureSize, updateScratchSize, buildScratchSize returned by vkGetAccelerationStructureBuildSizesKHR
export VKTRACE_AS_BUILD_RESIZE=2

# Optional:  Trim/Fast forward
export VKTRACE_DISABLE_CAPTUREREPLAY_FEATURE=1

# optional
export VKTRACE_PAGEGUARD_ENABLE_SYNC_GPU_DATA_BACK=1
export VKTRACE_FORCE_FIFO=1
export VKTRACE_DELAY_SIGNAL_FENCE_FRAMES=3
```

### Trace Options
| Trace Option         | Description |  Default |
| -------------------- | ----------------- | --- |
| -a&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;Arguments&nbsp;&lt;string&gt; | Command line arguments to pass to the application to be traced | none |
| -o&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;OutputTrace&nbsp;&lt;string&gt; | Name of the generated trace file | `vktrace_out.vktrace` |
| -p&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;Program&nbsp;&lt;string&gt; | Name of the application to trace  | if not provided, server mode tracing is enabled |
| -ptm&nbsp;&lt;bool&gt;<br>&#x2011;&#x2011;PrintTraceMessages&nbsp;&lt;bool&gt; | Print trace messages to console | on |
| -s&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;Screenshot&nbsp;&lt;string&gt; | Frame numbers of which to take screen shots. String arg is one of:<br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;comma separated list of frames<br> &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&lt;start&gt;-&nbsp;&lt;count&gt;-&nbsp;&lt;interval&gt; <br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"all"  | no screenshots |
| -w&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;WorkingDir&nbsp;&lt;string&gt; | Alternate working directory | the application's directory |
| -P&nbsp;&lt;bool&gt;<br>&#x2011;&#x2011;PMB&nbsp;&lt;bool&gt; | Trace persistently mapped buffers | true |
| -tr&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;TraceTrigger&nbsp;&lt;string&gt; | Start/stop trim by hotkey or frame range. String arg is one of:<br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;hotkey-[F1-F12\|TAB\|CONTROL]<br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;frames-&lt;startframe&gt;-&lt;endframe&gt;| on |
| -tpp&nbsp;&lt;bool&gt;<br>&#x2011;&#x2011;TrimPostProcessing&nbsp;&lt;bool&gt; | Enable trim post-processing to make trimmed trace file smaller, see description of `VKTRACE_TRIM_POST_PROCESS` below | false |
| -tl&nbsp;&lt;bool&gt;<br>&#x2011;&#x2011;TraceLock&nbsp;&lt;bool&gt; | Enable locking of API calls during trace. Default is TRUE if trimming is enabled, FALSE otherwise. See description of `VKTRACE_ENABLE_TRACE_LOCK` below | See description |
| -v&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;Verbosity&nbsp;&lt;string&gt; | Verbosity mode - `quiet`, `errors`, `warnings`, `full`, or `max` | `errors` | The level of messages that should be logged.  The named level and below will be included.  The special value `max` always prints out all information available, and is generally equivalent to `full`.
| -tbs&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;TrimBatchSize&nbsp;&lt;string&gt; | Set the maximum trim commands batch size per command buffer, see description of `VKTRACE_TRIM_MAX_COMMAND_BATCH_SIZE` below  |  device memory allocation limit divided by 100 |

### Start Tracing
There are two tracing mode for vktrace: one is local file mode, the other one is client/server mode (recommended).


#### Local File Mode
In local tracing mode, both the `vktrace` and application executables reside on the same system.

```
# Set save trace packet to local file path and file name. if the option is not setted, default file path and name is : "vktrace_out.vktrace", ${trace.path} and ${app.trace.name} are your defined path and filename.
export VKTRACE_TRACE_PATH_FILENAME=/${trace.path}/${app.trace.name}.vktrace

# * the local file mode can't start vktrace server
 
# Run an application through vktrace:
vktrace -v full -p ./your-app -a "your-app args" -o <filename>.vktrace
```

An example command to trace the sample Vulkan Cube application in local mode follows.

Linux shell:

```
vktrace -p ./vkcube -o cubetrace.vktrace
```


Trace packets are written to the file `cubetrace.vktrace` in the local directory.  Output messages from the replay operation are written to `stdout`.

*Important*:  Subsequent `vktrace` runs with the same `-o` option value will overwrite the trace file, preventing the generation of multiple, large trace files.  Be sure to specify a unique output trace file name for each `vktrace` invocation if you do not desire this behaviour.

`vktrace` is in `dbuild_<target>/<target>/bin` after building.

#### Client/Server Mode
The tools also support tracing Vulkan applications in client/server mode, where the trace server resides on a local or a remote system.

Run the server on in one terminal
```
vktrace -v full -o <filename>.vktrace
```
In another terminal (client), do
```
# Set trace layer
export LD_LIBRARY_PATH=<vktrace dir>/dbuild_<target>/<target>/bin/:$LD_LIBRARY_PATH
export VK_LAYER_PATH=<vktrace dir>/dbuild_<target>/<target>/bin/
export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_vktrace

# Run application
```

##### Server

In client/server mode, the `vktrace` *server* is started without specifying the `-p` or `--Program` option.  Invoked in this fashion, `vktrace` enters a waiting mode for trace packets.

For example:

```
# save the trace to examples/traces
mkdir examples/traces
cd examples/traces
vktrace -o cubetrace_s.vktrace
```

The trace file will be written to `cubetrace_s.vktrace`. If additional programs are traced with this trace server, subsequent trace files will be named `cubetrace_s-<`_`N`_`>.vktrace`, with the trace server incrementing _`N`_ for each time the application is run.


##### Client
The tracer is implemented as a Vulkan layer.  When tracing in server mode, the local or remote client must enable the `Vktrace` layer.  The `Vktrace` layer *must* be the first layer identified in the `VK_INSTANCE_LAYERS` lists.

```
export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_vktrace
```

###### Local Client
Local client/server mode is particularly useful for Vulkan applications with multiple runtime options or complicated startup.

In local client/server mode, the tracer layer must be enabled before the local client application to be traced is launched.


```
cd examples/build
export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_vktrace
./vkcube
```
The generated trace file is found at `examples/traces/cubetrace_s.vktrace`.

*Note*:  The Vulkan Cube application is used to demonstrate tracing in client/server mode.  Vulkan Cube is a very simple application, and would not be traced in this mode in practice.

###### Remote Client
Remote client/server mode is useful if the client is a mobile device or running in a lightweight environment that might not have the disk or other capabilities to store large trace files.

In remote client/server mode, the remote client must additionally identify the IP address of the trace server system.

```
cd examples/build
export VKTRACE_LIB_IPADDR=<ip address of trace server system>
export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_vktrace
./vkcube
```

The generated trace file is located at `examples/traces/cubetrace_s.vktrace` on the remote trace server system.

## Replaying
The vkreplay command is used to replay a Vulkan application trace.

**Important**: Trace files generated with earlier versions of the vktrace tools may not be replayable with later versions of the tools. The trace file format has evolved over time - vkreplay attempts to identify incompatible versions during replay and can often successfully replay them, but it does not handle all such cases.

**Important**: Trace files generated on one GPU may or may not be replayable on other GPUs, as well as trace files generated on different OSes and different driver versions. vkreplay attempts to replay such trace files, translating API calls as needed for the new platform, but it does not handle all such cases.



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
| Linux Only |  |  |
| -ds&nbsp;&lt;string&gt;<br>&#x2011;&#x2011;DisplayServer&nbsp;&lt;string&gt; | Display server - "xcb", or "wayland" | No | xcb |

### Start Replay
>64-bit trace file needs to be replayed using 64-bit vkreplay.

```
export LD_LIBRARY_PATH=<vktrace dir>/dbuild_<target>/<target>/bin/:$LD_LIBRARY_PATH

vkreplay -o <filename>.vktrace
```
`vkreplay` is in `dbuild_<target>/<target>/bin` after building.

To replay the Vulkan Cube application trace captured in the example above:

```
cd examples/build
vkreplay -o cubetrace.vktrace
```

Output messages from the replay operation are written to `stdout`.

#### Linux Display Server Support

To run vkreplay with a different display server implementation than XCB, the command-line option --DisplayServer (-ds) can be set. Currently, the available options are XCB and WAYLAND.

Example for running on Wayland:
```
vkreplay -o <tracefile> -ds wayland
```

> Disable the trace layer  `VK_LAYER_LUNARG_vktrace` when replaying if you don't want trace it.


#### Acceleration Structure Related Replay Options
Replaying ray query/ray tracing traces need add special parameters.


```
# Replaying with ray tracing capture replay feature:
./vkreplay ... -drcr 0 ...
# the default value of drcr is 0, except drcr is written to vktrace header

# Replaying without ray tracing capture replay feature:
./vkreplay ... -drcr 7  ...
# the drcr option can be used with combined values

# If the buffer device addresses in the memory of buffer need be remapped:
./vkreplay ... -drcr 7 -spc 1 ...
# the drcr option can be used with combined values
```

### Take Screenshots
Screenshots can be taken using screenshot layer.
```
export LD_LIBRARY_PATH=<vktrace dir>/dbuild_<target>/<target>/bin/:$LD_LIBRARY_PATH
export VK_LAYER_PATH=<vktrace dir>/dbuild_<target>/<target>/bin/
export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot

vkreplay -o <filename>.vktrace -s <frame range>
```

The path to the layer's `VkLayer_screenshot.json` file and corresponding `libVkLayer_screenshot.so` library must be added to `VK_LAYER_PATH` environment variable for the Vulkan loader to find the layer. The layer and library are in `dbuild_<target>/<target>/bin` after building.