#!/usr/bin/env python3
"""Patches a raylib 6.0 source checkout for the rlvk Vulkan backend.

Applied by CMake at configure time when WUXING_USE_VULKAN=ON (idempotent: a marker
comment guards every edit, re-running is a no-op). The patch is only ACTIVE when the
GRAPHICS_API_VULKAN compile definition is set — an unpatched-behavior GL build still
works from the same patched tree, so flipping the CMake option back and forth is safe.

Usage: rlvk_patch_raylib.py <raylib_source_dir>
"""
import sys, os

MARKER = "RLVK PATCH"

def patch(path, replacements):
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    if MARKER in src:
        print(f"already patched: {path}")
        return
    for old, new in replacements:
        if old not in src:
            print(f"ERROR: anchor not found in {path}:\n{old}")
            sys.exit(1)
        src = src.replace(old, new, 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    print(f"patched: {path}")

root = sys.argv[1]

# --- rcore.c: swap the rlgl implementation for rlvk, attach the surface after rlglInit ---
patch(os.path.join(root, "src/rcore.c"), [
    (
        '#define RLGL_IMPLEMENTATION\n'
        '#include "rlgl.h"                   // OpenGL abstraction layer to OpenGL 1.1, 3.3+ or ES2\n',

        '// --- ' + MARKER + ': rlvk provides the rlgl implementation on Vulkan builds ---\n'
        '#if defined(GRAPHICS_API_VULKAN)\n'
        '    #define RLVK_IMPLEMENTATION\n'
        '    #include "rlvk.h"               // Vulkan 1.1 backend implementing the rlgl API\n'
        '#else\n'
        '    #define RLGL_IMPLEMENTATION\n'
        '    #include "rlgl.h"               // OpenGL abstraction layer to OpenGL 1.1, 3.3+ or ES2\n'
        '#endif\n',
    ),
    (
        '    rlglInit(CORE.Window.render.width, CORE.Window.render.height);\n',

        '    rlglInit(CORE.Window.render.width, CORE.Window.render.height);\n'
        '#if defined(GRAPHICS_API_VULKAN)\n'
        '    // rlvk created the VkInstance/device inside rlglInit; the platform layer now\n'
        '    // creates the VkSurfaceKHR and hands it over (builds the swapchain)\n'
        '    {\n'
        '        void WindowAttachVulkanSurface(void);   // defined by the platform layer\n'
        '        WindowAttachVulkanSurface();\n'
        '    }\n'
        '#endif\n',
    ),
])

# --- GLFW desktop platform: NO_API window, surface attach hook, Vulkan present ---
patch(os.path.join(root, "src/platforms/rcore_desktop_glfw.c"), [
    (
        '    glfwDefaultWindowHints();                       // Set default windows hints\n',

        '    glfwDefaultWindowHints();                       // Set default windows hints\n'
        '#if defined(GRAPHICS_API_VULKAN)\n'
        '    // ' + MARKER + ': no GL context - rendering goes through the Vulkan swapchain.\n'
        '    // Context-version hints below are ignored under GLFW_NO_API; the existing\n'
        '    // glfwMakeContextCurrent error check already tolerates GLFW_NO_WINDOW_CONTEXT.\n'
        '    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);\n'
        '#endif\n',
    ),
    (
        '    glfwMakeContextCurrent(platform.handle);\n'
        '    result = glfwGetError(NULL);\n'
        '    if ((result != GLFW_NO_WINDOW_CONTEXT) && (result != GLFW_PLATFORM_ERROR)) CORE.Window.ready = true; // Checking context activation\n',

        '#if defined(GRAPHICS_API_VULKAN)\n'
        '    // ' + MARKER + ': a GLFW_NO_API window has no context to make current; the window\n'
        '    // existing at all is the readiness signal (rlvk attaches the surface later)\n'
        '    CORE.Window.ready = true;\n'
        '#else\n'
        '    glfwMakeContextCurrent(platform.handle);\n'
        '    result = glfwGetError(NULL);\n'
        '    if ((result != GLFW_NO_WINDOW_CONTEXT) && (result != GLFW_PLATFORM_ERROR)) CORE.Window.ready = true; // Checking context activation\n'
        '#endif\n',
    ),
    (
        '// Swap back buffer with front buffer (screen drawing)\n'
        'void SwapScreenBuffer(void)\n'
        '{\n'
        '    glfwSwapBuffers(platform.handle);\n'
        '}\n',

        '// Swap back buffer with front buffer (screen drawing)\n'
        'void SwapScreenBuffer(void)\n'
        '{\n'
        '#if defined(GRAPHICS_API_VULKAN)\n'
        '    rlvkPresent();      // ' + MARKER + ': close the frame scope, blit, submit, present\n'
        '#else\n'
        '    glfwSwapBuffers(platform.handle);\n'
        '#endif\n'
        '}\n'
        '\n'
        '#if defined(GRAPHICS_API_VULKAN)\n'
        '// ' + MARKER + ': create the window surface and attach it to the Vulkan backend.\n'
        '// Called from InitWindow right after rlglInit (the VkInstance exists by then).\n'
        '// VkSurfaceKHR/glfwCreateWindowSurface are visible because rlvk.h (which includes\n'
        '// vulkan.h) precedes this file inside the rcore.c translation unit.\n'
        'void WindowAttachVulkanSurface(void)\n'
        '{\n'
        '    if (FLAG_IS_SET(CORE.Window.flags, FLAG_MSAA_4X_HINT)) rlvkSetMsaaSamples(4);\n'
        '    VkSurfaceKHR surface = VK_NULL_HANDLE;\n'
        '    VkResult result = glfwCreateWindowSurface(rlvkGetInstance(), platform.handle, NULL, &surface);\n'
        '    if (result != VK_SUCCESS) { TRACELOG(LOG_WARNING, "GLFW: Failed to create Vulkan window surface (VkResult %i)", result); return; }\n'
        '    rlvkAttachSurface(surface);\n'
        '}\n'
        '#endif\n',
    ),
])

print("raylib rlvk patch complete")
