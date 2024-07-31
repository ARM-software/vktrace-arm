/*
 *
 * Copyright (C) 2015-2017 Valve Corporation
 * Copyright (C) 2015-2017 LunarG, Inc.
 * Copyright (C) 2017-2024 ARM Limited
 * All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Peter Lohrmann <peterl@valvesoftware.com>
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: Mark Lobodzinski <mark@lunarg.com>
 * Author: Joey Bzdek <joey@lunarg.com>
 */

#pragma once

#include <stdexcept>
#include <vector>

#include "vkreplay_window.h"
#if !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NV_acquire_winrt_display 1
#define VK_NV_ACQUIRE_WINRT_DISPLAY_SPEC_VERSION 1
#define VK_NV_ACQUIRE_WINRT_DISPLAY_EXTENSION_NAME "VK_NV_acquire_winrt_display"
typedef VkResult (VKAPI_PTR *PFN_vkAcquireWinrtDisplayNV)(VkPhysicalDevice physicalDevice, VkDisplayKHR display);
typedef VkResult (VKAPI_PTR *PFN_vkGetWinrtDisplayNV)(VkPhysicalDevice physicalDevice, uint32_t deviceRelativeId, VkDisplayKHR* pDisplay);
#ifndef VK_NO_PROTOTYPES
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireWinrtDisplayNV(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display);
VKAPI_ATTR VkResult VKAPI_CALL vkGetWinrtDisplayNV(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    deviceRelativeId,
    VkDisplayKHR*                               pDisplay);
#endif
#define undef_VK_USE_PLATFORM_WIN32_KHR
#endif

#if !defined(VK_USE_PLATFORM_XLIB_KHR)
#define VK_USE_PLATFORM_XLIB_KHR
#define undef_VK_USE_PLATFORM_XLIB_KHR
#endif

#if !defined(VK_USE_PLATFORM_XCB_KHR)
#define VK_USE_PLATFORM_XCB_KHR
#define undef_VK_USE_PLATFORM_XCB_KHR
#endif

#if !defined(VK_USE_PLATFORM_WAYLAND_KHR)
#define VK_USE_PLATFORM_WAYLAND_KHR
#define undef_VK_USE_PLATFORM_WAYLAND_KHR
#endif

#if !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR
#define undef_VK_USE_PLATFORM_ANDROID_KHR
#endif
#include "vk_layer_dispatch_table.h"

#if defined(undef_VK_USE_PLATFORM_WIN32_KHR)
#undef VK_USE_PLATFORM_WIN32_KHR
#endif

#if defined(undef_VK_USE_PLATFORM_XLIB_KHR)
#undef VK_USE_PLATFORM_XLIB_KHR
#endif

#if defined(undef_VK_USE_PLATFORM_XCB_KHR)
#undef VK_USE_PLATFORM_XCB_KHR
#endif

#if defined(undef_VK_USE_PLATFORM_WAYLAND_KHR)
#undef VK_USE_PLATFORM_WAYLAND_KHR
#endif

#if defined(undef_VK_USE_PLATFORM_ANDROID_KHR)
#undef VK_USE_PLATFORM_ANDROID_KHR
#endif


enum VkDisplayType {
    VK_DISPLAY_XCB,
    VK_DISPLAY_XLIB,
    VK_DISPLAY_WAYLAND,
    VK_DISPLAY_NONE,
};

int GetDisplayImplementation(const char *displayServer, vktrace_replay::ReplayDisplayImp **ppDisp);

#if defined(PLATFORM_LINUX) && defined(VK_USE_PLATFORM_XCB_KHR)

class vkDisplayXcb : public vktrace_replay::ReplayDisplayImp {
   public:
    vkDisplayXcb();
    ~vkDisplayXcb();
    int init(const unsigned int gpu_idx) override;
    int create_window(const unsigned int width, const unsigned int height) override;
    void resize_window(const unsigned int width, const unsigned int height, VkSurfaceTransformFlagBitsKHR rot, const VkSurfaceCapabilitiesKHR& surf_caps) override;
    void process_event() override;
    bool get_pause_status() override { return m_pause; }
    void set_pause_status(bool pause) override { m_pause = pause; }
    bool get_quit_status() override { return m_quit; }
    void set_quit_status(bool quit) override { m_quit = quit; }
    VkSurfaceKHR get_surface() override { return (VkSurfaceKHR)&m_surface; };
    void set_window_handle(void *pHandle) override;

    xcb_window_t get_window_handle() { return m_XcbWindow; }
    xcb_connection_t* get_connection_handle() { return m_pXcbConnection; }
    xcb_screen_t* get_screen_handle() { return m_pXcbScreen; }

   private:
    VkResult init_vk(const unsigned int gpu_idx);
    VkIcdSurfaceXcb m_surface;
    xcb_connection_t *m_pXcbConnection;
    xcb_screen_t *m_pXcbScreen;
    xcb_window_t m_XcbWindow;
    xcb_intern_atom_reply_t *atom_wm_delete_window;

