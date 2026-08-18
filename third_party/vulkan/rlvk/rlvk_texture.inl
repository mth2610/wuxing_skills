//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Host<->image staging transfer, textures, framebuffers (FBO)
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

//----------------------------------------------------------------------------------
// Host <-> image transfer (Vulkan 1.1 baseline)
// Replaces VK_EXT_host_image_copy with the classic staging buffer + one-shot submission.
// A TRANSIENT command pool per call keeps this safe even while a frame is being recorded
// (never touches the frame ring's pools); synchronous by design - these are load-time and
// GL-style read-back paths, not per-frame hot paths.
//----------------------------------------------------------------------------------

static VkCommandBuffer rlvkOneShotBegin(VkCommandPool *outPool)
{
    /* Anything else submitting one-shot work (uploads, readbacks, layout
     * transitions) must land AFTER any compute already recorded, exactly as it
     * did when every dispatch submitted itself. Flushing here keeps that
     * ordering automatic for every present and future caller. */
    rlvkComputeBatchFlush();
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(RLVK.device,
                            &(VkCommandPoolCreateInfo){
                                VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                .queueFamilyIndex = RLVK.graphicsFamily,
                            },
                            RLVK_ALLOC, &pool) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(RLVK.device,
                             &(VkCommandBufferAllocateInfo){
                                 VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                 .commandPool = pool,
                                 .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                 .commandBufferCount = 1,
                             },
                             &cmdBuffer);
    if (cmdBuffer == VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(RLVK.device, pool, RLVK_ALLOC);
        return VK_NULL_HANDLE;
    }
    vk.BeginCommandBuffer(cmdBuffer, &(VkCommandBufferBeginInfo){
                                         VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});
    *outPool = pool;
    return cmdBuffer;
}

static void rlvkOneShotEnd(VkCommandPool pool, VkCommandBuffer cmdBuffer)
{
    vk.EndCommandBuffer(cmdBuffer);

    // Tối ưu: Bỏ tạo Fence thừa thãi, dùng QueueWaitIdle để chặn đồng bộ nhanh hơn
    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){
                                               VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                               .commandBufferInfoCount = 1,
                                               .pCommandBufferInfos = &(VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmdBuffer},
                                           },
                    VK_NULL_HANDLE); // Không cần Fence

    vkQueueWaitIdle(RLVK.graphicsQueue);
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
                                              .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                              .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                                              .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                              .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                              .oldLayout = oldLayout,
                                              .newLayout = newLayout,
                                              .image = image,
                                              .subresourceRange = {aspect, baseMip, mipCount, baseLayer, layerCount},
                                          },
                                      });
}

