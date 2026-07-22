//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Defines, tunables, constants, PFN dispatch table, sync2->sync1 shim
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
#define RLVK_ALLOC              NULL
#define RLVK_COUNTOF(arr)       ((u32)(sizeof(arr)/sizeof((arr)[0])))

// VK_KHR_line_rasterization (the KHR promotion of the older VK_EXT_line_rasterization, which
// IS present) is newer than the Vulkan-Headers bundled with some NDK releases (confirmed
// missing on NDK 28's headers, 2026-07-17 Android bring-up) - only the string constant is
// needed here (queried by name via vkEnumerateDeviceExtensionProperties, never through a
// KHR-specific struct/enum), so a manual fallback is safe and exactly matches the upstream
// Khronos registry value.
#ifndef VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME
    #define VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME "VK_KHR_line_rasterization"
#endif

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
// How many frames after the last bind the Caps.noSampledDepth depth twin keeps being refilled at
// scope close (§7.27/§7.29). Must comfortably exceed the frames-in-flight count AND tolerate the
// usual "sample the depth captured by the PREVIOUS frame" pattern, so 3 - a consumer that binds
// every frame re-arms it continuously and never lapses.
#ifndef RLVK_TWIN_KEEPALIVE_FRAMES
    #define RLVK_TWIN_KEEPALIVE_FRAMES   3
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
    // Graphics-stage SSBOs (GPU-particle draw path: vertex shader reads per-instance data).
    // GLSL declares std430 bindings 0..3; rlvkRebaseStorageBuffers rewrites them to 18..21
    // so they never collide with the sampler units at 0..15. READ-ONLY unless the device
    // has vertexPipelineStoresAndAtomics (NonWritable is injected otherwise).
    RLVK_SSBO_BINDING_BASE,
    RLVK_SET0_SSBO_COUNT   = 4,     // spec minimum maxPerStageDescriptorStorageBuffers
    RLVK_SET0_BINDING_COUNT = RLVK_SSBO_BINDING_BASE + RLVK_SET0_SSBO_COUNT,
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

