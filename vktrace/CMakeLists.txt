#
# cmake -DCMAKE_BUILD_TYPE=Debug ..
#
#   http://www.cmake.org/Wiki/CMake_FAQ
#   http://www.cmake.org/Wiki/CMake_Useful_Variables
#   http://clang.llvm.org/docs/LanguageExtensions.html
#
#
cmake_minimum_required(VERSION 3.10.2)
PROJECT(vktrace_project)

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../vktrace_version VKTRACE_RELEASE_VERSION)
set(VKTRACE_RELEASE_TYPE "dev unofficial")
if (VKTRACE_RELEASE STREQUAL "TRUE")
    add_definitions(-DVKTRACE_VERSION="${VKTRACE_RELEASE_VERSION}")
else()
    add_definitions(-DVKTRACE_VERSION="${VKTRACE_RELEASE_VERSION} ${VKTRACE_RELEASE_TYPE}")
endif()


set(BUILD_X64 "" CACHE STRING "whether to perform 64-bit build: ON or OFF overrides default detection")

option(CMAKE_VERBOSE "Verbose CMake" FALSE)
if (CMAKE_VERBOSE)
    SET(CMAKE_VERBOSE_MAKEFILE ON)
endif()

# With gcc48: http://indico.cern.ch/getFile.py/access?contribId=1&resId=0&materialId=slides&confId=230762

option(WITH_HARDENING "Enable hardening: Compile-time protection against static sized buffer overflows" OFF)

# Unless user specifies BUILD_X64 explicitly, assume native target
if (BUILD_X64 STREQUAL "")
  if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BUILD_X64 "TRUE")
  else()
    set(BUILD_X64 "FALSE")
  endif()
endif()

# Generate bitness suffix to use, but make sure to include the existing suffix (.exe)
# for platforms that need it (ie, Windows)
if (BUILD_X64)
    set(CMAKE_EXECUTABLE_SUFFIX "${CMAKE_EXECUTABLE_SUFFIX}")
    set(CMAKE_SHARED_LIBRARY_SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}")
