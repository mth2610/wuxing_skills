//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Host<->image staging transfer, textures, framebuffers (FBO)
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

//----------------------------------------------------------------------------------
// Host <-> image transfer (Vulkan 1.1 baseline)
//----------------------------------------------------------------------------------

static VkCommandBuffer rlvkOneShotBegin(VkCommandPool *outPool)
{
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
                                         VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});
    *outPool = pool;
    return cmdBuffer;
}

static void rlvkOneShotEnd(VkCommandPool pool, VkCommandBuffer cmdBuffer)
{
    vk.EndCommandBuffer(cmdBuffer);
    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){
                                               VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                               .commandBufferInfoCount = 1,
                                               .pCommandBufferInfos = &(VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmdBuffer},
                                           },
                    VK_NULL_HANDLE);

    bool profile = rlvkDebugFlag("RLVK_PROFILE", &s_dbgProfile);
    f64 profileTime = profile ? rlvkProfileNow() : 0;
    vkQueueWaitIdle(RLVK.graphicsQueue);
    if (profile)
        s_profileEndIdleMs += rlvkProfileNow() - profileTime;
    vkDestroyCommandPool(RLVK.device, pool, RLVK_ALLOC);
}

// Invalidate cached descriptor shadow entries referencing a dying view or sampler
static void rlvkInvalidateDescriptorShadow(VkImageView dyingView, VkSampler dyingSampler)
{
    bool touched = false;
    for (u32 b = 0; b < RLVK_MAX_TEXTURE_UNITS; b++)
    {
        if (dyingView && RLVK.pushedView[b] == dyingView)
        {
            RLVK.pushedView[b] = VK_NULL_HANDLE;
            touched = true;
        }
        if (dyingSampler && RLVK.pushedSampler[b] == dyingSampler)
        {
            RLVK.pushedSampler[b] = VK_NULL_HANDLE;
            touched = true;
        }
    }
    if (touched)
        RLVK.set0Dirty = true;
    s_bindingValid = false;
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
                                              .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                                              .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                              .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
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
    if (stagingMem == VK_NULL_HANDLE)
    {
        vkDestroyBuffer(RLVK.device, staging, RLVK_ALLOC);
        return;
    }
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
    s_memHostBytes -= (ill)memReq.size;
}

// Staging read-back of a full single-layer image (GL-style synchronous pixel read).
static void rlvkStagingReadImage(VkImage image, VkImageAspectFlags aspect,
                                 u32 width, u32 height, VkImageLayout currentLayout, void *out, VkDeviceSize bytes)
{
    if (!image || !out || bytes == 0)
        return;

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
    s_memHostBytes -= (ill)memReq.size;
}

static u32 rlvkCreateVBO(const void *data, int size, bool isIndex, bool dynamic)
{
    if (!isGpuReady || size <= 0)
        return RLVK_INVALID_SLOT;

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
    if (b->buffer != VK_NULL_HANDLE)
    {
        if (b->mapped && b->memory)
            vkUnmapMemory(RLVK.device, b->memory);
        rlvkDeferDestroy(b->buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, b->memory, VK_NULL_HANDLE);
    }
    s_vboCreateCount++;
    b->sizeBytes = (u32)size;
    b->isIndex = isIndex;
    VkBufferUsageFlags usage =
        (isIndex ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                 : (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));

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
    u32 slot = rlvkCreateVBO(data, size, false, dynamic);
    if (slot == RLVK_INVALID_SLOT)
        return 0;
    RLVK.State.currentVBO = slot;
    return slot;
}

// Load a new attributes element buffer
unsigned int rlLoadVertexBufferElement(const void *data, int size, bool dynamic)
{
    u32 slot = rlvkCreateVBO(data, size, true, dynamic);
    if (slot == RLVK_INVALID_SLOT)
        return 0;
    if (RLVK.State.currentVAO && RLVK.State.currentVAO < RLVK_MAX_VAO_SLOTS)
        RLVK.vertexArrays[RLVK.State.currentVAO].indexSlot = slot;
    return slot;
}

// Update vertex buffer with new data
void rlUpdateVertexBuffer(unsigned int id, const void *d, int s, int o)
{
    if (!id || id >= RLVK_MAX_BUFFER_SLOTS || !d || s <= 0 || o < 0)
        return;
    rlvkBufferSlot *b = &RLVK.bufferSlots[id];
    if ((u32)o + (u32)s > b->sizeBytes)
        return;
    if (b->mapped)
        memcpy((char *)b->mapped + o, d, (size_t)s);
    else if (b->buffer)
        rlvkUploadBuffer(b->buffer, (u32)o, d, (u32)s);
}

// Update vertex buffer elements with new data
void rlUpdateVertexBufferElements(unsigned int id, const void *d, int s, int o) { rlUpdateVertexBuffer(id, d, s, o); }

void rlUnloadVertexArray(unsigned int id)
{
    if (id && id < RLVK_MAX_VAO_SLOTS)
    {
        RLVK.vertexArrays[id].inUse = false;
        if (RLVK.State.currentVAO == id)
            RLVK.State.currentVAO = 0;
        s_bindingValid = false;
    }
}