    unsigned int m_windowWidth;
    unsigned int m_windowHeight;
    unsigned int m_screenWidth;
    unsigned int m_screenHeight;
    std::vector<VkExtent2D> imageExtents;
    std::vector<VkImage> imageHandles;
    std::vector<VkDeviceMemory> imageMemory;
    std::vector<VkDevice> imageDevice;
#if 0
    VK_DEVICE m_dev[VK_MAX_PHYSICAL_GPUS];
    uint32_t m_gpuCount;
    unsigned int m_gpuIdx;
    VK_PHYSICAL_GPU m_gpus[VK_MAX_PHYSICAL_GPUS];
    VK_PHYSICAL_GPU_PROPERTIES m_gpuProps[VK_MAX_PHYSICAL_GPUS];
#endif
    std::vector<char *> m_extensions;
    bool m_pause = false;
    bool m_quit = false;
};
#endif

#if defined(PLATFORM_LINUX) && defined(VK_USE_PLATFORM_WAYLAND_KHR)
#include <xdg-shell-client-protocol.h>
class vkDisplayWayland : public vktrace_replay::ReplayDisplayImp {
   public:
    vkDisplayWayland();
    ~vkDisplayWayland();
    int init(const unsigned int gpu_idx) override;
    int create_window(const unsigned int width, const unsigned int height) override;
    void resize_window(const unsigned int width, const unsigned int height, VkSurfaceTransformFlagBitsKHR rot, const VkSurfaceCapabilitiesKHR& surf_caps) override;
    void process_event() override;
    bool get_pause_status() override { return m_pause; }
    void set_pause_status(bool pause) override { m_pause = pause; }
    bool get_quit_status() override { return m_quit; }
    void set_quit_status(bool quit) override { m_quit = quit; }
    VkSurfaceKHR get_surface() override { return (VkSurfaceKHR)&m_surface; };
    void set_window_handle(void *pHandle) override;
    wl_display *get_display_handle() { return m_display; }
    unsigned int m_windowWidth = 0;
    unsigned int m_windowHeight = 0;

   private:
    VkIcdSurfaceWayland m_surface;
    struct wl_display *m_display = nullptr;
    struct wl_registry *m_registry = nullptr;
    struct wl_compositor *m_compositor = nullptr;
    struct wl_surface *window = nullptr;
    struct xdg_wm_base *m_xdg_shell = nullptr;
    struct xdg_surface *window_surface = nullptr;
    struct wl_seat *m_seat = nullptr;
    struct wl_pointer *m_pointer = nullptr;
    struct wl_keyboard *m_keyboard = nullptr;
    struct xdg_toplevel *window_toplevel = nullptr;
    struct wl_output *m_output = nullptr;

    static void handle_ping(void *data, struct xdg_wm_base *xdg_shell, uint32_t serial);
    static struct xdg_wm_base_listener xdg_shell_listener; //handle_ping
    static void handle_configure(void *data, struct xdg_surface *surface, uint32_t serial);

    static struct xdg_surface_listener surface_listener; //handle_configure
    static void handle_toplevel_configure(void *data, xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states);
    static void handle_toplevel_close(void *data, xdg_toplevel *xdg_toplevel);

    static struct xdg_toplevel_listener toplevel_listener;
    static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy);
    static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface);
    static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy);
    static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
    static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
    static struct wl_pointer_listener pointer_listener;
    static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int fd, uint32_t size);
    static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
    static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface);
    static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    static void keyboard_handle_modifiers(void *data, wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group);
    static struct wl_keyboard_listener keyboard_listener;
    static void seat_handle_capabilities(void *data, wl_seat *seat, uint32_t caps);
    static struct wl_seat_listener seat_listener;
    static void global_registry_handler(void *data, wl_registry *registry, uint32_t id, const char *interface, uint32_t version);
    static void global_registry_remover(void *data, wl_registry *registry, uint32_t id);
    static struct wl_registry_listener registry_listener;
    static void output_handle_geometry(void *data, struct wl_output *wl_output, int x, int y, int physical_width,
                                       int physical_height, int subpixel, const char *make, const char *model, int transform);
    static void output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags, int width, int height, int refresh);
    static struct wl_output_listener output_listener;

    unsigned int m_screenWidth = 0;
    unsigned int m_screenHeight = 0;
    unsigned int m_refresh = 0;
    std::vector<VkExtent2D> imageExtents;
    std::vector<VkImage> imageHandles;
    std::vector<VkDeviceMemory> imageMemory;
    std::vector<VkDevice> imageDevice;
    std::vector<char *> m_extensions;
    bool m_pause = false;
    bool m_quit = false;
};
#endif

#if defined(PLATFORM_LINUX) && defined(ANDROID)