else()
    # Don't add the 32 for Windows because it goes in a different location.
    if (MSVC)
        set(CMAKE_EXECUTABLE_SUFFIX "${CMAKE_EXECUTABLE_SUFFIX}")
        set(CMAKE_SHARED_LIBRARY_SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}")
    else()
        set(CMAKE_EXECUTABLE_SUFFIX "32${CMAKE_EXECUTABLE_SUFFIX}")
        set(CMAKE_SHARED_LIBRARY_SUFFIX "32${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()
endif()

# Default to release build
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Make sure we're using 64-bit versions of stat, fopen, etc.
# Large File Support extensions:
#   http://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html#Feature-Test-Macros
add_definitions(-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGE_FILES -DNOMINMAX)

# support for inttypes.h macros
add_definitions(-D__STDC_LIMIT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_CONSTANT_MACROS)

if(MSVC)
    set(CMAKE_CXX_FLAGS_LIST "/W3 /D_CRT_SECURE_NO_WARNINGS=1 /DWIN32 /D_WIN32")
    set(CMAKE_CXX_FLAGS_RELEASE_LIST "/O2 ${CMAKE_CXX_FLAGS_RELEASE_LIST} /DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG_LIST "/Od /D_DEBUG")
else()
    set(CMAKE_CXX_FLAGS_LIST "${CMAKE_CXX_FLAGS} -Wall -Wextra")
    set(CMAKE_CXX_FLAGS_RELEASE_LIST "-O2 ${CMAKE_CXX_FLAGS_RELEASE_LIST} -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG_LIST "-g -O0 -D_DEBUG")
endif()


if (${CMAKE_C_COMPILER_ID} STREQUAL "Clang")

  # clang doesn't print colored diagnostics when invoked from Ninja
  if (UNIX AND CMAKE_GENERATOR STREQUAL "Ninja")
      add_definitions ("-fcolor-diagnostics")
  endif()

  if (CLANG_EVERYTHING)
      set(CMAKE_CXX_FLAGS_LIST ${CMAKE_CXX_FLAGS_LIST}
          # "-pedantic"             # Warn on language extensions
          "-Weverything"            # Enable all warnings
          "-fdiagnostics-show-category=name"
          "-Wno-unused-macros"
          "-Wno-padded"
          "-Wno-variadic-macros"
          "-Wno-missing-variable-declarations"
          "-Wno-missing-prototypes"
          "-Wno-sign-conversion"
          "-Wno-conversion"
          "-Wno-cast-align"
          "-Wno-exit-time-destructors"
          "-Wno-documentation-deprecated-sync"
          "-Wno-documentation-unknown-command"

          # TODO: Would be great to start enabling some of these warnings...
          "-Wno-undefined-reinterpret-cast"
          "-Wno-incompatible-pointer-types-discards-qualifiers"
          "-Wno-float-equal"
          "-Wno-unreachable-code"
          "-Wno-weak-vtables"
          "-Wno-extra-semi"
          "-Wno-disabled-macro-expansion"
          "-Wno-format-nonliteral"
          "-Wno-packed"
          "-Wno-c++11-long-long"
          "-Wno-c++11-extensions"
          "-Wno-gnu-anonymous-struct"
          "-Wno-gnu-zero-variadic-macro-arguments"
          "-Wno-nested-anon-types"
          "-Wno-gnu-redeclared-enum"

          "-Wno-pedantic"
          "-Wno-header-hygiene"
          "-Wno-covered-switch-default"
          "-Wno-duplicate-enum"
          "-Wno-switch-enum"
          "-Wno-extra-tokens"

          # Added because SDL2 headers have a ton of Doxygen warnings currently.
          "-Wno-documentation"

          )
  endif()

endif()


if ((NOT MSVC) AND (NOT BUILD_X64) AND (NOT CMAKE_SIZEOF_VOID_P EQUAL 8) AND (NOT ARCH_ARM))
    set(CMAKE_CXX_FLAGS_LIST "${CMAKE_CXX_FLAGS_LIST} -m32")
    set(CMAKE_EXE_LINK_FLAGS_LIST "${CMAKE_EXE_LINK_FLAGS_LIST} -m32")
    set(CMAKE_SHARED_LINK_FLAGS_LIST "${CMAKE_SHARED_LINK_FLAGS_LIST} -m32")

    set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS OFF)
    set(CMAKE_SYSTEM_LIBRARY_PATH /lib32 /usr/lib32 /usr/lib/i386-linux-gnu /usr/local/lib32)
    set(CMAKE_IGNORE_PATH /lib /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/local/lib)
endif()

function(add_compiler_flag flag)
    set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS}   ${flag}" PARENT_SCOPE)
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${flag}" PARENT_SCOPE)
endfunction()

function(add_c_compiler_flag flag)
    set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS}   ${flag}" PARENT_SCOPE)
endfunction()

function(add_cpp_compiler_flag flag)
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${flag}" PARENT_SCOPE)
endfunction()

function(add_compiler_flag_debug flag)
    set(CMAKE_C_FLAGS_DEBUG    "${CMAKE_C_FLAGS_DEBUG}   ${flag}" PARENT_SCOPE)
    set(CMAKE_CXX_FLAGS_DEBUG  "${CMAKE_CXX_FLAGS_DEBUG} ${flag}" PARENT_SCOPE)
endfunction()

function(add_compiler_flag_release flag)
    set(CMAKE_C_FLAGS_RELEASE    "${CMAKE_C_FLAGS_RELEASE}   ${flag}" PARENT_SCOPE)
    set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} ${flag}" PARENT_SCOPE)
endfunction()


function(add_linker_flag flag)
    set(CMAKE_EXE_LINKER_FLAGS   "${CMAKE_EXE_LINKER_FLAGS} ${flag}" PARENT_SCOPE)
endfunction()

function(add_shared_linker_flag flag)
    set(CMAKE_SHARED_LINKER_FLAGS   "${CMAKE_SHARED_LINKER_FLAGS} ${flag}" PARENT_SCOPE)
endfunction()

#
# To show the include files as you're building, do this:
#    add_compiler_flag("-H")
# For Visual Studio, it's /showIncludes I believe...
#

# stack-protector-strong: http://gcc.gnu.org/ml/gcc-patches/2012-06/msg00974.html
## -fstack-protector-strong
# Compile with the option "-fstack-usage" and a file .su will be generated with stack
# information for each function.
## -fstack-usage