// Unload vertex buffer (VBO)
void rlUnloadVertexBuffer(unsigned int id)
{
    if (id && id < RLVK_MAX_BUFFER_SLOTS)
    {
        rlvkBufferSlot *b = &RLVK.bufferSlots[id];
        b->inUse = false;
        b->freedFrame = RLVK.frameCounter;
        s_bindingValid = false;

        // Static DEVICE_LOCAL buffers are not reused by the dynamic pool: queue for destruction
        if (b->mapped == NULL && b->buffer != VK_NULL_HANDLE)
        {
            rlvkDeferDestroy(b->buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, b->memory, VK_NULL_HANDLE);
            b->buffer = VK_NULL_HANDLE;
            b->memory = VK_NULL_HANDLE;
            b->sizeBytes = 0;
        }
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
        break;
    default:
        break;
    }
}

void rlSetVertexAttributeDivisor(unsigned int idx, int d)
{
    (void)idx;
    (void)d;
}
void rlSetVertexAttributeDefault(int loc, const void *v, int t, int c)
{
    (void)loc;
    (void)v;
    (void)t;
    (void)c;
}

// Record a mesh draw (DrawMesh path)
static void rlvkDrawMesh(int offset, int count, bool indexed, int instances)
{
    if (!isGpuReady || count <= 0)
        return;
    u32 vao = RLVK.State.currentVAO;
    if (!vao || vao >= RLVK_MAX_VAO_SLOTS || !RLVK.vertexArrays[vao].inUse)
        return;
    rlvkVertexArray *a = &RLVK.vertexArrays[vao];
    if (a->posSlot == 0 || a->posSlot >= RLVK_MAX_BUFFER_SLOTS || RLVK.bufferSlots[a->posSlot].buffer == VK_NULL_HANDLE)
        return;

    rlvkBeginFrame();
    if (!RLVK.frameActive)
        return;
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];

    bool hasUV = (a->uvSlot != 0) && (a->uvSlot < RLVK_MAX_BUFFER_SLOTS) && (RLVK.bufferSlots[a->uvSlot].buffer != VK_NULL_HANDLE);
    bool hasNormal = (a->normalSlot != 0) && (a->normalSlot < RLVK_MAX_BUFFER_SLOTS) && (RLVK.bufferSlots[a->normalSlot].buffer != VK_NULL_HANDLE);
    bool hasColor = (a->colorSlot != 0) && (a->colorSlot < RLVK_MAX_BUFFER_SLOTS) && (RLVK.bufferSlots[a->colorSlot].buffer != VK_NULL_HANDLE);
    VkBuffer dummy = RLVK.bufferSlots[RLVK.dummyAttribSlot].buffer;

    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];

    if (shader->usesUbo && RLVK.frameActive)
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
                    s_bindingValid = false;
                }
            }
        }
    }

    bool wantUV2 = (a->uv2Slot != 0) && (a->uv2Slot < RLVK_MAX_BUFFER_SLOTS) && (shader->attribLocs[RLVK_ATTRIB_TEXCOORD2] >= 0);
    bool wantTan = (a->tangentSlot != 0) && (a->tangentSlot < RLVK_MAX_BUFFER_SLOTS) && (shader->attribLocs[RLVK_ATTRIB_TANGENT] >= 0);
    bool wantBones = (shader->attribLocs[RLVK_ATTRIB_BONEIDS] >= 0) && (shader->attribLocs[RLVK_ATTRIB_BONEWEIGHTS] >= 0);
    bool wantInst = (instances > 1) && a->instSlot && (a->instSlot < RLVK_MAX_BUFFER_SLOTS) && (shader->attribLocs[RLVK_ATTRIB_INSTANCE_TX] >= 0);
    bool realBones = (a->boneIdSlot != 0) && (a->boneWtSlot != 0) &&
                     (a->boneIdSlot < RLVK_MAX_BUFFER_SLOTS) && (a->boneWtSlot < RLVK_MAX_BUFFER_SLOTS);

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

    int rlvkTexSlot = RLVK.State.activeTextureSlots[0] ? (int)RLVK.State.activeTextureSlots[0] : (int)RLVK.State.currentTextureSlot;
    rlvkBindingSig bsig = {
        .shaderSlot = (int)RLVK.State.activeShaderSlot,
        .texSlot = rlvkTexSlot,
        .posSlot = (int)a->posSlot,
        .uvSlot = (int)a->uvSlot,
        .normalSlot = (int)a->normalSlot,
        .colorSlot = (int)a->colorSlot,
        .uv2Slot = (int)a->uv2Slot,
        .tangentSlot = (int)a->tangentSlot,
        .boneIdSlot = (int)a->boneIdSlot,
        .boneWtSlot = (int)a->boneWtSlot,
        .posOff = (ull)a->posOffset,
        .uvOff = (ull)a->uvOffset,
        .normalOff = (ull)a->normalOffset,
        .colorOff = (ull)a->colorOffset,
        .uv2Off = (ull)a->uv2Offset,
        .tangentOff = (ull)a->tangentOffset,
        .boneIdOff = (ull)a->boneIdOffset,
        .boneWtOff = (ull)a->boneWtOffset,
        .instSlot = wantInst ? (int)a->instSlot : 0,
        .indexSlot = indexed ? (int)a->indexSlot : 0,
        .instOff = wantInst ? (ull)a->instOffset : 0,
    };
    bool sameBinding = (s_bindingValid && (memcmp(&bsig, &s_bindingSig, sizeof(bsig)) == 0));
    if (!sameBinding)
    {
        s_bindingSig = bsig;
        s_bindingValid = true;
    }

    if (!rlvkBindPipeline(cmdBuffer, 1, vertexLayout, RLVK.State.activeShaderSlot))
        return;

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
                                   hasUV ? a->uvOffset : 0,
                                   hasNormal ? a->normalOffset : 12,
                                   hasColor ? a->colorOffset : 8,
                               });

    if (shader->usesUbo)
    {
        rlvkBindShaderUbos(cmdBuffer, shader);
        rlvkBindShaderSsbos(cmdBuffer, shader);
        if (!sameBinding)
            rlvkBindShaderSamplers(cmdBuffer, shader, true);
    }
    else
    {
        rlvkPushConstants pc;
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
            rlvkPushTexture(cmdBuffer, 0, (u32)rlvkTexSlot);
    }

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

    if (wantInst && !sameBinding)
        vkCmdBindVertexBuffers(cmdBuffer, 4, 1,
                               (VkBuffer[]){RLVK.bufferSlots[a->instSlot].buffer}, (VkDeviceSize[]){a->instOffset});

    if (!sameBinding)
        rlvkBindDummyAttribBuffers(cmdBuffer, vertexLayout, shader);

    rlvkFlushSet0(cmdBuffer);
    rlvkProfileDraw();
    if (indexed && a->indexSlot && a->indexSlot < RLVK_MAX_BUFFER_SLOTS && RLVK.bufferSlots[a->indexSlot].buffer != VK_NULL_HANDLE)
    {
        if (!sameBinding)
            vkCmdBindIndexBuffer(cmdBuffer, RLVK.bufferSlots[a->indexSlot].buffer, 0, VK_INDEX_TYPE_UINT16);
        vk.CmdDrawIndexed(cmdBuffer, count, (instances > 0) ? instances : 1, offset, 0, 0);
    }
    else
        vk.CmdDraw(cmdBuffer, count, (instances > 0) ? instances : 1, offset, 0);
}

