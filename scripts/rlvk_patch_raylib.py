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

# --- Android NativeActivity platform: surface attach/detach across the async window
# lifecycle (APP_CMD_INIT_WINDOW/APP_CMD_TERM_WINDOW), Vulkan present ---
#
# Unlike GLFW desktop (window exists synchronously inside InitPlatform, one attach for the
# whole app lifetime), Android's ANativeWindow is created/destroyed by the OS at arbitrary
# times (first launch, and every pause/resume) and delivered via AndroidCommandCallback.
# Two non-obvious constraints drive this patch:
#   1. rcore_android.c's APP_CMD_INIT_WINDOW handler normally calls rlglInit() ITSELF (plus
#      SetupViewport/InitTimer/LoadFontDefault/SetRandomSeed) before InitPlatform()'s wait
#      loop returns — and then rcore.c's InitWindow() calls rlglInit() AGAIN right after
#      InitPlatform() returns (the same generic site the GLFW patch above hooks). On GL this
#      double-init is silently tolerated (rlglInit/SetupViewport/LoadFontDefault happen to be
#      safe to re-run). Under Vulkan it is NOT: rlglInit creates the VkInstance/device, and a
#      second call would recreate them over the live RLVK globals mid-use. Fix: under Vulkan,
#      the first-launch branch of APP_CMD_INIT_WINDOW is reduced to just the window-dimension
#      bookkeeping + CORE.Window.ready=true — rlglInit + the surface attach happen exactly
#      once, from the generic rcore.c site (via a new Android WindowAttachVulkanSurface,
#      mirroring the GLFW one) using platform.app->window (already valid by then).
#   2. Pause destroys the ANativeWindow (APP_CMD_TERM_WINDOW) and resume creates a NEW one
#      (APP_CMD_INIT_WINDOW with platform.contextRebindRequired == true, set by the pause
#      handler) — WITHOUT another rlglInit() call. This is the genuine attach/detach cycle:
#      rlvkDetachSurface() on pause, vkCreateAndroidSurfaceKHR + rlvkAttachSurface() (which is
#      re-entrant) on resume. platform.contextRebindRequired is reused unchanged as the
#      "window was lost and came back" signal for both the EGL and Vulkan paths.
#
# VK_USE_PLATFORM_ANDROID_KHR (needed for vkCreateAndroidSurfaceKHR/VkAndroidSurfaceCreateInfoKHR
# to be declared by vulkan.h) must come from a compiler flag (-DVK_USE_PLATFORM_ANDROID_KHR),
# not from this patch — vulkan.h is first included by rlvk.h from INSIDE the RLVK_IMPLEMENTATION
# block near the top of rcore.c, before this platform file is reached in the same translation
# unit, so a #define placed here would already be too late.
patch(os.path.join(root, "src/platforms/rcore_android.c"), [
    (
        '// Swap back buffer with front buffer (screen drawing)\n'
        'void SwapScreenBuffer(void)\n'
        '{\n'
        '    if (platform.surface != EGL_NO_SURFACE) eglSwapBuffers(platform.device, platform.surface);\n'
        '}\n',

        '// Swap back buffer with front buffer (screen drawing)\n'
        'void SwapScreenBuffer(void)\n'
        '{\n'
        '#if defined(GRAPHICS_API_VULKAN)\n'
        '    rlvkPresent();      // ' + MARKER + ': close the frame scope, blit, submit, present\n'
        '#else\n'
        '    if (platform.surface != EGL_NO_SURFACE) eglSwapBuffers(platform.device, platform.surface);\n'
        '#endif\n'
        '}\n'
        '\n'
        '#if defined(GRAPHICS_API_VULKAN)\n'
        '// ' + MARKER + ': create the window surface and attach it to the Vulkan backend.\n'
        '// Called from InitWindow (rcore.c) right after rlglInit (the VkInstance exists by\n'
        '// then). By the time InitPlatform() returns on Android, APP_CMD_INIT_WINDOW has\n'
        '// already run and platform.app->window is a valid ANativeWindow (InitPlatform\'s wait\n'
        '// loop guarantees this - see the reduced APP_CMD_INIT_WINDOW branch below).\n'
        'void WindowAttachVulkanSurface(void)\n'
        '{\n'
        '    VkSurfaceKHR surface = VK_NULL_HANDLE;\n'
        '    VkResult result = vkCreateAndroidSurfaceKHR(rlvkGetInstance(),\n'
        '        &(VkAndroidSurfaceCreateInfoKHR){ VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window = platform.app->window },\n'
        '        NULL, &surface);\n'
        '    if (result != VK_SUCCESS) { TRACELOG(LOG_WARNING, "ANDROID: Failed to create Vulkan window surface (VkResult %i)", result); return; }\n'
        '    rlvkAttachSurface(surface);\n'
        '}\n'
        '#endif\n',
    ),
    (
        '        case APP_CMD_START:\n'
        '        {\n'
        '            //rendering = true;\n'
        '        } break;\n'
        '        case APP_CMD_RESUME: break;\n'
        '        case APP_CMD_INIT_WINDOW:\n'
        '        {\n'
        '            if (app->window != NULL)\n'
        '            {\n'
        '                if (platform.contextRebindRequired)\n'
        '                {\n'
        '                    // Reset screen scaling to full display size\n'
        '                    EGLint displayFormat = 0;\n'
        '                    eglGetConfigAttrib(platform.device, platform.config, EGL_NATIVE_VISUAL_ID, &displayFormat);\n'
        '\n'
        '                    // Adding renderOffset here feels rather hackish, but the viewport scaling is wrong after the\n'
        '                    // context rebinding if the screen is scaled unless offsets are added. There\'s probably a more\n'
        '                    // appropriate way to fix this\n'
        '                    ANativeWindow_setBuffersGeometry(app->window,\n'
        '                        CORE.Window.render.width + CORE.Window.renderOffset.x,\n'
        '                        CORE.Window.render.height + CORE.Window.renderOffset.y,\n'
        '                        displayFormat);\n'
        '\n'
        '                    // Recreate display surface and re-attach OpenGL context\n'
        '                    platform.surface = eglCreateWindowSurface(platform.device, platform.config, app->window, NULL);\n'
        '                    eglMakeCurrent(platform.device, platform.surface, platform.surface, platform.context);\n'
        '\n'
        '                    platform.contextRebindRequired = false;\n'
        '                }\n'
        '                else\n'
        '                {\n'
        '                    CORE.Window.display.width = ANativeWindow_getWidth(platform.app->window);\n'
        '                    CORE.Window.display.height = ANativeWindow_getHeight(platform.app->window);\n'
        '\n'
        '                    // Initialize graphics device (display device and OpenGL context)\n'
        '                    InitGraphicsDevice();\n'
        '\n'
        '                    // Initialize OpenGL context (states and resources)\n'
        '                    // NOTE: CORE.Window.currentFbo.width and CORE.Window.currentFbo.height not used, stored as globals in rlgl\n'
        '                    rlglInit(CORE.Window.currentFbo.width, CORE.Window.currentFbo.height);\n'
        '\n'
        '                    // Setup default viewport\n'
        '                    // NOTE: It updated CORE.Window.render.width and CORE.Window.render.height\n'
        '                    SetupViewport(CORE.Window.currentFbo.width, CORE.Window.currentFbo.height);\n'
        '\n'
        '                    // Initialize hi-res timer\n'
        '                    InitTimer();\n'
        '\n'
        '                #if SUPPORT_MODULE_RTEXT\n'
        '                    // Load default font\n'
        '                    // WARNING: External function: Module required: rtext\n'
        '                    LoadFontDefault();\n'
        '                    #if SUPPORT_MODULE_RSHAPES\n'
        '                    // Set font white rectangle for shapes drawing, so shapes and text can be batched together\n'
        '                    // WARNING: rshapes module is required, if not available, default internal white rectangle is used\n'
        '                    Rectangle rec = GetFontDefault().recs[95];\n'
        '                    if (FLAG_IS_SET(CORE.Window.flags, FLAG_MSAA_4X_HINT))\n'
        '                    {\n'
        '                        // NOTE: Trying to maxime rec padding to avoid pixel bleeding on MSAA filtering\n'
        '                        SetShapesTexture(GetFontDefault().texture, (Rectangle){ rec.x + 2, rec.y + 2, 1, 1 });\n'
        '                    }\n'
        '                    else\n'
        '                    {\n'
        '                        // NOTE: Setting up a 1px padding on char rectangle to avoid pixel bleeding\n'
        '                        SetShapesTexture(GetFontDefault().texture, (Rectangle){ rec.x + 1, rec.y + 1, rec.width - 2, rec.height - 2 });\n'
        '                    }\n'
        '                    #endif\n'
        '                #else\n'
        '                    #if SUPPORT_MODULE_RSHAPES\n'
        '                    // Set default texture and rectangle to be used for shapes drawing\n'
        '                    // NOTE: rlgl default texture is a 1x1 pixel UNCOMPRESSED_R8G8B8A8\n'
        '                    Texture2D texture = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };\n'
        '                    SetShapesTexture(texture, (Rectangle){ 0.0f, 0.0f, 1.0f, 1.0f });    // WARNING: Module required: rshapes\n'
        '                    #endif\n'
        '                #endif\n'
        '\n'
        '                    // Initialize random seed\n'
        '                    SetRandomSeed((unsigned int)time(NULL));\n'
        '                }\n'
        '            }\n'
        '        } break;\n',

        '        case APP_CMD_START:\n'
        '        {\n'
        '            //rendering = true;\n'
        '        } break;\n'
        '        case APP_CMD_RESUME: break;\n'
        '        case APP_CMD_INIT_WINDOW:\n'
        '        {\n'
        '#if defined(GRAPHICS_API_VULKAN)\n'
        '            // ' + MARKER + ': no EGL, no local rlglInit - see the file-level comment above\n'
        '            // WindowAttachVulkanSurface for why. First launch just records the window and\n'
        '            // flags readiness; rcore.c\'s InitWindow does rlglInit + the one real surface\n'
        '            // attach right after. Resume (contextRebindRequired) re-attaches directly, since\n'
        '            // rlglInit already ran once and must not run again.\n'
        '            if (app->window != NULL)\n'
        '            {\n'
        '                if (platform.contextRebindRequired)\n'
        '                {\n'
        '                    VkSurfaceKHR surface = VK_NULL_HANDLE;\n'
        '                    VkResult result = vkCreateAndroidSurfaceKHR(rlvkGetInstance(),\n'
        '                        &(VkAndroidSurfaceCreateInfoKHR){ VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window = app->window },\n'
        '                        NULL, &surface);\n'
        '                    if (result != VK_SUCCESS) TRACELOG(LOG_WARNING, "ANDROID: Failed to recreate Vulkan window surface (VkResult %i)", result);\n'
        '                    else rlvkAttachSurface(surface);   // re-entrant: rebuilds the swapchain\n'
        '                    platform.contextRebindRequired = false;\n'
        '                }\n'
        '                else\n'
        '                {\n'
        '                    CORE.Window.display.width = ANativeWindow_getWidth(platform.app->window);\n'
        '                    CORE.Window.display.height = ANativeWindow_getHeight(platform.app->window);\n'
        '\n'
        '                    // ' + MARKER + ': UNIFORM-scale letterbox, computed directly (not via\n'
        '                    // SetupFramebuffer, whose branches assume GL\'s separate\n'
        '                    // ANativeWindow_setBuffersGeometry + OS-compositor-upscale step - rlvk\'s\n'
        '                    // swapchain has no such step; it is created directly at caps.currentExtent, the\n'
        '                    // real display resolution). Two prior attempts here were both wrong: (1)\n'
        '                    // render=screen (1280x720) left the Vulkan viewport smaller than the swapchain -\n'
        '                    // undrawn black region outside it. (2) render=display (2320x1080) filled the\n'
        '                    // screen but with NON-uniform per-axis scale (screen and display have DIFFERENT\n'
        '                    // aspect ratios - 1280x720=1.778 vs e.g. 2320x1080=2.148) - circles drawn in\n'
        '                    // logical screen-space came out as ellipses on screen, confirmed visually. The\n'
        '                    // correct fix is a UNIFORM scale (same factor both axes) sized to FIT within the\n'
        '                    // display without exceeding it (letterbox bars on the shorter axis, centered via\n'
        '                    // renderOffset/2 in SetupViewport\'s rlViewport call) - shapes stay round, and\n'
        '                    // AndroidInputCallback\'s touch-scaling formula (which already accounts for\n'
        '                    // renderOffset) maps taps back through the exact same transform.\n'
        '                    {\n'
        '                        float wRatio = (float)CORE.Window.display.width/(float)CORE.Window.screen.width;\n'
        '                        float hRatio = (float)CORE.Window.display.height/(float)CORE.Window.screen.height;\n'
        '                        float uniformScale = (wRatio < hRatio) ? wRatio : hRatio;\n'
        '                        CORE.Window.render.width = (int)((float)CORE.Window.screen.width*uniformScale + 0.5f);\n'
        '                        CORE.Window.render.height = (int)((float)CORE.Window.screen.height*uniformScale + 0.5f);\n'
        '                        CORE.Window.renderOffset.x = CORE.Window.display.width - CORE.Window.render.width;\n'
        '                        CORE.Window.renderOffset.y = CORE.Window.display.height - CORE.Window.render.height;\n'
        '                        CORE.Window.currentFbo.width = CORE.Window.render.width;\n'
        '                        CORE.Window.currentFbo.height = CORE.Window.render.height;\n'
        '\n'
        '                        // ' + MARKER + ': stock raylib\'s SetupViewport()/rlOrtho() always draw 2D\n'
        '                        // content in CORE.Window.render pixel space, never screen space - on GL this is\n'
        '                        // invisible because SetupFramebuffer() pins currentFbo to screen size and lets\n'
        '                        // the OS compositor upscale the small buffer at present time, so render==screen\n'
        '                        // numerically. rlvk has no such OS-upscale step, so render is intentionally\n'
        '                        // bigger than screen here (the letterbox fit above) - without this line every\n'
        '                        // GetScreenWidth()-based UI layout (menu buttons, HUD) draws compressed into a\n'
        '                        // fraction of the real viewport while hit-testing against the ORIGINAL small\n'
        '                        // screen size, so displayed button position and tap-effective position diverge.\n'
        '                        // Keeping screen in lockstep with render restores a single consistent coordinate\n'
        '                        // space for both drawing and touch (AndroidInputCallback\'s widthRatio/heightRatio\n'
        '                        // read CORE.Window.screen live, so this also fixes touch mapping for free).\n'
        '                        CORE.Window.screen.width = CORE.Window.render.width;\n'
        '                        CORE.Window.screen.height = CORE.Window.render.height;\n'
        '                    }\n'
        '\n'
        '                    CORE.Window.ready = true;\n'
        '                }\n'
        '            }\n'
        '#else\n'
        '            if (app->window != NULL)\n'
        '            {\n'
        '                if (platform.contextRebindRequired)\n'
        '                {\n'
        '                    // Reset screen scaling to full display size\n'
        '                    EGLint displayFormat = 0;\n'
        '                    eglGetConfigAttrib(platform.device, platform.config, EGL_NATIVE_VISUAL_ID, &displayFormat);\n'
        '\n'
        '                    // Adding renderOffset here feels rather hackish, but the viewport scaling is wrong after the\n'
        '                    // context rebinding if the screen is scaled unless offsets are added. There\'s probably a more\n'
        '                    // appropriate way to fix this\n'
        '                    ANativeWindow_setBuffersGeometry(app->window,\n'
        '                        CORE.Window.render.width + CORE.Window.renderOffset.x,\n'
        '                        CORE.Window.render.height + CORE.Window.renderOffset.y,\n'
        '                        displayFormat);\n'
        '\n'
        '                    // Recreate display surface and re-attach OpenGL context\n'
        '                    platform.surface = eglCreateWindowSurface(platform.device, platform.config, app->window, NULL);\n'
        '                    eglMakeCurrent(platform.device, platform.surface, platform.surface, platform.context);\n'
        '\n'
        '                    platform.contextRebindRequired = false;\n'
        '                }\n'
        '                else\n'
        '                {\n'
        '                    CORE.Window.display.width = ANativeWindow_getWidth(platform.app->window);\n'
        '                    CORE.Window.display.height = ANativeWindow_getHeight(platform.app->window);\n'
        '\n'
        '                    // Initialize graphics device (display device and OpenGL context)\n'
        '                    InitGraphicsDevice();\n'
        '\n'
        '                    // Initialize OpenGL context (states and resources)\n'
        '                    // NOTE: CORE.Window.currentFbo.width and CORE.Window.currentFbo.height not used, stored as globals in rlgl\n'
        '                    rlglInit(CORE.Window.currentFbo.width, CORE.Window.currentFbo.height);\n'
        '\n'
        '                    // Setup default viewport\n'
        '                    // NOTE: It updated CORE.Window.render.width and CORE.Window.render.height\n'
        '                    SetupViewport(CORE.Window.currentFbo.width, CORE.Window.currentFbo.height);\n'
        '\n'
        '                    // Initialize hi-res timer\n'
        '                    InitTimer();\n'
        '\n'
        '                #if SUPPORT_MODULE_RTEXT\n'
        '                    // Load default font\n'
        '                    // WARNING: External function: Module required: rtext\n'
        '                    LoadFontDefault();\n'
        '                    #if SUPPORT_MODULE_RSHAPES\n'
        '                    // Set font white rectangle for shapes drawing, so shapes and text can be batched together\n'
        '                    // WARNING: rshapes module is required, if not available, default internal white rectangle is used\n'
        '                    Rectangle rec = GetFontDefault().recs[95];\n'
        '                    if (FLAG_IS_SET(CORE.Window.flags, FLAG_MSAA_4X_HINT))\n'
        '                    {\n'
        '                        // NOTE: Trying to maxime rec padding to avoid pixel bleeding on MSAA filtering\n'
        '                        SetShapesTexture(GetFontDefault().texture, (Rectangle){ rec.x + 2, rec.y + 2, 1, 1 });\n'
        '                    }\n'
        '                    else\n'
        '                    {\n'
        '                        // NOTE: Setting up a 1px padding on char rectangle to avoid pixel bleeding\n'
        '                        SetShapesTexture(GetFontDefault().texture, (Rectangle){ rec.x + 1, rec.y + 1, rec.width - 2, rec.height - 2 });\n'
        '                    }\n'
        '                    #endif\n'
        '                #else\n'
        '                    #if SUPPORT_MODULE_RSHAPES\n'
        '                    // Set default texture and rectangle to be used for shapes drawing\n'
        '                    // NOTE: rlgl default texture is a 1x1 pixel UNCOMPRESSED_R8G8B8A8\n'
        '                    Texture2D texture = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };\n'
        '                    SetShapesTexture(texture, (Rectangle){ 0.0f, 0.0f, 1.0f, 1.0f });    // WARNING: Module required: rshapes\n'
        '                    #endif\n'
        '                #endif\n'
        '\n'
        '                    // Initialize random seed\n'
        '                    SetRandomSeed((unsigned int)time(NULL));\n'
        '                }\n'
        '            }\n'
        '#endif\n'
        '        } break;\n',
    ),
    (
        '            // Detach OpenGL context and destroy display surface\n'
        '            // NOTE 1: This case is used when the user exits the app without closing it, context is detached to ensure everything is recoverable upon resuming\n'
        '            // NOTE 2: Detaching context before destroying display surface avoids losing our resources (textures, shaders, VBOs...)\n'
        '            // NOTE 3: In some cases (too many context loaded), OS could unload context automatically... :(\n'
        '            if (platform.device != EGL_NO_DISPLAY)\n'
        '            {\n'
        '                eglMakeCurrent(platform.device, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);\n'
        '\n'
        '                if (platform.surface != EGL_NO_SURFACE)\n'
        '                {\n'
        '                    eglDestroySurface(platform.device, platform.surface);\n'
        '                    platform.surface = EGL_NO_SURFACE;\n'
        '                }\n'
        '\n'
        '                platform.contextRebindRequired = true;\n'
        '            }\n'
        '            // If \'platform.device\' is already set to \'EGL_NO_DISPLAY\'\n'
        '            // this means that the user has already called \'CloseWindow()\'\n'
        '\n'
        '        } break;\n',

        '#if defined(GRAPHICS_API_VULKAN)\n'
        '            // ' + MARKER + ': the ANativeWindow (and any surface built on it) is invalid the\n'
        '            // instant this callback returns - tear down synchronously, before returning.\n'
        '            rlvkDetachSurface();\n'
        '            platform.contextRebindRequired = true;   // resume (APP_CMD_INIT_WINDOW) re-attaches\n'
        '#else\n'
        '            // Detach OpenGL context and destroy display surface\n'
        '            // NOTE 1: This case is used when the user exits the app without closing it, context is detached to ensure everything is recoverable upon resuming\n'
        '            // NOTE 2: Detaching context before destroying display surface avoids losing our resources (textures, shaders, VBOs...)\n'
        '            // NOTE 3: In some cases (too many context loaded), OS could unload context automatically... :(\n'
        '            if (platform.device != EGL_NO_DISPLAY)\n'
        '            {\n'
        '                eglMakeCurrent(platform.device, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);\n'
        '\n'
        '                if (platform.surface != EGL_NO_SURFACE)\n'
        '                {\n'
        '                    eglDestroySurface(platform.device, platform.surface);\n'
        '                    platform.surface = EGL_NO_SURFACE;\n'
        '                }\n'
        '\n'
        '                platform.contextRebindRequired = true;\n'
        '            }\n'
        '            // If \'platform.device\' is already set to \'EGL_NO_DISPLAY\'\n'
        '            // this means that the user has already called \'CloseWindow()\'\n'
        '#endif\n'
        '\n'
        '        } break;\n',
    ),
])

print("raylib rlvk patch complete")
