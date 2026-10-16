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
/* Out-of-frame compute dispatches used to pay a full command pool + submit +
 * vkQueueWaitIdle + pool destroy EACH. perf_dispatch_count measured ~0.6 ms per
 * dispatch, which is what made a 9-dispatch PBD solve cost 4.4 ms for 72
 * workgroups of real work. Batch them into one command buffer instead: the
 * per-dispatch memory barriers already recorded between them give the same
 * ordering the separate submits did, at pipeline-barrier cost rather than
 * queue-drain cost. */
static VkCommandBuffer rlvkComputeBatchAcquire(void)
{
    if (RLVK.computeBatch.open)
        return RLVK.computeBatch.cmd;

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    if (!RLVK.frameActive)
    {
        // Out-of-frame dispatches run before rlvkBeginFrame waits on this slot's fence or
        // resets its descriptor pool and bump arena. Ensure the slot is idle and fresh now.
        if (RLVK.frameFences[frameIndex] != VK_NULL_HANDLE)
            vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[frameIndex], VK_TRUE, UINT64_MAX);
        if (RLVK.computeDescPools[frameIndex] != VK_NULL_HANDLE)
            vkResetDescriptorPool(RLVK.device, RLVK.computeDescPools[frameIndex], 0);
        RLVK.arenaOffset[frameIndex] = 0;
    }

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = rlvkOneShotBegin(&pool);
    if (cmd == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    // Make host writes to the arena (compute uniform snapshots) visible to the compute stage
    vk.CmdPipelineBarrier2(cmd, &(VkDependencyInfo){
                                    VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                    .memoryBarrierCount = 1,
                                    .pMemoryBarriers = &(VkMemoryBarrier2){
                                        VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                        .srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
                                        .srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT,
                                        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                                        .dstAccessMask = VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                    },
                                });

    RLVK.computeBatch.pool = pool;
    RLVK.computeBatch.cmd = cmd;
    RLVK.computeBatch.open = true;
    return cmd;
}

/* Submit and WAIT. Every caller either needs the results now or is about to free
 * something the batch references — there is no lazy variant on purpose. */
static void rlvkComputeBatchFlush(void)
{
    if (!RLVK.computeBatch.open)
        return;
    VkCommandPool pool = RLVK.computeBatch.pool;
    VkCommandBuffer cmd = RLVK.computeBatch.cmd;
    /* Cleared BEFORE the submit: rlvkOneShotEnd must not see an open batch. */
    RLVK.computeBatch.open = false;
    RLVK.computeBatch.pool = VK_NULL_HANDLE;
    RLVK.computeBatch.cmd = VK_NULL_HANDLE;
    rlvkOneShotEnd(pool, cmd);
}