void rlDrawVertexArray(int offset, int count) { rlvkDrawMesh(offset, count, false, 1); }
void rlDrawVertexArrayElements(int offset, int count, const void *b)
{
    (void)b;
    rlvkDrawMesh(offset, count, true, 1);
}
void rlDrawVertexArrayInstanced(int offset, int count, int instances) { rlvkDrawMesh(offset, count, false, instances); }
void rlDrawVertexArrayElementsInstanced(int offset, int count, const void *b, int instances)
{
    (void)b;
    rlvkDrawMesh(offset, count, true, instances);
}

//-----------------------------------------------------------------------------------------
// Textures data management
//-----------------------------------------------------------------------------------------

unsigned int rlLoadTexture(const void *data, int width, int height, int format, int mipmapCount)
{
    (void)mipmapCount;
    if (!isGpuReady || width <= 0 || height <= 0)
        return RLVK_INVALID_SLOT;

    u32 slot = rlvkAllocTextureSlot();
    if (slot == RLVK_INVALID_SLOT)
        return RLVK_INVALID_SLOT;

    rlvkTextureSlot *t = &RLVK.textureSlots[slot];
    t->width = width;
    t->height = height;
    t->mipCount = 1;
    t->rlFormat = format;
    VkFormat vkfmt = rlvkGetVkTextureFormat(format);
    t->format = vkfmt;

    VkComponentMapping swizzle = {0};
    const void *uploadData = data;
    void *converted = NULL;
    size_t pixels = (size_t)width * height;

    if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE ||
        format == RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA ||
        format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)
        vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
    else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16)
        vkfmt = VK_FORMAT_R16G16B16A16_SFLOAT;
    else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32)
        vkfmt = VK_FORMAT_R32G32B32A32_SFLOAT;

    if (data != NULL)
    {
        if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE)
        {
            const unsigned char *s = (const unsigned char *)data;
            uint32_t *d = (uint32_t *)RL_MALLOC(pixels * 4);
            for (size_t i = 0; i < pixels; i++)
            {
                uint32_t v = s[i];
                d[i] = v | (v << 8) | (v << 16) | 0xFF000000u;
            }
            converted = d;
            uploadData = d;
        }
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA)
        {
            const unsigned char *s = (const unsigned char *)data;
            uint32_t *d = (uint32_t *)RL_MALLOC(pixels * 4);
            for (size_t i = 0; i < pixels; i++)
            {
                uint32_t g = s[i * 2];
                uint32_t a = s[i * 2 + 1];
                d[i] = g | (g << 8) | (g << 16) | (a << 24);
            }
            converted = d;
            uploadData = d;
        }
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)
        {
            const unsigned char *s = (const unsigned char *)data;
            uint32_t *d = (uint32_t *)RL_MALLOC(pixels * 4);
            for (size_t i = 0; i < pixels; i++)
                d[i] = (uint32_t)s[i * 3] | ((uint32_t)s[i * 3 + 1] << 8) | ((uint32_t)s[i * 3 + 2] << 16) | 0xFF000000u;
            converted = d;
            uploadData = d;
        }
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16)
        {
            const unsigned short *s = (const unsigned short *)data;
            unsigned short *d = (unsigned short *)RL_MALLOC(pixels * 4 * sizeof(unsigned short));
            for (size_t i = 0; i < pixels; i++)
            {
                d[i * 4 + 0] = s[i * 3 + 0];
                d[i * 4 + 1] = s[i * 3 + 1];
                d[i * 4 + 2] = s[i * 3 + 2];
                d[i * 4 + 3] = 0x3C00;
            }
            converted = d;
            uploadData = d;
        }
        else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32)
        {
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
    t->mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    t->mipEnabled = false;
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

unsigned int rlLoadTextureDepth(int width, int height, bool useRenderBuffer)
{
    (void)useRenderBuffer;
    if (!isGpuReady || width <= 0 || height <= 0)
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
                                 .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | (RLVK.Caps.noSampledDepth ? 0 : VK_IMAGE_USAGE_SAMPLED_BIT),
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
    t->mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    t->mipEnabled = false;
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

unsigned int rlLoadTextureCubemap(const void *data, int size, int format, int mipmapCount)
{
    (void)mipmapCount;
    if (!isGpuReady || size <= 0)
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

    const void *uploadData = data;
    void *converted = NULL;
    size_t facePixels = (size_t)size * size;
    if (data && format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)
    {
        vkfmt = VK_FORMAT_R8G8B8A8_UNORM;
        const unsigned char *src = (const unsigned char *)data;
        size_t totalPixels = facePixels * 6;
        uint32_t *dst = (uint32_t *)RL_MALLOC(totalPixels * 4);
        for (size_t i = 0; i < totalPixels; i++)
            dst[i] = (uint32_t)src[i * 3] | ((uint32_t)src[i * 3 + 1] << 8) | ((uint32_t)src[i * 3 + 2] << 16) | 0xFF000000u;
        converted = dst;
        uploadData = dst;
    }
    t->format = vkfmt;
    u32 faceBytes = (vkfmt == VK_FORMAT_R8G8B8A8_UNORM) ? (u32)(facePixels * 4) : (u32)rlvkGetPixelDataSize(size, size, format);

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

    t->minFilter = VK_FILTER_LINEAR;
    t->mipMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    t->mipEnabled = false;
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

void rlUpdateTexture(unsigned int id, int x, int y, int w, int h, int format, const void *data)
{
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS || !data || w <= 0 || h <= 0)
        return;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->image)
        return;

    const void *uploadData = data;
    void *converted = NULL;
    int uploadFormat = format;
    size_t pixels = (size_t)w * h;
    if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE)
    {
        const unsigned char *src = (const unsigned char *)data;
        uint32_t *dst = (uint32_t *)RL_MALLOC(pixels * 4);
        for (size_t i = 0; i < pixels; i++)
        {
            uint32_t v = src[i];
            dst[i] = v | (v << 8) | (v << 16) | 0xFF000000u;
        }
        converted = dst;
        uploadData = dst;
        uploadFormat = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    }
    else if (format == RL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA)
    {
        const unsigned char *src = (const unsigned char *)data;
        uint32_t *dst = (uint32_t *)RL_MALLOC(pixels * 4);
        for (size_t i = 0; i < pixels; i++)
        {
            uint32_t g = src[i * 2];
            uint32_t a = src[i * 2 + 1];
            dst[i] = g | (g << 8) | (g << 16) | (a << 24);
        }
        converted = dst;
        uploadData = dst;
        uploadFormat = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    }
    else if (format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8)
    {
        const unsigned char *src = (const unsigned char *)data;
        uint32_t *dst = (uint32_t *)RL_MALLOC(pixels * 4);
        for (size_t i = 0; i < pixels; i++)
            dst[i] = (uint32_t)src[i * 3] | ((uint32_t)src[i * 3 + 1] << 8) | ((uint32_t)src[i * 3 + 2] << 16) | 0xFF000000u;
        converted = dst;
        uploadData = dst;
        uploadFormat = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    }

    if (!RLVK.frameActive && RLVK.swapchain && RLVK.frameCounter)
        rlvkBeginFrame();

    VkDeviceSize bytes = (VkDeviceSize)rlvkGetPixelDataSize(w, h, uploadFormat);
    if (rlvkDebugFlag("RLVK_PROFILE", &s_dbgProfile))
    {
        s_profileTextureUploads++;
        s_profileTextureBytes += bytes;
    }
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
    VkDeviceSize stagingOff = (RLVK.arenaOffset[frameIndex] + 15) & ~(VkDeviceSize)15;
    if (RLVK.frameActive && stagingOff + bytes > arena->sizeBytes)
    {
        RLVK.arenaWanted[frameIndex] += bytes + 16;
        rlvkFlushFrame();
        stagingOff = (RLVK.arenaOffset[frameIndex] + 15) & ~(VkDeviceSize)15;
    }
    if (RLVK.frameActive && stagingOff + bytes <= arena->sizeBytes)
    {
        if (rlvkDebugFlag("RLVK_PROFILE", &s_dbgProfile))
            s_profileTextureAsync++;
        VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
        memcpy((char *)arena->mapped + stagingOff, uploadData, (size_t)bytes);
        RLVK.arenaOffset[frameIndex] = stagingOff + bytes;
        RLVK.arenaWanted[frameIndex] += bytes + 16;

        u32 openFb = RLVK.scope.fbSlot;
        if (openFb)
            rlDisableFramebuffer();

        rlvkProfileEndScope();
        vkCmdEndRenderPass(cmdBuffer);
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                              VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                              .imageMemoryBarrierCount = 1,
                                              .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                  .oldLayout = t->currentLayout,
                                                  .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                  .image = t->image,
                                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, (u32)t->mipCount, 0, 1},
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
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                  .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                  .image = t->image,
                                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, (u32)t->mipCount, 0, 1},
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

    rlvkWaitInFlightFrames();
    rlvkStagingUploadImage(t->image, VK_IMAGE_ASPECT_COLOR_BIT, x, y, (u32)w, (u32)h, 0, 1,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           uploadData, bytes);
    if (converted)
        RL_FREE(converted);
}

