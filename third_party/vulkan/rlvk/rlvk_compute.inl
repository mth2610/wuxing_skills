//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Compute dispatch, shader buffers (SSBO), matrix state, draw stubs
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

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
    // Bail on a failed build rather than blit with a stale pipeline (better a black frame).
    if (!rlvkBindPipeline(cmdBuffer, 2, RLVK_VLAYOUT_QUAD, RLVK.State.activeShaderSlot)) return;
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

