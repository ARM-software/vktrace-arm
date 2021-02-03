cmake_minimum_required(VERSION 3.10.2)
project(vktrace_common)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/../)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/../)

include_directories(
    ${PROJECT_BINARY_DIR}/../..
    ${SRC_DIR}/vktrace_common
    ${SRC_DIR}/thirdparty
    ${CMAKE_BINARY_DIR}/vktrace
    ${Vulkan-ValidationLayers_INCLUDE_DIR}
    ${SRC_DIR}/../submodules/lz4/lib/
    ${SRC_DIR}/../submodules/snappy/
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    require_pthreads()
endif()

set(SRC_LIST
    ${SRC_LIST}
    vktrace_filelike.c
    vktrace_interconnect.c
    vktrace_platform.c
    vktrace_process.c
    vktrace_settings.c
    vktrace_tracelog.c
    vktrace_trace_packet_utils.c
    vktrace_pageguard_memorycopy.cpp
    ${SRC_DIR}/../submodules/zlib/adler32.c
    ${SRC_DIR}/../submodules/zlib/crc32.c
    ${SRC_DIR}/../submodules/zlib/deflate.c
    ${SRC_DIR}/../submodules/zlib/gzclose.c
    ${SRC_DIR}/../submodules/zlib/gzlib.c
    ${SRC_DIR}/../submodules/zlib/gzread.c
    ${SRC_DIR}/../submodules/zlib/gzwrite.c
    ${SRC_DIR}/../submodules/zlib/inflate.c
    ${SRC_DIR}/../submodules/zlib/infback.c
    ${SRC_DIR}/../submodules/zlib/inftrees.c
    ${SRC_DIR}/../submodules/zlib/inffast.c
    ${SRC_DIR}/../submodules/zlib/trees.c
    ${SRC_DIR}/../submodules/zlib/zutil.c
    ${SRC_DIR}/../submodules/lz4/lib/lz4.c
)

set (CXX_SRC_LIST
     vktrace_pageguard_memorycopy.cpp
     ${JSONCPP_SOURCE_DIR}/jsoncpp.cpp
     compression/compressor.cpp
     compression/decompressor.cpp
     compression/lz4compressor.cpp
     compression/lz4decompressor.cpp
     compression/snpcompressor.cpp
     compression/snpdecompressor.cpp
     ${SRC_DIR}/../submodules/snappy/snappy-c.cc
     ${SRC_DIR}/../submodules/snappy/snappy.cc
     ${SRC_DIR}/../submodules/snappy/snappy-sinksource.cc
)

set_source_files_properties( ${SRC_LIST} PROPERTIES LANGUAGE C)
set_source_files_properties( ${CXX_SRC_LIST} PROPERTIES LANGUAGE CXX)

file( GLOB_RECURSE HDRS *.[h|inl] )

if (NOT MSVC)
    add_c_compiler_flag("-fPIC")
    add_cpp_compiler_flag("-fPIC -std=c++11")
endif()

add_library(${PROJECT_NAME} STATIC ${SRC_LIST} ${CXX_SRC_LIST} ${HDRS})

add_dependencies(${PROJECT_NAME} vktrace_generate_helper_files)

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
target_link_libraries(${PROJECT_NAME}
    Rpcrt4.lib
)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
target_link_Libraries(${PROJECT_NAME}
    dl
    pthread
)
    find_library(LIBRT rt)
    if(LIBRT)
        target_link_libraries(${PROJECT_NAME} ${LIBRT})
    endif()
endif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")

build_options_finalize()

set_target_properties(vktrace_common PROPERTIES LINKER_LANGUAGE C)