// One-shot layout transition (image with no data to upload still needs to leave UNDEFINED)
static void rlvkHostTransitionImage(VkImage image, VkImageAspectFlags aspect,
                                    u32 mipCount, u32 baseLayer, u32 layerCount, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = rlvkOneShotBegin(&pool);
    if (cmdBuffer == VK_NULL_HANDLE)
        return;
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
        if (oldLayout != finalLayout)
            rlvkHostTransitionImage(image, aspect, 1, baseLayer, layerCount, oldLayout, finalLayout);
        return;
    }

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    void *map = NULL;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
                              &(VkBufferCreateInfo){VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                    .size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              RLVK_ALLOC, &staging));
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(RLVK.device, staging, &memReq);
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
                                   .imageSubresource = {aspect, 0, baseLayer, layerCount},
                                   .imageOffset = {x, y, 0},
                                   .imageExtent = {width, height, 1},
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
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    void *map = NULL;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
                              &(VkBufferCreateInfo){VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                    .size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              RLVK_ALLOC, &staging));
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(RLVK.device, staging, &memReq);
    stagingMem = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(RLVK.device, staging, stagingMem, 0);

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = rlvkOneShotBegin(&pool);
    if (cmdBuffer != VK_NULL_HANDLE)
    {
        rlvkCmdTransitionImage(cmdBuffer, image, aspect, 0, 1, 0, 1, currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vk.CmdCopyImageToBuffer(cmdBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1,
                                &(VkBufferImageCopy){
                                    .imageSubresource = {aspect, 0, 0, 1},
                                    .imageExtent = {width, height, 1},
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
    if (dynamic)
        for (u32 i = 1; i < RLVK_MAX_BUFFER_SLOTS; i++)
        {
            rlvkBufferSlot *p = &RLVK.bufferSlots[i];
            if (p->inUse || (p->buffer == VK_NULL_HANDLE) || (p->mapped == NULL))
                continue;
            if ((p->isIndex != isIndex) || (p->sizeBytes < (u32)size) || (p->sizeBytes > 4u * (u32)size))
                continue;
            if (p->freedFrame + RLVK_FRAME_INDEX_COUNT > RLVK.frameCounter)
                continue;
            p->inUse = true;
            s_vboReuseCount++;
            if (data)
                memcpy(p->mapped, data, (size_t)size);
            return i;
        }

    u32 slot = rlvkAllocBufferSlot();
    if (slot == RLVK_INVALID_SLOT)
        return RLVK_INVALID_SLOT;
    rlvkBufferSlot *b = &RLVK.bufferSlots[slot];
    // The slot may hold pooled resources that missed the reuse checks: evict them safely
    if (b->buffer != VK_NULL_HANDLE)
        rlvkDeferDestroy(b->buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, b->memory, VK_NULL_HANDLE);
    s_vboCreateCount++;
    b->sizeBytes = (u32)size;
    b->isIndex = isIndex;
    VkBufferUsageFlags usage =
        (isIndex ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                 : (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));

    // Static buffers live DEVICE_LOCAL (VRAM-bandwidth reads, not per-frame bus streaming);
    // dynamic buffers stay host-cached and persistently mapped for cheap CPU writes
    if (!dynamic)
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    RLVK_CHECK(vkCreateBuffer(RLVK.device,
                              &(VkBufferCreateInfo){VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                    .size = (VkDeviceSize)size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              RLVK_ALLOC, &b->buffer));
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(RLVK.device, b->buffer, &memReq);
    b->memory = rlvkAllocMemory(memReq, dynamic ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                                : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindBufferMemory(RLVK.device, b->buffer, b->memory, 0);
    if (dynamic)
    {
        vkMapMemory(RLVK.device, b->memory, 0, (VkDeviceSize)size, 0, &b->mapped);
        if (data)
            memcpy(b->mapped, data, (size_t)size);
    }
    else
    {
        b->mapped = NULL;
        if (data)
            rlvkUploadBuffer(b->buffer, 0, data, (u32)size);
    }
    return slot;
}

// Load vertex array object (VAO)
unsigned int rlLoadVertexArray(void)
{
    if (!isGpuReady)
        return 0;
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
    if (slot == RLVK_INVALID_SLOT)
        return 0;
    RLVK.State.currentVBO = slot;
    return slot;
}
// Load a new attributes element buffer
unsigned int rlLoadVertexBufferElement(const void *data, int size, bool dynamic)
{
    (void)dynamic;
    u32 slot = rlvkCreateVBO(data, size, true, dynamic);
    if (slot == RLVK_INVALID_SLOT)
        return 0;
    if (RLVK.State.currentVAO && RLVK.State.currentVAO < RLVK_MAX_VAO_SLOTS)
        RLVK.vertexArrays[RLVK.State.currentVAO].indexSlot = slot;
    return slot;
}
// Update vertex buffer with new data
// NOTE: dataSize and offset must be provided in bytes
void rlUpdateVertexBuffer(unsigned int id, const void *d, int s, int o)
{
    if (!id || id >= RLVK_MAX_BUFFER_SLOTS || !d)
        return;
    rlvkBufferSlot *b = &RLVK.bufferSlots[id];
    if (b->mapped)
        memcpy((char *)b->mapped + o, d, (size_t)s); // dynamic: direct host write
    else if (b->buffer)
        rlvkUploadBuffer(b->buffer, (u32)o, d, (u32)s); // static: staged copy (glBufferSubData semantics)
}
// Update vertex buffer elements with new data
// NOTE: dataSize and offset must be provided in bytes
void rlUpdateVertexBufferElements(unsigned int id, const void *d, int s, int o) { rlUpdateVertexBuffer(id, d, s, o); }
void rlUnloadVertexArray(unsigned int id)
{
    if (id && id < RLVK_MAX_VAO_SLOTS)
        RLVK.vertexArrays[id].inUse = false;
}
// Unload vertex buffer (VBO)
void rlUnloadVertexBuffer(unsigned int id)
{
    if (id && id < RLVK_MAX_BUFFER_SLOTS)
    {
        RLVK.bufferSlots[id].inUse = false;
        RLVK.bufferSlots[id].freedFrame = (u32)RLVK.frameCounter;
    }
}
// Set vertex attribute
void rlSetVertexAttribute(unsigned int idx, int compCount, int type, bool norm, int stride, int offset)
{
    (void)compCount;
    (void)type;
    (void)norm;
    (void)stride;
    u32 vao = RLVK.State.currentVAO;
    if (!vao || vao >= RLVK_MAX_VAO_SLOTS)
        return;
    rlvkVertexArray *a = &RLVK.vertexArrays[vao];
    u32 vbo = (RLVK.State.currentVBO < RLVK_MAX_BUFFER_SLOTS) ? RLVK.State.currentVBO : 0;

    // idx is a canonical raylib attribute LOCATION: shaders are compiled with canonicalized
    // locations (rlvkCanonicalizeInputLocations), so UploadMesh's fixed constants and DrawMesh's
    // reflected shader locs are the same numbers - exact rlgl/glBindAttribLocation semantics.
    switch (idx)
    {
    case 0:
        a->posSlot = vbo;
        a->posOffset = (u32)offset;
        break;
    case 1:
        a->uvSlot = vbo;
        a->uvOffset = (u32)offset;
        break;
    case 2:
        a->normalSlot = vbo;
        a->normalOffset = (u32)offset;
        break;
    case 3:
        a->colorSlot = vbo;
        a->colorOffset = (u32)offset;
        break;
    case 4:
        a->tangentSlot = vbo;
        a->tangentOffset = (u32)offset;
        break;
    case 5:
        a->uv2Slot = vbo;
        a->uv2Offset = (u32)offset;
        break;
    case 7:
        a->boneIdSlot = vbo;
        a->boneIdOffset = (u32)offset;
        if (rlvkDebugFlag("RLVK_DEBUG_VAO", &s_dbgVao))
            TRACELOG(RL_LOG_WARNING, "VKDBG vao=%u boneIds vbo=%u", vao, vbo);
        break;
    case 8:
        a->boneWtSlot = vbo;
        a->boneWtOffset = (u32)offset;
        if (rlvkDebugFlag("RLVK_DEBUG_VAO", &s_dbgVao))
            TRACELOG(RL_LOG_WARNING, "VKDBG vao=%u boneWts vbo=%u", vao, vbo);
        break;
    case 9:
        if (offset == 0)
        {
            a->instSlot = vbo;
            a->instOffset = 0;
        }
        break; // mat4 columns 9..12
    default:
        break;
    }
}
// Set vertex attribute divisor
void rlSetVertexAttributeDivisor(unsigned int idx, int d)
{
    (void)idx;
    (void)d;
}
// Set shader value attribute
void rlSetVertexAttributeDefault(int loc, const void *v, int t, int c)
{
    (void)loc;
    (void)v;
    (void)t;
    (void)c;
}

// Record a mesh draw (DrawMesh path): VAO buffers bind at raylib's canonical locations,
// mvp/colDiffuse come from rlSetUniform*, missing attributes ride the divisor-0 broadcast
static void rlvkDrawMesh(int offset, int count, bool indexed, int instances)
{
    if (!isGpuReady)
        return;
    u32 vao = RLVK.State.currentVAO;
    if (!vao || vao >= RLVK_MAX_VAO_SLOTS || !RLVK.vertexArrays[vao].inUse)
        return;
    rlvkVertexArray *a = &RLVK.vertexArrays[vao];
    if (a->posSlot == 0)
        return;

    rlvkBeginFrame();
    if (!RLVK.frameActive)
        return;
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];

    bool hasUV = (a->uvSlot != 0);
    bool hasNormal = (a->normalSlot != 0);
    bool hasColor = (a->colorSlot != 0);
    VkBuffer dummy = RLVK.bufferSlots[RLVK.dummyAttribSlot].buffer;

    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];

    // Reserve this draw's UBO snapshot BEFORE anything is recorded. rlvkBindShaderUbos (below,
    // after the pipeline bind) cannot drain the arena — it silently skips the push and the draw
    // then runs with the PREVIOUS push still bound (stale mvp/uniforms). Draining here is still
    // legal: nothing of this draw has been recorded yet, and the drain refreshes the cmd buffer.
    if (shader->usesUbo && RLVK.frameActive)
    {
        VkDeviceSize need = 0;
        if (shader->vsBlockSize)
            need += ((VkDeviceSize)shader->vsBlockSize + 255) & ~(VkDeviceSize)255;
        if (shader->fsBlockSize)
            need += ((VkDeviceSize)shader->fsBlockSize + 255) & ~(VkDeviceSize)255;
        if (need)
        {
            need += 256; // alignment slack for the first block's 256-byte rounding
            rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
            if (RLVK.arenaOffset[frameIndex] + need > arena->sizeBytes)
            {
                RLVK.arenaWanted[frameIndex] += need;
                if (need <= arena->sizeBytes) // a block bigger than the whole arena: growth request stands, skip the drain
                {
                    rlvkFlushFrame();
                    cmdBuffer = RLVK.cmdBuffers[frameIndex];
                    s_bindingValid = false; // the drain reset the command buffer: rebind everything
                }
            }
        }
    }

    // Optional attribute tiers at bindings 4+, mutually exclusive with the same priority the
    // dynamic path's overwriting vkCmdSetVertexInputEXT calls resolved to: instancing wins,
    // then bones, then uv2/tangent
    bool wantUV2 = (a->uv2Slot != 0) && (shader->attribLocs[RLVK_ATTRIB_TEXCOORD2] >= 0);
    bool wantTan = (a->tangentSlot != 0) && (shader->attribLocs[RLVK_ATTRIB_TANGENT] >= 0);
    bool wantBones = (shader->attribLocs[RLVK_ATTRIB_BONEIDS] >= 0) && (shader->attribLocs[RLVK_ATTRIB_BONEWEIGHTS] >= 0);
    bool wantInst = (instances > 1) && a->instSlot && (shader->attribLocs[RLVK_ATTRIB_INSTANCE_TX] >= 0);
    bool realBones = (a->boneIdSlot != 0) && (a->boneWtSlot != 0);

    unsigned short vertexLayout = RLVK_VLAYOUT_MESH;
    if (hasUV)
        vertexLayout |= RLVK_VLAYOUT_MESH_UV;
    if (hasNormal)
        vertexLayout |= RLVK_VLAYOUT_MESH_NORMAL;
    if (hasColor)
        vertexLayout |= RLVK_VLAYOUT_MESH_COLOR;
    if (wantInst)
        vertexLayout |= RLVK_VLAYOUT_MESH_INSTANCED;
    else if (wantBones)
        vertexLayout |= realBones ? RLVK_VLAYOUT_MESH_BONES : RLVK_VLAYOUT_MESH_BONES_DUMMY;
    else
    {
        if (wantUV2)
            vertexLayout |= RLVK_VLAYOUT_MESH_UV2;
        if (wantTan)
            vertexLayout |= RLVK_VLAYOUT_MESH_TANGENT;
    }

    // Binding-state dedup: when this draw binds exactly what the previous mesh draw did, only
    // push-constants/uniforms and the draw itself run (thousands of DrawModel of one mesh);
    // invalidated on command-buffer restart and by the batch-flush / quad-blit paths
    int rlvkTexSlot = RLVK.State.activeTextureSlots[0] ? RLVK.State.activeTextureSlots[0] : RLVK.State.currentTextureSlot;
    rlvkBindingSig bsig;
    memset(&bsig, 0, sizeof(bsig));
    bsig.shaderSlot = (int)RLVK.State.activeShaderSlot;
    bsig.texSlot = rlvkTexSlot;
    bsig.posSlot = (int)a->posSlot;
    bsig.uvSlot = (int)a->uvSlot;
    bsig.normalSlot = (int)a->normalSlot;
    bsig.colorSlot = (int)a->colorSlot;
    bsig.uv2Slot = (int)a->uv2Slot;
    bsig.tangentSlot = (int)a->tangentSlot;
    bsig.boneIdSlot = (int)a->boneIdSlot;
    bsig.boneWtSlot = (int)a->boneWtSlot;
    bsig.posOff = (ull)a->posOffset;
    bsig.uvOff = (ull)a->uvOffset;
    bsig.normalOff = (ull)a->normalOffset;
    bsig.colorOff = (ull)a->colorOffset;
    bsig.uv2Off = (ull)a->uv2Offset;
    bsig.tangentOff = (ull)a->tangentOffset;
    bsig.boneIdOff = (ull)a->boneIdOffset;
    bsig.boneWtOff = (ull)a->boneWtOffset;
    bool sameBinding = (s_bindingValid && (memcmp(&bsig, &s_bindingSig, sizeof(bsig)) == 0));
    if (!sameBinding)
    {
        s_bindingSig = bsig;
        s_bindingValid = true;
    }

    // Pipeline MUST be bound BEFORE vertex buffers: MoltenVK resolves Metal buffer
    // indices via the active pipeline's reflection data. Without a pipeline, vertex buffer
    // bindings are silently lost and attribute reads return all zeros.
    // A failed pipeline build returns false and leaves the PREVIOUS pipeline bound - drawing
    // anyway rasterizes this mesh with the wrong shader (a soft-alpha particle/VFX quad shows
    // as an opaque square). Skip the draw so a failed shader is invisible, not garbage.
    if (!rlvkBindPipeline(cmdBuffer, 1, vertexLayout, RLVK.State.activeShaderSlot))
        return;

    // Vertex layout and shader stages are baked into the cached pipeline; only the buffer
    // bindings are recorded here. Missing mesh attributes fall back to the divisor-0
    // broadcast constants in the dummy buffer.
    if (!sameBinding)
        vkCmdBindVertexBuffers(cmdBuffer, 0, 4,
                               (VkBuffer[]){
                                   RLVK.bufferSlots[a->posSlot].buffer,
                                   hasUV ? RLVK.bufferSlots[a->uvSlot].buffer : dummy,
                                   hasNormal ? RLVK.bufferSlots[a->normalSlot].buffer : dummy,
                                   hasColor ? RLVK.bufferSlots[a->colorSlot].buffer : dummy,
                               },
                               (VkDeviceSize[]){
                                   a->posOffset,
                                   hasUV ? a->uvOffset : 0,          // dummy offset  0 = vec2(0,0)
                                   hasNormal ? a->normalOffset : 12, // dummy offset 12 = +Z normal
                                   hasColor ? a->colorOffset : 8,    // dummy offset  8 = opaque white
                               });

    if (shader->usesUbo)
    {
        // Uniforms (mvp, colDiffuse, user values) were already written by DrawMesh via
        // rlSetUniform* into the staging; snapshot, then resolve ALL sampler bindings (incl. 0)
        rlvkBindShaderUbos(cmdBuffer, shader);
        rlvkBindShaderSsbos(cmdBuffer, shader);
        if (!sameBinding)
            rlvkBindShaderSamplers(cmdBuffer, shader, true); // samplers unchanged across identical binds
    }
    else
    {
        rlvkPushConstants pc = {0};
        Matrix m = RLVK.State.meshMVP;
        pc.mvp[0] = m.m0;
        pc.mvp[1] = m.m1;
        pc.mvp[2] = m.m2;
        pc.mvp[3] = m.m3;
        pc.mvp[4] = m.m4;
        pc.mvp[5] = m.m5;
        pc.mvp[6] = m.m6;
        pc.mvp[7] = m.m7;
        pc.mvp[8] = m.m8;
        pc.mvp[9] = m.m9;
        pc.mvp[10] = m.m10;
        pc.mvp[11] = m.m11;
        pc.mvp[12] = m.m12;
        pc.mvp[13] = m.m13;
        pc.mvp[14] = m.m14;
        pc.mvp[15] = m.m15;
        memcpy(pc.colDiffuse, RLVK.State.meshColDiffuse, sizeof(f32) * 4);
        vk.CmdPushConstants(cmdBuffer, RLVK.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        if (!sameBinding)
            rlvkPushTexture(cmdBuffer, 0, rlvkTexSlot);
    }

    // Optional texcoord2 / tangent streams at sequential bindings from 4 (layout baked)
    if (!sameBinding && !wantInst && !wantBones && (wantUV2 || wantTan))
    {
        VkBuffer extraBufs[2];
        VkDeviceSize extraOffs[2];
        u32 extraCount = 0;
        if (wantUV2)
        {
            extraBufs[extraCount] = RLVK.bufferSlots[a->uv2Slot].buffer;
            extraOffs[extraCount] = a->uv2Offset;
            extraCount++;
        }
        if (wantTan)
        {
            extraBufs[extraCount] = RLVK.bufferSlots[a->tangentSlot].buffer;
            extraOffs[extraCount] = a->tangentOffset;
            extraCount++;
        }
        vkCmdBindVertexBuffers(cmdBuffer, 4, extraCount, extraBufs, extraOffs);
    }

    // GPU skinning: bone id/weight streams whenever the shader consumes them; meshes without bone
    // buffers get the divisor-0 broadcast defaults (ids 0, weights (1,0,0,0)) like GL's attribute
    // defaults from rlSetVertexAttributeDefault
    if (!sameBinding && !wantInst && wantBones)
    {
        if (rlvkDebugFlag("RLVK_DEBUG_VAO", &s_dbgVao))
            TRACELOG(RL_LOG_WARNING, "VKDBG draw vao=%u realBones=%d idSlot=%u wtSlot=%u", RLVK.State.currentVAO, (int)realBones, a->boneIdSlot, a->boneWtSlot);
        vkCmdBindVertexBuffers(cmdBuffer, 4, 2,
                               (VkBuffer[]){realBones ? RLVK.bufferSlots[a->boneIdSlot].buffer : dummy,
                                            realBones ? RLVK.bufferSlots[a->boneWtSlot].buffer : dummy},
                               (VkDeviceSize[]){realBones ? (VkDeviceSize)a->boneIdOffset : 24,
                                                realBones ? (VkDeviceSize)a->boneWtOffset : 28});
    }

    // mat4 instanceTransform stream at binding 4 (offset can vary per draw: not dedup-gated)
    if (wantInst)
        vkCmdBindVertexBuffers(cmdBuffer, 4, 1,
                               (VkBuffer[]){RLVK.bufferSlots[a->instSlot].buffer}, (VkDeviceSize[]){a->instOffset});

    // Dummy broadcasts for any remaining shader-declared attributes this layout leaves unfed
    if (!sameBinding)
        rlvkBindDummyAttribBuffers(cmdBuffer, vertexLayout, shader);

    rlvkFlushSet0(cmdBuffer);
    if (indexed && a->indexSlot && a->indexSlot < RLVK_MAX_BUFFER_SLOTS)
    {
        vkCmdBindIndexBuffer(cmdBuffer, RLVK.bufferSlots[a->indexSlot].buffer, 0, VK_INDEX_TYPE_UINT16);
        vk.CmdDrawIndexed(cmdBuffer, count, (instances > 0) ? instances : 1, offset, 0, 0);
    }
    else
        vk.CmdDraw(cmdBuffer, count, (instances > 0) ? instances : 1, offset, 0);
}
// Draw vertex array
void rlDrawVertexArray(int offset, int count) { rlvkDrawMesh(offset, count, false, 1); }
// Draw vertex array elements
void rlDrawVertexArrayElements(int offset, int count, const void *b)
{
    (void)b;
    rlvkDrawMesh(offset, count, true, 1);
}
// Draw vertex array instanced
void rlDrawVertexArrayInstanced(int offset, int count, int instances) { rlvkDrawMesh(offset, count, false, instances); }
// Draw vertex array elements instanced
void rlDrawVertexArrayElementsInstanced(int offset, int count, const void *b, int instances)
{
    (void)b;
    rlvkDrawMesh(offset, count, true, instances);
}

// Textures data management
//-----------------------------------------------------------------------------------------

// Convert image data to OpenGL texture (returns OpenGL valid Id)
unsigned int rlLoadTexture(const void *data, int width, int height, int format, int mipmapCount)
{
    if (!isGpuReady)
        return RLVK_INVALID_SLOT;

    u32 slot = rlvkAllocTextureSlot();
    if (slot == RLVK_INVALID_SLOT)
        return RLVK_INVALID_SLOT;

    rlvkTextureSlot *t = &RLVK.textureSlots[slot];
    t->width = width;
    t->height = height;
    t->mipCount = (mipmapCount > 0) ? mipmapCount : 1;
    t->rlFormat = format;
    VkFormat vkfmt = rlvkGetVkTextureFormat(format);
    t->format = vkfmt;

    VkComponentMapping swizzle = {0};

    const void *uploadData = data;
    void *converted = NULL;
    size_t pixels = (size_t)width * height;

    if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE)
        vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA)
        vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)
        vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16)
        vkfmt = VK_FORMAT_R16G16B16A16_SFLOAT;
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32)
        vkfmt = VK_FORMAT_R32G32B32A32_SFLOAT;

    if (data != NULL)
    {
        if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE)
        {
            vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
            const unsigned char *s = (const unsigned char *)data;
            uint32_t *d = (uint32_t *)RL_MALLOC(pixels * 4);
            // Tối ưu: Ghi 32-bit (4 channels) trong một phép gán
            for (size_t i = 0; i < pixels; i++)
            {
                d[i] = (uint32_t)s[i] | ((uint32_t)s[i] << 8) | ((uint32_t)s[i] << 16) | 0xFF000000;
            }
            converted = d;
            uploadData = d;
        }
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA)
        {
            vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
            const unsigned char *s = (const unsigned char *)data;
            uint32_t *d = (uint32_t *)RL_MALLOC(pixels * 4);
            for (size_t i = 0; i < pixels; i++)
            {
                d[i] = (uint32_t)s[i * 2] | ((uint32_t)s[i * 2] << 8) | ((uint32_t)s[i * 2] << 16) | ((uint32_t)s[i * 2 + 1] << 24);
            }
            converted = d;
            uploadData = d;
        }
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)
        {
            vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
            const unsigned char *s = (const unsigned char *)data;
            uint32_t *d = (uint32_t *)RL_MALLOC(pixels * 4);
            // Tối ưu điểm nghẽn chuyển đổi phổ biến nhất (RGB -> RGBA)
            for (size_t i = 0; i < pixels; i++)
            {
                d[i] = (uint32_t)s[i * 3] | ((uint32_t)s[i * 3 + 1] << 8) | ((uint32_t)s[i * 3 + 2] << 16) | 0xFF000000;
            }
            converted = d;
            uploadData = d;
        }
        // ... (Phần R16 và R32 giữ nguyên thuật toán cũ vì kích thước vùng nhớ lớn hơn)
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16)
        {
            vkfmt = VK_FORMAT_R16G16B16A16_SFLOAT;
            const unsigned short *s = (const unsigned short *)data;
            unsigned short *d = (unsigned short *)RL_MALLOC(pixels * 4 * sizeof(unsigned short));
            for (size_t i = 0; i < pixels; i++)
            {
                d[i * 4 + 0] = s[i * 3 + 0];
                d[i * 4 + 1] = s[i * 3 + 1];
                d[i * 4 + 2] = s[i * 3 + 2];
                d[i * 4 + 3] = 0x3C00;
            } // half 1.0
            converted = d;
            uploadData = d;
        }
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32)
        {
            vkfmt = VK_FORMAT_R32G32B32A32_SFLOAT;
            const f32 *s = (const f32 *)data;
            f32 *d = (f32 *)RL_MALLOC(pixels * 4 * sizeof(f32));
            for (size_t i = 0; i < pixels; i++)
            {
                d[i * 4 + 0] = s[i * 3 + 0];
                d[i * 4 + 1] = s[i * 3 + 1];
                d[i * 4 + 2] = s[i * 3 + 2];
                d[i * 4 + 3] = 1.0f;
            }
            converted = d;
            uploadData = d;
        }
    }
    t->format = vkfmt;

    RLVK_CHECK(vkCreateImage(RLVK.device,
                             &(VkImageCreateInfo){
                                 VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                 .imageType = VK_IMAGE_TYPE_2D,
                                 .format = vkfmt,
                                 .extent = {(u32)width, (u32)height, 1},
                                 .mipLevels = 1,
                                 .arrayLayers = 1,
                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                 .tiling = VK_IMAGE_TILING_OPTIMAL,
                                 .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | ((format < RL_PIXELFORMAT_COMPRESSED_DXT1_RGB) ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0),
                                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                             },
                             RLVK_ALLOC, &t->image));

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(RLVK.device, t->image, &memReq);
    t->memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RLVK_CHECK(vkBindImageMemory(RLVK.device, t->image, t->memory, 0));

    VkDeviceSize uploadBytes = 0;
    if (uploadData != NULL)
    {
        if (converted && (vkfmt == VK_FORMAT_R8G8B8A8_UNORM))
            uploadBytes = (VkDeviceSize)pixels * 4;
        else if (converted && (vkfmt == VK_FORMAT_R16G16B16A16_SFLOAT))
            uploadBytes = (VkDeviceSize)pixels * 8;
        else if (converted && (vkfmt == VK_FORMAT_R32G32B32A32_SFLOAT))
            uploadBytes = (VkDeviceSize)pixels * 16;
        else
            uploadBytes = (VkDeviceSize)rlvkGetPixelDataSize(width, height, format);
    }

    rlvkStagingUploadImage(t->image, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, (u32)width, (u32)height, 0, 1,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, uploadData, uploadBytes);
    if (converted)
        RL_FREE(converted);
    t->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RLVK_CHECK(vkCreateImageView(RLVK.device,
                                 &(VkImageViewCreateInfo){
                                     VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                     .image = t->image,
                                     .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                     .format = vkfmt,
                                     .components = swizzle,
                                     .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                 },
                                 RLVK_ALLOC, &t->view));

    t->minFilter = VK_FILTER_NEAREST;
    t->magFilter = VK_FILTER_NEAREST;
    t->wrapS = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    t->wrapT = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    RLVK_CHECK(vkCreateSampler(RLVK.device,
                               &(VkSamplerCreateInfo){
                                   VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .magFilter = t->magFilter,
                                   .minFilter = t->minFilter,
                                   .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                   .addressModeU = t->wrapS,
                                   .addressModeV = t->wrapT,
                                   .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                   .maxLod = 1.0f,
                               },
                               RLVK_ALLOC, &t->sampler));

    return slot;
}

