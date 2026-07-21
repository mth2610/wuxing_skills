//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: rlglInit/Close, blend state, render batch, rlSetTexture, vertex buffers
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

//----------------------------------------------------------------------------------
// Module Functions Definition - rlgl functionality
//----------------------------------------------------------------------------------

// Initialize rlgl: OpenGL extensions, default buffers/shaders/textures, OpenGL states
void rlglInit(int width, int height)
{
    RLVK.State.framebufferWidth = width;
    RLVK.State.framebufferHeight = height;
    RLVK.State.viewportW = width;
    RLVK.State.viewportH = height;

    // Defaults match GL backend
    RLVK.State.depthTest = false;
    RLVK.State.depthWrite = true;
    RLVK.State.cullEnabled = true; // rlgl does glEnable(GL_CULL_FACE) at init
    RLVK.State.cullMode = RL_CULL_FACE_BACK;
    RLVK.State.cullMode = RL_CULL_FACE_BACK;
    RLVK.State.colorBlendEnabled = true;
    RLVK.State.colorMask[0] = RLVK.State.colorMask[1] = RLVK.State.colorMask[2] = RLVK.State.colorMask[3] = true;
    RLVK.State.blendMode = RL_BLEND_ALPHA;
    RLVK.State.pointSize = 1.0f;
    RLVK.State.lineWidth = 1.0f;
    RLVK.State.modelview = rlvkMatrixIdentity();
    RLVK.State.projection = rlvkMatrixIdentity();
    RLVK.State.transform = rlvkMatrixIdentity();
    RLVK.State.meshMVP = rlvkMatrixIdentity();
    RLVK.State.meshColDiffuse[0] = RLVK.State.meshColDiffuse[1] = RLVK.State.meshColDiffuse[2] = RLVK.State.meshColDiffuse[3] = 1.0f;
    RLVK.State.currentMatrix = &RLVK.State.modelview;

    if (!rlvkInitInstance())
    {
        TRACELOG(RL_LOG_FATAL, "RLVK: instance creation failed");
        return;
    }
    if (!rlvkPickPhysicalDevice())
    {
        TRACELOG(RL_LOG_FATAL, "RLVK: no suitable physical device");
        return;
    }
    if (!rlvkInitLogicalDevice())
    {
        TRACELOG(RL_LOG_FATAL, "RLVK: logical device creation failed");
        return;
    }
    rlvkLoadEntrypoints();
    if (!rlvkInitSet0Layout())
    {
        TRACELOG(RL_LOG_FATAL, "RLVK: descriptor set layout creation failed");
        return;
    }
    if (!rlvkInitFrameRing())
    {
        TRACELOG(RL_LOG_FATAL, "RLVK: frame ring creation failed");
        return;
    }
    rlvkInitPipelineCache();

    isGpuReady = true;

    // Default shader (embedded SPIR-V modules) + pipeline layout for push constants / descriptors
    if (!rlvkInitDefaultShader())
    {
        TRACELOG(RL_LOG_FATAL, "RLVK: default shader creation failed");
        isGpuReady = false;
        return;
    }

    // Default 1x1 white texture: untextured draws (shapes) sample it, so white*vertexColor = vertexColor
    unsigned char whitePixel[4] = {255, 255, 255, 255};
    RLVK.defaultTextureSlot = rlLoadTexture(whitePixel, 1, 1, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);

    // Dummy attribute buffer for the divisor-0 broadcast: missing attributes read constants -
    // uv vec2(0,0) @0, opaque white @8, +Z normal @12, bone ids/weights @24/@28 = GL's generic
    // attribute default (0,0,0,1)
    typedef struct
    {
        f32 uv[2];
        unsigned int white;
        f32 normal[3];
        unsigned char boneIds[4];
        f32 boneW[4];
    } rlvkDummyData;
    rlvkDummyData dummyData = {{0.0f, 0.0f}, 0xFFFFFFFFu, {0.0f, 0.0f, 1.0f}, {0, 0, 0, 1}, {0.0f, 0.0f, 0.0f, 1.0f}};
#if defined(__APPLE__)
    // MoltenVK portability subset rejects stride < format size. We use stride = sizeof(rlvkDummyData)
    // and allocate a large enough buffer (1M vertices = ~44MB) to prevent out-of-bounds reads.
    int dummyCopies = 1048576;
    rlvkDummyData *dummyArray = (rlvkDummyData *)RL_MALLOC(dummyCopies * sizeof(rlvkDummyData));
    for (int i = 0; i < dummyCopies; i++)
        dummyArray[i] = dummyData;
    RLVK.dummyAttribSlot = rlvkCreateVBO(dummyArray, dummyCopies * sizeof(rlvkDummyData), false, false);
    RL_FREE(dummyArray);
#else
    RLVK.dummyAttribSlot = rlvkCreateVBO(&dummyData, sizeof(dummyData), false, false);
#endif

    // The default shader is the embedded push-constant SPIR-V (rlvk_shaders.h), NOT a runtime
    // compile: its draws take the push-constant fast path (no uniform snapshots or descriptor
    // pushes); shaderc loads lazily on the first user shader (rlLoadShaderProgram)

    RLVK.defaultShaderLocs = RLVK.shaderSlots[RLVK.defaultShaderSlot].locs;
    RLVK.State.currentShaderSlot = RLVK.defaultShaderSlot;
    RLVK.State.activeShaderSlot = RLVK.defaultShaderSlot;
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
    if (!isGpuReady)
        return;
    if (RLVK.device)
        vkDeviceWaitIdle(RLVK.device); // shutdown teardown: the one place a full device drain is the right tool

    // Release the persistent-mapped batch backing buffers and the per-frame bump arenas
    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        rlvkDestroyBatchBacking(&RLVK.batchBacking[i]);
        rlvkDestroyBatchBacking(&RLVK.arena[i]);
    }

    // Persist the driver pipeline cache for the next run, then release the cached pipelines
    rlvkSavePipelineCache();
    for (int i = 0; i < RLVK.pipelineCount; i++)
        vkDestroyPipeline(RLVK.device, RLVK.pipelines[i].pipeline, RLVK_ALLOC);
    RLVK.pipelineCount = 0;
    if (RLVK.pipelineCache != VK_NULL_HANDLE)
        vkDestroyPipelineCache(RLVK.device, RLVK.pipelineCache, RLVK_ALLOC);

    // Release the render-pass + framebuffer caches (framebuffers first: they reference the passes)
    for (int i = 0; i < RLVK.framebufferCount; i++)
        vkDestroyFramebuffer(RLVK.device, RLVK.framebuffers[i].framebuffer, RLVK_ALLOC);
    RLVK.framebufferCount = 0;
    for (int i = 0; i < RLVK.renderPassCount; i++)
        vkDestroyRenderPass(RLVK.device, RLVK.renderPasses[i].pass, RLVK_ALLOC);
    RLVK.renderPassCount = 0;

    if (rlvkDebugFlag("RLVK_MEM_REPORT", &s_dbgMem))
        TRACELOG(RL_LOG_WARNING, "VKMEM local=%lldKB host=%lldKB allocs=%d vboCreate=%d vboReuse=%d", s_memLocalBytes / 1024, s_memHostBytes / 1024, s_memAllocCount, s_vboCreateCount, s_vboReuseCount);

    // Full lifetime cleanup so repeated InitWindow()/CloseWindow() in one process does not
    // leak: slot tables, frame ring, swapchain targets, layouts, surface, device, instance
    for (int f = 0; f < RLVK_FRAME_INDEX_COUNT; f++)
    {
        for (int d = 0; d < RLVK.deadResourceCount[f]; d++)
        {
            rlvkDeadResource *r = &RLVK.deadResources[f][d];
            if (r->framebuffer)
                vkDestroyFramebuffer(RLVK.device, r->framebuffer, RLVK_ALLOC);
            if (r->view)
                vkDestroyImageView(RLVK.device, r->view, RLVK_ALLOC);
            if (r->sampler)
                vkDestroySampler(RLVK.device, r->sampler, RLVK_ALLOC);
            if (r->image)
                vkDestroyImage(RLVK.device, r->image, RLVK_ALLOC);
            if (r->buffer)
                vkDestroyBuffer(RLVK.device, r->buffer, RLVK_ALLOC);
            if (r->memory)
                vkFreeMemory(RLVK.device, r->memory, RLVK_ALLOC);
            if (r->pipeline)
                vkDestroyPipeline(RLVK.device, r->pipeline, RLVK_ALLOC);
        }
        RLVK.deadResourceCount[f] = 0;
    }
    for (int i = 1; i < RLVK_MAX_TEXTURE_SLOTS; i++)
    {
        rlvkTextureSlot *t = &RLVK.textureSlots[i];
        if (!t->inUse)
            continue;
        if (t->view)
            vkDestroyImageView(RLVK.device, t->view, RLVK_ALLOC);
        if (t->sampler)
            vkDestroySampler(RLVK.device, t->sampler, RLVK_ALLOC);
        if (t->image)
            vkDestroyImage(RLVK.device, t->image, RLVK_ALLOC);
        if (t->memory)
            vkFreeMemory(RLVK.device, t->memory, RLVK_ALLOC);
    }
    for (int i = 1; i < RLVK_MAX_BUFFER_SLOTS; i++)
    {
        rlvkBufferSlot *b = &RLVK.bufferSlots[i];
        if (b->buffer)
            vkDestroyBuffer(RLVK.device, b->buffer, RLVK_ALLOC);
        if (b->memory)
            vkFreeMemory(RLVK.device, b->memory, RLVK_ALLOC);
    }
    for (int i = 1; i < RLVK_MAX_SHADER_SLOTS; i++)
    {
        rlvkShaderSlot *s = &RLVK.shaderSlots[i];
        if (s->vertMod)
            vkDestroyShaderModule(RLVK.device, s->vertMod, RLVK_ALLOC);
        if (s->fragMod)
            vkDestroyShaderModule(RLVK.device, s->fragMod, RLVK_ALLOC);
        if (s->uniforms)
            RL_FREE(s->uniforms);
        if (s->vsStage)
            RL_FREE(s->vsStage);
        if (s->fsStage)
            RL_FREE(s->fsStage);
    }
    if (s_gpuPool)
    {
        vkDestroyQueryPool(RLVK.device, s_gpuPool, RLVK_ALLOC);
        s_gpuPool = VK_NULL_HANDLE;
    }

    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        if (RLVK.depthView[i])
            vkDestroyImageView(RLVK.device, RLVK.depthView[i], RLVK_ALLOC);
        if (RLVK.depthImage[i])
            vkDestroyImage(RLVK.device, RLVK.depthImage[i], RLVK_ALLOC);
        if (RLVK.depthMemory[i])
            vkFreeMemory(RLVK.device, RLVK.depthMemory[i], RLVK_ALLOC);
        if (RLVK.interView[i])
            vkDestroyImageView(RLVK.device, RLVK.interView[i], RLVK_ALLOC);
        if (RLVK.interImage[i])
            vkDestroyImage(RLVK.device, RLVK.interImage[i], RLVK_ALLOC);
        if (RLVK.interMemory[i])
            vkFreeMemory(RLVK.device, RLVK.interMemory[i], RLVK_ALLOC);
        if (RLVK.msaaView[i])
            vkDestroyImageView(RLVK.device, RLVK.msaaView[i], RLVK_ALLOC);
        if (RLVK.msaaImage[i])
            vkDestroyImage(RLVK.device, RLVK.msaaImage[i], RLVK_ALLOC);
        if (RLVK.msaaMemory[i])
            vkFreeMemory(RLVK.device, RLVK.msaaMemory[i], RLVK_ALLOC);
        if (RLVK.acquireSemaphores[i])
            vkDestroySemaphore(RLVK.device, RLVK.acquireSemaphores[i], RLVK_ALLOC);
        if (RLVK.frameFences[i])
            vkDestroyFence(RLVK.device, RLVK.frameFences[i], RLVK_ALLOC);
        if (RLVK.cmdPools[i])
            vkDestroyCommandPool(RLVK.device, RLVK.cmdPools[i], RLVK_ALLOC);
        if (RLVK.descPools[i])
            vkDestroyDescriptorPool(RLVK.device, RLVK.descPools[i], RLVK_ALLOC);
        if (RLVK.computeDescPools[i])
            vkDestroyDescriptorPool(RLVK.device, RLVK.computeDescPools[i], RLVK_ALLOC);
    }
    for (int i = 0; i < RLVK_MAX_SWAPCHAIN_IMAGES; i++)
    {
        if (RLVK.swapchainViews[i])
            vkDestroyImageView(RLVK.device, RLVK.swapchainViews[i], RLVK_ALLOC);
        if (RLVK.renderSemaphores[i])
            vkDestroySemaphore(RLVK.device, RLVK.renderSemaphores[i], RLVK_ALLOC);
    }
    if (RLVK.swapchain)
        vkDestroySwapchainKHR(RLVK.device, RLVK.swapchain, RLVK_ALLOC);
    if (RLVK.set0Layout)
        vkDestroyDescriptorSetLayout(RLVK.device, RLVK.set0Layout, RLVK_ALLOC);
    if (RLVK.pipelineLayout)
        vkDestroyPipelineLayout(RLVK.device, RLVK.pipelineLayout, RLVK_ALLOC);
    if (RLVK.computeSetLayout)
        vkDestroyDescriptorSetLayout(RLVK.device, RLVK.computeSetLayout, RLVK_ALLOC);
    if (RLVK.computePipelineLayout)
        vkDestroyPipelineLayout(RLVK.device, RLVK.computePipelineLayout, RLVK_ALLOC);
    if (RLVK.device)
        vkDestroyDevice(RLVK.device, RLVK_ALLOC);
    if (RLVK.surface)
        vkDestroySurfaceKHR(RLVK.instance, RLVK.surface, RLVK_ALLOC);
    if (RLVK.instance)
        vkDestroyInstance(RLVK.instance, RLVK_ALLOC);
    if (RLVK.shadercCompiler)
        p_shaderc_compiler_release(RLVK.shadercCompiler);
    memset(&RLVK, 0, sizeof(RLVK));
    s_pipelineFastValid = false;
    isGpuReady = false;
}

