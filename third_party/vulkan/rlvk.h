/**********************************************************************************************
*
*   rlvk v1.1 - A Vulkan 1.1 backend implementation of the rlgl API
*
*   DESCRIPTION:
*       rlvk is an alternative backend for the rlgl abstraction layer, implementing the
*       complete rlgl API on core Vulkan 1.1 (the ONLY hard device requirement beyond it
*       is VK_KHR_swapchain), a direct replacement for the OpenGL backends when a
*       Vulkan-only environment is required. It plays the same role for Vulkan that rlsw
*       plays for software rendering: same rl* API, different rasterizer.
*       Retargeted from the original Vulkan 1.3 design to a 1.1 baseline so old/weak
*       devices (pre-1.3 Android drivers, older desktop GPUs) run the same single code
*       path: render-pass + framebuffer caches instead of dynamic rendering, a sync1
*       shim behind the sync2-shaped call sites, staging uploads instead of host image
*       copy, a per-frame descriptor-pool ring when push descriptors are absent, a
*       vertex-shader clip-z epilogue instead of depth_clip_control, and stride-0
*       broadcast bindings instead of zero-divisor attributes.
*
*       rlgl.h is included verbatim and NEVER modified; this header provides function
*       bodies for every RLAPI symbol rlgl.h declares
*
*   FEATURES:
*       - Full rlgl API: matrix stack, immediate-mode vertex submission, render batch,
*         textures (incl. cubemaps and mipmaps), shaders, framebuffers (MRT), blend modes,
*         scissor/viewport, instancing, GPU skinning, stereo (VR) rendering
*       - Runtime GLSL 330 compilation through shaderc relaxed Vulkan rules: stock raylib
*         shaders compile unmodified (loose uniforms gathered into an implicit block,
*         samplers auto-bound, locations auto-mapped, uniforms resolved by reflection)
*       - Cached VkPipeline draw path: GL-equivalent state baked into monolithic pipelines
*         bound once per state combo, disk-persisted through VkPipelineCache for instant
*         warm starts; rendering goes through cached VkRenderPass/VkFramebuffer objects
*         (bounded caches keyed by scope shape / attachment views)
*       - Pixel-for-pixel equivalence with the OpenGL backend for aliased rendering:
*         unmirrored rasterization (GL memory orientation, Y-flip at present), GL-style
*         [-1,1] clip-z remapped by a vertex-shader epilogue on every device, GL 1px line
*         coverage where VK_EXT/KHR_line_rasterization exists (default lines otherwise,
*         cosmetic delta). MSAA uses standard Vulkan (the implementation's sample pattern
*         + fixed-function resolve), so AA edge coverage may differ slightly from GL -
*         accepted, covered by test tolerances
*       - Persistent-mapped batch vertex buffer with a fence-gated per-frame bump arena
*       - Sync2-shaped code, 1.1-core execution: every barrier/submit call site uses the
*         VkDependencyInfo/VkSubmitInfo2 shapes; on devices without synchronization2 a
*         lossless sync1 shim is installed into the dispatch table. Binary semaphores
*         plus per-frame fences pace the frame ring
*
*   ADDITIONAL NOTES:
*       Including this header automatically defines GRAPHICS_API_VULKAN_14 (so other modules
*       can detect the active backend) and GRAPHICS_API_OPENGL_33 (ONLY to fix rlgl.h's
*       rlVertexBuffer struct layout to 32-bit indices), then includes rlgl.h
*
*       NEVER define RLGL_IMPLEMENTATION in the same build as RLVK_IMPLEMENTATION, the GL
*       implementation in rlgl.h conflicts with this one; an #error guards this case
*
*       The Vulkan instance/device are created in rlglInit(). The platform layer creates the
*       VkSurfaceKHR and attaches it through rlvkAttachSurface(); frames are presented from
*       SwapScreenBuffer() through rlvkPresent(). These hooks are NOT part of rlgl.h's API
*
*   CONFIGURATION:
*       rlvk integrates exactly like the GL implementation inside rlgl.h: rlgl.h remains
*       the API header the rest of the code calls (every rl* function keeps its declaration
*       there), and rlvk provides the function bodies behind it. In the ONE translation unit
*       that would normally define RLGL_IMPLEMENTATION, define RLVK_IMPLEMENTATION and
*       include rlvk.h instead (it includes rlgl.h itself); everywhere else include rlvk.h
*       plainly for the API:
*
*           // In ONE translation unit (e.g. rcore.c): generate the implementation
*           #define RLVK_IMPLEMENTATION
*           #include "rlvk.h"
*
*           // Anywhere else that calls rl* functions or the rlvk platform hooks
*           #include "rlvk.h"
*
*       #define RLVK_IMPLEMENTATION
*           Generates the implementation of the library into the included file
*           If not defined, the library is in header only mode and can be included in other headers
*           or source files without problems. But only ONE file should hold the implementation
*           NEVER combined with RLGL_IMPLEMENTATION in the same build (#error guarded)
*
*       rlvk capabilities could be customized defining some internal
*       values before library inclusion (default values listed):
*
*           #define RLVK_MAX_TEXTURE_SLOTS          16384   // Maximum texture slot table size
*           #define RLVK_MAX_SHADER_SLOTS             256   // Maximum shader slot table size
*           #define RLVK_MAX_FRAMEBUFFER_SLOTS         64   // Maximum framebuffer slot table size
*           #define RLVK_MAX_BUFFER_SLOTS            4096   // Maximum buffer slot table size
*
*   ARCHITECTURE:
*       rlgl exposes OpenGL semantics: global state that may change between any two draws,
*       GL texture units, lazy framebuffer binding, a CPU immediate-mode vertex stream.
*       The design maps each of those to the cheapest STABLE Vulkan construct rather than
*       the most general one, which is what makes this the fastest way to run a GL-style
*       renderer on Vulkan:
*
*       - Cached monolithic VkPipelines keyed by GL state. Shader, topology, vertex layout,
*         blend, cull/polygon, depth, sample state and attachment formats form one compact
*         key into a bounded pipeline table; raylib workloads produce a small handful of
*         combos, so after warm-up a state change costs one vkCmdBindPipeline (dedup-skipped
*         when unchanged) instead of ~25 re-issued dynamic-state commands, and the whole
*         table disk-persists through VkPipelineCache so later runs never compile. Only
*         viewport and scissor stay dynamic, both dedup-gated.
*       - Cached render passes + framebuffers (1.1 core). rlgl's framebuffer model is
*         bind-and-draw, so rlEnableFramebuffer is pure bookkeeping and a render-pass
*         scope opens lazily at the next draw from two bounded caches (keyed by scope
*         shape and attachment views), matching GL's cost model. On tilers this also
*         hands the driver real load/store ops to schedule around.
*       - Per-unit push descriptors where VK_KHR_push_descriptor exists: set 0 is one
*         combined-image-sampler binding per GL texture unit plus two implicit-UBO
*         bindings. Without it, the same push call sites feed a CPU shadow and a
*         per-frame descriptor-pool ring binds a snapshot set at each draw.
*       - Persistent-mapped vertex stream: rlVertexBuffer pointers alias GPU-visible
*         host-coherent memory, so rlVertex3f writes land in place; each flush bump-copies
*         the used range into a fence-gated per-frame arena. No transcode at flush.
*       - GL usage flags are placement contracts: static buffers stage into DEVICE_LOCAL
*         once, dynamic buffers stay host-cached and mapped. Texture uploads go through
*         classic staging buffers with one-shot submissions (transient command pool, so
*         an in-progress frame recording is never clobbered).
*       - Pixel equivalence is architectural, not patched in shaders: rasterize unmirrored
*         into an intermediate image in GL's memory orientation (fill rule, Bresenham rows,
*         gl_FragCoord and winding all match natively), Y-flip once at present. MSAA stays
*         standard Vulkan (implementation sample pattern, fixed-function AVERAGE resolve):
*         exact-matching GL's pattern and resolve is possible but was judged not worth the
*         machinery; AA edges carry a small accepted tolerance in the equivalence tests.
*       - Binary semaphores + per-frame fences pace the frame ring; fences gate every
*         transient resource (arenas, buffer pools, dead-resource ring), so nothing ever
*         waits on the whole device mid-run.
*
*       Measured against the GL backend on the benchmark suite the ORIGINAL 1.3 design was
*       faster on 17 of 19 scenes (1.5x-7.5x) and statistically tied on the other two
*       (fragment-ALU-saturated, deltas under 0.3%, within run noise). Most of the margin
*       is CPU hygiene the design keeps after the 1.1 retarget (cached pipelines, no
*       per-draw state re-issue, write-combined-aware placement); the retargeted paths
*       (render passes, staging uploads, descriptor ring) have NOT been re-benchmarked yet.
*
*   DEPENDENCIES:
*       - Vulkan loader (vulkan-1.dll / libvulkan.so) and recent headers (compile-time
*         only; new-header types are used behind runtime capability checks). The DEVICE
*         minimum is core Vulkan 1.1 + VK_KHR_swapchain - nothing else is required.
*         Queried and used opportunistically when present: synchronization2 (1.3 core),
*         VK_KHR_push_descriptor, VK_EXT/KHR_line_rasterization (bresenhamLines),
*         fillModeNonSolid, VK_EXT_memory_priority, VK_EXT_pageable_device_local_memory,
*         VK_EXT_graphics_pipeline_library, VK_KHR_portability_enumeration/subset
*       - shaderc_shared.dll (loaded at runtime) for GLSL->SPIR-V (targets Vulkan 1.1 /
*         SPIR-V 1.3 so modules stay loadable everywhere); when absent, only the
*         embedded default shader is available (rlvk_shaders.h)
*
*   LICENSE: zlib/libpng
*
*   rlvk implements the rlgl API and derives its architecture patterns, conventions and
*   documentation style from rlgl.h by Ramon Santamaria (@raysan5), which it includes
*   and consumes verbatim
*
*   Copyright (c) 2014-2026 Ramon Santamaria (@raysan5)
*   Copyright (c) 2026 rygo6 and claude
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#ifndef RLVK_H
#define RLVK_H

#define RLVK_VERSION  "1.1"

// Function specifiers definition
#ifndef RLVKAPI
    #define RLVKAPI     // Functions defined as 'extern' by default (implicit specifiers)
#endif

// Mark the Vulkan backend active. Other modules can detect this with #if defined.
#ifndef GRAPHICS_API_VULKAN_14
    #define GRAPHICS_API_VULKAN_14
#endif

// rlVertexBuffer struct layout in rlgl.h is conditional on GRAPHICS_API_OPENGL_*.
// We force the 32-bit-indices layout here so the struct comes out with `unsigned int *indices`.
// rlgl.h itself is NOT modified.
#ifndef GRAPHICS_API_OPENGL_33
    #define GRAPHICS_API_OPENGL_33
#endif

#if defined(RLGL_IMPLEMENTATION)
    #error "rlvk.h is the Vulkan backend implementation; do not also define RLGL_IMPLEMENTATION in this build"
#endif

// rlgl.h gates these default shader-name string macros behind #if defined(RLGL_IMPLEMENTATION).
// rcore.c / rmodels.c use them OUTSIDE that guard, so we pre-define them here. The matching
// #ifndef guards inside rlgl.h see they're already set and skip - values stay public.
#ifndef RL_DEFAULT_SHADER_ATTRIB_NAME_POSITION
    #define RL_DEFAULT_SHADER_ATTRIB_NAME_POSITION          "vertexPosition"
#endif
#ifndef RL_DEFAULT_SHADER_ATTRIB_NAME_TEXCOORD
    #define RL_DEFAULT_SHADER_ATTRIB_NAME_TEXCOORD          "vertexTexCoord"
#endif
#ifndef RL_DEFAULT_SHADER_ATTRIB_NAME_NORMAL
    #define RL_DEFAULT_SHADER_ATTRIB_NAME_NORMAL            "vertexNormal"
#endif
#ifndef RL_DEFAULT_SHADER_ATTRIB_NAME_COLOR
    #define RL_DEFAULT_SHADER_ATTRIB_NAME_COLOR             "vertexColor"
#endif
#ifndef RL_DEFAULT_SHADER_ATTRIB_NAME_TANGENT
    #define RL_DEFAULT_SHADER_ATTRIB_NAME_TANGENT           "vertexTangent"
#endif
#ifndef RL_DEFAULT_SHADER_ATTRIB_NAME_TEXCOORD2
    #define RL_DEFAULT_SHADER_ATTRIB_NAME_TEXCOORD2         "vertexTexCoord2"
#endif
#ifndef RL_DEFAULT_SHADER_ATTRIB_NAME_BONEINDICES
    #define RL_DEFAULT_SHADER_ATTRIB_NAME_BONEINDICES       "vertexBoneIndices"
#endif
#ifndef RL_DEFAULT_SHADER_ATTRIB_NAME_BONEWEIGHTS
    #define RL_DEFAULT_SHADER_ATTRIB_NAME_BONEWEIGHTS       "vertexBoneWeights"
#endif
#ifndef RL_DEFAULT_SHADER_ATTRIB_NAME_INSTANCETRANSFORM
    #define RL_DEFAULT_SHADER_ATTRIB_NAME_INSTANCETRANSFORM "instanceTransform"
#endif
#ifndef RL_DEFAULT_SHADER_UNIFORM_NAME_MVP
    #define RL_DEFAULT_SHADER_UNIFORM_NAME_MVP              "mvp"
#endif
#ifndef RL_DEFAULT_SHADER_UNIFORM_NAME_VIEW
    #define RL_DEFAULT_SHADER_UNIFORM_NAME_VIEW             "matView"
#endif
#ifndef RL_DEFAULT_SHADER_UNIFORM_NAME_PROJECTION
    #define RL_DEFAULT_SHADER_UNIFORM_NAME_PROJECTION       "matProjection"
#endif
#ifndef RL_DEFAULT_SHADER_UNIFORM_NAME_MODEL
    #define RL_DEFAULT_SHADER_UNIFORM_NAME_MODEL            "matModel"
#endif
#ifndef RL_DEFAULT_SHADER_UNIFORM_NAME_NORMAL
    #define RL_DEFAULT_SHADER_UNIFORM_NAME_NORMAL           "matNormal"
#endif
#ifndef RL_DEFAULT_SHADER_UNIFORM_NAME_COLOR
    #define RL_DEFAULT_SHADER_UNIFORM_NAME_COLOR            "colDiffuse"
#endif
#ifndef RL_DEFAULT_SHADER_UNIFORM_NAME_BONEMATRICES
    #define RL_DEFAULT_SHADER_UNIFORM_NAME_BONEMATRICES     "boneMatrices"
#endif
#ifndef RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE0
    #define RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE0       "texture0"
#endif
#ifndef RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE1
    #define RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE1       "texture1"
#endif
#ifndef RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE2
    #define RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE2       "texture2"
#endif

#include "rlgl.h"
#include <vulkan/vulkan.h>

#if defined(__cplusplus)
extern "C" {            // Prevents name mangling of functions
#endif

//------------------------------------------------------------------------------------
// Functions Declaration - Platform-layer hooks (intentionally NOT part of rlgl.h)
//------------------------------------------------------------------------------------
RLVKAPI VkInstance rlvkGetInstance(void);           // Get the VkInstance (the platform needs it to create a surface)
RLVKAPI void rlvkAttachSurface(VkSurfaceKHR surface); // Attach the platform-created surface, builds the swapchain
RLVKAPI void rlvkSetMsaaSamples(int samples);       // Set MSAA sample count, call BEFORE rlvkAttachSurface (FLAG_MSAA_4X_HINT)
RLVKAPI void rlvkPresent(void);                     // Present the current frame, called from SwapScreenBuffer()

#if defined(__cplusplus)
}
#endif

#endif // RLVK_H

/***********************************************************************************
*
*   RLVK IMPLEMENTATION
*
************************************************************************************/