void rlGetGlTextureFormats(int format, unsigned int *glInternal, unsigned int *glFormat, unsigned int *glType)
{
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

void rlUnloadTexture(unsigned int id)
{
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS)
        return;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->inUse)
        return;

    rlvkInvalidateDescriptorShadow(t->view, t->sampler);
    if (t->sampleView)
        rlvkInvalidateDescriptorShadow(t->sampleView, VK_NULL_HANDLE);

    rlvkDeferDestroy(VK_NULL_HANDLE, t->image, t->view, t->sampler, t->memory, VK_NULL_HANDLE);
    if (t->sampleImage)
        rlvkDeferDestroy(VK_NULL_HANDLE, t->sampleImage, t->sampleView, VK_NULL_HANDLE, t->sampleMemory, VK_NULL_HANDLE);
    if (t->sampleScratch)
        rlvkDeferDestroy(t->sampleScratch, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, t->sampleScratchMemory, VK_NULL_HANDLE);
    memset(t, 0, sizeof(*t));
}

void rlGenTextureMipmaps(unsigned int id, int w, int h, int format, int *mipmaps)
{
    if (mipmaps)
        *mipmaps = 1;
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS || w <= 0 || h <= 0)
        return;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->image || t->format != VK_FORMAT_R8G8B8A8_UNORM)
        return;
    (void)format;

    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(RLVK.physicalDevice, t->format, &properties);
    VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & needed) != needed)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: [ID %u] linear mip blits unsupported; retaining existing levels", id);
        if (mipmaps)
            *mipmaps = t->mipCount;
        return;
    }

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
        rlvkCmdTransitionImage(cmd, t->image, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1, t->currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        rlvkCmdTransitionImage(cmd, newImage, VK_IMAGE_ASPECT_COLOR_BIT, 0, (u32)mipCount, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vkCmdCopyImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, newImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                       &(VkImageCopy){
                           .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                           .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                           .extent = {(u32)w, (u32)h, 1}});

        int mipWidth = w;
        int mipHeight = h;
        for (int i = 1; i < mipCount; i++)
        {
            rlvkCmdTransitionImage(cmd, newImage, VK_IMAGE_ASPECT_COLOR_BIT, (u32)(i - 1), 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            VkImageBlit blit = {
                .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (u32)(i - 1), 0, 1},
                .srcOffsets[0] = {0, 0, 0},
                .srcOffsets[1] = {mipWidth, mipHeight, 1},
                .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (u32)i, 0, 1},
                .dstOffsets[0] = {0, 0, 0},
                .dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1}};

            vk.CmdBlitImage(cmd, newImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, newImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            rlvkCmdTransitionImage(cmd, newImage, VK_IMAGE_ASPECT_COLOR_BIT, (u32)(i - 1), 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            if (mipWidth > 1)
                mipWidth /= 2;
            if (mipHeight > 1)
                mipHeight /= 2;
        }

        rlvkCmdTransitionImage(cmd, newImage, VK_IMAGE_ASPECT_COLOR_BIT, (u32)(mipCount - 1), 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        rlvkOneShotEnd(pool, cmd);
    }

    rlvkInvalidateDescriptorShadow(t->view, t->sampler);
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
                                   .maxLod = t->mipEnabled ? (f32)(mipCount - 1) : 0.0f,
                               },
                               RLVK_ALLOC, &t->sampler));

    if (mipmaps)
        *mipmaps = mipCount;
    TRACELOG(RL_LOG_INFO, "RLVK: [ID %u] GPU mipmaps generated (%d levels)", id, mipCount);
}