// Load depth texture/renderbuffer (to be attached to fbo)
// WARNING: OpenGL ES 2.0 requires GL_OES_depth_texture and WebGL requires WEBGL_depth_texture extensions
unsigned int rlLoadTextureDepth(int width, int height, bool useRenderBuffer)
{
    (void)useRenderBuffer; // no renderbuffer concept: always a depth image
    if (!isGpuReady)
        return RLVK_INVALID_SLOT;
    u32 slot = rlvkAllocTextureSlot();
    if (slot == RLVK_INVALID_SLOT)
        return RLVK_INVALID_SLOT;
    rlvkTextureSlot *t = &RLVK.textureSlots[slot];
    t->width = width;
    t->height = height;
    t->mipCount = 1;
    t->format = VK_FORMAT_D32_SFLOAT;

    RLVK_CHECK(vkCreateImage(RLVK.device,
                             &(VkImageCreateInfo){
                                 VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                 .imageType = VK_IMAGE_TYPE_2D,
                                 .format = t->format,
                                 .extent = {(u32)width, (u32)height, 1},
                                 .mipLevels = 1,
                                 .arrayLayers = 1,
                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                 .tiling = VK_IMAGE_TILING_OPTIMAL,
                                 // Caps.noSampledDepth (MoltenVK/Intel): SAMPLED usage on a depth image
                                 // silently disables depth test/write on it - see quirk note in rlvk_frame.inl
                                 .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                                        | (RLVK.Caps.noSampledDepth ? 0 : VK_IMAGE_USAGE_SAMPLED_BIT),
                                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                             },
                             RLVK_ALLOC, &t->image));
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(RLVK.device, t->image, &memReq);
    t->memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RLVK_CHECK(vkBindImageMemory(RLVK.device, t->image, t->memory, 0));
    RLVK_CHECK(vkCreateImageView(RLVK.device,
                                 &(VkImageViewCreateInfo){
                                     VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                     .image = t->image,
                                     .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                     .format = t->format,
                                     .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
                                 },
                                 RLVK_ALLOC, &t->view));
    t->minFilter = VK_FILTER_NEAREST;
    t->magFilter = VK_FILTER_NEAREST;
    t->wrapS = t->wrapT = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    RLVK_CHECK(vkCreateSampler(RLVK.device,
                               &(VkSamplerCreateInfo){
                                   VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .magFilter = t->magFilter,
                                   .minFilter = t->minFilter,
                                   .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                   .addressModeU = t->wrapS,
                                   .addressModeV = t->wrapT,
                                   .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   .maxLod = 1.0f,
                               },
                               RLVK_ALLOC, &t->sampler));
    t->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Caps.noSampledDepth: the attachment depth above has no SAMPLED usage, so soft-particle /
    // depth_copy shaders can't read it. Expose a sampleable R32_SFLOAT COLOR twin (Metal can't
    // sample a depth-format texture via sampler2D); rlDisableFramebuffer bounces the raw depth
    // depth-image -> sampleScratch -> twin at scope close and rlvkPushTexture routes here (§7.1).
    if (RLVK.Caps.noSampledDepth)
    {
        RLVK_CHECK(vkCreateImage(RLVK.device,
                                 &(VkImageCreateInfo){
                                     VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                     .imageType = VK_IMAGE_TYPE_2D,
                                     .format = VK_FORMAT_R32_SFLOAT,
                                     .extent = {(u32)width, (u32)height, 1},
                                     .mipLevels = 1,
                                     .arrayLayers = 1,
                                     .samples = VK_SAMPLE_COUNT_1_BIT,
                                     .tiling = VK_IMAGE_TILING_OPTIMAL,
                                     .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                     .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                 },
                                 RLVK_ALLOC, &t->sampleImage));
        VkMemoryRequirements smReq;
        vkGetImageMemoryRequirements(RLVK.device, t->sampleImage, &smReq);
        t->sampleMemory = rlvkAllocMemory(smReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        RLVK_CHECK(vkBindImageMemory(RLVK.device, t->sampleImage, t->sampleMemory, 0));
        RLVK_CHECK(vkCreateImageView(RLVK.device,
                                     &(VkImageViewCreateInfo){
                                         VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                         .image = t->sampleImage,
                                         .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                         .format = VK_FORMAT_R32_SFLOAT,
                                         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                     },
                                     RLVK_ALLOC, &t->sampleView));
        t->sampleLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // D32_SFLOAT and R32_SFLOAT are both 4 bytes/texel: the depth bounces through this buffer
        RLVK_CHECK(vkCreateBuffer(RLVK.device,
                                  &(VkBufferCreateInfo){
                                      VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                      .size = (VkDeviceSize)width * height * 4,
                                      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                  },
                                  RLVK_ALLOC, &t->sampleScratch));
        VkMemoryRequirements sbReq;
        vkGetBufferMemoryRequirements(RLVK.device, t->sampleScratch, &sbReq);
        t->sampleScratchMemory = rlvkAllocMemory(sbReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        RLVK_CHECK(vkBindBufferMemory(RLVK.device, t->sampleScratch, t->sampleScratchMemory, 0));
    }
    return slot;
}

// Load texture cubemap
// NOTE: Cubemap data is expected to be 6 images in a single data array (one after the other),
// expected the following convention: +X, -X, +Y, -Y, +Z, -Z
unsigned int rlLoadTextureCubemap(const void *data, int size, int format, int mipmapCount)
{
    (void)mipmapCount;
    if (!isGpuReady)
        return RLVK_INVALID_SLOT;
    u32 slot = rlvkAllocTextureSlot();
    if (slot == RLVK_INVALID_SLOT)
        return RLVK_INVALID_SLOT;
    rlvkTextureSlot *t = &RLVK.textureSlots[slot];
    t->width = size;
    t->height = size;
    t->mipCount = 1;
    t->rlFormat = format;
    VkFormat vkfmt = rlvkGetVkTextureFormat(format);

    // Same RGB->RGBA expansion as rlLoadTexture (3-channel formats are not sampleable)
    const void *uploadData = data;
    void *converted = NULL;
    size_t facePixels = (size_t)size * size;
    if (data && format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)
    {
        vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
        const unsigned char *src = (const unsigned char *)data;
        unsigned char *dst = (unsigned char *)RL_MALLOC(facePixels * 6 * 4);
        for (size_t i = 0; i < facePixels * 6; i++)
        {
            dst[i * 4 + 0] = src[i * 3 + 0];
            dst[i * 4 + 1] = src[i * 3 + 1];
            dst[i * 4 + 2] = src[i * 3 + 2];
            dst[i * 4 + 3] = 255;
        }
        converted = dst;
        uploadData = dst;
    }
    t->format = vkfmt;
    u32 faceBytes = (u32)(facePixels * ((vkfmt == VK_FORMAT_R8G8B8A8_UNORM) ? 4 : rlvkGetPixelDataSize(1, 1, format) * 1));
    if (vkfmt != VK_FORMAT_R8G8B8A8_UNORM)
        faceBytes = (u32)rlvkGetPixelDataSize(size, size, format);

    RLVK_CHECK(vkCreateImage(RLVK.device,
                             &(VkImageCreateInfo){
                                 VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                 .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                                 .imageType = VK_IMAGE_TYPE_2D,
                                 .format = vkfmt,
                                 .extent = {(u32)size, (u32)size, 1},
                                 .mipLevels = 1,
                                 .arrayLayers = 6,
                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                 .tiling = VK_IMAGE_TILING_OPTIMAL,
                                 .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                             },
                             RLVK_ALLOC, &t->image));
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(RLVK.device, t->image, &memReq);
    t->memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RLVK_CHECK(vkBindImageMemory(RLVK.device, t->image, t->memory, 0));

    // Staging upload: rlgl layout stores the 6 faces consecutively (+X, -X, +Y, -Y, +Z, -Z),
    // which matches the tightly packed multi-layer copy exactly
    rlvkStagingUploadImage(t->image, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, (u32)size, (u32)size, 0, 6,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           uploadData, uploadData ? (VkDeviceSize)faceBytes * 6 : 0);
    if (converted)
        RL_FREE(converted);
    t->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RLVK_CHECK(vkCreateImageView(RLVK.device,
                                 &(VkImageViewCreateInfo){
                                     VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                     .image = t->image,
                                     .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
                                     .format = vkfmt,
                                     .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6},
                                 },
                                 RLVK_ALLOC, &t->view));

    t->minFilter = VK_FILTER_LINEAR; // rlgl sets GL_LINEAR for cubemaps
    t->magFilter = VK_FILTER_LINEAR;
    t->wrapS = t->wrapT = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    RLVK_CHECK(vkCreateSampler(RLVK.device,
                               &(VkSamplerCreateInfo){
                                   VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .magFilter = t->magFilter,
                                   .minFilter = t->minFilter,
                                   .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                   .addressModeU = t->wrapS,
                                   .addressModeV = t->wrapT,
                                   .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   .maxLod = 1.0f,
                               },
                               RLVK_ALLOC, &t->sampler));
    TRACELOG(RL_LOG_INFO, "RLVK: [ID %u] cubemap loaded (%dx%d, 6 faces)", slot, size, size);
    return slot;
}
// Update already loaded texture in GPU with new data
// WARNING: Not possible to know safely if internal texture format is the expected one...
void rlUpdateTexture(unsigned int id, int x, int y, int w, int h, int format, const void *data)
{
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS || !data)
        return;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->image)
        return;

    // Match the load-time RGB->RGBA expansion (3-channel formats are not sampleable)
    const void *uploadData = data;
    void *converted = NULL;
    int uploadFormat = format;
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE)
    {
        size_t pixels = (size_t)w * h;
        const unsigned char *src = (const unsigned char *)data;
        unsigned char *dst = (unsigned char *)RL_MALLOC(pixels * 4);
        for (size_t i = 0; i < pixels; i++)
        {
            dst[i * 4 + 0] = src[i];
            dst[i * 4 + 1] = src[i];
            dst[i * 4 + 2] = src[i];
            dst[i * 4 + 3] = 255;
        }
        converted = dst;
        uploadData = dst;
        uploadFormat = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    }
    else if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA)
    {
        size_t pixels = (size_t)w * h;
        const unsigned char *src = (const unsigned char *)data;
        unsigned char *dst = (unsigned char *)RL_MALLOC(pixels * 4);
        for (size_t i = 0; i < pixels; i++)
        {
            dst[i * 4 + 0] = src[i * 2 + 0];
            dst[i * 4 + 1] = src[i * 2 + 0];
            dst[i * 4 + 2] = src[i * 2 + 0];
            dst[i * 4 + 3] = src[i * 2 + 1];
        }
        converted = dst;
        uploadData = dst;
        uploadFormat = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    }
    else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)
    {
        size_t pixels = (size_t)w * h;
        const unsigned char *src = (const unsigned char *)data;
        unsigned char *dst = (unsigned char *)RL_MALLOC(pixels * 4);
        for (size_t i = 0; i < pixels; i++)
        {
            dst[i * 4 + 0] = src[i * 3 + 0];
            dst[i * 4 + 1] = src[i * 3 + 1];
            dst[i * 4 + 2] = src[i * 3 + 2];
            dst[i * 4 + 3] = 255;
        }
        converted = dst;
        uploadData = dst;
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
        if (openFb)
            rlDisableFramebuffer();

        vkCmdEndRenderPass(cmdBuffer);
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                              VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                              .imageMemoryBarrierCount = 1,
                                              .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  // Access mask matches the OLD layout (SHADER_READ_ONLY): prior use is
                                                  // sampling; any attachment write was already made visible by the FBO
                                                  // scope's own transition out of COLOR_ATTACHMENT
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                  .oldLayout = t->currentLayout,
                                                  .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                  .image = t->image,
                                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                              },
                                          });
        vkCmdCopyBufferToImage(cmdBuffer, arena->buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &(VkBufferImageCopy){
                                   .bufferOffset = stagingOff,
                                   .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                   .imageOffset = {x, y, 0},
                                   .imageExtent = {(u32)w, (u32)h, 1},
                               });
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                              VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                              .imageMemoryBarrierCount = 1,
                                              .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                  .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                  .image = t->image,
                                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                              },
                                          });
        t->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        rlvkResumeSwapchainScope(cmdBuffer);

        if (openFb)
            rlEnableFramebuffer(openFb);
        if (converted)
            RL_FREE(converted);
        return;
    }

    // No frame being recorded (init-time upload): staging copy after a full wait, keeping
    // the texture in SHADER_READ_ONLY around the update
    rlvkWaitInFlightFrames();
    rlvkStagingUploadImage(t->image, VK_IMAGE_ASPECT_COLOR_BIT, x, y, (u32)w, (u32)h, 0, 1,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           uploadData, (VkDeviceSize)rlvkGetPixelDataSize(w, h, uploadFormat));
    if (converted)
        RL_FREE(converted);
}