void rlComputeShaderDispatch(unsigned int gx, unsigned int gy, unsigned int gz)
{
    if (!isGpuReady || gx == 0 || gy == 0 || gz == 0 ||
        RLVK.State.activeShaderSlot == 0 || RLVK.State.activeShaderSlot >= RLVK_MAX_SHADER_SLOTS)
        return;

    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];
    if (!shader->isCompute || (shader->computePipeline == VK_NULL_HANDLE))
    {
        // A silent no-op here looks like "physics frozen" upstream and is brutal to trace
        // (see RLVK_HANDOFF §7.7 epilogue) - name the reason, once per offending slot
        static u32 s_warnedSlot = 0;
        if (s_warnedSlot != RLVK.State.activeShaderSlot)
        {
            s_warnedSlot = RLVK.State.activeShaderSlot;
            TRACELOG(RL_LOG_WARNING, "RLVK: rlComputeShaderDispatch on slot %u skipped (%s) - dispatch is a no-op",
                     RLVK.State.activeShaderSlot,
                     !shader->isCompute ? "not a compute program (slot recycled?)" : "no compute pipeline");
        }
        return;
    }

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    bool inFrame = RLVK.frameActive;
    VkDeviceSize uboNeed = (shader->vsBlockSize && shader->vsStage)
                               ? (((VkDeviceSize)shader->vsBlockSize + 255) & ~(VkDeviceSize)255) + 256
                               : 0;

    if (inFrame && uboNeed > 0)
    {
        rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
        if (RLVK.arenaOffset[frameIndex] + uboNeed > arena->sizeBytes)
        {
            RLVK.arenaWanted[frameIndex] += uboNeed;
            if (uboNeed <= arena->sizeBytes)
                rlvkFlushFrame();
        }
    }
    else if (!inFrame && RLVK.computeBatch.open && uboNeed > 0)
    {
        rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
        if (RLVK.arenaOffset[frameIndex] + uboNeed > arena->sizeBytes)
        {
            RLVK.arenaWanted[frameIndex] += uboNeed;
            rlvkComputeBatchFlush();
        }
    }

    VkCommandBuffer cmdBuffer;
    u32 openFb = 0;
    if (inFrame)
    {
        cmdBuffer = RLVK.cmdBuffers[frameIndex];
        openFb = RLVK.scope.fbSlot;
        if (openFb)
            rlDisableFramebuffer();
        rlvkProfileEndScope();
        vkCmdEndRenderPass(cmdBuffer);
    }
    else
    {
        /* Out of frame (init-time seeding, and every gameplay-update dispatch —
         * main.c updates physics before BeginDrawing). Joins the open batch. */
        cmdBuffer = rlvkComputeBatchAcquire();
        if (cmdBuffer == VK_NULL_HANDLE)
            return;
    }

    // Snapshot descriptor set: bound SSBOs, sampler uniforms, uniform block
    VkDescriptorSet ds = VK_NULL_HANDLE;
    VkResult allocRes = vkAllocateDescriptorSets(RLVK.device,
                                                 &(VkDescriptorSetAllocateInfo){
                                                     VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                     .descriptorPool = RLVK.computeDescPools[frameIndex],
                                                     .descriptorSetCount = 1,
                                                     .pSetLayouts = &RLVK.computeSetLayout,
                                                 },
                                                 &ds);
    if (allocRes != VK_SUCCESS && !inFrame)
    {
        // Out-of-frame batch exhausted the per-frame compute pool: flush batch, reset pool, retry
        rlvkComputeBatchFlush();
        cmdBuffer = rlvkComputeBatchAcquire();
        if (cmdBuffer == VK_NULL_HANDLE)
            return;
        allocRes = vkAllocateDescriptorSets(RLVK.device,
                                            &(VkDescriptorSetAllocateInfo){
                                                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                .descriptorPool = RLVK.computeDescPools[frameIndex],
                                                .descriptorSetCount = 1,
                                                .pSetLayouts = &RLVK.computeSetLayout,
                                            },
                                            &ds);
    }

    if (ds != VK_NULL_HANDLE)
    {
        VkWriteDescriptorSet writes[11]; // 8 SSBO (0..7) + 2 Samplers (12..13) + 1 UBO (14)
        VkDescriptorBufferInfo bufInfos[9];
        VkDescriptorImageInfo imgInfos[2];
        u32 nWrites = 0, nBuf = 0;

        // Every SSBO binding gets a valid buffer (unbound slots alias the arena)
        for (u32 i = 0; i < 8; i++)
        {
            u32 slot = RLVK.computeSSBO[i];
            bool bound = (slot && slot < RLVK_MAX_BUFFER_SLOTS && RLVK.bufferSlots[slot].buffer != VK_NULL_HANDLE);
            bufInfos[nBuf] = bound
                                 ? (VkDescriptorBufferInfo){RLVK.bufferSlots[slot].buffer, 0, RLVK.bufferSlots[slot].sizeBytes}
                                 : (VkDescriptorBufferInfo){RLVK.arena[frameIndex].buffer, 0, 16};
            writes[nWrites++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
                                                       .dstBinding = i, .descriptorCount = 1,
                                                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                       .pBufferInfo = &bufInfos[nBuf]};
            nBuf++;
        }

        // Resolve bindings 12 and 13 once (avoiding duplicate writes to the same binding in one update)
        rlvkTextureSlot *dt = &RLVK.textureSlots[RLVK.defaultTextureSlot];
        for (u32 s = 0; s < 2; s++)
        {
            imgInfos[s] = (VkDescriptorImageInfo){
                .sampler = dt->sampler,
                .imageView = dt->view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        for (int u = 0; u < shader->uniformCount; u++)
        {
            int b = shader->uniforms[u].samplerBinding;
            if (b < 12 || b > 13)
                continue;
            u32 texSlot = shader->bindingTexture[b];
            if (texSlot == 0)
            {
                int unit = shader->bindingUnit[b];
                if (unit >= 0 && unit < RLVK_MAX_TEXTURE_UNITS)
                    texSlot = RLVK.State.activeTextureSlots[unit];
            }
            rlvkTextureSlot *t = &RLVK.textureSlots[(texSlot && texSlot < RLVK_MAX_TEXTURE_SLOTS) ? texSlot : RLVK.defaultTextureSlot];
            VkImageView view = t->sampleImage ? t->sampleView : t->view;
            VkImageLayout layout = t->sampleImage ? t->sampleLayout : t->currentLayout;
            VkSampler sampler = t->sampler;
            if (t->sampleImage)
                t->sampleWantedFrame = RLVK.frameCounter + 1;
            if (layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || !view)
            {
                view = dt->view;
                sampler = dt->sampler;
            }
            imgInfos[b - 12] = (VkDescriptorImageInfo){
                .sampler = sampler,
                .imageView = view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        for (u32 s = 0; s < 2; s++)
        {
            writes[nWrites++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
                                                       .dstBinding = 12 + s, .descriptorCount = 1,
                                                       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                       .pImageInfo = &imgInfos[s]};
        }

        // Loose-uniform block (binding 14): snapshot the staging block into the frame arena
        bool uboWritten = false;
        if (shader->vsBlockSize && shader->vsStage)
        {
            rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
            VkDeviceSize off = (RLVK.arenaOffset[frameIndex] + 255) & ~(VkDeviceSize)255;
            if (off + shader->vsBlockSize <= arena->sizeBytes)
            {
                memcpy((char *)arena->mapped + off, shader->vsStage, shader->vsBlockSize);
                RLVK.arenaOffset[frameIndex] = off + shader->vsBlockSize;
                RLVK.arenaWanted[frameIndex] += shader->vsBlockSize + 256;
                bufInfos[nBuf] = (VkDescriptorBufferInfo){arena->buffer, off, shader->vsBlockSize};
                writes[nWrites++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
                                                           .dstBinding = 14, .descriptorCount = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .pBufferInfo = &bufInfos[nBuf]};
                nBuf++;
                uboWritten = true;
            }
            else
                TRACELOG(RL_LOG_WARNING, "RLVK: arena full, compute uniforms skipped this dispatch");
        }
        if (!uboWritten)
        {
            bufInfos[nBuf] = (VkDescriptorBufferInfo){RLVK.arena[frameIndex].buffer, 0, 16};
            writes[nWrites++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
                                                       .dstBinding = 14, .descriptorCount = 1,
                                                       .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                       .pBufferInfo = &bufInfos[nBuf]};
            nBuf++;
        }
        if (nWrites)
            vkUpdateDescriptorSets(RLVK.device, nWrites, writes, 0, NULL);

        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->computePipeline);
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, RLVK.computePipelineLayout, 0, 1, &ds, 0, NULL);
        if (rlvkDebugFlag("RLVK_PROFILE", &s_dbgProfile))
            s_profileDispatches++;
        vkCmdDispatch(cmdBuffer, gx, gy, gz);

        // glMemoryBarrier semantics: compute writes visible to subsequent compute dispatches and graphics stages
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                              VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                              .memoryBarrierCount = 1,
                                              .pMemoryBarriers = &(VkMemoryBarrier2){
                                                  VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT,
                                              },
                                          });
    }
    else
        TRACELOG(RL_LOG_WARNING, "RLVK: compute descriptor pool exhausted - raise RLVK_COMPUTE_SETS_PER_FRAME");

    if (inFrame)
    {
        rlvkResumeSwapchainScope(cmdBuffer);
        if (openFb)
            rlEnableFramebuffer(openFb);
    }
    /* Else: left OPEN in the batch. It is submitted by rlvkComputeBatchFlush,
     * which every path that must observe the result already calls. */
}

