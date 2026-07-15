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
// Defines and Macros
//----------------------------------------------------------------------------------
#define RLVK_ALLOC              NULL
#define RLVK_COUNTOF(arr)       ((u32)(sizeof(arr)/sizeof((arr)[0])))

#define RLVK_CHECK(expr) do {                                                         \
    VkResult _r = (expr);                                                             \
    if (_r != VK_SUCCESS) TRACELOG(RL_LOG_ERROR, "VK: " #expr " => %d", (int)_r);     \
} while (0)

#define RLVK_CHECK_LOG(cond, msg) do {                                                \
    if (cond) TRACELOG(RL_LOG_ERROR, "VK: " msg);                                     \
} while (0)

//----------------------------------------------------------------------------------
// Tunables, overridable at build time (see CONFIGURATION in the header banner)
//----------------------------------------------------------------------------------
#ifndef RLVK_MAX_TEXTURE_SLOTS
    #define RLVK_MAX_TEXTURE_SLOTS       16384
#endif
#ifndef RLVK_MAX_SHADER_SLOTS
    #define RLVK_MAX_SHADER_SLOTS        256
#endif
#ifndef RLVK_MAX_FRAMEBUFFER_SLOTS
    #define RLVK_MAX_FRAMEBUFFER_SLOTS   64
#endif
#ifndef RLVK_MAX_BUFFER_SLOTS
    #define RLVK_MAX_BUFFER_SLOTS        4096
#endif
// Deferred-destruction queue depth per frame-in-flight (objects released mid-recording)
#ifndef RLVK_MAX_DEAD_RESOURCES
    #define RLVK_MAX_DEAD_RESOURCES      256
#endif
#ifndef RLVK_MAX_SWAPCHAIN_IMAGES
    #define RLVK_MAX_SWAPCHAIN_IMAGES    8
#endif
// Cached-pipeline draw path (see ARCHITECTURE in the banner). RLVK_FASTLINK (GPL fast-linking)
// is detected (Caps.graphicsPipelineLibrary) but not yet wired into pipeline builds.
#ifndef RLVK_FASTLINK
    #define RLVK_FASTLINK                0
#endif
// Max distinct cached pipelines (state combos). raylib workloads use a small bounded set.
#ifndef RLVK_MAX_PIPELINES
    #define RLVK_MAX_PIPELINES           256
#endif
// Per-frame flush arena STARTING capacity, as a multiple of one batch buffer; the arena
// grows automatically (with a mid-frame drain on first overflow) when a frame demands more
#ifndef RLVK_ARENA_SLOTS
    #define RLVK_ARENA_SLOTS             2
#endif
#ifndef RLVK_MAX_VAO_SLOTS
    #define RLVK_MAX_VAO_SLOTS           2048
#endif
// GL texture units exposed as push-descriptor bindings in set 0 (binding N = texture unit N)
#ifndef RLVK_MAX_TEXTURE_UNITS
    #define RLVK_MAX_TEXTURE_UNITS       16
#endif
// Reflected uniforms per shader (default-uniform-block members + samplers)
#ifndef RLVK_MAX_SHADER_UNIFORMS
    #define RLVK_MAX_SHADER_UNIFORMS     160
#endif
// Set-0 bindings for each stage's implicit default uniform block (gl_DefaultUniformBlock).
// shaderc's relaxed Vulkan rules gather loose GLSL uniforms into this block; per-stage binding
// bases keep the VS and FS blocks from colliding. Samplers auto-bind from 0 (= GL texture units).
enum {
    RLVK_UBO_BINDING_VS = RLVK_MAX_TEXTURE_UNITS,
    RLVK_UBO_BINDING_FS,
    RLVK_SET0_BINDING_COUNT,
};
// Canonical raylib vertex attributes (indices into rlvkShaderSlot.attribLocs)
enum {
    RLVK_ATTRIB_POSITION,
    RLVK_ATTRIB_TEXCOORD,
    RLVK_ATTRIB_NORMAL,
    RLVK_ATTRIB_COLOR,
    RLVK_ATTRIB_TANGENT,
    RLVK_ATTRIB_TEXCOORD2,
    RLVK_ATTRIB_INSTANCE_TX,    // mat4 instanceTransform (4 consecutive locations, instance rate)
    RLVK_ATTRIB_BONEIDS,
    RLVK_ATTRIB_BONEWEIGHTS,
    RLVK_ATTRIB_COUNT,
};

// Fixed uniform/attribute "locations" for the default (mesh) shader. DrawMesh reads these out of
// material.shader.locs[] (which rlvk fills from defaultShaderLocs) and passes them back to
// rlSetUniform*/rlSetVertexAttribute, where they are decoded. Non-zero, distinct values.
enum {
    RLVK_ALOC_POSITION   = 0,
    RLVK_ALOC_TEXCOORD   = 1,
    RLVK_ALOC_NORMAL     = 2,
    RLVK_ALOC_COLOR      = 3,
    RLVK_ULOC_MVP        = 100,
    RLVK_ULOC_COLDIFFUSE = 101,
    RLVK_ULOC_TEXTURE0   = 102,
};

#define RLVK_INVALID_SLOT 0u

//----------------------------------------------------------------------------------
// Internal Constants Definition
//----------------------------------------------------------------------------------
enum {
    RLVK_FRAME_INDEX_0,
    RLVK_FRAME_INDEX_1,          // 2 frames in flight: CPU records frame N+1 while the GPU executes N
    RLVK_FRAME_INDEX_COUNT,      // (1 frame-in-flight measured 2x slower on draw-heavy scenes: CPU and GPU serialize)
};

enum {
    RLVK_QUEUE_GRAPHICS,
    RLVK_QUEUE_TRANSFER,
    RLVK_QUEUE_COUNT,
};

// NOTE: On the Vulkan 1.1 baseline the ONLY hard device requirement is VK_KHR_swapchain.
// Every other extension/feature is enumerated at device creation and recorded in RLVK.Caps;
// the two EXT-then-KHR-promoted names (line_rasterization, vertex_attribute_divisor) prefer
// their KHR spellings when the driver ships them.

// Pipeline-key vertex layouts: the batch's fixed four streams, a mesh draw carrying a
// presence mask of its optional attributes (position always present, missing ones ride the
// divisor-0 dummy broadcast), or no vertex input at all
#define RLVK_VLAYOUT_BATCH              0x00
#define RLVK_VLAYOUT_QUAD               0x01    // rlLoadDrawQuad: one interleaved pos3+uv2 binding
#define RLVK_VLAYOUT_MESH               0x80    // Mesh path marker; low bits = presence mask
#define RLVK_VLAYOUT_MESH_UV            0x01
#define RLVK_VLAYOUT_MESH_NORMAL        0x02
#define RLVK_VLAYOUT_MESH_COLOR         0x04
#define RLVK_VLAYOUT_MESH_UV2           0x08
#define RLVK_VLAYOUT_MESH_TANGENT       0x10
#define RLVK_VLAYOUT_MESH_BONES         0x20
#define RLVK_VLAYOUT_MESH_INSTANCED     0x40
#define RLVK_VLAYOUT_MESH_BONES_DUMMY   0x100   // Shader consumes bones, mesh has no streams: divisor-0 broadcast
#define RLVK_VLAYOUT_NONE               0xFFFF

//----------------------------------------------------------------------------------
// PFN dispatch table (X-macro): hot-path and EXT entry points; init-time API goes via the loader
//----------------------------------------------------------------------------------
#define RLVK_PFN_FUNCS                                                  \
    /* sync2 (native on 1.3 devices; sync1 compat shim installed otherwise) */ \
    RLVK_PFN_FUNC(CmdPipelineBarrier2)                                  \
    RLVK_PFN_FUNC(QueueSubmit2)                                         \
    /* push descriptors (VK_KHR_push_descriptor) */                     \
    RLVK_PFN_FUNC(CmdPushDescriptorSetKHR)                              \
    /* general hot path */                                              \
    RLVK_PFN_FUNC(BeginCommandBuffer)                                   \
    RLVK_PFN_FUNC(EndCommandBuffer)                                     \
    RLVK_PFN_FUNC(CmdPushConstants)                                     \
    RLVK_PFN_FUNC(CmdDraw)                                              \
    RLVK_PFN_FUNC(CmdDrawIndexed)                                       \
    RLVK_PFN_FUNC(CmdBlitImage)                                         \
    RLVK_PFN_FUNC(CmdCopyImageToBuffer)                                 \
    /* swapchain (loaded once surface is attached) */                   \
    RLVK_PFN_FUNC(AcquireNextImageKHR)                                  \
    RLVK_PFN_FUNC(QueuePresentKHR)                                      \

// Function pointers live inside a struct so their names don't collide with the actual
// Vulkan API symbols declared in vulkan.h. Call as `vk.CmdPushDescriptorSetKHR(...)`.
static struct {
#define RLVK_PFN_FUNC(_func) PFN_vk##_func _func;
    RLVK_PFN_FUNCS
#undef RLVK_PFN_FUNC
} vk;

//----------------------------------------------------------------------------------
// Synchronization2 -> sync1 compat shim (Vulkan 1.1 baseline)
//
// Every barrier/submit call site stays sync2-shaped (vk.CmdPipelineBarrier2 /
// vk.QueueSubmit2 with VkDependencyInfo / VkSubmitInfo2). On devices without
// synchronization2 these two shims are installed into the dispatch table instead of the
// native entry points and translate to core-1.1 vkCmdPipelineBarrier / vkQueueSubmit.
// Only the stage/access bits rlvk actually emits are mapped; sync2-only bits that have
// no 32-bit sync1 twin fold into their containing sync1 stage (COPY/BLIT -> TRANSFER,
// INDEX/VERTEX_ATTRIBUTE_INPUT -> VERTEX_INPUT, SAMPLED/STORAGE_READ -> SHADER_READ).
//----------------------------------------------------------------------------------

// Sync2 shim caps: sized for rlvk's own call sites (max seen: 2 image barriers, 1 of the
// rest); generous headroom, loud failure if a future site outgrows them
#define RLVK_SYNC1_MAX_BARRIERS     8
#define RLVK_SYNC1_MAX_SEMAPHORES   4

static VkPipelineStageFlags rlvkStageMask1(VkPipelineStageFlags2 s, bool isDst)
{
    if (s == VK_PIPELINE_STAGE_2_NONE) return isDst ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags out = (VkPipelineStageFlags)(s & 0x7fffffffull);   // classic bits are numerically identical
    if (s & (VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT))
        out |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (s & (VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT))
        out |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    return out;
}

static VkAccessFlags rlvkAccessMask1(VkAccessFlags2 a)
{
    VkAccessFlags out = (VkAccessFlags)(a & 0x7fffffffull);                 // classic bits are numerically identical
    if (a & (VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT)) out |= VK_ACCESS_SHADER_READ_BIT;
    if (a & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) out |= VK_ACCESS_SHADER_WRITE_BIT;
    return out;
}

static VKAPI_ATTR void VKAPI_CALL rlvkCmdPipelineBarrier2Compat(VkCommandBuffer cmdBuffer, const VkDependencyInfo *dep)
{
    // sync1 takes ONE src/dst stage pair for the whole command: aggregate every barrier's masks
    VkPipelineStageFlags srcStages = 0, dstStages = 0;
    VkMemoryBarrier       mem[RLVK_SYNC1_MAX_BARRIERS];
    VkBufferMemoryBarrier buf[RLVK_SYNC1_MAX_BARRIERS];
    VkImageMemoryBarrier  img[RLVK_SYNC1_MAX_BARRIERS];
    uint32_t nMem = dep->memoryBarrierCount, nBuf = dep->bufferMemoryBarrierCount, nImg = dep->imageMemoryBarrierCount;
    if ((nMem > RLVK_SYNC1_MAX_BARRIERS) || (nBuf > RLVK_SYNC1_MAX_BARRIERS) || (nImg > RLVK_SYNC1_MAX_BARRIERS))
    {
        TRACELOG(RL_LOG_ERROR, "RLVK: sync1 shim barrier overflow (%u/%u/%u)", nMem, nBuf, nImg);
        nMem = (nMem > RLVK_SYNC1_MAX_BARRIERS)? RLVK_SYNC1_MAX_BARRIERS : nMem;
        nBuf = (nBuf > RLVK_SYNC1_MAX_BARRIERS)? RLVK_SYNC1_MAX_BARRIERS : nBuf;
        nImg = (nImg > RLVK_SYNC1_MAX_BARRIERS)? RLVK_SYNC1_MAX_BARRIERS : nImg;
    }
    for (uint32_t i = 0; i < nMem; i++)
    {
        const VkMemoryBarrier2 *b = &dep->pMemoryBarriers[i];
        srcStages |= rlvkStageMask1(b->srcStageMask, false);
        dstStages |= rlvkStageMask1(b->dstStageMask, true);
        mem[i] = (VkMemoryBarrier){ VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = rlvkAccessMask1(b->srcAccessMask),
            .dstAccessMask = rlvkAccessMask1(b->dstAccessMask) };
    }
    for (uint32_t i = 0; i < nBuf; i++)
    {
        const VkBufferMemoryBarrier2 *b = &dep->pBufferMemoryBarriers[i];
        srcStages |= rlvkStageMask1(b->srcStageMask, false);
        dstStages |= rlvkStageMask1(b->dstStageMask, true);
        buf[i] = (VkBufferMemoryBarrier){ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask       = rlvkAccessMask1(b->srcAccessMask),
            .dstAccessMask       = rlvkAccessMask1(b->dstAccessMask),
            .srcQueueFamilyIndex = b->srcQueueFamilyIndex,
            .dstQueueFamilyIndex = b->dstQueueFamilyIndex,
            .buffer = b->buffer, .offset = b->offset, .size = b->size };
    }
    for (uint32_t i = 0; i < nImg; i++)
    {
        const VkImageMemoryBarrier2 *b = &dep->pImageMemoryBarriers[i];
        srcStages |= rlvkStageMask1(b->srcStageMask, false);
        dstStages |= rlvkStageMask1(b->dstStageMask, true);
        img[i] = (VkImageMemoryBarrier){ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = rlvkAccessMask1(b->srcAccessMask),
            .dstAccessMask       = rlvkAccessMask1(b->dstAccessMask),
            .oldLayout           = b->oldLayout,
            .newLayout           = b->newLayout,
            .srcQueueFamilyIndex = b->srcQueueFamilyIndex,
            .dstQueueFamilyIndex = b->dstQueueFamilyIndex,
            .image = b->image, .subresourceRange = b->subresourceRange };
    }
    if (srcStages == 0) srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;      // sync1 forbids empty stage masks
    if (dstStages == 0) dstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    vkCmdPipelineBarrier(cmdBuffer, srcStages, dstStages, dep->dependencyFlags,
        nMem, mem, nBuf, buf, nImg, img);
}

static VKAPI_ATTR VkResult VKAPI_CALL rlvkQueueSubmit2Compat(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence fence)
{
    // rlvk submits one VkSubmitInfo2 at a time; translate each (binary semaphores only)
    for (uint32_t s = 0; s < submitCount; s++)
    {
        const VkSubmitInfo2 *in = &pSubmits[s];
        VkSemaphore          waitSems  [RLVK_SYNC1_MAX_SEMAPHORES];
        VkPipelineStageFlags waitStages[RLVK_SYNC1_MAX_SEMAPHORES];
        VkSemaphore          signalSems[RLVK_SYNC1_MAX_SEMAPHORES];
        VkCommandBuffer      cmdBufs   [RLVK_SYNC1_MAX_SEMAPHORES];
        uint32_t nWait = in->waitSemaphoreInfoCount, nSig = in->signalSemaphoreInfoCount, nCmd = in->commandBufferInfoCount;
        if ((nWait > RLVK_SYNC1_MAX_SEMAPHORES) || (nSig > RLVK_SYNC1_MAX_SEMAPHORES) || (nCmd > RLVK_SYNC1_MAX_SEMAPHORES))
        {
            TRACELOG(RL_LOG_ERROR, "RLVK: sync1 shim submit overflow (%u/%u/%u)", nWait, nSig, nCmd);
            return VK_ERROR_UNKNOWN;
        }
        for (uint32_t i = 0; i < nWait; i++)
        {
            waitSems[i]   = in->pWaitSemaphoreInfos[i].semaphore;
            waitStages[i] = rlvkStageMask1(in->pWaitSemaphoreInfos[i].stageMask, true);
        }
        for (uint32_t i = 0; i < nSig; i++) signalSems[i] = in->pSignalSemaphoreInfos[i].semaphore;
        for (uint32_t i = 0; i < nCmd; i++) cmdBufs[i]    = in->pCommandBufferInfos[i].commandBuffer;

        VkResult res = vkQueueSubmit(queue, 1, &(VkSubmitInfo){
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount   = nWait,
            .pWaitSemaphores      = waitSems,
            .pWaitDstStageMask    = waitStages,
            .commandBufferCount   = nCmd,
            .pCommandBuffers      = cmdBufs,
            .signalSemaphoreCount = nSig,
            .pSignalSemaphores    = signalSems,
        }, (s == submitCount - 1) ? fence : VK_NULL_HANDLE);
        if (res != VK_SUCCESS) return res;
    }
    return VK_SUCCESS;
}

//----------------------------------------------------------------------------------
// Module Types and Structures Definition
//----------------------------------------------------------------------------------

// Persistently-mapped buffer (one per frame-in-flight) backing ALL rlVertexBuffers of a render
// batch: the public rlVertexBuffer pointers alias mapped slices, so vertex writes land in place
typedef struct rlvkBatchBackingBuffer {
    VkBuffer            buffer;                 // Buffer handle
    VkDeviceMemory      memory;                 // Backing device memory
    void               *mapped;                 // Persistent host mapping
    u32                 sizeBytes;              // Buffer size in bytes
    u32                 freedFrame;             // Frame counter at unload (pooled-reuse fence gate)
} rlvkBatchBackingBuffer;

// Deferred GPU-object destruction: objects released while a command buffer may still reference
// them (recorded but not yet executed) are queued per frame-in-flight and destroyed once that
// frame slot's fence has been waited on, i.e. when the GPU is provably done with them
typedef struct rlvkDeadResource {
    VkBuffer            buffer;                 // Pooled buffer being evicted
    VkImage             image;
    VkImageView         view;
    VkSampler           sampler;
    VkDeviceMemory      memory;                 // Backing device memory
    VkPipeline          pipeline;               // Cached pipeline evicted by shader unload
    VkFramebuffer       framebuffer;            // Cached framebuffer evicted with a dying view
} rlvkDeadResource;

typedef struct rlvkTextureSlot {
    VkImage             image;
    VkImageView         view;
    VkSampler           sampler;
    VkDeviceMemory      memory;                 // Backing device memory
    VkFormat            format;
    int                 width, height;
    int                 mipCount;
    int                 rlFormat;
    VkImageLayout       currentLayout;         // Tracked image layout (barriers use it as oldLayout)
    VkFilter            minFilter, magFilter;  // Sampler filters (rlTextureParameters)
    VkSamplerMipmapMode mipMode;               // Sampler mipmap mode
    VkSamplerAddressMode wrapS, wrapT;         // Sampler wrap modes (GL default: repeat)
    bool                inUse;                 // Slot occupied
} rlvkTextureSlot;

// One reflected uniform: a member of a stage's default uniform block and/or a sampler.
typedef struct rlvkUniform {
    char                name[64];
    int                 vsOffset, fsOffset;     // byte offset in that stage's default block (-1 = absent)
    int                 samplerBinding;         // >= 0: sampler uniform at this set-0 binding
} rlvkUniform;

typedef struct rlvkShaderSlot {
    VkShaderModule      vertMod;                // VS module for the static-pipeline (pipeline-cache) draw path
    VkShaderModule      fragMod;                // FS module for the static-pipeline (pipeline-cache) draw path
    VkShaderModule      compMod;                // CS module (compute programs)
    VkPipeline          computePipeline;        // Monolithic compute pipeline (one per program)
    char               *pendingCode;            // rlLoadShader stash until rlLoadShaderProgram* consumes it
    int                 pendingType;            // RL_VERTEX_SHADER / RL_FRAGMENT_SHADER / RL_COMPUTE_SHADER
    bool                isCompute;              // Slot holds a compute program (vsStage doubles as its uniform block)
    rlvkUniform        *uniforms;               // reflected uniform table ("location" = index here)
    unsigned char      *vsStage;                // CPU staging for the VS default uniform block
    unsigned char      *fsStage;                // CPU staging for the FS default uniform block
    int                 locs[RL_MAX_SHADER_LOCATIONS];  // Default shader locations table (rlgl semantics)
    VkPushConstantRange pcRange;                // Push-constant range (embedded default shader path)
    u32                 bindingTexture[RLVK_MAX_TEXTURE_UNITS]; // sampler binding -> explicit texture (rlSetUniformSampler)
    int                 bindingUnit[RLVK_MAX_TEXTURE_UNITS];    // sampler binding -> GL texture unit (glUniform1i)
    int                 attribLocs[RLVK_ATTRIB_COUNT];          // canonical attribute -> shader input location (-1 absent)
    u32                 vsBlockSize, fsBlockSize;   // Default uniform block sizes per stage
    u32                 vsWriteGen, fsWriteGen;     // Bumped when a stage block is written (rlSetUniform*)
    u32                 vsPushedGen, fsPushedGen;   // Write generation last snapshotted+pushed
    u32                 uboPushedEpoch;             // Command-buffer epoch of the last push (pushes die with the cb)
    int                 uniformCount;           // Entries in the reflected uniform table
    bool                usesUbo;                // runtime-compiled (reflected uniforms); false = embedded push-constant shader
    bool                inUse;                 // Slot occupied
} rlvkShaderSlot;

typedef struct rlvkFramebufferSlot {
    u32                 width, height;          // Framebuffer dimensions (from first attachment)
    u32                 colorCount;             // Color attachments in use (MRT)
    u32                 colorTextures[8];       // Color attachment texture slots
    u32                 depthTexture;           // Depth attachment texture slot (0 = none)
    u32                 stencilTexture;         // Stencil attachment texture slot (0 = none)
    bool                hasDepth, hasStencil;   // Attachment presence flags
    bool                inUse;                 // Slot occupied
} rlvkFramebufferSlot;

typedef struct rlvkBufferSlot {
    VkBuffer            buffer;                 // Buffer handle
    VkDeviceMemory      memory;                 // Backing device memory
    void               *mapped;                 // Persistent host mapping
    u32                 sizeBytes;              // Buffer size in bytes
    u32                 freedFrame;             // Frame counter at unload (pooled-reuse fence gate)
    int                 usageHint;              // rlgl usage hint (static/dynamic/stream)
    bool                isIndex;                // Created as an index buffer
    bool                inUse;                 // Slot occupied
} rlvkBufferSlot;

// Push-constant block, byte-for-byte matching the default shader's push_constant layout
// (src/shaders/rlvk_default.vert/.frag); serves both the batch and DrawMesh, like rlgl
typedef struct rlvkPushConstants {
    f32                 mvp[16];        // 0
    f32                 colDiffuse[4];  // 64
} rlvkPushConstants;                    // 80

// Pipeline key: every GL-changeable state a pipeline bakes; equal keys share one pipeline.
// Viewport/scissor stay dynamic; constants that never vary are baked and not part of the key.
typedef struct rlvkPipelineKey {
    VkFormat            colorFormats[8];        // Attachment formats of the target scope
    VkFormat            depthFormat;            // VK_FORMAT_UNDEFINED = scope has no depth
    u32                 shaderSlot;             // Shader modules + reflected attribute locations
    int                 blendMode;              // raylib blend mode (custom factors below)
    int                 blendSrcRGB, blendDstRGB, blendEqRGB;   // RL_BLEND_CUSTOM* factors (GL enums), else 0
    int                 blendSrcA, blendDstA, blendEqA;
    unsigned char       topology;               // 0 = line list, 1 = triangle list, 2 = triangle strip
    unsigned short      vertexLayout;           // RLVK_VLAYOUT_* + mesh attribute presence mask
    unsigned char       cullMode;               // VK_CULL_MODE_*
    unsigned char       polygonMode;            // Fill / line / point
    unsigned char       samples;                // Rasterization samples
    unsigned char       colorCount;             // Color attachments in the scope
    unsigned char       depthTest, depthWrite;  // Depth state
} rlvkPipelineKey;

// One cached pipeline: the key it was built from and the pipeline itself
typedef struct rlvkPipelineEntry {
    VkPipeline          pipeline;               // Ready-to-bind pipeline
    rlvkPipelineKey     key;                    // State combo this pipeline bakes
} rlvkPipelineEntry;

//----------------------------------------------------------------------------------
// Render-pass + framebuffer caches (Vulkan 1.1 baseline)
//
// The dynamic-rendering scope model is kept conceptually (scopes open lazily, suspend
// around copies/blits, resume with LOAD), but every scope now begins a cached
// VkRenderPass into a cached VkFramebuffer. Load/store ops are part of the render-pass
// key; compatibility for pipelines only depends on formats/samples/counts, so pipeline
// creation requests a canonical LOAD-ops pass of the same shape.
// Attachment order convention everywhere: colors [0..colorCount), then the MSAA resolve
// target (when hasResolve), then depth last.
//----------------------------------------------------------------------------------
#define RLVK_MAX_RENDER_PASSES          32
#define RLVK_MAX_CACHED_FRAMEBUFFERS    64
#define RLVK_MAX_SCOPE_ATTACHMENTS      10      // 8 colors + resolve + depth
#define RLVK_DESC_SETS_PER_FRAME        1024    // pool-ring fallback: snapshot sets per frame slot
#define RLVK_COMPUTE_SETS_PER_FRAME     256     // compute dispatch snapshot sets per frame slot

typedef struct rlvkRenderPassKey {
    VkFormat            colorFormats[8];        // [i] for i < colorCount
    VkFormat            depthFormat;            // VK_FORMAT_UNDEFINED = no depth attachment
    unsigned char       colorCount;
    unsigned char       samples;                // normalized: 1 or 4 (matches pipeline multisample state)
    unsigned char       colorLoad;              // VkAttachmentLoadOp shared by every color attachment
    unsigned char       depthLoad;              // VkAttachmentLoadOp of the depth attachment
    unsigned char       depthStore;             // VkAttachmentStoreOp of the depth attachment
    unsigned char       hasResolve;             // MSAA fixed-function resolve into a 1x attachment (colorCount == 1 only)
    unsigned char       _pad[2];                // keep memcmp-comparable: always zero
} rlvkRenderPassKey;

typedef struct rlvkRenderPassEntry {
    VkRenderPass        pass;
    rlvkRenderPassKey   key;
} rlvkRenderPassEntry;

typedef struct rlvkFramebufferEntry {
    VkFramebuffer       framebuffer;
    VkRenderPass        pass;                   // compatibility class it was created against
    VkImageView         views[RLVK_MAX_SCOPE_ATTACHMENTS];
    u32                 viewCount;
    u32                 width, height;
} rlvkFramebufferEntry;

// A "VAO": records buffer slot + byte offset per vertex attribute plus the index buffer slot.
// Built by rlLoadVertexArray + rlLoadVertexBuffer + rlSetVertexAttribute at model-load time;
// consumed by rlvkDrawMesh as real vertex-buffer bindings (vkCmdSetVertexInputEXT).
typedef struct rlvkVertexArray {
    u32                 posSlot,    posOffset;
    u32                 uvSlot,     uvOffset;
    u32                 normalSlot, normalOffset;
    u32                 colorSlot,  colorOffset;
    u32                 tangentSlot, tangentOffset; // vertexTangent (normal mapping / PBR)
    u32                 uv2Slot,    uv2Offset;    // vertexTexCoord2 (lightmaps)
    u32                 instSlot,   instOffset;   // mat4 instanceTransform stream (instance rate)
    u32                 boneIdSlot, boneIdOffset; // vertexBoneIds (u8x4, unscaled)
    u32                 boneWtSlot, boneWtOffset; // vertexBoneWeights (f32x4)
    u32                 indexSlot;      // bufferSlots[] id holding the index buffer (0 = none)
    bool                inUse;                 // Slot occupied
} rlvkVertexArray;

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
typedef struct rlvkData {
    // 8B-aligned tier - slot tables holding 8B handles, descending by total size
    rlvkTextureSlot         textureSlots[RLVK_MAX_TEXTURE_SLOTS];
    rlvkBufferSlot          bufferSlots [RLVK_MAX_BUFFER_SLOTS];
    rlvkShaderSlot          shaderSlots [RLVK_MAX_SHADER_SLOTS];
    rlvkVertexArray         vertexArrays[RLVK_MAX_VAO_SLOTS];

    // 8B-aligned nested struct (contains pointers + matrix arrays)
    struct {
        // Pointers (8B)
        Matrix         *currentMatrix;
        int            *currentShaderLocs;

        // 4B-aligned, large Matrix arrays
        Matrix          stack[RL_MAX_MATRIX_STACK_SIZE];
        Matrix          projectionStereo[2];
        Matrix          viewOffsetStereo[2];
        Matrix          modelview;
        Matrix          projection;
        Matrix          transform;
        Matrix          meshMVP;                    // captured from rlSetUniformMatrix(MVP) by DrawMesh

        // 4B scalars and 4B-aligned arrays
        u32             activeTextureSlots[RLVK_MAX_TEXTURE_UNITS];   // GL texture units 0..15 (material maps use up to 10)
        int             scissorX, scissorY, scissorW, scissorH;     // Scissor rectangle (GL bottom-left origin)
        int             viewportX, viewportY, viewportW, viewportH; // Viewport rectangle (rlViewport)
        int             framebufferWidth, framebufferHeight;        // Current render dimensions
        int             blendSrcRGB, blendDstRGB, blendSrcA, blendDstA, blendEqRGB, blendEqA;  // Custom separate blend factors (GL enums)
        int             blendSrc, blendDst, blendEq;                // Custom blend factors (GL enums)
        int             blendMode;              // Current raylib blend mode
        int             cullMode;               // Face culling mode (front/back)
        int             stackCounter;           // Matrix stack depth
        int             vertexCounter;          // Vertices written into the current batch buffer
        int             currentMatrixMode;      // Current matrix mode (modelview/projection)
        u32             currentTextureSlot;     // Batch draw texture (rlSetTexture)
        u32             stateGeneration;        // Bumped by every setter feeding the pipeline key or viewport/scissor
        u32             cbEpoch;                // Bumped at every command-buffer restart (push descriptors reset)
        u32             currentShaderSlot;          // BATCH shader (rlgl currentShaderId): rlSetShader only
        u32             activeShaderSlot;           // "glUseProgram" shader: uniform writes + mesh/quad draws
        u32             currentFramebufferSlot;     // 0 = swapchain
        u32             currentVAO;                 // mesh path: bound vertex array (0 = none)
        int             activeTextureUnit;          // GL glActiveTexture unit (rlActiveTextureSlot)
        u32             samplerTextures[4];         // rlSetUniformSampler registrations (units 1..4, rlgl semantics)
        u32             currentVBO;                 // last bound vertex buffer (for rlSetVertexAttribute)
        f32             meshColDiffuse[4];          // captured from rlSetUniform(COLOR_DIFFUSE) by DrawMesh
        f32             texcoordx, texcoordy;
        f32             normalx, normaly, normalz;
        f32             pointSize;
        f32             lineWidth;

        // 1B (chars and bools)
        unsigned char   colorr, colorg, colorb, colora;
        unsigned char   clearR, clearG, clearB, clearA;
        bool            colorMask[4];
        bool            transformRequired;
        bool            stereoRender;
        bool            depthTest, depthWrite;
        bool            cullEnabled;
        bool            scissorEnabled;
        bool            colorBlendEnabled;
        bool            customBlendModified;
        bool            wireMode;
        bool            pointMode;
        bool            smoothLines;
    } State;

    // Current dynamic-rendering scope (0 = swapchain/backbuffer, else fbSlots[] id). GL render
    // textures are BOTTOM-UP, so FBO scopes render without the Y-flip (flipY=false, CCW front);
    // raylib's negative-source-rect convention then displays them correctly.
    struct rlvkScope {
        VkFormat        colorFormats[8];    // attachment formats (float formats disable blending)
        u32             fbSlot;
        u32             width, height;
        u32             colorCount;     // color attachments in the open scope (swapchain = 1)
        u32             samples;        // rasterization samples of the open scope (MSAA on swapchain only)
        bool            flipY;
    } scope;
    int                     deadResourceCount[RLVK_FRAME_INDEX_COUNT];
    int                     pipelineCount;      // entries used in pipelines[]
    int                     renderPassCount;    // entries used in renderPasses[]
    int                     framebufferCount;   // entries used in framebuffers[]
    int                     msaaSamples;        // requested via rlvkSetMsaaSamples (1 = off)
    u32                     blitReadFb;         // rlBindFramebuffer(RL_READ_FRAMEBUFFER, ...) source

    bool                    frameActive;        // a swapchain image is acquired and the render scope is open
    bool                    frameConsumed;      // rlReadScreenPixels already ended+presented this frame
    bool                    acquireWaited;      // this frame's acquire semaphore was consumed by an earlier submit (mid-frame flush)

    // 8B-aligned smaller arrays
    rlvkBatchBackingBuffer  batchBacking[RLVK_FRAME_INDEX_COUNT];
    rlvkBatchBackingBuffer  arena       [RLVK_FRAME_INDEX_COUNT];   // per-frame bump arena for flush data
    VkDeviceSize            arenaOffset [RLVK_FRAME_INDEX_COUNT];   // reset each frame in rlvkBeginFrame
    VkDeviceSize            arenaWanted [RLVK_FRAME_INDEX_COUNT];   // total bytes the frame demanded (grow arena when it exceeds capacity)
    VkCommandPool           cmdPools    [RLVK_FRAME_INDEX_COUNT];
    VkCommandBuffer         cmdBuffers     [RLVK_FRAME_INDEX_COUNT];

    // 8B-aligned struct (contains pointers internally)
    rlRenderBatch           defaultBatch;

    // 8B handles
    VkInstance              instance;
    VkPhysicalDevice        physicalDevice;
    VkDevice                device;
    VkQueue                 graphicsQueue;
    VkQueue                 transferQueue;
    VkSurfaceKHR            surface;
    VkDescriptorSetLayout   set0Layout;         // push-descriptor layout: texture units + per-stage UBOs
    VkPipelineLayout        pipelineLayout;     // shared by every pipeline: set 0 + push-constant range

    // Swapchain + per-frame present synchronization
    VkSwapchainKHR          swapchain;
    VkImage                 swapchainImages   [RLVK_MAX_SWAPCHAIN_IMAGES];
    VkImageView             swapchainViews    [RLVK_MAX_SWAPCHAIN_IMAGES];
    VkSemaphore             renderSemaphores  [RLVK_MAX_SWAPCHAIN_IMAGES]; // signaled by submit, waited by present (per image)
    VkSemaphore             acquireSemaphores [RLVK_FRAME_INDEX_COUNT];    // signaled by acquire (per frame-in-flight)
    VkFence                 frameFences       [RLVK_FRAME_INDEX_COUNT];    // CPU wait before reusing a frame's resources
    VkImage                 depthImage [RLVK_FRAME_INDEX_COUNT];   // one depth buffer per frame-in-flight
    VkImageView             depthView  [RLVK_FRAME_INDEX_COUNT];
    VkDeviceMemory          depthMemory[RLVK_FRAME_INDEX_COUNT];
    VkImage                 msaaImage  [RLVK_FRAME_INDEX_COUNT];   // 4x color target (resolved into interImage)
    VkImageView             msaaView   [RLVK_FRAME_INDEX_COUNT];
    VkDeviceMemory          msaaMemory [RLVK_FRAME_INDEX_COUNT];
    VkImage                 interImage [RLVK_FRAME_INDEX_COUNT];   // 1x UNMIRRORED color target (flip-blitted to swapchain)
    VkImageView             interView  [RLVK_FRAME_INDEX_COUNT];
    VkDeviceMemory          interMemory[RLVK_FRAME_INDEX_COUNT];
    rlvkDeadResource    deadResources[RLVK_FRAME_INDEX_COUNT][RLVK_MAX_DEAD_RESOURCES];   // deferred destruction, fence-gated
    rlvkPipelineEntry       pipelines[RLVK_MAX_PIPELINES];  // cached pipelines by baked-state key
    rlvkRenderPassEntry     renderPasses[RLVK_MAX_RENDER_PASSES];        // cached render passes by scope-shape key
    rlvkFramebufferEntry    framebuffers[RLVK_MAX_CACHED_FRAMEBUFFERS];  // cached framebuffers by pass + view set
    VkPipelineCache         pipelineCache;      // driver pipeline cache, persisted to disk across runs
    VkPipeline              boundPipeline;      // currently bound pipeline (skip redundant binds)
    rlvkShaderSlot         *lastUboShader;      // shader whose blocks hold UBO bindings 16/17 (they overwrite each other)
    VkImageView             pushedView   [RLVK_MAX_TEXTURE_UNITS];  // last view pushed per unit binding (skip redundant pushes; doubles as the set-0 shadow on the pool-ring path)
    VkSampler               pushedSampler[RLVK_MAX_TEXTURE_UNITS];  // last sampler pushed per unit binding

    // Pool-ring fallback state (devices without VK_KHR_push_descriptor): CPU shadow of the
    // UBO bindings + per-frame descriptor pools; a fresh set is allocated, fully written and
    // bound at the next draw whenever the shadow changed (rlvkFlushSet0)
    VkDescriptorBufferInfo  shadowUbo[2];                       // [0]=VS binding, [1]=FS binding
    VkDescriptorPool        descPools[RLVK_FRAME_INDEX_COUNT];  // reset with each frame slot's fence
    bool                    set0Dirty;                          // shadow changed since the last bound set

    // Compute state (core Vulkan 1.0/1.1 features only). Fixed set-0 layout for every compute
    // program: bindings 0..7 SSBO, 8..11 storage image, 12..13 combined sampler, 14 the
    // implicit loose-uniform block. GL-style bind-then-dispatch: rlBindShaderBuffer /
    // rlBindImageTexture record here; rlComputeShaderDispatch snapshots into a fresh set.
    VkDescriptorSetLayout   computeSetLayout;
    VkPipelineLayout        computePipelineLayout;
    VkDescriptorPool        computeDescPools[RLVK_FRAME_INDEX_COUNT];   // reset with each frame slot's fence
    u32                     computeSSBO[8];                     // buffer slots bound per SSBO index (0 = unbound)
    u32                     computeImage[4];                    // texture slots bound per image unit (0 = unbound)
    VkExtent2D              swapchainExtent;
    VkFormat                swapchainFormat;
    VkFormat                depthFormat;
    u32                     swapchainImageCount;
    u32                     currentImageIndex;

    // 8B pointers
    rlRenderBatch          *currentBatch;
    int                    *defaultShaderLocs;

    // 8B scalar and pointer
    u64                     frameCounter;
    void                   *shadercCompiler;    // shaderc_compiler_t (shaderc_shared.dll), NULL if unavailable

    // 4B-aligned tier - fbSlots (4B inner alignment)
    rlvkFramebufferSlot     fbSlots[RLVK_MAX_FRAMEBUFFER_SLOTS];

    // 4B-aligned nested struct
    struct {
        u32         apiVersion;         // Selected device's VkPhysicalDeviceProperties.apiVersion
        bool        memoryPriority;     // VK_EXT_memory_priority available (optional)
        bool        pageableMemory;     // VK_EXT_pageable_device_local_memory available (optional)
        bool        graphicsPipelineLibrary;    // VK_EXT_graphics_pipeline_library available (fast-linked pipelines)
        // Vulkan 1.1 retarget: every former 1.3-floor requirement is a queried capability.
        // Each flag's 1.1-core fallback lands milestone by milestone; once a fallback is in,
        // the flag either becomes a pure fast-path gate or is deleted along with the old path.
        bool        dynamicRendering;   // 1.3 core / VK_KHR_dynamic_rendering (unused: render-pass cache is the single path)
        bool        synchronization2;   // 1.3 core / VK_KHR_synchronization2 (fallback: sync1 shim)
        bool        pushDescriptor;     // VK_KHR_push_descriptor (fallback: per-frame descriptor-pool ring)
        bool        bresenhamLines;     // VK_EXT/KHR_line_rasterization (fallback: default line raster, cosmetic delta)
        bool        wideLines;          // Core optional feature (fallback: clamp rlSetLineWidth to 1.0)
        bool        fillModeNonSolid;   // Core optional feature (fallback: rlEnableWireMode/PointMode no-op)
    } Caps;

    // 4B scalars
    u32                     graphicsFamily;
    u32                     transferFamily;
    u32                     frameIndex;
    u32                     defaultTextureSlot;
    u32                     defaultShaderSlot;
    u32                     dummyAttribSlot;    // divisor-0 broadcast buffer: [0]=vec2(0,0) uv, [8]=white color
} rlvkData;

static rlvkData     RLVK = { 0 };
static bool         isGpuReady = false;

// Binding signature: the shader + mesh vertex-buffer slots/offsets + texture bound by a mesh
// draw. When it matches the previous draw the texture push and buffer bindings are redundant
// and skipped (invalidated on every command-buffer restart, same as above).
typedef struct rlvkBindingSig {
    int shaderSlot, texSlot;
    int posSlot, uvSlot, normalSlot, colorSlot, uv2Slot, tangentSlot, boneIdSlot, boneWtSlot;
    ull posOff, uvOff, normalOff, colorOff, uv2Off, tangentOff, boneIdOff, boneWtOff;
} rlvkBindingSig;
static bool           s_bindingValid = false;
static rlvkBindingSig s_bindingSig;

// Viewport/scissor signature: the only dynamic pipeline state. Consecutive draws with the
// same inputs skip the two set commands (invalidated on every command-buffer restart).
typedef struct rlvkViewportSig {
    int vx, vy, vw, vh;
    int scEn, scx, scy, scw, sch;
    int scopeW, scopeH, flipY;
} rlvkViewportSig;
static bool            s_viewportValid = false;
static rlvkViewportSig s_viewportSig;

// Pipeline fast path: when no key-feeding state changed since the last draw, the previous
// pipeline and viewport/scissor are still exactly right and the whole bind path is skipped
static bool           s_pipelineFastValid = false;

// Debug trace flags, resolved once: getenv scans the environment block and is far too slow
// for per-draw paths
static int rlvkDebugFlag(const char *name, int *cache)
{
    if (*cache < 0) *cache = (getenv(name) != NULL);
    return *cache;
}
static int s_dbgSamplers = -1, s_dbgFbo = -1, s_dbgFlush = -1, s_dbgVao = -1, s_dbgPipe = -1, s_dbgVtx = -1;
static ill s_memLocalBytes, s_memHostBytes; static int s_memAllocCount, s_vboCreateCount, s_vboReuseCount, s_dbgMem = -1;   // RLVK_MEM_REPORT accounting

// GPU timestamp trace (RLVK_GPU_TRACE env): three timestamps per frame measure the GPU span
// of scene rendering vs the present chain (resolve/flip blit + layout transitions), read back
// one frame ring behind. Averages print every 512 frames.
static VkQueryPool s_gpuPool = VK_NULL_HANDLE;
static int         s_dbgGpu = -1;
static f32         s_gpuPeriod;      // nanoseconds per timestamp tick
static f64         s_gpuScene, s_gpuPresent;
static int         s_gpuFrames;
static u32            s_lastGeneration;
static u32            s_lastShaderSlot;
static unsigned short s_lastVertexLayout;
static unsigned char  s_lastTopology;
static f64          rlCullDistanceNear = RL_CULL_DISTANCE_NEAR;
static f64          rlCullDistanceFar  = RL_CULL_DISTANCE_FAR;

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
static Matrix       rlvkMatrixIdentity (void);                     // Get identity matrix
static Matrix       rlvkMatrixMultiply (Matrix left, Matrix right); // Get two matrix multiplication result
static Matrix       rlvkMatrixTranspose(Matrix mat);               // Get transposed input matrix
static Matrix       rlvkMatrixInvert   (Matrix mat);               // Get inverted input matrix

static int          rlvkGetPixelDataSize     (int width, int height, int format);  // Get pixel data size in bytes
static VkFormat     rlvkGetVkTextureFormat(int rlFormat);          // Get Vulkan format for a raylib pixel format

static bool         rlvkInitInstance      (void);                  // Initialize Vulkan instance
static bool         rlvkPickPhysicalDevice(void);                  // Pick a Vulkan 1.1+ capable physical device
static bool         rlvkInitLogicalDevice (void);                  // Initialize logical device, features and extensions
static bool         rlvkInitDefaultShader (void);                  // Initialize pipeline layout and embedded default shader
static void         rlvkBeginFrame        (void);                  // Begin frame: acquire image, open rendering scope
static void         rlvkDeferDestroy      (VkBuffer buffer, VkImage image, VkImageView view, VkSampler sampler, VkDeviceMemory memory, VkPipeline pipeline); // Queue GPU objects for fence-gated destruction
static void         rlvkResumeSwapchainScope(VkCommandBuffer cmdBuffer); // Re-open the swapchain scope preserving content
static void         rlvkFlushFrame        (void);                  // Submit current recording, wait, resume (GL-ordered readbacks)
static void         rlvkWaitInFlightFrames(void);                  // Fence-wait all submitted frames (no vkDeviceWaitIdle)
static void         rlvkInitPipelineCache (void);                  // Create the driver pipeline cache, seed from disk
static void         rlvkSavePipelineCache (void);                  // Persist the driver pipeline cache to disk
static bool         rlvkBindPipeline      (VkCommandBuffer cmdBuffer, unsigned char topology, unsigned short vertexLayout, u32 shaderSlot); // Bind the cached pipeline for the current state
static void         rlvkBindDummyAttribBuffers(VkCommandBuffer cmdBuffer, unsigned short vertexLayout, rlvkShaderSlot *shader); // Bind dummy buffers at the layout's broadcast bindings
static void         rlvkFinishSwapchainImage(VkCommandBuffer cmdBuffer); // Flip-blit the frame into the swapchain image
static void         rlvkPushTexture       (VkCommandBuffer cmdBuffer, u32 binding, u32 textureSlot); // Push a texture descriptor at a GL texture unit binding
static u32          rlvkCreateVBO         (const void *data, int size, bool isIndex, bool dynamic); // Create a buffer slot (static: device-local, dynamic: host-mapped)
static void         rlvkUploadBuffer      (VkBuffer dst, u32 dstOffset, const void *data, u32 size); // Copy into a device-local buffer (in-frame or one-shot)
static void         rlvkShaderWriteUniform(rlvkShaderSlot *shader, int loc, const void *data, u32 bytes); // Write a uniform into the shader staging blocks
static void         rlvkBindShaderUbos    (VkCommandBuffer cmdBuffer, rlvkShaderSlot *shader); // Snapshot uniform staging and push UBO descriptors
static void         rlvkBindShaderSamplers(VkCommandBuffer cmdBuffer, rlvkShaderSlot *shader, bool includeBinding0); // Push the shader sampler bindings
static void         rlvkFlushSet0         (VkCommandBuffer cmdBuffer); // Pool-ring fallback: bind a snapshot set before a draw (no-op with native push descriptors)
static VKAPI_ATTR void VKAPI_CALL rlvkPushDescriptorSetCompat(VkCommandBuffer cmdBuffer, VkPipelineBindPoint bindPoint,
    VkPipelineLayout layout, uint32_t set, uint32_t writeCount, const VkWriteDescriptorSet *writes); // Shadow-updating shim installed when VK_KHR_push_descriptor is absent
static void         rlvkLoadEntrypoints   (void);                  // Load device-level entry points into the vk dispatch table
static bool         rlvkInitSet0Layout    (void);                  // Initialize the push-descriptor set layout
static bool         rlvkInitFrameRing     (void);                  // Initialize per-frame command pools, semaphores, fences
static bool         rlvkCreateBatchBacking (int bufferElements, rlvkBatchBackingBuffer *out); // Create a persistent-mapped batch backing buffer
static void         rlvkDestroyBatchBacking(rlvkBatchBackingBuffer *b);   // Unmap and release a batch backing buffer
static u32          rlvkAllocTextureSlot     (void);               // Get a free texture slot index
static u32          rlvkAllocShaderSlot      (void);               // Get a free shader slot index
static u32          rlvkAllocFramebufferSlot (void);               // Get a free framebuffer slot index
static u32          rlvkAllocBufferSlot      (void);               // Get a free buffer slot index
static u32          rlvkFindMemoryType       (u32 typeBits, VkMemoryPropertyFlags wanted); // Find a device memory type index
static VkDeviceMemory rlvkAllocMemory       (VkMemoryRequirements memReq, VkMemoryPropertyFlags props); // Allocate resource memory (type + priority)

// rlvkAttachSurface is declared in the public section above (rlvk.h, not rlgl.h).

//----------------------------------------------------------------------------------
// Runtime GLSL compilation (shaderc, relaxed Vulkan rules) + SPIR-V uniform reflection.
// shaderc_shared.dll loads at RUNTIME (MSVC static libs are not MinGW-linkable); relaxed rules
// accept stock GL-dialect GLSL, reflection recovers glGetUniformLocation/glUniform* semantics
//----------------------------------------------------------------------------------

#define RLVK_SHADERC_FUNCS                                                \
    RLVK_SC_FUNC(shaderc_compiler_initialize)                             \
    RLVK_SC_FUNC(shaderc_compile_options_initialize)                      \
    RLVK_SC_FUNC(shaderc_compile_options_release)                         \
    RLVK_SC_FUNC(shaderc_compile_options_set_target_env)                  \
    RLVK_SC_FUNC(shaderc_compile_options_set_auto_bind_uniforms)          \
    RLVK_SC_FUNC(shaderc_compile_options_set_auto_map_locations)          \
    RLVK_SC_FUNC(shaderc_compile_options_set_vulkan_rules_relaxed)        \
    RLVK_SC_FUNC(shaderc_compile_options_set_optimization_level)          \
    RLVK_SC_FUNC(shaderc_compile_options_set_generate_debug_info)         \
    RLVK_SC_FUNC(shaderc_compiler_release)                                \
    RLVK_SC_FUNC(shaderc_compile_options_set_binding_base_for_stage)      \
    RLVK_SC_FUNC(shaderc_compile_into_spv)                                \
    RLVK_SC_FUNC(shaderc_result_get_compilation_status)                   \
    RLVK_SC_FUNC(shaderc_result_get_bytes)                                \
    RLVK_SC_FUNC(shaderc_result_get_length)                               \
    RLVK_SC_FUNC(shaderc_result_get_error_message)                        \
    RLVK_SC_FUNC(shaderc_result_release)

#define RLVK_SC_FUNC(_func) static __typeof__(_func) *p_##_func;
RLVK_SHADERC_FUNCS
#undef RLVK_SC_FUNC

// Load the shaderc shared library at runtime and resolve the compiler entry points
static bool rlvkLoadShaderc(void)
{
#if defined(_WIN32)
    void *lib = LoadLibraryA("shaderc_shared.dll");
    if (!lib) return false;
#define RLVK_SC_FUNC(_func) p_##_func = (__typeof__(_func) *)GetProcAddress(lib, #_func); if (!p_##_func) return false;
    RLVK_SHADERC_FUNCS
#undef RLVK_SC_FUNC
#else
    // Linux/Android/macOS: try the common sonames (the Android NDK ships libshaderc.so;
    // desktop SDK/distro builds ship libshaderc_shared)
    static const char *names[] = {
        "libshaderc_shared.so.1", "libshaderc_shared.so", "libshaderc.so",
        "libshaderc_shared.dylib", "libshaderc_shared.1.dylib",
    };
    void *lib = NULL;
    for (size_t i = 0; (i < RLVK_COUNTOF(names)) && !lib; i++) lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
#define RLVK_SC_FUNC(_func) p_##_func = (__typeof__(_func) *)dlsym(lib, #_func); if (!p_##_func) return false;
    RLVK_SHADERC_FUNCS
#undef RLVK_SC_FUNC
#endif
    RLVK.shadercCompiler = p_shaderc_compiler_initialize();
    return RLVK.shadercCompiler != NULL;
}

// Stock raylib default shader, GLSL 330 (mirrors rlgl.h's defaultVShaderCode/defaultFShaderCode)
static const char *rlvkDefaultVShaderCode =
    "#version 330                       \n"
    "in vec3 vertexPosition;            \n"
    "in vec2 vertexTexCoord;            \n"
    "in vec4 vertexColor;               \n"
    "out vec2 fragTexCoord;             \n"
    "out vec4 fragColor;                \n"
    "uniform mat4 mvp;                  \n"
    "void main()                        \n"
    "{                                  \n"
    "    fragTexCoord = vertexTexCoord; \n"
    "    fragColor = vertexColor;       \n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0); \n"
    "}                                  \n";

static const char *rlvkDefaultFShaderCode =
    "#version 330       \n"
    "in vec2 fragTexCoord;              \n"
    "in vec4 fragColor;                 \n"
    "out vec4 finalColor;               \n"
    "uniform sampler2D texture0;        \n"
    "uniform vec4 colDiffuse;           \n"
    "void main()                        \n"
    "{                                  \n"
    "    vec4 texelColor = texture(texture0, fragTexCoord);   \n"
    "    finalColor = texelColor*colDiffuse*fragColor;        \n"
    "}                                  \n";

// GL GLSL 330 allows identifiers that Vulkan GLSL reserves ("sampler" as a struct member in
// fog.fs etc.). Rename whole-word occurrences so relaxed compilation accepts stock shaders.
// Returns NULL when nothing needed renaming; else an RL_MALLOC'd rewritten copy.
static char *rlvkSanitizeGlsl(const char *src)
{
    static const char *bad = "sampler";      // whole word only ("sampler2D" is untouched)
    static const char *fix = "sampler_";
    size_t badLen = strlen(bad), n = 0;
    for (const char *c = strstr(src, bad); c; c = strstr(c + badLen, bad))
    {
        char prev = (c == src) ? 0 : c[-1], next = c[badLen];
        bool prevId = (prev == '_') || isalnum((unsigned char)prev);
        bool nextId = (next == '_') || isalnum((unsigned char)next);
        if (!prevId && !nextId) n++;
    }
    if (n == 0) return NULL;
    char *out = (char *)RL_MALLOC(strlen(src) + n*(strlen(fix) - badLen) + 1);
    char *w = out;
    const char *r = src;
    while (*r)
    {
        if (strncmp(r, bad, badLen) == 0)
        {
            char prev = (r == src) ? 0 : r[-1], next = r[badLen];
            bool prevId = (prev == '_') || isalnum((unsigned char)prev);
            bool nextId = (next == '_') || isalnum((unsigned char)next);
            if (!prevId && !nextId) { strcpy(w, fix); w += strlen(fix); r += badLen; continue; }
        }
        *w++ = *r++;
    }
    *w = 0;
    return out;
}

// Vulkan clip-z is [0,1] while GL-dialect shaders assume [-1,1]: rename the user's main()
// (whole-word: GLSL has no other legal whole-word `main` outside comments, where a rename is
// harmless) and append a wrapper that remaps gl_Position.z after it runs. One behavior on
// every device - the embedded default shader bakes the same epilogue; depth_clip_control is
// deliberately not used. Returns an RL_MALLOC'd rewritten copy.
static char *rlvkInjectClipZEpilogue(const char *src)
{
    static const char *epilogue =
        "\nvoid main() { rlvk_main_(); gl_Position.z = (gl_Position.z + gl_Position.w)*0.5; }\n";
    static const char *bad = "main";
    static const char *fix = "rlvk_main_";
    size_t badLen = strlen(bad), fixLen = strlen(fix), n = 0;
    for (const char *c = strstr(src, bad); c; c = strstr(c + badLen, bad))
    {
        char prev = (c == src) ? 0 : c[-1], next = c[badLen];
        bool prevId = (prev == '_') || isalnum((unsigned char)prev);
        bool nextId = (next == '_') || isalnum((unsigned char)next);
        if (!prevId && !nextId) n++;
    }
    char *out = (char *)RL_MALLOC(strlen(src) + n*(fixLen - badLen) + strlen(epilogue) + 1);
    char *w = out;
    const char *r = src;
    while (*r)
    {
        if (strncmp(r, bad, badLen) == 0)
        {
            char prev = (r == src) ? 0 : r[-1], next = r[badLen];
            bool prevId = (prev == '_') || isalnum((unsigned char)prev);
            bool nextId = (next == '_') || isalnum((unsigned char)next);
            if (!prevId && !nextId) { memcpy(w, fix, fixLen); w += fixLen; r += badLen; continue; }
        }
        *w++ = *r++;
    }
    strcpy(w, epilogue);
    return out;
}

// GLSL -> SPIR-V through shaderc with relaxed Vulkan rules. Optimization stays OFF so OpName /
// OpMemberName debug info survives for reflection. Returned words are RL_MALLOC'd.
static bool rlvkCompileGlsl(const char *source, int stage /*0=vs 1=fs 2=cs*/, u32 **outWords, size_t *outWordCount)
{
    // Vertex stage: clip-z remap epilogue (see rlvkInjectClipZEpilogue)
    char *patched = (stage == 0)? rlvkInjectClipZEpilogue(source) : NULL;
    if (patched) source = patched;

    shaderc_compile_options_t opts = p_shaderc_compile_options_initialize();
    // Baseline target: Vulkan 1.1 / SPIR-V 1.3 - modules stay loadable on every supported
    // device (a 1.3-era target would emit SPIR-V 1.6, invalid on 1.1 drivers)
    p_shaderc_compile_options_set_target_env(opts, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    p_shaderc_compile_options_set_vulkan_rules_relaxed(opts, true);
    // NOTE: spirv-opt stays OFF: it strips the symbol names reflection needs, and gains only
    // ~0.15% even with debug info kept. The residual ~2% fragment-ALU deficit vs rlgl was
    // isolated to NVIDIA's separate GL/Vulkan compiler backends (controlled three-way test,
    // all spec-level levers audited); not addressable from application code, recheck after
    // driver updates.
    p_shaderc_compile_options_set_auto_bind_uniforms(opts, true);
    p_shaderc_compile_options_set_auto_map_locations(opts, true);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_vertex_shader,   shaderc_uniform_kind_buffer, RLVK_UBO_BINDING_VS);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_fragment_shader, shaderc_uniform_kind_buffer, RLVK_UBO_BINDING_FS);
    // Vertex-stage samplers start at binding 8 so they never collide with FS samplers (0..7):
    // both stages auto-bind from 0 otherwise (vertex texture fetch, e.g. displacement maps)
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_vertex_shader,   shaderc_uniform_kind_texture, 8);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_vertex_shader,   shaderc_uniform_kind_sampler, 8);
    // Compute stage: auto-bound resources land in the fixed compute set-0 layout ranges
    // (SSBOs declare explicit std430 bindings 0..7 themselves; images 8..11, samplers 12..13,
    // the implicit loose-uniform block at 14)
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_compute_shader, shaderc_uniform_kind_image,   8);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_compute_shader, shaderc_uniform_kind_texture, 12);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_compute_shader, shaderc_uniform_kind_sampler, 12);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_compute_shader, shaderc_uniform_kind_buffer,  14);

    shaderc_shader_kind kind = (stage == 0) ? shaderc_vertex_shader : (stage == 1) ? shaderc_fragment_shader : shaderc_compute_shader;
    shaderc_compilation_result_t res = p_shaderc_compile_into_spv(
        (shaderc_compiler_t)RLVK.shadercCompiler, source, strlen(source), kind, "rlvk", "main", opts);
    p_shaderc_compile_options_release(opts);
    if (patched) RL_FREE(patched);

    if (p_shaderc_result_get_compilation_status(res) != shaderc_compilation_status_success)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: GLSL compile failed:\n%s", p_shaderc_result_get_error_message(res));
        p_shaderc_result_release(res);
        return false;
    }
    size_t bytes = p_shaderc_result_get_length(res);
    *outWords = (u32 *)RL_MALLOC(bytes);
    memcpy(*outWords, p_shaderc_result_get_bytes(res), bytes);
    *outWordCount = bytes/4;
    p_shaderc_result_release(res);
    if (getenv("RLVK_DUMP_SPV"))   // debug: write the module for spirv-dis inspection
    {
        char path[256]; snprintf(path, sizeof(path), "%s/rlvk_dump_stage%d.spv", getenv("RLVK_DUMP_SPV"), stage);
        FILE *f = fopen(path, "wb"); if (f) { fwrite(*outWords, 4, *outWordCount, f); fclose(f); }
    }
    return true;
}

// Minimal SPIR-V reflection: default-uniform-block members (name/offset), block binding/size,
// samplers (name/binding), and vertex-stage inputs (name/location).
typedef struct rlvkSpvReflection {
    struct { char name[64]; u32 offset; } members[RLVK_MAX_SHADER_UNIFORMS];
    struct { char name[64]; int binding; }     samplers[RLVK_MAX_TEXTURE_UNITS];
    struct { char name[64]; int location; }    inputs[16];
    struct { char name[64]; int location; }    outputs[16];
    u32 blockBinding, blockSize;
    int      memberCount, samplerCount, inputCount, outputCount;
    bool     hasBlock;
} rlvkSpvReflection;

// Reflect a SPIR-V module: default uniform block members, samplers and vertex inputs
static void rlvkReflectSpv(const u32 *spv, size_t wordCount, rlvkSpvReflection *out)
{
    enum {
        SpvOpName = 5, SpvOpMemberName = 6, SpvOpTypeStruct = 30, SpvOpTypeArray = 28,
        SpvOpConstant = 43, SpvOpTypePointer = 32, SpvOpVariable = 59,
        SpvOpDecorate = 71, SpvOpMemberDecorate = 72,
        SpvDecorationArrayStride = 6, SpvDecorationLocation = 30, SpvDecorationBinding = 33, SpvDecorationOffset = 35,
        SpvStorageUniformConstant = 0, SpvStorageInput = 1, SpvStorageUniform = 2, SpvStorageOutput = 3,
    };
    memset(out, 0, sizeof(*out));
    if (wordCount < 5 || spv[0] != 0x07230203) return;
    u32 bound = spv[3];

    const char **idName      = (const char **)RL_CALLOC(bound, sizeof(char *));
    int         *idBinding   = (int *)RL_MALLOC(bound*sizeof(int));
    int         *idLoc       = (int *)RL_MALLOC(bound*sizeof(int));
    u32         *ptrType     = (u32 *)RL_CALLOC(bound, sizeof(u32));
    u32         *arrElem     = (u32 *)RL_CALLOC(bound, sizeof(u32));   // OpTypeArray: element type
    u32         *arrLenId    = (u32 *)RL_CALLOC(bound, sizeof(u32));   // OpTypeArray: length const id
    u32         *arrStride   = (u32 *)RL_CALLOC(bound, sizeof(u32));   // ArrayStride decoration
    u32         *constVal    = (u32 *)RL_CALLOC(bound, sizeof(u32));   // OpConstant value (word 3)
    for (u32 k = 0; k < bound; k++) { idBinding[k] = -1; idLoc[k] = -1; }

    struct { u32 structId, member; const char *name; } mnames[256]; int mnameCount = 0;
    struct { u32 structId, member, offset; }            moffs [256]; int moffCount  = 0;
    struct { u32 id, ptrTypeId, storage; }              vars  [128]; int varCount   = 0;
    struct { u32 id; u32 members[32]; u32 count; } structs[32]; int structCount = 0;

    size_t i = 5;
    while (i < wordCount)
    {
        u32 w = spv[i]; u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount) break;
        const u32 *a = spv + i + 1;
        switch (op)
        {
            case SpvOpName:       if (a[0] < bound) idName[a[0]] = (const char *)&a[1]; break;
            case SpvOpMemberName: if (mnameCount < 256) { mnames[mnameCount].structId = a[0]; mnames[mnameCount].member = a[1]; mnames[mnameCount].name = (const char *)&a[2]; mnameCount++; } break;
            case SpvOpTypeStruct:
                if (structCount < 32)
                {
                    structs[structCount].id = a[0];
                    structs[structCount].count = (len - 2 < 32) ? (len - 2) : 32;
                    for (u32 m = 0; m < structs[structCount].count; m++) structs[structCount].members[m] = a[1 + m];
                    structCount++;
                }
                break;
            case SpvOpTypeArray:  if (a[0] < bound) { arrElem[a[0]] = a[1]; arrLenId[a[0]] = a[2]; } break;
            case SpvOpConstant:   if (a[1] < bound) constVal[a[1]] = a[2]; break;
            case SpvOpTypePointer: if (a[0] < bound) ptrType[a[0]] = a[2]; break;
            case SpvOpVariable:   if (varCount < 128) { vars[varCount].id = a[1]; vars[varCount].ptrTypeId = a[0]; vars[varCount].storage = a[2]; varCount++; } break;
            case SpvOpDecorate:
                if (a[1] == SpvDecorationBinding     && a[0] < bound) idBinding[a[0]] = (int)a[2];
                if (a[1] == SpvDecorationLocation    && a[0] < bound) idLoc[a[0]]     = (int)a[2];
                if (a[1] == SpvDecorationArrayStride && a[0] < bound) arrStride[a[0]] = a[2];
                break;
            case SpvOpMemberDecorate:
                if (a[2] == SpvDecorationOffset && moffCount < 256) { moffs[moffCount].structId = a[0]; moffs[moffCount].member = a[1]; moffs[moffCount].offset = a[3]; moffCount++; }
                break;
            default: break;
        }
        i += len;
    }

    #define RLVK_FIND_STRUCT(_sid) ({ int _f = -1; for (int _t = 0; _t < structCount; _t++) if (structs[_t].id == (_sid)) { _f = _t; break; } _f; })
    #define RLVK_MEMBER_NAME(_sid, _m) ({ const char *_n = NULL; for (int _t = 0; _t < mnameCount; _t++) if (mnames[_t].structId == (_sid) && mnames[_t].member == (_m)) { _n = mnames[_t].name; break; } _n; })
    #define RLVK_MEMBER_OFF(_sid, _m)  ({ u32 _o = 0; for (int _t = 0; _t < moffCount; _t++) if (moffs[_t].structId == (_sid) && moffs[_t].member == (_m)) { _o = moffs[_t].offset; break; } _o; })

    for (int v = 0; v < varCount; v++)
    {
        const char *name = (vars[v].id < bound) ? idName[vars[v].id] : NULL;
        if (vars[v].storage == SpvStorageUniform)
        {
            u32 structId = (vars[v].ptrTypeId < bound) ? ptrType[vars[v].ptrTypeId] : 0;
            const char *structName = (structId && structId < bound) ? idName[structId] : NULL;
            if (!structName || strcmp(structName, "gl_DefaultUniformBlock") != 0) continue;

            out->hasBlock = true;
            out->blockBinding = (idBinding[vars[v].id] >= 0) ? (u32)idBinding[vars[v].id] : 0;
            int bs = RLVK_FIND_STRUCT(structId);
            u32 maxOff = 0;
            if (bs >= 0) for (u32 m = 0; m < structs[bs].count; m++)
            {
                const char *mn = RLVK_MEMBER_NAME(structId, m);
                u32         mo = RLVK_MEMBER_OFF(structId, m);
                if (mo > maxOff) maxOff = mo;
                if (!mn) continue;
                u32 T = structs[bs].members[m];

                // Array of structs (e.g. "uniform Light lights[4]"): flatten to "lights[i].member",
                // the composite names GL's glGetUniformLocation exposes and rlights.h queries.
                u32 elemT = (T < bound) ? arrElem[T] : 0;
                int es = elemT ? RLVK_FIND_STRUCT(elemT) : -1;
                if (es >= 0)
                {
                    u32 stride = arrStride[T];
                    u32 count  = (arrLenId[T] < bound) ? constVal[arrLenId[T]] : 0;
                    if (count > 16) count = 16;
                    for (u32 e = 0; e < count; e++)
                        for (u32 sm = 0; sm < structs[es].count; sm++)
                        {
                            const char *sn = RLVK_MEMBER_NAME(elemT, sm);
                            if (!sn || out->memberCount >= RLVK_MAX_SHADER_UNIFORMS) continue;
                            snprintf(out->members[out->memberCount].name, 64, "%s[%u].%s", mn, e, sn);
                            out->members[out->memberCount].offset = mo + e*stride + RLVK_MEMBER_OFF(elemT, sm);
                            out->memberCount++;
                        }
                    u32 arrEnd = mo + count*stride;
                    if (arrEnd > maxOff) maxOff = arrEnd;
                    continue;
                }
                // Plain struct member: flatten to "name.member"
                int ms = (T < bound) ? RLVK_FIND_STRUCT(T) : -1;
                if (ms >= 0 && idName[T] && strcmp(idName[T], "gl_DefaultUniformBlock") != 0)
                {
                    for (u32 sm = 0; sm < structs[ms].count; sm++)
                    {
                        const char *sn = RLVK_MEMBER_NAME(T, sm);
                        if (!sn || out->memberCount >= RLVK_MAX_SHADER_UNIFORMS) continue;
                        snprintf(out->members[out->memberCount].name, 64, "%s.%s", mn, sn);
                        out->members[out->memberCount].offset = mo + RLVK_MEMBER_OFF(T, sm);
                        out->memberCount++;
                    }
                    continue;
                }
                // Scalar/vector/matrix or array thereof
                if (out->memberCount < RLVK_MAX_SHADER_UNIFORMS)
                {
                    strncpy(out->members[out->memberCount].name, mn, 63);
                    out->members[out->memberCount].offset = mo;
                    out->memberCount++;
                }
                if (elemT)   // plain array: extend the block size to cover every element
                {
                    u32 stride = arrStride[T];
                    u32 count  = (arrLenId[T] < bound) ? constVal[arrLenId[T]] : 0;
                    u32 arrEnd = mo + count*stride;
                    if (arrEnd > maxOff) maxOff = arrEnd;
                }
            }
            out->blockSize = maxOff + 64;   // conservative tail padding (largest member is a mat4)
        }
        else if (vars[v].storage == SpvStorageUniformConstant && idBinding[vars[v].id] >= 0)
        {
            if (name && out->samplerCount < RLVK_MAX_TEXTURE_UNITS)
            {
                strncpy(out->samplers[out->samplerCount].name, name, 63);
                out->samplers[out->samplerCount].binding = idBinding[vars[v].id];
                out->samplerCount++;
            }
        }
        else if (vars[v].storage == SpvStorageInput && idLoc[vars[v].id] >= 0)
        {
            if (name && name[0] && strncmp(name, "gl_", 3) != 0 && out->inputCount < 16)
            {
                strncpy(out->inputs[out->inputCount].name, name, 63);
                out->inputs[out->inputCount].location = idLoc[vars[v].id];
                out->inputCount++;
            }
        }
        else if (vars[v].storage == SpvStorageOutput && idLoc[vars[v].id] >= 0)
        {
            if (name && name[0] && strncmp(name, "gl_", 3) != 0 && out->outputCount < 16)
            {
                strncpy(out->outputs[out->outputCount].name, name, 63);
                out->outputs[out->outputCount].location = idLoc[vars[v].id];
                out->outputCount++;
            }
        }
    }
    #undef RLVK_FIND_STRUCT
    #undef RLVK_MEMBER_NAME
    #undef RLVK_MEMBER_OFF

    RL_FREE(idName); RL_FREE(idBinding); RL_FREE(idLoc); RL_FREE(ptrType);
    RL_FREE(arrElem); RL_FREE(arrLenId); RL_FREE(arrStride); RL_FREE(constVal);
}

// GL blend enums -> Vulkan (rlSetBlendFactors* pass GL enums per the rlgl API contract)
static VkBlendFactor rlvkBlendFactorFromGL(int glf)
{
    switch (glf)
    {
        case 0:      return VK_BLEND_FACTOR_ZERO;                       // GL_ZERO
        case 1:      return VK_BLEND_FACTOR_ONE;                        // GL_ONE
        case 0x0300: return VK_BLEND_FACTOR_SRC_COLOR;                  // GL_SRC_COLOR
        case 0x0301: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 0x0302: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 0x0303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x0304: return VK_BLEND_FACTOR_DST_ALPHA;
        case 0x0305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 0x0306: return VK_BLEND_FACTOR_DST_COLOR;
        case 0x0307: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case 0x0308: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case 0x8001: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case 0x8002: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case 0x8003: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case 0x8004: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        default:     return VK_BLEND_FACTOR_ONE;
    }
}
// Translate a GL blend equation to the equivalent Vulkan blend operation
static VkBlendOp rlvkBlendOpFromGL(int gleq)
{
    switch (gleq)
    {
        case 0x8006: return VK_BLEND_OP_ADD;                // GL_FUNC_ADD
        case 0x8007: return VK_BLEND_OP_MIN;                // GL_MIN
        case 0x8008: return VK_BLEND_OP_MAX;                // GL_MAX
        case 0x800A: return VK_BLEND_OP_SUBTRACT;           // GL_FUNC_SUBTRACT
        case 0x800B: return VK_BLEND_OP_REVERSE_SUBTRACT;   // GL_FUNC_REVERSE_SUBTRACT
        default:     return VK_BLEND_OP_ADD;
    }
}

// Canonical raylib attribute LOCATIONS (mirrors rlgl's glBindAttribLocation calls)
static int rlvkCanonicalAttribLocation(const char *name)
{
    if (strcmp(name, "vertexPosition")    == 0) return 0;   // RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION
    if (strcmp(name, "vertexTexCoord")    == 0) return 1;
    if (strcmp(name, "vertexNormal")      == 0) return 2;
    if (strcmp(name, "vertexColor")       == 0) return 3;
    if (strcmp(name, "vertexTangent")     == 0) return 4;
    if (strcmp(name, "vertexTexCoord2")   == 0) return 5;
    if (strcmp(name, "vertexBoneIds")     == 0) return 7;   // RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEINDICES
    if (strcmp(name, "vertexBoneIndices") == 0) return 7;   // alternate spelling used by stock skinning.vs
    if (strcmp(name, "vertexBoneWeights") == 0) return 8;
    if (strcmp(name, "instanceTransform") == 0) return 9;   // mat4: locations 9..12
    return -1;
}

// Rewrite the VS SPIR-V's input Location decorations so named raylib attributes land at their
// canonical locations - the exact equivalent of rlgl's glBindAttribLocation before linking.
// (glslang's auto-map assigns declaration-order locations, which differ per shader.)
static void rlvkCanonicalizeInputLocations(u32 *spv, size_t wordCount)
{
    enum { SpvOpName = 5, SpvOpTypePointer = 32, SpvOpVariable = 59, SpvOpDecorate = 71,
           SpvDecorationLocation = 30, SpvStorageInput = 1 };
    if (wordCount < 5 || spv[0] != 0x07230203) return;
    u32 bound = spv[3];
    const char **idName  = (const char **)RL_CALLOC(bound, sizeof(char *));
    unsigned char *isInput = (unsigned char *)RL_CALLOC(bound, 1);

    size_t i = 5;
    while (i < wordCount)
    {
        u32 w = spv[i]; u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount) break;
        const u32 *a = spv + i + 1;
        if (op == SpvOpName && a[0] < bound) idName[a[0]] = (const char *)&a[1];
        else if (op == SpvOpVariable && a[2] == SpvStorageInput && a[1] < bound) isInput[a[1]] = 1;
        i += len;
    }
    i = 5;
    while (i < wordCount)
    {
        u32 w = spv[i]; u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount) break;
        u32 *a = spv + i + 1;
        if (op == SpvOpDecorate && a[1] == SpvDecorationLocation && a[0] < bound && isInput[a[0]] && idName[a[0]])
        {
            int loc = rlvkCanonicalAttribLocation(idName[a[0]]);
            if (loc >= 0) a[2] = (u32)loc;
        }
        i += len;
    }
    RL_FREE(idName); RL_FREE(isInput);
}

// GL links varyings BY NAME, SPIR-V stages match BY LOCATION: rewrite each FS input's Location
// to the same-named VS output's location. FS inputs with no matching VS output are demoted to
// Private storage (valid-but-undefined in GL; a Vulkan violation of VUID-RuntimeSpirv-
// OpEntryPoint-08743 that also broke real drivers). May grow the SPIR-V; buffer by reference.
static void rlvkMatchStageInterface(u32 **pFsSpv, size_t *pWordCount, const rlvkSpvReflection *vsRef)
{
    enum { SpvOpNop = 0, SpvOpName = 5, SpvOpEntryPoint = 15, SpvOpTypePointer = 32,
           SpvOpVariable = 59, SpvOpDecorate = 71,
           SpvDecorationLocation = 30, SpvStorageInput = 1, SpvStoragePrivate = 6 };
    u32 *fsSpv = *pFsSpv; size_t wordCount = *pWordCount;
    if (wordCount < 5 || fsSpv[0] != 0x07230203) return;
    u32 bound = fsSpv[3];
    const char **idName    = (const char **)RL_CALLOC(bound, sizeof(char *));
    unsigned char *isInput = (unsigned char *)RL_CALLOC(bound, 1);

    size_t i = 5;
    while (i < wordCount)
    {
        u32 w = fsSpv[i]; u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount) break;
        const u32 *a = fsSpv + i + 1;
        if (op == SpvOpName && a[0] < bound) idName[a[0]] = (const char *)&a[1];
        else if (op == SpvOpVariable && a[2] == SpvStorageInput && a[1] < bound) isInput[a[1]] = 1;
        i += len;
    }
    u32 unmatched[16]; size_t unmatchedDecoAt[16]; u32 unmatchedDecoLen[16]; int unmatchedCount = 0;
    i = 5;
    while (i < wordCount)
    {
        u32 w = fsSpv[i]; u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount) break;
        u32 *a = fsSpv + i + 1;
        if (op == SpvOpDecorate && a[1] == SpvDecorationLocation && a[0] < bound && isInput[a[0]] &&
            idName[a[0]] && strncmp(idName[a[0]], "gl_", 3) != 0)
        {
            int matched = -1;
            for (int o = 0; o < vsRef->outputCount; o++)
                if (strcmp(vsRef->outputs[o].name, idName[a[0]]) == 0) { matched = vsRef->outputs[o].location; break; }
            if (matched >= 0) a[2] = (u32)matched;
            else if (unmatchedCount < 16)
            {
                unmatched[unmatchedCount] = a[0];
                unmatchedDecoAt[unmatchedCount] = i;
                unmatchedDecoLen[unmatchedCount] = len;
                unmatchedCount++;
            }
        }
        i += len;
    }
    RL_FREE(idName); RL_FREE(isInput);

    if (unmatchedCount == 0) return;

    // Remove the demoted inputs' Location decorations (OpNop is only legal inside a function
    // body, so the instructions must be compacted out, back-to-front to keep positions valid)
    for (int u = unmatchedCount - 1; u >= 0; u--)
    {
        size_t at = unmatchedDecoAt[u]; u32 len = unmatchedDecoLen[u];
        memmove(fsSpv + at, fsSpv + at + len, (wordCount - at - len)*sizeof(u32));
        wordCount -= len;
    }

    // Grow once: each demotion inserts one 4-word OpTypePointer Private right before its OpVariable
    fsSpv = (u32 *)RL_REALLOC(fsSpv, (wordCount + (size_t)unmatchedCount*4)*sizeof(u32));
    *pFsSpv = fsSpv;

    for (int u = 0; u < unmatchedCount; u++)
    {
        u32 varId = unmatched[u];
        // Locate the variable, its Input pointer type, and that type's pointee
        size_t varAt = 0; u32 ptrType = 0, pointee = 0;
        i = 5;
        while (i < wordCount)
        {
            u32 w = fsSpv[i]; u32 op = w & 0xFFFF, len = w >> 16;
            if (len == 0 || i + len > wordCount) break;
            u32 *a = fsSpv + i + 1;
            if (op == SpvOpVariable && a[1] == varId) { varAt = i; ptrType = a[0]; }
            else if (op == SpvOpTypePointer && ptrType && a[0] == ptrType) pointee = a[2];
            i += len;
        }
        if (!varAt) continue;
        if (!pointee)   // pointer type declared before the variable: rescan for it
        {
            i = 5;
            while (i < wordCount)
            {
                u32 w = fsSpv[i]; u32 op = w & 0xFFFF, len = w >> 16;
                if (len == 0 || i + len > wordCount) break;
                u32 *a = fsSpv + i + 1;
                if (op == SpvOpTypePointer && a[0] == ptrType) { pointee = a[2]; break; }
                i += len;
            }
        }
        if (!pointee) continue;

        u32 newPtr = fsSpv[3]++;   // fresh id (bump the module bound)
        memmove(fsSpv + varAt + 4, fsSpv + varAt, (wordCount - varAt)*sizeof(u32));
        fsSpv[varAt + 0] = (4u << 16) | SpvOpTypePointer;
        fsSpv[varAt + 1] = newPtr;
        fsSpv[varAt + 2] = SpvStoragePrivate;
        fsSpv[varAt + 3] = pointee;
        wordCount += 4;
        u32 *va = fsSpv + varAt + 4 + 1;
        va[0] = newPtr;                 // result type -> Private pointer
        va[2] = SpvStoragePrivate;      // storage class -> Private

        // Pre-1.4 SPIR-V lists only Input/Output in the entry-point interface: remove the id.
        // 1.4+ lists ALL globals, so a Private variable stays listed.
        if (fsSpv[1] < 0x00010400)
        {
            i = 5;
            while (i < wordCount)
            {
                u32 w = fsSpv[i]; u32 op = w & 0xFFFF, len = w >> 16;
                if (len == 0 || i + len > wordCount) break;
                if (op == SpvOpEntryPoint)
                {
                    for (u32 n = 1; n < len; n++)
                        if (fsSpv[i + n] == varId)
                        {
                            memmove(fsSpv + i + n, fsSpv + i + n + 1, (wordCount - i - n - 1)*sizeof(u32));
                            fsSpv[i] = ((len - 1) << 16) | SpvOpEntryPoint;
                            wordCount -= 1;
                            break;
                        }
                    break;
                }
                i += len;
            }
        }
    }
    *pWordCount = wordCount;
}

// Get the canonical attribute table index for a vertex attribute name
static int rlvkCanonicalAttribIndex(const char *name)
{
    if (strcmp(name, "vertexPosition")  == 0) return RLVK_ATTRIB_POSITION;
    if (strcmp(name, "vertexTexCoord")  == 0) return RLVK_ATTRIB_TEXCOORD;
    if (strcmp(name, "vertexNormal")    == 0) return RLVK_ATTRIB_NORMAL;
    if (strcmp(name, "vertexColor")     == 0) return RLVK_ATTRIB_COLOR;
    if (strcmp(name, "vertexTangent")   == 0) return RLVK_ATTRIB_TANGENT;
    if (strcmp(name, "vertexTexCoord2") == 0) return RLVK_ATTRIB_TEXCOORD2;
    if (strcmp(name, "instanceTransform") == 0) return RLVK_ATTRIB_INSTANCE_TX;
    if (strcmp(name, "vertexBoneIds")     == 0) return RLVK_ATTRIB_BONEIDS;
    if (strcmp(name, "vertexBoneIndices") == 0) return RLVK_ATTRIB_BONEIDS;
    if (strcmp(name, "vertexBoneWeights") == 0) return RLVK_ATTRIB_BONEWEIGHTS;
    return -1;
}

static void rlvkShaderWriteMatrixUniform(rlvkShaderSlot *shader, int loc, Matrix mat);

// Write into the CPU staging of whichever stage blocks contain this uniform
static void rlvkShaderWriteUniform(rlvkShaderSlot *shader, int loc, const void *data, u32 bytes)
{
    if (!shader->usesUbo || loc < 0 || loc >= shader->uniformCount || !data) return;
    rlvkUniform *u = &shader->uniforms[loc];
    if (u->vsOffset >= 0 && shader->vsStage && (u32)u->vsOffset + bytes <= shader->vsBlockSize) { memcpy(shader->vsStage + u->vsOffset, data, bytes); shader->vsWriteGen++; }
    if (u->fsOffset >= 0 && shader->fsStage && (u32)u->fsOffset + bytes <= shader->fsBlockSize) { memcpy(shader->fsStage + u->fsOffset, data, bytes); shader->fsWriteGen++; }
}

// Write a Matrix uniform in rlMatrixToFloat (column-major/std140) order
static void rlvkShaderWriteMatrixUniform(rlvkShaderSlot *shader, int loc, Matrix mat)
{
    f32 f[16] = {
        mat.m0, mat.m1, mat.m2, mat.m3, mat.m4, mat.m5, mat.m6, mat.m7,
        mat.m8, mat.m9, mat.m10, mat.m11, mat.m12, mat.m13, mat.m14, mat.m15 };
    rlvkShaderWriteUniform(shader, loc, f, sizeof(f));
}

// Snapshot the shader's uniform staging into the per-frame arena (glUniform semantics: each draw
// sees the values current at record time) and push the UBO descriptors.
static void rlvkBindShaderUbos(VkCommandBuffer cmdBuffer, rlvkShaderSlot *shader)
{
    if (!shader->usesUbo) return;

    // Snapshot and push a stage's block ONLY when its uniforms changed since the last push in
    // this command buffer (pushes persist until overwritten); both stages ride one push call
    bool cbFresh = (shader->uboPushedEpoch != RLVK.State.cbEpoch) || (RLVK.lastUboShader != shader);
    bool wantVs = shader->vsBlockSize && shader->vsStage && (cbFresh || (shader->vsPushedGen != shader->vsWriteGen));
    bool wantFs = shader->fsBlockSize && shader->fsStage && (cbFresh || (shader->fsPushedGen != shader->fsWriteGen));
    if (!wantVs && !wantFs) return;

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
    VkDescriptorBufferInfo bufferInfos[2];
    VkWriteDescriptorSet writes[2];
    u32 writeCount = 0;
    for (int stage = 0; stage < 2; stage++)
    {
        if (stage ? !wantFs : !wantVs) continue;
        u32 size = stage ? shader->fsBlockSize : shader->vsBlockSize;
        unsigned char *src = stage ? shader->fsStage : shader->vsStage;
        VkDeviceSize off = (RLVK.arenaOffset[frameIndex] + 255) & ~(VkDeviceSize)255;   // minUniformBufferOffsetAlignment
        if (off + size > arena->sizeBytes)
        {
            // Cannot drain here (this draw's binds would be lost): request growth, skip the push
            RLVK.arenaWanted[frameIndex] += size + 256;   // demand grows even when the push is skipped
            return;
        }
        memcpy((char *)arena->mapped + off, src, size);
        RLVK.arenaOffset[frameIndex] = off + size;
        RLVK.arenaWanted[frameIndex] += size + 256;
        bufferInfos[writeCount] = (VkDescriptorBufferInfo){ arena->buffer, off, size };
        writes[writeCount] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding      = stage ? (u32)RLVK_UBO_BINDING_FS : (u32)RLVK_UBO_BINDING_VS,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &bufferInfos[writeCount],
        };
        writeCount++;
        if (stage) shader->fsPushedGen = shader->fsWriteGen;
        else       shader->vsPushedGen = shader->vsWriteGen;
    }
    vk.CmdPushDescriptorSetKHR(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, RLVK.pipelineLayout, 0, writeCount, writes);
    shader->uboPushedEpoch = RLVK.State.cbEpoch;
    RLVK.lastUboShader = shader;
}

// Push the shader's sampler bindings: rlSetUniformSampler's explicit texture wins, else the GL
// texture unit's. Batch flush pushes binding 0 itself (includeBinding0=false); mesh path resolves
// binding 0 here (a samplerCube at binding 0 gets the cubemap unit, not the diffuse map)
static void rlvkBindShaderSamplers(VkCommandBuffer cmdBuffer, rlvkShaderSlot *shader, bool includeBinding0)
{
    if (!shader->usesUbo) return;
    for (int i = 0; i < shader->uniformCount; i++)
    {
        int b = shader->uniforms[i].samplerBinding;
        if (b < 0 || b >= RLVK_MAX_TEXTURE_UNITS || (b == 0 && !includeBinding0)) continue;
        u32 tex = shader->bindingTexture[b];
        if (tex == 0)
        {
            int unit = shader->bindingUnit[b];
            if (unit >= 0 && unit < RLVK_MAX_TEXTURE_UNITS) tex = RLVK.State.activeTextureSlots[unit];
            if (tex == 0 && b == 0) tex = RLVK.State.currentTextureSlot;   // mesh diffuse fallback
        }
        if (tex == 0 || tex >= RLVK_MAX_TEXTURE_SLOTS || !RLVK.textureSlots[tex].view) tex = RLVK.defaultTextureSlot;
        if (rlvkDebugFlag("RLVK_DEBUG_SAMPLERS", &s_dbgSamplers)) TRACELOG(RL_LOG_WARNING, "VKDBG sampler %s b=%d bindTex=%u unit=%d unitTex=%u -> %u",
            shader->uniforms[i].name, b, shader->bindingTexture[b], shader->bindingUnit[b],
            (shader->bindingUnit[b] >= 0 && shader->bindingUnit[b] < RL_DEFAULT_BATCH_MAX_TEXTURE_UNITS) ? RLVK.State.activeTextureSlots[shader->bindingUnit[b]] : 9999, tex);
        rlvkPushTexture(cmdBuffer, (u32)b, tex);
    }
}


//----------------------------------------------------------------------------------
// Module Functions Definition - Matrix operations
//----------------------------------------------------------------------------------

// Choose the current matrix to be transformed
void rlMatrixMode(int mode)
{
    switch (mode)
    {
        case RL_PROJECTION: RLVK.State.currentMatrix = &RLVK.State.projection; break;
        case RL_MODELVIEW:  RLVK.State.currentMatrix = &RLVK.State.modelview;  break;
        // RL_TEXTURE intentionally unimplemented (no rlgl consumer; the texture matrix stays identity)
        default: break;
    }
    RLVK.State.currentMatrixMode = mode;
}

// Push the current matrix into RLVK.State.stack
void rlPushMatrix(void)
{
    if (RLVK.State.stackCounter >= RL_MAX_MATRIX_STACK_SIZE)
    {
        TRACELOG(RL_LOG_ERROR, "RLVK: Matrix stack overflow (RL_MAX_MATRIX_STACK_SIZE)");
        return;
    }

    // Only the MODELVIEW stack redirects to the software 'transform' matrix; PROJECTION pushes
    // must keep operating on the projection matrix (so BeginMode3D's rlFrustum lands there).
    if (RLVK.State.currentMatrixMode == RL_MODELVIEW)
    {
        RLVK.State.transformRequired = true;
        RLVK.State.currentMatrix = &RLVK.State.transform;
    }

    RLVK.State.stack[RLVK.State.stackCounter] = *RLVK.State.currentMatrix;
    RLVK.State.stackCounter++;
}

// Pop latest inserted matrix from RLVK.State.stack
void rlPopMatrix(void)
{
    if (RLVK.State.stackCounter > 0)
    {
        Matrix mat = RLVK.State.stack[RLVK.State.stackCounter - 1];
        *RLVK.State.currentMatrix = mat;
        RLVK.State.stackCounter--;
    }

    if ((RLVK.State.stackCounter == 0) && (RLVK.State.currentMatrixMode == RL_MODELVIEW))
    {
        RLVK.State.currentMatrix = &RLVK.State.modelview;
        RLVK.State.transformRequired = false;
    }
}

// Reset current matrix to identity matrix
void rlLoadIdentity(void)
{
    *RLVK.State.currentMatrix = rlvkMatrixIdentity();
}

// Multiply the current matrix by a translation matrix
void rlTranslatef(f32 x, f32 y, f32 z)
{
    Matrix t = { 1,0,0,x,  0,1,0,y,  0,0,1,z,  0,0,0,1 };
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(t, *RLVK.State.currentMatrix);
}

// Multiply the current matrix by a rotation matrix
// NOTE: The provided angle must be in degrees
void rlRotatef(f32 angleDeg, f32 x, f32 y, f32 z)
{
    f32 a = angleDeg*DEG2RAD;
    f32 c = cosf(a), s = sinf(a);
    f32 len = sqrtf(x*x + y*y + z*z);
    if (len > 1e-6f) { f32 il = 1.0f/len; x*=il; y*=il; z*=il; }
    f32 t = 1.0f - c;
    Matrix r = {
        c + x*x*t,    x*y*t - z*s,  x*z*t + y*s,  0,
        y*x*t + z*s,  c + y*y*t,    y*z*t - x*s,  0,
        z*x*t - y*s,  z*y*t + x*s,  c + z*z*t,    0,
        0, 0, 0, 1
    };
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(r, *RLVK.State.currentMatrix);
}

// Multiply the current matrix by a scaling matrix
void rlScalef(f32 x, f32 y, f32 z)
{
    Matrix s = { x,0,0,0,  0,y,0,0,  0,0,z,0,  0,0,0,1 };
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(s, *RLVK.State.currentMatrix);
}

// Multiply the current matrix by another matrix
void rlMultMatrixf(const f32 *matf)
{
    Matrix m = {
        matf[0], matf[4], matf[ 8], matf[12],
        matf[1], matf[5], matf[ 9], matf[13],
        matf[2], matf[6], matf[10], matf[14],
        matf[3], matf[7], matf[11], matf[15]
    };
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(m, *RLVK.State.currentMatrix);
}

// Multiply the current matrix by a perspective matrix generated by parameters
void rlFrustum(f64 left, f64 right, f64 bottom, f64 top, f64 znear, f64 zfar)
{
    f32 rl = (f32)(right - left), tb = (f32)(top - bottom), fn = (f32)(zfar - znear);
    Matrix m = {
        (f32)(2*znear)/rl, 0, (f32)(right + left)/rl, 0,
        0, (f32)(2*znear)/tb, (f32)(top + bottom)/tb, 0,
        0, 0, -(f32)(zfar + znear)/fn, -(f32)(2*zfar*znear)/fn,
        0, 0, -1, 0
    };
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(m, *RLVK.State.currentMatrix);
}

// Multiply the current matrix by an orthographic matrix generated by parameters
void rlOrtho(f64 left, f64 right, f64 bottom, f64 top, f64 znear, f64 zfar)
{
    f32 rl = (f32)(right - left), tb = (f32)(top - bottom), fn = (f32)(zfar - znear);
    Matrix m = {
        2.0f/rl, 0, 0, -(f32)(right + left)/rl,
        0, 2.0f/tb, 0, -(f32)(top + bottom)/tb,
        0, 0, -2.0f/fn, -(f32)(zfar + znear)/fn,
        0, 0, 0, 1
    };
    *RLVK.State.currentMatrix = rlvkMatrixMultiply(m, *RLVK.State.currentMatrix);
}

// Set the viewport area (transformation from normalized device coordinates to window coordinates)
void rlViewport(int x, int y, int width, int height)
{
    RLVK.State.viewportX = x; RLVK.State.viewportY = y; RLVK.State.stateGeneration++;
    RLVK.State.viewportW = width; RLVK.State.viewportH = height;
    // vkCmdSetViewport happens at draw flush; Y is flipped via negative-height
    // viewport so GL-style up-Y conventions hold.
}

// Set clip planes distances
void rlSetClipPlanes(f64 n, f64 f) { rlCullDistanceNear = n; rlCullDistanceFar = f; }
// Get cull plane distance near
f64 rlGetCullDistanceNear(void)        { return rlCullDistanceNear; }
// Get cull plane distance far
f64 rlGetCullDistanceFar(void)         { return rlCullDistanceFar; }

//----------------------------------------------------------------------------------
// Module Functions Definition - Vertex level operations
//----------------------------------------------------------------------------------

// Initialize drawing mode (how to organize vertex)
void rlBegin(int mode)
{
    rlRenderBatch *batch = RLVK.currentBatch;
    if (!batch) return;
    rlDrawCall *cur = &batch->draws[batch->drawCounter - 1];

    if (cur->mode != mode)
    {
        if (cur->vertexCount > 0)
        {
            if (cur->mode == RL_LINES)
                cur->vertexAlignment = (cur->vertexCount < 4) ? cur->vertexCount : (cur->vertexCount % 4);
            else if (cur->mode == RL_TRIANGLES)
                cur->vertexAlignment = (cur->vertexCount < 4) ? 1 : (4 - (cur->vertexCount % 4));
            else
                cur->vertexAlignment = 0;

            if (!rlCheckRenderBatchLimit(cur->vertexAlignment))
            {
                RLVK.State.vertexCounter += cur->vertexAlignment;
                batch->drawCounter++;
            }
        }
        if (batch->drawCounter >= RL_DEFAULT_BATCH_DRAWCALLS) rlDrawRenderBatch(batch);

        cur = &batch->draws[batch->drawCounter - 1];
        cur->mode = mode;
        cur->textureId = RLVK.State.currentTextureSlot;
        RLVK.State.currentTextureSlot = RLVK.defaultTextureSlot;
    }
}

// Finish vertex providing
void rlEnd(void)
{
    if (RLVK.currentBatch) RLVK.currentBatch->currentDepth += (1.0f/20000.0f);
}

// Hot path: writes go directly into the persistently-mapped GPU buffer behind rlVertexBuffer.
// The vertex shader will pull these via BDA pointers at draw time - no intermediate copy.
void rlVertex3f(f32 x, f32 y, f32 z)
{
    rlRenderBatch *batch = RLVK.currentBatch;
    if (!batch) return;
    rlVertexBuffer *vb = &batch->vertexBuffer[batch->currentBuffer];
    rlDrawCall *cur = &batch->draws[batch->drawCounter - 1];

    f32 tx = x, ty = y, tz = z;
    if (RLVK.State.transformRequired)
    {
        // OPTIMIZATION TODO: lift to vertex shader once per-vertex draw-range index lands.
        tx = RLVK.State.transform.m0*x + RLVK.State.transform.m4*y + RLVK.State.transform.m8 *z + RLVK.State.transform.m12;
        ty = RLVK.State.transform.m1*x + RLVK.State.transform.m5*y + RLVK.State.transform.m9 *z + RLVK.State.transform.m13;
        tz = RLVK.State.transform.m2*x + RLVK.State.transform.m6*y + RLVK.State.transform.m10*z + RLVK.State.transform.m14;
    }

    if (RLVK.State.vertexCounter > (vb->elementCount*4 - 4))
    {
        if      (cur->mode == RL_LINES     && cur->vertexCount % 2 == 0) rlCheckRenderBatchLimit(2 + 1);
        else if (cur->mode == RL_TRIANGLES && cur->vertexCount % 3 == 0) rlCheckRenderBatchLimit(3 + 1);
        else if (cur->mode == RL_QUADS     && cur->vertexCount % 4 == 0) rlCheckRenderBatchLimit(4 + 1);

        // The flush reset drawCounter/currentBuffer - refresh the cached pointers (rlgl re-reads
        // draws[drawCounter-1] on every access; a stale cur here incremented the DEAD draw entry,
        // leaving the live draw one vertex short and shearing every triangle after the flush)
        vb  = &batch->vertexBuffer[batch->currentBuffer];
        cur = &batch->draws[batch->drawCounter - 1];
    }

    int i = RLVK.State.vertexCounter;
    vb->vertices [3*i + 0] = tx;
    vb->vertices [3*i + 1] = ty;
    vb->vertices [3*i + 2] = tz;
    vb->texcoords[2*i + 0] = RLVK.State.texcoordx;
    vb->texcoords[2*i + 1] = RLVK.State.texcoordy;
    vb->normals  [3*i + 0] = RLVK.State.normalx;
    vb->normals  [3*i + 1] = RLVK.State.normaly;
    vb->normals  [3*i + 2] = RLVK.State.normalz;
    vb->colors   [4*i + 0] = RLVK.State.colorr;
    vb->colors   [4*i + 1] = RLVK.State.colorg;
    vb->colors   [4*i + 2] = RLVK.State.colorb;
    vb->colors   [4*i + 3] = RLVK.State.colora;

    RLVK.State.vertexCounter++;
    cur->vertexCount++;
}

void rlVertex2f(f32 x, f32 y) { rlVertex3f(x, y, RLVK.currentBatch ? RLVK.currentBatch->currentDepth : 0.0f); }
void rlVertex2i(int x, int y)     { rlVertex3f((f32)x, (f32)y, RLVK.currentBatch ? RLVK.currentBatch->currentDepth : 0.0f); }

void rlTexCoord2f(f32 x, f32 y) { RLVK.State.texcoordx = x; RLVK.State.texcoordy = y; }
void rlNormal3f(f32 x, f32 y, f32 z)
{
    if (RLVK.State.transformRequired)
    {
        RLVK.State.normalx = RLVK.State.transform.m0*x + RLVK.State.transform.m4*y + RLVK.State.transform.m8 *z;
        RLVK.State.normaly = RLVK.State.transform.m1*x + RLVK.State.transform.m5*y + RLVK.State.transform.m9 *z;
        RLVK.State.normalz = RLVK.State.transform.m2*x + RLVK.State.transform.m6*y + RLVK.State.transform.m10*z;
    }
    else { RLVK.State.normalx = x; RLVK.State.normaly = y; RLVK.State.normalz = z; }
}
void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{ RLVK.State.colorr = r; RLVK.State.colorg = g; RLVK.State.colorb = b; RLVK.State.colora = a; }
void rlColor3f(f32 x, f32 y, f32 z)            { rlColor4ub((unsigned char)(x*255), (unsigned char)(y*255), (unsigned char)(z*255), 255); }
void rlColor4f(f32 r, f32 g, f32 b, f32 a)   { rlColor4ub((unsigned char)(r*255), (unsigned char)(g*255), (unsigned char)(b*255), (unsigned char)(a*255)); }

//----------------------------------------------------------------------------------
// Module Functions Definition - OpenGL style functions
//----------------------------------------------------------------------------------

// Enable vertex array object (VAO)
bool rlEnableVertexArray(unsigned int v)
{
    if (v == 0 || v >= RLVK_MAX_VAO_SLOTS || !RLVK.vertexArrays[v].inUse) return false;
    RLVK.State.currentVAO = v;   // true => DrawMesh reuses the VAO's recorded attributes
    return true;
}
// Disable vertex array object (VAO)
void rlDisableVertexArray(void)                  { RLVK.State.currentVAO = 0; }
// Enable vertex buffer (VBO)
void rlEnableVertexBuffer(unsigned int id)       { RLVK.State.currentVBO = id; }
// Disable vertex buffer (VBO)
void rlDisableVertexBuffer(void)                 {}
// Enable vertex buffer element (VBO element)
void rlEnableVertexBufferElement(unsigned int id)
{
    if (RLVK.State.currentVAO && RLVK.State.currentVAO < RLVK_MAX_VAO_SLOTS && id < RLVK_MAX_BUFFER_SLOTS)
        RLVK.vertexArrays[RLVK.State.currentVAO].indexSlot = id;
}
// Disable vertex buffer element (VBO element)
void rlDisableVertexBufferElement(void)          {}
// Enable vertex attribute index
void rlEnableVertexAttribute(unsigned int idx)   { (void)idx; }
// Disable vertex attribute index
void rlDisableVertexAttribute(unsigned int idx)  { (void)idx; }
// Enable vertex state pointer
void rlEnableStatePointer(int t, void *b)        { (void)t; (void)b; }
// Disable vertex state pointer
void rlDisableStatePointer(int t)                { (void)t; }

// Select and active a texture slot
void rlActiveTextureSlot(int slot)               { if (slot >= 0 && slot < RLVK_MAX_TEXTURE_UNITS) RLVK.State.activeTextureUnit = slot; }

// rlgl semantics: rlEnableTexture = glBindTexture on the ACTIVE UNIT only. It must NOT select
// the batch draw's texture - only rlSetTexture does that (deferred example binds gbuffer maps
// via units right before batching light spheres; aliasing unit 0 into the batch hijacked them).
void rlEnableTexture(unsigned int id)
{
    u32 tex = (id == 0) ? RLVK.defaultTextureSlot : id;
    RLVK.State.activeTextureSlots[RLVK.State.activeTextureUnit] = tex;
}
// Disable texture
void rlDisableTexture(void)                      { RLVK.State.currentTextureSlot = RLVK.defaultTextureSlot; }
// Enable texture cubemap
void rlEnableTextureCubemap(unsigned int id)
{
    u32 tex = (id == 0) ? RLVK.defaultTextureSlot : id;
    RLVK.State.activeTextureSlots[RLVK.State.activeTextureUnit] = tex;
}
// Disable texture cubemap
void rlDisableTextureCubemap(void)               { RLVK.State.currentTextureSlot = RLVK.defaultTextureSlot; }

// Set texture parameters (wrap mode/filter mode)
void rlTextureParameters(unsigned int id, int param, int value)
{
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS) return;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->image) return;

    switch (param)
    {
        case RL_TEXTURE_MAG_FILTER:
            t->magFilter = (value == RL_TEXTURE_FILTER_NEAREST) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            break;
        case RL_TEXTURE_MIN_FILTER:
            t->minFilter = ((value == RL_TEXTURE_FILTER_NEAREST) || (value == RL_TEXTURE_FILTER_MIP_NEAREST) ||
                            (value == RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR)) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            t->mipMode   = ((value == RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR) || (value == RL_TEXTURE_FILTER_MIP_LINEAR)) ?
                            VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case RL_TEXTURE_WRAP_S:
        case RL_TEXTURE_WRAP_T:
        {
            VkSamplerAddressMode m = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            if (value == RL_TEXTURE_WRAP_CLAMP)         m = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            if (value == RL_TEXTURE_WRAP_MIRROR_REPEAT) m = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            if (value == RL_TEXTURE_WRAP_MIRROR_CLAMP)  m = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
            if (param == RL_TEXTURE_WRAP_S) t->wrapS = m; else t->wrapT = m;
            break;
        }
        default: return;   // anisotropy / mipmap bias unsupported for now
    }

    // Recreate the sampler; it is pushed fresh each draw (push descriptors), so nothing else
    // to update. The old sampler may still be referenced by in-flight or still-recording
    // command buffers: destruction is deferred until this frame slot's fence
    rlvkDeferDestroy(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, t->sampler, VK_NULL_HANDLE, VK_NULL_HANDLE);
    RLVK_CHECK(vkCreateSampler(RLVK.device,
        &(VkSamplerCreateInfo){
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = t->magFilter,
            .minFilter    = t->minFilter,
            .mipmapMode   = t->mipMode,
            .addressModeU = t->wrapS,
            .addressModeV = t->wrapT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .maxLod       = (t->mipCount > 1) ? (f32)t->mipCount : 1.0f,
        }, RLVK_ALLOC, &t->sampler));
}
// Set cubemap parameters (wrap mode/filter mode)
void rlCubemapParameters(unsigned int id, int param, int value) { rlTextureParameters(id, param, value); }

// rlgl semantics: rlEnableShader is glUseProgram - it selects the target for rlSetUniform* and
// the shader mesh/quad draws use. The BATCH's shader (rlgl State.currentShaderId) changes only
// through rlSetShader; a stray SetShaderValue before drawing must NOT hijack the batch.
void rlEnableShader(unsigned int id)
{
    RLVK.State.activeShaderSlot = (id == 0 || id >= RLVK_MAX_SHADER_SLOTS) ? RLVK.defaultShaderSlot : id;
}
// Disable shader program
void rlDisableShader(void) { RLVK.State.activeShaderSlot = RLVK.defaultShaderSlot; }

//----------------------------------------------------------------------------------
// Render-pass + framebuffer cache implementation (Vulkan 1.1 baseline)
//----------------------------------------------------------------------------------

// Get (or build and cache) the render pass for a scope-shape key. Attachment layouts are
// initial == final (rlvk transitions layouts with explicit barriers outside the pass), so a
// pass never changes an image's layout behind the manual tracking's back.
static VkRenderPass rlvkGetRenderPass(const rlvkRenderPassKey *key)
{
    for (int i = 0; i < RLVK.renderPassCount; i++)
        if (memcmp(&RLVK.renderPasses[i].key, key, sizeof(rlvkRenderPassKey)) == 0) return RLVK.renderPasses[i].pass;

    if (RLVK.renderPassCount >= RLVK_MAX_RENDER_PASSES)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: render-pass cache full (%d), raise RLVK_MAX_RENDER_PASSES", RLVK_MAX_RENDER_PASSES);
        return VK_NULL_HANDLE;
    }

    VkSampleCountFlagBits samples = (key->samples > 1)? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
    bool hasDepth = (key->depthFormat != VK_FORMAT_UNDEFINED);
    u32 attCount = 0;
    VkAttachmentDescription atts[RLVK_MAX_SCOPE_ATTACHMENTS];
    VkAttachmentReference   colorRefs[8], resolveRef, depthRef;

    for (u32 c = 0; c < key->colorCount; c++)
    {
        atts[attCount] = (VkAttachmentDescription){
            .format         = key->colorFormats[c],
            .samples        = samples,
            .loadOp         = (VkAttachmentLoadOp)key->colorLoad,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        colorRefs[c] = (VkAttachmentReference){ attCount, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        attCount++;
    }
    if (key->hasResolve)   // MSAA fixed-function resolve into a 1x attachment (single color scope only)
    {
        atts[attCount] = (VkAttachmentDescription){
            .format         = key->colorFormats[0],
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        resolveRef = (VkAttachmentReference){ attCount, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        attCount++;
    }
    if (hasDepth)
    {
        atts[attCount] = (VkAttachmentDescription){
            .format         = key->depthFormat,
            .samples        = samples,
            .loadOp         = (VkAttachmentLoadOp)key->depthLoad,
            .storeOp        = (VkAttachmentStoreOp)key->depthStore,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        depthRef = (VkAttachmentReference){ attCount, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        attCount++;
    }

    VkRenderPass pass = VK_NULL_HANDLE;
    VkResult res = vkCreateRenderPass(RLVK.device,
        &(VkRenderPassCreateInfo){
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = attCount,
            .pAttachments    = atts,
            .subpassCount    = 1,
            .pSubpasses      = &(VkSubpassDescription){
                .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount    = key->colorCount,
                .pColorAttachments       = key->colorCount? colorRefs : NULL,
                .pResolveAttachments     = key->hasResolve? &resolveRef : NULL,
                .pDepthStencilAttachment = hasDepth? &depthRef : NULL,
            },
            // No subpass dependencies: rlvk orders everything with explicit barriers outside
            // the pass, exactly like the dynamic-rendering path did
        }, RLVK_ALLOC, &pass);
    if (res != VK_SUCCESS) { TRACELOG(RL_LOG_WARNING, "RLVK: vkCreateRenderPass => %d", (int)res); return VK_NULL_HANDLE; }

    RLVK.renderPasses[RLVK.renderPassCount++] = (rlvkRenderPassEntry){ pass, *key };
    return pass;
}

// Get (or build and cache) the framebuffer for a pass + attachment view set
static VkFramebuffer rlvkGetFramebuffer(VkRenderPass pass, const VkImageView *views, u32 viewCount, u32 width, u32 height)
{
    for (int i = 0; i < RLVK.framebufferCount; i++)
    {
        rlvkFramebufferEntry *e = &RLVK.framebuffers[i];
        if ((e->pass == pass) && (e->viewCount == viewCount) && (e->width == width) && (e->height == height)
            && (memcmp(e->views, views, viewCount*sizeof(VkImageView)) == 0)) return e->framebuffer;
    }

    if (RLVK.framebufferCount >= RLVK_MAX_CACHED_FRAMEBUFFERS)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: framebuffer cache full (%d), raise RLVK_MAX_CACHED_FRAMEBUFFERS", RLVK_MAX_CACHED_FRAMEBUFFERS);
        return VK_NULL_HANDLE;
    }

    VkFramebuffer fb = VK_NULL_HANDLE;
    VkResult res = vkCreateFramebuffer(RLVK.device,
        &(VkFramebufferCreateInfo){
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = pass,
            .attachmentCount = viewCount,
            .pAttachments    = views,
            .width           = width,
            .height          = height,
            .layers          = 1,
        }, RLVK_ALLOC, &fb);
    if (res != VK_SUCCESS) { TRACELOG(RL_LOG_WARNING, "RLVK: vkCreateFramebuffer => %d", (int)res); return VK_NULL_HANDLE; }

    rlvkFramebufferEntry *e = &RLVK.framebuffers[RLVK.framebufferCount++];
    memset(e, 0, sizeof(*e));
    e->framebuffer = fb; e->pass = pass; e->viewCount = viewCount; e->width = width; e->height = height;
    memcpy(e->views, views, viewCount*sizeof(VkImageView));
    return fb;
}

static void rlvkDeferDestroy(VkBuffer buffer, VkImage image, VkImageView view, VkSampler sampler, VkDeviceMemory memory, VkPipeline pipeline);
static void rlvkDeferDestroyFramebufferOnly(VkFramebuffer framebuffer);

// Evict every cached framebuffer that references a dying view. MUST run before the view is
// destroyed or queued for deferred destruction; the framebuffers ride the same fence-gated
// dead-resource ring so in-flight frames finish with them intact.
static void rlvkEvictFramebuffersForView(VkImageView view)
{
    if (view == VK_NULL_HANDLE) return;
    for (int i = 0; i < RLVK.framebufferCount; )
    {
        rlvkFramebufferEntry *e = &RLVK.framebuffers[i];
        bool hit = false;
        for (u32 v = 0; v < e->viewCount; v++) if (e->views[v] == view) { hit = true; break; }
        if (!hit) { i++; continue; }
        rlvkDeferDestroyFramebufferOnly(e->framebuffer);
        RLVK.framebuffers[i] = RLVK.framebuffers[--RLVK.framebufferCount];   // swap-remove
    }
}

// Open a rendering scope: cached render pass + cached framebuffer + vkCmdBeginRenderPass.
// clearColor/clearDepth are consumed only by CLEAR load ops of the key.
static void rlvkBeginScopeRenderPass(VkCommandBuffer cmdBuffer, const rlvkRenderPassKey *key,
    const VkImageView *views, u32 viewCount, u32 width, u32 height,
    const VkClearValue *clearColor, const VkClearValue *clearDepth)
{
    VkRenderPass pass = rlvkGetRenderPass(key);
    if (pass == VK_NULL_HANDLE) return;
    VkFramebuffer fb = rlvkGetFramebuffer(pass, views, viewCount, width, height);
    if (fb == VK_NULL_HANDLE) return;

    // pClearValues is indexed by attachment: colors [0..colorCount), resolve, depth last
    VkClearValue clears[RLVK_MAX_SCOPE_ATTACHMENTS];
    memset(clears, 0, sizeof(clears));
    if (clearColor) for (u32 c = 0; c < key->colorCount; c++) clears[c] = *clearColor;
    if (clearDepth) clears[key->colorCount + (key->hasResolve? 1u : 0u)] = *clearDepth;

    vkCmdBeginRenderPass(cmdBuffer,
        &(VkRenderPassBeginInfo){
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = pass,
            .framebuffer     = fb,
            .renderArea      = { { 0, 0 }, { width, height } },
            .clearValueCount = viewCount,       // >= highest CLEAR attachment index + 1
            .pClearValues    = clears,
        }, VK_SUBPASS_CONTENTS_INLINE);
}

// Switch the rendering scope to a user framebuffer (render texture). The pending batch
// was already flushed by raylib's BeginTextureMode/EndTextureMode before this is called.
void rlEnableFramebuffer(unsigned int id)
{
    RLVK.State.stateGeneration++;
    RLVK.State.currentFramebufferSlot = id;
    if (!isGpuReady || id == 0 || id >= RLVK_MAX_FRAMEBUFFER_SLOTS) return;
    if (RLVK.scope.fbSlot == id && RLVK.frameActive) return;   // this framebuffer's scope is already open

    rlvkBeginFrame();                     // ensure a frame is active (opens the swapchain scope)
    if (!RLVK.frameActive) return;
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
    rlvkFramebufferSlot *f = &RLVK.fbSlots[id];
    u32 colorCount = 0;
    rlvkTextureSlot *colors[8] = { 0 };
    for (u32 c = 0; c < f->colorCount && c < 8; c++)
    {
        rlvkTextureSlot *t = &RLVK.textureSlots[f->colorTextures[c]];
        if (t->image) colors[colorCount++] = t;
    }
    rlvkTextureSlot *color = colorCount ? colors[0] : NULL;
    rlvkTextureSlot *fbDepth = f->hasDepth ? &RLVK.textureSlots[f->depthTexture] : NULL;
    if (!color && !(fbDepth && fbDepth->image)) return;   // nothing attached yet

    vkCmdEndRenderPass(cmdBuffer);

    for (u32 c = 0; c < colorCount; c++)
    {
        // Color texture: SHADER_READ_ONLY (its resting state) -> COLOR_ATTACHMENT
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask     = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .srcAccessMask    = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask    = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .image            = colors[c]->image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            },
        });
        colors[c]->currentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    rlvkTextureSlot *depth = fbDepth;
    if (depth && depth->image && depth->currentLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        // The depth texture may have been SAMPLED since the last bind (depth shaders)
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask     = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                                  | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .srcAccessMask    = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .dstAccessMask    = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout        = (depth->currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ?
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .image            = depth->image,
                .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
            },
        });
        depth->currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    // GL semantics: framebuffer content persists across binds -> loadOp LOAD (ClearBackground
    // inside the texture mode clears explicitly). Depth-only framebuffers (shadowmaps) have no
    // color attachment at all; depth must be STORED so it can be sampled afterwards.
    int fbW = color ? color->width : depth->width;
    int fbH = color ? color->height : depth->height;
    bool fbHasDepth = (depth && depth->image);
    VkImageView scopeViews[RLVK_MAX_SCOPE_ATTACHMENTS];
    u32 scopeViewCount = 0;
    rlvkRenderPassKey rpKey;
    memset(&rpKey, 0, sizeof(rpKey));
    for (u32 c = 0; c < colorCount; c++)
    {
        scopeViews[scopeViewCount++] = colors[c]->view;
        rpKey.colorFormats[c] = colors[c]->format;
    }
    if (fbHasDepth) scopeViews[scopeViewCount++] = depth->view;
    rpKey.depthFormat = fbHasDepth ? depth->format : VK_FORMAT_UNDEFINED;
    rpKey.colorCount  = (unsigned char)colorCount;
    rpKey.samples     = 1;
    rpKey.colorLoad   = VK_ATTACHMENT_LOAD_OP_LOAD;
    rpKey.depthLoad   = VK_ATTACHMENT_LOAD_OP_CLEAR;
    rpKey.depthStore  = VK_ATTACHMENT_STORE_OP_STORE;
    rlvkBeginScopeRenderPass(cmdBuffer, &rpKey, scopeViews, scopeViewCount, (u32)fbW, (u32)fbH,
        NULL, fbHasDepth? &(VkClearValue){ .depthStencil = { 1.0f, 0 } } : NULL);
    RLVK.scope.fbSlot = id;
    RLVK.scope.width  = (u32)fbW;
    RLVK.scope.height = (u32)fbH;
    RLVK.scope.colorCount = colorCount;
    for (u32 c = 0; c < colorCount; c++) RLVK.scope.colorFormats[c] = colors[c]->format;
    RLVK.scope.samples = 1;      // GL: MSAA applies to the default framebuffer only, not FBOs
    RLVK.scope.flipY  = false;   // GL render textures are bottom-up: no Y flip inside FBOs
}

// Back to the swapchain scope; the render texture becomes sampleable again
void rlDisableFramebuffer(void)
{
    RLVK.State.stateGeneration++;
    u32 id = RLVK.scope.fbSlot;   // the scope actually open (bookkeeping may be stale)
    RLVK.State.currentFramebufferSlot = 0;
    if (!isGpuReady || id == 0 || id >= RLVK_MAX_FRAMEBUFFER_SLOTS || !RLVK.frameActive) return;

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
    rlvkFramebufferSlot *f = &RLVK.fbSlots[id];

    vkCmdEndRenderPass(cmdBuffer);

    for (u32 c = 0; c < f->colorCount && c < 8; c++)
    {
        rlvkTextureSlot *color = &RLVK.textureSlots[f->colorTextures[c]];
        if (!color->image || color->currentLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) continue;
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask    = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .dstAccessMask    = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .image            = color->image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            },
        });
        color->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // The depth texture becomes sampleable too (depth-render / shadowmap shaders)
    if (f->hasDepth)
    {
        rlvkTextureSlot *depth = &RLVK.textureSlots[f->depthTexture];
        if (depth->image)
        {
            vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask     = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    .srcAccessMask    = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .dstStageMask     = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .dstAccessMask    = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    .oldLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .image            = depth->image,
                    .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
                },
            });
            depth->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }

    // Resume the swapchain scope, preserving its content
    rlvkResumeSwapchainScope(cmdBuffer);
}

// Re-open the swapchain-target rendering scope preserving content (color LOAD + depth LOAD)
// after a mid-frame suspension (FBO round-trip, depth blit, in-stream texture update).
// Renders into the UNMIRRORED per-frame target: msaa image when active, else the intermediate.
static void rlvkResumeSwapchainScope(VkCommandBuffer cmdBuffer)
{
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    bool msaa = (RLVK.msaaSamples > 1);
    // Fixed-function resolve into the 1x intermediate at every scope close (MSAA only)
    VkImageView scopeViews[3];
    u32 scopeViewCount = 0;
    scopeViews[scopeViewCount++] = msaa? RLVK.msaaView[frameIndex] : RLVK.interView[frameIndex];
    if (msaa) scopeViews[scopeViewCount++] = RLVK.interView[frameIndex];
    scopeViews[scopeViewCount++] = RLVK.depthView[frameIndex];
    rlvkRenderPassKey rpKey;
    memset(&rpKey, 0, sizeof(rpKey));
    rpKey.colorFormats[0] = RLVK.swapchainFormat;
    rpKey.depthFormat     = RLVK.depthFormat;
    rpKey.colorCount      = 1;
    rpKey.samples         = msaa? 4 : 1;
    rpKey.colorLoad       = VK_ATTACHMENT_LOAD_OP_LOAD;
    rpKey.depthLoad       = VK_ATTACHMENT_LOAD_OP_LOAD;
    rpKey.depthStore      = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    rpKey.hasResolve      = msaa? 1 : 0;
    rlvkBeginScopeRenderPass(cmdBuffer, &rpKey, scopeViews, scopeViewCount,
        RLVK.swapchainExtent.width, RLVK.swapchainExtent.height, NULL, NULL);
    RLVK.scope.fbSlot = 0;
    RLVK.scope.width  = RLVK.swapchainExtent.width;
    RLVK.scope.height = RLVK.swapchainExtent.height;
    RLVK.scope.colorCount = 1;
    RLVK.scope.colorFormats[0] = RLVK.swapchainFormat;
    RLVK.scope.samples = (u32)RLVK.msaaSamples;
    RLVK.scope.flipY  = false;   // UNMIRRORED: swapchain-scope rendering matches GL memory orientation
    if (rlvkDebugFlag("RLVK_DEBUG_FBO", &s_dbgFbo)) TRACELOG(RL_LOG_WARNING, "VKDBG frameOPEN fc=%llu img=%u", (ull)RLVK.frameCounter, RLVK.currentImageIndex);
}

// Submit the current recording, wait for it, resume recording in the same frame (same arena and
// swapchain image). Host reads of GPU-written resources need every RECORDED command executed
// first, matching GL's in-order reads; vkDeviceWaitIdle cannot run work that was never submitted.
// Wait the fences of every frame slot except the recording one: full completion of submitted
// work without vkDeviceWaitIdle's whole-device drain (the recording slot's fence is reset with
// nothing pending, so including it would deadlock)
static void rlvkWaitInFlightFrames(void)
{
    u32 recording = RLVK.frameActive? (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT) : UINT32_MAX;
    for (u32 i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
        if ((i != recording) && (RLVK.frameFences[i] != VK_NULL_HANDLE)) vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[i], VK_TRUE, UINT64_MAX);
}

static void rlvkFlushFrame(void)
{
    if (!RLVK.frameActive) return;
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];

    // Close any open FBO scope through its normal path (attachment layout transitions), then
    // suspend the swapchain scope
    u32 openFb = RLVK.scope.fbSlot;
    if (openFb) rlDisableFramebuffer();
    vkCmdEndRenderPass(cmdBuffer);
    vk.EndCommandBuffer(cmdBuffer);

    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = RLVK.acquireWaited? 0u : 1u,
        .pWaitSemaphoreInfos    = &(VkSemaphoreSubmitInfo){
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = RLVK.acquireSemaphores[frameIndex],
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT },
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos    = &(VkCommandBufferSubmitInfo){
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmdBuffer },
    }, RLVK.frameFences[frameIndex]);
    RLVK.acquireWaited = true;

    vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[frameIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(RLVK.device, 1, &RLVK.frameFences[frameIndex]);
    RLVK.arenaOffset[frameIndex] = 0;   // the drained submission consumed all arena data: reuse from the start

    // Resume recording into the same frame: fresh command buffer, content preserved (LOAD)
    vkResetCommandPool(RLVK.device, RLVK.cmdPools[frameIndex], 0);
    vk.BeginCommandBuffer(cmdBuffer, &(VkCommandBufferBeginInfo){
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT });
    RLVK.boundPipeline = VK_NULL_HANDLE;    // pipeline binding is command-buffer state too
    memset(RLVK.pushedView, 0, sizeof(RLVK.pushedView));      // push-descriptor state resets with the command buffer
    s_pipelineFastValid = false;
    RLVK.State.cbEpoch++;
    memset(RLVK.pushedSampler, 0, sizeof(RLVK.pushedSampler));
    s_viewportValid = false;
    s_bindingValid   = false;
    // Pool-ring fallback: the drained submission released every snapshot set; the arena reset
    // also invalidated the shadowed UBO regions
    memset(RLVK.shadowUbo, 0, sizeof(RLVK.shadowUbo));
    RLVK.set0Dirty = true;
    if (!RLVK.Caps.pushDescriptor && RLVK.descPools[frameIndex]) vkResetDescriptorPool(RLVK.device, RLVK.descPools[frameIndex], 0);
    if (RLVK.computeDescPools[frameIndex]) vkResetDescriptorPool(RLVK.device, RLVK.computeDescPools[frameIndex], 0);

    // Host writes made after this point (arena bump continues) must be visible again
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &(VkMemoryBarrier2){
            VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_HOST_BIT,
            .srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT
                           | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT
                           | VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        },
    });

    rlvkResumeSwapchainScope(cmdBuffer);
    if (openFb) rlEnableFramebuffer(openFb);
}
// return the active render texture (fbo)
unsigned int rlGetActiveFramebuffer(void)        { return RLVK.State.currentFramebufferSlot; }
// Activate multiple draw color buffers
// NOTE: One color buffer is always active by default
void rlActiveDrawBuffers(int count)              { (void)count; }
// Depth-only blit (GL_DEPTH_BUFFER_BIT) from the READ framebuffer's depth texture into the
// current scope's depth target - deferred rendering copies gbuffer depth to the backbuffer so
// forward geometry depth-tests against it. Color blits unimplemented (nothing exercises them).
void rlBlitFramebuffer(int srcX, int srcY, int srcWidth, int srcHeight, int dstX, int dstY, int dstWidth, int dstHeight, int bufferMask)
{
    (void)srcX; (void)srcY; (void)dstX; (void)dstY;
    if (!(bufferMask & 0x00000100)) return;                     // GL_DEPTH_BUFFER_BIT only
    if (!RLVK.frameActive || RLVK.blitReadFb == 0 || RLVK.blitReadFb >= RLVK_MAX_FRAMEBUFFER_SLOTS) return;
    rlvkFramebufferSlot *src = &RLVK.fbSlots[RLVK.blitReadFb];
    if (!src->hasDepth) return;
    rlvkTextureSlot *srcDepth = &RLVK.textureSlots[src->depthTexture];
    if (!srcDepth->image) return;
    if (RLVK.scope.fbSlot != 0) return;                   // only swapchain destination exercised

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];

    vkCmdEndRenderPass(cmdBuffer);

    // src depth (whatever state it rests in) -> TRANSFER_SRC; dst frame depth -> TRANSFER_DST
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
            {   VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                // Access matched to the tracked OLD layout: after rlDisableFramebuffer the
                // depth texture rests in SHADER_READ_ONLY (sampled); only when still bound
                // as an attachment do prior depth writes need making available
                .srcStageMask     = (srcDepth->currentLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)?
                                        (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT) :
                                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .srcAccessMask    = (srcDepth->currentLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)?
                                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT :
                                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .dstAccessMask    = VK_ACCESS_2_TRANSFER_READ_BIT,
                .oldLayout        = srcDepth->currentLayout,
                .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .image            = srcDepth->image,
                .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
            },
            {   VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask     = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .srcAccessMask    = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .dstAccessMask    = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .image            = RLVK.depthImage[frameIndex],
                .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
            },
        },
    });

    // Straight copy: FBO depth AND the frame depth are both GL-oriented (bottom-up) now that
    // the swapchain scope renders unmirrored, exactly like GL's FBO->backbuffer depth blit.
    vk.CmdBlitImage(cmdBuffer, srcDepth->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        RLVK.depthImage[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
        &(VkImageBlit){
            .srcSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 },
            .srcOffsets     = { { 0, 0, 0 }, { srcWidth, srcHeight, 1 } },
            .dstSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 },
            .dstOffsets     = { { 0, 0, 0 }, { dstWidth, dstHeight, 1 } },
        }, VK_FILTER_NEAREST);

    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
            {   VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask     = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .srcAccessMask    = VK_ACCESS_2_TRANSFER_READ_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .dstAccessMask    = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .image            = srcDepth->image,
                .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
            },
            {   VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask     = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .srcAccessMask    = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .dstAccessMask    = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .image            = RLVK.depthImage[frameIndex],
                .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
            },
        },
    });
    srcDepth->currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    rlvkResumeSwapchainScope(cmdBuffer);
    RLVK.blitReadFb = 0;
}
// Bind framebuffer object (fbo)
void rlBindFramebuffer(unsigned int target, unsigned int fb)
{
    if (target == RL_READ_FRAMEBUFFER) RLVK.blitReadFb = fb;      // source for rlBlitFramebuffer
    else if (fb == 0) rlDisableFramebuffer();
    else rlEnableFramebuffer(fb);
}

// Enable color blending
void rlEnableColorBlend(void)         { RLVK.State.colorBlendEnabled = true; RLVK.State.stateGeneration++; }
// Disable color blending
void rlDisableColorBlend(void)        { RLVK.State.colorBlendEnabled = false; RLVK.State.stateGeneration++; }
// Enable depth test
void rlEnableDepthTest(void)          { RLVK.State.depthTest = true; RLVK.State.stateGeneration++; }
// Disable depth test
void rlDisableDepthTest(void)         { RLVK.State.depthTest = false; RLVK.State.stateGeneration++; }
// Enable depth write
void rlEnableDepthMask(void)          { RLVK.State.depthWrite = true; RLVK.State.stateGeneration++; }
// Disable depth write
void rlDisableDepthMask(void)         { RLVK.State.depthWrite = false; RLVK.State.stateGeneration++; }
// Enable backface culling
void rlEnableBackfaceCulling(void)    { RLVK.State.cullEnabled = true; RLVK.State.stateGeneration++; }
// Disable backface culling
void rlDisableBackfaceCulling(void)   { RLVK.State.cullEnabled = false; RLVK.State.stateGeneration++; }
// Set color mask active for screen read/draw
void rlColorMask(bool r, bool g, bool b, bool a) { RLVK.State.colorMask[0]=r; RLVK.State.colorMask[1]=g; RLVK.State.colorMask[2]=b; RLVK.State.colorMask[3]=a; }
// Set face culling mode
void rlSetCullFace(int mode)          { RLVK.State.cullMode = mode; RLVK.State.stateGeneration++; }
// Enable scissor test
void rlEnableScissorTest(void)        { RLVK.State.scissorEnabled = true; RLVK.State.stateGeneration++; }
// Disable scissor test
void rlDisableScissorTest(void)       { RLVK.State.scissorEnabled = false; RLVK.State.stateGeneration++; }
// Scissor test
void rlScissor(int x, int y, int w, int h) { RLVK.State.scissorX=x; RLVK.State.scissorY=y; RLVK.State.scissorW=w; RLVK.State.scissorH=h; RLVK.State.stateGeneration++; }
// Enable point mode
void rlEnablePointMode(void)          { RLVK.State.pointMode = true; RLVK.State.stateGeneration++; }
// Disable point mode
void rlDisablePointMode(void)         { RLVK.State.pointMode = false; RLVK.State.stateGeneration++; }
// Set the point drawing size
void rlSetPointSize(f32 s)          { RLVK.State.pointSize = s; }
// Get the point drawing size
f32 rlGetPointSize(void)            { return RLVK.State.pointSize; }
// Enable wire mode
void rlEnableWireMode(void)           { RLVK.State.wireMode = true; RLVK.State.stateGeneration++; }
// Disable wire mode
void rlDisableWireMode(void)          { RLVK.State.wireMode = false; RLVK.State.stateGeneration++; }
// Set the line drawing width
void rlSetLineWidth(f32 w)          { RLVK.State.lineWidth = w; }
// Get the line drawing width
f32 rlGetLineWidth(void)            { return RLVK.State.lineWidth; }
// Enable line aliasing
void rlEnableSmoothLines(void)        { RLVK.State.smoothLines = true; }
// Disable line aliasing
void rlDisableSmoothLines(void)       { RLVK.State.smoothLines = false; }
// Enable stereo rendering
void rlEnableStereoRender(void)       { RLVK.State.stereoRender = true; }
// Disable stereo rendering
void rlDisableStereoRender(void)      { RLVK.State.stereoRender = false; }
// Check if stereo render is enabled
bool rlIsStereoRenderEnabled(void)    { return RLVK.State.stereoRender; }

// Clear color buffer with color
void rlClearColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{ RLVK.State.clearR=r; RLVK.State.clearG=g; RLVK.State.clearB=b; RLVK.State.clearA=a; }

// Clear used screen buffers (color and depth)
void rlClearScreenBuffers(void)
{
    // Frame not open yet: the scope's loadOp CLEAR (with State.clear*) handles it lazily.
    // Inside an active scope (e.g. ClearBackground inside BeginTextureMode): clear explicitly.
    if (!RLVK.frameActive) return;
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
    // Build the clear list from what the current scope actually has attached: the swapchain
    // scope always has color+depth; an FBO may lack either (depth-only shadowmaps have no color)
    bool hasColor = true, hasDepth = true;
    if (RLVK.scope.fbSlot != 0)
    {
        rlvkFramebufferSlot *f = &RLVK.fbSlots[RLVK.scope.fbSlot];
        hasColor = (f->colorCount > 0) && RLVK.textureSlots[f->colorTextures[0]].image;
        hasDepth = f->hasDepth && RLVK.textureSlots[f->depthTexture].image;
    }
    VkClearAttachment clears[9];
    u32 clearCount = 0;
    if (hasColor) for (u32 c = 0; c < RLVK.scope.colorCount && c < 8; c++)
        clears[clearCount++] = (VkClearAttachment){
            .aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT,
            .colorAttachment = c,
            .clearValue      = { .color = { .float32 = {
                RLVK.State.clearR/255.0f, RLVK.State.clearG/255.0f,
                RLVK.State.clearB/255.0f, RLVK.State.clearA/255.0f } } } };
    if (hasDepth) clears[clearCount++] = (VkClearAttachment){
        .aspectMask      = VK_IMAGE_ASPECT_DEPTH_BIT,
        .clearValue      = { .depthStencil = { 1.0f, 0 } } };
    if (clearCount) vkCmdClearAttachments(cmdBuffer, clearCount, clears,
        1, &(VkClearRect){ { { 0, 0 }, { RLVK.scope.width, RLVK.scope.height } }, 0, 1 });
}

// Check and log OpenGL error codes
void rlCheckErrors(void)
{
    // TODO(vk): drain VK_DEBUG_UTILS messenger callbacks.
}

// Set blend mode
void rlSetBlendMode(int mode)
{
    RLVK.State.stateGeneration++;
    if ((RLVK.State.blendMode != mode) || ((mode == RL_BLEND_CUSTOM || mode == RL_BLEND_CUSTOM_SEPARATE) && RLVK.State.customBlendModified))
    {
        rlDrawRenderBatch(RLVK.currentBatch);   // flush geometry drawn with the previous mode
        RLVK.State.blendMode = mode;
        RLVK.State.customBlendModified = false;
    }
}
// Set blending mode factor and equation
void rlSetBlendFactors(int srcF, int dstF, int eq)
{
    RLVK.State.stateGeneration++;
    RLVK.State.blendSrc = srcF; RLVK.State.blendDst = dstF; RLVK.State.blendEq = eq;
    RLVK.State.customBlendModified = true;
}
// Set blending mode factor and equation separately for RGB and alpha
void rlSetBlendFactorsSeparate(int srcRGB, int dstRGB, int srcA, int dstA, int eqRGB, int eqA)
{
    RLVK.State.stateGeneration++;
    RLVK.State.blendSrcRGB = srcRGB; RLVK.State.blendDstRGB = dstRGB;
    RLVK.State.blendSrcA   = srcA;   RLVK.State.blendDstA   = dstA;
    RLVK.State.blendEqRGB  = eqRGB;  RLVK.State.blendEqA    = eqA;
    RLVK.State.customBlendModified = true;
}

//----------------------------------------------------------------------------------
// Module Functions Definition - rlgl functionality
//----------------------------------------------------------------------------------

// Initialize rlgl: OpenGL extensions, default buffers/shaders/textures, OpenGL states
void rlglInit(int width, int height)
{
    RLVK.State.framebufferWidth  = width;
    RLVK.State.framebufferHeight = height;
    RLVK.State.viewportW = width;
    RLVK.State.viewportH = height;

    // Defaults match GL backend
    RLVK.State.depthTest         = false;
    RLVK.State.depthWrite        = true;
    RLVK.State.cullEnabled       = true;                // rlgl does glEnable(GL_CULL_FACE) at init
    RLVK.State.cullMode          = RL_CULL_FACE_BACK;
    RLVK.State.cullMode          = RL_CULL_FACE_BACK;
    RLVK.State.colorBlendEnabled = true;
    RLVK.State.colorMask[0] = RLVK.State.colorMask[1] = RLVK.State.colorMask[2] = RLVK.State.colorMask[3] = true;
    RLVK.State.blendMode         = RL_BLEND_ALPHA;
    RLVK.State.pointSize         = 1.0f;
    RLVK.State.lineWidth         = 1.0f;
    RLVK.State.modelview         = rlvkMatrixIdentity();
    RLVK.State.projection        = rlvkMatrixIdentity();
    RLVK.State.transform         = rlvkMatrixIdentity();
    RLVK.State.meshMVP           = rlvkMatrixIdentity();
    RLVK.State.meshColDiffuse[0] = RLVK.State.meshColDiffuse[1] = RLVK.State.meshColDiffuse[2] = RLVK.State.meshColDiffuse[3] = 1.0f;
    RLVK.State.currentMatrix     = &RLVK.State.modelview;

    if (!rlvkInitInstance())       { TRACELOG(RL_LOG_FATAL, "RLVK: instance creation failed");        return; }
    if (!rlvkPickPhysicalDevice()) { TRACELOG(RL_LOG_FATAL, "RLVK: no suitable physical device");     return; }
    if (!rlvkInitLogicalDevice())  { TRACELOG(RL_LOG_FATAL, "RLVK: logical device creation failed");  return; }
    rlvkLoadEntrypoints();
    if (!rlvkInitSet0Layout())     { TRACELOG(RL_LOG_FATAL, "RLVK: descriptor set layout creation failed"); return; }
    if (!rlvkInitFrameRing())      { TRACELOG(RL_LOG_FATAL, "RLVK: frame ring creation failed");      return; }
    rlvkInitPipelineCache();

    isGpuReady = true;

    // Default shader (embedded SPIR-V modules) + pipeline layout for push constants / descriptors
    if (!rlvkInitDefaultShader()) { TRACELOG(RL_LOG_FATAL, "RLVK: default shader creation failed"); isGpuReady = false; return; }

    // Default 1x1 white texture: untextured draws (shapes) sample it, so white*vertexColor = vertexColor
    unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    RLVK.defaultTextureSlot = rlLoadTexture(whitePixel, 1, 1, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);

    // Dummy attribute buffer for the divisor-0 broadcast: missing attributes read constants -
    // uv vec2(0,0) @0, opaque white @8, +Z normal @12, bone ids/weights @24/@28 = GL's generic
    // attribute default (0,0,0,1)
    struct { f32 uv[2]; unsigned int white; f32 normal[3]; unsigned char boneIds[4]; f32 boneW[4]; } dummyData =
        { { 0.0f, 0.0f }, 0xFFFFFFFFu, { 0.0f, 0.0f, 1.0f }, { 0, 0, 0, 1 }, { 0.0f, 0.0f, 0.0f, 1.0f } };
    RLVK.dummyAttribSlot = rlvkCreateVBO(&dummyData, sizeof(dummyData), false, false);

    // The default shader is the embedded push-constant SPIR-V (rlvk_shaders.h), NOT a runtime
    // compile: its draws take the push-constant fast path (no uniform snapshots or descriptor
    // pushes); shaderc loads lazily on the first user shader (rlLoadShaderProgram)

    RLVK.defaultShaderLocs       = RLVK.shaderSlots[RLVK.defaultShaderSlot].locs;
    RLVK.State.currentShaderSlot = RLVK.defaultShaderSlot;
    RLVK.State.activeShaderSlot  = RLVK.defaultShaderSlot;
    RLVK.State.currentShaderLocs = RLVK.defaultShaderLocs;
    RLVK.State.currentTextureSlot = RLVK.defaultTextureSlot;

    // Default render batch (one backing buffer per frame-in-flight)
    RLVK.defaultBatch = rlLoadRenderBatch(RLVK_FRAME_INDEX_COUNT, RL_DEFAULT_BATCH_BUFFER_ELEMENTS);
    RLVK.currentBatch = &RLVK.defaultBatch;

    TRACELOG(RL_LOG_INFO, "RLVK: Vulkan backend initialized (device API %u.%u, %dx%d)",
             VK_API_VERSION_MAJOR(RLVK.Caps.apiVersion), VK_API_VERSION_MINOR(RLVK.Caps.apiVersion), width, height);
}

// Vertex Buffer Object deinitialization (memory free)
void rlglClose(void)
{
    if (!isGpuReady) return;
    if (RLVK.device) vkDeviceWaitIdle(RLVK.device);   // shutdown teardown: the one place a full device drain is the right tool

    // Release the persistent-mapped batch backing buffers and the per-frame bump arenas
    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        rlvkDestroyBatchBacking(&RLVK.batchBacking[i]);
        rlvkDestroyBatchBacking(&RLVK.arena[i]);
    }

    // Persist the driver pipeline cache for the next run, then release the cached pipelines
    rlvkSavePipelineCache();
    for (int i = 0; i < RLVK.pipelineCount; i++) vkDestroyPipeline(RLVK.device, RLVK.pipelines[i].pipeline, RLVK_ALLOC);
    RLVK.pipelineCount = 0;
    if (RLVK.pipelineCache != VK_NULL_HANDLE) vkDestroyPipelineCache(RLVK.device, RLVK.pipelineCache, RLVK_ALLOC);

    // Release the render-pass + framebuffer caches (framebuffers first: they reference the passes)
    for (int i = 0; i < RLVK.framebufferCount; i++) vkDestroyFramebuffer(RLVK.device, RLVK.framebuffers[i].framebuffer, RLVK_ALLOC);
    RLVK.framebufferCount = 0;
    for (int i = 0; i < RLVK.renderPassCount; i++) vkDestroyRenderPass(RLVK.device, RLVK.renderPasses[i].pass, RLVK_ALLOC);
    RLVK.renderPassCount = 0;

    if (rlvkDebugFlag("RLVK_MEM_REPORT", &s_dbgMem)) TRACELOG(RL_LOG_WARNING, "VKMEM local=%lldKB host=%lldKB allocs=%d vboCreate=%d vboReuse=%d", s_memLocalBytes/1024, s_memHostBytes/1024, s_memAllocCount, s_vboCreateCount, s_vboReuseCount);

    // Full lifetime cleanup so repeated InitWindow()/CloseWindow() in one process does not
    // leak: slot tables, frame ring, swapchain targets, layouts, surface, device, instance
    for (int f = 0; f < RLVK_FRAME_INDEX_COUNT; f++)
    {
        for (int d = 0; d < RLVK.deadResourceCount[f]; d++)
        {
            rlvkDeadResource *r = &RLVK.deadResources[f][d];
            if (r->framebuffer) vkDestroyFramebuffer(RLVK.device, r->framebuffer, RLVK_ALLOC);
            if (r->view)    vkDestroyImageView(RLVK.device, r->view, RLVK_ALLOC);
            if (r->sampler) vkDestroySampler(RLVK.device, r->sampler, RLVK_ALLOC);
            if (r->image)   vkDestroyImage(RLVK.device, r->image, RLVK_ALLOC);
            if (r->buffer)  vkDestroyBuffer(RLVK.device, r->buffer, RLVK_ALLOC);
            if (r->memory)  vkFreeMemory(RLVK.device, r->memory, RLVK_ALLOC);
            if (r->pipeline) vkDestroyPipeline(RLVK.device, r->pipeline, RLVK_ALLOC);
        }
        RLVK.deadResourceCount[f] = 0;
    }
    for (int i = 1; i < RLVK_MAX_TEXTURE_SLOTS; i++)
    {
        rlvkTextureSlot *t = &RLVK.textureSlots[i];
        if (!t->inUse) continue;
        if (t->view)    vkDestroyImageView(RLVK.device, t->view, RLVK_ALLOC);
        if (t->sampler) vkDestroySampler(RLVK.device, t->sampler, RLVK_ALLOC);
        if (t->image)   vkDestroyImage(RLVK.device, t->image, RLVK_ALLOC);
        if (t->memory)  vkFreeMemory(RLVK.device, t->memory, RLVK_ALLOC);
    }
    for (int i = 1; i < RLVK_MAX_BUFFER_SLOTS; i++)
    {
        rlvkBufferSlot *b = &RLVK.bufferSlots[i];
        if (b->buffer) vkDestroyBuffer(RLVK.device, b->buffer, RLVK_ALLOC);
        if (b->memory) vkFreeMemory(RLVK.device, b->memory, RLVK_ALLOC);
    }
    for (int i = 1; i < RLVK_MAX_SHADER_SLOTS; i++)
    {
        rlvkShaderSlot *s = &RLVK.shaderSlots[i];
        if (s->vertMod)  vkDestroyShaderModule(RLVK.device, s->vertMod, RLVK_ALLOC);
        if (s->fragMod)  vkDestroyShaderModule(RLVK.device, s->fragMod, RLVK_ALLOC);
        if (s->uniforms) RL_FREE(s->uniforms);
        if (s->vsStage)  RL_FREE(s->vsStage);
        if (s->fsStage)  RL_FREE(s->fsStage);
    }
    if (s_gpuPool) { vkDestroyQueryPool(RLVK.device, s_gpuPool, RLVK_ALLOC); s_gpuPool = VK_NULL_HANDLE; }

    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        if (RLVK.depthView[i])   vkDestroyImageView(RLVK.device, RLVK.depthView[i], RLVK_ALLOC);
        if (RLVK.depthImage[i])  vkDestroyImage(RLVK.device, RLVK.depthImage[i], RLVK_ALLOC);
        if (RLVK.depthMemory[i]) vkFreeMemory(RLVK.device, RLVK.depthMemory[i], RLVK_ALLOC);
        if (RLVK.interView[i])   vkDestroyImageView(RLVK.device, RLVK.interView[i], RLVK_ALLOC);
        if (RLVK.interImage[i])  vkDestroyImage(RLVK.device, RLVK.interImage[i], RLVK_ALLOC);
        if (RLVK.interMemory[i]) vkFreeMemory(RLVK.device, RLVK.interMemory[i], RLVK_ALLOC);
        if (RLVK.msaaView[i])    vkDestroyImageView(RLVK.device, RLVK.msaaView[i], RLVK_ALLOC);
        if (RLVK.msaaImage[i])   vkDestroyImage(RLVK.device, RLVK.msaaImage[i], RLVK_ALLOC);
        if (RLVK.msaaMemory[i])  vkFreeMemory(RLVK.device, RLVK.msaaMemory[i], RLVK_ALLOC);
        if (RLVK.acquireSemaphores[i]) vkDestroySemaphore(RLVK.device, RLVK.acquireSemaphores[i], RLVK_ALLOC);
        if (RLVK.frameFences[i])       vkDestroyFence(RLVK.device, RLVK.frameFences[i], RLVK_ALLOC);
        if (RLVK.cmdPools[i])          vkDestroyCommandPool(RLVK.device, RLVK.cmdPools[i], RLVK_ALLOC);
        if (RLVK.descPools[i])         vkDestroyDescriptorPool(RLVK.device, RLVK.descPools[i], RLVK_ALLOC);
        if (RLVK.computeDescPools[i])  vkDestroyDescriptorPool(RLVK.device, RLVK.computeDescPools[i], RLVK_ALLOC);
    }
    for (int i = 0; i < RLVK_MAX_SWAPCHAIN_IMAGES; i++)
    {
        if (RLVK.swapchainViews[i])   vkDestroyImageView(RLVK.device, RLVK.swapchainViews[i], RLVK_ALLOC);
        if (RLVK.renderSemaphores[i]) vkDestroySemaphore(RLVK.device, RLVK.renderSemaphores[i], RLVK_ALLOC);
    }
    if (RLVK.swapchain)      vkDestroySwapchainKHR(RLVK.device, RLVK.swapchain, RLVK_ALLOC);
    if (RLVK.set0Layout) vkDestroyDescriptorSetLayout(RLVK.device, RLVK.set0Layout, RLVK_ALLOC);
    if (RLVK.pipelineLayout) vkDestroyPipelineLayout(RLVK.device, RLVK.pipelineLayout, RLVK_ALLOC);
    if (RLVK.computeSetLayout)      vkDestroyDescriptorSetLayout(RLVK.device, RLVK.computeSetLayout, RLVK_ALLOC);
    if (RLVK.computePipelineLayout) vkDestroyPipelineLayout(RLVK.device, RLVK.computePipelineLayout, RLVK_ALLOC);
    if (RLVK.device)         vkDestroyDevice(RLVK.device, RLVK_ALLOC);
    if (RLVK.surface)        vkDestroySurfaceKHR(RLVK.instance, RLVK.surface, RLVK_ALLOC);
    if (RLVK.instance)       vkDestroyInstance(RLVK.instance, RLVK_ALLOC);
    if (RLVK.shadercCompiler) p_shaderc_compiler_release(RLVK.shadercCompiler);
    memset(&RLVK, 0, sizeof(RLVK));
    s_pipelineFastValid = false;
    isGpuReady = false;
}

// Load OpenGL extensions
// NOTE: External loader function must be provided
void rlLoadExtensions(void *loader) { (void)loader; }
// Get OpenGL procedure address
void *rlGetProcAddress(const char *name) { (void)name; return NULL; }

// Get current OpenGL version
int rlGetVersion(void) { return RL_OPENGL_43; }

void rlSetFramebufferWidth (int w) { RLVK.State.framebufferWidth  = w; }
int  rlGetFramebufferWidth (void)  { return RLVK.State.framebufferWidth; }
// Set current framebuffer height
void rlSetFramebufferHeight(int h) { RLVK.State.framebufferHeight = h; }
// Get default framebuffer height
int  rlGetFramebufferHeight(void)  { return RLVK.State.framebufferHeight; }

// Get default internal texture (white texture)
// NOTE: Default texture is a 1x1 pixel UNCOMPRESSED_R8G8B8A8
unsigned int rlGetTextureIdDefault(void) { return RLVK.defaultTextureSlot; }
unsigned int rlGetShaderIdDefault (void) { return RLVK.defaultShaderSlot; }
int *rlGetShaderLocsDefault       (void) { return RLVK.defaultShaderLocs; }

// Render batch management
//-----------------------------------------------------------------------------------------

rlRenderBatch rlLoadRenderBatch(int numBuffers, int bufferElements)
{
    rlRenderBatch batch = { 0 };
    if (!isGpuReady) { TRACELOG(RL_LOG_WARNING, "RLVK: rlLoadRenderBatch called before rlglInit"); return batch; }

    batch.vertexBuffer = (rlVertexBuffer *)RL_CALLOC(numBuffers, sizeof(rlVertexBuffer));

    for (int i = 0; i < numBuffers; i++)
    {
        if (!rlvkCreateBatchBacking(bufferElements, &RLVK.batchBacking[i % RLVK_FRAME_INDEX_COUNT]))
        {
            TRACELOG(RL_LOG_ERROR, "RLVK: failed to create batch backing buffer #%d", i);
            return batch;
        }

        rlvkBatchBackingBuffer *backing = &RLVK.batchBacking[i % RLVK_FRAME_INDEX_COUNT];
        char *base = (char *)backing->mapped;
        size_t off = 0;

        size_t posBytes = (size_t)bufferElements*3*4*sizeof(f32);
        size_t uvBytes  = (size_t)bufferElements*2*4*sizeof(f32);
        size_t nrmBytes = (size_t)bufferElements*3*4*sizeof(f32);
        size_t colBytes = (size_t)bufferElements*4*4*sizeof(unsigned char);
        size_t idxBytes = (size_t)bufferElements*6*sizeof(unsigned int);

        batch.vertexBuffer[i].elementCount = bufferElements;
        batch.vertexBuffer[i].vertices  = (f32 *)         (base + off); off += posBytes;
        batch.vertexBuffer[i].texcoords = (f32 *)         (base + off); off += uvBytes;
        batch.vertexBuffer[i].normals   = (f32 *)         (base + off); off += nrmBytes;
        batch.vertexBuffer[i].colors    = (unsigned char *) (base + off); off += colBytes;
        batch.vertexBuffer[i].indices   = (unsigned int *)  (base + off); off += idxBytes;

        unsigned int *idx = batch.vertexBuffer[i].indices;
        for (int j = 0, k = 0; j < bufferElements*6; j += 6, k++)
        {
            idx[j+0] = 4*k+0; idx[j+1] = 4*k+1; idx[j+2] = 4*k+2;
            idx[j+3] = 4*k+0; idx[j+4] = 4*k+2; idx[j+5] = 4*k+3;
        }

        batch.vertexBuffer[i].vaoId    = 0;
        batch.vertexBuffer[i].vboId[0] = 0;
        batch.vertexBuffer[i].vboId[1] = 0;
        batch.vertexBuffer[i].vboId[2] = 0;
        batch.vertexBuffer[i].vboId[3] = 0;
        batch.vertexBuffer[i].vboId[4] = 0;
    }

    batch.draws = (rlDrawCall *)RL_CALLOC(RL_DEFAULT_BATCH_DRAWCALLS, sizeof(rlDrawCall));
    for (int i = 0; i < RL_DEFAULT_BATCH_DRAWCALLS; i++)
    {
        batch.draws[i].mode      = RL_QUADS;
        batch.draws[i].textureId = RLVK.defaultTextureSlot;
    }
    batch.bufferCount  = numBuffers;
    batch.drawCounter  = 1;
    batch.currentDepth = -1.0f;

    // Per-frame bump arenas: each flush copies its used vertices here (fence-gated by the frame),
    // decoupling GPU reads from the batch's host-write buffer (which every flush overwrites).
    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        if (!rlvkCreateBatchBacking(bufferElements*RLVK_ARENA_SLOTS, &RLVK.arena[i]))
        {
            TRACELOG(RL_LOG_ERROR, "RLVK: failed to create flush arena #%d", i);
            return batch;
        }
        RLVK.arenaOffset[i] = 0;
    }

    RLVK.State.vertexCounter = 0;
    return batch;
}

// Unload default internal buffers vertex data from CPU and GPU
void rlUnloadRenderBatch(rlRenderBatch batch)
{
    if (!isGpuReady) return;
    rlvkWaitInFlightFrames();

    for (int i = 0; i < batch.bufferCount; i++)
    {
        // CPU pointers belong to backing buffer; freeing them individually would be wrong.
        batch.vertexBuffer[i].vertices  = NULL;
        batch.vertexBuffer[i].texcoords = NULL;
        batch.vertexBuffer[i].normals   = NULL;
        batch.vertexBuffer[i].colors    = NULL;
        batch.vertexBuffer[i].indices   = NULL;
    }
    RL_FREE(batch.vertexBuffer);
    RL_FREE(batch.draws);
}

// Draw render batch
// NOTE: Batch is reseted and current buffer is updated (for multi-buffer config)
void rlDrawRenderBatch(rlRenderBatch *batch)
{
    if (!batch || RLVK.State.vertexCounter == 0)
    {
        if (batch) batch->currentDepth = -1.0f;
        RLVK.State.vertexCounter = 0;
        if (batch) batch->drawCounter = 1;
        return;
    }

    rlvkBeginFrame();
    if (RLVK.frameActive)
    {
        u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
        VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];

        // Update batch vertex buffers
        //------------------------------------------------------------------------------------------------------------
        // Bump-copy this flush's used range into the per-frame arena and bind it as real vertex
        // buffers: multiple flushes coexist per frame, GPU reads decouple from the host writes
        rlVertexBuffer *srcvb = &batch->vertexBuffer[batch->currentBuffer];
        rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
        u32 vcount = (u32)RLVK.State.vertexCounter;   // vertices written this flush
        u32 icount = (vcount/4)*6;                         // quad indices (only RL_QUADS uses them)

        // The default shader consumes no normals: skip copying the normal stream entirely
        // (a third of the batch vertex bytes) and satisfy binding 2 with the dummy buffer
        rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.currentShaderSlot];
        bool wantNormals = (shader->attribLocs[RLVK_ATTRIB_NORMAL] >= 0);

        VkDeviceSize posBytes = (VkDeviceSize)vcount*3*sizeof(f32);
        VkDeviceSize uvBytes  = (VkDeviceSize)vcount*2*sizeof(f32);
        VkDeviceSize nrmBytes = wantNormals? (VkDeviceSize)vcount*3*sizeof(f32) : 0;
        VkDeviceSize colBytes = (VkDeviceSize)vcount*4*sizeof(unsigned char);
        VkDeviceSize idxBytes = (VkDeviceSize)icount*sizeof(unsigned int);

        // Arena exhaustion is handled, never dropped: drain the recording mid-frame (the wait
        // consumes all arena data, so the arena restarts from offset 0) and record the demanded
        // size so the arena grows at the next frame boundary and steady state stops draining
        VkDeviceSize posOff, uvOff, nrmOff, colOff, idxOff;
        for (int attempt = 0; ; attempt++)
        {
            posOff = (RLVK.arenaOffset[frameIndex] + 15) & ~(VkDeviceSize)15;   // 16-byte align
            uvOff  = posOff + posBytes;
            nrmOff = uvOff + uvBytes;
            colOff = nrmOff + nrmBytes;
            idxOff = colOff + colBytes;
            if (idxOff + idxBytes <= arena->sizeBytes) break;
            if (attempt > 0)
            {
                // A single flush larger than the whole arena: drop it, the growth request stands
                batch->currentDepth = -1.0f; RLVK.State.vertexCounter = 0; batch->drawCounter = 1;
                return;
            }
            rlvkFlushFrame();
            cmdBuffer = RLVK.cmdBuffers[frameIndex];
        }
        RLVK.arenaOffset[frameIndex] = idxOff + idxBytes;
        RLVK.arenaWanted[frameIndex] += (idxOff + idxBytes) - posOff + 16;   // this flush's arena demand

        char *dst = (char *)arena->mapped;
        memcpy(dst + posOff, srcvb->vertices,  (size_t)posBytes);
        // (The old +0.5px horizontal-line tie-break nudge is GONE: every scope now rasterizes
        // in GL's memory orientation - positive viewport, flip at present - so boundary
        // tie-breaks match GL natively, in 2D and 3D, for lines, triangles, and points.)
        memcpy(dst + uvOff,  srcvb->texcoords, (size_t)uvBytes);
        if (nrmBytes) memcpy(dst + nrmOff, srcvb->normals, (size_t)nrmBytes);
        memcpy(dst + colOff, srcvb->colors,    (size_t)colBytes);
        if (idxBytes) memcpy(dst + idxOff, srcvb->indices, (size_t)idxBytes);

        s_bindingValid = false;   // batch flush rebinds vertex buffers outside the mesh-draw dedup cache

        if (rlvkDebugFlag("RLVK_DEBUG_VTX", &s_dbgVtx))
        {
            const f32 *vp = (const f32 *)(dst + posOff);
            const unsigned char *cp = (const unsigned char *)(dst + colOff);
            const unsigned int *ip = (const unsigned int *)(dst + idxOff);
            TRACELOG(RL_LOG_WARNING, "VKDBG idx: [%u %u %u %u %u %u] [%u %u %u %u %u %u] idxOff=%u posOff=%u",
                ip[0], ip[1], ip[2], ip[3], ip[4], ip[5], ip[6], ip[7], ip[8], ip[9], ip[10], ip[11], (u32)idxOff, (u32)posOff);
            TRACELOG(RL_LOG_WARNING, "VKDBG vtx: n=%u v0=(%.1f,%.1f,%.2f) v1=(%.1f,%.1f,%.2f) col0=(%u,%u,%u,%u) vpState=(%d,%d,%dx%d) scopeWH=%ux%u mvp[0]=%f mvp[5]=%f mvp[12]=%f mvp[13]=%f",
                vcount, vp[0], vp[1], vp[2], vp[3], vp[4], vp[5], cp[0], cp[1], cp[2], cp[3],
                RLVK.State.viewportX, RLVK.State.viewportY, RLVK.State.viewportW, RLVK.State.viewportH,
                RLVK.scope.width, RLVK.scope.height,
                RLVK.State.projection.m0, RLVK.State.projection.m5, RLVK.State.projection.m12, RLVK.State.projection.m13);
        }

        // Vertex input layout and shader stages are baked into the cached pipeline
        // (bound per draw below); only the buffer bindings at this flush's offsets are dynamic.
        // Bound AFTER the first pipeline bind of the loop below: spec-wise vertex bindings are
        // command-buffer state independent of the pipeline, but MoltenVK resolves the Metal
        // buffer indices through the pipeline's vertex descriptor - binding buffers with no
        // (or a stale) pipeline bound leaves the attributes reading zeros on some drivers.
        bool batchBuffersBound = false;

        // Draw batch vertex buffers (considering VR stereo if required) - mirrors rlgl's eye
        // loop exactly: two half-width viewports, per-eye view offset multiplied into the
        // modelview and per-eye projection, geometry recorded twice.
        Matrix matProjection = RLVK.State.projection;
        Matrix matModelView  = RLVK.State.modelview;
        int eyeCount = RLVK.State.stereoRender ? 2 : 1;
        for (int eye = 0; eye < eyeCount; eye++)
        {
        if (eyeCount == 2)
        {
            rlViewport(eye*RLVK.State.framebufferWidth/2, 0, RLVK.State.framebufferWidth/2, RLVK.State.framebufferHeight);
            RLVK.State.modelview  = rlvkMatrixMultiply(matModelView, RLVK.State.viewOffsetStereo[eye]);
            RLVK.State.projection = RLVK.State.projectionStereo[eye];
        }

        rlvkPushConstants pc = { 0 };
        Matrix matMVP = rlvkMatrixMultiply(RLVK.State.modelview, RLVK.State.projection);
        // Fill the MVP in rlMatrixToFloat order (column-major floats), exactly as the GL backend
        // passes it to glUniformMatrix4fv. NOTE: this is NOT raylib's struct memory order (its transpose).
        pc.mvp[0]  = matMVP.m0;  pc.mvp[1]  = matMVP.m1;  pc.mvp[2]  = matMVP.m2;  pc.mvp[3]  = matMVP.m3;
        pc.mvp[4]  = matMVP.m4;  pc.mvp[5]  = matMVP.m5;  pc.mvp[6]  = matMVP.m6;  pc.mvp[7]  = matMVP.m7;
        pc.mvp[8]  = matMVP.m8;  pc.mvp[9]  = matMVP.m9;  pc.mvp[10] = matMVP.m10; pc.mvp[11] = matMVP.m11;
        pc.mvp[12] = matMVP.m12; pc.mvp[13] = matMVP.m13; pc.mvp[14] = matMVP.m14; pc.mvp[15] = matMVP.m15;
        pc.colDiffuse[0] = pc.colDiffuse[1] = pc.colDiffuse[2] = pc.colDiffuse[3] = 1.0f;

        if (shader->usesUbo)
        {
            // glUniform semantics (mirrors rlgl's flush): write MVP + white colDiffuse into the
            // current shader's staging, snapshot it into the arena, and push samplers > unit 0
            int *L = RLVK.State.currentShaderLocs;
            if (L)
            {
                // Mirrors rlgl's rlDrawRenderBatch uniform uploads: MVP, projection, view,
                // model (= State.transform), normal (= transpose(invert(transform))), colDiffuse
                if (L[SHADER_LOC_MATRIX_MVP]        >= 0) rlvkShaderWriteUniform(shader, L[SHADER_LOC_MATRIX_MVP], pc.mvp, 64);
                if (L[SHADER_LOC_MATRIX_PROJECTION] >= 0) rlvkShaderWriteMatrixUniform(shader, L[SHADER_LOC_MATRIX_PROJECTION], RLVK.State.projection);
                if (L[SHADER_LOC_MATRIX_VIEW]       >= 0) rlvkShaderWriteMatrixUniform(shader, L[SHADER_LOC_MATRIX_VIEW], RLVK.State.modelview);
                if (L[SHADER_LOC_MATRIX_MODEL]      >= 0) rlvkShaderWriteMatrixUniform(shader, L[SHADER_LOC_MATRIX_MODEL], RLVK.State.transform);
                if (L[SHADER_LOC_MATRIX_NORMAL]     >= 0) rlvkShaderWriteMatrixUniform(shader, L[SHADER_LOC_MATRIX_NORMAL], rlvkMatrixTranspose(rlvkMatrixInvert(RLVK.State.transform)));
                if (L[SHADER_LOC_COLOR_DIFFUSE]     >= 0) rlvkShaderWriteUniform(shader, L[SHADER_LOC_COLOR_DIFFUSE], pc.colDiffuse, 16);
            }
            rlvkBindShaderUbos(cmdBuffer, shader);
            rlvkBindShaderSamplers(cmdBuffer, shader, false);
        }
        else vk.CmdPushConstants(cmdBuffer, RLVK.pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        // Same per-draw structure as the GL backend: LINES/TRIANGLES non-indexed, QUADS indexed.
        // The draw's texture is pushed at unit 0 (rlgl texture-unit semantics).
        for (int i = 0, vertexOffset = 0; i < batch->drawCounter; i++)
        {
            rlDrawCall *drawCall = &batch->draws[i];
            if (rlvkDebugFlag("RLVK_DEBUG_FLUSH", &s_dbgFlush)) TRACELOG(RL_LOG_WARNING,
                "VKDBG flush draw %d/%d mode=%d verts=%d tex=%u shader=%u scope=%u depthT=%d vtxCtr=%d",
                i, batch->drawCounter, drawCall->mode, drawCall->vertexCount, drawCall->textureId,
                RLVK.State.currentShaderSlot, RLVK.scope.fbSlot, (int)RLVK.State.depthTest, RLVK.State.vertexCounter);
            if (drawCall->vertexCount > 0)
            {
                rlvkPushTexture(cmdBuffer, 0, drawCall->textureId);
                rlvkBindPipeline(cmdBuffer, (drawCall->mode == RL_LINES)? 0 : 1,
                    RLVK_VLAYOUT_BATCH, RLVK.State.currentShaderSlot);
                rlvkFlushSet0(cmdBuffer);
                if (!batchBuffersBound)
                {
                    vkCmdBindVertexBuffers(cmdBuffer, 0, 4,
                        (VkBuffer[]){ arena->buffer, arena->buffer, wantNormals? arena->buffer : RLVK.bufferSlots[RLVK.dummyAttribSlot].buffer, arena->buffer },
                        (VkDeviceSize[]){ posOff, uvOff, wantNormals? nrmOff : 12, colOff });
                    vkCmdBindIndexBuffer(cmdBuffer, arena->buffer, idxOff, VK_INDEX_TYPE_UINT32);
                    rlvkBindDummyAttribBuffers(cmdBuffer, RLVK_VLAYOUT_BATCH, shader);
                    batchBuffersBound = true;
                }
                if ((drawCall->mode == RL_LINES) || (drawCall->mode == RL_TRIANGLES))
                    vk.CmdDraw(cmdBuffer, drawCall->vertexCount, 1, vertexOffset, 0);
                else // RL_QUADS -> 2 triangles per quad via the index buffer
                    vk.CmdDrawIndexed(cmdBuffer, drawCall->vertexCount/4*6, 1, vertexOffset/4*6, 0, 0);
            }
            vertexOffset += drawCall->vertexCount + drawCall->vertexAlignment;
        }
        }   // eye loop

        // Restore viewport and matrices to pre-stereo state (mirrors rlgl)
        if (eyeCount == 2) rlViewport(0, 0, RLVK.State.framebufferWidth, RLVK.State.framebufferHeight);
        RLVK.State.projection = matProjection;
        RLVK.State.modelview  = matModelView;

        // Cycle to the next backing buffer so the next frame doesn't overwrite in-flight data
        batch->currentBuffer++;
        if (batch->currentBuffer >= batch->bufferCount) batch->currentBuffer = 0;
    }

    // Reset batch buffers
    //------------------------------------------------------------------------------------------------------------
    // Reset vertex counter for next frame
    RLVK.State.vertexCounter = 0;
    batch->currentDepth = -1.0f;
    for (int i = 0; i < RL_DEFAULT_BATCH_DRAWCALLS; i++)
    {
        batch->draws[i].mode        = RL_QUADS;
        batch->draws[i].vertexCount = 0;
        batch->draws[i].textureId   = RLVK.defaultTextureSlot;
    }
    batch->drawCounter = 1;
}

// Set the active render batch for rlgl
void rlSetRenderBatchActive(rlRenderBatch *batch) { RLVK.currentBatch = (batch != NULL) ? batch : &RLVK.defaultBatch; }
// Update and draw internal render batch
void rlDrawRenderBatchActive(void)                { rlDrawRenderBatch(RLVK.currentBatch); }

// Check internal buffer overflow for a given number of vertex
// and force a rlRenderBatch draw call if required
bool rlCheckRenderBatchLimit(int vCount)
{
    if (!RLVK.currentBatch) return false;
    rlVertexBuffer *vb = &RLVK.currentBatch->vertexBuffer[RLVK.currentBatch->currentBuffer];
    if ((RLVK.State.vertexCounter + vCount) >= (vb->elementCount*4))
    {
        int currentMode    = RLVK.currentBatch->draws[RLVK.currentBatch->drawCounter - 1].mode;
        unsigned int curTx = RLVK.currentBatch->draws[RLVK.currentBatch->drawCounter - 1].textureId;
        rlDrawRenderBatch(RLVK.currentBatch);
        RLVK.currentBatch->draws[RLVK.currentBatch->drawCounter - 1].mode      = currentMode;
        RLVK.currentBatch->draws[RLVK.currentBatch->drawCounter - 1].textureId = curTx;
        return true;
    }
    return false;
}

// Set current texture to use
void rlSetTexture(unsigned int id)
{
    rlRenderBatch *batch = RLVK.currentBatch;
    if (!batch) return;

    if (id == 0)
    {
        if (RLVK.State.vertexCounter >= batch->vertexBuffer[batch->currentBuffer].elementCount*4)
            rlDrawRenderBatch(batch);
        RLVK.State.currentTextureSlot = RLVK.defaultTextureSlot;
        return;
    }

    RLVK.State.currentTextureSlot = id;

    rlDrawCall *cur = &batch->draws[batch->drawCounter - 1];
    if (cur->textureId != id)
    {
        if (cur->vertexCount > 0)
        {
            if (cur->mode == RL_LINES)
                cur->vertexAlignment = (cur->vertexCount < 4) ? cur->vertexCount : (cur->vertexCount % 4);
            else if (cur->mode == RL_TRIANGLES)
                cur->vertexAlignment = (cur->vertexCount < 4) ? 1 : (4 - (cur->vertexCount % 4));
            else
                cur->vertexAlignment = 0;

            if (!rlCheckRenderBatchLimit(cur->vertexAlignment))
            {
                RLVK.State.vertexCounter += cur->vertexAlignment;
                batch->drawCounter++;
                batch->draws[batch->drawCounter - 1].mode = batch->draws[batch->drawCounter - 2].mode;
            }
        }
        if (batch->drawCounter >= RL_DEFAULT_BATCH_DRAWCALLS) rlDrawRenderBatch(batch);

        batch->draws[batch->drawCounter - 1].textureId   = id;
        batch->draws[batch->drawCounter - 1].vertexCount = 0;
    }
}

// Vertex data management
//-----------------------------------------------------------------------------------------

// Upload data into a DEVICE_LOCAL buffer: mid-frame the copy records into the frame's command
// buffer (staged via the bump arena, GL command order); at load time it runs as a one-shot
// fenced submission through a transient staging buffer
static void rlvkUploadBuffer(VkBuffer dst, u32 dstOffset, const void *data, u32 size)
{
    if (RLVK.frameActive)
    {
        u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
        rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
        VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
        VkDeviceSize off = (RLVK.arenaOffset[frameIndex] + 15) & ~(VkDeviceSize)15;
        if (off + size > arena->sizeBytes)
        {
            RLVK.arenaWanted[frameIndex] += size + 16;
            rlvkFlushFrame();
            cmdBuffer = RLVK.cmdBuffers[frameIndex];
            off = 0;
            if (size > arena->sizeBytes) { TRACELOG(RL_LOG_WARNING, "RLVK: buffer upload larger than arena"); return; }
        }
        memcpy((char *)arena->mapped + off, data, size);
        RLVK.arenaOffset[frameIndex] = off + size;
        RLVK.arenaWanted[frameIndex] += size + 16;

        // Copies and buffer barriers are illegal inside a rendering scope: close any open FBO
        // scope through its normal path, suspend the swapchain scope, record, then reopen
        // (content persists, both scopes resume with LOAD) - same dance as rlUpdateTexture
        u32 openFb = RLVK.scope.fbSlot;
        if (openFb) rlDisableFramebuffer();
        vkCmdEndRenderPass(cmdBuffer);

        vkCmdCopyBuffer(cmdBuffer, arena->buffer, dst, 1,
            &(VkBufferCopy){ .srcOffset = off, .dstOffset = dstOffset, .size = size });
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &(VkBufferMemoryBarrier2){
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT,
                .buffer        = dst,
                .offset        = dstOffset,
                .size          = size,
            },
        });

        rlvkResumeSwapchainScope(cmdBuffer);
        if (openFb) rlEnableFramebuffer(openFb);
        return;
    }

    // Load time: transient staging buffer + one-shot submission, waited synchronously
    VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE; void *map = NULL;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
        &(VkBufferCreateInfo){ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE },
        RLVK_ALLOC, &staging));
    VkMemoryRequirements memReq; vkGetBufferMemoryRequirements(RLVK.device, staging, &memReq);
    stagingMem = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(RLVK.device, staging, stagingMem, 0);
    vkMapMemory(RLVK.device, stagingMem, 0, size, 0, &map);
    memcpy(map, data, size);
    vkUnmapMemory(RLVK.device, stagingMem);

    // Own transient fence: the frame fences belong to the swapchain and may not exist yet
    // (this path runs during rlglInit for the dummy attribute buffer, before any surface)
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    rlvkWaitInFlightFrames();
    vkResetCommandPool(RLVK.device, RLVK.cmdPools[frameIndex], 0);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
    vk.BeginCommandBuffer(cmdBuffer, &(VkCommandBufferBeginInfo){
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT });
    vkCmdCopyBuffer(cmdBuffer, staging, dst, 1, &(VkBufferCopy){ .dstOffset = dstOffset, .size = size });
    vk.EndCommandBuffer(cmdBuffer);
    VkFence uploadFence = VK_NULL_HANDLE;
    vkCreateFence(RLVK.device, &(VkFenceCreateInfo){ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }, RLVK_ALLOC, &uploadFence);
    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &(VkCommandBufferSubmitInfo){
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmdBuffer },
    }, uploadFence);
    vkWaitForFences(RLVK.device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(RLVK.device, uploadFence, RLVK_ALLOC);
    vkDestroyBuffer(RLVK.device, staging, RLVK_ALLOC);
    vkFreeMemory(RLVK.device, stagingMem, RLVK_ALLOC);
}

//----------------------------------------------------------------------------------
// Host <-> image transfer (Vulkan 1.1 baseline)
// Replaces VK_EXT_host_image_copy with the classic staging buffer + one-shot submission.
// A TRANSIENT command pool per call keeps this safe even while a frame is being recorded
// (never touches the frame ring's pools); synchronous by design - these are load-time and
// GL-style read-back paths, not per-frame hot paths.
//----------------------------------------------------------------------------------

static VkCommandBuffer rlvkOneShotBegin(VkCommandPool *outPool)
{
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(RLVK.device,
        &(VkCommandPoolCreateInfo){
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = RLVK.graphicsFamily,
        }, RLVK_ALLOC, &pool) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(RLVK.device,
        &(VkCommandBufferAllocateInfo){
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
        }, &cmdBuffer);
    if (cmdBuffer == VK_NULL_HANDLE) { vkDestroyCommandPool(RLVK.device, pool, RLVK_ALLOC); return VK_NULL_HANDLE; }
    vk.BeginCommandBuffer(cmdBuffer, &(VkCommandBufferBeginInfo){
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT });
    *outPool = pool;
    return cmdBuffer;
}

static void rlvkOneShotEnd(VkCommandPool pool, VkCommandBuffer cmdBuffer)
{
    vk.EndCommandBuffer(cmdBuffer);
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(RLVK.device, &(VkFenceCreateInfo){ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }, RLVK_ALLOC, &fence);
    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &(VkCommandBufferSubmitInfo){
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmdBuffer },
    }, fence);
    vkWaitForFences(RLVK.device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(RLVK.device, fence, RLVK_ALLOC);
    vkDestroyCommandPool(RLVK.device, pool, RLVK_ALLOC);
}

// Record oldLayout -> newLayout for the given mip/layer ranges into cmdBuffer
static void rlvkCmdTransitionImage(VkCommandBuffer cmdBuffer, VkImage image, VkImageAspectFlags aspect,
    u32 baseMip, u32 mipCount, u32 baseLayer, u32 layerCount, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask    = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask    = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT
                              | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout        = oldLayout,
            .newLayout        = newLayout,
            .image            = image,
            .subresourceRange = { aspect, baseMip, mipCount, baseLayer, layerCount },
        },
    });
}

// One-shot layout transition (image with no data to upload still needs to leave UNDEFINED)
static void rlvkHostTransitionImage(VkImage image, VkImageAspectFlags aspect,
    u32 mipCount, u32 baseLayer, u32 layerCount, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = rlvkOneShotBegin(&pool);
    if (cmdBuffer == VK_NULL_HANDLE) return;
    rlvkCmdTransitionImage(cmdBuffer, image, aspect, 0, mipCount, baseLayer, layerCount, oldLayout, newLayout);
    rlvkOneShotEnd(pool, cmdBuffer);
}

// Staging upload into an image region, transitioning oldLayout -> finalLayout around the copy.
// `data` is tightly packed; multi-layer uploads stack layers consecutively.
static void rlvkStagingUploadImage(VkImage image, VkImageAspectFlags aspect,
    int x, int y, u32 width, u32 height, u32 baseLayer, u32 layerCount,
    VkImageLayout oldLayout, VkImageLayout finalLayout, const void *data, VkDeviceSize bytes)
{
    if ((data == NULL) || (bytes == 0))
    {
        if (oldLayout != finalLayout) rlvkHostTransitionImage(image, aspect, 1, baseLayer, layerCount, oldLayout, finalLayout);
        return;
    }

    VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE; void *map = NULL;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
        &(VkBufferCreateInfo){ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE },
        RLVK_ALLOC, &staging));
    VkMemoryRequirements memReq; vkGetBufferMemoryRequirements(RLVK.device, staging, &memReq);
    stagingMem = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(RLVK.device, staging, stagingMem, 0);
    vkMapMemory(RLVK.device, stagingMem, 0, bytes, 0, &map);
    memcpy(map, data, (size_t)bytes);
    vkUnmapMemory(RLVK.device, stagingMem);

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = rlvkOneShotBegin(&pool);
    if (cmdBuffer != VK_NULL_HANDLE)
    {
        rlvkCmdTransitionImage(cmdBuffer, image, aspect, 0, 1, baseLayer, layerCount, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(cmdBuffer, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
            &(VkBufferImageCopy){
                .imageSubresource = { aspect, 0, baseLayer, layerCount },
                .imageOffset      = { x, y, 0 },
                .imageExtent      = { width, height, 1 },
            });
        rlvkCmdTransitionImage(cmdBuffer, image, aspect, 0, 1, baseLayer, layerCount, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout);
        rlvkOneShotEnd(pool, cmdBuffer);
    }
    vkDestroyBuffer(RLVK.device, staging, RLVK_ALLOC);
    vkFreeMemory(RLVK.device, stagingMem, RLVK_ALLOC);
}

// Staging read-back of a full single-layer image (GL-style synchronous pixel read).
// The image's layout is preserved (transitioned to TRANSFER_SRC and back).
static void rlvkStagingReadImage(VkImage image, VkImageAspectFlags aspect,
    u32 width, u32 height, VkImageLayout currentLayout, void *out, VkDeviceSize bytes)
{
    VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE; void *map = NULL;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
        &(VkBufferCreateInfo){ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE },
        RLVK_ALLOC, &staging));
    VkMemoryRequirements memReq; vkGetBufferMemoryRequirements(RLVK.device, staging, &memReq);
    stagingMem = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(RLVK.device, staging, stagingMem, 0);

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = rlvkOneShotBegin(&pool);
    if (cmdBuffer != VK_NULL_HANDLE)
    {
        rlvkCmdTransitionImage(cmdBuffer, image, aspect, 0, 1, 0, 1, currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vk.CmdCopyImageToBuffer(cmdBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1,
            &(VkBufferImageCopy){
                .imageSubresource = { aspect, 0, 0, 1 },
                .imageExtent      = { width, height, 1 },
            });
        rlvkCmdTransitionImage(cmdBuffer, image, aspect, 0, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, currentLayout);
        rlvkOneShotEnd(pool, cmdBuffer);

        vkMapMemory(RLVK.device, stagingMem, 0, bytes, 0, &map);
        memcpy(out, map, (size_t)bytes);
        vkUnmapMemory(RLVK.device, stagingMem);
    }
    vkDestroyBuffer(RLVK.device, staging, RLVK_ALLOC);
    vkFreeMemory(RLVK.device, stagingMem, RLVK_ALLOC);
}

static u32 rlvkCreateVBO(const void *data, int size, bool isIndex, bool dynamic)
{
    // Reuse a pooled DYNAMIC buffer first: per-frame streams become a memcpy instead of a
    // create/map/destroy cycle; safe once the frame ring has wrapped past the freeing fence
    if (dynamic) for (u32 i = 1; i < RLVK_MAX_BUFFER_SLOTS; i++)
    {
        rlvkBufferSlot *p = &RLVK.bufferSlots[i];
        if (p->inUse || (p->buffer == VK_NULL_HANDLE) || (p->mapped == NULL)) continue;
        if ((p->isIndex != isIndex) || (p->sizeBytes < (u32)size) || (p->sizeBytes > 4u*(u32)size)) continue;
        if (p->freedFrame + RLVK_FRAME_INDEX_COUNT > RLVK.frameCounter) continue;
        p->inUse = true;
        s_vboReuseCount++;
        if (data) memcpy(p->mapped, data, (size_t)size);
        return i;
    }

    u32 slot = rlvkAllocBufferSlot();
    if (slot == RLVK_INVALID_SLOT) return RLVK_INVALID_SLOT;
    rlvkBufferSlot *b = &RLVK.bufferSlots[slot];
    // The slot may hold pooled resources that missed the reuse checks: evict them safely
    if (b->buffer != VK_NULL_HANDLE) rlvkDeferDestroy(b->buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, b->memory, VK_NULL_HANDLE);
    s_vboCreateCount++;
    b->sizeBytes = (u32)size;
    b->isIndex   = isIndex;
    VkBufferUsageFlags usage =
        (isIndex ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                 : (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));

    // Static buffers live DEVICE_LOCAL (VRAM-bandwidth reads, not per-frame bus streaming);
    // dynamic buffers stay host-cached and persistently mapped for cheap CPU writes
    if (!dynamic) usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
        &(VkBufferCreateInfo){ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = (VkDeviceSize)size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE },
        RLVK_ALLOC, &b->buffer));
    VkMemoryRequirements memReq; vkGetBufferMemoryRequirements(RLVK.device, b->buffer, &memReq);
    b->memory = rlvkAllocMemory(memReq, dynamic? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                                : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindBufferMemory(RLVK.device, b->buffer, b->memory, 0);
    if (dynamic)
    {
        vkMapMemory(RLVK.device, b->memory, 0, (VkDeviceSize)size, 0, &b->mapped);
        if (data) memcpy(b->mapped, data, (size_t)size);
    }
    else
    {
        b->mapped = NULL;
        if (data) rlvkUploadBuffer(b->buffer, 0, data, (u32)size);
    }
    return slot;
}

// Load vertex array object (VAO)
unsigned int rlLoadVertexArray(void)
{
    if (!isGpuReady) return 0;
    for (u32 i = 1; i < RLVK_MAX_VAO_SLOTS; i++)
    {
        if (!RLVK.vertexArrays[i].inUse)
        {
            memset(&RLVK.vertexArrays[i], 0, sizeof(rlvkVertexArray));
            RLVK.vertexArrays[i].inUse = true;
            RLVK.State.currentVAO = i;
            return i;
        }
    }
    return 0;
}
// Load a new attributes buffer
unsigned int rlLoadVertexBuffer(const void *data, int size, bool dynamic)
{
    (void)dynamic;
    u32 slot = rlvkCreateVBO(data, size, false, dynamic);
    if (slot == RLVK_INVALID_SLOT) return 0;
    RLVK.State.currentVBO = slot;
    return slot;
}
// Load a new attributes element buffer
unsigned int rlLoadVertexBufferElement(const void *data, int size, bool dynamic)
{
    (void)dynamic;
    u32 slot = rlvkCreateVBO(data, size, true, dynamic);
    if (slot == RLVK_INVALID_SLOT) return 0;
    if (RLVK.State.currentVAO && RLVK.State.currentVAO < RLVK_MAX_VAO_SLOTS)
        RLVK.vertexArrays[RLVK.State.currentVAO].indexSlot = slot;
    return slot;
}
// Update vertex buffer with new data
// NOTE: dataSize and offset must be provided in bytes
void rlUpdateVertexBuffer(unsigned int id, const void *d, int s, int o)
{
    if (!id || id >= RLVK_MAX_BUFFER_SLOTS || !d) return;
    rlvkBufferSlot *b = &RLVK.bufferSlots[id];
    if (b->mapped) memcpy((char *)b->mapped + o, d, (size_t)s);            // dynamic: direct host write
    else if (b->buffer) rlvkUploadBuffer(b->buffer, (u32)o, d, (u32)s);  // static: staged copy (glBufferSubData semantics)
}
// Update vertex buffer elements with new data
// NOTE: dataSize and offset must be provided in bytes
void rlUpdateVertexBufferElements(unsigned int id, const void *d, int s, int o) { rlUpdateVertexBuffer(id, d, s, o); }
void rlUnloadVertexArray (unsigned int id) { if (id && id < RLVK_MAX_VAO_SLOTS) RLVK.vertexArrays[id].inUse = false; }
// Unload vertex buffer (VBO)
void rlUnloadVertexBuffer(unsigned int id) { if (id && id < RLVK_MAX_BUFFER_SLOTS) { RLVK.bufferSlots[id].inUse = false; RLVK.bufferSlots[id].freedFrame = (u32)RLVK.frameCounter; } }
// Set vertex attribute
void rlSetVertexAttribute(unsigned int idx, int compCount, int type, bool norm, int stride, int offset)
{
    (void)compCount; (void)type; (void)norm; (void)stride;
    u32 vao = RLVK.State.currentVAO;
    if (!vao || vao >= RLVK_MAX_VAO_SLOTS) return;
    rlvkVertexArray *a = &RLVK.vertexArrays[vao];
    u32 vbo = (RLVK.State.currentVBO < RLVK_MAX_BUFFER_SLOTS) ? RLVK.State.currentVBO : 0;

    // idx is a canonical raylib attribute LOCATION: shaders are compiled with canonicalized
    // locations (rlvkCanonicalizeInputLocations), so UploadMesh's fixed constants and DrawMesh's
    // reflected shader locs are the same numbers - exact rlgl/glBindAttribLocation semantics.
    switch (idx)
    {
        case 0:  a->posSlot    = vbo; a->posOffset    = (u32)offset; break;
        case 1:  a->uvSlot     = vbo; a->uvOffset     = (u32)offset; break;
        case 2:  a->normalSlot = vbo; a->normalOffset = (u32)offset; break;
        case 3:  a->colorSlot  = vbo; a->colorOffset  = (u32)offset; break;
        case 4:  a->tangentSlot = vbo; a->tangentOffset = (u32)offset; break;
        case 5:  a->uv2Slot    = vbo; a->uv2Offset    = (u32)offset; break;
        case 7:  a->boneIdSlot = vbo; a->boneIdOffset = (u32)offset;
                 if (rlvkDebugFlag("RLVK_DEBUG_VAO", &s_dbgVao)) TRACELOG(RL_LOG_WARNING, "VKDBG vao=%u boneIds vbo=%u", vao, vbo);
                 break;
        case 8:  a->boneWtSlot = vbo; a->boneWtOffset = (u32)offset;
                 if (rlvkDebugFlag("RLVK_DEBUG_VAO", &s_dbgVao)) TRACELOG(RL_LOG_WARNING, "VKDBG vao=%u boneWts vbo=%u", vao, vbo);
                 break;
        case 9:  if (offset == 0) { a->instSlot = vbo; a->instOffset = 0; } break;   // mat4 columns 9..12
        default: break;
    }
}
// Set vertex attribute divisor
void rlSetVertexAttributeDivisor(unsigned int idx, int d) { (void)idx;(void)d; }
// Set shader value attribute
void rlSetVertexAttributeDefault(int loc, const void *v, int t, int c) { (void)loc;(void)v;(void)t;(void)c; }

// Record a mesh draw (DrawMesh path): VAO buffers bind at raylib's canonical locations,
// mvp/colDiffuse come from rlSetUniform*, missing attributes ride the divisor-0 broadcast
static void rlvkDrawMesh(int offset, int count, bool indexed, int instances)
{
    if (!isGpuReady) return;
    u32 vao = RLVK.State.currentVAO;
    if (!vao || vao >= RLVK_MAX_VAO_SLOTS || !RLVK.vertexArrays[vao].inUse) return;
    rlvkVertexArray *a = &RLVK.vertexArrays[vao];
    if (a->posSlot == 0) return;

    rlvkBeginFrame();
    if (!RLVK.frameActive) return;
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];

    bool hasUV     = (a->uvSlot != 0);
    bool hasNormal = (a->normalSlot != 0);
    bool hasColor  = (a->colorSlot != 0);
    VkBuffer dummy = RLVK.bufferSlots[RLVK.dummyAttribSlot].buffer;

    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];

    // Optional attribute tiers at bindings 4+, mutually exclusive with the same priority the
    // dynamic path's overwriting vkCmdSetVertexInputEXT calls resolved to: instancing wins,
    // then bones, then uv2/tangent
    bool wantUV2   = (a->uv2Slot != 0)     && (shader->attribLocs[RLVK_ATTRIB_TEXCOORD2] >= 0);
    bool wantTan   = (a->tangentSlot != 0) && (shader->attribLocs[RLVK_ATTRIB_TANGENT] >= 0);
    bool wantBones = (shader->attribLocs[RLVK_ATTRIB_BONEIDS] >= 0) && (shader->attribLocs[RLVK_ATTRIB_BONEWEIGHTS] >= 0);
    bool wantInst  = (instances > 1) && a->instSlot && (shader->attribLocs[RLVK_ATTRIB_INSTANCE_TX] >= 0);
    bool realBones = (a->boneIdSlot != 0) && (a->boneWtSlot != 0);

    unsigned short vertexLayout = RLVK_VLAYOUT_MESH;
    if (hasUV)     vertexLayout |= RLVK_VLAYOUT_MESH_UV;
    if (hasNormal) vertexLayout |= RLVK_VLAYOUT_MESH_NORMAL;
    if (hasColor)  vertexLayout |= RLVK_VLAYOUT_MESH_COLOR;
    if (wantInst)       vertexLayout |= RLVK_VLAYOUT_MESH_INSTANCED;
    else if (wantBones) vertexLayout |= realBones? RLVK_VLAYOUT_MESH_BONES : RLVK_VLAYOUT_MESH_BONES_DUMMY;
    else
    {
        if (wantUV2) vertexLayout |= RLVK_VLAYOUT_MESH_UV2;
        if (wantTan) vertexLayout |= RLVK_VLAYOUT_MESH_TANGENT;
    }

    // Binding-state dedup: when this draw binds exactly what the previous mesh draw did, only
    // push-constants/uniforms and the draw itself run (thousands of DrawModel of one mesh);
    // invalidated on command-buffer restart and by the batch-flush / quad-blit paths
    int rlvkTexSlot = RLVK.State.activeTextureSlots[0] ? RLVK.State.activeTextureSlots[0] : RLVK.State.currentTextureSlot;
    rlvkBindingSig bsig; memset(&bsig, 0, sizeof(bsig));
    bsig.shaderSlot = (int)RLVK.State.activeShaderSlot; bsig.texSlot = rlvkTexSlot;
    bsig.posSlot = (int)a->posSlot; bsig.uvSlot = (int)a->uvSlot; bsig.normalSlot = (int)a->normalSlot; bsig.colorSlot = (int)a->colorSlot;
    bsig.uv2Slot = (int)a->uv2Slot; bsig.tangentSlot = (int)a->tangentSlot; bsig.boneIdSlot = (int)a->boneIdSlot; bsig.boneWtSlot = (int)a->boneWtSlot;
    bsig.posOff = (ull)a->posOffset; bsig.uvOff = (ull)a->uvOffset; bsig.normalOff = (ull)a->normalOffset; bsig.colorOff = (ull)a->colorOffset;
    bsig.uv2Off = (ull)a->uv2Offset; bsig.tangentOff = (ull)a->tangentOffset; bsig.boneIdOff = (ull)a->boneIdOffset; bsig.boneWtOff = (ull)a->boneWtOffset;
    bool sameBinding = (s_bindingValid && (memcmp(&bsig, &s_bindingSig, sizeof(bsig)) == 0));
    if (!sameBinding) { s_bindingSig = bsig; s_bindingValid = true; }

    // Pipeline MUST be bound BEFORE vertex buffers: MoltenVK resolves Metal buffer
    // indices via the active pipeline's reflection data. Without a pipeline, vertex buffer
    // bindings are silently lost and attribute reads return all zeros.
    rlvkBindPipeline(cmdBuffer, 1, vertexLayout, RLVK.State.activeShaderSlot);

    // Vertex layout and shader stages are baked into the cached pipeline; only the buffer
    // bindings are recorded here. Missing mesh attributes fall back to the divisor-0
    // broadcast constants in the dummy buffer.
    if (!sameBinding)
    vkCmdBindVertexBuffers(cmdBuffer, 0, 4,
        (VkBuffer[]){
            RLVK.bufferSlots[a->posSlot].buffer,
            hasUV     ? RLVK.bufferSlots[a->uvSlot].buffer     : dummy,
            hasNormal ? RLVK.bufferSlots[a->normalSlot].buffer : dummy,
            hasColor  ? RLVK.bufferSlots[a->colorSlot].buffer  : dummy,
        },
        (VkDeviceSize[]){
            a->posOffset,
            hasUV     ? a->uvOffset     : 0,    // dummy offset  0 = vec2(0,0)
            hasNormal ? a->normalOffset : 12,   // dummy offset 12 = +Z normal
            hasColor  ? a->colorOffset  : 8,    // dummy offset  8 = opaque white
        });

    if (shader->usesUbo)
    {
        // Uniforms (mvp, colDiffuse, user values) were already written by DrawMesh via
        // rlSetUniform* into the staging; snapshot, then resolve ALL sampler bindings (incl. 0)
        rlvkBindShaderUbos(cmdBuffer, shader);
        if (!sameBinding) rlvkBindShaderSamplers(cmdBuffer, shader, true);   // samplers unchanged across identical binds
    }
    else
    {
        rlvkPushConstants pc = { 0 };
        Matrix m = RLVK.State.meshMVP;
        pc.mvp[0]=m.m0;  pc.mvp[1]=m.m1;  pc.mvp[2]=m.m2;  pc.mvp[3]=m.m3;
        pc.mvp[4]=m.m4;  pc.mvp[5]=m.m5;  pc.mvp[6]=m.m6;  pc.mvp[7]=m.m7;
        pc.mvp[8]=m.m8;  pc.mvp[9]=m.m9;  pc.mvp[10]=m.m10; pc.mvp[11]=m.m11;
        pc.mvp[12]=m.m12; pc.mvp[13]=m.m13; pc.mvp[14]=m.m14; pc.mvp[15]=m.m15;
        memcpy(pc.colDiffuse, RLVK.State.meshColDiffuse, sizeof(f32)*4);
        vk.CmdPushConstants(cmdBuffer, RLVK.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        if (!sameBinding) rlvkPushTexture(cmdBuffer, 0, rlvkTexSlot);
    }

    // Optional texcoord2 / tangent streams at sequential bindings from 4 (layout baked)
    if (!sameBinding && !wantInst && !wantBones && (wantUV2 || wantTan))
    {
        VkBuffer    extraBufs[2]; VkDeviceSize extraOffs[2]; u32 extraCount = 0;
        if (wantUV2) { extraBufs[extraCount] = RLVK.bufferSlots[a->uv2Slot].buffer;     extraOffs[extraCount] = a->uv2Offset;     extraCount++; }
        if (wantTan) { extraBufs[extraCount] = RLVK.bufferSlots[a->tangentSlot].buffer; extraOffs[extraCount] = a->tangentOffset; extraCount++; }
        vkCmdBindVertexBuffers(cmdBuffer, 4, extraCount, extraBufs, extraOffs);
    }

    // GPU skinning: bone id/weight streams whenever the shader consumes them; meshes without bone
    // buffers get the divisor-0 broadcast defaults (ids 0, weights (1,0,0,0)) like GL's attribute
    // defaults from rlSetVertexAttributeDefault
    if (!sameBinding && !wantInst && wantBones)
    {
        if (rlvkDebugFlag("RLVK_DEBUG_VAO", &s_dbgVao)) TRACELOG(RL_LOG_WARNING, "VKDBG draw vao=%u realBones=%d idSlot=%u wtSlot=%u", RLVK.State.currentVAO, (int)realBones, a->boneIdSlot, a->boneWtSlot);
        vkCmdBindVertexBuffers(cmdBuffer, 4, 2,
            (VkBuffer[]){ realBones ? RLVK.bufferSlots[a->boneIdSlot].buffer : dummy,
                          realBones ? RLVK.bufferSlots[a->boneWtSlot].buffer : dummy },
            (VkDeviceSize[]){ realBones ? (VkDeviceSize)a->boneIdOffset : 24,
                              realBones ? (VkDeviceSize)a->boneWtOffset : 28 });
    }

    // mat4 instanceTransform stream at binding 4 (offset can vary per draw: not dedup-gated)
    if (wantInst)
        vkCmdBindVertexBuffers(cmdBuffer, 4, 1,
            (VkBuffer[]){ RLVK.bufferSlots[a->instSlot].buffer }, (VkDeviceSize[]){ a->instOffset });

    // Dummy broadcasts for any remaining shader-declared attributes this layout leaves unfed
    if (!sameBinding) rlvkBindDummyAttribBuffers(cmdBuffer, vertexLayout, shader);

    rlvkFlushSet0(cmdBuffer);
    if (indexed && a->indexSlot && a->indexSlot < RLVK_MAX_BUFFER_SLOTS)
    {
        vkCmdBindIndexBuffer(cmdBuffer, RLVK.bufferSlots[a->indexSlot].buffer, 0, VK_INDEX_TYPE_UINT16);
        vk.CmdDrawIndexed(cmdBuffer, count, (instances > 0) ? instances : 1, offset, 0, 0);
    }
    else vk.CmdDraw(cmdBuffer, count, (instances > 0) ? instances : 1, offset, 0);
}
// Draw vertex array
void rlDrawVertexArray(int offset, int count)                        { rlvkDrawMesh(offset, count, false, 1); }
// Draw vertex array elements
void rlDrawVertexArrayElements(int offset, int count, const void *b)  { (void)b; rlvkDrawMesh(offset, count, true, 1); }
// Draw vertex array instanced
void rlDrawVertexArrayInstanced(int offset, int count, int instances)  { rlvkDrawMesh(offset, count, false, instances); }
// Draw vertex array elements instanced
void rlDrawVertexArrayElementsInstanced(int offset, int count, const void *b, int instances) { (void)b; rlvkDrawMesh(offset, count, true, instances); }

// Textures data management
//-----------------------------------------------------------------------------------------

// Convert image data to OpenGL texture (returns OpenGL valid Id)
unsigned int rlLoadTexture(const void *data, int width, int height, int format, int mipmapCount)
{
    if (!isGpuReady) return RLVK_INVALID_SLOT;

    u32 slot = rlvkAllocTextureSlot();
    if (slot == RLVK_INVALID_SLOT) return RLVK_INVALID_SLOT;

    rlvkTextureSlot *t = &RLVK.textureSlots[slot];
    t->width    = width;
    t->height   = height;
    t->mipCount = (mipmapCount > 0) ? mipmapCount : 1;
    t->rlFormat = format;
    VkFormat vkfmt = rlvkGetVkTextureFormat(format);
    t->format   = vkfmt;

    // Component swizzle so 1/2-channel formats sample like the GL backend: (L,L,L,1) / (L,L,L,A)
    // NOTE: Vulkan portability subset (MoltenVK) often disables imageViewFormatSwizzle.
    // To support it universally, we always expand GRAYSCALE/GRAY_ALPHA to RGBA instead of swizzling.
    VkComponentMapping swizzle = { 0 };   // all IDENTITY

    // Vulkan optimal-tiling sampled images don't support 3-channel (RGB) formats - expand to
    // 4-channel with opaque alpha. (PNGs without alpha load as R8G8B8, so this is very common.)
    const void *uploadData = data;
    void *converted = NULL;
    size_t pixels = (size_t)width*height;
    // 3-channel formats are not sampleable/renderable: remap even when there is no data to convert
    // We also expand 1/2 channel formats to 4-channels to avoid MoltenVK portability swizzle limitations.
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE)    vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA)   vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)       vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16)    vkfmt = VK_FORMAT_R16G16B16A16_SFLOAT;
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32)    vkfmt = VK_FORMAT_R32G32B32A32_SFLOAT;
    if (data != NULL)
    {
        if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE)
        {
            vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
            const unsigned char *s = (const unsigned char *)data;
            unsigned char *d = (unsigned char *)RL_MALLOC(pixels*4);
            for (size_t i = 0; i < pixels; i++) { d[i*4+0]=s[i]; d[i*4+1]=s[i]; d[i*4+2]=s[i]; d[i*4+3]=255; }
            converted = d; uploadData = d;
        }
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA)
        {
            vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
            const unsigned char *s = (const unsigned char *)data;
            unsigned char *d = (unsigned char *)RL_MALLOC(pixels*4);
            for (size_t i = 0; i < pixels; i++) { d[i*4+0]=s[i*2+0]; d[i*4+1]=s[i*2+0]; d[i*4+2]=s[i*2+0]; d[i*4+3]=s[i*2+1]; }
            converted = d; uploadData = d;
        }
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)
        {
            vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
            const unsigned char *s = (const unsigned char *)data;
            unsigned char *d = (unsigned char *)RL_MALLOC(pixels*4);
            for (size_t i = 0; i < pixels; i++) { d[i*4+0]=s[i*3+0]; d[i*4+1]=s[i*3+1]; d[i*4+2]=s[i*3+2]; d[i*4+3]=255; }
            converted = d; uploadData = d;
        }
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16)
        {
            vkfmt = VK_FORMAT_R16G16B16A16_SFLOAT;
            const unsigned short *s = (const unsigned short *)data;
            unsigned short *d = (unsigned short *)RL_MALLOC(pixels*4*sizeof(unsigned short));
            for (size_t i = 0; i < pixels; i++) { d[i*4+0]=s[i*3+0]; d[i*4+1]=s[i*3+1]; d[i*4+2]=s[i*3+2]; d[i*4+3]=0x3C00; } // half 1.0
            converted = d; uploadData = d;
        }
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32)
        {
            vkfmt = VK_FORMAT_R32G32B32A32_SFLOAT;
            const f32 *s = (const f32 *)data;
            f32 *d = (f32 *)RL_MALLOC(pixels*4*sizeof(f32));
            for (size_t i = 0; i < pixels; i++) { d[i*4+0]=s[i*3+0]; d[i*4+1]=s[i*3+1]; d[i*4+2]=s[i*3+2]; d[i*4+3]=1.0f; }
            converted = d; uploadData = d;
        }
    }
    t->format = vkfmt;

    // Device-local image, sampled + host-transfer (VK_EXT_host_image_copy: no staging buffer)
    RLVK_CHECK(vkCreateImage(RLVK.device,
        &(VkImageCreateInfo){
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType     = VK_IMAGE_TYPE_2D,
            .format        = vkfmt,
            .extent        = { (u32)width, (u32)height, 1 },
            .mipLevels     = 1,
            .arrayLayers   = 1,
            .samples       = VK_SAMPLE_COUNT_1_BIT,
            .tiling        = VK_IMAGE_TILING_OPTIMAL,
            // TRANSFER_DST: rlUpdateTexture records an in-stream vkCmdCopyBufferToImage when
            // a frame is being recorded (ordering with already-recorded commands, GL semantics)
            .usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                           | ((format < RL_PIXELFORMAT_COMPRESSED_DXT1_RGB) ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0),
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        }, RLVK_ALLOC, &t->image));

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(RLVK.device, t->image, &memReq);
    t->memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RLVK_CHECK(vkBindImageMemory(RLVK.device, t->image, t->memory, 0));

    // Staging upload (one-shot, synchronous): UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY
    VkDeviceSize uploadBytes = 0;
    if (uploadData != NULL)
    {
        if      (converted && (vkfmt == VK_FORMAT_R8G8B8A8_UNORM))      uploadBytes = (VkDeviceSize)pixels*4;    // expanded RGB8 -> RGBA8
        else if (converted && (vkfmt == VK_FORMAT_R16G16B16A16_SFLOAT)) uploadBytes = (VkDeviceSize)pixels*8;    // expanded RGB16 -> RGBA16F
        else if (converted && (vkfmt == VK_FORMAT_R32G32B32A32_SFLOAT)) uploadBytes = (VkDeviceSize)pixels*16;   // expanded RGB32 -> RGBA32F
        else uploadBytes = (VkDeviceSize)rlvkGetPixelDataSize(width, height, format);
    }
    rlvkStagingUploadImage(t->image, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, (u32)width, (u32)height, 0, 1,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, uploadData, uploadBytes);
    if (converted) RL_FREE(converted);
    t->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RLVK_CHECK(vkCreateImageView(RLVK.device,
        &(VkImageViewCreateInfo){
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = t->image,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = vkfmt,
            .components       = swizzle,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }, RLVK_ALLOC, &t->view));

    // GL defaults: point filter + REPEAT wrap (rlgl sets GL_REPEAT at texture load)
    t->minFilter = VK_FILTER_NEAREST;
    t->magFilter = VK_FILTER_NEAREST;
    t->wrapS     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    t->wrapT     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    RLVK_CHECK(vkCreateSampler(RLVK.device,
        &(VkSamplerCreateInfo){
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = t->magFilter,
            .minFilter    = t->minFilter,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = t->wrapS,
            .addressModeV = t->wrapT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .maxLod       = 1.0f,
        }, RLVK_ALLOC, &t->sampler));

    // PIVOT: no persistent descriptor set - textures are pushed per draw (rlvkPushTexture)

    return slot;
}

// Load depth texture/renderbuffer (to be attached to fbo)
// WARNING: OpenGL ES 2.0 requires GL_OES_depth_texture and WebGL requires WEBGL_depth_texture extensions
unsigned int rlLoadTextureDepth(int width, int height, bool useRenderBuffer)
{
    (void)useRenderBuffer;   // no renderbuffer concept: always a depth image
    if (!isGpuReady) return RLVK_INVALID_SLOT;
    u32 slot = rlvkAllocTextureSlot();
    if (slot == RLVK_INVALID_SLOT) return RLVK_INVALID_SLOT;
    rlvkTextureSlot *t = &RLVK.textureSlots[slot];
    t->width = width; t->height = height; t->mipCount = 1;
    t->format = VK_FORMAT_D32_SFLOAT;

    RLVK_CHECK(vkCreateImage(RLVK.device,
        &(VkImageCreateInfo){
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType     = VK_IMAGE_TYPE_2D,
            .format        = t->format,
            .extent        = { (u32)width, (u32)height, 1 },
            .mipLevels     = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling        = VK_IMAGE_TILING_OPTIMAL,
            .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                           | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        }, RLVK_ALLOC, &t->image));
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(RLVK.device, t->image, &memReq);
    t->memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RLVK_CHECK(vkBindImageMemory(RLVK.device, t->image, t->memory, 0));
    RLVK_CHECK(vkCreateImageView(RLVK.device,
        &(VkImageViewCreateInfo){
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = t->image,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = t->format,
            .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
        }, RLVK_ALLOC, &t->view));
    t->minFilter = VK_FILTER_NEAREST; t->magFilter = VK_FILTER_NEAREST;
    t->wrapS = t->wrapT = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    RLVK_CHECK(vkCreateSampler(RLVK.device,
        &(VkSamplerCreateInfo){
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = t->magFilter,
            .minFilter    = t->minFilter,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = t->wrapS,
            .addressModeV = t->wrapT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod       = 1.0f,
        }, RLVK_ALLOC, &t->sampler));
    t->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return slot;
}

// Load texture cubemap
// NOTE: Cubemap data is expected to be 6 images in a single data array (one after the other),
// expected the following convention: +X, -X, +Y, -Y, +Z, -Z
unsigned int rlLoadTextureCubemap(const void *data, int size, int format, int mipmapCount)
{
    (void)mipmapCount;
    if (!isGpuReady) return RLVK_INVALID_SLOT;
    u32 slot = rlvkAllocTextureSlot();
    if (slot == RLVK_INVALID_SLOT) return RLVK_INVALID_SLOT;
    rlvkTextureSlot *t = &RLVK.textureSlots[slot];
    t->width = size; t->height = size; t->mipCount = 1; t->rlFormat = format;
    VkFormat vkfmt = rlvkGetVkTextureFormat(format);

    // Same RGB->RGBA expansion as rlLoadTexture (3-channel formats are not sampleable)
    const void *uploadData = data;
    void *converted = NULL;
    size_t facePixels = (size_t)size*size;
    if (data && format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)
    {
        vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
        const unsigned char *src = (const unsigned char *)data;
        unsigned char *dst = (unsigned char *)RL_MALLOC(facePixels*6*4);
        for (size_t i = 0; i < facePixels*6; i++) { dst[i*4+0]=src[i*3+0]; dst[i*4+1]=src[i*3+1]; dst[i*4+2]=src[i*3+2]; dst[i*4+3]=255; }
        converted = dst; uploadData = dst;
    }
    t->format = vkfmt;
    u32 faceBytes = (u32)(facePixels*((vkfmt == VK_FORMAT_R8G8B8A8_UNORM) ? 4 : rlvkGetPixelDataSize(1, 1, format)*1));
    if (vkfmt != VK_FORMAT_R8G8B8A8_UNORM) faceBytes = (u32)rlvkGetPixelDataSize(size, size, format);

    RLVK_CHECK(vkCreateImage(RLVK.device,
        &(VkImageCreateInfo){
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            .imageType     = VK_IMAGE_TYPE_2D,
            .format        = vkfmt,
            .extent        = { (u32)size, (u32)size, 1 },
            .mipLevels     = 1,
            .arrayLayers   = 6,
            .samples       = VK_SAMPLE_COUNT_1_BIT,
            .tiling        = VK_IMAGE_TILING_OPTIMAL,
            .usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        }, RLVK_ALLOC, &t->image));
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(RLVK.device, t->image, &memReq);
    t->memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RLVK_CHECK(vkBindImageMemory(RLVK.device, t->image, t->memory, 0));

    // Staging upload: rlgl layout stores the 6 faces consecutively (+X, -X, +Y, -Y, +Z, -Z),
    // which matches the tightly packed multi-layer copy exactly
    rlvkStagingUploadImage(t->image, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, (u32)size, (u32)size, 0, 6,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        uploadData, uploadData? (VkDeviceSize)faceBytes*6 : 0);
    if (converted) RL_FREE(converted);
    t->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RLVK_CHECK(vkCreateImageView(RLVK.device,
        &(VkImageViewCreateInfo){
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = t->image,
            .viewType         = VK_IMAGE_VIEW_TYPE_CUBE,
            .format           = vkfmt,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 },
        }, RLVK_ALLOC, &t->view));

    t->minFilter = VK_FILTER_LINEAR;   // rlgl sets GL_LINEAR for cubemaps
    t->magFilter = VK_FILTER_LINEAR;
    t->wrapS = t->wrapT = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    RLVK_CHECK(vkCreateSampler(RLVK.device,
        &(VkSamplerCreateInfo){
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = t->magFilter,
            .minFilter    = t->minFilter,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = t->wrapS,
            .addressModeV = t->wrapT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod       = 1.0f,
        }, RLVK_ALLOC, &t->sampler));
    TRACELOG(RL_LOG_INFO, "RLVK: [ID %u] cubemap loaded (%dx%d, 6 faces)", slot, size, size);
    return slot;
}
// Update already loaded texture in GPU with new data
// WARNING: Not possible to know safely if internal texture format is the expected one...
void rlUpdateTexture(unsigned int id, int x, int y, int w, int h, int format, const void *data)
{
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS || !data) return;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->image) return;

    // Match the load-time RGB->RGBA expansion (3-channel formats are not sampleable)
    const void *uploadData = data;
    void *converted = NULL;
    int uploadFormat = format;
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE)
    {
        size_t pixels = (size_t)w*h;
        const unsigned char *src = (const unsigned char *)data;
        unsigned char *dst = (unsigned char *)RL_MALLOC(pixels*4);
        for (size_t i = 0; i < pixels; i++) { dst[i*4+0]=src[i]; dst[i*4+1]=src[i]; dst[i*4+2]=src[i]; dst[i*4+3]=255; }
        converted = dst; uploadData = dst;
        uploadFormat = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    }
    else if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA)
    {
        size_t pixels = (size_t)w*h;
        const unsigned char *src = (const unsigned char *)data;
        unsigned char *dst = (unsigned char *)RL_MALLOC(pixels*4);
        for (size_t i = 0; i < pixels; i++) { dst[i*4+0]=src[i*2+0]; dst[i*4+1]=src[i*2+0]; dst[i*4+2]=src[i*2+0]; dst[i*4+3]=src[i*2+1]; }
        converted = dst; uploadData = dst;
        uploadFormat = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    }
    else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)
    {
        size_t pixels = (size_t)w*h;
        const unsigned char *src = (const unsigned char *)data;
        unsigned char *dst = (unsigned char *)RL_MALLOC(pixels*4);
        for (size_t i = 0; i < pixels; i++) { dst[i*4+0]=src[i*3+0]; dst[i*4+1]=src[i*3+1]; dst[i*4+2]=src[i*3+2]; dst[i*4+3]=255; }
        converted = dst; uploadData = dst;
        uploadFormat = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    }

    // GL runs glTexSubImage2D IN ORDER with prior commands; an immediate host copy would run
    // BEFORE a recording frame's commands. Record an arena-staged buffer-to-image copy instead.
    VkDeviceSize bytes = (VkDeviceSize)rlvkGetPixelDataSize(w, h, uploadFormat);
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
    VkDeviceSize stagingOff = (RLVK.arenaOffset[frameIndex] + 15) & ~(VkDeviceSize)15;
    if (RLVK.frameActive && stagingOff + bytes <= arena->sizeBytes)
    {
        VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
        memcpy((char *)arena->mapped + stagingOff, uploadData, (size_t)bytes);
        RLVK.arenaOffset[frameIndex] = stagingOff + bytes;
        RLVK.arenaWanted[frameIndex] += bytes + 16;

        // If an FBO scope is open, close it (its resume machinery restores the swapchain
        // scope), record the copy there, and reopen it afterwards - content persists (LOAD)
        u32 openFb = RLVK.scope.fbSlot;
        if (openFb) rlDisableFramebuffer();

        vkCmdEndRenderPass(cmdBuffer);
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                // Access mask matches the OLD layout (SHADER_READ_ONLY): prior use is
                // sampling; any attachment write was already made visible by the FBO
                // scope's own transition out of COLOR_ATTACHMENT
                .srcStageMask     = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .srcAccessMask    = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_COPY_BIT,
                .dstAccessMask    = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout        = t->currentLayout,
                .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .image            = t->image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            },
        });
        vkCmdCopyBufferToImage(cmdBuffer, arena->buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
            &(VkBufferImageCopy){
                .bufferOffset     = stagingOff,
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .imageOffset      = { x, y, 0 },
                .imageExtent      = { (u32)w, (u32)h, 1 },
            });
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask     = VK_PIPELINE_STAGE_2_COPY_BIT,
                .srcAccessMask    = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                .dstAccessMask    = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .image            = t->image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            },
        });
        t->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        rlvkResumeSwapchainScope(cmdBuffer);

        if (openFb) rlEnableFramebuffer(openFb);
        if (converted) RL_FREE(converted);
        return;
    }

    // No frame being recorded (init-time upload): staging copy after a full wait, keeping
    // the texture in SHADER_READ_ONLY around the update
    rlvkWaitInFlightFrames();
    rlvkStagingUploadImage(t->image, VK_IMAGE_ASPECT_COLOR_BIT, x, y, (u32)w, (u32)h, 0, 1,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        uploadData, (VkDeviceSize)rlvkGetPixelDataSize(w, h, uploadFormat));
    if (converted) RL_FREE(converted);
}

// Get OpenGL internal formats and data type from raylib PixelFormat
void rlGetGlTextureFormats(int format, unsigned int *glInternal, unsigned int *glFormat, unsigned int *glType)
{
    // Compatibility lookup. Internally never consumed; preserved so callers inspecting these
    // values for documentation/logging continue to work.
    *glInternal = 0; *glFormat = 0; *glType = 0;
    switch (format)
    {
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:    *glInternal = 0x8229; *glFormat = 0x1903; *glType = 0x1401; break;
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:   *glInternal = 0x822B; *glFormat = 0x8227; *glType = 0x1401; break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:       *glInternal = 0x8D62; *glFormat = 0x1907; *glType = 0x8363; break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8:       *glInternal = 0x8051; *glFormat = 0x1907; *glType = 0x1401; break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:     *glInternal = 0x8058; *glFormat = 0x1908; *glType = 0x1401; break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32:          *glInternal = 0x822E; *glFormat = 0x1903; *glType = 0x1406; break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32: *glInternal = 0x8814; *glFormat = 0x1908; *glType = 0x1406; break;
        default: break;
    }
}

// Get name string for pixel format
const char *rlGetPixelFormatName(unsigned int format)
{
    switch (format)
    {
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:    return "GRAYSCALE";
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:   return "GRAY_ALPHA";
        case RL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:       return "R5G6B5";
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8:       return "R8G8B8";
        case RL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:     return "R5G5B5A1";
        case RL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:     return "R4G4B4A4";
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:     return "R8G8B8A8";
        case RL_PIXELFORMAT_UNCOMPRESSED_R32:          return "R32";
        case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32:    return "R32G32B32";
        case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32: return "R32G32B32A32";
        case RL_PIXELFORMAT_UNCOMPRESSED_R16:          return "R16";
        case RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16:    return "R16G16B16";
        case RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16: return "R16G16B16A16";
        case RL_PIXELFORMAT_COMPRESSED_DXT1_RGB:       return "DXT1_RGB";
        case RL_PIXELFORMAT_COMPRESSED_DXT1_RGBA:      return "DXT1_RGBA";
        case RL_PIXELFORMAT_COMPRESSED_DXT3_RGBA:      return "DXT3_RGBA";
        case RL_PIXELFORMAT_COMPRESSED_DXT5_RGBA:      return "DXT5_RGBA";
        case RL_PIXELFORMAT_COMPRESSED_ETC1_RGB:       return "ETC1_RGB";
        case RL_PIXELFORMAT_COMPRESSED_ETC2_RGB:       return "ETC2_RGB";
        case RL_PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA:  return "ETC2_EAC_RGBA";
        case RL_PIXELFORMAT_COMPRESSED_PVRT_RGB:       return "PVRT_RGB";
        case RL_PIXELFORMAT_COMPRESSED_PVRT_RGBA:      return "PVRT_RGBA";
        case RL_PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA:  return "ASTC_4x4_RGBA";
        case RL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA:  return "ASTC_8x8_RGBA";
        default: return "UNKNOWN";
    }
}

// Unload texture from GPU memory
void rlUnloadTexture(unsigned int id)
{
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS) return;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->inUse) return;

    // A recorded (or executing) command buffer may still reference the texture: defer
    rlvkDeferDestroy(VK_NULL_HANDLE, t->image, t->view, t->sampler, t->memory, VK_NULL_HANDLE);
    t->image = VK_NULL_HANDLE;
    t->view = VK_NULL_HANDLE;
    t->sampler = VK_NULL_HANDLE;
    t->memory = VK_NULL_HANDLE;
    t->inUse = false;
}

// Generate a full mip chain: read level 0 back, recreate the image with mipLevels, box-filter
// each level on the CPU (matches GL glGenerateMipmap's conventional box filter), upload via host
// image copy. RGBA8 only (every uncompressed load lands there via the RGB->RGBA expansion).
void rlGenTextureMipmaps(unsigned int id, int w, int h, int format, int *mipmaps)
{
    if (mipmaps) *mipmaps = 1;
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS) return;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->image || t->format != VK_FORMAT_R8G8B8A8_UNORM) return;
    (void)format;

    // GL order: glGenerateMipmap reads the texture as already-rendered commands left it.
    // Execute everything recorded so far (a render texture may have been drawn THIS frame),
    // then drain in-flight work before the host-side level-0 readback below
    rlvkFlushFrame();
    rlvkWaitInFlightFrames();

    // Read back level 0 (staging, synchronous; layout preserved)
    unsigned char *level0 = (unsigned char *)RL_MALLOC((size_t)w*h*4);
    rlvkStagingReadImage(t->image, VK_IMAGE_ASPECT_COLOR_BIT, (u32)w, (u32)h,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, level0, (VkDeviceSize)w*h*4);

    int mipCount = 1;
    { int mw = w, mh = h; while (mw > 1 || mh > 1) { mw = (mw > 1) ? mw/2 : 1; mh = (mh > 1) ? mh/2 : 1; mipCount++; } }

    // Recreate the image with the full chain. The old image may be referenced by the frame
    // being recorded (mipmap generation right after drawing with the texture): defer
    rlvkDeferDestroy(VK_NULL_HANDLE, t->image, t->view, VK_NULL_HANDLE, t->memory, VK_NULL_HANDLE);
    RLVK_CHECK(vkCreateImage(RLVK.device,
        &(VkImageCreateInfo){
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType     = VK_IMAGE_TYPE_2D,
            .format        = t->format,
            .extent        = { (u32)w, (u32)h, 1 },
            .mipLevels     = (u32)mipCount,
            .arrayLayers   = 1,
            .samples       = VK_SAMPLE_COUNT_1_BIT,
            .tiling        = VK_IMAGE_TILING_OPTIMAL,
            // TRANSFER_DST: render-texture attachments can be rlUpdateTexture'd mid-frame too
            // (game_of_life seeds its world texture that way)
            .usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        }, RLVK_ALLOC, &t->image));
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(RLVK.device, t->image, &memReq);
    t->memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RLVK_CHECK(vkBindImageMemory(RLVK.device, t->image, t->memory, 0));
    // Box-filter the whole chain on the CPU first, packing every level into one staging
    // buffer, then upload all levels in a single one-shot submission
    size_t totalBytes = 0;
    { int mw = w, mh = h; for (int l = 0; l < mipCount; l++) { totalBytes += (size_t)mw*mh*4; mw = (mw > 1)? mw/2 : 1; mh = (mh > 1)? mh/2 : 1; } }
    unsigned char *chain = (unsigned char *)RL_MALLOC(totalBytes);
    {
        unsigned char *src = level0;
        unsigned char *wPtr = chain;
        int srcWidth = w, srcHeight = h;
        for (int level = 0; level < mipCount; level++)
        {
            memcpy(wPtr, src, (size_t)srcWidth*srcHeight*4);
            wPtr += (size_t)srcWidth*srcHeight*4;
            if (level == mipCount - 1) break;
            int dstWidth = (srcWidth > 1) ? srcWidth/2 : 1, dstHeight = (srcHeight > 1) ? srcHeight/2 : 1;
            unsigned char *dst = (unsigned char *)RL_MALLOC((size_t)dstWidth*dstHeight*4);
            for (int y = 0; y < dstHeight; y++)
            {
                int y0 = y*2, y1 = (srcHeight > 1) ? y*2 + 1 : y0;
                for (int x = 0; x < dstWidth; x++)
                {
                    int x0 = x*2, x1 = (srcWidth > 1) ? x*2 + 1 : x0;
                    for (int c = 0; c < 4; c++)
                    {
                        int sum = src[(y0*srcWidth + x0)*4 + c] + src[(y0*srcWidth + x1)*4 + c]
                                + src[(y1*srcWidth + x0)*4 + c] + src[(y1*srcWidth + x1)*4 + c];
                        dst[(y*dstWidth + x)*4 + c] = (unsigned char)((sum + 2)/4);
                    }
                }
            }
            RL_FREE(src);
            src = dst; srcWidth = dstWidth; srcHeight = dstHeight;
        }
        RL_FREE(src);
    }

    // Staging upload of the packed chain: one buffer, one region per level
    {
        VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE; void *map = NULL;
        RLVK_CHECK(vkCreateBuffer(RLVK.device,
            &(VkBufferCreateInfo){ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = totalBytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE },
            RLVK_ALLOC, &staging));
        VkMemoryRequirements smemReq; vkGetBufferMemoryRequirements(RLVK.device, staging, &smemReq);
        stagingMem = rlvkAllocMemory(smemReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(RLVK.device, staging, stagingMem, 0);
        vkMapMemory(RLVK.device, stagingMem, 0, totalBytes, 0, &map);
        memcpy(map, chain, totalBytes);
        vkUnmapMemory(RLVK.device, stagingMem);

        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer oneShot = rlvkOneShotBegin(&pool);
        if (oneShot != VK_NULL_HANDLE)
        {
            rlvkCmdTransitionImage(oneShot, t->image, VK_IMAGE_ASPECT_COLOR_BIT, 0, (u32)mipCount, 0, 1,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkDeviceSize levelOffset = 0;
            int mw = w, mh = h;
            for (int level = 0; level < mipCount; level++)
            {
                vkCmdCopyBufferToImage(oneShot, staging, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                    &(VkBufferImageCopy){
                        .bufferOffset     = levelOffset,
                        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, (u32)level, 0, 1 },
                        .imageExtent      = { (u32)mw, (u32)mh, 1 },
                    });
                levelOffset += (VkDeviceSize)mw*mh*4;
                mw = (mw > 1)? mw/2 : 1; mh = (mh > 1)? mh/2 : 1;
            }
            rlvkCmdTransitionImage(oneShot, t->image, VK_IMAGE_ASPECT_COLOR_BIT, 0, (u32)mipCount, 0, 1,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            rlvkOneShotEnd(pool, oneShot);
        }
        vkDestroyBuffer(RLVK.device, staging, RLVK_ALLOC);
        vkFreeMemory(RLVK.device, stagingMem, RLVK_ALLOC);
    }
    RL_FREE(chain);

    RLVK_CHECK(vkCreateImageView(RLVK.device,
        &(VkImageViewCreateInfo){
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = t->image,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = t->format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, (u32)mipCount, 0, 1 },
        }, RLVK_ALLOC, &t->view));

    // Sampler maxLod must cover the new chain (filters unchanged until SetTextureFilter)
    t->mipCount = mipCount;
    t->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    rlvkDeferDestroy(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, t->sampler, VK_NULL_HANDLE, VK_NULL_HANDLE);
    RLVK_CHECK(vkCreateSampler(RLVK.device,
        &(VkSamplerCreateInfo){
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = t->magFilter,
            .minFilter    = t->minFilter,
            .mipmapMode   = t->mipMode,
            .addressModeU = t->wrapS,
            .addressModeV = t->wrapT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .maxLod       = (f32)mipCount,
        }, RLVK_ALLOC, &t->sampler));

    if (mipmaps) *mipmaps = mipCount;
    TRACELOG(RL_LOG_INFO, "RLVK: [ID %u] mipmaps generated (%d levels)", id, mipCount);
}

// Read texture pixel data
void *rlReadTexturePixels(unsigned int id, int width, int height, int format)
{
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS) return NULL;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->image) return NULL;

    // The texture was loaded as RGBA when the rl format was RGB (3-channel expansion), so read
    // back 4 channels and repack when the caller expects RGB
    bool expanded = (format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8) && (t->format == VK_FORMAT_R8G8B8A8_UNORM);
    size_t pixels = (size_t)width*height;
    size_t gpuBytes = expanded ? pixels*4 : (size_t)rlvkGetPixelDataSize(width, height, format);
    void *gpuData = RL_MALLOC(gpuBytes);

    // GL order: glGetTexImage sees the texture as already-issued commands left it. Execute the
    // current recording first (a render texture read back mid-frame was returning zeros), then
    // drain in-flight frames before the staging read-back
    rlvkFlushFrame();
    rlvkWaitInFlightFrames();
    rlvkStagingReadImage(t->image, VK_IMAGE_ASPECT_COLOR_BIT, (u32)width, (u32)height,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, gpuData, (VkDeviceSize)gpuBytes);

    if (!expanded) return gpuData;

    // Repack RGBA -> RGB for callers that asked for the original 3-channel format
    unsigned char *rgb = (unsigned char *)RL_MALLOC(pixels*3);
    const unsigned char *src = (const unsigned char *)gpuData;
    for (size_t i = 0; i < pixels; i++) { rgb[i*3+0]=src[i*4+0]; rgb[i*3+1]=src[i*4+1]; rgb[i*3+2]=src[i*4+2]; }
    RL_FREE(gpuData);
    return rgb;
}

// Read the current color image back to CPU (TakeScreenshot / capture hook): ends the render
// scope, copies to a host buffer, presents, waits, returns RGBA; marks the frame consumed so
// rlvkPresent does not submit it again
unsigned char *rlReadScreenPixels(int width, int height)
{
    (void)width; (void)height;
    if (!isGpuReady || !RLVK.swapchain) return NULL;

    // No frame recording (TakeScreenshot after EndDrawing, the common raylib pattern):
    // GL's glReadPixels there sees the just-presented frame, which still lives in the
    // PREVIOUS slot's intermediate image (color STOREd, left in TRANSFER_SRC by the flip
    // blit). Read THAT - opening a fresh frame here would capture an empty clear instead.
    if (!RLVK.frameActive)
    {
        if (RLVK.frameCounter == 0) return NULL;   // nothing was ever drawn/presented
        u32 prev = (u32)((RLVK.frameCounter + RLVK_FRAME_INDEX_COUNT - 1) % RLVK_FRAME_INDEX_COUNT);
        vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[prev], VK_TRUE, UINT64_MAX);
        u32 w = RLVK.swapchainExtent.width, h = RLVK.swapchainExtent.height;
        unsigned char *rows = (unsigned char *)RL_MALLOC((size_t)w*h*4);
        rlvkStagingReadImage(RLVK.interImage[prev], VK_IMAGE_ASPECT_COLOR_BIT, w, h,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rows, (VkDeviceSize)w*h*4);
        // The intermediate is unmirrored (GL memory orientation): flip rows for the
        // top-down BGRA->RGBA conversion below, matching the swapchain-path output
        unsigned char *out = (unsigned char *)RL_MALLOC((size_t)w*h*4);
        for (u32 y = 0; y < h; y++)
            memcpy(out + (size_t)y*w*4, rows + (size_t)(h - 1 - y)*w*4, (size_t)w*4);
        RL_FREE(rows);
        // Return RGBA with opaque alpha, swapping R/B when the target format is BGRA -
        // identical conversion to the in-frame path below
        bool bgraPrev = (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM) || (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB);
        for (size_t i = 0; i < (size_t)w*h; i++)
        {
            if (bgraPrev) { unsigned char b = out[i*4+0]; out[i*4+0] = out[i*4+2]; out[i*4+2] = b; }
            out[i*4+3] = 255;
        }
        return out;
    }

    u32 frameIndex         = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer  = RLVK.cmdBuffers[frameIndex];
    u32 imageIndex = RLVK.currentImageIndex;
    VkImage img         = RLVK.swapchainImages[imageIndex];
    u32 w = RLVK.swapchainExtent.width, h = RLVK.swapchainExtent.height;
    VkDeviceSize sizeBytes = (VkDeviceSize)w*h*4;

    // Transient host-visible readback buffer
    VkBuffer rbBuf = VK_NULL_HANDLE; VkDeviceMemory rbMem = VK_NULL_HANDLE; void *rbMapped = NULL;
    RLVK_CHECK(vkCreateBuffer(RLVK.device, &(VkBufferCreateInfo){
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeBytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE },
        RLVK_ALLOC, &rbBuf));
    VkMemoryRequirements memReq; vkGetBufferMemoryRequirements(RLVK.device, rbBuf, &memReq);
    rbMem = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(RLVK.device, rbBuf, rbMem, 0);
    vkMapMemory(RLVK.device, rbMem, 0, sizeBytes, 0, &rbMapped);

    vkCmdEndRenderPass(cmdBuffer);
    rlvkFinishSwapchainImage(cmdBuffer);   // flip-blit the frame into the swapchain

    // COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .image = img, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } } });

    vk.CmdCopyImageToBuffer(cmdBuffer, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rbBuf, 1, &(VkBufferImageCopy){
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { w, h, 1 } });

    // copy write -> host read ; image TRANSFER_SRC -> PRESENT_SRC
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1, .pMemoryBarriers = &(VkMemoryBarrier2){
            VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT, .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT },
        .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = img, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } } });

    vk.EndCommandBuffer(cmdBuffer);

    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount   = RLVK.acquireWaited? 0u : 1u,    // a mid-frame flush may have consumed it
        .pWaitSemaphoreInfos      = &(VkSemaphoreSubmitInfo){
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = RLVK.acquireSemaphores[frameIndex],
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT },
        .commandBufferInfoCount   = 1, .pCommandBufferInfos = &(VkCommandBufferSubmitInfo){
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmdBuffer },
        .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &(VkSemaphoreSubmitInfo){
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = RLVK.renderSemaphores[imageIndex],
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT } },
        RLVK.frameFences[frameIndex]);
    RLVK.acquireWaited = true;

    vk.QueuePresentKHR(RLVK.graphicsQueue, &(VkPresentInfoKHR){
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &RLVK.renderSemaphores[imageIndex],
        .swapchainCount = 1, .pSwapchains = &RLVK.swapchain, .pImageIndices = &imageIndex });

    vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[frameIndex], VK_TRUE, UINT64_MAX);

    // Return RGBA (raylib PIXELFORMAT_UNCOMPRESSED_R8G8B8A8). Swap R/B if the swapchain is BGRA.
    unsigned char *out = (unsigned char *)RL_MALLOC((size_t)w*h*4);
    const unsigned char *src = (const unsigned char *)rbMapped;
    bool bgra = (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM) || (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB);
    for (size_t i = 0; i < (size_t)w*h; i++)
    {
        out[i*4 + 0] = bgra ? src[i*4 + 2] : src[i*4 + 0];
        out[i*4 + 1] = src[i*4 + 1];
        out[i*4 + 2] = bgra ? src[i*4 + 0] : src[i*4 + 2];
        out[i*4 + 3] = 255;   // opaque, like rlgl's rlReadScreenPixels (backbuffer alpha is not retrieved)
    }

    vkUnmapMemory(RLVK.device, rbMem);
    vkDestroyBuffer(RLVK.device, rbBuf, RLVK_ALLOC);
    vkFreeMemory(RLVK.device, rbMem, RLVK_ALLOC);

    RLVK.frameActive   = false;
    RLVK.frameConsumed = true;
    if (rlvkDebugFlag("RLVK_DEBUG_FBO", &s_dbgFbo)) TRACELOG(RL_LOG_WARNING, "VKDBG frameREADBACK fc=%llu", (ull)RLVK.frameCounter);
    return out;
}

// Framebuffer management (fbo)
//-----------------------------------------------------------------------------------------

// Load a framebuffer to be used for rendering
// NOTE: No textures attached
unsigned int rlLoadFramebuffer(void)
{
    u32 slot = rlvkAllocFramebufferSlot();
    if (slot == RLVK_INVALID_SLOT) return RLVK_INVALID_SLOT;
    return slot;
}

// Attach color buffer texture to a framebuffer object (unloads previous attachment)
// NOTE: Attach type: 0-Color, 1-Depth renderbuffer, 2-Depth texture
void rlFramebufferAttach(unsigned int fb, unsigned int texId, int attachType, int texType, int mipLevel)
{
    if (fb == 0 || fb >= RLVK_MAX_FRAMEBUFFER_SLOTS) return;
    rlvkFramebufferSlot *f = &RLVK.fbSlots[fb];
    (void)texType; (void)mipLevel;

    if (attachType >= RL_ATTACHMENT_COLOR_CHANNEL0 && attachType <= RL_ATTACHMENT_COLOR_CHANNEL7)
    {
        f->colorTextures[attachType] = texId;
        if ((u32)(attachType + 1) > f->colorCount) f->colorCount = attachType + 1;
    }
    else if (attachType == RL_ATTACHMENT_DEPTH)   { f->depthTexture   = texId; f->hasDepth   = true; }
    else if (attachType == RL_ATTACHMENT_STENCIL) { f->stencilTexture = texId; f->hasStencil = true; }
    // No VkFramebuffer object created; everything is inferred at vkCmdBeginRendering time.
}

// Verify render texture is complete
bool rlFramebufferComplete(unsigned int fb)
{
    if (fb == 0 || fb >= RLVK_MAX_FRAMEBUFFER_SLOTS) return false;
    rlvkFramebufferSlot *f = &RLVK.fbSlots[fb];
    rlDisableFramebuffer();   // rlgl unbinds the framebuffer after the completeness check
    return f->inUse && (f->colorCount > 0 || f->hasDepth);
}

// Unload framebuffer from GPU memory
// NOTE: All attached textures/cubemaps/renderbuffers are also deleted
void rlUnloadFramebuffer(unsigned int fb)
{
    if (fb == 0 || fb >= RLVK_MAX_FRAMEBUFFER_SLOTS) return;
    // The depth/stencil attachment is deleted with the framebuffer, exactly like the GL
    // backend (which queries GL_DEPTH_ATTACHMENT and deletes it): UnloadRenderTexture()
    // relies on this - color attachments stay caller-owned
    rlvkFramebufferSlot *slot = &RLVK.fbSlots[fb];
    if (slot->hasDepth && slot->depthTexture) rlUnloadTexture(slot->depthTexture);
    if (slot->hasStencil && slot->stencilTexture && (slot->stencilTexture != slot->depthTexture)) rlUnloadTexture(slot->stencilTexture);
    memset(slot, 0, sizeof(rlvkFramebufferSlot));
}

// Copy framebuffer pixel data to internal buffer
void rlCopyFramebuffer(int x, int y, int w, int h, int f, void *p) { (void)x;(void)y;(void)w;(void)h;(void)f;(void)p; }
// Resize internal framebuffer
void rlResizeFramebuffer(int w, int h)                              { (void)w;(void)h; }

// Shaders management (SPIR-V shader modules consumed by the cached-pipeline draw path)
//-----------------------------------------------------------------------------------------

// Load (compile) shader and return shader id
// Stage-compilation model: GL compiles stages separately and links them later. rlvk defers
// everything to the program step - rlLoadShader just stashes a copy of the GLSL source in
// the slot; rlLoadShaderProgramCompute (and, later, rlLoadShaderProgramEx) consumes it.
unsigned int rlLoadShader(const char *code, int type)
{
    if (!isGpuReady) { TRACELOG(RL_LOG_ERROR, "rlLoadShader failed: isGpuReady is false"); return RLVK_INVALID_SLOT; }
    if (!code) { TRACELOG(RL_LOG_ERROR, "rlLoadShader failed: code is NULL"); return RLVK_INVALID_SLOT; }
    u32 slot = rlvkAllocShaderSlot();
    if (slot == RLVK_INVALID_SLOT) { TRACELOG(RL_LOG_ERROR, "rlLoadShader failed: rlvkAllocShaderSlot returned 0"); return RLVK_INVALID_SLOT; }

    rlvkShaderSlot *s = &RLVK.shaderSlots[slot];
    size_t len = strlen(code);
    s->pendingCode = (char *)RL_MALLOC(len + 1);
    memcpy(s->pendingCode, code, len + 1);
    s->pendingType = type;
    return slot;
}

// Load shader program from code strings
// NOTE: If shader string is NULL, using default vertex/fragment shaders
unsigned int rlLoadShaderProgram(const char *vsCode, const char *fsCode)
{
    if (!isGpuReady) return 0;
    // shaderc loads lazily on the first custom shader: apps using only the default shader
    // never pay its module footprint or load time
    if (!RLVK.shadercCompiler && !rlvkLoadShaderc())
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: custom shaders need shaderc_shared.dll (not found) - using default shader");
        return RLVK.defaultShaderSlot;
    }
    if (!vsCode) vsCode = rlvkDefaultVShaderCode;
    if (!fsCode) fsCode = rlvkDefaultFShaderCode;

    // Rename identifiers Vulkan GLSL reserves (GL 330 allows them)
    char *sanVs = rlvkSanitizeGlsl(vsCode);
    char *sanFs = rlvkSanitizeGlsl(fsCode);
    if (sanVs) vsCode = sanVs;
    if (sanFs) fsCode = sanFs;

    // gl_FragCoord needs NO wrapper: every scope rasterizes in GL's memory orientation
    // (positive viewport, final flip at present), so Vulkan's framebuffer-row gl_FragCoord.y
    // equals GL's bottom-left window y numerically.

    u32 *vsSpv = NULL, *fsSpv = NULL; size_t vsWords = 0, fsWords = 0;
    bool vsOk = rlvkCompileGlsl(vsCode, 0, &vsSpv, &vsWords);
    bool fsOk = vsOk && rlvkCompileGlsl(fsCode, 1, &fsSpv, &fsWords);
    if (sanVs) RL_FREE(sanVs);
    if (sanFs) RL_FREE(sanFs);
    if (!fsOk) { if (vsSpv) RL_FREE(vsSpv); return RLVK.defaultShaderSlot; }

    // Force raylib's canonical attribute locations (the Vulkan glBindAttribLocation), reflect the
    // VS, then rewrite the FS's input locations to match the VS outputs BY NAME (GL link rules)
    rlvkCanonicalizeInputLocations(vsSpv, vsWords);
    rlvkSpvReflection vsRef, fsRef;
    rlvkReflectSpv(vsSpv, vsWords, &vsRef);
    rlvkMatchStageInterface(&fsSpv, &fsWords, &vsRef);
    rlvkReflectSpv(fsSpv, fsWords, &fsRef);

    // Compile both stages to VkShaderModules; pipelines consume them at creation time
    VkShaderModule vsMod = VK_NULL_HANDLE, fsMod = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smi.codeSize = vsWords*4; smi.pCode = vsSpv; VkResult r = vkCreateShaderModule(RLVK.device, &smi, RLVK_ALLOC, &vsMod);
    smi.codeSize = fsWords*4; smi.pCode = fsSpv; if (r == VK_SUCCESS) r = vkCreateShaderModule(RLVK.device, &smi, RLVK_ALLOC, &fsMod);

    RL_FREE(vsSpv); RL_FREE(fsSpv);
    if (r != VK_SUCCESS)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: vkCreateShaderModule (custom shader) => %d", (int)r);
        return RLVK.defaultShaderSlot;
    }

    u32 slot = rlvkAllocShaderSlot();
    if (slot == RLVK_INVALID_SLOT) return RLVK.defaultShaderSlot;
    rlvkShaderSlot *shader = &RLVK.shaderSlots[slot];
    shader->vertMod = vsMod;
    shader->fragMod = fsMod;
    for (int i = 0; i < RL_MAX_SHADER_LOCATIONS; i++) shader->locs[i] = -1;
    for (int i = 0; i < RLVK_MAX_TEXTURE_UNITS; i++) { shader->bindingUnit[i] = i; shader->bindingTexture[i] = 0; }
    for (int i = 0; i < RLVK_ATTRIB_COUNT; i++) shader->attribLocs[i] = -1;

    // Merge the two stages' reflections into one uniform table ("location" = table index)
    shader->uniforms = (rlvkUniform *)RL_CALLOC(RLVK_MAX_SHADER_UNIFORMS, sizeof(rlvkUniform));
    shader->uniformCount = 0;
    for (int stage = 0; stage < 2; stage++)
    {
        rlvkSpvReflection *ref = stage ? &fsRef : &vsRef;
        for (int m = 0; m < ref->memberCount; m++)
        {
            int found = -1;
            for (int u = 0; u < shader->uniformCount; u++)
                if (strcmp(shader->uniforms[u].name, ref->members[m].name) == 0) { found = u; break; }
            if (found < 0 && shader->uniformCount < RLVK_MAX_SHADER_UNIFORMS)
            {
                found = shader->uniformCount++;
                strncpy(shader->uniforms[found].name, ref->members[m].name, 63);
                shader->uniforms[found].vsOffset = -1; shader->uniforms[found].fsOffset = -1; shader->uniforms[found].samplerBinding = -1;
            }
            if (found >= 0) { if (stage) shader->uniforms[found].fsOffset = (int)ref->members[m].offset; else shader->uniforms[found].vsOffset = (int)ref->members[m].offset; }
        }
        for (int sm = 0; sm < ref->samplerCount; sm++)
        {
            int found = -1;
            for (int u = 0; u < shader->uniformCount; u++)
                if (strcmp(shader->uniforms[u].name, ref->samplers[sm].name) == 0) { found = u; break; }
            if (found < 0 && shader->uniformCount < RLVK_MAX_SHADER_UNIFORMS)
            {
                found = shader->uniformCount++;
                strncpy(shader->uniforms[found].name, ref->samplers[sm].name, 63);
                shader->uniforms[found].vsOffset = -1; shader->uniforms[found].fsOffset = -1; shader->uniforms[found].samplerBinding = -1;
            }
            if (found >= 0) shader->uniforms[found].samplerBinding = ref->samplers[sm].binding;
        }
    }

    // Vertex attribute locations by canonical raylib name
    for (int a = 0; a < vsRef.inputCount; a++)
    {
        int c = rlvkCanonicalAttribIndex(vsRef.inputs[a].name);
        if (c >= 0) shader->attribLocs[c] = vsRef.inputs[a].location;
    }

    // Per-stage default-uniform-block staging
    shader->vsBlockSize = vsRef.hasBlock ? vsRef.blockSize : 0;
    shader->fsBlockSize = fsRef.hasBlock ? fsRef.blockSize : 0;
    shader->vsStage = shader->vsBlockSize ? (unsigned char *)RL_CALLOC(1, shader->vsBlockSize) : NULL;
    shader->fsStage = shader->fsBlockSize ? (unsigned char *)RL_CALLOC(1, shader->fsBlockSize) : NULL;
    shader->usesUbo = true;

    TRACELOG(RL_LOG_INFO, "RLVK: [ID %u] shader program compiled (%d uniforms, VS block %uB at %u, FS block %uB at %u)",
        slot, shader->uniformCount, shader->vsBlockSize, vsRef.blockBinding, shader->fsBlockSize, fsRef.blockBinding);
    return slot;
}


// Load shader program from already loaded shader ids
unsigned int rlLoadShaderProgramEx(unsigned int vsId, unsigned int fsId)        { (void)vsId;(void)fsId; return RLVK_INVALID_SLOT; }

// Lazily create the fixed compute set-0 layout + pipeline layout + per-frame pools
static bool rlvkInitComputeLayout(void)
{
    if (RLVK.computePipelineLayout != VK_NULL_HANDLE) return true;

    // NOTE: NO storage-image bindings. Empirically (raw-Vulkan bisect on MoltenVK 1.2.11 /
    // Intel Iris 6000): merely DECLARING storage-image bindings 8..11 in this layout makes
    // the UBO at binding 14 read as zeros in the shader, even when the images are never
    // written or statically used. rlBindImageTexture is a recorded no-op until a dedicated
    // storage-image path exists (textures lack STORAGE usage anyway); when that lands, put
    // the images in their own descriptor SET rather than re-adding them here.
    VkDescriptorSetLayoutBinding bindings[11];
    for (u32 b = 0; b < 8; b++) bindings[b] = (VkDescriptorSetLayoutBinding){ .binding = b, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    bindings[8]  = (VkDescriptorSetLayoutBinding){ .binding = 12, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    bindings[9]  = (VkDescriptorSetLayoutBinding){ .binding = 13, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    bindings[10] = (VkDescriptorSetLayoutBinding){ .binding = 14, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    RLVK_CHECK(vkCreateDescriptorSetLayout(RLVK.device,
        &(VkDescriptorSetLayoutCreateInfo){
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 11, .pBindings = bindings,
        }, RLVK_ALLOC, &RLVK.computeSetLayout));
    RLVK_CHECK(vkCreatePipelineLayout(RLVK.device,
        &(VkPipelineLayoutCreateInfo){
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1, .pSetLayouts = &RLVK.computeSetLayout,
        }, RLVK_ALLOC, &RLVK.computePipelineLayout));
    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
        RLVK_CHECK(vkCreateDescriptorPool(RLVK.device,
            &(VkDescriptorPoolCreateInfo){
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets       = RLVK_COMPUTE_SETS_PER_FRAME,
                .poolSizeCount = 3,
                .pPoolSizes    = (VkDescriptorPoolSize[]){
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         RLVK_COMPUTE_SETS_PER_FRAME*8 },
                    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RLVK_COMPUTE_SETS_PER_FRAME*2 },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         RLVK_COMPUTE_SETS_PER_FRAME },
                },
            }, RLVK_ALLOC, &RLVK.computeDescPools[i]));
    return true;
}

// Load compute shader program: compile the stashed GLSL (shaderc, Vulkan 1.1 target),
// reflect the loose-uniform block, build the monolithic compute pipeline. Returns csId
// itself - the stage slot becomes the program slot, mirroring GL id semantics closely
// enough for raylib's LoadComputeShaderProgram flow.
unsigned int rlLoadShaderProgramCompute(unsigned int csId)
{
    if (!isGpuReady || csId == 0 || csId >= RLVK_MAX_SHADER_SLOTS) return RLVK_INVALID_SLOT;
    rlvkShaderSlot *shader = &RLVK.shaderSlots[csId];
    if (!shader->inUse || !shader->pendingCode) return RLVK_INVALID_SLOT;
    if (!RLVK.shadercCompiler && !rlvkLoadShaderc())
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: compute shader needs shaderc (GLSL compiler) - not available");
        return RLVK_INVALID_SLOT;
    }
    if (!rlvkInitComputeLayout()) return RLVK_INVALID_SLOT;

    char *sanitized = rlvkSanitizeGlsl(shader->pendingCode);
    u32 *spv = NULL; size_t words = 0;
    bool ok = rlvkCompileGlsl(sanitized ? sanitized : shader->pendingCode, 2, &spv, &words);
    if (sanitized) RL_FREE(sanitized);
    RL_FREE(shader->pendingCode);
    shader->pendingCode = NULL;
    if (!ok) return RLVK_INVALID_SLOT;

    // Reflect the implicit loose-uniform block (binding 14) + samplers for rlGetLocationUniform
    rlvkSpvReflection ref;
    rlvkReflectSpv(spv, words, &ref);

    VkResult r = vkCreateShaderModule(RLVK.device,
        &(VkShaderModuleCreateInfo){ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = words*4, .pCode = spv }, RLVK_ALLOC, &shader->compMod);
    RL_FREE(spv);
    if (r != VK_SUCCESS) { TRACELOG(RL_LOG_WARNING, "RLVK: vkCreateShaderModule (compute) => %d", (int)r); return RLVK_INVALID_SLOT; }

    r = vkCreateComputePipelines(RLVK.device, RLVK.pipelineCache, 1,
        &(VkComputePipelineCreateInfo){
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = (VkPipelineShaderStageCreateInfo){
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader->compMod, .pName = "main" },
            .layout = RLVK.computePipelineLayout,
        }, RLVK_ALLOC, &shader->computePipeline);
    if (r != VK_SUCCESS) { TRACELOG(RL_LOG_WARNING, "RLVK: vkCreateComputePipelines => %d", (int)r); return RLVK_INVALID_SLOT; }

    for (int i = 0; i < RL_MAX_SHADER_LOCATIONS; i++) shader->locs[i] = -1;
    for (int i = 0; i < RLVK_MAX_TEXTURE_UNITS; i++) { shader->bindingUnit[i] = i; shader->bindingTexture[i] = 0; }
    shader->uniforms = (rlvkUniform *)RL_CALLOC(RLVK_MAX_SHADER_UNIFORMS, sizeof(rlvkUniform));
    shader->uniformCount = 0;
    for (int m = 0; m < ref.memberCount && shader->uniformCount < RLVK_MAX_SHADER_UNIFORMS; m++)
    {
        rlvkUniform *u = &shader->uniforms[shader->uniformCount++];
        strncpy(u->name, ref.members[m].name, 63);
        u->vsOffset = (int)ref.members[m].offset;   // compute block rides the VS staging fields
        u->fsOffset = -1;
        u->samplerBinding = -1;
    }
    for (int sm = 0; sm < ref.samplerCount && shader->uniformCount < RLVK_MAX_SHADER_UNIFORMS; sm++)
    {
        rlvkUniform *u = &shader->uniforms[shader->uniformCount++];
        strncpy(u->name, ref.samplers[sm].name, 63);
        u->vsOffset = -1; u->fsOffset = -1;
        u->samplerBinding = ref.samplers[sm].binding;
    }
    shader->vsBlockSize = ref.hasBlock ? ref.blockSize : 0;
    shader->vsStage = shader->vsBlockSize ? (unsigned char *)RL_CALLOC(1, shader->vsBlockSize) : NULL;
    shader->usesUbo = (shader->vsBlockSize > 0);
    shader->isCompute = true;

    TRACELOG(RL_LOG_INFO, "RLVK: [ID %u] compute program compiled (%d uniforms, block %uB at binding %u)",
        csId, shader->uniformCount, shader->vsBlockSize, ref.blockBinding);
    return csId;
}

// Delete shader
void rlUnloadShader(unsigned int id)
{
    if (id == 0 || id >= RLVK_MAX_SHADER_SLOTS) return;
    rlvkShaderSlot *s = &RLVK.shaderSlots[id];
    if (!s->inUse) return;

    // Evict this slot's cached pipelines (slot numbers recycle; a stale pipeline would draw
    // the next shader with this one's modules): pipelines go through the fence-gated dead ring,
    // modules destroy immediately (spec-legal after pipeline creation)
    for (int i = RLVK.pipelineCount - 1; i >= 0; i--)
    {
        if (RLVK.pipelines[i].key.shaderSlot != id) continue;
        if (RLVK.pipelines[i].pipeline == RLVK.boundPipeline) RLVK.boundPipeline = VK_NULL_HANDLE;
        rlvkDeferDestroy(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, RLVK.pipelines[i].pipeline);
        RLVK.pipelines[i] = RLVK.pipelines[RLVK.pipelineCount - 1];
        RLVK.pipelineCount--;
    }
    s_pipelineFastValid = false;
    if (s->vertMod) vkDestroyShaderModule(RLVK.device, s->vertMod, RLVK_ALLOC);
    if (s->fragMod) vkDestroyShaderModule(RLVK.device, s->fragMod, RLVK_ALLOC);
    if (s->compMod) vkDestroyShaderModule(RLVK.device, s->compMod, RLVK_ALLOC);
    if (s->computePipeline) rlvkDeferDestroy(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, s->computePipeline);
    if (s->pendingCode) RL_FREE(s->pendingCode);
    if (s->uniforms) RL_FREE(s->uniforms);
    if (s->vsStage) RL_FREE(s->vsStage);
    if (s->fsStage) RL_FREE(s->fsStage);
    memset(s, 0, sizeof(rlvkShaderSlot));
}
// Unload shader program
void rlUnloadShaderProgram(unsigned int id) { rlUnloadShader(id); }

// Get shader location uniform
// NOTE: First parameter refers to shader program id
int rlGetLocationUniform(unsigned int id, const char *name)
{
    if (id == 0 || id >= RLVK_MAX_SHADER_SLOTS || !name) return -1;
    rlvkShaderSlot *shader = &RLVK.shaderSlots[id];
    for (int i = 0; i < shader->uniformCount; i++)
        if (strcmp(shader->uniforms[i].name, name) == 0) return i;
    return -1;
}
// Get shader location attribute
// NOTE: First parameter refers to shader program id
int rlGetLocationAttrib(unsigned int id, const char *name)
{
    if (id == 0 || id >= RLVK_MAX_SHADER_SLOTS || !name) return -1;
    int c = rlvkCanonicalAttribIndex(name);
    return (c >= 0) ? RLVK.shaderSlots[id].attribLocs[c] : -1;
}

// Set shader value uniform
void rlSetUniform(int loc, const void *value, int uniformType, int count)
{
    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];
    if (!shader->usesUbo)
    {
        // Embedded fallback shader: capture DrawMesh's colDiffuse (push-constant path)
        if (loc == RLVK_ULOC_COLDIFFUSE && value) memcpy(RLVK.State.meshColDiffuse, value, sizeof(f32)*4);
        return;
    }
    if (loc < 0 || loc >= shader->uniformCount || !value) return;
    if (shader->uniforms[loc].samplerBinding >= 0 &&
        (uniformType == RL_SHADER_UNIFORM_INT || uniformType == RL_SHADER_UNIFORM_SAMPLER2D))
    {
        int b = shader->uniforms[loc].samplerBinding;
        if (b >= 0 && b < RLVK_MAX_TEXTURE_UNITS) shader->bindingUnit[b] = *(const int *)value;
        return;
    }

    u32 elemBytes = 0;
    switch (uniformType)
    {
        case RL_SHADER_UNIFORM_FLOAT: case RL_SHADER_UNIFORM_INT: case RL_SHADER_UNIFORM_UINT: elemBytes = 4;  break;
        case RL_SHADER_UNIFORM_VEC2:  case RL_SHADER_UNIFORM_IVEC2: case RL_SHADER_UNIFORM_UIVEC2: elemBytes = 8;  break;
        case RL_SHADER_UNIFORM_VEC3:  case RL_SHADER_UNIFORM_IVEC3: case RL_SHADER_UNIFORM_UIVEC3: elemBytes = 12; break;
        case RL_SHADER_UNIFORM_VEC4:  case RL_SHADER_UNIFORM_IVEC4: case RL_SHADER_UNIFORM_UIVEC4: elemBytes = 16; break;
        case RL_SHADER_UNIFORM_SAMPLER2D:
        {
            // glUniform1i on a sampler: associates the sampler's binding with a GL texture unit
            int b = shader->uniforms[loc].samplerBinding;
            if (b >= 0 && b < RLVK_MAX_TEXTURE_UNITS) shader->bindingUnit[b] = *(const int *)value;
            return;
        }
        default: return;
    }

    if (count <= 1) rlvkShaderWriteUniform(shader, loc, value, elemBytes);
    else
    {
        // std140 array stride is 16 for scalar/vec2/vec3/vec4 elements; source data is packed
        rlvkUniform *u = &shader->uniforms[loc];
        for (int i = 0; i < count; i++)
        {
            const unsigned char *src = (const unsigned char *)value + (size_t)i*elemBytes;
            if (u->vsOffset >= 0 && shader->vsStage && (u32)u->vsOffset + i*16 + elemBytes <= shader->vsBlockSize) memcpy(shader->vsStage + u->vsOffset + i*16, src, elemBytes);
            if (u->fsOffset >= 0 && shader->fsStage && (u32)u->fsOffset + i*16 + elemBytes <= shader->fsBlockSize) memcpy(shader->fsStage + u->fsOffset + i*16, src, elemBytes);
        }
        shader->vsWriteGen++; shader->fsWriteGen++;   // conservative: array writes may touch either stage
    }
}
// Set shader value uniform matrix
void rlSetUniformMatrix(int loc, Matrix mat)
{
    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];
    if (!shader->usesUbo)
    {
        if (loc == RLVK_ULOC_MVP) RLVK.State.meshMVP = mat;   // fallback DrawMesh capture
        return;
    }
    // rlMatrixToFloat order (column-major) == std140 mat4 memory layout
    f32 f[16] = {
        mat.m0, mat.m1, mat.m2, mat.m3, mat.m4, mat.m5, mat.m6, mat.m7,
        mat.m8, mat.m9, mat.m10, mat.m11, mat.m12, mat.m13, mat.m14, mat.m15 };
    rlvkShaderWriteUniform(shader, loc, f, sizeof(f));
}
// Set shader value uniform matrix
void rlSetUniformMatrices(int loc, const Matrix *mat, int count)
{
    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];
    if (!shader->usesUbo || loc < 0 || loc >= shader->uniformCount || !mat) return;
    rlvkUniform *u = &shader->uniforms[loc];
    for (int i = 0; i < count; i++)
    {
        f32 f[16] = {
            mat[i].m0, mat[i].m1, mat[i].m2, mat[i].m3, mat[i].m4, mat[i].m5, mat[i].m6, mat[i].m7,
            mat[i].m8, mat[i].m9, mat[i].m10, mat[i].m11, mat[i].m12, mat[i].m13, mat[i].m14, mat[i].m15 };
        if (u->vsOffset >= 0 && shader->vsStage && (u32)u->vsOffset + i*64 + 64 <= shader->vsBlockSize) memcpy(shader->vsStage + u->vsOffset + i*64, f, 64);
        if (u->fsOffset >= 0 && shader->fsStage && (u32)u->fsOffset + i*64 + 64 <= shader->fsBlockSize) memcpy(shader->fsStage + u->fsOffset + i*64, f, 64);
    }
    shader->vsWriteGen++; shader->fsWriteGen++;   // conservative: bone-matrix writes may touch either stage
}
// Set shader value uniform sampler
void rlSetUniformSampler(int loc, unsigned int textureId)
{
    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];
    if (!shader->usesUbo || loc < 0 || loc >= shader->uniformCount) return;
    int b = shader->uniforms[loc].samplerBinding;
    if (b < 0 || b >= RLVK_MAX_TEXTURE_UNITS) return;

    // Some callers pass a texture UNIT index (rlSetUniformSampler(loc, 1) after binding at unit 1
    // - GL resolves this by binding-model coincidence): a small value naming a unit that has a
    // texture bound aliases that unit LIVE. Anything else is a texture id, bound directly.
    if (textureId >= 1 && textureId <= 4 && RLVK.State.activeTextureSlots[textureId] != 0)
    {
        shader->bindingUnit[b] = (int)textureId;
        shader->bindingTexture[b] = 0;
    }
    else
    {
        shader->bindingTexture[b] = textureId;
    }
}

// Set shader currently active (id and locations)
void rlSetShader(unsigned int id, int *locs)
{
    u32 slot = (id == 0) ? RLVK.defaultShaderSlot : id;
    if (RLVK.State.currentShaderSlot != slot) rlDrawRenderBatch(RLVK.currentBatch);   // flush old-shader geometry (mirrors rlgl)
    if (locs && id != 0 && id < RLVK_MAX_SHADER_SLOTS)
        memcpy(RLVK.shaderSlots[id].locs, locs, sizeof(int)*RL_MAX_SHADER_LOCATIONS);
    RLVK.State.currentShaderSlot = slot;
    RLVK.State.activeShaderSlot  = slot;
    RLVK.State.currentShaderLocs = (id != 0 && id < RLVK_MAX_SHADER_SLOTS) ?
        RLVK.shaderSlots[id].locs : RLVK.defaultShaderLocs;
}

// Compute shaders and shader buffer objects (SSBO)
//-----------------------------------------------------------------------------------------

// Dispatch compute shader (equivalent to *draw* for graphics pipeline). Core 1.0 compute:
// suspends the open render scope (dispatch is illegal inside a render pass), snapshots the
// GL-style bind state into a fresh descriptor set, dispatches, and fences the writes so
// later vertex/fragment/transfer reads observe them.
void rlComputeShaderDispatch(unsigned int gx, unsigned int gy, unsigned int gz)
{
    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];
    if (!isGpuReady || !shader->isCompute || (shader->computePipeline == VK_NULL_HANDLE)) return;

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    bool inFrame = RLVK.frameActive;
    VkCommandPool oneShotPool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer;
    u32 openFb = 0;
    if (inFrame)
    {
        cmdBuffer = RLVK.cmdBuffers[frameIndex];
        openFb = RLVK.scope.fbSlot;
        if (openFb) rlDisableFramebuffer();
        vkCmdEndRenderPass(cmdBuffer);
    }
    else
    {
        cmdBuffer = rlvkOneShotBegin(&oneShotPool);   // init-time dispatch (seeding etc.)
        if (cmdBuffer == VK_NULL_HANDLE) return;
    }

    // Snapshot descriptor set: bound SSBOs, storage images, sampler uniforms, uniform block
    VkDescriptorSet ds = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(RLVK.device,
        &(VkDescriptorSetAllocateInfo){
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = RLVK.computeDescPools[frameIndex],
            .descriptorSetCount = 1, .pSetLayouts = &RLVK.computeSetLayout,
        }, &ds);
    if (ds != VK_NULL_HANDLE)
    {
        VkWriteDescriptorSet  writes[18];      // 8 SSBO + 2 sampler defaults + 4 images + 2 sampler uniforms + 1 UBO
        VkDescriptorBufferInfo bufInfos[9];
        VkDescriptorImageInfo  imgInfos[8];
        u32 nWrites = 0, nBuf = 0, nImg = 0;
        // Every SSBO binding gets a valid buffer (unbound slots alias the arena): unwritten
        // "hole" bindings in a set are technically legal for unused bindings, but MoltenVK's
        // Metal argument mapping is happier - and it costs nothing
        for (u32 i = 0; i < 8; i++)
        {
            u32 slot = RLVK.computeSSBO[i];
            bool bound = (slot && slot < RLVK_MAX_BUFFER_SLOTS && RLVK.bufferSlots[slot].buffer != VK_NULL_HANDLE);
            bufInfos[nBuf] = bound
                ? (VkDescriptorBufferInfo){ RLVK.bufferSlots[slot].buffer, 0, RLVK.bufferSlots[slot].sizeBytes }
                : (VkDescriptorBufferInfo){ RLVK.arena[frameIndex].buffer, 0, 16 };
            writes[nWrites++] = (VkWriteDescriptorSet){ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
                .dstBinding = i, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bufInfos[nBuf] };
            nBuf++;
        }
        // Sampler bindings always get the default texture (overridden below when a sampler
        // uniform names a real one)
        for (u32 b = 12; b <= 13; b++)
        {
            rlvkTextureSlot *dt = &RLVK.textureSlots[RLVK.defaultTextureSlot];
            imgInfos[nImg] = (VkDescriptorImageInfo){ .sampler = dt->sampler, .imageView = dt->view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            writes[nWrites++] = (VkWriteDescriptorSet){ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
                .dstBinding = b, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &imgInfos[nImg] };
            nImg++;
        }
        // (No storage-image writes: the fixed layout deliberately has no image bindings -
        // see rlvkInitComputeLayout's MoltenVK note. rlBindImageTexture records + warns.)
        // Sampler uniforms (bindings 12..13): rlSetUniformSampler's explicit texture, else default
        for (int u = 0; u < shader->uniformCount; u++)
        {
            int b = shader->uniforms[u].samplerBinding;
            if (b < 12 || b > 13) continue;
            u32 texSlot = shader->bindingTexture[b];
            rlvkTextureSlot *t = &RLVK.textureSlots[(texSlot && texSlot < RLVK_MAX_TEXTURE_SLOTS)? texSlot : RLVK.defaultTextureSlot];
            if (t->currentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || !t->view) t = &RLVK.textureSlots[RLVK.defaultTextureSlot];
            imgInfos[nImg] = (VkDescriptorImageInfo){ .sampler = t->sampler, .imageView = t->view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            writes[nWrites++] = (VkWriteDescriptorSet){ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
                .dstBinding = (u32)b, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &imgInfos[nImg] };
            nImg++;
        }
        // Loose-uniform block (binding 14): snapshot the staging block into the frame arena
        if (shader->vsBlockSize && shader->vsStage)
        {
            rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
            VkDeviceSize off = (RLVK.arenaOffset[frameIndex] + 255) & ~(VkDeviceSize)255;
            if (off + shader->vsBlockSize <= arena->sizeBytes)
            {
                memcpy((char *)arena->mapped + off, shader->vsStage, shader->vsBlockSize);
                RLVK.arenaOffset[frameIndex] = off + shader->vsBlockSize;
                RLVK.arenaWanted[frameIndex] += shader->vsBlockSize + 256;
                bufInfos[nBuf] = (VkDescriptorBufferInfo){ arena->buffer, off, shader->vsBlockSize };
                writes[nWrites++] = (VkWriteDescriptorSet){ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
                    .dstBinding = 14, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &bufInfos[nBuf] };
                nBuf++;
            }
            else TRACELOG(RL_LOG_WARNING, "RLVK: arena full, compute uniforms skipped this dispatch");
        }
        if (nWrites) vkUpdateDescriptorSets(RLVK.device, nWrites, writes, 0, NULL);

        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->computePipeline);
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, RLVK.computePipelineLayout, 0, 1, &ds, 0, NULL);
        vkCmdDispatch(cmdBuffer, gx, gy, gz);

        // glMemoryBarrier semantics: compute writes visible to every later consumer
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &(VkMemoryBarrier2){
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                               | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT
                               | VK_ACCESS_2_UNIFORM_READ_BIT,
            },
        });
    }
    else TRACELOG(RL_LOG_WARNING, "RLVK: compute descriptor pool exhausted - raise RLVK_COMPUTE_SETS_PER_FRAME");

    if (inFrame)
    {
        rlvkResumeSwapchainScope(cmdBuffer);
        if (openFb) rlEnableFramebuffer(openFb);
    }
    else rlvkOneShotEnd(oneShotPool, cmdBuffer);
}

// Load shader storage buffer object (SSBO): DEVICE_LOCAL storage buffer, optional initial data
unsigned int rlLoadShaderBuffer(unsigned int size, const void *data, int usageHint)
{
    if (!isGpuReady || size == 0) return RLVK_INVALID_SLOT;
    u32 slot = rlvkAllocBufferSlot();
    if (slot == RLVK_INVALID_SLOT) return RLVK_INVALID_SLOT;
    rlvkBufferSlot *b = &RLVK.bufferSlots[slot];
    b->sizeBytes = size;
    b->usageHint = usageHint;
    b->isIndex   = false;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
        &(VkBufferCreateInfo){ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size  = size,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                   | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,   // particle systems draw straight from their SSBO
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE }, RLVK_ALLOC, &b->buffer));
    VkMemoryRequirements memReq; vkGetBufferMemoryRequirements(RLVK.device, b->buffer, &memReq);
    b->memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindBufferMemory(RLVK.device, b->buffer, b->memory, 0);
    if (data) rlvkUploadBuffer(b->buffer, 0, data, size);
    return slot;
}

// Unload shader storage buffer object (SSBO): fence-gated like every other GPU object
void rlUnloadShaderBuffer(unsigned int id)
{
    if (id == 0 || id >= RLVK_MAX_BUFFER_SLOTS) return;
    rlvkBufferSlot *b = &RLVK.bufferSlots[id];
    rlvkDeferDestroy(b->buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, b->memory, VK_NULL_HANDLE);
    b->buffer = VK_NULL_HANDLE; b->memory = VK_NULL_HANDLE; b->mapped = NULL;
    b->inUse = false;
    b->freedFrame = (u32)RLVK.frameCounter;
    for (u32 i = 0; i < 8; i++) if (RLVK.computeSSBO[i] == id) RLVK.computeSSBO[i] = 0;
}

// Update SSBO buffer data (in-stream mid-frame, one-shot staging at load time)
void rlUpdateShaderBuffer(unsigned int id, const void *data, unsigned int dataSize, unsigned int offset)
{
    if (id == 0 || id >= RLVK_MAX_BUFFER_SLOTS || !data || !dataSize) return;
    rlvkBufferSlot *b = &RLVK.bufferSlots[id];
    if (b->buffer == VK_NULL_HANDLE) return;
    rlvkUploadBuffer(b->buffer, offset, data, dataSize);
}

// Bind SSBO at an indexed binding point (GL glBindBufferBase semantics, consumed at dispatch)
void rlBindShaderBuffer(unsigned int id, unsigned int index)
{
    if (index >= 8) { TRACELOG(RL_LOG_WARNING, "RLVK: SSBO binding %u out of range (max 8)", index); return; }
    RLVK.computeSSBO[index] = (id < RLVK_MAX_BUFFER_SLOTS)? id : 0;
}

// Read SSBO buffer data (GPU->CPU): synchronous, GL glGetBufferSubData semantics
void rlReadShaderBuffer(unsigned int id, void *dest, unsigned int count, unsigned int offset)
{
    if (id == 0 || id >= RLVK_MAX_BUFFER_SLOTS || !dest || !count) return;
    rlvkBufferSlot *b = &RLVK.bufferSlots[id];
    if (b->buffer == VK_NULL_HANDLE) return;

    // Execute everything recorded so far, then drain, so the read observes GL command order
    rlvkFlushFrame();
    rlvkWaitInFlightFrames();

    VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE; void *map = NULL;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
        &(VkBufferCreateInfo){ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = count, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE },
        RLVK_ALLOC, &staging));
    VkMemoryRequirements memReq; vkGetBufferMemoryRequirements(RLVK.device, staging, &memReq);
    stagingMem = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(RLVK.device, staging, stagingMem, 0);

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = rlvkOneShotBegin(&pool);
    if (cmdBuffer != VK_NULL_HANDLE)
    {
        vkCmdCopyBuffer(cmdBuffer, b->buffer, staging, 1, &(VkBufferCopy){ .srcOffset = offset, .size = count });
        rlvkOneShotEnd(pool, cmdBuffer);
        vkMapMemory(RLVK.device, stagingMem, 0, count, 0, &map);
        memcpy(dest, map, count);
        vkUnmapMemory(RLVK.device, stagingMem);
    }
    vkDestroyBuffer(RLVK.device, staging, RLVK_ALLOC);
    vkFreeMemory(RLVK.device, stagingMem, RLVK_ALLOC);
}

// Copy SSBO data between buffers (GPU-side)
void rlCopyShaderBuffer(unsigned int destId, unsigned int srcId, unsigned int destOffset, unsigned int srcOffset, unsigned int count)
{
    if (destId == 0 || destId >= RLVK_MAX_BUFFER_SLOTS || srcId == 0 || srcId >= RLVK_MAX_BUFFER_SLOTS || !count) return;
    rlvkBufferSlot *dst = &RLVK.bufferSlots[destId], *src = &RLVK.bufferSlots[srcId];
    if (dst->buffer == VK_NULL_HANDLE || src->buffer == VK_NULL_HANDLE) return;

    if (RLVK.frameActive)
    {
        // In-stream: suspend the scope like every other mid-frame copy
        u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
        VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
        u32 openFb = RLVK.scope.fbSlot;
        if (openFb) rlDisableFramebuffer();
        vkCmdEndRenderPass(cmdBuffer);
        vkCmdCopyBuffer(cmdBuffer, src->buffer, dst->buffer, 1,
            &(VkBufferCopy){ .srcOffset = srcOffset, .dstOffset = destOffset, .size = count });
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &(VkMemoryBarrier2){
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
            },
        });
        rlvkResumeSwapchainScope(cmdBuffer);
        if (openFb) rlEnableFramebuffer(openFb);
    }
    else
    {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmdBuffer = rlvkOneShotBegin(&pool);
        if (cmdBuffer == VK_NULL_HANDLE) return;
        vkCmdCopyBuffer(cmdBuffer, src->buffer, dst->buffer, 1,
            &(VkBufferCopy){ .srcOffset = srcOffset, .dstOffset = destOffset, .size = count });
        rlvkOneShotEnd(pool, cmdBuffer);
    }
}

// Get SSBO buffer size
unsigned int rlGetShaderBufferSize(unsigned int id) { return (id < RLVK_MAX_BUFFER_SLOTS) ? RLVK.bufferSlots[id].sizeBytes : 0; }

// Bind image texture at an image unit (GL glBindImageTexture semantics). Recorded but NOT
// consumed yet: the fixed compute layout deliberately has no storage-image bindings (a
// MoltenVK/Intel bug zeroes the UBO when they merely exist - see rlvkInitComputeLayout),
// and rlLoadTexture images lack STORAGE usage anyway. Image load/store consumers need a
// dedicated path (own descriptor set + STORAGE-usage image creation) when one appears.
void rlBindImageTexture(unsigned int id, unsigned int index, int format, bool readonly)
{
    (void)format; (void)readonly;
    if (index >= 4) { TRACELOG(RL_LOG_WARNING, "RLVK: image unit %u out of range (max 4)", index); return; }
    RLVK.computeImage[index] = (id < RLVK_MAX_TEXTURE_SLOTS)? id : 0;
    static bool warned = false;
    if (!warned) { TRACELOG(RL_LOG_WARNING, "RLVK: rlBindImageTexture recorded but compute image load/store is not wired yet"); warned = true; }
}

// Matrix state management
//-----------------------------------------------------------------------------------------

// Get internal modelview matrix
Matrix rlGetMatrixModelview(void)               { return RLVK.State.modelview; }
// Get internal projection matrix
Matrix rlGetMatrixProjection(void)              { return RLVK.State.projection; }
// Get internal accumulated transform matrix
Matrix rlGetMatrixTransform(void)               { return RLVK.State.transform; }
// Get internal projection matrix for stereo render (selected eye)
Matrix rlGetMatrixProjectionStereo(int eye)     { return RLVK.State.projectionStereo[eye & 1]; }
// Get internal view offset matrix for stereo render (selected eye)
Matrix rlGetMatrixViewOffsetStereo(int eye)     { return RLVK.State.viewOffsetStereo[eye & 1]; }

// Set a custom projection matrix (replaces internal projection matrix)
void rlSetMatrixProjection(Matrix p)            { RLVK.State.projection = p; }
void rlSetMatrixModelview (Matrix v)            { RLVK.State.modelview  = v; }
// Set eyes projection matrices for stereo rendering
void rlSetMatrixProjectionStereo(Matrix r, Matrix l) { RLVK.State.projectionStereo[0] = r; RLVK.State.projectionStereo[1] = l; }
// Set eyes view offsets matrices for stereo rendering
void rlSetMatrixViewOffsetStereo(Matrix r, Matrix l) { RLVK.State.viewOffsetStereo[0] = r; RLVK.State.viewOffsetStereo[1] = l; }

// Default internal resources (default texture, default shader)
//-----------------------------------------------------------------------------------------

// Load and draw a cube in NDC
void rlLoadDrawCube(void) {}
// Draw a fullscreen NDC quad (interleaved pos3+uv2, triangle strip) with the CURRENT shader -
// used by post-processing / deferred lighting passes. The quad VBO is created once and cached.
void rlLoadDrawQuad(void)
{
    if (!isGpuReady) return;
    static const f32 quadVerts[] = {
        -1.0f,  1.0f, 0.0f,   0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f,   0.0f, 0.0f,
         1.0f,  1.0f, 0.0f,   1.0f, 1.0f,
         1.0f, -1.0f, 0.0f,   1.0f, 0.0f,
    };
    static u32 quadVbo = 0;
    if (quadVbo == 0) quadVbo = rlvkCreateVBO(quadVerts, (int)sizeof(quadVerts), false, false);
    if (quadVbo == RLVK_INVALID_SLOT) return;

    rlvkBeginFrame();
    if (!RLVK.frameActive) return;
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];

    s_bindingValid = false;   // quad blit rebinds vertex buffers outside the mesh-draw dedup cache
    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];

    // Interleaved pos+uv layout and shader stages are baked into the cached pipeline
    // Pipeline MUST be bound BEFORE vertex buffers (MoltenVK Metal buffer index resolution)
    rlvkBindPipeline(cmdBuffer, 2, RLVK_VLAYOUT_QUAD, RLVK.State.activeShaderSlot);
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1,
        (VkBuffer[]){ RLVK.bufferSlots[quadVbo].buffer }, (VkDeviceSize[]){ 0 });
    rlvkBindDummyAttribBuffers(cmdBuffer, RLVK_VLAYOUT_QUAD, shader);

    if (shader->usesUbo)
    {
        rlvkBindShaderUbos(cmdBuffer, shader);
        rlvkBindShaderSamplers(cmdBuffer, shader, true);
    }
    rlvkFlushSet0(cmdBuffer);
    vk.CmdDraw(cmdBuffer, 4, 1, 0, 0);
}

//----------------------------------------------------------------------------------
// Internal Functions Definition - Vulkan initialization and frame lifecycle
//----------------------------------------------------------------------------------

// Initialize the Vulkan instance (surface extensions + validation message filter)
static bool rlvkInitInstance(void)
{
    // Surface extensions so the platform layer can create a VkSurfaceKHR (via glfwCreateWindowSurface).
    const char *instanceExtensions[4];
    u32 instanceExtensionCount = 0;
    instanceExtensions[instanceExtensionCount++] = VK_KHR_SURFACE_EXTENSION_NAME;
#if defined(_WIN32)
    instanceExtensions[instanceExtensionCount++] = "VK_KHR_win32_surface";
#elif defined(__ANDROID__)
    instanceExtensions[instanceExtensionCount++] = "VK_KHR_android_surface";
#elif defined(__APPLE__)
    instanceExtensions[instanceExtensionCount++] = "VK_EXT_metal_surface";
#else
    instanceExtensions[instanceExtensionCount++] = "VK_KHR_xcb_surface";
#endif

    // Baseline is Vulkan 1.1; ask for 1.3 only when the loader has it, so devices that
    // still expose the 1.3 fast paths (queried per-feature at device creation) may use them.
    // vkEnumerateInstanceVersion is a core 1.1 loader export - the 1.1 baseline requires it.
    u32 loaderVersion = VK_API_VERSION_1_1;
    vkEnumerateInstanceVersion(&loaderVersion);
    u32 apiVersion = (loaderVersion >= VK_API_VERSION_1_3) ? VK_API_VERSION_1_3 : VK_API_VERSION_1_1;

    // MoltenVK (macOS) is a non-conformant "portability" ICD: recent loaders hide it unless
    // VK_KHR_portability_enumeration is enabled with the matching create flag
    VkInstanceCreateFlags instanceFlags = 0;
    {
        u32 propCount = 0;
        vkEnumerateInstanceExtensionProperties(NULL, &propCount, NULL);
        VkExtensionProperties *props = (VkExtensionProperties *)RL_MALLOC(propCount*sizeof(VkExtensionProperties));
        vkEnumerateInstanceExtensionProperties(NULL, &propCount, props);
        for (u32 i = 0; i < propCount; i++)
        {
            if (strcmp(props[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
            {
                instanceExtensions[instanceExtensionCount++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
                instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
                break;
            }
        }
        RL_FREE(props);
    }

    // DESIGN NOTE: rlvk never enables validation layers itself; that stays a developer-machine
    // choice via vkconfig / loader settings (zero layer overhead by default, reconfigurable
    // without rebuilds, one external source of truth). The backend only carries the FALSE-FLAG
    // suppression filter below, passed via VK_EXT_layer_settings - never activation.

    // FALSE-FLAG filter (VK_EXT_layer_settings): suppressions live in code, each justified
    // below. Verified against SDK 1.4.313.1 validation; re-verify on layer updates. Ignored
    // when validation is off, so always safe to pass.
    static const char *messageIdFilter[] = {
        // BestPractices-specialuse-extension: depth_clip_control is registry-marked "special
        // use: GL/D3D emulation" - a GL-emulating backend is exactly that use, not misuse
        "BestPractices-specialuse-extension",

        // FALSE FLAG (SDK 1.4.313.1, re-verified 2026-07-06, forced GPU-AV via
        // VK_LAYER_SETTINGS_PATH): the layer mis-sizes ZERO-DIVISOR vertex bindings - a
        // divisor-0 stride-0 attribute fetches exactly formatSize bytes, yet a 4-byte fetch
        // from a 248-byte binding is flagged ("Vertices count: 0" betrays the broken bound)
        "VUID-vkCmdDrawIndexed-None-02721",
        "VUID-vkCmdDraw-None-02721",

        // GL semantics, not a defect: depth-only framebuffers (shadowmaps) legally run
        // fragment shaders whose color output has no attachment to land in; GL discards such
        // writes silently and this backend reproduces GL behavior bit-for-bit.
        "Undefined-Value-ShaderOutputNotConsumed-DynamicRendering",

        // Layer-internal notices (SDK 1.4.313.1), not app-actionable: GPU-AV force-enables
        // device features for its own instrumentation, reports its shader-cache file state,
        // and prints enabled-settings banners at instance creation.
        "WARNING-GPU-Assisted-Validation",
        "WARNING-cache-file-error",
        "VALIDATION-SETTINGS",
        "WARNING-CreateInstance-status-message",

        // ACCEPTED BY DESIGN, documented tradeoffs (vendor perf advisories, not correctness):
        // - D32 depth is REQUIRED: pixel-for-pixel equivalence with the GL backend depends on
        //   matching its depth precision (a D24 experiment measurably diverged and was reverted)
        "BestPractices-NVIDIA-CreateImage-Depth32Format",
        // - The pipeline layout mirrors rlgl's 16-texture-unit model exactly; shrinking it
        //   would break the GL texture-unit semantics this backend exists to reproduce
        "BestPractices-AMD-CreatePipelinesLayout-KeepLayoutSmall",
        // - Clear colors are app-controlled (rlClearColor / ClearBackground, GL semantics);
        //   raylib apps clear to arbitrary colors like RAYWHITE by design
        "BestPractices-AMD-ClearAttachment-FastClearValues-color",
        // - One dedicated allocation per resource keeps the backend simple and mirrors the
        //   one-GL-object-per-resource model; a suballocator is a deliberate non-goal
        //   (ReuseAllocations likewise advises pooling freed allocations)
        "BestPractices-vkAllocateMemory-small-allocation",
        "BestPractices-vkBindImageMemory-small-dedicated-allocation",
        "BestPractices-vkBindBufferMemory-small-dedicated-allocation",
        "BestPractices-NVIDIA-AllocateMemory-ReuseAllocations",
    };

    return vkCreateInstance(
        &(VkInstanceCreateInfo){
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            &(VkLayerSettingsCreateInfoEXT){
                VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
                .settingCount = 1,
                .pSettings    = &(VkLayerSettingEXT){
                    .pLayerName   = "VK_LAYER_KHRONOS_validation",
                    .pSettingName = "message_id_filter",
                    .type         = VK_LAYER_SETTING_TYPE_STRING_EXT,
                    .valueCount   = RLVK_COUNTOF(messageIdFilter),
                    .pValues      = messageIdFilter,
                },
            },
            .flags = instanceFlags,
            .pApplicationInfo = &(VkApplicationInfo){
                VK_STRUCTURE_TYPE_APPLICATION_INFO,
                .pApplicationName = "raylib",
                .apiVersion       = apiVersion,
            },
            .enabledExtensionCount   = instanceExtensionCount,
            .ppEnabledExtensionNames = instanceExtensions,
        }, RLVK_ALLOC, &RLVK.instance) == VK_SUCCESS;
}

// Pick the physical device: best-scoring GPU supporting Vulkan 1.1+ (discrete first, newer API as tiebreak)
static bool rlvkPickPhysicalDevice(void)
{
    u32 count = 0;
    vkEnumeratePhysicalDevices(RLVK.instance, &count, NULL);
    if (count == 0) return false;

    VkPhysicalDevice *devs = (VkPhysicalDevice *)RL_MALLOC(count*sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(RLVK.instance, &count, devs);

    VkPhysicalDevice best = VK_NULL_HANDLE;

    // Optional override for cross-device/driver comparison (the same build can target a specific
    // GPU): RLVK_DEVICE_INDEX = enumeration index, or RLVK_DEVICE_NAME = case-sensitive substring
    // of the device name (e.g. "Intel", "RTX"). Falls back to automatic scoring when unset/unmatched.
    const char *envIdx  = getenv("RLVK_DEVICE_INDEX");
    const char *envName = getenv("RLVK_DEVICE_NAME");
    if (envIdx != NULL)
    {
        int idx = atoi(envIdx);
        if ((idx >= 0) && (idx < (int)count)) best = devs[idx];
    }
    if ((best == VK_NULL_HANDLE) && (envName != NULL) && (envName[0] != '\0'))
    {
        for (u32 i = 0; i < count; i++)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devs[i], &props);
            if (strstr(props.deviceName, envName) != NULL) { best = devs[i]; break; }
        }
    }

    int bestScore = -1;
    if (best == VK_NULL_HANDLE)
    {
        for (u32 i = 0; i < count; i++)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devs[i], &props);
            if (props.apiVersion < VK_API_VERSION_1_1) continue;   // 1.1 is the hard floor

            int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 100 : 50;
            if (props.apiVersion >= VK_API_VERSION_1_3) score += 10;   // richer fast-path caps as tiebreak
            if (score > bestScore) { bestScore = score; best = devs[i]; }
        }
    }
    RL_FREE(devs);
    if (!best) return false;
    RLVK.physicalDevice = best;

    // Log the selected device (mirrors the GL backend's device info output)
    // VkPhysicalDeviceDriverProperties needs 1.2 / VK_KHR_driver_properties; on a plain 1.1
    // device the struct must not be chained (driverName/driverInfo stay empty strings)
    VkPhysicalDeviceProperties baseProps;
    vkGetPhysicalDeviceProperties(best, &baseProps);
    VkPhysicalDeviceDriverProperties driverProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
    VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        (baseProps.apiVersion >= VK_API_VERSION_1_2) ? &driverProps : NULL };
    vkGetPhysicalDeviceProperties2(best, &props2);
    RLVK.Caps.apiVersion = props2.properties.apiVersion;
    TRACELOG(RL_LOG_INFO, "RLVK: Vulkan device information:");
    TRACELOG(RL_LOG_INFO, "    > Device:   %s", props2.properties.deviceName);
    TRACELOG(RL_LOG_INFO, "    > Driver:   %s %s", driverProps.driverName, driverProps.driverInfo);
    TRACELOG(RL_LOG_INFO, "    > API:      %u.%u.%u",
             VK_API_VERSION_MAJOR(props2.properties.apiVersion),
             VK_API_VERSION_MINOR(props2.properties.apiVersion),
             VK_API_VERSION_PATCH(props2.properties.apiVersion));

    u32 qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(best, &qfCount, NULL);
    VkQueueFamilyProperties *qfs = (VkQueueFamilyProperties *)RL_MALLOC(qfCount*sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(best, &qfCount, qfs);
    RLVK.graphicsFamily = UINT32_MAX;
    RLVK.transferFamily = UINT32_MAX;
    for (u32 i = 0; i < qfCount; i++)
    {
        if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && RLVK.graphicsFamily == UINT32_MAX) RLVK.graphicsFamily = i;
        if ((qfs[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && !(qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && RLVK.transferFamily == UINT32_MAX) RLVK.transferFamily = i;
    }
    if (RLVK.transferFamily == UINT32_MAX) RLVK.transferFamily = RLVK.graphicsFamily;
    RL_FREE(qfs);
    return RLVK.graphicsFamily != UINT32_MAX;
}

// Initialize the logical device: VK_KHR_swapchain is the only hard requirement (1.1 baseline);
// every other extension/feature is enumerated, recorded in RLVK.Caps, and enabled when present
static bool rlvkInitLogicalDevice(void)
{
    const char *deviceExtensions[12];
    u32 deviceExtensionCount = 0;

    // Extension roll-call
    bool hasSwapchain = false, hasPushDesc = false;
    bool hasLineRasterEXT = false, hasLineRasterKHR = false;
    bool hasPriority = false, hasPageable = false, hasGpl = false, hasPipelineLibrary = false;
    bool hasPortabilitySubset = false;
    {
        u32 propCount = 0;
        vkEnumerateDeviceExtensionProperties(RLVK.physicalDevice, NULL, &propCount, NULL);
        VkExtensionProperties *props = (VkExtensionProperties *)RL_MALLOC(propCount*sizeof(VkExtensionProperties));
        vkEnumerateDeviceExtensionProperties(RLVK.physicalDevice, NULL, &propCount, props);
        for (u32 i = 0; i < propCount; i++)
        {
            const char *n = props[i].extensionName;
            if      (strcmp(n, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)                  hasSwapchain = true;
            else if (strcmp(n, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0)            hasPushDesc = true;
            else if (strcmp(n, VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME) == 0)         hasLineRasterEXT = true;
            else if (strcmp(n, VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME) == 0)         hasLineRasterKHR = true;
            else if (strcmp(n, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME) == 0)            hasPriority = true;
            else if (strcmp(n, VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME) == 0) hasPageable = true;
            else if (strcmp(n, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME) == 0)  hasGpl = true;
            else if (strcmp(n, VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME) == 0)           hasPipelineLibrary = true;
            else if (strcmp(n, "VK_KHR_portability_subset") == 0)                      hasPortabilitySubset = true;
        }
        RL_FREE(props);
    }
    if (!hasSwapchain) { TRACELOG(RL_LOG_FATAL, "RLVK: required device extension not supported: %s", VK_KHR_SWAPCHAIN_EXTENSION_NAME); return false; }

    // Feature query: chain each optional struct only when its extension exists (chaining a
    // struct of an unsupported extension is invalid); 1.3 core features only on 1.3+ devices
    VkPhysicalDeviceLineRasterizationFeaturesEXT qLine = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT };
    VkPhysicalDeviceVulkan13Features q13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceFeatures2 q2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    {
        void **qTail = &q2.pNext;
        #define RLVK_CHAIN_IF(_cond, _s) if (_cond) { *qTail = (void *)&_s; qTail = &_s.pNext; }
        RLVK_CHAIN_IF(hasLineRasterEXT || hasLineRasterKHR, qLine);
        RLVK_CHAIN_IF(RLVK.Caps.apiVersion >= VK_API_VERSION_1_3, q13);
        #undef RLVK_CHAIN_IF
        vkGetPhysicalDeviceFeatures2(RLVK.physicalDevice, &q2);
    }

    RLVK.Caps.dynamicRendering  = (RLVK.Caps.apiVersion >= VK_API_VERSION_1_3) && q13.dynamicRendering;
    RLVK.Caps.synchronization2  = (RLVK.Caps.apiVersion >= VK_API_VERSION_1_3) && q13.synchronization2;
    RLVK.Caps.pushDescriptor    = hasPushDesc;
    RLVK.Caps.bresenhamLines    = (hasLineRasterEXT || hasLineRasterKHR) && qLine.bresenhamLines;
    RLVK.Caps.wideLines         = q2.features.wideLines;
    RLVK.Caps.fillModeNonSolid  = q2.features.fillModeNonSolid;
    RLVK.Caps.memoryPriority    = hasPriority;
    RLVK.Caps.pageableMemory    = (hasPriority && hasPageable);
    RLVK.Caps.graphicsPipelineLibrary = (hasGpl && hasPipelineLibrary);

    // Enable everything supported (spec: VK_KHR_portability_subset MUST be enabled when present)
    deviceExtensions[deviceExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (hasPortabilitySubset)       deviceExtensions[deviceExtensionCount++] = "VK_KHR_portability_subset";
    if (RLVK.Caps.pushDescriptor)   deviceExtensions[deviceExtensionCount++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
    if (RLVK.Caps.bresenhamLines)   deviceExtensions[deviceExtensionCount++] = hasLineRasterKHR ? VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME : VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME;
    if (RLVK.Caps.memoryPriority)   deviceExtensions[deviceExtensionCount++] = VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME;
    if (RLVK.Caps.pageableMemory)   deviceExtensions[deviceExtensionCount++] = VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME;
    if (RLVK.Caps.graphicsPipelineLibrary)
    {
        deviceExtensions[deviceExtensionCount++] = VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME;   // required by graphics_pipeline_library
        deviceExtensions[deviceExtensionCount++] = VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME;
    }

    f32 queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queues[RLVK_QUEUE_COUNT] = {
        [RLVK_QUEUE_GRAPHICS] = {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = RLVK.graphicsFamily,
            .queueCount       = 1,
            .pQueuePriorities = &queuePriority,
        },
        [RLVK_QUEUE_TRANSFER] = {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = RLVK.transferFamily,
            .queueCount       = 1,
            .pQueuePriorities = &queuePriority,
        },
    };
    u32 queueCount = (RLVK.transferFamily != RLVK.graphicsFamily) ? RLVK_QUEUE_COUNT : 1;

    // Requested features: each struct links into the pNext chain only when RLVK.Caps says
    // the device has it (requesting an unsupported feature fails vkCreateDevice wholesale)
    VkPhysicalDeviceMemoryPriorityFeaturesEXT memoryPriorityFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT,
        .memoryPriority = VK_TRUE,
    };
    VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pageableMemoryFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT,
        .pageableDeviceLocalMemory = VK_TRUE,
    };
    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT gplFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT,
        .graphicsPipelineLibrary = VK_TRUE,
    };
    VkPhysicalDeviceLineRasterizationFeaturesEXT lineRasterizationFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT,
        .bresenhamLines = VK_TRUE,   // match GL's 1px line pixel coverage
    };
    VkPhysicalDeviceVulkan13Features vulkan13Features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features2 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .features = {
            // wideLines is deliberately NOT requested: every pipeline uses lineWidth 1.0
            // (rlSetLineWidth is stored for rlGetLineWidth but never widens rasterization)
            .fillModeNonSolid = RLVK.Caps.fillModeNonSolid ? VK_TRUE : VK_FALSE,
        },
    };
    {
        void **fTail = &features2.pNext;
        #define RLVK_CHAIN_IF(_cond, _s) if (_cond) { *fTail = (void *)&_s; fTail = &_s.pNext; }
        RLVK_CHAIN_IF(RLVK.Caps.memoryPriority,   memoryPriorityFeatures);
        RLVK_CHAIN_IF(RLVK.Caps.pageableMemory,   pageableMemoryFeatures);
        RLVK_CHAIN_IF(RLVK.Caps.graphicsPipelineLibrary, gplFeatures);
        RLVK_CHAIN_IF(RLVK.Caps.bresenhamLines,   lineRasterizationFeatures);
        RLVK_CHAIN_IF(RLVK.Caps.dynamicRendering || RLVK.Caps.synchronization2, vulkan13Features);
        #undef RLVK_CHAIN_IF
        vulkan13Features.dynamicRendering = RLVK.Caps.dynamicRendering ? VK_TRUE : VK_FALSE;
        vulkan13Features.synchronization2 = RLVK.Caps.synchronization2 ? VK_TRUE : VK_FALSE;
    }

    VkResult _cdResult = vkCreateDevice(RLVK.physicalDevice,
        &(VkDeviceCreateInfo){
            VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            &features2,
            .queueCreateInfoCount    = queueCount,
            .pQueueCreateInfos       = queues,
            .enabledExtensionCount   = deviceExtensionCount,
            .ppEnabledExtensionNames = deviceExtensions,
        }, RLVK_ALLOC, &RLVK.device);
    if (_cdResult != VK_SUCCESS) { TRACELOG(RL_LOG_FATAL, "RLVK: vkCreateDevice failed VkResult=%d", (int)_cdResult); return false; }
    vkGetDeviceQueue(RLVK.device, RLVK.graphicsFamily, 0, &RLVK.graphicsQueue);
    vkGetDeviceQueue(RLVK.device, RLVK.transferFamily, 0, &RLVK.transferQueue);
    return true;
}

// Load the device-level entry points into the vk dispatch table
static void rlvkLoadEntrypoints(void)
{
#define RLVK_PFN_FUNC(_func)                                                          \
    vk._func = (PFN_vk##_func)vkGetDeviceProcAddr(RLVK.device, "vk" #_func);
    RLVK_PFN_FUNCS
#undef RLVK_PFN_FUNC

    // Vulkan 1.1 fallbacks: install compat shims where the native entry point is absent,
    // so every call site keeps its sync2/push-descriptor shape with zero branching
    if (!RLVK.Caps.synchronization2 || (vk.CmdPipelineBarrier2 == NULL)) vk.CmdPipelineBarrier2 = rlvkCmdPipelineBarrier2Compat;
    if (!RLVK.Caps.synchronization2 || (vk.QueueSubmit2 == NULL))        vk.QueueSubmit2        = rlvkQueueSubmit2Compat;
    if (!RLVK.Caps.pushDescriptor || (vk.CmdPushDescriptorSetKHR == NULL)) vk.CmdPushDescriptorSetKHR = rlvkPushDescriptorSetCompat;

    // Warn about anything still unresolved (a genuine gap: no native support, no fallback yet)
#define RLVK_PFN_FUNC(_func) RLVK_CHECK_LOG(vk._func == NULL, "Couldn't load vk" #_func);
    RLVK_PFN_FUNCS
#undef RLVK_PFN_FUNC
}

// PIVOT: set 0 is a push-descriptor layout of combined image samplers, one binding per GL texture
// unit (binding 0 = texture0). Each draw pushes the textures it consumes via
// vkCmdPushDescriptorSetKHR - no descriptor pool, no persistent set, exact rlgl texture-unit semantics.
static bool rlvkInitSet0Layout(void)
{
    VkDescriptorSetLayoutBinding bindings[RLVK_SET0_BINDING_COUNT];
    for (u32 i = 0; i < RLVK_MAX_TEXTURE_UNITS; i++)
    {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding         = i,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    bindings[RLVK_UBO_BINDING_VS] = (VkDescriptorSetLayoutBinding){
        .binding = RLVK_UBO_BINDING_VS, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT };
    bindings[RLVK_UBO_BINDING_FS] = (VkDescriptorSetLayoutBinding){
        .binding = RLVK_UBO_BINDING_FS, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
    RLVK_CHECK(vkCreateDescriptorSetLayout(RLVK.device,
        &(VkDescriptorSetLayoutCreateInfo){
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            // Push-descriptor layouts need the flag; the pool-ring fallback (no push
            // descriptor support) allocates plain sets from this same layout instead
            .flags        = RLVK.Caps.pushDescriptor ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0,
            .bindingCount = RLVK_SET0_BINDING_COUNT,
            .pBindings    = bindings,
        }, RLVK_ALLOC, &RLVK.set0Layout));
    return true;
}

//----------------------------------------------------------------------------------
// Push-descriptor fallback (Vulkan 1.1 baseline, no VK_KHR_push_descriptor)
//
// Every push site keeps calling vk.CmdPushDescriptorSetKHR. On devices without the
// extension this shim is installed instead: it only updates a CPU shadow of set 0
// (pushedView/pushedSampler for the 16 texture units, shadowUbo for the 2 UBOs) and
// marks it dirty. rlvkFlushSet0 - called right before every draw - then allocates a
// fresh set from the frame's descriptor pool, writes the whole shadow and binds it.
// GL bind-at-draw semantics are preserved exactly; consecutive draws with unchanged
// bindings reuse the previously bound set (dirty flag).
//----------------------------------------------------------------------------------

static VKAPI_ATTR void VKAPI_CALL rlvkPushDescriptorSetCompat(VkCommandBuffer cmdBuffer, VkPipelineBindPoint bindPoint,
    VkPipelineLayout layout, uint32_t set, uint32_t writeCount, const VkWriteDescriptorSet *writes)
{
    (void)cmdBuffer; (void)bindPoint; (void)layout; (void)set;
    for (uint32_t i = 0; i < writeCount; i++)
    {
        const VkWriteDescriptorSet *w = &writes[i];
        if (w->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        {
            if (w->dstBinding >= RLVK_MAX_TEXTURE_UNITS) continue;
            RLVK.pushedView[w->dstBinding]    = w->pImageInfo->imageView;
            RLVK.pushedSampler[w->dstBinding] = w->pImageInfo->sampler;
        }
        else if (w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        {
            if (w->dstBinding == (u32)RLVK_UBO_BINDING_VS)      RLVK.shadowUbo[0] = *w->pBufferInfo;
            else if (w->dstBinding == (u32)RLVK_UBO_BINDING_FS) RLVK.shadowUbo[1] = *w->pBufferInfo;
        }
    }
    RLVK.set0Dirty = true;
}

// Bind a snapshot of the set-0 shadow before a draw (no-op with native push descriptors)
static void rlvkFlushSet0(VkCommandBuffer cmdBuffer)
{
    if (RLVK.Caps.pushDescriptor || !RLVK.set0Dirty) return;

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkDescriptorSet ds = VK_NULL_HANDLE;
    VkResult res = vkAllocateDescriptorSets(RLVK.device,
        &(VkDescriptorSetAllocateInfo){
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = RLVK.descPools[frameIndex],
            .descriptorSetCount = 1,
            .pSetLayouts        = &RLVK.set0Layout,
        }, &ds);
    if (res != VK_SUCCESS)
    {
        // Pool exhausted: keep the previously bound set (stale textures beat a crash)
        TRACELOG(RL_LOG_WARNING, "RLVK: descriptor pool exhausted (VkResult %d) - raise RLVK_DESC_SETS_PER_FRAME", (int)res);
        RLVK.set0Dirty = false;
        return;
    }

    rlvkTextureSlot *def = &RLVK.textureSlots[RLVK.defaultTextureSlot];
    VkDescriptorImageInfo  imageInfos[RLVK_MAX_TEXTURE_UNITS];
    VkWriteDescriptorSet   writes[RLVK_SET0_BINDING_COUNT];
    u32 writeCount = 0;
    for (u32 b = 0; b < RLVK_MAX_TEXTURE_UNITS; b++)
    {
        // Unset shadow entries fall back to the default texture: every binding of the set
        // is valid regardless of which units the current shader statically uses
        imageInfos[b] = (VkDescriptorImageInfo){
            .sampler     = RLVK.pushedSampler[b] ? RLVK.pushedSampler[b] : def->sampler,
            .imageView   = RLVK.pushedView[b]    ? RLVK.pushedView[b]    : def->view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        writes[writeCount++] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = ds,
            .dstBinding      = b,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &imageInfos[b],
        };
    }
    for (u32 s = 0; s < 2; s++)
    {
        if (RLVK.shadowUbo[s].buffer == VK_NULL_HANDLE) continue;   // shader without a UBO never binds these
        writes[writeCount++] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = ds,
            .dstBinding      = s ? (u32)RLVK_UBO_BINDING_FS : (u32)RLVK_UBO_BINDING_VS,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &RLVK.shadowUbo[s],
        };
    }
    vkUpdateDescriptorSets(RLVK.device, writeCount, writes, 0, NULL);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, RLVK.pipelineLayout, 0, 1, &ds, 0, NULL);
    RLVK.set0Dirty = false;
}

// Push one texture at a GL-texture-unit binding of set 0
static void rlvkPushTexture(VkCommandBuffer cmdBuffer, u32 binding, u32 textureSlot)
{
    rlvkTextureSlot *t = &RLVK.textureSlots[textureSlot];
    // An attachment of the open scope can't be sampled (GL feedback loop, undefined there
    // too): substitute the default texture - same "undefined" class, but layout-legal
    if (t->currentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || !t->view)
        t = &RLVK.textureSlots[RLVK.defaultTextureSlot];
    // Skip the push when this binding already holds exactly this view+sampler (consecutive
    // batch draws almost always share one texture: font atlas, white texture, one material)
    if ((RLVK.pushedView[binding] == t->view) && (RLVK.pushedSampler[binding] == t->sampler)) return;
    RLVK.pushedView[binding] = t->view;
    RLVK.pushedSampler[binding] = t->sampler;
    vk.CmdPushDescriptorSetKHR(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, RLVK.pipelineLayout, 0, 1,
        &(VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding      = binding,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &(VkDescriptorImageInfo){
                .sampler     = t->sampler,
                .imageView   = t->view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
        });
}

// Initialize the pipeline layout and the embedded default shader
static bool rlvkInitDefaultShader(void)
{
    VkPushConstantRange pcRange = {
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(rlvkPushConstants) };

    // One pipeline layout shared by every pipeline: set 0 (texture units + per-stage UBOs)
    // plus the push-constant range. Uniform shader interfaces keep pipelines compatible.
    RLVK_CHECK(vkCreatePipelineLayout(RLVK.device,
        &(VkPipelineLayoutCreateInfo){
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount         = 1, .pSetLayouts            = &RLVK.set0Layout,
            .pushConstantRangeCount = 1, .pPushConstantRanges    = &pcRange,
        }, RLVK_ALLOC, &RLVK.pipelineLayout));

    u32 slot = rlvkAllocShaderSlot();
    if (slot == RLVK_INVALID_SLOT) return false;
    for (int i = 0; i < RL_MAX_SHADER_LOCATIONS; i++) RLVK.shaderSlots[slot].locs[i] = -1;

    // Shader modules from the embedded SPIR-V; the cached-pipeline draw path consumes modules
    // only (VkShaderEXT objects are no longer created since the pipeline pivot)
    vkCreateShaderModule(RLVK.device, &(VkShaderModuleCreateInfo){
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(rlvkDefaultVertSpv), .pCode = rlvkDefaultVertSpv,
    }, RLVK_ALLOC, &RLVK.shaderSlots[slot].vertMod);
    vkCreateShaderModule(RLVK.device, &(VkShaderModuleCreateInfo){
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(rlvkDefaultFragSpv), .pCode = rlvkDefaultFragSpv,
    }, RLVK_ALLOC, &RLVK.shaderSlots[slot].fragMod);

    // Default shader locations DrawMesh reads (decoded back in rlSetUniform*/rlSetVertexAttribute).
    // Everything left at -1 makes DrawMesh skip that uniform (matView/matModel/matNormal/etc.).
    int *L = RLVK.shaderSlots[slot].locs;
    L[SHADER_LOC_VERTEX_POSITION]   = RLVK_ALOC_POSITION;
    L[SHADER_LOC_VERTEX_TEXCOORD01] = RLVK_ALOC_TEXCOORD;
    L[SHADER_LOC_VERTEX_NORMAL]     = RLVK_ALOC_NORMAL;
    L[SHADER_LOC_VERTEX_COLOR]      = RLVK_ALOC_COLOR;
    L[SHADER_LOC_MATRIX_MVP]        = RLVK_ULOC_MVP;
    L[SHADER_LOC_COLOR_DIFFUSE]     = RLVK_ULOC_COLDIFFUSE;
    L[SHADER_LOC_MAP_DIFFUSE]       = RLVK_ULOC_TEXTURE0;

    // Embedded shader uses explicit layout locations 0/1/3 and push constants (usesUbo = false)
    rlvkShaderSlot *shader = &RLVK.shaderSlots[slot];
    for (int i = 0; i < RLVK_ATTRIB_COUNT; i++) shader->attribLocs[i] = -1;
    shader->attribLocs[RLVK_ATTRIB_POSITION] = 0;
    shader->attribLocs[RLVK_ATTRIB_TEXCOORD] = 1;
    shader->attribLocs[RLVK_ATTRIB_COLOR]    = 3;
    for (int i = 0; i < RLVK_MAX_TEXTURE_UNITS; i++) { shader->bindingUnit[i] = i; shader->bindingTexture[i] = 0; }

    RLVK.defaultShaderSlot = slot;
    return true;
}

// Pipeline cache management
//-----------------------------------------------------------------------------------------
// GL-equivalent state baked into VkPipelines keyed by rlvkPipelineKey, bound once per state
// combo; only viewport/scissor stay dynamic. The VkPipelineCache persists to disk across runs.

// Get the pipeline-cache file path (driver blob reused across runs for instant creation)
static const char *rlvkGetPipelineCachePath(void)
{
    static char path[512] = { 0 };
    if (path[0] == 0)
    {
        const char *tmp = getenv("TEMP");
        if (tmp == NULL) tmp = getenv("TMP");
        if (tmp == NULL) tmp = ".";
        snprintf(path, sizeof(path), "%s/raylib_rlvk.pipelinecache", tmp);
    }
    return path;
}

// Initialize the driver pipeline cache, seeding it with the previous run's data when present
static void rlvkInitPipelineCache(void)
{
    void *data = NULL;
    long dataSize = 0;
    FILE *file = fopen(rlvkGetPipelineCachePath(), "rb");
    if (file != NULL)
    {
        fseek(file, 0, SEEK_END);
        dataSize = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (dataSize > 0)
        {
            data = RL_MALLOC((size_t)dataSize);
            if (fread(data, 1, (size_t)dataSize, file) != (size_t)dataSize) { RL_FREE(data); data = NULL; dataSize = 0; }
        }
        fclose(file);
    }

    // The driver validates the blob header (UUID) itself and falls back to empty on mismatch
    RLVK_CHECK(vkCreatePipelineCache(RLVK.device,
        &(VkPipelineCacheCreateInfo){
            VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            .initialDataSize = (size_t)dataSize,
            .pInitialData    = data,
        }, RLVK_ALLOC, &RLVK.pipelineCache));
    if (data != NULL) RL_FREE(data);
    if (dataSize > 0) TRACELOG(RL_LOG_INFO, "RLVK: pipeline cache seeded from disk (%ld bytes)", dataSize);
}

// Save the driver pipeline cache to disk so the next run creates pipelines instantly
static void rlvkSavePipelineCache(void)
{
    if (RLVK.pipelineCache == VK_NULL_HANDLE) return;

    size_t dataSize = 0;
    vkGetPipelineCacheData(RLVK.device, RLVK.pipelineCache, &dataSize, NULL);
    if (dataSize == 0) return;

    void *data = RL_MALLOC(dataSize);
    if (vkGetPipelineCacheData(RLVK.device, RLVK.pipelineCache, &dataSize, data) == VK_SUCCESS)
    {
        FILE *file = fopen(rlvkGetPipelineCachePath(), "wb");
        if (file != NULL)
        {
            fwrite(data, 1, dataSize, file);
            fclose(file);
            TRACELOG(RL_LOG_INFO, "RLVK: pipeline cache saved to disk (%llu bytes)", (ull)dataSize);
        }
    }
    RL_FREE(data);
}

// Get the blend attachment state for a pipeline key (same factor mapping as the GL backend)
static VkPipelineColorBlendAttachmentState rlvkGetBlendAttachmentState(const rlvkPipelineKey *key, bool blendEnabled)
{
    VkPipelineColorBlendAttachmentState state = {
        .blendEnable         = blendEnabled? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    switch (key->blendMode)
    {
        case RL_BLEND_ALPHA: break;     // defaults above
        case RL_BLEND_ADDITIVE:
            state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case RL_BLEND_MULTIPLIED:
            state.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR; state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_COLOR; state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case RL_BLEND_ADD_COLORS:
            state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case RL_BLEND_SUBTRACT_COLORS:
            state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.colorBlendOp = VK_BLEND_OP_SUBTRACT; state.alphaBlendOp = VK_BLEND_OP_SUBTRACT;
            break;
        case RL_BLEND_ALPHA_PREMULTIPLY:
            state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case RL_BLEND_CUSTOM:
            state.srcColorBlendFactor = rlvkBlendFactorFromGL(key->blendSrcRGB);
            state.dstColorBlendFactor = rlvkBlendFactorFromGL(key->blendDstRGB);
            state.colorBlendOp        = rlvkBlendOpFromGL(key->blendEqRGB);
            state.srcAlphaBlendFactor = state.srcColorBlendFactor;
            state.dstAlphaBlendFactor = state.dstColorBlendFactor;
            state.alphaBlendOp        = state.colorBlendOp;
            break;
        case RL_BLEND_CUSTOM_SEPARATE:
            state.srcColorBlendFactor = rlvkBlendFactorFromGL(key->blendSrcRGB);
            state.dstColorBlendFactor = rlvkBlendFactorFromGL(key->blendDstRGB);
            state.colorBlendOp        = rlvkBlendOpFromGL(key->blendEqRGB);
            state.srcAlphaBlendFactor = rlvkBlendFactorFromGL(key->blendSrcA);
            state.dstAlphaBlendFactor = rlvkBlendFactorFromGL(key->blendDstA);
            state.alphaBlendOp        = rlvkBlendOpFromGL(key->blendEqA);
            break;
        default: break;
    }
    return state;
}

// Dummy-broadcast table for shader-declared attributes the layout doesn't feed: a pipeline
// must feed every consumed location, so uncovered ones get divisor-0 broadcasts (GL's generic
// vertex defaults); INSTANCE_TX exempt (instancing shaders always draw instanced in raylib)
static const struct { unsigned char attrib; unsigned char dummyOffset; VkFormat format; } rlvkDummyAttribs[] = {
    { RLVK_ATTRIB_TEXCOORD,     0, VK_FORMAT_R32G32_SFLOAT       },  // vec2(0,0)
    { RLVK_ATTRIB_NORMAL,      12, VK_FORMAT_R32G32B32_SFLOAT    },  // +Z
    { RLVK_ATTRIB_COLOR,        8, VK_FORMAT_R8G8B8A8_UNORM      },  // opaque white
    { RLVK_ATTRIB_TANGENT,     28, VK_FORMAT_R32G32B32A32_SFLOAT },  // (0,0,0,1) GL default
    { RLVK_ATTRIB_TEXCOORD2,    0, VK_FORMAT_R32G32_SFLOAT       },  // vec2(0,0)
    { RLVK_ATTRIB_BONEIDS,     24, VK_FORMAT_R8G8B8A8_USCALED    },  // (0,0,0,1)
    { RLVK_ATTRIB_BONEWEIGHTS, 28, VK_FORMAT_R32G32B32A32_SFLOAT },  // (0,0,0,1)
};

// Which canonical attributes a vertex layout feeds, and the first binding index free for
// dummy broadcasts. Must mirror rlvkBuildVertexInput's binding assignment exactly.
static unsigned int rlvkLayoutCoverage(unsigned short vertexLayout, u32 *firstFreeBinding)
{
    if (vertexLayout == RLVK_VLAYOUT_NONE) { *firstFreeBinding = 0; return 0xFFFFFFFFu; }
    if (vertexLayout == RLVK_VLAYOUT_QUAD)
    {
        *firstFreeBinding = 1;
        return (1u << RLVK_ATTRIB_POSITION) | (1u << RLVK_ATTRIB_TEXCOORD);
    }
    unsigned int covered = (1u << RLVK_ATTRIB_POSITION) | (1u << RLVK_ATTRIB_TEXCOORD) |
                           (1u << RLVK_ATTRIB_NORMAL) | (1u << RLVK_ATTRIB_COLOR);
    if (vertexLayout == RLVK_VLAYOUT_BATCH) { *firstFreeBinding = 4; return covered; }

    u32 next = 4;
    if (vertexLayout & RLVK_VLAYOUT_MESH_INSTANCED) { covered |= (1u << RLVK_ATTRIB_INSTANCE_TX); next = 5; }
    else if (vertexLayout & (RLVK_VLAYOUT_MESH_BONES | RLVK_VLAYOUT_MESH_BONES_DUMMY))
    {
        covered |= (1u << RLVK_ATTRIB_BONEIDS) | (1u << RLVK_ATTRIB_BONEWEIGHTS);
        next = 6;
    }
    else
    {
        if (vertexLayout & RLVK_VLAYOUT_MESH_UV2)     { covered |= (1u << RLVK_ATTRIB_TEXCOORD2); next++; }
        if (vertexLayout & RLVK_VLAYOUT_MESH_TANGENT) { covered |= (1u << RLVK_ATTRIB_TANGENT);   next++; }
    }
    *firstFreeBinding = next;
    return covered;
}

// Append stride-0 dummy broadcasts for every shader-declared attribute the layout leaves
// unfed. Core Vulkan 1.0 semantics: stride 0 at VERTEX rate re-reads the same bytes for
// every vertex (address = offset + index*0), replacing the old zero-divisor trick that
// needed VK_EXT_vertex_attribute_divisor. NOTE: MoltenVK's portability subset rejects
// stride < format size - revisit only if macOS-over-Vulkan ever becomes a target.
static void rlvkAppendDummyAttribs(unsigned short vertexLayout, rlvkShaderSlot *shader,
    VkVertexInputBindingDescription *binds, u32 *bindCount,
    VkVertexInputAttributeDescription *attrs, u32 *attrCount)
{
    u32 dummyBinding = 0;
    unsigned int covered = rlvkLayoutCoverage(vertexLayout, &dummyBinding);
    for (int i = 0; i < (int)RLVK_COUNTOF(rlvkDummyAttribs); i++)
    {
        int idx = rlvkDummyAttribs[i].attrib;
        if ((covered & (1u << idx)) || (shader->attribLocs[idx] < 0)) continue;
        binds[*bindCount] = (VkVertexInputBindingDescription){ .binding = dummyBinding, .stride = 0, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[idx], .binding = dummyBinding, .format = rlvkDummyAttribs[i].format };
        (*bindCount)++; dummyBinding++;
    }
}

// Build a pipeline key's static vertex-input state: batch streams at fixed strides, mesh
// streams with divisor-0 broadcasts for missing attributes, extra bindings for uv2/tangent/
// bones/instancing, plus broadcasts for any shader-declared attributes left unfed
static void rlvkBuildVertexInput(const rlvkPipelineKey *key,
    VkVertexInputBindingDescription *binds, u32 *bindCount,
    VkVertexInputAttributeDescription *attrs, u32 *attrCount)
{
    *bindCount = 0; *attrCount = 0;
    if (key->vertexLayout == RLVK_VLAYOUT_NONE) return;

    rlvkShaderSlot *shader = &RLVK.shaderSlots[key->shaderSlot];

    if (key->vertexLayout == RLVK_VLAYOUT_QUAD)
    {
        // One interleaved pos3+uv2 binding (rlLoadDrawQuad)
        binds[0] = (VkVertexInputBindingDescription){ .binding = 0, .stride = 5*sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
        *bindCount = 1;
        if (shader->attribLocs[RLVK_ATTRIB_POSITION] >= 0) attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_POSITION], .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 };
        if (shader->attribLocs[RLVK_ATTRIB_TEXCOORD] >= 0) attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_TEXCOORD], .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 3*sizeof(f32) };
        rlvkAppendDummyAttribs(key->vertexLayout, shader, binds, bindCount, attrs, attrCount);
        return;
    }

    if (key->vertexLayout == RLVK_VLAYOUT_BATCH)
    {
        // Batch streams: pos | uv | normal | color at fixed strides, subset by the shader
        binds[0] = (VkVertexInputBindingDescription){ .binding = 0, .stride = 3*sizeof(f32),         .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
        binds[1] = (VkVertexInputBindingDescription){ .binding = 1, .stride = 2*sizeof(f32),         .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
        binds[2] = (VkVertexInputBindingDescription){ .binding = 2, .stride = 3*sizeof(f32),         .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
        binds[3] = (VkVertexInputBindingDescription){ .binding = 3, .stride = 4*sizeof(unsigned char), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
        *bindCount = 4;
        if (shader->attribLocs[RLVK_ATTRIB_POSITION] >= 0) attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_POSITION], .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT };
        if (shader->attribLocs[RLVK_ATTRIB_TEXCOORD] >= 0) attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_TEXCOORD], .binding = 1, .format = VK_FORMAT_R32G32_SFLOAT };
        if (shader->attribLocs[RLVK_ATTRIB_NORMAL]   >= 0) attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_NORMAL],   .binding = 2, .format = VK_FORMAT_R32G32B32_SFLOAT };
        if (shader->attribLocs[RLVK_ATTRIB_COLOR]    >= 0) attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_COLOR],    .binding = 3, .format = VK_FORMAT_R8G8B8A8_UNORM };
        rlvkAppendDummyAttribs(key->vertexLayout, shader, binds, bindCount, attrs, attrCount);
        return;
    }

    // Mesh layout: presence mask selects real streams vs divisor-0 dummy broadcasts
    bool hasUV     = (key->vertexLayout & RLVK_VLAYOUT_MESH_UV) != 0;
    bool hasNormal = (key->vertexLayout & RLVK_VLAYOUT_MESH_NORMAL) != 0;
    bool hasColor  = (key->vertexLayout & RLVK_VLAYOUT_MESH_COLOR) != 0;

    binds[0] = (VkVertexInputBindingDescription){ .binding = 0, .stride = 3*sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    binds[1] = (VkVertexInputBindingDescription){ .binding = 1, .stride = hasUV? 2*(u32)sizeof(f32) : 0u,
                                                  .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };   // !hasUV: stride-0 broadcast
    binds[2] = (VkVertexInputBindingDescription){ .binding = 2, .stride = hasNormal? 3*(u32)sizeof(f32) : 0u,
                                                  .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };   // !hasNormal: stride-0 broadcast
    binds[3] = (VkVertexInputBindingDescription){ .binding = 3, .stride = hasColor? 4*(u32)sizeof(unsigned char) : 0u,
                                                  .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };   // !hasColor: stride-0 broadcast
    *bindCount = 4;

    if (shader->attribLocs[RLVK_ATTRIB_POSITION] >= 0) attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_POSITION], .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT };
    if (shader->attribLocs[RLVK_ATTRIB_TEXCOORD] >= 0) attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_TEXCOORD], .binding = 1, .format = VK_FORMAT_R32G32_SFLOAT };
    if (shader->attribLocs[RLVK_ATTRIB_NORMAL]   >= 0) attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_NORMAL],   .binding = 2, .format = VK_FORMAT_R32G32B32_SFLOAT };
    if (shader->attribLocs[RLVK_ATTRIB_COLOR]    >= 0) attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_COLOR],    .binding = 3, .format = VK_FORMAT_R8G8B8A8_UNORM };

    // Sequential extra bindings, same assignment order as the dynamic path
    u32 nextBinding = 4;
    if (key->vertexLayout & RLVK_VLAYOUT_MESH_UV2)
    {
        binds[*bindCount] = (VkVertexInputBindingDescription){ .binding = nextBinding, .stride = 2*sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_TEXCOORD2], .binding = nextBinding, .format = VK_FORMAT_R32G32_SFLOAT };
        (*bindCount)++; nextBinding++;
    }
    if (key->vertexLayout & RLVK_VLAYOUT_MESH_TANGENT)
    {
        binds[*bindCount] = (VkVertexInputBindingDescription){ .binding = nextBinding, .stride = 4*sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_TANGENT], .binding = nextBinding, .format = VK_FORMAT_R32G32B32A32_SFLOAT };
        (*bindCount)++; nextBinding++;
    }
    if (key->vertexLayout & (RLVK_VLAYOUT_MESH_BONES | RLVK_VLAYOUT_MESH_BONES_DUMMY))
    {
        // Real bone streams, or divisor-0 dummy broadcasts of GL's attribute defaults
        bool realBones = (key->vertexLayout & RLVK_VLAYOUT_MESH_BONES) != 0;
        binds[*bindCount] = (VkVertexInputBindingDescription){ .binding = 4, .stride = realBones? 4u : 0u,
                                                               .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };   // !realBones: stride-0 broadcast
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_BONEIDS], .binding = 4, .format = VK_FORMAT_R8G8B8A8_USCALED };
        (*bindCount)++;
        binds[*bindCount] = (VkVertexInputBindingDescription){ .binding = 5, .stride = realBones? 4*(u32)sizeof(f32) : 0u,
                                                               .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };   // !realBones: stride-0 broadcast
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = (u32)shader->attribLocs[RLVK_ATTRIB_BONEWEIGHTS], .binding = 5, .format = VK_FORMAT_R32G32B32A32_SFLOAT };
        (*bindCount)++;
    }
    if (key->vertexLayout & RLVK_VLAYOUT_MESH_INSTANCED)
    {
        // mat4 instanceTransform: four vec4 columns at consecutive locations, instance rate
        u32 base = (u32)shader->attribLocs[RLVK_ATTRIB_INSTANCE_TX];
        binds[*bindCount] = (VkVertexInputBindingDescription){ .binding = 4, .stride = 16*sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE };
        for (u32 c = 0; c < 4; c++)
            attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){ .location = base + c, .binding = 4, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = c*4*sizeof(f32) };
        (*bindCount)++;
    }

    rlvkAppendDummyAttribs(key->vertexLayout, shader, binds, bindCount, attrs, attrCount);
}

// Bind the dummy attribute buffer at every dummy-broadcast binding the pipeline layout
// implies for this shader (mirrors rlvkAppendDummyAttribs' binding assignment)
static void rlvkBindDummyAttribBuffers(VkCommandBuffer cmdBuffer, unsigned short vertexLayout, rlvkShaderSlot *shader)
{
    u32 dummyBinding = 0;
    unsigned int covered = rlvkLayoutCoverage(vertexLayout, &dummyBinding);
    VkBuffer dummyBufs[8]; VkDeviceSize dummyOffs[8]; u32 dummyCount = 0;
    for (int i = 0; i < (int)RLVK_COUNTOF(rlvkDummyAttribs); i++)
    {
        int idx = rlvkDummyAttribs[i].attrib;
        if ((covered & (1u << idx)) || (shader->attribLocs[idx] < 0)) continue;
        dummyBufs[dummyCount] = RLVK.bufferSlots[RLVK.dummyAttribSlot].buffer;
        dummyOffs[dummyCount] = rlvkDummyAttribs[i].dummyOffset;
        dummyCount++;
    }
    if (dummyCount > 0) vkCmdBindVertexBuffers(cmdBuffer, dummyBinding, dummyCount, dummyBufs, dummyOffs);
}

// Build a monolithic pipeline for a state key (through the disk-backed driver cache)
static VkPipeline rlvkBuildPipeline(const rlvkPipelineKey *key)
{
    VkShaderModule vertMod = RLVK.shaderSlots[key->shaderSlot].vertMod;
    VkShaderModule fragMod = RLVK.shaderSlots[key->shaderSlot].fragMod;
    if (vertMod == VK_NULL_HANDLE || fragMod == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    VkPipelineShaderStageCreateInfo stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,   .module = vertMod, .pName = "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragMod, .pName = "main" },
    };

    VkVertexInputBindingDescription binds[12];
    VkVertexInputAttributeDescription attrs[16];
    u32 bindCount = 0, attrCount = 0;
    rlvkBuildVertexInput(key, binds, &bindCount, attrs, &attrCount);

    VkPipelineVertexInputStateCreateInfo vertexInput = {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = bindCount,
        .pVertexBindingDescriptions      = binds,
        .vertexAttributeDescriptionCount = attrCount,
        .pVertexAttributeDescriptions    = attrs,
    };

    bool blendEnabled = (key->blendMode >= 0);   // negative blendMode encodes blend-disabled
    VkPipelineColorBlendAttachmentState blendAttachments[8];
    for (u32 a = 0; a < key->colorCount && a < 8; a++)
    {
        // Float attachments never blend (GL treats their undefined alpha as 1.0)
        VkFormat fmt = key->colorFormats[a];
        bool isFloat = (fmt == VK_FORMAT_R16G16B16A16_SFLOAT) || (fmt == VK_FORMAT_R32G32B32A32_SFLOAT) ||
                       (fmt == VK_FORMAT_R16_SFLOAT) || (fmt == VK_FORMAT_R32_SFLOAT);
        blendAttachments[a] = rlvkGetBlendAttachmentState(key, blendEnabled && !isFloat);
    }
    if (rlvkDebugFlag("RLVK_DEBUG_PIPE", &s_dbgPipe)) TRACELOG(RL_LOG_WARNING,
        "VKDBG pipeline build: shader=%u layout=0x%X topo=%d samples=%d colors=%d blend=%d attrs=%u binds=%u",
        key->shaderSlot, key->vertexLayout, key->topology, key->samples, key->colorCount, key->blendMode, attrCount, bindCount);

    // Canonical compatibility pass for this pipeline: load/store ops never affect render-pass
    // compatibility, so a LOAD-ops pass of the same shape (formats/samples/counts/resolve)
    // matches every scope this state combo can draw into
    rlvkRenderPassKey rpKey;
    memset(&rpKey, 0, sizeof(rpKey));
    for (u32 a = 0; a < key->colorCount && a < 8; a++) rpKey.colorFormats[a] = key->colorFormats[a];
    rpKey.depthFormat = key->depthFormat;
    rpKey.colorCount  = key->colorCount;
    rpKey.samples     = (key->samples > 1)? 4 : 1;
    rpKey.colorLoad   = VK_ATTACHMENT_LOAD_OP_LOAD;
    rpKey.depthLoad   = VK_ATTACHMENT_LOAD_OP_LOAD;
    rpKey.depthStore  = VK_ATTACHMENT_STORE_OP_STORE;
    rpKey.hasResolve  = (key->samples > 1)? 1 : 0;   // MSAA scopes always resolve into the 1x intermediate
    VkRenderPass compatPass = rlvkGetRenderPass(&rpKey);

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(RLVK.device, RLVK.pipelineCache, 1,
        &(VkGraphicsPipelineCreateInfo){
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount          = 2,
            .pStages             = stages,
            .pVertexInputState   = &vertexInput,
            .pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo){
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = (key->topology == 0)? VK_PRIMITIVE_TOPOLOGY_LINE_LIST :
                            (key->topology == 2)? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            },
            .pViewportState      = &(VkPipelineViewportStateCreateInfo){
                VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                // GL-style [-1,1] clip-z is remapped to [0,1] by a vertex-shader epilogue on
                // EVERY device (embedded default shader has it baked; the shaderc path injects
                // it) - one behavior everywhere instead of a depth_clip_control split
                .viewportCount = 1,     // set dynamically (VK_DYNAMIC_STATE_VIEWPORT/SCISSOR)
                .scissorCount  = 1,
            },
            .pRasterizationState = &(VkPipelineRasterizationStateCreateInfo){
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                // Bresenham matches GL's aliased 1px lines; MSAA uses rectangular (GL smooth)
                // lines. Without the extension: implementation-default lines, cosmetic delta.
                RLVK.Caps.bresenhamLines? (const void *)&(VkPipelineRasterizationLineStateCreateInfoEXT){
                    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT,
                    .lineRasterizationMode = (key->samples > 1)? VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT : VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT,
                } : NULL,
                .polygonMode = (VkPolygonMode)key->polygonMode,
                .cullMode    = (VkCullModeFlags)key->cullMode,
                .frontFace   = VK_FRONT_FACE_CLOCKWISE,     // unmirrored rendering: CW everywhere
                .lineWidth   = 1.0f,
            },
            .pMultisampleState   = &(VkPipelineMultisampleStateCreateInfo){
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = (key->samples > 1)? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
            },
            .pDepthStencilState  = &(VkPipelineDepthStencilStateCreateInfo){
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable  = key->depthTest? VK_TRUE : VK_FALSE,
                .depthWriteEnable = key->depthWrite? VK_TRUE : VK_FALSE,
                .depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL,
            },
            .pColorBlendState    = &(VkPipelineColorBlendStateCreateInfo){
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = key->colorCount,
                .pAttachments    = blendAttachments,
            },
            .pDynamicState       = &(VkPipelineDynamicStateCreateInfo){
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = 2,
                .pDynamicStates    = (VkDynamicState[]){
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                },
            },
            .layout              = RLVK.pipelineLayout,
            .renderPass          = compatPass,
            .subpass             = 0,
        }, RLVK_ALLOC, &pipeline);

    if (result != VK_SUCCESS) TRACELOG(RL_LOG_WARNING, "RLVK: vkCreateGraphicsPipelines => %d", (int)result);
    return pipeline;
}

// Get (or build and cache) the pipeline for a state key
static VkPipeline rlvkGetPipeline(const rlvkPipelineKey *key)
{
    for (int i = 0; i < RLVK.pipelineCount; i++)
        if (memcmp(&RLVK.pipelines[i].key, key, sizeof(rlvkPipelineKey)) == 0) return RLVK.pipelines[i].pipeline;

    if (RLVK.pipelineCount >= RLVK_MAX_PIPELINES)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: pipeline cache table full - raise RLVK_MAX_PIPELINES");
        return VK_NULL_HANDLE;
    }

    VkPipeline pipeline = rlvkBuildPipeline(key);
    if (pipeline == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    RLVK.pipelines[RLVK.pipelineCount].key = *key;
    RLVK.pipelines[RLVK.pipelineCount].pipeline = pipeline;
    RLVK.pipelineCount++;
    return pipeline;
}

// Build the pipeline key for the CURRENT scope and GL-style state, bind the cached pipeline
// (skipping redundant binds) and set the remaining dynamic state (viewport + scissor)
static bool rlvkBindPipeline(VkCommandBuffer cmdBuffer, unsigned char topology, unsigned short vertexLayout, u32 shaderSlot)
{
    // Fast path: nothing feeding the key or viewport changed since the previous draw, so the
    // bound pipeline and dynamic state are still exactly right (the common case in batch loops)
    if (s_pipelineFastValid && (RLVK.State.stateGeneration == s_lastGeneration) &&
        (shaderSlot == s_lastShaderSlot) && (vertexLayout == s_lastVertexLayout) && (topology == s_lastTopology))
        return true;

    // Depth presence mirrors the scope-open logic: the swapchain scope always attaches depth,
    // an FBO scope only when the framebuffer has a live depth texture
    bool scopeHasDepth = true;
    if (RLVK.scope.fbSlot != 0)
    {
        rlvkFramebufferSlot *fb = &RLVK.fbSlots[RLVK.scope.fbSlot];
        scopeHasDepth = fb->hasDepth && (RLVK.textureSlots[fb->depthTexture].image != VK_NULL_HANDLE);
    }

    rlvkPipelineKey key = { 0 };    // zero-init: memcmp-comparable, no padding garbage
    for (u32 a = 0; a < RLVK.scope.colorCount && a < 8; a++) key.colorFormats[a] = RLVK.scope.colorFormats[a];
    key.depthFormat  = scopeHasDepth? RLVK.depthFormat : VK_FORMAT_UNDEFINED;
    key.shaderSlot   = shaderSlot;
    key.topology     = topology;
    key.vertexLayout = vertexLayout;
    key.cullMode     = (unsigned char)(RLVK.State.cullEnabled?
                           ((RLVK.State.cullMode == RL_CULL_FACE_FRONT)? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT) :
                           VK_CULL_MODE_NONE);
    // Wire/point fill modes need the optional fillModeNonSolid feature; without it (some
    // mobile GPUs) they degrade to solid fill instead of failing pipeline creation
    key.polygonMode  = (unsigned char)((RLVK.Caps.fillModeNonSolid && RLVK.State.pointMode)? VK_POLYGON_MODE_POINT :
                                       (RLVK.Caps.fillModeNonSolid && RLVK.State.wireMode)?  VK_POLYGON_MODE_LINE  : VK_POLYGON_MODE_FILL);
    key.samples      = (unsigned char)RLVK.scope.samples;
    key.colorCount   = (unsigned char)RLVK.scope.colorCount;
    key.depthTest    = RLVK.State.depthTest? 1 : 0;
    key.depthWrite   = RLVK.State.depthWrite? 1 : 0;
    key.blendMode    = RLVK.State.colorBlendEnabled? RLVK.State.blendMode : -1;    // -1 = blending off
    if (RLVK.State.blendMode == RL_BLEND_CUSTOM)
    {
        key.blendSrcRGB = RLVK.State.blendSrc; key.blendDstRGB = RLVK.State.blendDst; key.blendEqRGB = RLVK.State.blendEq;
    }
    else if (RLVK.State.blendMode == RL_BLEND_CUSTOM_SEPARATE)
    {
        key.blendSrcRGB = RLVK.State.blendSrcRGB; key.blendDstRGB = RLVK.State.blendDstRGB; key.blendEqRGB = RLVK.State.blendEqRGB;
        key.blendSrcA   = RLVK.State.blendSrcA;   key.blendDstA   = RLVK.State.blendDstA;   key.blendEqA   = RLVK.State.blendEqA;
    }

    VkPipeline pipeline = rlvkGetPipeline(&key);
    if (pipeline == VK_NULL_HANDLE) return false;

    if (pipeline != RLVK.boundPipeline)
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        RLVK.boundPipeline = pipeline;
    }

    // Viewport and scissor stay dynamic (the only non-baked state); skip both commands when
    // nothing they read has changed since the previous draw in this command buffer
    rlvkViewportSig vpSig; memset(&vpSig, 0, sizeof(vpSig));
    vpSig.vx = RLVK.State.viewportX; vpSig.vy = RLVK.State.viewportY;
    vpSig.vw = RLVK.State.viewportW; vpSig.vh = RLVK.State.viewportH;
    vpSig.scEn = RLVK.State.scissorEnabled? 1 : 0;
    vpSig.scx = RLVK.State.scissorX; vpSig.scy = RLVK.State.scissorY;
    vpSig.scw = RLVK.State.scissorW; vpSig.sch = RLVK.State.scissorH;
    vpSig.scopeW = (int)RLVK.scope.width; vpSig.scopeH = (int)RLVK.scope.height;
    vpSig.flipY = RLVK.scope.flipY? 1 : 0;
    s_pipelineFastValid = true;
    s_lastGeneration    = RLVK.State.stateGeneration;
    s_lastShaderSlot    = shaderSlot;
    s_lastVertexLayout  = vertexLayout;
    s_lastTopology      = topology;

    if (s_viewportValid && (memcmp(&vpSig, &s_viewportSig, sizeof(vpSig)) == 0)) return true;
    s_viewportSig = vpSig; s_viewportValid = true;

    f32 vx = (f32)RLVK.State.viewportX, vy = (f32)RLVK.State.viewportY;
    f32 vw = (f32)RLVK.State.viewportW, vh = (f32)RLVK.State.viewportH;
    if (vw <= 0.0f || vh <= 0.0f) { vw = (f32)RLVK.scope.width; vh = (f32)RLVK.scope.height; }
    if (RLVK.scope.flipY) vkCmdSetViewport(cmdBuffer, 0, 1, &(VkViewport){ vx, vy + vh, vw, -vh, 0.0f, 1.0f });
    else                  vkCmdSetViewport(cmdBuffer, 0, 1, &(VkViewport){ vx, vy, vw, vh, 0.0f, 1.0f });

    if (RLVK.State.scissorEnabled)
    {
        int sx = RLVK.State.scissorX;
        int sy = RLVK.scope.flipY? ((int)RLVK.scope.height - (RLVK.State.scissorY + RLVK.State.scissorH)) : RLVK.State.scissorY;
        int scw = RLVK.State.scissorW, sch = RLVK.State.scissorH;
        if (sx < 0) { scw += sx; sx = 0; }
        if (sy < 0) { sch += sy; sy = 0; }
        if (sx + scw > (int)RLVK.scope.width)  scw = (int)RLVK.scope.width - sx;
        if (sy + sch > (int)RLVK.scope.height) sch = (int)RLVK.scope.height - sy;
        if (scw < 0) scw = 0;
        if (sch < 0) sch = 0;
        vkCmdSetScissor(cmdBuffer, 0, 1, &(VkRect2D){ { sx, sy }, { (u32)scw, (u32)sch } });
    }
    else vkCmdSetScissor(cmdBuffer, 0, 1, &(VkRect2D){ { 0, 0 }, { RLVK.scope.width, RLVK.scope.height } });

    return true;
}

// Y-flip the unmirrored intermediate into the swapchain image with an exact row-swap blit
// (NEAREST, same size); under MSAA the intermediate holds the fixed-function resolve output.
// Leaves the swapchain in COLOR_ATTACHMENT_OPTIMAL for present/readback.
static void rlvkFlipToSwapchain(VkCommandBuffer cmdBuffer)
{
    u32 frameIndex         = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    u32 imageIndex = RLVK.currentImageIndex;
    int w = (int)RLVK.swapchainExtent.width, h = (int)RLVK.swapchainExtent.height;

    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
            {   VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask    = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .dstAccessMask    = VK_ACCESS_2_TRANSFER_READ_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .image            = RLVK.interImage[frameIndex],
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            },
            {   VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .dstAccessMask    = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .image            = RLVK.swapchainImages[imageIndex],
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            },
        },
    });

    vk.CmdBlitImage(cmdBuffer, RLVK.interImage[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        RLVK.swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
        &(VkImageBlit){
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .srcOffsets     = { { 0, h, 0 }, { w, 0, 1 } },   // Y-mirrored source = row swap
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstOffsets     = { { 0, 0, 0 }, { w, h, 1 } },
        }, VK_FILTER_NEAREST);

    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask     = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask    = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            // Access mask matches the LAYOUT (BestPractices-ImageBarrierAccessLayout): the
            // readback path performs its own COLOR_ATTACHMENT -> TRANSFER_SRC transition
            .dstStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask    = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image            = RLVK.swapchainImages[imageIndex],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        },
    });
}

// After the last scope of the frame closes: get the frame's content into the swapchain image
// (exact flip blit; under MSAA the intermediate was already resolved at scope close),
// COLOR_ATTACHMENT layout on exit.
static void rlvkFinishSwapchainImage(VkCommandBuffer cmdBuffer)
{
    // GPU trace: scene-end stamp before the present chain, present-end stamp after it
    u32 gpuFrame = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    bool gpuTrace = rlvkDebugFlag("RLVK_GPU_TRACE", &s_dbgGpu) && (s_gpuPool != VK_NULL_HANDLE);
    if (gpuTrace) vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, s_gpuPool, gpuFrame*3 + 1);

    rlvkFlipToSwapchain(cmdBuffer);

    if (gpuTrace) vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, s_gpuPool, gpuFrame*3 + 2);
}

// Initialize the per-frame ring: command pools/buffers, semaphores and fences
static bool rlvkInitFrameRing(void)
{
    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        // One pool per frame-in-flight holding a single command buffer: the whole POOL is
        // reset each frame (vkResetCommandPool), which is cheaper than per-buffer reset and
        // avoids the BestPractices command-buffer-reset warning
        RLVK_CHECK(vkCreateCommandPool(RLVK.device,
            &(VkCommandPoolCreateInfo){
                VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .queueFamilyIndex = RLVK.graphicsFamily,
            }, RLVK_ALLOC, &RLVK.cmdPools[i]));

        RLVK_CHECK(vkAllocateCommandBuffers(RLVK.device,
            &(VkCommandBufferAllocateInfo){
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool        = RLVK.cmdPools[i],
                .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            }, &RLVK.cmdBuffers[i]));

        // Pool-ring fallback (no push descriptors): one descriptor pool per frame slot,
        // reset together with the slot's command pool at its fence
        if (!RLVK.Caps.pushDescriptor)
        {
            RLVK_CHECK(vkCreateDescriptorPool(RLVK.device,
                &(VkDescriptorPoolCreateInfo){
                    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                    .maxSets       = RLVK_DESC_SETS_PER_FRAME,
                    .poolSizeCount = 2,
                    .pPoolSizes    = (VkDescriptorPoolSize[]){
                        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RLVK_DESC_SETS_PER_FRAME*RLVK_MAX_TEXTURE_UNITS },
                        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         RLVK_DESC_SETS_PER_FRAME*2 },
                    },
                }, RLVK_ALLOC, &RLVK.descPools[i]));
        }
    }

    // Frame pacing is fence-based; no timeline semaphore is created (the original design's
    // timeline pacing was replaced by per-slot fences)
    return true;
}

// Create a persistently-mapped host-visible buffer backing a render batch
static bool rlvkCreateBatchBacking(int bufferElements, rlvkBatchBackingBuffer *out)
{
    size_t posBytes = (size_t)bufferElements*3*4*sizeof(f32);
    size_t uvBytes  = (size_t)bufferElements*2*4*sizeof(f32);
    size_t nrmBytes = (size_t)bufferElements*3*4*sizeof(f32);
    size_t colBytes = (size_t)bufferElements*4*4*sizeof(unsigned char);
    size_t idxBytes = (size_t)bufferElements*6*sizeof(unsigned int);
    VkDeviceSize total = (VkDeviceSize)(posBytes + uvBytes + nrmBytes + colBytes + idxBytes);

    RLVK_CHECK(vkCreateBuffer(RLVK.device,
        &(VkBufferCreateInfo){
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size  = total,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                   | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                   | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                   | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                   | VK_BUFFER_USAGE_TRANSFER_SRC_BIT      // rlUpdateTexture stages in-frame texture updates here
                  ,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        }, RLVK_ALLOC, &out->buffer));

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(RLVK.device, out->buffer, &memReq);

    out->memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    RLVK_CHECK(vkBindBufferMemory(RLVK.device, out->buffer, out->memory, 0));
    RLVK_CHECK(vkMapMemory(RLVK.device, out->memory, 0, total, 0, &out->mapped));

    out->sizeBytes = (u32)total;
    return true;
}

// Unmap and release a batch backing buffer
static void rlvkDestroyBatchBacking(rlvkBatchBackingBuffer *b)
{
    if (b->memory) { vkUnmapMemory(RLVK.device, b->memory); vkFreeMemory(RLVK.device, b->memory, RLVK_ALLOC); }
    if (b->buffer) vkDestroyBuffer(RLVK.device, b->buffer, RLVK_ALLOC);
    memset(b, 0, sizeof(*b));
}

static u32 rlvkFindMemoryType(u32 typeBits, VkMemoryPropertyFlags wanted)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(RLVK.physicalDevice, &props);
    for (u32 i = 0; i < props.memoryTypeCount; i++)
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & wanted) == wanted)
            return i;
    return UINT32_MAX;
}

// Allocate memory for a resource: memory-type selection and the memory-priority hint
// (NVIDIA best practice, when VK_EXT_memory_priority is available) handled in ONE place
// for every allocation in the backend

static VkDeviceMemory rlvkAllocMemory(VkMemoryRequirements memReq, VkMemoryPropertyFlags props)
{
    // Host-visible allocations prefer HOST_CACHED: cached memory takes the CPU's constant
    // streaming writes several times faster than write-combined; GPU reads are identical
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        if (rlvkFindMemoryType(memReq.memoryTypeBits, props | VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != UINT32_MAX)
            props |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }

    VkMemoryPriorityAllocateInfoEXT priorityInfo = {
        VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT,
        .priority = (props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)? 1.0f : 0.5f,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    RLVK_CHECK(vkAllocateMemory(RLVK.device,
        &(VkMemoryAllocateInfo){
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            RLVK.Caps.memoryPriority? (void *)&priorityInfo : NULL,
            .allocationSize  = memReq.size,
            .memoryTypeIndex = rlvkFindMemoryType(memReq.memoryTypeBits, props),
        }, RLVK_ALLOC, &memory));
    if (props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) s_memLocalBytes += (ill)memReq.size; else s_memHostBytes += (ill)memReq.size;
    s_memAllocCount++;
    return memory;
}

static u32 rlvkAllocTextureSlot(void)
{
    for (u32 i = 1; i < RLVK_MAX_TEXTURE_SLOTS; i++)
        if (!RLVK.textureSlots[i].inUse)
        {
            RLVK.textureSlots[i].inUse = true;
            return i;
        }
    return RLVK_INVALID_SLOT;
}
static u32 rlvkAllocShaderSlot(void)
{
    for (u32 i = 1; i < RLVK_MAX_SHADER_SLOTS; i++)
        if (!RLVK.shaderSlots[i].inUse) { RLVK.shaderSlots[i].inUse = true; return i; }
    return RLVK_INVALID_SLOT;
}
static u32 rlvkAllocFramebufferSlot(void)
{
    for (u32 i = 1; i < RLVK_MAX_FRAMEBUFFER_SLOTS; i++)
        if (!RLVK.fbSlots[i].inUse) { RLVK.fbSlots[i].inUse = true; return i; }
    return RLVK_INVALID_SLOT;
}
static u32 rlvkAllocBufferSlot(void)
{
    // Prefer virgin slots so freed-but-pooled buffers survive until their fence-gated reuse
    // age (recycling a pooled slot immediately would evict the buffer the pool exists to keep)
    for (u32 i = 1; i < RLVK_MAX_BUFFER_SLOTS; i++)
        if (!RLVK.bufferSlots[i].inUse && (RLVK.bufferSlots[i].buffer == VK_NULL_HANDLE)) { RLVK.bufferSlots[i].inUse = true; return i; }
    for (u32 i = 1; i < RLVK_MAX_BUFFER_SLOTS; i++)
        if (!RLVK.bufferSlots[i].inUse) { RLVK.bufferSlots[i].inUse = true; return i; }
    return RLVK_INVALID_SLOT;
}

//----------------------------------------------------------------------------------
// Platform-layer hook (declared in rlvk.h, not rlgl.h).
//----------------------------------------------------------------------------------
VkInstance rlvkGetInstance(void) { return RLVK.instance; }

// Set the MSAA sample count, must be called before rlvkAttachSurface()
void rlvkSetMsaaSamples(int samples)
{
    if (RLVK.swapchain) { TRACELOG(RL_LOG_WARNING, "RLVK: rlvkSetMsaaSamples must be called before the swapchain exists"); return; }
    RLVK.msaaSamples = (samples >= 4) ? 4 : 1;
    if (RLVK.msaaSamples > 1) TRACELOG(RL_LOG_INFO, "RLVK: MSAA x%d enabled (matches GL FLAG_MSAA_4X_HINT)", RLVK.msaaSamples);
}

// Destroy everything sized to the swapchain (the swapchain itself, its views + present
// semaphores, and the per-frame depth/intermediate/msaa targets), evicting cached
// framebuffers built on the dying views. Callers guarantee the device is idle.
static void rlvkDestroySwapchainSizedObjects(void)
{
    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        rlvkEvictFramebuffersForView(RLVK.depthView[i]);
        rlvkEvictFramebuffersForView(RLVK.interView[i]);
        rlvkEvictFramebuffersForView(RLVK.msaaView[i]);
        if (RLVK.depthView[i])   vkDestroyImageView(RLVK.device, RLVK.depthView[i], RLVK_ALLOC);
        if (RLVK.depthImage[i])  vkDestroyImage(RLVK.device, RLVK.depthImage[i], RLVK_ALLOC);
        if (RLVK.depthMemory[i]) vkFreeMemory(RLVK.device, RLVK.depthMemory[i], RLVK_ALLOC);
        if (RLVK.interView[i])   vkDestroyImageView(RLVK.device, RLVK.interView[i], RLVK_ALLOC);
        if (RLVK.interImage[i])  vkDestroyImage(RLVK.device, RLVK.interImage[i], RLVK_ALLOC);
        if (RLVK.interMemory[i]) vkFreeMemory(RLVK.device, RLVK.interMemory[i], RLVK_ALLOC);
        if (RLVK.msaaView[i])    vkDestroyImageView(RLVK.device, RLVK.msaaView[i], RLVK_ALLOC);
        if (RLVK.msaaImage[i])   vkDestroyImage(RLVK.device, RLVK.msaaImage[i], RLVK_ALLOC);
        if (RLVK.msaaMemory[i])  vkFreeMemory(RLVK.device, RLVK.msaaMemory[i], RLVK_ALLOC);
        RLVK.depthView[i] = VK_NULL_HANDLE; RLVK.depthImage[i] = VK_NULL_HANDLE; RLVK.depthMemory[i] = VK_NULL_HANDLE;
        RLVK.interView[i] = VK_NULL_HANDLE; RLVK.interImage[i] = VK_NULL_HANDLE; RLVK.interMemory[i] = VK_NULL_HANDLE;
        RLVK.msaaView[i]  = VK_NULL_HANDLE; RLVK.msaaImage[i]  = VK_NULL_HANDLE; RLVK.msaaMemory[i]  = VK_NULL_HANDLE;
    }
    for (int i = 0; i < RLVK_MAX_SWAPCHAIN_IMAGES; i++)
    {
        if (RLVK.swapchainViews[i])   vkDestroyImageView(RLVK.device, RLVK.swapchainViews[i], RLVK_ALLOC);
        if (RLVK.renderSemaphores[i]) vkDestroySemaphore(RLVK.device, RLVK.renderSemaphores[i], RLVK_ALLOC);
        RLVK.swapchainViews[i] = VK_NULL_HANDLE;
        RLVK.renderSemaphores[i] = VK_NULL_HANDLE;
        RLVK.swapchainImages[i] = VK_NULL_HANDLE;
    }
    if (RLVK.swapchain) vkDestroySwapchainKHR(RLVK.device, RLVK.swapchain, RLVK_ALLOC);
    RLVK.swapchain = VK_NULL_HANDLE;
    RLVK.swapchainImageCount = 0;
}

// Recreate the swapchain after OUT_OF_DATE/SUBOPTIMAL (window resize, Android rotate/resume).
// Full device drain + rebuild: resize is rare, simplicity beats oldSwapchain retirement.
static void rlvkRecreateSwapchain(void)
{
    if (!isGpuReady || (RLVK.surface == VK_NULL_HANDLE)) return;
    vkDeviceWaitIdle(RLVK.device);

    // A minimized window reports a 0x0 extent: creating a swapchain there is invalid.
    // Keep the retired one; the next acquire fails OUT_OF_DATE again and retries here.
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RLVK.physicalDevice, RLVK.surface, &caps);
    if ((caps.currentExtent.width == 0) || (caps.currentExtent.height == 0)) return;

    rlvkDestroySwapchainSizedObjects();
    rlvkAttachSurface(RLVK.surface);
    TRACELOG(RL_LOG_INFO, "RLVK: swapchain recreated (%ux%u)", RLVK.swapchainExtent.width, RLVK.swapchainExtent.height);
}

// Attach the platform-created surface and build the swapchain around it.
// Re-entrant by design: rlvkRecreateSwapchain calls it again after destroying the
// size-dependent objects; the once-only per-frame pacing objects are guarded below.
void rlvkAttachSurface(VkSurfaceKHR surface)
{
    RLVK.surface = surface;
    if (!isGpuReady) return;

    // Confirm the graphics queue family can present to this surface
    VkBool32 presentSupport = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(RLVK.physicalDevice, RLVK.graphicsFamily, surface, &presentSupport);
    RLVK_CHECK_LOG(!presentSupport, "graphics queue family cannot present to surface");

    // Surface capabilities -> extent + image count
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RLVK.physicalDevice, surface, &caps);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        extent.width  = (u32)RLVK.State.framebufferWidth;
        extent.height = (u32)RLVK.State.framebufferHeight;
    }
    if (extent.width  < caps.minImageExtent.width)  extent.width  = caps.minImageExtent.width;
    if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;

    // Minimum image count: rendering targets the per-frame intermediate image, the swapchain
    // only receives the final flip blit, so extra queue depth buys nothing and costs VRAM
    u32 imageCount = caps.minImageCount;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
    if (imageCount > RLVK_MAX_SWAPCHAIN_IMAGES) imageCount = RLVK_MAX_SWAPCHAIN_IMAGES;

    // Pick a UNORM (non-sRGB) BGRA format so stored bytes match the GL default framebuffer
    u32 fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(RLVK.physicalDevice, surface, &fmtCount, NULL);
    VkSurfaceFormatKHR *fmts = (VkSurfaceFormatKHR *)RL_MALLOC(fmtCount*sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(RLVK.physicalDevice, surface, &fmtCount, fmts);
    VkSurfaceFormatKHR chosen = fmts[0];
    for (u32 i = 0; i < fmtCount; i++)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = fmts[i]; break; }
    RL_FREE(fmts);

    RLVK.swapchainFormat = chosen.format;
    RLVK.swapchainExtent = extent;

    // FIFO is always supported and is vsync-locked (caps the frame rate to the display refresh).
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
#if defined(PERFORMANCE_CAPTURE)
    // Benchmarking wants uncapped presentation: prefer IMMEDIATE (no sync), then MAILBOX.
    {
        u32 pmCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(RLVK.physicalDevice, surface, &pmCount, NULL);
        VkPresentModeKHR pms[16];
        if (pmCount > 16) pmCount = 16;
        vkGetPhysicalDeviceSurfacePresentModesKHR(RLVK.physicalDevice, surface, &pmCount, pms);
        bool hasImmediate = false, hasMailbox = false;
        for (u32 i = 0; i < pmCount; i++)
        {
            if (pms[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) hasImmediate = true;
            if (pms[i] == VK_PRESENT_MODE_MAILBOX_KHR)   hasMailbox = true;
        }
        if (hasImmediate) presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        else if (hasMailbox) presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
#endif

    RLVK_CHECK(vkCreateSwapchainKHR(RLVK.device,
        &(VkSwapchainCreateInfoKHR){
            VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface          = surface,
            .minImageCount    = imageCount,
            .imageFormat      = chosen.format,
            .imageColorSpace  = chosen.colorSpace,
            .imageExtent      = extent,
            .imageArrayLayers = 1,
            // TRANSFER_DST: the unmirrored intermediate image is flip-blitted INTO the
            // swapchain image; TRANSFER_SRC: rlReadScreenPixels copies out of it
            .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform     = caps.currentTransform,
            .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode      = presentMode,
            .clipped          = VK_TRUE,
        }, RLVK_ALLOC, &RLVK.swapchain));

    vkGetSwapchainImagesKHR(RLVK.device, RLVK.swapchain, &RLVK.swapchainImageCount, NULL);
    if (RLVK.swapchainImageCount > RLVK_MAX_SWAPCHAIN_IMAGES) RLVK.swapchainImageCount = RLVK_MAX_SWAPCHAIN_IMAGES;
    vkGetSwapchainImagesKHR(RLVK.device, RLVK.swapchain, &RLVK.swapchainImageCount, RLVK.swapchainImages);

    for (u32 i = 0; i < RLVK.swapchainImageCount; i++)
    {
        RLVK_CHECK(vkCreateImageView(RLVK.device,
            &(VkImageViewCreateInfo){
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image            = RLVK.swapchainImages[i],
                .viewType         = VK_IMAGE_VIEW_TYPE_2D,
                .format           = chosen.format,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            }, RLVK_ALLOC, &RLVK.swapchainViews[i]));

        RLVK_CHECK(vkCreateSemaphore(RLVK.device,
            &(VkSemaphoreCreateInfo){ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO },
            RLVK_ALLOC, &RLVK.renderSemaphores[i]));
    }

    // Once-only frame pacing objects (size-independent): survive swapchain recreation
    if (RLVK.frameFences[0] == VK_NULL_HANDLE)
    {
        for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
        {
            RLVK_CHECK(vkCreateSemaphore(RLVK.device,
                &(VkSemaphoreCreateInfo){ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO },
                RLVK_ALLOC, &RLVK.acquireSemaphores[i]));
            RLVK_CHECK(vkCreateFence(RLVK.device,
                &(VkFenceCreateInfo){ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT },
                RLVK_ALLOC, &RLVK.frameFences[i]));
        }
    }

    // One depth buffer per frame-in-flight (sized to the swapchain, cleared each frame)
    RLVK.depthFormat = VK_FORMAT_D32_SFLOAT;
    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        RLVK_CHECK(vkCreateImage(RLVK.device,
            &(VkImageCreateInfo){
                VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType     = VK_IMAGE_TYPE_2D,
                .format        = RLVK.depthFormat,
                .extent        = { extent.width, extent.height, 1 },
                .mipLevels     = 1, .arrayLayers = 1,
                .samples       = (RLVK.msaaSamples > 1) ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
                .tiling        = VK_IMAGE_TILING_OPTIMAL,
                .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            }, RLVK_ALLOC, &RLVK.depthImage[i]));

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(RLVK.device, RLVK.depthImage[i], &memReq);
        RLVK.depthMemory[i] = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        RLVK_CHECK(vkBindImageMemory(RLVK.device, RLVK.depthImage[i], RLVK.depthMemory[i], 0));

        RLVK_CHECK(vkCreateImageView(RLVK.device,
            &(VkImageViewCreateInfo){
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image            = RLVK.depthImage[i],
                .viewType         = VK_IMAGE_VIEW_TYPE_2D,
                .format           = RLVK.depthFormat,
                .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
            }, RLVK_ALLOC, &RLVK.depthView[i]));
    }

    // 1x UNMIRRORED color targets (per frame-in-flight): all rendering uses a POSITIVE viewport
    // in GL's memory orientation so every rasterizer tie-break matches GL natively; the image
    // Y-flips into the swapchain at present
    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        RLVK_CHECK(vkCreateImage(RLVK.device,
            &(VkImageCreateInfo){
                VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType     = VK_IMAGE_TYPE_2D,
                .format        = RLVK.swapchainFormat,
                .extent        = { extent.width, extent.height, 1 },
                .mipLevels     = 1, .arrayLayers = 1,
                .samples       = VK_SAMPLE_COUNT_1_BIT,
                .tiling        = VK_IMAGE_TILING_OPTIMAL,
                .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            }, RLVK_ALLOC, &RLVK.interImage[i]));
        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(RLVK.device, RLVK.interImage[i], &memReq);
        RLVK.interMemory[i] = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        RLVK_CHECK(vkBindImageMemory(RLVK.device, RLVK.interImage[i], RLVK.interMemory[i], 0));
        RLVK_CHECK(vkCreateImageView(RLVK.device,
            &(VkImageViewCreateInfo){
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image            = RLVK.interImage[i],
                .viewType         = VK_IMAGE_VIEW_TYPE_2D,
                .format           = RLVK.swapchainFormat,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            }, RLVK_ALLOC, &RLVK.interView[i]));
    }

    // 4x MSAA color targets (per frame-in-flight), fixed-function-resolved into the 1x
    // intermediate at CmdEndRendering (FLAG_MSAA_4X_HINT; standard Vulkan sample pattern)
    if (RLVK.msaaSamples > 1)
    {
        for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
        {
            RLVK_CHECK(vkCreateImage(RLVK.device,
                &(VkImageCreateInfo){
                    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                    .imageType     = VK_IMAGE_TYPE_2D,
                    .format        = RLVK.swapchainFormat,
                    .extent        = { extent.width, extent.height, 1 },
                    .mipLevels     = 1, .arrayLayers = 1,
                    .samples       = VK_SAMPLE_COUNT_4_BIT,
                    .tiling        = VK_IMAGE_TILING_OPTIMAL,
                    .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                }, RLVK_ALLOC, &RLVK.msaaImage[i]));
            VkMemoryRequirements memReq;
            vkGetImageMemoryRequirements(RLVK.device, RLVK.msaaImage[i], &memReq);
            RLVK.msaaMemory[i] = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            RLVK_CHECK(vkBindImageMemory(RLVK.device, RLVK.msaaImage[i], RLVK.msaaMemory[i], 0));
            RLVK_CHECK(vkCreateImageView(RLVK.device,
                &(VkImageViewCreateInfo){
                    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                    .image            = RLVK.msaaImage[i],
                    .viewType         = VK_IMAGE_VIEW_TYPE_2D,
                    .format           = RLVK.swapchainFormat,
                    .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
                }, RLVK_ALLOC, &RLVK.msaaView[i]));
        }

    }

    TRACELOG(RL_LOG_INFO, "RLVK: swapchain created (%ux%u, %u images)", extent.width, extent.height, RLVK.swapchainImageCount);
}

// Begin a frame: acquire a swapchain image, open the command buffer, transition the image to
// COLOR_ATTACHMENT_OPTIMAL, and begin the dynamic-rendering scope with loadOp=CLEAR (the render
// batch flush records draws into this scope; rlvkPresent closes and presents it).
// Queue GPU objects for fence-gated destruction: a command buffer may still reference them
// (recorded OR executing), and immediate destroy - even after vkDeviceWaitIdle - invalidates
// a still-RECORDING command buffer
static void rlvkDeferDestroy(VkBuffer buffer, VkImage image, VkImageView view, VkSampler sampler, VkDeviceMemory memory, VkPipeline pipeline)
{
    // A dying view invalidates any cached framebuffer built on it (they follow via the same ring)
    if (view) rlvkEvictFramebuffersForView(view);

    // Nothing recorded yet: no command buffer can reference these, destroy immediately
    if ((RLVK.frameCounter == 0) && !RLVK.frameActive)
    {
        if (buffer) vkDestroyBuffer(RLVK.device, buffer, RLVK_ALLOC);
        if (view) vkDestroyImageView(RLVK.device, view, RLVK_ALLOC);
        if (image) vkDestroyImage(RLVK.device, image, RLVK_ALLOC);
        if (sampler) vkDestroySampler(RLVK.device, sampler, RLVK_ALLOC);
        if (memory) vkFreeMemory(RLVK.device, memory, RLVK_ALLOC);
        if (pipeline) vkDestroyPipeline(RLVK.device, pipeline, RLVK_ALLOC);
        return;
    }

    // Queue on the most recent recording's frame slot (its fence covers the referencing work)
    u32 frameIndex = (u32)((RLVK.frameActive? RLVK.frameCounter : (RLVK.frameCounter + RLVK_FRAME_INDEX_COUNT - 1))%RLVK_FRAME_INDEX_COUNT);
    if (RLVK.deadResourceCount[frameIndex] >= RLVK_MAX_DEAD_RESOURCES)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: deferred-destruction queue full, leaking objects - raise RLVK_MAX_DEAD_RESOURCES");
        return;
    }
    RLVK.deadResources[frameIndex][RLVK.deadResourceCount[frameIndex]++] = (rlvkDeadResource){ buffer, image, view, sampler, memory, pipeline, VK_NULL_HANDLE };
}

// Queue a cached framebuffer for fence-gated destruction (evicted when one of its views dies)
static void rlvkDeferDestroyFramebufferOnly(VkFramebuffer framebuffer)
{
    if (framebuffer == VK_NULL_HANDLE) return;
    if ((RLVK.frameCounter == 0) && !RLVK.frameActive)
    {
        vkDestroyFramebuffer(RLVK.device, framebuffer, RLVK_ALLOC);
        return;
    }
    u32 frameIndex = (u32)((RLVK.frameActive? RLVK.frameCounter : (RLVK.frameCounter + RLVK_FRAME_INDEX_COUNT - 1))%RLVK_FRAME_INDEX_COUNT);
    if (RLVK.deadResourceCount[frameIndex] >= RLVK_MAX_DEAD_RESOURCES)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: deferred-destruction queue full, leaking objects - raise RLVK_MAX_DEAD_RESOURCES");
        return;
    }
    rlvkDeadResource *r = &RLVK.deadResources[frameIndex][RLVK.deadResourceCount[frameIndex]++];
    memset(r, 0, sizeof(*r));
    r->framebuffer = framebuffer;
}

// Begin a frame: acquire a swapchain image and open the rendering scope
static void rlvkBeginFrame(void)
{
    if (RLVK.frameActive || !isGpuReady || !RLVK.swapchain) return;

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[frameIndex], VK_TRUE, UINT64_MAX);

    // This slot's previous submission has fully executed: destroy its deferred objects
    for (int d = 0; d < RLVK.deadResourceCount[frameIndex]; d++)
    {
        rlvkDeadResource *r = &RLVK.deadResources[frameIndex][d];
        if (r->framebuffer) vkDestroyFramebuffer(RLVK.device, r->framebuffer, RLVK_ALLOC);
        if (r->buffer) vkDestroyBuffer(RLVK.device, r->buffer, RLVK_ALLOC);
        if (r->view) vkDestroyImageView(RLVK.device, r->view, RLVK_ALLOC);
        if (r->image) vkDestroyImage(RLVK.device, r->image, RLVK_ALLOC);
        if (r->sampler) vkDestroySampler(RLVK.device, r->sampler, RLVK_ALLOC);
        if (r->memory) vkFreeMemory(RLVK.device, r->memory, RLVK_ALLOC);
        if (r->pipeline) vkDestroyPipeline(RLVK.device, r->pipeline, RLVK_ALLOC);
    }
    RLVK.deadResourceCount[frameIndex] = 0;

    u32 imageIndex = 0;
    VkResult acq = vk.AcquireNextImageKHR(RLVK.device, RLVK.swapchain, UINT64_MAX,
                       RLVK.acquireSemaphores[frameIndex], VK_NULL_HANDLE, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // Resize/rotation retired the swapchain: rebuild and retry the acquire once.
        // A failed acquire leaves the semaphore unsignaled, so it is safe to reuse.
        rlvkRecreateSwapchain();
        if (RLVK.swapchain == VK_NULL_HANDLE) return;   // still minimized (0x0): skip the frame
        acq = vk.AcquireNextImageKHR(RLVK.device, RLVK.swapchain, UINT64_MAX,
                  RLVK.acquireSemaphores[frameIndex], VK_NULL_HANDLE, &imageIndex);
        if ((acq != VK_SUCCESS) && (acq != VK_SUBOPTIMAL_KHR)) return;
    }
    RLVK.currentImageIndex = imageIndex;
    RLVK.acquireWaited = false;

    vkResetFences(RLVK.device, 1, &RLVK.frameFences[frameIndex]);

    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
    vkResetCommandPool(RLVK.device, RLVK.cmdPools[frameIndex], 0);
    vk.BeginCommandBuffer(cmdBuffer, &(VkCommandBufferBeginInfo){
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT });
    RLVK.boundPipeline = VK_NULL_HANDLE;    // pipeline binding is command-buffer state too
    memset(RLVK.pushedView, 0, sizeof(RLVK.pushedView));      // push-descriptor state resets with the command buffer
    s_pipelineFastValid = false;
    RLVK.State.cbEpoch++;
    memset(RLVK.pushedSampler, 0, sizeof(RLVK.pushedSampler));
    s_viewportValid = false;
    s_bindingValid   = false;
    // Pool-ring fallback: this slot's fence has signaled, its snapshot sets are reusable
    memset(RLVK.shadowUbo, 0, sizeof(RLVK.shadowUbo));
    RLVK.set0Dirty = true;
    if (!RLVK.Caps.pushDescriptor && RLVK.descPools[frameIndex]) vkResetDescriptorPool(RLVK.device, RLVK.descPools[frameIndex], 0);
    if (RLVK.computeDescPools[frameIndex]) vkResetDescriptorPool(RLVK.device, RLVK.computeDescPools[frameIndex], 0);

    RLVK.arenaOffset[frameIndex] = 0;   // reset this frame's bump arena (the frame fence gates reuse)
    // A freshly-begun frame WILL be presented: cancel any pending consumed-skip. Without this,
    // a rlReadScreenPixels that presented its own frame leaves the flag armed, the NEXT frame's
    // present gets skipped while its command buffer is still recording, frameCounter advances
    // under it, and later commands land in a never-begun command buffer (GPU fault).
    RLVK.frameConsumed = false;

    // GPU trace: harvest the ring-behind frame's timestamps, then reset+stamp this frame's start
    if (rlvkDebugFlag("RLVK_GPU_TRACE", &s_dbgGpu))
    {
        if (s_gpuPool == VK_NULL_HANDLE)
        {
            VkPhysicalDeviceProperties pdp; vkGetPhysicalDeviceProperties(RLVK.physicalDevice, &pdp);
            s_gpuPeriod = pdp.limits.timestampPeriod;
            vkCreateQueryPool(RLVK.device, &(VkQueryPoolCreateInfo){
                VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 3*RLVK_FRAME_INDEX_COUNT,
            }, RLVK_ALLOC, &s_gpuPool);
        }
        else if (RLVK.frameCounter >= RLVK_FRAME_INDEX_COUNT)
        {
            u64 q[3] = { 0 };
            if (vkGetQueryPoolResults(RLVK.device, s_gpuPool, frameIndex*3, 3, sizeof(q), q,
                    sizeof(u64), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            {
                s_gpuScene   += (f64)(q[1] - q[0])*s_gpuPeriod*1e-6;   // ms
                s_gpuPresent += (f64)(q[2] - q[1])*s_gpuPeriod*1e-6;
                if (((++s_gpuFrames) & 511) == 0)
                    TRACELOG(RL_LOG_WARNING, "VKGPU frames=%d scene=%.3fms present=%.3fms (avg)",
                        s_gpuFrames, s_gpuScene/s_gpuFrames, s_gpuPresent/s_gpuFrames);
            }
        }
    }
    // Grow this frame's bump arena when its last use ran out (mid-frame drains recorded the
    // demanded size): 168 bytes per batch element, 2x headroom so growth converges in one step
    if (RLVK.arenaWanted[frameIndex] > RLVK.arena[frameIndex].sizeBytes)
    {
        rlvkBatchBackingBuffer *grown = &RLVK.arena[frameIndex];
        int elems = (int)(RLVK.arenaWanted[frameIndex]*5/4/168) + 64;   // 25% headroom over measured demand
        rlvkDeferDestroy(grown->buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, grown->memory, VK_NULL_HANDLE);
        if (rlvkCreateBatchBacking(elems, grown))
            TRACELOG(RL_LOG_INFO, "RLVK: flush arena grown to %u bytes", grown->sizeBytes);
    }
    RLVK.arenaWanted[frameIndex] = 0;

    // Make host writes to the persistent-mapped vertex buffers visible to the vertex shader (BDA reads)
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &(VkMemoryBarrier2){
            VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_HOST_BIT,
            .srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT
                           | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT
                           | VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        },
    });

    // The frame's render targets start UNDEFINED: the 1x unmirrored intermediate (also the
    // resolve destination under MSAA) and, when MSAA is on, the multisampled color target
    {
        VkImageMemoryBarrier2 targetBarriers[2];
        u32 targetCount = 0;
        targetBarriers[targetCount++] = (VkImageMemoryBarrier2){
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask    = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image            = RLVK.interImage[frameIndex],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        if (RLVK.msaaSamples > 1) targetBarriers[targetCount++] = (VkImageMemoryBarrier2){
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask    = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image            = RLVK.msaaImage[frameIndex],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = targetCount,
            .pImageMemoryBarriers = targetBarriers,
        });
    }

    // Transition color -> COLOR_ATTACHMENT_OPTIMAL and depth -> DEPTH_ATTACHMENT_OPTIMAL (cleared each frame)
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
            {   VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                // srcStage matches the acquire-semaphore wait stage so the transition happens-after acquire
                .srcStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask    = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .image            = RLVK.swapchainImages[imageIndex],
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            },
            {   VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask     = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .dstStageMask     = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .dstAccessMask    = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .image            = RLVK.depthImage[frameIndex],
                .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
            },
        },
    });

    bool msaa = (RLVK.msaaSamples > 1);
    {
        // Fixed-function resolve into the 1x intermediate at every scope close (MSAA only)
        VkImageView scopeViews[3];
        u32 scopeViewCount = 0;
        scopeViews[scopeViewCount++] = msaa? RLVK.msaaView[frameIndex] : RLVK.interView[frameIndex];
        if (msaa) scopeViews[scopeViewCount++] = RLVK.interView[frameIndex];
        scopeViews[scopeViewCount++] = RLVK.depthView[frameIndex];
        rlvkRenderPassKey rpKey;
        memset(&rpKey, 0, sizeof(rpKey));
        rpKey.colorFormats[0] = RLVK.swapchainFormat;
        rpKey.depthFormat     = RLVK.depthFormat;
        rpKey.colorCount      = 1;
        rpKey.samples         = msaa? 4 : 1;
        rpKey.colorLoad       = VK_ATTACHMENT_LOAD_OP_CLEAR;
        rpKey.depthLoad       = VK_ATTACHMENT_LOAD_OP_CLEAR;
        rpKey.depthStore      = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        rpKey.hasResolve      = msaa? 1 : 0;
        rlvkBeginScopeRenderPass(cmdBuffer, &rpKey, scopeViews, scopeViewCount,
            RLVK.swapchainExtent.width, RLVK.swapchainExtent.height,
            &(VkClearValue){ .color = { .float32 = {
                RLVK.State.clearR/255.0f, RLVK.State.clearG/255.0f,
                RLVK.State.clearB/255.0f, RLVK.State.clearA/255.0f } } },
            &(VkClearValue){ .depthStencil = { 1.0f, 0 } });
    }
    RLVK.scope.fbSlot = 0;
    RLVK.scope.width  = RLVK.swapchainExtent.width;
    RLVK.scope.height = RLVK.swapchainExtent.height;
    RLVK.scope.colorCount = 1;
    RLVK.scope.colorFormats[0] = RLVK.swapchainFormat;
    RLVK.scope.samples = (u32)RLVK.msaaSamples;
    RLVK.scope.flipY  = false;   // UNMIRRORED: swapchain-scope rendering matches GL memory orientation


    // GPU trace: frame-start stamp (query slots were harvested above, safe to reuse)
    if (rlvkDebugFlag("RLVK_GPU_TRACE", &s_dbgGpu) && (s_gpuPool != VK_NULL_HANDLE))
    {
        vkCmdResetQueryPool(cmdBuffer, s_gpuPool, frameIndex*3, 3);
        vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, s_gpuPool, frameIndex*3 + 0);
    }
    RLVK.frameActive = true;
}

// Close the render scope, transition to PRESENT_SRC, submit and present. Called from the platform's
// SwapScreenBuffer. If nothing drew this frame, still present a cleared frame.
void rlvkPresent(void)
{
    if (rlvkDebugFlag("RLVK_DEBUG_FBO", &s_dbgFbo)) TRACELOG(RL_LOG_WARNING, "VKDBG present FA=%d consumed=%d fc=%llu", (int)RLVK.frameActive, (int)RLVK.frameConsumed, (ull)RLVK.frameCounter);
    if (!isGpuReady || !RLVK.swapchain) return;
    if (RLVK.frameConsumed) { RLVK.frameConsumed = false; RLVK.frameCounter++; return; }  // rlReadScreenPixels already presented
    if (rlvkDebugFlag("RLVK_MEM_REPORT", &s_dbgMem) && ((RLVK.frameCounter & 2047) == 2047)) TRACELOG(RL_LOG_WARNING, "VKMEM local=%lldKB host=%lldKB allocs=%d vboCreate=%d vboReuse=%d", s_memLocalBytes/1024, s_memHostBytes/1024, s_memAllocCount, s_vboCreateCount, s_vboReuseCount);
    if (!RLVK.frameActive) rlvkBeginFrame();
    if (!RLVK.frameActive) return;                  // acquire failed (e.g. out-of-date)

    u32 frameIndex         = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    u32 imageIndex = RLVK.currentImageIndex;
    VkCommandBuffer cmdBuffer  = RLVK.cmdBuffers[frameIndex];

    vkCmdEndRenderPass(cmdBuffer);
    rlvkFinishSwapchainImage(cmdBuffer);   // flip-blit the frame into the swapchain

    // COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask    = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask     = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image            = RLVK.swapchainImages[imageIndex],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        },
    });

    vk.EndCommandBuffer(cmdBuffer);

    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount   = RLVK.acquireWaited? 0u : 1u,    // a mid-frame flush may have consumed it
        .pWaitSemaphoreInfos      = &(VkSemaphoreSubmitInfo){
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = RLVK.acquireSemaphores[frameIndex],
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT },
        .commandBufferInfoCount   = 1,
        .pCommandBufferInfos      = &(VkCommandBufferSubmitInfo){
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmdBuffer },
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &(VkSemaphoreSubmitInfo){
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = RLVK.renderSemaphores[imageIndex],
            // Signal after all work (incl. the ->PRESENT_SRC transition) so the present waits for it
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT },
    }, RLVK.frameFences[frameIndex]);

    VkResult pres = vk.QueuePresentKHR(RLVK.graphicsQueue, &(VkPresentInfoKHR){
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &RLVK.renderSemaphores[imageIndex],
        .swapchainCount     = 1,
        .pSwapchains        = &RLVK.swapchain,
        .pImageIndices      = &imageIndex,
    });

    RLVK.frameActive = false;
    RLVK.frameCounter++;

    // The frame's work is already submitted (its fence gates the drain inside the rebuild);
    // rebuild now so the NEXT acquire starts from a valid swapchain
    if ((pres == VK_ERROR_OUT_OF_DATE_KHR) || (pres == VK_SUBOPTIMAL_KHR)) rlvkRecreateSwapchain();
}

//----------------------------------------------------------------------------------
// Internal Functions Definition - Math and format mapping
//----------------------------------------------------------------------------------

// Get identity matrix
static Matrix rlvkMatrixIdentity(void)
{
    return (Matrix){ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
}

// Get two matrix multiplication result
static Matrix rlvkMatrixMultiply(Matrix l, Matrix r)
{
    Matrix m;
    m.m0  = l.m0 *r.m0  + l.m1 *r.m4  + l.m2 *r.m8   + l.m3 *r.m12;
    m.m1  = l.m0 *r.m1  + l.m1 *r.m5  + l.m2 *r.m9   + l.m3 *r.m13;
    m.m2  = l.m0 *r.m2  + l.m1 *r.m6  + l.m2 *r.m10  + l.m3 *r.m14;
    m.m3  = l.m0 *r.m3  + l.m1 *r.m7  + l.m2 *r.m11  + l.m3 *r.m15;
    m.m4  = l.m4 *r.m0  + l.m5 *r.m4  + l.m6 *r.m8   + l.m7 *r.m12;
    m.m5  = l.m4 *r.m1  + l.m5 *r.m5  + l.m6 *r.m9   + l.m7 *r.m13;
    m.m6  = l.m4 *r.m2  + l.m5 *r.m6  + l.m6 *r.m10  + l.m7 *r.m14;
    m.m7  = l.m4 *r.m3  + l.m5 *r.m7  + l.m6 *r.m11  + l.m7 *r.m15;
    m.m8  = l.m8 *r.m0  + l.m9 *r.m4  + l.m10*r.m8   + l.m11*r.m12;
    m.m9  = l.m8 *r.m1  + l.m9 *r.m5  + l.m10*r.m9   + l.m11*r.m13;
    m.m10 = l.m8 *r.m2  + l.m9 *r.m6  + l.m10*r.m10  + l.m11*r.m14;
    m.m11 = l.m8 *r.m3  + l.m9 *r.m7  + l.m10*r.m11  + l.m11*r.m15;
    m.m12 = l.m12*r.m0  + l.m13*r.m4  + l.m14*r.m8   + l.m15*r.m12;
    m.m13 = l.m12*r.m1  + l.m13*r.m5  + l.m14*r.m9   + l.m15*r.m13;
    m.m14 = l.m12*r.m2  + l.m13*r.m6  + l.m14*r.m10  + l.m15*r.m14;
    m.m15 = l.m12*r.m3  + l.m13*r.m7  + l.m14*r.m11  + l.m15*r.m15;
    return m;
}

// Get transposed input matrix
static Matrix rlvkMatrixTranspose(Matrix m)
{
    return (Matrix){
        m.m0,  m.m4,  m.m8,  m.m12,
        m.m1,  m.m5,  m.m9,  m.m13,
        m.m2,  m.m6,  m.m10, m.m14,
        m.m3,  m.m7,  m.m11, m.m15
    };
}

// Get inverted input matrix
static Matrix rlvkMatrixInvert(Matrix m)
{
    f32 a00=m.m0, a01=m.m1, a02=m.m2, a03=m.m3;
    f32 a10=m.m4, a11=m.m5, a12=m.m6, a13=m.m7;
    f32 a20=m.m8, a21=m.m9, a22=m.m10, a23=m.m11;
    f32 a30=m.m12, a31=m.m13, a32=m.m14, a33=m.m15;
    f32 b00=a00*a11-a01*a10, b01=a00*a12-a02*a10, b02=a00*a13-a03*a10;
    f32 b03=a01*a12-a02*a11, b04=a01*a13-a03*a11, b05=a02*a13-a03*a12;
    f32 b06=a20*a31-a21*a30, b07=a20*a32-a22*a30, b08=a20*a33-a23*a30;
    f32 b09=a21*a32-a22*a31, b10=a21*a33-a23*a31, b11=a22*a33-a23*a32;
    f32 det = b00*b11 - b01*b10 + b02*b09 + b03*b08 - b04*b07 + b05*b06;
    if (fabsf(det) < 1e-8f) return rlvkMatrixIdentity();
    f32 inv = 1.0f/det;
    Matrix r;
    r.m0 =( a11*b11-a12*b10+a13*b09)*inv;
    r.m1 =(-a01*b11+a02*b10-a03*b09)*inv;
    r.m2 =( a31*b05-a32*b04+a33*b03)*inv;
    r.m3 =(-a21*b05+a22*b04-a23*b03)*inv;
    r.m4 =(-a10*b11+a12*b08-a13*b07)*inv;
    r.m5 =( a00*b11-a02*b08+a03*b07)*inv;
    r.m6 =(-a30*b05+a32*b02-a33*b01)*inv;
    r.m7 =( a20*b05-a22*b02+a23*b01)*inv;
    r.m8 =( a10*b10-a11*b08+a13*b06)*inv;
    r.m9 =(-a00*b10+a01*b08-a03*b06)*inv;
    r.m10=( a30*b04-a31*b02+a33*b00)*inv;
    r.m11=(-a20*b04+a21*b02-a23*b00)*inv;
    r.m12=(-a10*b09+a11*b07-a12*b06)*inv;
    r.m13=( a00*b09-a01*b07+a02*b06)*inv;
    r.m14=(-a30*b03+a31*b01-a32*b00)*inv;
    r.m15=( a20*b03-a21*b01+a22*b00)*inv;
    return r;
}

// Get pixel data size in bytes (image or texture), mirrors rlGetPixelDataSize()
static int rlvkGetPixelDataSize(int width, int height, int format)
{
    int bpp = 0;
    switch (format)
    {
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:    bpp = 8;   break;
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
        case RL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:
        case RL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
        case RL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:     bpp = 16;  break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8:       bpp = 24;  break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
        case RL_PIXELFORMAT_UNCOMPRESSED_R32:          bpp = 32;  break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32:    bpp = 96;  break;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32: bpp = 128; break;
        default: bpp = 32; break;
    }
    return (width*height*bpp)/8;
}

// Get the Vulkan format equivalent to a raylib pixel format
static VkFormat rlvkGetVkTextureFormat(int format)
{
    switch (format)
    {
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:    return VK_FORMAT_R8_UNORM;
        case RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:   return VK_FORMAT_R8G8_UNORM;
        case RL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:       return VK_FORMAT_R5G6B5_UNORM_PACK16;
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8:       return VK_FORMAT_R8G8B8_UNORM;
        case RL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:     return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
        case RL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:     return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
        case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:     return VK_FORMAT_R8G8B8A8_UNORM;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32:          return VK_FORMAT_R32_SFLOAT;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32:    return VK_FORMAT_R32G32B32_SFLOAT;
        case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case RL_PIXELFORMAT_UNCOMPRESSED_R16:          return VK_FORMAT_R16_SFLOAT;
        case RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16:    return VK_FORMAT_R16G16B16_SFLOAT;
        case RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case RL_PIXELFORMAT_COMPRESSED_DXT1_RGB:       return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_DXT1_RGBA:      return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_DXT3_RGBA:      return VK_FORMAT_BC2_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_DXT5_RGBA:      return VK_FORMAT_BC3_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_ETC1_RGB:
        case RL_PIXELFORMAT_COMPRESSED_ETC2_RGB:       return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA:  return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA:  return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case RL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA:  return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

#endif // RLVK_IMPLEMENTATION