// Load OpenGL extensions
// NOTE: External loader function must be provided
void rlLoadExtensions(void *loader) { (void)loader; }
// Get OpenGL procedure address
void *rlGetProcAddress(const char *name)
{
    (void)name;
    return NULL;
}

// Get current OpenGL version
int rlGetVersion(void) { return RL_OPENGL_43; }

void rlSetFramebufferWidth(int w) { RLVK.State.framebufferWidth = w; }
int rlGetFramebufferWidth(void) { return RLVK.State.framebufferWidth; }
// Set current framebuffer height
void rlSetFramebufferHeight(int h) { RLVK.State.framebufferHeight = h; }
// Get default framebuffer height
int rlGetFramebufferHeight(void) { return RLVK.State.framebufferHeight; }

// Get default internal texture (white texture)
// NOTE: Default texture is a 1x1 pixel UNCOMPRESSED_R8G8B8A8
unsigned int rlGetTextureIdDefault(void) { return RLVK.defaultTextureSlot; }
unsigned int rlGetShaderIdDefault(void) { return RLVK.defaultShaderSlot; }
int *rlGetShaderLocsDefault(void) { return RLVK.defaultShaderLocs; }

// Render batch management
//-----------------------------------------------------------------------------------------

rlRenderBatch rlLoadRenderBatch(int numBuffers, int bufferElements)
{
    rlRenderBatch batch = {0};
    if (!isGpuReady)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: rlLoadRenderBatch called before rlglInit");
        return batch;
    }

    batch.vertexBuffer = (rlVertexBuffer *)RL_CALLOC(numBuffers, sizeof(rlVertexBuffer));

    for (int i = 0; i < numBuffers; i++)
    {
        // Vá lỗi Leak Memory: Giải phóng backing buffer cũ nếu người dùng khởi tạo nhiều Render Batch
        rlvkDestroyBatchBacking(&RLVK.batchBacking[i % RLVK_FRAME_INDEX_COUNT]);

        if (!rlvkCreateBatchBacking(bufferElements, &RLVK.batchBacking[i % RLVK_FRAME_INDEX_COUNT]))
        {
            TRACELOG(RL_LOG_ERROR, "RLVK: failed to create batch backing buffer #%d", i);
            return batch;
        }

        rlvkBatchBackingBuffer *backing = &RLVK.batchBacking[i % RLVK_FRAME_INDEX_COUNT];
        char *base = (char *)backing->mapped;
        size_t off = 0;

        size_t posBytes = (size_t)bufferElements * 3 * 4 * sizeof(f32);
        size_t uvBytes = (size_t)bufferElements * 2 * 4 * sizeof(f32);
        size_t nrmBytes = (size_t)bufferElements * 3 * 4 * sizeof(f32);
        size_t colBytes = (size_t)bufferElements * 4 * 4 * sizeof(unsigned char);
        size_t idxBytes = (size_t)bufferElements * 6 * sizeof(unsigned int);

        batch.vertexBuffer[i].elementCount = bufferElements;
        batch.vertexBuffer[i].vertices = (f32 *)(base + off);
        off += posBytes;
        batch.vertexBuffer[i].texcoords = (f32 *)(base + off);
        off += uvBytes;
        batch.vertexBuffer[i].normals = (f32 *)(base + off);
        off += nrmBytes;
        batch.vertexBuffer[i].colors = (unsigned char *)(base + off);
        off += colBytes;
        batch.vertexBuffer[i].indices = (unsigned int *)(base + off);
        off += idxBytes;

        unsigned int *idx = batch.vertexBuffer[i].indices;
        for (int j = 0, k = 0; j < bufferElements * 6; j += 6, k++)
        {
            idx[j + 0] = 4 * k + 0;
            idx[j + 1] = 4 * k + 1;
            idx[j + 2] = 4 * k + 2;
            idx[j + 3] = 4 * k + 0;
            idx[j + 4] = 4 * k + 2;
            idx[j + 5] = 4 * k + 3;
        }

        batch.vertexBuffer[i].vaoId = 0;
        batch.vertexBuffer[i].vboId[0] = 0;
        batch.vertexBuffer[i].vboId[1] = 0;
        batch.vertexBuffer[i].vboId[2] = 0;
        batch.vertexBuffer[i].vboId[3] = 0;
        batch.vertexBuffer[i].vboId[4] = 0;
    }

    batch.draws = (rlDrawCall *)RL_CALLOC(RL_DEFAULT_BATCH_DRAWCALLS, sizeof(rlDrawCall));
    for (int i = 0; i < RL_DEFAULT_BATCH_DRAWCALLS; i++)
    {
        batch.draws[i].mode = RL_QUADS;
        batch.draws[i].textureId = RLVK.defaultTextureSlot;
    }
    batch.bufferCount = numBuffers;
    batch.drawCounter = 1;
    batch.currentDepth = -1.0f;

    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        // Cũng dọn dẹp arena cũ để tránh rò rỉ nếu re-load batch
        rlvkDestroyBatchBacking(&RLVK.arena[i]);
        if (!rlvkCreateBatchBacking(bufferElements * RLVK_ARENA_SLOTS, &RLVK.arena[i]))
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
    if (!isGpuReady)
        return;
    rlvkWaitInFlightFrames();

    for (int i = 0; i < batch.bufferCount; i++)
    {
        // CPU pointers belong to backing buffer; freeing them individually would be wrong.
        batch.vertexBuffer[i].vertices = NULL;
        batch.vertexBuffer[i].texcoords = NULL;
        batch.vertexBuffer[i].normals = NULL;
        batch.vertexBuffer[i].colors = NULL;
        batch.vertexBuffer[i].indices = NULL;
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
        if (batch)
            batch->currentDepth = -1.0f;
        RLVK.State.vertexCounter = 0;
        if (batch)
            batch->drawCounter = 1;
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
        u32 vcount = (u32)RLVK.State.vertexCounter; // vertices written this flush
        u32 icount = (vcount / 4) * 6;              // quad indices (only RL_QUADS uses them)

        // The default shader consumes no normals: skip copying the normal stream entirely
        // (a third of the batch vertex bytes) and satisfy binding 2 with the dummy buffer
        rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.currentShaderSlot];
        bool wantNormals = (shader->attribLocs[RLVK_ATTRIB_NORMAL] >= 0);

        VkDeviceSize posBytes = (VkDeviceSize)vcount * 3 * sizeof(f32);
        VkDeviceSize uvBytes = (VkDeviceSize)vcount * 2 * sizeof(f32);
        VkDeviceSize nrmBytes = wantNormals ? (VkDeviceSize)vcount * 3 * sizeof(f32) : 0;
        VkDeviceSize colBytes = (VkDeviceSize)vcount * 4 * sizeof(unsigned char);
        VkDeviceSize idxBytes = (VkDeviceSize)icount * sizeof(unsigned int);

        // Arena exhaustion is handled, never dropped: drain the recording mid-frame (the wait
        // consumes all arena data, so the arena restarts from offset 0) and record the demanded
        // size so the arena grows at the next frame boundary and steady state stops draining
        VkDeviceSize posOff, uvOff, nrmOff, colOff, idxOff;
        for (int attempt = 0;; attempt++)
        {
            posOff = (RLVK.arenaOffset[frameIndex] + 15) & ~(VkDeviceSize)15; // 16-byte align
            uvOff = posOff + posBytes;
            nrmOff = uvOff + uvBytes;
            colOff = nrmOff + nrmBytes;
            idxOff = colOff + colBytes;
            if (idxOff + idxBytes <= arena->sizeBytes)
                break;
            if (attempt > 0)
            {
                // A single flush larger than the whole arena: drop it, the growth request stands
                batch->currentDepth = -1.0f;
                RLVK.State.vertexCounter = 0;
                batch->drawCounter = 1;
                return;
            }
            rlvkFlushFrame();
            cmdBuffer = RLVK.cmdBuffers[frameIndex];
        }
        RLVK.arenaOffset[frameIndex] = idxOff + idxBytes;
        RLVK.arenaWanted[frameIndex] += (idxOff + idxBytes) - posOff + 16; // this flush's arena demand

        char *dst = (char *)arena->mapped;
        memcpy(dst + posOff, srcvb->vertices, (size_t)posBytes);
        // (The old +0.5px horizontal-line tie-break nudge is GONE: every scope now rasterizes
        // in GL's memory orientation - positive viewport, flip at present - so boundary
        // tie-breaks match GL natively, in 2D and 3D, for lines, triangles, and points.)
        memcpy(dst + uvOff, srcvb->texcoords, (size_t)uvBytes);
        if (nrmBytes)
            memcpy(dst + nrmOff, srcvb->normals, (size_t)nrmBytes);
        memcpy(dst + colOff, srcvb->colors, (size_t)colBytes);
        if (idxBytes)
            memcpy(dst + idxOff, srcvb->indices, (size_t)idxBytes);

        s_bindingValid = false; // batch flush rebinds vertex buffers outside the mesh-draw dedup cache

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
        Matrix matModelView = RLVK.State.modelview;
        int eyeCount = RLVK.State.stereoRender ? 2 : 1;
        for (int eye = 0; eye < eyeCount; eye++)
        {
            if (eyeCount == 2)
            {
                rlViewport(eye * RLVK.State.framebufferWidth / 2, 0, RLVK.State.framebufferWidth / 2, RLVK.State.framebufferHeight);
                RLVK.State.modelview = rlvkMatrixMultiply(matModelView, RLVK.State.viewOffsetStereo[eye]);
                RLVK.State.projection = RLVK.State.projectionStereo[eye];
            }

            rlvkPushConstants pc = {0};
            Matrix matMVP = rlvkMatrixMultiply(RLVK.State.modelview, RLVK.State.projection);
            // Fill the MVP in rlMatrixToFloat order (column-major floats), exactly as the GL backend
            // passes it to glUniformMatrix4fv. NOTE: this is NOT raylib's struct memory order (its transpose).
            pc.mvp[0] = matMVP.m0;
            pc.mvp[1] = matMVP.m1;
            pc.mvp[2] = matMVP.m2;
            pc.mvp[3] = matMVP.m3;
            pc.mvp[4] = matMVP.m4;
            pc.mvp[5] = matMVP.m5;
            pc.mvp[6] = matMVP.m6;
            pc.mvp[7] = matMVP.m7;
            pc.mvp[8] = matMVP.m8;
            pc.mvp[9] = matMVP.m9;
            pc.mvp[10] = matMVP.m10;
            pc.mvp[11] = matMVP.m11;
            pc.mvp[12] = matMVP.m12;
            pc.mvp[13] = matMVP.m13;
            pc.mvp[14] = matMVP.m14;
            pc.mvp[15] = matMVP.m15;
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
                    if (L[SHADER_LOC_MATRIX_MVP] >= 0)
                        rlvkShaderWriteUniform(shader, L[SHADER_LOC_MATRIX_MVP], pc.mvp, 64);
                    if (L[SHADER_LOC_MATRIX_PROJECTION] >= 0)
                        rlvkShaderWriteMatrixUniform(shader, L[SHADER_LOC_MATRIX_PROJECTION], RLVK.State.projection);
                    if (L[SHADER_LOC_MATRIX_VIEW] >= 0)
                        rlvkShaderWriteMatrixUniform(shader, L[SHADER_LOC_MATRIX_VIEW], RLVK.State.modelview);
                    if (L[SHADER_LOC_MATRIX_MODEL] >= 0)
                        rlvkShaderWriteMatrixUniform(shader, L[SHADER_LOC_MATRIX_MODEL], RLVK.State.transform);
                    if (L[SHADER_LOC_MATRIX_NORMAL] >= 0)
                        rlvkShaderWriteMatrixUniform(shader, L[SHADER_LOC_MATRIX_NORMAL], rlvkMatrixTranspose(rlvkMatrixInvert(RLVK.State.transform)));
                    if (L[SHADER_LOC_COLOR_DIFFUSE] >= 0)
                        rlvkShaderWriteUniform(shader, L[SHADER_LOC_COLOR_DIFFUSE], pc.colDiffuse, 16);
                }
                // NOTE: for a UBO shader the whole set-0 (UBO + binding-0 texture + samplers +
                // SSBOs) is issued as ONE coalesced CmdPushDescriptorSetKHR per draw, inside the
                // loop below after the pipeline bind (rlvkPushSet0Batch). MoltenVK drops the SECOND
                // separate push-descriptor call of a draw, so pushing the UBO here and the texture
                // later as two calls lost the texture (HANDOFF §7.26).
            }
            else
                vk.CmdPushConstants(cmdBuffer, RLVK.pipelineLayout,
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

            // Same per-draw structure as the GL backend: LINES/TRIANGLES non-indexed, QUADS indexed.
            // The draw's texture is pushed at unit 0 (rlgl texture-unit semantics).
            for (int i = 0, vertexOffset = 0; i < batch->drawCounter; i++)
            {
                rlDrawCall *drawCall = &batch->draws[i];
                if (rlvkDebugFlag("RLVK_DEBUG_FLUSH", &s_dbgFlush))
                    TRACELOG(RL_LOG_WARNING,
                             "VKDBG flush draw %d/%d mode=%d verts=%d tex=%u shader=%u scope=%u depthT=%d vtxCtr=%d",
                             i, batch->drawCounter, drawCall->mode, drawCall->vertexCount, drawCall->textureId,
                             RLVK.State.currentShaderSlot, RLVK.scope.fbSlot, (int)RLVK.State.depthTest, RLVK.State.vertexCounter);
                // EXP: push set-0 BEFORE binding the ground pipeline (while the prior pipeline is
                // still bound); compatible layouts mean the push must persist across the bind.
                if (shader->usesUbo && drawCall->vertexCount > 0 && getenv("RLVK_EXP_PUSH_BEFORE_PIPE"))
                    rlvkPushSet0Batch(cmdBuffer, shader, drawCall->textureId);
                // A failed pipeline build leaves the previous pipeline bound; drawing this batch
                // segment with it would rasterize it under the WRONG shader (a custom-shader VFX
                // quad shows as an opaque square). Skip the segment's draw when the build fails.
                if ((drawCall->vertexCount > 0) &&
                    rlvkBindPipeline(cmdBuffer, (drawCall->mode == RL_LINES) ? 0 : 1,
                                     RLVK_VLAYOUT_BATCH, RLVK.State.currentShaderSlot))
                {
                    // GL bind-at-draw semantics, atomically. A UBO shader coalesces its entire
                    // set-0 into one push AFTER the pipeline bind (defeats the MoltenVK 2nd-push
                    // drop, §7.26); the default shader pushes just its texture0 (a single call).
                    if (shader->usesUbo)
                    {
                        if (!getenv("RLVK_EXP_PUSH_BEFORE_PIPE"))
                            rlvkPushSet0Batch(cmdBuffer, shader, drawCall->textureId);
                    }
                    else
                        rlvkPushTexture(cmdBuffer, 0, drawCall->textureId);
                    rlvkFlushSet0(cmdBuffer);
                    if (!batchBuffersBound)
                    {
                        vkCmdBindVertexBuffers(cmdBuffer, 0, 4,
                                               (VkBuffer[]){arena->buffer, arena->buffer, wantNormals ? arena->buffer : RLVK.bufferSlots[RLVK.dummyAttribSlot].buffer, arena->buffer},
                                               (VkDeviceSize[]){posOff, uvOff, wantNormals ? nrmOff : 12, colOff});
                        vkCmdBindIndexBuffer(cmdBuffer, arena->buffer, idxOff, VK_INDEX_TYPE_UINT32);
                        rlvkBindDummyAttribBuffers(cmdBuffer, RLVK_VLAYOUT_BATCH, shader);
                        batchBuffersBound = true;
                    }
                    if (getenv("RLVK_DBG_DRAWSITE")) { fprintf(stderr, "[DRAW] pre mode=%d verts=%d ubo=%d slot=%u\n", drawCall->mode, drawCall->vertexCount, (int)shader->usesUbo, RLVK.State.currentShaderSlot); fflush(stderr); }
                    if ((drawCall->mode == RL_LINES) || (drawCall->mode == RL_TRIANGLES))
                        vk.CmdDraw(cmdBuffer, drawCall->vertexCount, 1, vertexOffset, 0);
                    else // RL_QUADS -> 2 triangles per quad via the index buffer
                        vk.CmdDrawIndexed(cmdBuffer, drawCall->vertexCount / 4 * 6, 1, vertexOffset / 4 * 6, 0, 0);
                    if (getenv("RLVK_DBG_DRAWSITE")) { fprintf(stderr, "[DRAW] post\n"); fflush(stderr); }
                }
                vertexOffset += drawCall->vertexCount + drawCall->vertexAlignment;
            }
        } // eye loop

        // Restore viewport and matrices to pre-stereo state (mirrors rlgl)
        if (eyeCount == 2)
            rlViewport(0, 0, RLVK.State.framebufferWidth, RLVK.State.framebufferHeight);
        RLVK.State.projection = matProjection;
        RLVK.State.modelview = matModelView;

        // Cycle to the next backing buffer so the next frame doesn't overwrite in-flight data
        batch->currentBuffer++;
        if (batch->currentBuffer >= batch->bufferCount)
            batch->currentBuffer = 0;
    }

    // Reset batch buffers
    //------------------------------------------------------------------------------------------------------------
    // Reset vertex counter for next frame
    RLVK.State.vertexCounter = 0;
    batch->currentDepth = -1.0f;
    for (int i = 0; i < RL_DEFAULT_BATCH_DRAWCALLS; i++)
    {
        batch->draws[i].mode = RL_QUADS;
        batch->draws[i].vertexCount = 0;
        batch->draws[i].textureId = RLVK.defaultTextureSlot;
    }
    batch->drawCounter = 1;
}