// Load shader storage buffer object (SSBO): DEVICE_LOCAL storage buffer, optional initial data
unsigned int rlLoadShaderBuffer(unsigned int size, const void *data, int usageHint)
{
    if (!isGpuReady || size == 0)
        return RLVK_INVALID_SLOT;
    u32 slot = rlvkAllocBufferSlot();
    if (slot == RLVK_INVALID_SLOT)
        return RLVK_INVALID_SLOT;
    rlvkBufferSlot *b = &RLVK.bufferSlots[slot];
    if (b->buffer != VK_NULL_HANDLE)
    {
        if (b->mapped && b->memory)
            vkUnmapMemory(RLVK.device, b->memory);
        rlvkDeferDestroy(b->buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, b->memory, VK_NULL_HANDLE);
        b->buffer = VK_NULL_HANDLE;
        b->memory = VK_NULL_HANDLE;
        b->mapped = NULL;
    }
    b->sizeBytes = size;
    b->usageHint = usageHint;
    b->isIndex = false;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
                              &(VkBufferCreateInfo){VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                    .size = size,
                                                    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, // particle systems draw straight from their SSBO
                                                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              RLVK_ALLOC, &b->buffer));
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(RLVK.device, b->buffer, &memReq);
    b->memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindBufferMemory(RLVK.device, b->buffer, b->memory, 0);
    if (data)
        rlvkUploadBuffer(b->buffer, 0, data, size);
    return slot;
}