// Get OpenGL internal formats and data type from raylib PixelFormat
void rlGetGlTextureFormats(int format, unsigned int *glInternal, unsigned int *glFormat, unsigned int *glType)
{
    // Compatibility lookup. Internally never consumed; preserved so callers inspecting these
    // values for documentation/logging continue to work.
    *glInternal = 0;
    *glFormat = 0;
    *glType = 0;
    switch (format)
    {
    case RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:
        *glInternal = 0x8229;
        *glFormat = 0x1903;
        *glType = 0x1401;
        break;
    case RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
        *glInternal = 0x822B;
        *glFormat = 0x8227;
        *glType = 0x1401;
        break;
    case RL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:
        *glInternal = 0x8D62;
        *glFormat = 0x1907;
        *glType = 0x8363;
        break;
    case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8:
        *glInternal = 0x8051;
        *glFormat = 0x1907;
        *glType = 0x1401;
        break;
    case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
        *glInternal = 0x8058;
        *glFormat = 0x1908;
        *glType = 0x1401;
        break;
    case RL_PIXELFORMAT_UNCOMPRESSED_R32:
        *glInternal = 0x822E;
        *glFormat = 0x1903;
        *glType = 0x1406;
        break;
    case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
        *glInternal = 0x8814;
        *glFormat = 0x1908;
        *glType = 0x1406;
        break;
    default:
        break;
    }
}

