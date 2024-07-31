project(vktracerqpp)
cmake_minimum_required(VERSION 3.0)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/../)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/../)

set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "${SRC_DIR}/cmake/Modules/")
set(GENERATED_FILES_DIR ${CMAKE_BINARY_DIR}/vktrace)

# Run a codegen script to generate vktrace-specific vulkan utils
if(NOT EXISTS ${GENERATED_FILES_DIR}/vktrace_vk_packet_id.h)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vktrace_vk_packet_id.h)
endif()
if(NOT EXISTS ${GENERATED_FILES_DIR}/vktrace_vk_vk_packets.h)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} ${VULKANTOOLS_SCRIPTS_DIR}/vt_genvk.py -registry ${VulkanRegistry_DIR}/vk.xml -scripts ${VulkanRegistry_DIR} -o ${GENERATED_FILES_DIR} vktrace_vk_vk_packets.h)
endif()

set(SRC_LIST
    ${SRC_LIST}
    vktrace_rq_pp_main.cpp
    find_as_unmap.cpp
    find_as_flush.cpp
    find_sbt.cpp
    exclude_as.cpp
    as_dedicate_memory.cpp
    remove_capture_replay_bit.cpp
    remove_unused_memory.cpp
    remove_dummy_build_as.cpp
    ${JSONCPP_SOURCE_DIR}/jsoncpp.cpp
)

include_directories(
    ${GENERATED_FILES_DIR}
    ${SRC_DIR}
    ${SRC_DIR}/vktrace_common
    ${SRC_DIR}/thirdparty
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${VKTRACE_VULKAN_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}
    ${Vulkan-ValidationLayers_INCLUDE_DIR}
    ${JSONCPP_INCLUDE_DIR}
)

if (${CMAKE_C_COMPILER_ID} STREQUAL "Clang")
    add_compiler_flag("-Wno-global-constructors -Wno-used-but-marked-unused")
endif()

# Platform specific compile flags.
if (NOT MSVC)
    add_compiler_flag("-fPIC")
endif()

add_executable(${PROJECT_NAME} ${SRC_LIST} ${HEADER_LIST})

add_compiler_flag("-std=c++17")

add_dependencies(${PROJECT_NAME} vktrace_generate_helper_files)

target_link_libraries(${PROJECT_NAME}
    vktrace_common
    ${CMAKE_DL_LIBS}
)

build_options_finalize()