// Unload shader storage buffer object (SSBO): fence-gated like every other GPU object
void rlUnloadShaderBuffer(unsigned int id)
{
    if (id == 0 || id >= RLVK_MAX_BUFFER_SLOTS)
        return;
    rlvkBufferSlot *b = &RLVK.bufferSlots[id];
    if (b->mapped && b->memory)
        vkUnmapMemory(RLVK.device, b->memory);
    rlvkDeferDestroy(b->buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, b->memory, VK_NULL_HANDLE);
    b->buffer = VK_NULL_HANDLE;
    b->memory = VK_NULL_HANDLE;
    b->mapped = NULL;
    b->sizeBytes = 0;
    b->inUse = false;
    b->freedFrame = RLVK.frameCounter;
    for (u32 i = 0; i < 8; i++)
    {
        if (RLVK.computeSSBO[i] == id)
        {
            RLVK.computeSSBO[i] = 0;
            if (i < RLVK_SET0_SSBO_COUNT)
            {
                RLVK.pushedSsbo[i] = 0xFFFFFFFFu;
                RLVK.set0Dirty = true;
            }
        }
    }
    s_bindingValid = false;
}

// Update SSBO buffer data (in-stream mid-frame, one-shot staging at load time)
void rlUpdateShaderBuffer(unsigned int id, const void *data, unsigned int dataSize, unsigned int offset)
{
    if (id == 0 || id >= RLVK_MAX_BUFFER_SLOTS || !data || !dataSize)
        return;
    rlvkBufferSlot *b = &RLVK.bufferSlots[id];
    if (b->buffer == VK_NULL_HANDLE || offset + dataSize > b->sizeBytes)
        return;
    rlvkUploadBuffer(b->buffer, offset, data, dataSize);
}

// Bind SSBO at an indexed binding point (GL glBindBufferBase semantics, consumed at dispatch)
void rlBindShaderBuffer(unsigned int id, unsigned int index)
{
    if (index >= 8)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: SSBO binding %u out of range (max 8)", index);
        return;
    }
    u32 newSlot = (id < RLVK_MAX_BUFFER_SLOTS) ? id : 0;
    if (RLVK.computeSSBO[index] != newSlot)
    {
        RLVK.computeSSBO[index] = newSlot;
        if (index < RLVK_SET0_SSBO_COUNT)
            RLVK.set0Dirty = true; // graphics draws mirror bindings 0..3 into set 0
    }
}

// Read SSBO buffer data (GPU->CPU): synchronous, GL glGetBufferSubData semantics
void rlReadShaderBuffer(unsigned int id, void *dest, unsigned int count, unsigned int offset)
{
    if (id == 0 || id >= RLVK_MAX_BUFFER_SLOTS || !dest || !count)
        return;
    rlvkBufferSlot *b = &RLVK.bufferSlots[id];
    if (b->buffer == VK_NULL_HANDLE || offset + count > b->sizeBytes)
        return;

    // Execute everything recorded so far, then drain, so the read observes GL command order
    rlvkFlushFrame();
    rlvkWaitInFlightFrames();

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    void *map = NULL;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
                              &(VkBufferCreateInfo){VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                    .size = count, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              RLVK_ALLOC, &staging));
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(RLVK.device, staging, &memReq);
    stagingMem = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (stagingMem == VK_NULL_HANDLE)
    {
        vkDestroyBuffer(RLVK.device, staging, RLVK_ALLOC);
        return;
    }
    vkBindBufferMemory(RLVK.device, staging, stagingMem, 0);

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = rlvkOneShotBegin(&pool);
    if (cmdBuffer != VK_NULL_HANDLE)
    {
        vkCmdCopyBuffer(cmdBuffer, b->buffer, staging, 1, &(VkBufferCopy){.srcOffset = offset, .size = count});
        rlvkOneShotEnd(pool, cmdBuffer);
        vkMapMemory(RLVK.device, stagingMem, 0, count, 0, &map);
        memcpy(dest, map, count);
        vkUnmapMemory(RLVK.device, stagingMem);
    }
    vkDestroyBuffer(RLVK.device, staging, RLVK_ALLOC);
    vkFreeMemory(RLVK.device, stagingMem, RLVK_ALLOC);
    s_memHostBytes -= (ill)memReq.size;
}