// Get name string for pixel format
const char *rlGetPixelFormatName(unsigned int format)
{
    switch (format)
    {
    case RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:
        return "GRAYSCALE";
    case RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
        return "GRAY_ALPHA";
    case RL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:
        return "R5G6B5";
    case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8:
        return "R8G8B8";
    case RL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
        return "R5G5B5A1";
    case RL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:
        return "R4G4B4A4";
    case RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
        return "R8G8B8A8";
    case RL_PIXELFORMAT_UNCOMPRESSED_R32:
        return "R32";
    case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32:
        return "R32G32B32";
    case RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
        return "R32G32B32A32";
    case RL_PIXELFORMAT_UNCOMPRESSED_R16:
        return "R16";
    case RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16:
        return "R16G16B16";
    case RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:
        return "R16G16B16A16";
    case RL_PIXELFORMAT_COMPRESSED_DXT1_RGB:
        return "DXT1_RGB";
    case RL_PIXELFORMAT_COMPRESSED_DXT1_RGBA:
        return "DXT1_RGBA";
    case RL_PIXELFORMAT_COMPRESSED_DXT3_RGBA:
        return "DXT3_RGBA";
    case RL_PIXELFORMAT_COMPRESSED_DXT5_RGBA:
        return "DXT5_RGBA";
    case RL_PIXELFORMAT_COMPRESSED_ETC1_RGB:
        return "ETC1_RGB";
    case RL_PIXELFORMAT_COMPRESSED_ETC2_RGB:
        return "ETC2_RGB";
    case RL_PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA:
        return "ETC2_EAC_RGBA";
    case RL_PIXELFORMAT_COMPRESSED_PVRT_RGB:
        return "PVRT_RGB";
    case RL_PIXELFORMAT_COMPRESSED_PVRT_RGBA:
        return "PVRT_RGBA";
    case RL_PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA:
        return "ASTC_4x4_RGBA";
    case RL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA:
        return "ASTC_8x8_RGBA";
    default:
        return "UNKNOWN";
    }
}