class vkDisplayAndroid : public vktrace_replay::ReplayDisplayImp {
   public:
    vkDisplayAndroid(struct android_app *app);
    ~vkDisplayAndroid();
    int init(const unsigned int gpu_idx) override;
    int create_window(const unsigned int width, const unsigned int height) override;
    void resize_window(const unsigned int width, const unsigned int height, VkSurfaceTransformFlagBitsKHR rot, const VkSurfaceCapabilitiesKHR& surf_caps) override;
    void process_event() override;
    bool get_pause_status() override { return m_pause; }
    void set_pause_status(bool pause) override { m_pause = pause; }
    bool get_quit_status() override { return m_quit; }
    void set_quit_status(bool quit) override { m_quit = quit; }
    VkSurfaceKHR get_surface() override { return (VkSurfaceKHR)&m_surface; };
    void set_window_handle(void *pHandle) override;

    ANativeWindow *get_window_handle() { return m_window; }

   private:
    VkIcdSurfaceAndroid m_surface;
    ANativeWindow *m_window;
    struct android_app *m_android_app;

    unsigned int m_windowWidth;
    unsigned int m_windowHeight;
    VkSurfaceTransformFlagBitsKHR m_currentRot;
    std::vector<VkExtent2D> imageExtents;
    std::vector<VkImage> imageHandles;
    std::vector<VkDeviceMemory> imageMemory;
    std::vector<VkDevice> imageDevice;
#if 0
    VK_DEVICE m_dev[VK_MAX_PHYSICAL_GPUS];
    uint32_t m_gpuCount;
    unsigned int m_gpuIdx;
    VK_PHYSICAL_GPU m_gpus[VK_MAX_PHYSICAL_GPUS];
    VK_PHYSICAL_GPU_PROPERTIES m_gpuProps[VK_MAX_PHYSICAL_GPUS];
#endif
    std::vector<char *> m_extensions;
    bool m_pause = false;
    bool m_quit = false;
};
#endif

#if defined(WIN32)
class vkDisplayWin32 : public vktrace_replay::ReplayDisplayImp {
   public:
    vkDisplayWin32();
    ~vkDisplayWin32();
    int init(const unsigned int gpu_idx) override;
    int create_window(const unsigned int width, const unsigned int height) override;
    void resize_window(const unsigned int width, const unsigned int height, VkSurfaceTransformFlagBitsKHR rot, const VkSurfaceCapabilitiesKHR& surf_caps) override;
    void process_event() override;
    bool get_pause_status() override { return m_pause; }
    void set_pause_status(bool pause) override { m_pause = pause; }
    bool get_quit_status() override { return m_quit; }
    void set_quit_status(bool quit) override { m_quit = quit; }
    VkSurfaceKHR get_surface() override { return (VkSurfaceKHR)&m_surface; };
    void set_window_handle(void *pHandle) override;

    HWND get_window_handle() { return m_windowHandle; }
    HINSTANCE get_connection_handle() { return m_connection; }

   private:
    VkIcdSurfaceWin32 m_surface;
    HWND m_windowHandle;
    HINSTANCE m_connection;

    unsigned int m_windowWidth;
    unsigned int m_windowHeight;
    unsigned int m_screenWidth;
    unsigned int m_screenHeight;

    std::vector<VkExtent2D> imageExtents;
    std::vector<VkImage> imageHandles;
    std::vector<VkDeviceMemory> imageMemory;
    std::vector<VkDevice> imageDevice;
#if 0
    VK_DEVICE m_dev[VK_MAX_PHYSICAL_GPUS];
    uint32_t m_gpuCount;
    unsigned int m_gpuIdx;
    VK_PHYSICAL_GPU m_gpus[VK_MAX_PHYSICAL_GPUS];
    VK_PHYSICAL_GPU_PROPERTIES m_gpuProps[VK_MAX_PHYSICAL_GPUS];
#endif
    std::vector<char*> m_extensions;
    bool m_pause = false;
    bool m_quit = false;
};
#endif

#if defined(PLATFORM_LINUX) && !defined(ANDROID)
class vkDisplay : public vktrace_replay::ReplayDisplayImp {
   public:
    vkDisplay();
    ~vkDisplay();
    int init(const unsigned int gpu_idx) override;
    int create_window(const unsigned int width, const unsigned int height) override;
    void resize_window(const unsigned int width, const unsigned int height, VkSurfaceTransformFlagBitsKHR rot, const VkSurfaceCapabilitiesKHR& surf_caps) override;
    void process_event() override { return; };
    bool get_pause_status() override { return m_pause; }
    void set_pause_status(bool pause) override { m_pause = pause; }
    bool get_quit_status() override { return m_quit; }
    void set_quit_status(bool quit) override { m_quit = quit; }
    VkSurfaceKHR get_surface() override { return (VkSurfaceKHR)&m_surface; };
    void set_window_handle(void *pHandle) override { return; };
    int init_disp_wsi(VkLayerInstanceDispatchTable *vkFuncs);
    int init_headless_wsi(const char *extension_name);
   private:
    VkIcdSurfaceDisplay m_surface;
    unsigned int m_windowWidth;
    unsigned int m_windowHeight;
    bool m_pause = false;
    bool m_quit = false;
};
#endif
