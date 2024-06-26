file(GLOB TEXTURES
   "${PROJECT_SOURCE_DIR}/via/images/*"
)
file(COPY ${TEXTURES} DESTINATION ${CMAKE_BINARY_DIR}/via/images)

if(WIN32)

    # Use static MSVCRT libraries
    foreach(configuration in CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
                             CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        if(${configuration} MATCHES "/MD")
            string(REGEX REPLACE "/MD" "/MT" ${configuration} "${${configuration}}")
        endif()
    endforeach()

    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES")
    add_definitions(-DVIA_WINDOWS_TARGET)

else()
    if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(COMMON_COMPILE_FLAGS "-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers")
        set(COMMON_COMPILE_FLAGS "${COMMON_COMPILE_FLAGS} -fno-strict-aliasing -fno-builtin-memcmp")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_COMPILE_FLAGS} -fno-rtti")
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_COMPILE_FLAGS} -std=c++17")

    if (BUILD_WSI_XCB_SUPPORT)
        find_package(XCB REQUIRED)

        include_directories(${XCB_INCLUDE_DIRS})
        link_libraries(${XCB_LIBRARIES})
    endif()
    if (BUILD_WSI_XLIB_SUPPORT)
        find_package(X11 REQUIRED)

        include_directories(${X11_INCLUDE_DIRS})
        link_libraries(${X11_LIBRARIES})
    endif()
    if (BUILD_WSI_WAYLAND_SUPPORT)
        find_package(Wayland REQUIRED)

        include_directories(${WAYLAND_CLIENT_INCLUDE_DIR})
        link_libraries(${WAYLAND_CLIENT_LIBRARIES})
    endif()

    # Disable the RPATH for VIA because we want it to use
    # the environment setup by the user
    SET(CMAKE_SKIP_BUILD_RPATH  TRUE)

    add_definitions(-DVIA_LINUX_TARGET)
endif()

if(WIN32)
add_executable(vkvia
                  via.cpp
                  via_system.hpp
                  via_system.cpp
                  via_system_windows.hpp
                  via_system_windows.cpp
                  ${JSONCPP_SOURCE_DIR}/jsoncpp.cpp)
else()
add_executable(vkvia
                  via.cpp
                  via_system.hpp
                  via_system.cpp
                  via_system_linux.hpp
                  via_system_linux.cpp
                  ${JSONCPP_SOURCE_DIR}/jsoncpp.cpp)
endif()

target_include_directories(vkvia PUBLIC ${JSONCPP_INCLUDE_DIR})
target_link_libraries(vkvia Vulkan::Vulkan)
if(WIN32)
    target_link_libraries(vkvia version shlwapi Cfgmgr32)
else()
    target_link_libraries(vkvia dl)
endif()
if(UNIX)
    install(TARGETS vkvia DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