void *rlReadTexturePixels(unsigned int id, int width, int height, int format)
{
    if (id == 0 || id >= RLVK_MAX_TEXTURE_SLOTS || width <= 0 || height <= 0)
        return NULL;
    rlvkTextureSlot *t = &RLVK.textureSlots[id];
    if (!t->image)
        return NULL;

    bool expanded = (format == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8) && (t->format == VK_FORMAT_R8G8B8A8_UNORM);
    size_t pixels = (size_t)width * height;
    size_t gpuBytes = expanded ? pixels * 4 : (size_t)rlvkGetPixelDataSize(width, height, format);
    void *gpuData = RL_MALLOC(gpuBytes);

    rlvkFlushFrame();
    rlvkWaitInFlightFrames();
    rlvkStagingReadImage(t->image, VK_IMAGE_ASPECT_COLOR_BIT, (u32)width, (u32)height,
                         t->currentLayout, gpuData, (VkDeviceSize)gpuBytes);

    if (!expanded)
        return gpuData;

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

static inline uint32_t rlvkSwizzleScreenPixel(uint32_t px, bool bgra)
{
    if (bgra)
        return (px & 0x0000FF00u) | ((px & 0x00FF0000u) >> 16) | ((px & 0x000000FFu) << 16) | 0xFF000000u;
    return px | 0xFF000000u;
}

unsigned char *rlReadScreenPixels(int width, int height)
{
    (void)width;
    (void)height;
    if (!isGpuReady || !RLVK.swapchain)
        return NULL;

    if (!RLVK.frameActive)
    {
        if (RLVK.frameCounter == 0)
            return NULL;
        u32 prev = (u32)((RLVK.frameCounter + RLVK_FRAME_INDEX_COUNT - 1) % RLVK_FRAME_INDEX_COUNT);
        vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[prev], VK_TRUE, UINT64_MAX);
        u32 w = RLVK.swapchainExtent.width, h = RLVK.swapchainExtent.height;
        unsigned char *out = (unsigned char *)RL_MALLOC((size_t)w * h * 4);
        if (!out)
            return NULL;
        rlvkStagingReadImage(RLVK.interImage[prev], VK_IMAGE_ASPECT_COLOR_BIT, w, h,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out, (VkDeviceSize)w * h * 4);

        bool bgraPrev = (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM) || (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB);
        uint32_t *pixels32 = (uint32_t *)out;
        u32 halfH = h / 2;
        for (u32 y = 0; y < halfH; y++)
        {
            uint32_t *topRow = pixels32 + (size_t)y * w;
            uint32_t *botRow = pixels32 + (size_t)(h - 1 - y) * w;
            for (u32 x = 0; x < w; x++)
            {
                uint32_t topPx = rlvkSwizzleScreenPixel(topRow[x], bgraPrev);
                uint32_t botPx = rlvkSwizzleScreenPixel(botRow[x], bgraPrev);
                topRow[x] = botPx;
                botRow[x] = topPx;
            }
        }
        if (h & 1u)
        {
            uint32_t *midRow = pixels32 + (size_t)halfH * w;
            for (u32 x = 0; x < w; x++)
                midRow[x] = rlvkSwizzleScreenPixel(midRow[x], bgraPrev);
        }
        return out;
    }

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
    u32 imageIndex = RLVK.currentImageIndex;
    VkImage img = RLVK.swapchainImages[imageIndex];
    u32 w = RLVK.swapchainExtent.width, h = RLVK.swapchainExtent.height;
    VkDeviceSize sizeBytes = (VkDeviceSize)w * h * 4;

    VkBuffer rbBuf = VK_NULL_HANDLE;
    VkDeviceMemory rbMem = VK_NULL_HANDLE;
    void *rbMapped = NULL;
    RLVK_CHECK(vkCreateBuffer(RLVK.device, &(VkBufferCreateInfo){VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeBytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              RLVK_ALLOC, &rbBuf));
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(RLVK.device, rbBuf, &memReq);
    rbMem = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (rbMem == VK_NULL_HANDLE)
    {
        vkDestroyBuffer(RLVK.device, rbBuf, RLVK_ALLOC);
        return NULL;
    }
    vkBindBufferMemory(RLVK.device, rbBuf, rbMem, 0);
    vkMapMemory(RLVK.device, rbMem, 0, sizeBytes, 0, &rbMapped);

    rlvkProfileEndScope();
    vkCmdEndRenderPass(cmdBuffer);
    rlvkFinishSwapchainImage(cmdBuffer);

    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                          .imageMemoryBarrierCount = 1,
                                          .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                                              VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                              .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                              .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                              .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                              .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                              .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                              .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                              .image = img,
                                              .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}}});

    vk.CmdCopyImageToBuffer(cmdBuffer, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rbBuf, 1, &(VkBufferImageCopy){.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {w, h, 1}});

    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                          .memoryBarrierCount = 1,
                                          .pMemoryBarriers = &(VkMemoryBarrier2){
                                              VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                              .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                              .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                              .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
                                              .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT},
                                          .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &(VkImageMemoryBarrier2){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT, .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, .image = img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}}});

    vk.EndCommandBuffer(cmdBuffer);

    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .waitSemaphoreInfoCount = RLVK.acquireWaited ? 0u : 1u, .pWaitSemaphoreInfos = &(VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = RLVK.acquireSemaphores[frameIndex], .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}, .commandBufferInfoCount = 1, .pCommandBufferInfos = &(VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmdBuffer}, .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &(VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = RLVK.renderSemaphores[imageIndex], .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT}},
                    RLVK.frameFences[frameIndex]);
    RLVK.acquireWaited = true;

    vk.QueuePresentKHR(RLVK.graphicsQueue, &(VkPresentInfoKHR){
                                               VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                               .waitSemaphoreCount = 1, .pWaitSemaphores = &RLVK.renderSemaphores[imageIndex],
                                               .swapchainCount = 1, .pSwapchains = &RLVK.swapchain, .pImageIndices = &imageIndex});

    vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[frameIndex], VK_TRUE, UINT64_MAX);

    unsigned char *out = (unsigned char *)RL_MALLOC((size_t)w * h * 4);
    if (out)
    {
        const uint32_t *src32 = (const uint32_t *)rbMapped;
        uint32_t *dst32 = (uint32_t *)out;
        bool bgra = (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM) || (RLVK.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB);
        size_t totalPixels = (size_t)w * h;
        for (size_t i = 0; i < totalPixels; i++)
            dst32[i] = rlvkSwizzleScreenPixel(src32[i], bgra);
    }

    vkUnmapMemory(RLVK.device, rbMem);
    vkDestroyBuffer(RLVK.device, rbBuf, RLVK_ALLOC);
    vkFreeMemory(RLVK.device, rbMem, RLVK_ALLOC);
    s_memHostBytes -= (ill)memReq.size;

    RLVK.frameActive = false;
    RLVK.frameConsumed = true;
    if (rlvkDebugFlag("RLVK_DEBUG_FBO", &s_dbgFbo))
        TRACELOG(RL_LOG_WARNING, "VKDBG frameREADBACK fc=%llu", (ull)RLVK.frameCounter);
    return out;
}