#if defined(RLVK_IMPLEMENTATION)

#undef RLVK_IMPLEMENTATION  // Undef to allow template expanding without implementation redefinition

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <ctype.h>
#if !defined(_WIN32)
    #include <dlfcn.h>      // runtime shaderc loading (dlopen/dlsym)
#endif

//----------------------------------------------------------------------------------
// Base Types - short fixed-width aliases used throughout the implementation
//----------------------------------------------------------------------------------
typedef int8_t   i8;
typedef uint8_t  u8;
typedef int16_t  i16;
typedef uint16_t u16;
typedef int32_t  i32;
typedef uint32_t u32;
typedef int64_t  i64;
typedef uint64_t u64;

typedef long long          ill;
typedef unsigned long long ull;

#if defined(__FLT16_MANT_DIG__)     // _Float16 needs compiler support (GCC/Clang on x86-64/ARM; not MSVC)
typedef _Float16 f16;
#endif
typedef float    f32;
typedef double   f64;

#include "rlvk_shaders.h"       // Embedded SPIR-V for the default shader (fallback when shaderc is absent)
#include <shaderc/shaderc.h>     // Types/enums only - functions are loaded from shaderc_shared.dll at runtime

#if defined(_WIN32) && !defined(_WINDOWS_)
// Minimal kernel32 decls so we can load shaderc_shared.dll without pulling in windows.h
// (windows.h collides with raylib names: Rectangle, CloseWindow, ShowCursor, ...)
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *lpLibFileName);
__declspec(dllimport) void *__stdcall GetProcAddress(void *hModule, const char *lpProcName);
#endif

//----------------------------------------------------------------------------------
// Implementation body, split into rlvk/*.inl fragments for maintainability.
// These are textual includes (single TU); the order below is significant.
//----------------------------------------------------------------------------------
#include "rlvk/rlvk_config.inl"
#include "rlvk/rlvk_state.inl"
#include "rlvk/rlvk_forward.inl"
#include "rlvk/rlvk_shaderc.inl"
#include "rlvk/rlvk_matrix.inl"
#include "rlvk/rlvk_renderpass.inl"
#include "rlvk/rlvk_core.inl"
#include "rlvk/rlvk_texture.inl"
#include "rlvk/rlvk_shader.inl"
#include "rlvk/rlvk_compute.inl"
#include "rlvk/rlvk_frame.inl"
#include "rlvk/rlvk_pipeline.inl"
#include "rlvk/rlvk_platform.inl"
#include "rlvk/rlvk_format.inl"

#endif // RLVK_IMPLEMENTATION