// Unload texture from GPU memory
void rlUnloadTexture(unsigned int id)
{
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS)
        return;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->inUse)
        return;

    // A recorded (or executing) command buffer may still reference the texture: defer
    rlvkDeferDestroy(VK_NULL_HANDLE, t->image, t->view, t->sampler, t->memory, VK_NULL_HANDLE);
    if (t->sampleImage) // depth shadow-copy twin (Caps.noSampledDepth); shares no sampler
        rlvkDeferDestroy(VK_NULL_HANDLE, t->sampleImage, t->sampleView, VK_NULL_HANDLE, t->sampleMemory, VK_NULL_HANDLE);
    if (t->sampleScratch) // the depth->color bounce buffer
        rlvkDeferDestroy(t->sampleScratch, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, t->sampleScratchMemory, VK_NULL_HANDLE);
    t->image = VK_NULL_HANDLE;
    t->view = VK_NULL_HANDLE;
    t->sampler = VK_NULL_HANDLE;
    t->memory = VK_NULL_HANDLE;
    t->sampleImage = VK_NULL_HANDLE;
    t->sampleView = VK_NULL_HANDLE;
    t->sampleMemory = VK_NULL_HANDLE;
    t->sampleScratch = VK_NULL_HANDLE;
    t->sampleScratchMemory = VK_NULL_HANDLE;
    t->inUse = false;
}

// Generate a full mip chain: read level 0 back, recreate the image with mipLevels, box-filter
// each level on the CPU (matches GL glGenerateMipmap's conventional box filter), upload via host
// image copy. RGBA8 only (every uncompressed load lands there via the RGB->RGBA expansion).
void rlGenTextureMipmaps(unsigned int id, int w, int h, int format, int *mipmaps)
{
    if (mipmaps)
        *mipmaps = 1;
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS)
        return;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->image || t->format != VK_FORMAT_R8G8B8A8_UNORM)
        return;
    (void)format;

    // Flush công việc hiện tại để bắt đầu quy trình nội suy trên GPU
    rlvkFlushFrame();
    rlvkWaitInFlightFrames();

    int mipCount = 1;
    int mw = w, mh = h;
    while (mw > 1 || mh > 1)
    {
        mw = (mw > 1) ? mw / 2 : 1;
        mh = (mh > 1) ? mh / 2 : 1;
        mipCount++;
    }

    // Tối ưu: Đẩy toàn bộ quy trình tính toán mipmap xuống GPU bằng vkCmdBlitImage
    VkImage newImage;
    VkDeviceMemory newMemory;
    RLVK_CHECK(vkCreateImage(RLVK.device,
                             &(VkImageCreateInfo){
                                 VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                 .imageType = VK_IMAGE_TYPE_2D,
                                 .format = t->format,
                                 .extent = {(u32)w, (u32)h, 1},
                                 .mipLevels = (u32)mipCount,
                                 .arrayLayers = 1,
                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                 .tiling = VK_IMAGE_TILING_OPTIMAL,
                                 // Cần TRANSFER_SRC và TRANSFER_DST để thực hiện Blit
                                 .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                             },
                             RLVK_ALLOC, &newImage));

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(RLVK.device, newImage, &memReq);
    newMemory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RLVK_CHECK(vkBindImageMemory(RLVK.device, newImage, newMemory, 0));

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = rlvkOneShotBegin(&pool);
    if (cmd != VK_NULL_HANDLE)
    {
        // Transition ảnh gốc sang SRC và ảnh mới sang DST
        rlvkCmdTransitionImage(cmd, t->image, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1, t->currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        rlvkCmdTransitionImage(cmd, newImage, VK_IMAGE_ASPECT_COLOR_BIT, 0, (u32)mipCount, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Copy Base Level (Level 0) từ ảnh cũ sang ảnh mới
        vkCmdCopyImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, newImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                       &(VkImageCopy){
                           .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                           .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                           .extent = {(u32)w, (u32)h, 1}});

        // Blit theo chuỗi (Từ level i-1 tính toán ra level i)
        int mipWidth = w;
        int mipHeight = h;
        for (int i = 1; i < mipCount; i++)
        {
            rlvkCmdTransitionImage(cmd, newImage, VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            VkImageBlit blit = {
                .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (u32)(i - 1), 0, 1},
                .srcOffsets[0] = {0, 0, 0},
                .srcOffsets[1] = {mipWidth, mipHeight, 1},
                .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (u32)i, 0, 1},
                .dstOffsets[0] = {0, 0, 0},
                .dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1}};

            vkCmdBlitImage(cmd, newImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, newImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            // Level i-1 đã dùng xong, đổi sang định dạng Read-Only
            rlvkCmdTransitionImage(cmd, newImage, VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            if (mipWidth > 1)
                mipWidth /= 2;
            if (mipHeight > 1)
                mipHeight /= 2;
        }

        // Đổi level cuối cùng (nhỏ nhất) sang Read-Only
        rlvkCmdTransitionImage(cmd, newImage, VK_IMAGE_ASPECT_COLOR_BIT, mipCount - 1, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        rlvkOneShotEnd(pool, cmd);
    }

    rlvkDeferDestroy(VK_NULL_HANDLE, t->image, t->view, VK_NULL_HANDLE, t->memory, VK_NULL_HANDLE);

    t->image = newImage;
    t->memory = newMemory;
    t->mipCount = mipCount;
    t->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RLVK_CHECK(vkCreateImageView(RLVK.device,
                                 &(VkImageViewCreateInfo){
                                     VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                     .image = t->image,
                                     .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                     .format = t->format,
                                     .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, (u32)mipCount, 0, 1},
                                 },
                                 RLVK_ALLOC, &t->view));

    rlvkDeferDestroy(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, t->sampler, VK_NULL_HANDLE, VK_NULL_HANDLE);
    RLVK_CHECK(vkCreateSampler(RLVK.device,
                               &(VkSamplerCreateInfo){
                                   VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .magFilter = t->magFilter,
                                   .minFilter = t->minFilter,
                                   .mipmapMode = t->mipMode,
                                   .addressModeU = t->wrapS,
                                   .addressModeV = t->wrapT,
                                   .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                   .maxLod = (f32)mipCount,
                               },
                               RLVK_ALLOC, &t->sampler));

    if (mipmaps)
        *mipmaps = mipCount;
    TRACELOG(RL_LOG_INFO, "RLVK: [ID %u] GPU mipmaps generated (%d levels)", id, mipCount);
}