//-----------------------------------------------------------------------------------------
// Framebuffer management (fbo)
//-----------------------------------------------------------------------------------------

unsigned int rlLoadFramebuffer(void)
{
    u32 slot = rlvkAllocFramebufferSlot();
    if (slot == RLVK_INVALID_SLOT)
        return RLVK_INVALID_SLOT;
    return slot;
}

void rlFramebufferAttach(unsigned int fb, unsigned int texId, int attachType, int texType, int mipLevel)
{
    if (fb == 0 || fb >= RLVK_MAX_FRAMEBUFFER_SLOTS)
        return;
    rlvkFramebufferSlot *f = &RLVK.fbSlots[fb];
    (void)texType;
    (void)mipLevel;

    if (texId > 0 && texId < RLVK_MAX_TEXTURE_SLOTS && f->width == 0 && f->height == 0)
    {
        f->width = (u32)RLVK.textureSlots[texId].width;
        f->height = (u32)RLVK.textureSlots[texId].height;
    }

    if (attachType >= RL_ATTACHMENT_COLOR_CHANNEL0 && attachType <= RL_ATTACHMENT_COLOR_CHANNEL7)
    {
        f->colorTextures[attachType] = texId;
        if (texId != 0 && (u32)(attachType + 1) > f->colorCount)
            f->colorCount = attachType + 1;
    }
    else if (attachType == RL_ATTACHMENT_DEPTH)
    {
        f->depthTexture = texId;
        f->hasDepth = (texId != 0);
    }
    else if (attachType == RL_ATTACHMENT_STENCIL)
    {
        f->stencilTexture = texId;
        f->hasStencil = (texId != 0);
    }
}