# For more info on -fno-strict-aliasing: "Just Say No to strict aliasing optimizations in C": http://nothings.org/
# The Linux kernel is compiled with -fno-strict-aliasing: https://lkml.org/lkml/2003/2/26/158 or http://www.mail-archive.com/linux-btrfs@vger.kernel.org/msg01647.html

### TODO: see if sse is generated with these instructions and clang:
## -march=corei7 -msse -mfpmath=sse

if (${CMAKE_C_COMPILER_ID} STREQUAL "Clang")
   if ( NOT BUILD_X64 )
      # Fix startup crash in dlopen_notify_callback (called indirectly from our dlopen() function) when tracing glxspheres on my AMD dev box (x86 release only)
      # Also fixes tracing Q3 Arena using release tracer
      # Clang is generating sse2 code even when it shouldn't be:
      #  http://lists.cs.uiuc.edu/pipermail/cfe-dev/2012-March/020310.html
      set(MARCH_STR "-march=i586")
   endif()
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS_LIST
        ${CMAKE_CXX_FLAGS_LIST}
        "/EHsc" # Need exceptions
    )
    set(CMAKE_C_FLAGS_LIST
        ${CMAKE_C_FLAGS_LIST}
    )
else()
    set(CMAKE_CXX_FLAGS_LIST
        ${CMAKE_CXX_FLAGS_LIST}
        "-fno-omit-frame-pointer"
        ${MARCH_STR}
        # "-msse2 -mfpmath=sse" # To build with SSE instruction sets
        "-Wno-unused-parameter -Wno-unused-function"

        "-Wno-missing-field-initializers"
        "-Wno-cast-function-type"
        "-Wno-stringop-overflow"
        "-Wno-unused-result"
        "-Wno-cast-function-type"
        "-Wno-sign-compare"
        "-Wno-switch"
        "-Wno-stringop-truncation"
        "-Wno-unused-variable"
        "-Wno-ignored-qualifiers"
        "-Wno-parentheses"
        "-Wno-implicit-fallthrough"
        "-Wno-type-limits"

        "-fno-strict-aliasing" # DO NOT remove this, we have lots of code that will fail in obscure ways otherwise because it was developed with MSVC first.
        "-fno-math-errno"
        "-fvisibility=hidden"
        # "-fno-exceptions" # Exceptions are enabled by default for c++ files, disabled for c files.
    )
    set(CMAKE_C_FLAGS_LIST
        ${CMAKE_C_FLAGS_LIST}
        "-Wno-implicit-function-declaration"
    )
endif()

if (CMAKE_COMPILER_IS_GNUCC)
    execute_process(COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1} -dumpversion OUTPUT_VARIABLE GCC_VERSION)
    string(REGEX MATCHALL "[0-9]+" GCC_VERSION_COMPONENTS ${GCC_VERSION})
    list(LENGTH GCC_VERSION_COMPONENTS GCC_VERSION_COMPONENTS_LENGTH)
    list(GET GCC_VERSION_COMPONENTS 0 GCC_MAJOR)
    if (${GCC_VERSION_COMPONENTS_LENGTH} EQUAL 2)
        list(GET GCC_VERSION_COMPONENTS 1 GCC_MINOR)
    else()
        set(GCC_MINOR 0)
    endif()
    message(STATUS "Detected GCC v ${GCC_MAJOR} . ${GCC_MINOR}")
endif()

if (GCC_VERSION VERSION_GREATER 4.8 OR GCC_VERSION VERSION_EQUAL 4.8)
    set(CMAKE_CXX_FLAGS_LIST ${CMAKE_CXX_FLAGS_LIST}
        "-Wno-unused-local-typedefs"
    )
endif()