// Copy SSBO data between buffers (GPU-side)
void rlCopyShaderBuffer(unsigned int destId, unsigned int srcId, unsigned int destOffset, unsigned int srcOffset, unsigned int count)
{
    if (destId == 0 || destId >= RLVK_MAX_BUFFER_SLOTS || srcId == 0 || srcId >= RLVK_MAX_BUFFER_SLOTS || !count)
        return;
    rlvkBufferSlot *dst = &RLVK.bufferSlots[destId], *src = &RLVK.bufferSlots[srcId];
    if (dst->buffer == VK_NULL_HANDLE || src->buffer == VK_NULL_HANDLE)
        return;
    if (srcOffset + count > src->sizeBytes || destOffset + count > dst->sizeBytes)
        return;

    if (RLVK.frameActive)
    {
        // In-stream: suspend the scope like every other mid-frame copy
        u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
        VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
        u32 openFb = RLVK.scope.fbSlot;
        if (openFb)
            rlDisableFramebuffer();
        rlvkProfileEndScope();
        vkCmdEndRenderPass(cmdBuffer);
        vkCmdCopyBuffer(cmdBuffer, src->buffer, dst->buffer, 1,
                        &(VkBufferCopy){.srcOffset = srcOffset, .dstOffset = destOffset, .size = count});
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                              VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                              .memoryBarrierCount = 1,
                                              .pMemoryBarriers = &(VkMemoryBarrier2){
                                                  VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
                                              },
                                          });
        rlvkResumeSwapchainScope(cmdBuffer);
        if (openFb)
            rlEnableFramebuffer(openFb);
    }
    else if (RLVK.computeBatch.open)
    {
        // Append to the open out-of-frame compute batch instead of forcing a queue drain
        VkCommandBuffer cmdBuffer = RLVK.computeBatch.cmd;
        vkCmdCopyBuffer(cmdBuffer, src->buffer, dst->buffer, 1,
                        &(VkBufferCopy){.srcOffset = srcOffset, .dstOffset = destOffset, .size = count});
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                              VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                              .memoryBarrierCount = 1,
                                              .pMemoryBarriers = &(VkMemoryBarrier2){
                                                  VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                              },
                                          });
    }
    else
    {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmdBuffer = rlvkOneShotBegin(&pool);
        if (cmdBuffer == VK_NULL_HANDLE)
            return;
        vkCmdCopyBuffer(cmdBuffer, src->buffer, dst->buffer, 1,
                        &(VkBufferCopy){.srcOffset = srcOffset, .dstOffset = destOffset, .size = count});
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
    (void)format;
    (void)readonly;
    if (index >= 4)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: image unit %u out of range (max 4)", index);
        return;
    }
    RLVK.computeImage[index] = (id < RLVK_MAX_TEXTURE_SLOTS) ? id : 0;
    static bool warned = false;
    if (!warned)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: rlBindImageTexture recorded but compute image load/store is not wired yet");
        warned = true;
    }
}

// Matrix state management
//-----------------------------------------------------------------------------------------

// Get internal modelview matrix
Matrix rlGetMatrixModelview(void) { return RLVK.State.modelview; }
// Get internal projection matrix
Matrix rlGetMatrixProjection(void) { return RLVK.State.projection; }
// Get internal accumulated transform matrix
Matrix rlGetMatrixTransform(void) { return RLVK.State.transform; }
// Get internal projection matrix for stereo render (selected eye)
Matrix rlGetMatrixProjectionStereo(int eye) { return RLVK.State.projectionStereo[eye & 1]; }
// Get internal view offset matrix for stereo render (selected eye)
Matrix rlGetMatrixViewOffsetStereo(int eye) { return RLVK.State.viewOffsetStereo[eye & 1]; }