bool rlvkSetFramebufferDepthSampleOnDemand(unsigned int fbId, bool enabled)
{
    if (!isGpuReady || fbId == 0 || fbId >= RLVK_MAX_FRAMEBUFFER_SLOTS)
        return false;
    rlvkFramebufferSlot *f = &RLVK.fbSlots[fbId];
    if (!f->inUse || !f->hasDepth || !f->depthTexture ||
        f->depthTexture >= RLVK_MAX_TEXTURE_SLOTS ||
        !RLVK.textureSlots[f->depthTexture].image ||
        (RLVK.frameActive && RLVK.scope.fbSlot == fbId))
        return false;
    f->depthSampleOnDemand = enabled;
    return true;
}

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

static bool rlvkCreateMsaaAttachment(u32 width, u32 height, VkFormat format, bool isDepth, int samples,
                                     VkImage *outImage, VkImageView *outView, VkDeviceMemory *outMemory)
{
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult res = vkCreateImage(RLVK.device,
                                 &(VkImageCreateInfo){
                                     VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                     .imageType = VK_IMAGE_TYPE_2D,
                                     .format = format,
                                     .extent = {width, height, 1},
                                     .mipLevels = 1,
                                     .arrayLayers = 1,
                                     .samples = (VkSampleCountFlagBits)samples,
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
        if (memory)
        {
            vkFreeMemory(RLVK.device, memory, RLVK_ALLOC);
            s_memLocalBytes -= (ill)memReq.size;
        }
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
        s_memLocalBytes -= (ill)memReq.size;
        return false;
    }
    *outImage = image;
    *outView = view;
    *outMemory = memory;
    return true;
}

int rlvkSetFramebufferSamples(unsigned int fbId, int samples)
{
    if (!isGpuReady || fbId == 0 || fbId >= RLVK_MAX_FRAMEBUFFER_SLOTS)
        return 1;
    rlvkFramebufferSlot *f = &RLVK.fbSlots[fbId];
    if (!f->inUse)
        return 1;
    int want = (samples >= 4) ? 4 : ((samples >= 2) ? 2 : 1);
    if (want == 1)
    {
        if (f->samples > 1)
            rlvkReleaseFramebufferMsaa(f);
        f->samples = 1;
        return 1;
    }
    if (f->samples == want)
        return want;
    if (f->samples > 1)
        rlvkReleaseFramebufferMsaa(f);

    rlvkTextureSlot *color = (f->colorCount == 1 && f->colorTextures[0] < RLVK_MAX_TEXTURE_SLOTS)
                                 ? &RLVK.textureSlots[f->colorTextures[0]]
                                 : NULL;
    rlvkTextureSlot *depth = (f->hasDepth && f->depthTexture < RLVK_MAX_TEXTURE_SLOTS)
                                 ? &RLVK.textureSlots[f->depthTexture]
                                 : NULL;
    if ((want == 4 && !RLVK.Caps.msaa4x) || !color || !color->image || (f->colorCount != 1))
    {
        TRACELOG(RL_LOG_INFO, "RLVK: offscreen MSAA declined for fb %u (msaa4x=%d colorCount=%u)",
                 fbId, (int)RLVK.Caps.msaa4x, f->colorCount);
        return 1;
    }
    if (depth && depth->image && !RLVK.Caps.depthResolve)
    {
        TRACELOG(RL_LOG_INFO, "RLVK: offscreen MSAA declined for fb %u (no VK_KHR_depth_stencil_resolve)", fbId);
        return 1;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(RLVK.physicalDevice, &props);
    VkSampleCountFlags supported = props.limits.framebufferColorSampleCounts;
    rlvkTextureSlot *attachments[2] = {color, depth};
    for (int a = 0; a < 2; a++)
    {
        if (!attachments[a] || !attachments[a]->image)
            continue;
        if (a == 1)
            supported &= props.limits.framebufferDepthSampleCounts;
        VkImageFormatProperties imageProps;
        VkResult result = vkGetPhysicalDeviceImageFormatProperties(RLVK.physicalDevice,
                                                                   attachments[a]->format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                                   a == 1 ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                                   0, &imageProps);
        if (result != VK_SUCCESS || (u32)attachments[a]->width > imageProps.maxExtent.width ||
            (u32)attachments[a]->height > imageProps.maxExtent.height)
            supported = 0;
        else
            supported &= imageProps.sampleCounts;
    }
    if (!(supported & (VkSampleCountFlags)want))
    {
        TRACELOG(RL_LOG_INFO, "RLVK: offscreen MSAA x%d declined for fb %u (attachment sample mask=0x%x)",
                 want, fbId, (unsigned int)supported);
        return 1;
    }
    if (!rlvkCreateMsaaAttachment((u32)color->width, (u32)color->height, color->format, false, want,
                                  &f->msColorImage, &f->msColorView, &f->msColorMemory))
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: offscreen MSAA colour target allocation failed for fb %u", fbId);
        return 1;
    }
    if (depth && depth->image &&
        !rlvkCreateMsaaAttachment((u32)depth->width, (u32)depth->height, depth->format, true, want,
                                  &f->msDepthImage, &f->msDepthView, &f->msDepthMemory))
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: offscreen MSAA depth target allocation failed for fb %u", fbId);
        rlvkReleaseFramebufferMsaa(f);
        return 1;
    }
    f->msColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    f->msDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    f->samples = (unsigned char)want;
    TRACELOG(RL_LOG_INFO, "RLVK: offscreen MSAA x%d active on fb %u (%dx%d, depth resolve=%d)",
             want, fbId, color->width, color->height, (int)(f->msDepthImage != VK_NULL_HANDLE));
    return want;
}