if (MSVC)
else()
    if (WITH_HARDENING)
        # http://gcc.gnu.org/ml/gcc-patches/2004-09/msg02055.html
        add_definitions(-D_FORTIFY_SOURCE=2 -fpic)
        if (${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
            # During program load, several ELF memory sections need to be written to by the
            # linker, but can be turned read-only before turning over control to the
            # program. This prevents some GOT (and .dtors) overwrite attacks, but at least
            # the part of the GOT used by the dynamic linker (.got.plt) is still vulnerable.
            add_definitions(-pie -z now -z relro)
        endif()
    endif()
endif()

if (NOT MSVC)
    if(APPLE)
        set(CMAKE_EXE_LINK_FLAGS_LIST "-Wl,-undefined,error")
    else()
        set(CMAKE_EXE_LINK_FLAGS_LIST "-Wl,--no-undefined")
        set(CMAKE_EXE_LINK_FLAGS_LIST  ${CMAKE_EXE_LINK_FLAGS_LIST} "-Wl,-z,max-page-size=16384")
        set(CMAKE_SHARED_LINK_FLAGS_LIST  ${CMAKE_SHARED_LINK_FLAGS_LIST} "-Wl,-z,max-page-size=16384")
    endif()
endif()

# Compiler flags
string(REPLACE ";" " " CMAKE_C_FLAGS              "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_LIST} ${CMAKE_CXX_FLAGS_LIST}")
string(REPLACE ";" " " CMAKE_C_FLAGS_RELEASE      "${CMAKE_CXX_FLAGS_RELEASE_LIST}")
string(REPLACE ";" " " CMAKE_C_FLAGS_DEBUG        "${CMAKE_CXX_FLAGS_DEBUG_LIST}")

string(REPLACE ";" " " CMAKE_CXX_FLAGS            "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_LIST}")
string(REPLACE ";" " " CMAKE_CXX_FLAGS_RELEASE    "${CMAKE_CXX_FLAGS_RELEASE_LIST}")
string(REPLACE ";" " " CMAKE_CXX_FLAGS_DEBUG      "${CMAKE_CXX_FLAGS_DEBUG_LIST}")

# Linker flags (exe)
string(REPLACE ";" " " CMAKE_EXE_LINKER_FLAGS     "${CMAKE_EXE_LINK_FLAGS_LIST}")
# Linker flags (shared)
string(REPLACE ";" " " CMAKE_SHARED_LINKER_FLAGS  "${CMAKE_SHARED_LINK_FLAGS_LIST}")

function(build_options_finalize)
    if (CMAKE_VERBOSE)
        message("  CMAKE_PROJECT_NAME: ${CMAKE_PROJECT_NAME}")
        message("  PROJECT_NAME: ${PROJECT_NAME}")
        message("  BUILD_X64: ${BUILD_X64}")
        message("  BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
        message("  CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
        message("  PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")
        message("  CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
        message("  PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
        message("  CMAKE_CURRENT_LIST_FILE: ${CMAKE_CURRENT_LIST_FILE}")
        message("  CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
        message("  CXX_FLAGS_RELEASE: ${CMAKE_CXX_FLAGS_RELEASE}")
        message("  CXX_FLAGS_DEBUG: ${CMAKE_CXX_FLAGS_DEBUG}")
        message("  EXE_LINKER_FLAGS: ${CMAKE_EXE_LINKER_FLAGS}")
        message("  SHARED_LINKER_FLAGS: ${CMAKE_SHARED_LINKER_FLAGS}")
        message("  SHARED_LIBRARY_C_FLAGS: ${CMAKE_SHARED_LIBRARY_C_FLAGS}")
        message("  SHARED_LIBRARY_CXX_FLAGS: ${CMAKE_SHARED_LIBRARY_CXX_FLAGS}")
        message("  SHARED_LIBRARY_LINK_CXX_FLAGS: ${CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS}")
        message("  SHARED_LIBRARY_LINK_C_FLAGS: ${CMAKE_SHARED_LIBRARY_LINK_C_FLAGS}")
        message("  CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
        message("  CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
        message("  CMAKE_C_COMPILER_ID: ${CMAKE_C_COMPILER_ID}")
        message("  CMAKE_EXECUTABLE_SUFFIX: ${CMAKE_EXECUTABLE_SUFFIX}")
        message("")
    endif()
endfunction()

function(require_pthreads)
    find_package(Threads)
    if (NOT CMAKE_USE_PTHREADS_INIT AND NOT WIN32_PTHREADS_INCLUDE_PATH)
        message(FATAL_ERROR "pthread not found")
    endif()

    if (MSVC)
        include_directories("${WIN32_PTHREADS_INCLUDE_PATH}")
        if (BUILD_X64)
            set(PTHREAD_SRC_LIB "${WIN32_PTHREADS_PATH}/lib/x64/pthreadVC2.lib" PARENT_SCOPE)
            set(PTHREAD_SRC_DLL "${WIN32_PTHREADS_PATH}/dll/x64/pthreadVC2.dll" PARENT_SCOPE)
        else()
            set(PTHREAD_SRC_LIB "${WIN32_PTHREADS_PATH}/lib/x86/pthreadVC2.lib" PARENT_SCOPE)
            set(PTHREAD_SRC_DLL "${WIN32_PTHREADS_PATH}/dll/x86/pthreadVC2.dll" PARENT_SCOPE)
        endif()

    else()
        # Export the variable to the parent scope so the linker knows where to find the library.
        set(CMAKE_THREAD_LIBS_INIT ${CMAKE_THREAD_LIBS_INIT} PARENT_SCOPE)
    endif()
endfunction()

function(require_libjpegturbo)
    find_library(LibJpegTurbo_LIBRARY
        NAMES libturbojpeg.so libturbojpeg.so.0 libturbojpeg.dylib
        PATHS /opt/libjpeg-turbo/lib
    )

    # On platforms that find this, the include files will have also been installed to the system
    # so we don't need extra include dirs.
    if (LibJpegTurbo_LIBRARY)
        set(LibJpegTurbo_INCLUDE "" PARENT_SCOPE)
    else()
        if (BUILD_X64)
            set(BITS_STRING "x64")
        else()
            set(BITS_STRING "x86")
        endif()
        set(LibJpegTurbo_INCLUDE "${CMAKE_PREFIX_PATH}/libjpeg-turbo-2.1.3/include" PARENT_SCOPE)
        set(LibJpegTurbo_LIBRARY "${CMAKE_PREFIX_PATH}/libjpeg-turbo-2.1.3/lib_${BITS_STRING}/turbojpeg.lib" PARENT_SCOPE)
    endif()
endfunction()

function(require_sdl2)
    if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        include(FindPkgConfig)
        pkg_search_module(PC_SDL2 REQUIRED sdl2)

        find_path(SDL2_INCLUDE SDL.h
            DOC "SDL2 Include Path"
	    HINTS ${PC_SDL2_INCLUDEDIR} ${PC_SDL2_INCLUDE_DIRS} )

        find_library(SDL2_LIBRARY SDL2
            DOC "SDL2 Library"
	    HINTS ${PC_SDL2_LIBDIR} ${PC_SDL2_LIBRARY_DIRS} )
    elseif (MSVC)
        set(SDL2Root "${CMAKE_EXTERNAL_PATH}/SDL")

        set(SDL2_INCLUDE "${SDL2Root}/include" CACHE PATH "SDL2 Include Path")
        set(SDL2_LIBRARY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}/SDL2.lib" CACHE FILEPATH "SDL2 Library")

        # Only want to include this once.
        # This has to go into properties because it needs to persist across the entire cmake run.
        get_property(SDL_PROJECT_ALREADY_INCLUDED
            GLOBAL
            PROPERTY SDL_PROJECT_INCLUDED
            )

        if (NOT SDL_PROJECT_ALREADY_INCLUDED)
            INCLUDE_EXTERNAL_MSPROJECT(SDL "${SDL2Root}/VisualC/SDL/SDL_VS2013.vcxproj")
            set_property(GLOBAL
                PROPERTY SDL_PROJECT_INCLUDED "TRUE"
                )
            message("Including SDL_VS2013.vcxproj for you!")
        endif()

    else()
        message(FATAL_ERROR "Need to deal with SDL on non-Windows platforms")
    endif()
endfunction()

function(require_m)
    if (MSVC)
	set(M_LIBRARY "winmm.lib" PARENT_SCOPE)
    else()
	set(M_LIBRARY "m" PARENT_SCOPE)
    endif()
endfunction()

function(require_gl)
    if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	include(FindPkgConfig)
	pkg_search_module(PC_GL QUIET gl)

	find_path(GL_INCLUDE GL/gl.h
	    DOC "OpenGL Include Path"
	    HINTS ${PC_GL_INCLUDEDIR} ${PC_GL_INCLUDE_DIRS} )

	find_library(GL_LIBRARY GL
	    DOC "OpenGL Library"
	    HINTS ${PC_GL_LIBDIR} ${PC_GL_LIBRARY_DIRS} )
    elseif (MSVC)
	set(GL_INCLUDE ""	      CACHE PATH "OpenGL Include Path")
	set(GL_LIBRARY "opengl32.lib" CACHE FILEPATH "OpenGL Library")
    else()
	set(GL_INCLUDE ""   CACHE PATH "OpenGL Include Path")
	set(GL_LIBRARY "GL" CACHE FILEPATH "OpenGL Library")
    endif()
endfunction()

function(require_glu)
    if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	include(FindPkgConfig)
	pkg_search_module(PC_GLU QUIET glu)

	find_path(GLU_INCLUDE GL/glu.h
	    DOC "GLU Include Path"
	    HINTS ${PC_GLU_INCLUDEDIR} ${PC_GLU_INCLUDE_DIRS} )

	find_library(GLU_LIBRARY GLU
	    DOC "GLU Library"
	    HINTS ${PC_GLU_LIBDIR} ${PC_GLU_LIBRARY_DIRS} )
    elseif (MSVC)
	set(GLU_INCLUDE ""	    CACHE PATH "GLU Include Path")
	set(GLU_LIBRARY "glu32.lib" CACHE FILEPATH "GLU Library")
    else()
	set(GLU_INCLUDE ""    CACHE PATH "GLU Include Path")
	set(GLU_LIBRARY "GLU" CACHE FILEPATH "GLU Library")
    endif()
endfunction()

function(request_backtrace)
    if (NOT MSVC)
        set( LibBackTrace_INCLUDE "${SRC_DIR}/libbacktrace" PARENT_SCOPE )
        set( LibBackTrace_LIBRARY "backtracevogl" PARENT_SCOPE )
    else()
        set( LibBackTrace_INCLUDE "" PARENT_SCOPE )
        set( LibBackTrace_LIBRARY "" PARENT_SCOPE )
    endif()
endfunction()

# What compiler toolchain are we building on?
if (${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
    add_compiler_flag("-DCOMPILER_GCC=1")
    add_compiler_flag("-DCOMPILER_GCCLIKE=1")
elseif (${CMAKE_C_COMPILER_ID} STREQUAL "mingw")
    add_compiler_flag("-DCOMPILER_MINGW=1")
    add_compiler_flag("-DCOMPILER_GCCLIKE=1")
elseif (MSVC)
    add_compiler_flag("-DCOMPILER_MSVC=1")
elseif (${CMAKE_C_COMPILER_ID} STREQUAL "Clang")
    add_compiler_flag("-DCOMPILER_CLANG=1")
    add_compiler_flag("-DCOMPILER_GCCLIKE=1")
else()
    message("Compiler is ${CMAKE_C_COMPILER_ID}")
    message(FATAL_ERROR "Compiler unset, build will fail--stopping at CMake time.")
endif()

# Platform specific library defines.
if (WIN32)
    set( LIBRT "" )
    set( LIBDL "" )

elseif (UNIX)
    set( LIBRT rt )
    set( LIBDL dl )
else()
    message(FATAL_ERROR "Need to determine what the library name for 'rt' is for non-windows, non-unix platforms (or if it's even needed).")
endif()

# What OS will we be running on?
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_compiler_flag("-DPLATFORM_OSX=1")
    add_compiler_flag("-DPLATFORM_POSIX=1")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_compiler_flag("-DPLATFORM_LINUX=1")
    add_compiler_flag("-DPLATFORM_POSIX=1")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_compiler_flag("-DPLATFORM_WINDOWS=1")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Android")
    add_compiler_flag("-DPLATFORM_ANDROID=1")
else()
    message(FATAL_ERROR "Platform unset, build will fail--stopping at CMake time.")
endif()

if (ARCH_ARM)
    if (BUILD_X64)
        add_compiler_flag("-DARM_ARCH=8")
    else()
        add_compiler_flag("-DARM_ARCH=7")
    endif()
endif()

# What bittedness are we building?
if (BUILD_X64)
    add_compiler_flag("-DPLATFORM_64BIT=1")
else()
    add_compiler_flag("-DPLATFORM_32BIT=1")
endif()

# Compiler flags for windows.
if (MSVC)
    # Multithreaded compilation is a big time saver.
    add_compiler_flag("/MP")

    # In debug, we use the DLL debug runtime.
    add_compiler_flag_debug("/MDd")

    # And in release we use the DLL release runtime
    add_compiler_flag_release("/MD")

    # Generate debug information
    add_compiler_flag("/Zi")

    # And tell the linker to always generate the file for us.
    add_linker_flag("/DEBUG")
else()
    add_linker_flag("-lrt")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_definitions(-DVK_USE_PLATFORM_WIN32_KHR -DWIN32_LEAN_AND_MEAN)
    set(DisplayServer Win32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_definitions(-DVK_USE_PLATFORM_ANDROID_KHR)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    if (BUILD_WSI_XCB_SUPPORT)
        add_definitions(-DVK_USE_PLATFORM_XCB_KHR)
    endif()

    if (BUILD_WSI_XLIB_SUPPORT)
        add_definitions(-DVK_USE_PLATFORM_XLIB_KHR)
    endif()

    if (BUILD_WSI_WAYLAND_SUPPORT)
        add_definitions(-DVK_USE_PLATFORM_WAYLAND_KHR)
    endif()

    add_definitions(-DVK_USE_PLATFORM_HEADLESS_ARM)
    add_definitions(-DVK_USE_PLATFORM_HEADLESS_EXT)

elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # Only vktrace is supported on macOS
else()
    message(FATAL_ERROR "Unsupported Platform!")
endif()

add_definitions(-DVK_ENABLE_BETA_EXTENSIONS)

if (NOT ARCH_ARM)
    option(BUILD_VKTRACEVIEWER "Build VkTraceViewer" ON)
endif()

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

#set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${SRC_DIR}/cmake/Modules/")
#set(CMAKE_EXTERNAL_PATH "${SRC_DIR}/../../external")

if (WIN32)
    # TODO: s/CMAKE_PREFIX_PATH/CMAKE_EXTERNAL_WINDOWS_PATH/g
#    set(CMAKE_PREFIX_PATH "${CMAKE_EXTERNAL_PATH}/windows")
    set(WIN32_PTHREADS_PATH "${SRC_DIR}/thirdparty/pthreads.2")
    set(WIN32_PTHREADS_INCLUDE_PATH "${WIN32_PTHREADS_PATH}/include")
endif()

find_package(PythonInterp)

if (NOT PYTHONINTERP_FOUND)
    message(FATAL_ERROR "Missing PythonInterp. Install python interpreter 2.7 (on linux use cmd: sudo apt-get install python2.7)")
endif()

#search for QT only if BUILD_VKTRACEVIEWER is ON
if(BUILD_VKTRACEVIEWER)
    find_package(Qt5 COMPONENTS Widgets Gui Core Svg QUIET)
    if (NOT Qt5_FOUND)
        if (WIN32)
            message("WARNING: Qt5 dev libraries not found, vktraceviewer will not be built.\n   To enable build of vktraceviewer, set env var Qt5_Dir to\n   C:\\Qt\\5.3\\msvc2013_64\\lib\\cmake\\Qt5 or C:\\Qt\\5.3\\msvc2013\\lib\\cmake\\Qt5")
        else()
            message("WARNING: Qt5 dev libraries not found, vktraceviewer will not be built.\n   To enable build of vktraceviewer, install package qt5-default.")
        endif()
    endif()
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/submodules/zlib
)

#
#  Components to build
#
set(VKTRACE_VULKAN_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(GENERATED_FILES_DIR ${CMAKE_BINARY_DIR}/vktrace)

# Initial build of vktrace generated files when running cmake.
# Need to generate the files here because cmake of vktrace subdirectories depends on the generated files already existing.
if(NOT EXISTS ${GENERATED_FILES_DIR}/vk_struct_size_helper.h)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vk_struct_size_helper.h)
endif()
if(NOT EXISTS ${GENERATED_FILES_DIR}/vk_struct_size_helper.c)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vk_struct_size_helper.c)
endif()
if(NOT EXISTS ${GENERATED_FILES_DIR}/vktrace_vk_packet_id.h)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vktrace_vk_packet_id.h)
endif()
if(NOT EXISTS ${GENERATED_FILES_DIR}/vktrace_vk_vk_packets.h)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vktrace_vk_vk_packets.h)
endif()
if(NOT EXISTS ${GENERATED_FILES_DIR}/vktrace_vk_vk.h)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vktrace_vk_vk.h)
endif()
if(NOT EXISTS ${GENERATED_FILES_DIR}/vktrace_vk_vk.cpp)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vktrace_vk_vk.cpp)
endif()
if(NOT EXISTS ${GENERATED_FILES_DIR}/vkreplay_vk_replay_gen.cpp)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vkreplay_vk_replay_gen.cpp)
endif()
if(NOT EXISTS ${GENERATED_FILES_DIR}/vkreplay_vk_objmapper.h)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vkreplay_vk_objmapper.h)
endif()
if(NOT EXISTS ${GENERATED_FILES_DIR}/vkreplay_vk_func_ptrs.h)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vkreplay_vk_func_ptrs.h)
endif()
if(NOT EXISTS ${GENERATED_FILES_DIR}/vk_struct_member.h)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vk_struct_member.h)
endif()

# Command and target to build vktrace generated files from Makefile or VisualStudio.
# This makes sure that if (and only if) a dependency is modified, the files are regenerated.
add_custom_command(
    OUTPUT ${GENERATED_FILES_DIR}/vk_struct_size_helper.h ${GENERATED_FILES_DIR}/vk_struct_size_helper.c ${GENERATED_FILES_DIR}/vktrace_vk_packet_id.h
           ${GENERATED_FILES_DIR}/vktrace_vk_vk_packets.h ${GENERATED_FILES_DIR}/vktrace_vk_vk.h ${GENERATED_FILES_DIR}/vktrace_vk_vk.cpp
           ${GENERATED_FILES_DIR}/vkreplay_vk_replay_gen.cpp ${GENERATED_FILES_DIR}/vkreplay_vk_objmapper.h ${GENERATED_FILES_DIR}/vkreplay_vk_func_ptrs.h
    DEPENDS ${VULKANTOOLS_SCRIPTS_DIR}/vktrace_file_generator.py ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py ${VulkanRegistry_DIR}/vk.xml
    COMMENT Generating vktrace helper files
    COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vk_struct_size_helper.h
    COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vk_struct_size_helper.c
    COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vktrace_vk_packet_id.h
    COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vktrace_vk_vk_packets.h
    COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vktrace_vk_vk.h
    COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vktrace_vk_vk.cpp
    COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vkreplay_vk_replay_gen.cpp
    COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vkreplay_vk_objmapper.h
    COMMAND ${PYTHON_EXECUTABLE} -B ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vkreplay_vk_func_ptrs.h
    )
add_custom_target(vktrace_generate_helper_files
    DEPENDS ${VULKANTOOLS_SCRIPTS_DIR}/vktrace_file_generator.py ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py ${VulkanRegistry_DIR}/vk.xml
    )

add_subdirectory(vktrace_common)
add_subdirectory(vktrace_trace)
add_subdirectory(vktrace_dump)
add_subdirectory(vktrace_rq_pp)

option(BUILD_VKTRACE_LAYER "Build vktrace_layer" ON)
if(BUILD_VKTRACE_LAYER)
    add_subdirectory(vktrace_layer)
endif()
if(BUILD_VKTRACE_REPLAY)
    add_subdirectory(vktrace_replay)
endif()

# Only build vktraceviewer if Qt5 is available
if (Qt5_FOUND AND BUILD_VKTRACEVIEWER)
    add_subdirectory(vktrace_viewer)
endif()

# use macro from stackoverflow (link below) to get all the extensions that are on the current system
# http://stackoverflow.com/questions/7787823/cmake-how-to-get-the-name-of-all-subdirectories-of-a-directory
MACRO(SUBDIRLIST result curdir)
  FILE(GLOB children RELATIVE ${curdir} ${curdir}/*)
  SET(dirlist "")
  FOREACH(child ${children})
    IF(IS_DIRECTORY ${curdir}/${child})
        LIST(APPEND dirlist ${child})
    ENDIF()
  ENDFOREACH()
  SET(${result} ${dirlist})
ENDMACRO()