// Read texture pixel data
void *rlReadTexturePixels(unsigned int id, int width, int height, int format)
{
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS)
        return NULL;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->image)
        return NULL;

    // The texture was loaded as RGBA when the rl format was RGB (3-channel expansion), so read
    // back 4 channels and repack when the caller expects RGB
    bool expanded = (format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8) && (t->format == VK_FORMAT_R8G8B8A8_UNORM);
    size_t pixels = (size_t)width * height;
    size_t gpuBytes = expanded ? pixels * 4 : (size_t)rlvkGetPixelDataSize(width, height, format);
    void *gpuData = RL_MALLOC(gpuBytes);

    // GL order: glGetTexImage sees the texture as already-issued commands left it. Execute the
    // current recording first (a render texture read back mid-frame was returning zeros), then
    // drain in-flight frames before the staging read-back
    rlvkFlushFrame();
    rlvkWaitInFlightFrames();
    rlvkStagingReadImage(t->image, VK_IMAGE_ASPECT_COLOR_BIT, (u32)width, (u32)height,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, gpuData, (VkDeviceSize)gpuBytes);

    if (!expanded)
        return gpuData;

    // Repack RGBA -> RGB for callers that asked for the original 3-channel format
    unsigned char *rgb = (unsigned char *)RL_MALLOC(pixels * 3);
    const unsigned char *src = (const unsigned char *)gpuData;
    for (size_t i = 0; i < pixels; i++)
    {
        rgb[i * 3 + 0] = src[i * 4 + 0];
        rgb[i * 3 + 1] = src[i * 4 + 1];
        rgb[i * 3 + 2] = src[i * 4 + 2];
    }
    RL_FREE(gpuData);
    return rgb;
}

// Read the current color image back to CPU (TakeScreenshot / capture hook): ends the render
// scope, copies to a host buffer, presents, waits, returns RGBA; marks the frame consumed so
// rlvkPresent does not submit it again
unsigned char *rlReadScreenPixels(int width, int height)
{
    (void)width;
    (void)height;
    if (!isGpuReady || !RLVK.swapchain)
        return NULL;

    // No frame recording (TakeScreenshot after EndDrawing, the common raylib pattern):
    // GL's glReadPixels there sees the just-presented frame, which still lives in the
    // PREVIOUS slot's intermediate image (color STOREd, left in TRANSFER_SRC by the flip
    // blit). Read THAT - opening a fresh frame here would capture an empty clear instead.
    if (!RLVK.frameActive)
    {
        if (RLVK.frameCounter == 0)
            return NULL; // nothing was ever drawn/presented
        u32 prev = (u32)((RLVK.frameCounter + RLVK_FRAME_INDEX_COUNT - 1) % RLVK_FRAME_INDEX_COUNT);
        vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[prev], VK_TRUE, UINT64_MAX);
        u32 w = RLVK.swapchainExtent.width, h = RLVK.swapchainExtent.height;
        unsigned char *rows = (unsigned char *)RL_MALLOC((size_t)w * h * 4);
        rlvkStagingReadImage(RLVK.interImage[prev], VK_IMAGE_ASPECT_COLOR_BIT, w, h,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rows, (VkDeviceSize)w * h * 4);
        // The intermediate is unmirrored (GL memory orientation): flip rows for the
        // top-down BGRA->RGBA conversion below, matching the swapchain-path output
        unsigned char *out = (unsigned char *)RL_MALLOC((size_t)w * h * 4);
        for (u32 y = 0; y < h; y++)
            memcpy(out + (size_t)y * w * 4, rows + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
        RL_FREE(rows);
        // Return RGBA with opaque alpha, swapping R/B when the target format is BGRA -
        // identical conversion to the in-frame path below
        bool bgraPrev = (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM) || (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB);
        for (size_t i = 0; i < (size_t)w * h; i++)
        {
            if (bgraPrev)
            {
                unsigned char b = out[i * 4 + 0];
                out[i * 4 + 0] = out[i * 4 + 2];
                out[i * 4 + 2] = b;
            }
            out[i * 4 + 3] = 255;
        }
        return out;
    }

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
    u32 imageIndex = RLVK.currentImageIndex;
    VkImage img = RLVK.swapchainImages[imageIndex];
    u32 w = RLVK.swapchainExtent.width, h = RLVK.swapchainExtent.height;
    VkDeviceSize sizeBytes = (VkDeviceSize)w * h * 4;

    // Transient host-visible readback buffer
    VkBuffer rbBuf = VK_NULL_HANDLE;
    VkDeviceMemory rbMem = VK_NULL_HANDLE;
    void *rbMapped = NULL;
    RLVK_CHECK(vkCreateBuffer(RLVK.device, &(VkBufferCreateInfo){VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeBytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              RLVK_ALLOC, &rbBuf));
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(RLVK.device, rbBuf, &memReq);
    rbMem = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(RLVK.device, rbBuf, rbMem, 0);
    vkMapMemory(RLVK.device, rbMem, 0, sizeBytes, 0, &rbMapped);

    vkCmdEndRenderPass(cmdBuffer);
    rlvkFinishSwapchainImage(cmdBuffer); // flip-blit the frame into the swapchain

    // COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                          .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &(VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT, .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .image = img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}}});

    vk.CmdCopyImageToBuffer(cmdBuffer, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rbBuf, 1, &(VkBufferImageCopy){.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {w, h, 1}});

    // copy write -> host read ; image TRANSFER_SRC -> PRESENT_SRC
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                          .memoryBarrierCount = 1, .pMemoryBarriers = &(VkMemoryBarrier2){VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT, .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT},
                                          .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &(VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT, .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, .image = img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}}});

    vk.EndCommandBuffer(cmdBuffer);

    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                                            .waitSemaphoreInfoCount = RLVK.acquireWaited ? 0u : 1u, // a mid-frame flush may have consumed it
                                                            .pWaitSemaphoreInfos = &(VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = RLVK.acquireSemaphores[frameIndex], .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}, .commandBufferInfoCount = 1, .pCommandBufferInfos = &(VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmdBuffer}, .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &(VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = RLVK.renderSemaphores[imageIndex], .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT}},
                    RLVK.frameFences[frameIndex]);
    RLVK.acquireWaited = true;

    vk.QueuePresentKHR(RLVK.graphicsQueue, &(VkPresentInfoKHR){
                                               VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                               .waitSemaphoreCount = 1, .pWaitSemaphores = &RLVK.renderSemaphores[imageIndex],
                                               .swapchainCount = 1, .pSwapchains = &RLVK.swapchain, .pImageIndices = &imageIndex});

    vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[frameIndex], VK_TRUE, UINT64_MAX);

    // Return RGBA (raylib PIXELFORMAT_UNCOMPRESSED_R8G8B8A8). Swap R/B if the swapchain is BGRA.
    unsigned char *out = (unsigned char *)RL_MALLOC((size_t)w * h * 4);
    const unsigned char *src = (const unsigned char *)rbMapped;
    bool bgra = (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM) || (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB);
    for (size_t i = 0; i < (size_t)w * h; i++)
    {
        out[i * 4 + 0] = bgra ? src[i * 4 + 2] : src[i * 4 + 0];
        out[i * 4 + 1] = src[i * 4 + 1];
        out[i * 4 + 2] = bgra ? src[i * 4 + 0] : src[i * 4 + 2];
        out[i * 4 + 3] = 255; // opaque, like rlgl's rlReadScreenPixels (backbuffer alpha is not retrieved)
    }

    vkUnmapMemory(RLVK.device, rbMem);
    vkDestroyBuffer(RLVK.device, rbBuf, RLVK_ALLOC);
    vkFreeMemory(RLVK.device, rbMem, RLVK_ALLOC);

    RLVK.frameActive = false;
    RLVK.frameConsumed = true;
    if (rlvkDebugFlag("RLVK_DEBUG_FBO", &s_dbgFbo))
        TRACELOG(RL_LOG_WARNING, "VKDBG frameREADBACK fc=%llu", (ull)RLVK.frameCounter);
    return out;
}

// Framebuffer management (fbo)
//-----------------------------------------------------------------------------------------

// Load a framebuffer to be used for rendering
// NOTE: No textures attached
unsigned int rlLoadFramebuffer(void)
{
    u32 slot = rlvkAllocFramebufferSlot();
    if (slot == RLVK_INVALID_SLOT)
        return RLVK_INVALID_SLOT;
    return slot;
}