// Set the active render batch for rlgl
void rlSetRenderBatchActive(rlRenderBatch *batch) { RLVK.currentBatch = (batch != NULL) ? batch : &RLVK.defaultBatch; }
// Update and draw internal render batch
void rlDrawRenderBatchActive(void) { rlDrawRenderBatch(RLVK.currentBatch); }

// Check internal buffer overflow for a given number of vertex
// and force a rlRenderBatch draw call if required
bool rlCheckRenderBatchLimit(int vCount)
{
    if (!RLVK.currentBatch)
        return false;
    rlVertexBuffer *vb = &RLVK.currentBatch->vertexBuffer[RLVK.currentBatch->currentBuffer];
    if ((RLVK.State.vertexCounter + vCount) >= (vb->elementCount * 4))
    {
        int currentMode = RLVK.currentBatch->draws[RLVK.currentBatch->drawCounter - 1].mode;
        unsigned int curTx = RLVK.currentBatch->draws[RLVK.currentBatch->drawCounter - 1].textureId;
        rlDrawRenderBatch(RLVK.currentBatch);
        RLVK.currentBatch->draws[RLVK.currentBatch->drawCounter - 1].mode = currentMode;
        RLVK.currentBatch->draws[RLVK.currentBatch->drawCounter - 1].textureId = curTx;
        return true;
    }
    return false;
}