bool rlFramebufferComplete(unsigned int fb)
{
    if (fb == 0 || fb >= RLVK_MAX_FRAMEBUFFER_SLOTS)
        return false;
    rlvkFramebufferSlot *f = &RLVK.fbSlots[fb];
    rlDisableFramebuffer();
    return f->inUse && (f->colorCount > 0 || f->hasDepth);
}

void rlUnloadFramebuffer(unsigned int fb)
{
    if (fb == 0 || fb >= RLVK_MAX_FRAMEBUFFER_SLOTS)
        return;
    if (RLVK.frameActive && RLVK.scope.fbSlot == fb)
        rlDisableFramebuffer();
    if (RLVK.blitReadFb == fb)
        RLVK.blitReadFb = 0;

    rlvkFramebufferSlot *slot = &RLVK.fbSlots[fb];
    if (slot->samples > 1)
        rlvkReleaseFramebufferMsaa(slot);
    if (slot->hasDepth && slot->depthTexture)
        rlUnloadTexture(slot->depthTexture);
    if (slot->hasStencil && slot->stencilTexture && (slot->stencilTexture != slot->depthTexture))
        rlUnloadTexture(slot->stencilTexture);
    memset(slot, 0, sizeof(rlvkFramebufferSlot));
}

void rlCopyFramebuffer(int x, int y, int w, int h, int f, void *p)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)f;
    (void)p;
}
void rlResizeFramebuffer(int w, int h)
{
    (void)w;
    (void)h;
}