// Attach color buffer texture to a framebuffer object (unloads previous attachment)
// NOTE: Attach type: 0-Color, 1-Depth renderbuffer, 2-Depth texture
void rlFramebufferAttach(unsigned int fb, unsigned int texId, int attachType, int texType, int mipLevel)
{
    if (fb == 0 || fb >= RLVK_MAX_FRAMEBUFFER_SLOTS)
        return;
    rlvkFramebufferSlot *f = &RLVK.fbSlots[fb];
    (void)texType;
    (void)mipLevel;

    if (attachType >= RL_ATTACHMENT_COLOR_CHANNEL0 && attachType <= RL_ATTACHMENT_COLOR_CHANNEL7)
    {
        f->colorTextures[attachType] = texId;
        if ((u32)(attachType + 1) > f->colorCount)
            f->colorCount = attachType + 1;
    }
    else if (attachType == RL_ATTACHMENT_DEPTH)
    {
        f->depthTexture = texId;
        f->hasDepth = true;
    }
    else if (attachType == RL_ATTACHMENT_STENCIL)
    {
        f->stencilTexture = texId;
        f->hasStencil = true;
    }
    // No VkFramebuffer object created; everything is inferred at vkCmdBeginRendering time.
}

// Release an FBO's private multisample images (see rlvkSetFramebufferSamples). Evicts the
// cached VkFramebuffers that reference their views FIRST - a cached framebuffer outliving its
// attachment view is a use-after-free the moment the same scope shape is opened again.
static void rlvkReleaseFramebufferMsaa(rlvkFramebufferSlot *f)
{
    if (f->msColorView)
        rlvkEvictFramebuffersForView(f->msColorView);
    if (f->msDepthView)
        rlvkEvictFramebuffersForView(f->msDepthView);
    rlvkDeferDestroy(VK_NULL_HANDLE, f->msColorImage, f->msColorView, VK_NULL_HANDLE, f->msColorMemory, VK_NULL_HANDLE);
    rlvkDeferDestroy(VK_NULL_HANDLE, f->msDepthImage, f->msDepthView, VK_NULL_HANDLE, f->msDepthMemory, VK_NULL_HANDLE);
    f->msColorImage = VK_NULL_HANDLE;
    f->msColorView = VK_NULL_HANDLE;
    f->msColorMemory = VK_NULL_HANDLE;
    f->msColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    f->msDepthImage = VK_NULL_HANDLE;
    f->msDepthView = VK_NULL_HANDLE;
    f->msDepthMemory = VK_NULL_HANDLE;
    f->msDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    f->samples = 1;
}

// Allocate one private multisample attachment image + view. Returns false and leaves every
// out-param untouched-or-NULL on any failure, so the caller can degrade to 1 sample.
static bool rlvkCreateMsaaAttachment(u32 width, u32 height, VkFormat format, bool isDepth,
                                     VkImage *outImage, VkImageView *outView, VkDeviceMemory *outMemory)
{
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    // No SAMPLED usage and no TRANSFER: nothing ever reads these directly. That also keeps the
    // Caps.noSampledDepth quirk (SAMPLED on a depth image silently kills depth test on
    // MoltenVK/Intel) out of the multisample depth target by construction.
    VkResult res = vkCreateImage(RLVK.device,
                                 &(VkImageCreateInfo){
                                     VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                     .imageType = VK_IMAGE_TYPE_2D,
                                     .format = format,
                                     .extent = {width, height, 1},
                                     .mipLevels = 1,
                                     .arrayLayers = 1,
                                     .samples = VK_SAMPLE_COUNT_4_BIT,
                                     .tiling = VK_IMAGE_TILING_OPTIMAL,
                                     .usage = isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                     .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                 },
                                 RLVK_ALLOC, &image);
    if (res != VK_SUCCESS)
        return false;
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(RLVK.device, image, &memReq);
    memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if ((memory == VK_NULL_HANDLE) || (vkBindImageMemory(RLVK.device, image, memory, 0) != VK_SUCCESS))
    {
        vkDestroyImage(RLVK.device, image, RLVK_ALLOC);
        if (memory) vkFreeMemory(RLVK.device, memory, RLVK_ALLOC);
        return false;
    }
    res = vkCreateImageView(RLVK.device,
                            &(VkImageViewCreateInfo){
                                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                .image = image,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                .format = format,
                                .subresourceRange = {isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                            },
                            RLVK_ALLOC, &view);
    if (res != VK_SUCCESS)
    {
        vkDestroyImage(RLVK.device, image, RLVK_ALLOC);
        vkFreeMemory(RLVK.device, memory, RLVK_ALLOC);
        return false;
    }
    *outImage = image;
    *outView = view;
    *outMemory = memory;
    return true;
}

// Offscreen MSAA opt-in - see the contract in rlvk.h. Every refusal returns 1 rather than
// failing: a caller that renders single-sampled is correct, just aliased.
int rlvkSetFramebufferSamples(unsigned int fbId, int samples)
{
    if (!isGpuReady || fbId == 0 || fbId >= RLVK_MAX_FRAMEBUFFER_SLOTS)
        return 1;
    rlvkFramebufferSlot *f = &RLVK.fbSlots[fbId];
    if (!f->inUse)
        return 1;
    int want = (samples >= 4) ? 4 : 1;
    if (want == 1)
    {
        if (f->samples > 1)
            rlvkReleaseFramebufferMsaa(f);
        f->samples = 1;
        return 1;
    }
    if (f->samples == 4)
        return 4; // already on; attachments are assumed unchanged (rlgl never re-attaches in place)

    rlvkTextureSlot *color = (f->colorCount == 1) ? &RLVK.textureSlots[f->colorTextures[0]] : NULL;
    rlvkTextureSlot *depth = f->hasDepth ? &RLVK.textureSlots[f->depthTexture] : NULL;
    if (!RLVK.Caps.msaa4x || !color || !color->image || (f->colorCount != 1))
    {
        TRACELOG(RL_LOG_INFO, "RLVK: offscreen MSAA declined for fb %u (msaa4x=%d colorCount=%u)",
                 fbId, (int)RLVK.Caps.msaa4x, f->colorCount);
        return 1;
    }
    if (depth && depth->image && !RLVK.Caps.depthResolve)
    {
        // No depth resolve => the depth texture the caller attached would keep whatever stale
        // contents it had while the scene wrote a multisample depth nobody can read. Aliased
        // and correct beats anti-aliased with a dead depth buffer.
        TRACELOG(RL_LOG_INFO, "RLVK: offscreen MSAA declined for fb %u (no VK_KHR_depth_stencil_resolve)", fbId);
        return 1;
    }
    if (!rlvkCreateMsaaAttachment((u32)color->width, (u32)color->height, color->format, false,
                                  &f->msColorImage, &f->msColorView, &f->msColorMemory))
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: offscreen MSAA colour target allocation failed for fb %u", fbId);
        return 1;
    }
    if (depth && depth->image &&
        !rlvkCreateMsaaAttachment((u32)depth->width, (u32)depth->height, depth->format, true,
                                  &f->msDepthImage, &f->msDepthView, &f->msDepthMemory))
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: offscreen MSAA depth target allocation failed for fb %u", fbId);
        rlvkReleaseFramebufferMsaa(f);
        return 1;
    }
    f->msColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    f->msDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    f->samples = 4;
    TRACELOG(RL_LOG_INFO, "RLVK: offscreen MSAA x4 active on fb %u (%dx%d, depth resolve=%d)",
             fbId, color->width, color->height, (int)(f->msDepthImage != VK_NULL_HANDLE));
    return 4;
}

// Verify render texture is complete
bool rlFramebufferComplete(unsigned int fb)
{
    if (fb == 0 || fb >= RLVK_MAX_FRAMEBUFFER_SLOTS)
        return false;
    rlvkFramebufferSlot *f = &RLVK.fbSlots[fb];
    rlDisableFramebuffer(); // rlgl unbinds the framebuffer after the completeness check
    return f->inUse && (f->colorCount > 0 || f->hasDepth);
}

// Unload framebuffer from GPU memory
// NOTE: All attached textures/cubemaps/renderbuffers are also deleted
void rlUnloadFramebuffer(unsigned int fb)
{
    if (fb == 0 || fb >= RLVK_MAX_FRAMEBUFFER_SLOTS)
        return;
    // The depth/stencil attachment is deleted with the framebuffer, exactly like the GL
    // backend (which queries GL_DEPTH_ATTACHMENT and deletes it): UnloadRenderTexture()
    // relies on this - color attachments stay caller-owned
    rlvkFramebufferSlot *slot = &RLVK.fbSlots[fb];
    if (slot->samples > 1)
        rlvkReleaseFramebufferMsaa(slot);
    if (slot->hasDepth && slot->depthTexture)
        rlUnloadTexture(slot->depthTexture);
    if (slot->hasStencil && slot->stencilTexture && (slot->stencilTexture != slot->depthTexture))
        rlUnloadTexture(slot->stencilTexture);
    memset(slot, 0, sizeof(rlvkFramebufferSlot));
}

// Copy framebuffer pixel data to internal buffer
void rlCopyFramebuffer(int x, int y, int w, int h, int f, void *p)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)f;
    (void)p;
}
// Resize internal framebuffer
void rlResizeFramebuffer(int w, int h)
{
    (void)w;
    (void)h;
}