// Set current texture to use
void rlSetTexture(unsigned int id)
{
    rlRenderBatch *batch = RLVK.currentBatch;
    if (!batch)
        return;

    if (id == 0)
    {
        if (RLVK.State.vertexCounter >= batch->vertexBuffer[batch->currentBuffer].elementCount * 4)
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
        if (batch->drawCounter >= RL_DEFAULT_BATCH_DRAWCALLS)
            rlDrawRenderBatch(batch);

        batch->draws[batch->drawCounter - 1].textureId = id;
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
            if (size > arena->sizeBytes)
            {
                TRACELOG(RL_LOG_WARNING, "RLVK: buffer upload larger than arena");
                return;
            }
        }
        memcpy((char *)arena->mapped + off, data, size);
        RLVK.arenaOffset[frameIndex] = off + size;
        RLVK.arenaWanted[frameIndex] += size + 16;

        u32 openFb = RLVK.scope.fbSlot;
        if (openFb)
            rlDisableFramebuffer();
        vkCmdEndRenderPass(cmdBuffer);

        vkCmdCopyBuffer(cmdBuffer, arena->buffer, dst, 1,
                        &(VkBufferCopy){.srcOffset = off, .dstOffset = dstOffset, .size = size});
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                              VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                              .bufferMemoryBarrierCount = 1,
                                              .pBufferMemoryBarriers = &(VkBufferMemoryBarrier2){
                                                  VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                  // glBufferSubData semantics: EVERY later consumer sees the data.
                                                  // SSBOs ride this path too (rlUpdateShaderBuffer mid-frame -> the
                                                  // GPU-particle vertex shader reads them as storage buffers);
                                                  // vertex-attribute/index visibility alone left those reads STALE
                                                  // (invisible particles - silently wrong, no validation error).
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT
                                                                | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                                                                | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT
                                                                 | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT,
                                                  .buffer = dst,
                                                  .offset = dstOffset,
                                                  .size = size,
                                              },
                                          });

        rlvkResumeSwapchainScope(cmdBuffer);
        if (openFb)
            rlEnableFramebuffer(openFb);
        return;
    }

    // Load time: Tối ưu hoàn toàn để loại bỏ overhead, ngăn ngừa hỏng state
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    void *map = NULL;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
                              &(VkBufferCreateInfo){VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                    .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              RLVK_ALLOC, &staging));
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(RLVK.device, staging, &memReq);
    stagingMem = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(RLVK.device, staging, stagingMem, 0);
    vkMapMemory(RLVK.device, stagingMem, 0, size, 0, &map);
    memcpy(map, data, size);
    vkUnmapMemory(RLVK.device, stagingMem);

    // Tối ưu: Dùng Command Pool tạm (Transient) thay vì can thiệp vào Frame Ring đang nghỉ
    VkCommandPool tempPool = VK_NULL_HANDLE;
    vkCreateCommandPool(RLVK.device, &(VkCommandPoolCreateInfo){
                                         VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                         .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                         .queueFamilyIndex = RLVK.graphicsFamily,
                                     },
                        RLVK_ALLOC, &tempPool);

    VkCommandBuffer tempCmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(RLVK.device, &(VkCommandBufferAllocateInfo){
                                              VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                              .commandPool = tempPool,
                                              .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                              .commandBufferCount = 1,
                                          },
                             &tempCmd);

    vk.BeginCommandBuffer(tempCmd, &(VkCommandBufferBeginInfo){
                                       VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});

    vkCmdCopyBuffer(tempCmd, staging, dst, 1, &(VkBufferCopy){.dstOffset = dstOffset, .size = size});
    vk.EndCommandBuffer(tempCmd);

    // Bỏ `rlvkWaitInFlightFrames()` tại đây vì nó gây thắt cổ chai vô ích cho GPU
    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){
                                               VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                               .commandBufferInfoCount = 1,
                                               .pCommandBufferInfos = &(VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = tempCmd},
                                           },
                    VK_NULL_HANDLE);

    // Đồng bộ bằng vkQueueWaitIdle: gọn và ít tốn object hơn vkWaitForFences
    vkQueueWaitIdle(RLVK.graphicsQueue);

    vkDestroyCommandPool(RLVK.device, tempPool, RLVK_ALLOC);
    vkDestroyBuffer(RLVK.device, staging, RLVK_ALLOC);
    vkFreeMemory(RLVK.device, stagingMem, RLVK_ALLOC);
}