// Set a custom projection matrix (replaces internal projection matrix)
// §7.21's pre-rotation compensation here was reverted 2026-07-18 (see rlvkAttachSurface in
// rlvk_platform.inl for why) - RLVK.preRotationQuarterTurns is now always 0, so this is back to
// a plain assignment.
void rlSetMatrixProjection(Matrix p)
{
    RLVK.State.projection = p;
}
void rlSetMatrixModelview(Matrix v) { RLVK.State.modelview = v; }
// Set eyes projection matrices for stereo rendering
void rlSetMatrixProjectionStereo(Matrix r, Matrix l)
{
    RLVK.State.projectionStereo[0] = r;
    RLVK.State.projectionStereo[1] = l;
}
// Set eyes view offsets matrices for stereo rendering
void rlSetMatrixViewOffsetStereo(Matrix r, Matrix l)
{
    RLVK.State.viewOffsetStereo[0] = r;
    RLVK.State.viewOffsetStereo[1] = l;
}

// Default internal resources (default texture, default shader)
//-----------------------------------------------------------------------------------------

// Load and draw a cube in NDC
void rlLoadDrawCube(void) {}
// Draw a fullscreen NDC quad (interleaved pos3+uv2, triangle strip) with the CURRENT shader -
// used by post-processing / deferred lighting passes. The quad VBO is created once and cached.
void rlLoadDrawQuad(void)
{
    if (!isGpuReady)
        return;
    static const f32 quadVerts[] = {
        -1.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        -1.0f,
        -1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        -1.0f,
        0.0f,
        1.0f,
        0.0f,
    };
    static u32 quadVbo = 0;
    if (quadVbo == 0 || quadVbo >= RLVK_MAX_BUFFER_SLOTS ||
        !RLVK.bufferSlots[quadVbo].inUse ||
        RLVK.bufferSlots[quadVbo].buffer == VK_NULL_HANDLE)
    {
        quadVbo = rlvkCreateVBO(quadVerts, (int)sizeof(quadVerts), false, false);
    }
    if (quadVbo == RLVK_INVALID_SLOT)
        return;

    rlvkBeginFrame();
    if (!RLVK.frameActive)
        return;
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];

    s_bindingValid = false; // quad blit rebinds vertex buffers outside the mesh-draw dedup cache
    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];

    if (shader->usesUbo)
    {
        VkDeviceSize need = 0;
        if (shader->vsBlockSize)
            need += ((VkDeviceSize)shader->vsBlockSize + 255) & ~(VkDeviceSize)255;
        if (shader->fsBlockSize)
            need += ((VkDeviceSize)shader->fsBlockSize + 255) & ~(VkDeviceSize)255;
        if (need)
        {
            need += 256;
            rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
            if (RLVK.arenaOffset[frameIndex] + need > arena->sizeBytes)
            {
                RLVK.arenaWanted[frameIndex] += need;
                if (need <= arena->sizeBytes)
                {
                    rlvkFlushFrame();
                    cmdBuffer = RLVK.cmdBuffers[frameIndex];
                }
            }
        }
    }

    // Interleaved pos+uv layout and shader stages are baked into the cached pipeline
    // Pipeline MUST be bound BEFORE vertex buffers (MoltenVK Metal buffer index resolution)
    // Bail on a failed build rather than blit with a stale pipeline (better a black frame).
    if (!rlvkBindPipeline(cmdBuffer, 2, RLVK_VLAYOUT_QUAD, RLVK.State.activeShaderSlot))
        return;
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1,
                           (VkBuffer[]){RLVK.bufferSlots[quadVbo].buffer}, (VkDeviceSize[]){0});
    rlvkBindDummyAttribBuffers(cmdBuffer, RLVK_VLAYOUT_QUAD, shader);

    if (shader->usesUbo)
    {
        rlvkBindShaderUbos(cmdBuffer, shader);
        rlvkBindShaderSsbos(cmdBuffer, shader);
        rlvkBindShaderSamplers(cmdBuffer, shader, true);
    }
    rlvkFlushSet0(cmdBuffer);
    rlvkProfileDraw();
    vk.CmdDraw(cmdBuffer, 4, 1, 0, 0);
}