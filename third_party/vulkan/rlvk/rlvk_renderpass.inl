//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Render-pass + framebuffer cache implementation
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

//----------------------------------------------------------------------------------
// Render-pass + framebuffer cache implementation (Vulkan 1.1 baseline)
//----------------------------------------------------------------------------------

// Get (or build and cache) the render pass for a scope-shape key. Attachment layouts are
// initial == final (rlvk transitions layouts with explicit barriers outside the pass), so a
// pass never changes an image's layout behind the manual tracking's back.
static VkRenderPass rlvkGetRenderPass(const rlvkRenderPassKey *key)
{
    for (int i = 0; i < RLVK.renderPassCount; i++)
        if (memcmp(&RLVK.renderPasses[i].key, key, sizeof(rlvkRenderPassKey)) == 0)
            return RLVK.renderPasses[i].pass;

    if (RLVK.renderPassCount >= RLVK_MAX_RENDER_PASSES)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: render-pass cache full (%d), raise RLVK_MAX_RENDER_PASSES", RLVK_MAX_RENDER_PASSES);
        return VK_NULL_HANDLE;
    }

    VkSampleCountFlagBits samples = (key->samples > 1) ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
    bool hasDepth = (key->depthFormat != VK_FORMAT_UNDEFINED);
    u32 attCount = 0;
    VkAttachmentDescription atts[RLVK_MAX_SCOPE_ATTACHMENTS];
    VkAttachmentReference colorRefs[8], resolveRef, depthRef;

    for (u32 c = 0; c < key->colorCount; c++)
    {
        atts[attCount] = (VkAttachmentDescription){
            .format = key->colorFormats[c],
            .samples = samples,
            .loadOp = (VkAttachmentLoadOp)key->colorLoad,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        colorRefs[c] = (VkAttachmentReference){attCount, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        attCount++;
    }
    if (key->hasResolve) // MSAA fixed-function resolve into a 1x attachment (single color scope only)
    {
        atts[attCount] = (VkAttachmentDescription){
            .format = key->colorFormats[0],
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        resolveRef = (VkAttachmentReference){attCount, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        attCount++;
    }
    if (hasDepth)
    {
        atts[attCount] = (VkAttachmentDescription){
            .format = key->depthFormat,
            .samples = samples,
            .loadOp = (VkAttachmentLoadOp)key->depthLoad,
            .storeOp = (VkAttachmentStoreOp)key->depthStore,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        depthRef = (VkAttachmentReference){attCount, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        attCount++;
    }

    VkRenderPass pass = VK_NULL_HANDLE;
    VkResult res = vkCreateRenderPass(RLVK.device,
                                      &(VkRenderPassCreateInfo){
                                          VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                          .attachmentCount = attCount,
                                          .pAttachments = atts,
                                          .subpassCount = 1,
                                          .pSubpasses = &(VkSubpassDescription){
                                              .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              .colorAttachmentCount = key->colorCount,
                                              .pColorAttachments = key->colorCount ? colorRefs : NULL,
                                              .pResolveAttachments = key->hasResolve ? &resolveRef : NULL,
                                              .pDepthStencilAttachment = hasDepth ? &depthRef : NULL,
                                          },
                                          // No subpass dependencies: rlvk orders everything with explicit barriers outside
                                          // the pass, exactly like the dynamic-rendering path did
                                      },
                                      RLVK_ALLOC, &pass);
    if (res != VK_SUCCESS)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: vkCreateRenderPass => %d", (int)res);
        return VK_NULL_HANDLE;
    }

    RLVK.renderPasses[RLVK.renderPassCount++] = (rlvkRenderPassEntry){pass, *key};
    return pass;
}

// Get (or build and cache) the framebuffer for a pass + attachment view set
static VkFramebuffer rlvkGetFramebuffer(VkRenderPass pass, const VkImageView *views, u32 viewCount, u32 width, u32 height)
{
    for (int i = 0; i < RLVK.framebufferCount; i++)
    {
        rlvkFramebufferEntry *e = &RLVK.framebuffers[i];
        if ((e->pass == pass) && (e->viewCount == viewCount) && (e->width == width) && (e->height == height) && (memcmp(e->views, views, viewCount * sizeof(VkImageView)) == 0))
            return e->framebuffer;
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
                                           .renderPass = pass,
                                           .attachmentCount = viewCount,
                                           .pAttachments = views,
                                           .width = width,
                                           .height = height,
                                           .layers = 1,
                                       },
                                       RLVK_ALLOC, &fb);
    if (res != VK_SUCCESS)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: vkCreateFramebuffer => %d", (int)res);
        return VK_NULL_HANDLE;
    }

    rlvkFramebufferEntry *e = &RLVK.framebuffers[RLVK.framebufferCount++];
    memset(e, 0, sizeof(*e));
    e->framebuffer = fb;
    e->pass = pass;
    e->viewCount = viewCount;
    e->width = width;
    e->height = height;
    memcpy(e->views, views, viewCount * sizeof(VkImageView));
    return fb;
}

static void rlvkDeferDestroy(VkBuffer buffer, VkImage image, VkImageView view, VkSampler sampler, VkDeviceMemory memory, VkPipeline pipeline);
static void rlvkDeferDestroyFramebufferOnly(VkFramebuffer framebuffer);

// Evict every cached framebuffer that references a dying view. MUST run before the view is
// destroyed or queued for deferred destruction; the framebuffers ride the same fence-gated
// dead-resource ring so in-flight frames finish with them intact.
static void rlvkEvictFramebuffersForView(VkImageView view)
{
    if (view == VK_NULL_HANDLE)
        return;
    for (int i = 0; i < RLVK.framebufferCount;)
    {
        rlvkFramebufferEntry *e = &RLVK.framebuffers[i];
        bool hit = false;
        for (u32 v = 0; v < e->viewCount; v++)
            if (e->views[v] == view)
            {
                hit = true;
                break;
            }
        if (!hit)
        {
            i++;
            continue;
        }
        rlvkDeferDestroyFramebufferOnly(e->framebuffer);
        RLVK.framebuffers[i] = RLVK.framebuffers[--RLVK.framebufferCount]; // swap-remove
    }
}

// Open a rendering scope: cached render pass + cached framebuffer + vkCmdBeginRenderPass.
// clearColor/clearDepth are consumed only by CLEAR load ops of the key.
static void rlvkBeginScopeRenderPass(VkCommandBuffer cmdBuffer, const rlvkRenderPassKey *key,
                                     const VkImageView *views, u32 viewCount, u32 width, u32 height,
                                     const VkClearValue *clearColor, const VkClearValue *clearDepth)
{
    VkRenderPass pass = rlvkGetRenderPass(key);
    if (pass == VK_NULL_HANDLE)
        return;
    VkFramebuffer fb = rlvkGetFramebuffer(pass, views, viewCount, width, height);
    if (fb == VK_NULL_HANDLE)
        return;

    // pClearValues is indexed by attachment: colors [0..colorCount), resolve, depth last
    VkClearValue clears[RLVK_MAX_SCOPE_ATTACHMENTS];
    memset(clears, 0, sizeof(clears));
    if (clearColor)
        for (u32 c = 0; c < key->colorCount; c++)
            clears[c] = *clearColor;
    if (clearDepth)
        clears[key->colorCount + (key->hasResolve ? 1u : 0u)] = *clearDepth;

    vkCmdBeginRenderPass(cmdBuffer,
                         &(VkRenderPassBeginInfo){
                             VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                             .renderPass = pass,
                             .framebuffer = fb,
                             .renderArea = {{0, 0}, {width, height}},
                             .clearValueCount = viewCount, // >= highest CLEAR attachment index + 1
                             .pClearValues = clears,
                         },
                         VK_SUBPASS_CONTENTS_INLINE);
}

// Switch the rendering scope to a user framebuffer (render texture). The pending batch
// was already flushed by raylib's BeginTextureMode/EndTextureMode before this is called.
void rlEnableFramebuffer(unsigned int id)
{
    RLVK.State.stateGeneration++;
    RLVK.State.currentFramebufferSlot = id;
    if (!isGpuReady || id == 0 || id >= RLVK_MAX_FRAMEBUFFER_SLOTS)
        return;
    if (RLVK.scope.fbSlot == id && RLVK.frameActive)
        return; // this framebuffer's scope is already open

    rlvkBeginFrame(); // ensure a frame is active (opens the swapchain scope)
    if (!RLVK.frameActive)
        return;
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
    rlvkFramebufferSlot *f = &RLVK.fbSlots[id];
    u32 colorCount = 0;
    rlvkTextureSlot *colors[8] = {0};
    for (u32 c = 0; c < f->colorCount && c < 8; c++)
    {
        rlvkTextureSlot *t = &RLVK.textureSlots[f->colorTextures[c]];
        if (t->image)
            colors[colorCount++] = t;
    }
    rlvkTextureSlot *color = colorCount ? colors[0] : NULL;
    rlvkTextureSlot *fbDepth = f->hasDepth ? &RLVK.textureSlots[f->depthTexture] : NULL;
    if (!color && !(fbDepth && fbDepth->image))
        return; // nothing attached yet

    vkCmdEndRenderPass(cmdBuffer);

    for (u32 c = 0; c < colorCount; c++)
    {
        // Color texture -> COLOR_ATTACHMENT. oldLayout MUST be the tracked layout, not a
        // hardcoded SHADER_READ_ONLY: a freshly-created FBO texture is still UNDEFINED on its
        // first bind, and a fixed oldLayout trips VUID-VkImageMemoryBarrier-oldLayout-01211
        // (the depth barrier below already does this). Already-COLOR means nothing to transition.
        if (colors[c]->currentLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            continue;
        VkImageLayout oldL = colors[c]->currentLayout;
        bool wasRead = (oldL == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                              VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                              .imageMemoryBarrierCount = 1,
                                              .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = wasRead ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                                                  .srcAccessMask = wasRead ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                  .oldLayout = oldL,
                                                  .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                  .image = colors[c]->image,
                                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
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
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                  .oldLayout = (depth->currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                                                  .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                  .image = depth->image,
                                                  .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
                                              },
                                          });
        depth->currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    // GL semantics: framebuffer content persists across binds -> loadOp LOAD (ClearBackground
    // inside the texture mode clears explicitly). Depth-only framebuffers (shadowmaps) have no
    // color attachment at all; depth must be STORED so it can be sampled afterwards.
    // GL semantics: framebuffer content persists across binds -> loadOp LOAD
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
    if (fbHasDepth)
        scopeViews[scopeViewCount++] = depth->view;

    rpKey.depthFormat = fbHasDepth ? depth->format : VK_FORMAT_UNDEFINED;
    rpKey.colorCount = (unsigned char)colorCount;
    rpKey.samples = 1;
    rpKey.colorLoad = VK_ATTACHMENT_LOAD_OP_LOAD;

    // VÁ LỖI LOGIC: Sử dụng LOAD_OP_LOAD cho depth để không làm mất dữ liệu chiều sâu
    // Người dùng sẽ tự xóa nó thông qua hàm rlClearScreenBuffers (ClearBackground)
    rpKey.depthLoad = VK_ATTACHMENT_LOAD_OP_LOAD;
    rpKey.depthStore = VK_ATTACHMENT_STORE_OP_STORE;

    // Vì dùng LOAD, không cần truyền clearValues vào nữa
    rlvkBeginScopeRenderPass(cmdBuffer, &rpKey, scopeViews, scopeViewCount, (u32)fbW, (u32)fbH, NULL, NULL);

    RLVK.scope.fbSlot = id;
    RLVK.scope.width = (u32)fbW;
    RLVK.scope.height = (u32)fbH;
    RLVK.scope.colorCount = colorCount;
    for (u32 c = 0; c < colorCount; c++)
        RLVK.scope.colorFormats[c] = colors[c]->format;
    RLVK.scope.samples = 1;
    RLVK.scope.flipY = false;
}

// Back to the swapchain scope; the render texture becomes sampleable again
void rlDisableFramebuffer(void)
{
    RLVK.State.stateGeneration++;
    u32 id = RLVK.scope.fbSlot; // the scope actually open (bookkeeping may be stale)
    RLVK.State.currentFramebufferSlot = 0;
    if (!isGpuReady || id == 0 || id >= RLVK_MAX_FRAMEBUFFER_SLOTS || !RLVK.frameActive)
        return;

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
    rlvkFramebufferSlot *f = &RLVK.fbSlots[id];

    vkCmdEndRenderPass(cmdBuffer);

    for (u32 c = 0; c < f->colorCount && c < 8; c++)
    {
        rlvkTextureSlot *color = &RLVK.textureSlots[f->colorTextures[c]];
        if (!color->image || color->currentLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            continue;
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                              VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                              .imageMemoryBarrierCount = 1,
                                              .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                  .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                  .image = color->image,
                                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                              },
                                          });
        color->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // Make the depth texture sampleable for depth-render / shadowmap / soft-particle shaders.
    if (f->hasDepth)
    {
        rlvkTextureSlot *depth = &RLVK.textureSlots[f->depthTexture];
        if (depth->image && depth->sampleImage)
        {
            // Caps.noSampledDepth (MoltenVK/Intel, §7.1): the attachment depth has no SAMPLED usage
            // (transitioning it to SHADER_READ_ONLY is even illegal — VUID-...-oldLayout-01211), and
            // Metal can't sample a depth-format texture via sampler2D anyway. Bounce the raw depth
            // through sampleScratch into the R32F color twin (buffer copies cross the depth/color
            // aspect that vkCmdCopyImage cannot); the attachment stays in its resting
            // DEPTH_STENCIL_ATTACHMENT_OPTIMAL (the scope-open guard then skips re-transitioning it).
            vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                                  VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                                  .imageMemoryBarrierCount = 2,
                                                  .pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
                                                      {
                                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                          .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                          .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                          .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                          .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                                          .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                          .image = depth->image,
                                                          .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
                                                      },
                                                      {
                                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                          .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                          .srcAccessMask = 0,
                                                          .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                          .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, // prior twin contents are stale; discard
                                                          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                          .image = depth->sampleImage,
                                                          .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                                      },
                                                  },
                                              });
            // depth image (DEPTH aspect) -> scratch buffer -> twin (COLOR aspect), same 4 bytes/texel
            vkCmdCopyImageToBuffer(cmdBuffer, depth->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   depth->sampleScratch, 1,
                                   &(VkBufferImageCopy){
                                       .imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                                       .imageExtent = {(u32)depth->width, (u32)depth->height, 1},
                                   });
            vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                                  VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                                  .bufferMemoryBarrierCount = 1,
                                                  .pBufferMemoryBarriers = &(VkBufferMemoryBarrier2){
                                                      VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                                      .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                      .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                      .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                      .buffer = depth->sampleScratch,
                                                      .size = VK_WHOLE_SIZE,
                                                  },
                                              });
            vkCmdCopyBufferToImage(cmdBuffer, depth->sampleScratch, depth->sampleImage,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &(VkBufferImageCopy){
                                       .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                       .imageExtent = {(u32)depth->width, (u32)depth->height, 1},
                                   });
            vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                                  VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                                  .imageMemoryBarrierCount = 2,
                                                  .pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
                                                      {
                                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                          .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                          .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                                          .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                          .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                          .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                          .image = depth->image,
                                                          .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
                                                      },
                                                      {
                                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                          .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                                          .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                          .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                          .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                          .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                          .image = depth->sampleImage,
                                                          .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                                      },
                                                  },
                                              });
            depth->currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth->sampleLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        else if (depth->image)
        {
            // Healthy driver: the attachment depth carries SAMPLED — transition it directly.
            vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                                  VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                                  .imageMemoryBarrierCount = 1,
                                                  .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                                                      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                      .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                      .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                      .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                      .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                      .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                      .image = depth->image,
                                                      .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
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
    scopeViews[scopeViewCount++] = msaa ? RLVK.msaaView[frameIndex] : RLVK.interView[frameIndex];
    if (msaa)
        scopeViews[scopeViewCount++] = RLVK.interView[frameIndex];
    scopeViews[scopeViewCount++] = RLVK.depthView[frameIndex];
    rlvkRenderPassKey rpKey;
    memset(&rpKey, 0, sizeof(rpKey));
    rpKey.colorFormats[0] = RLVK.swapchainFormat;
    rpKey.depthFormat = RLVK.depthFormat;
    rpKey.colorCount = 1;
    rpKey.samples = msaa ? 4 : 1;
    rpKey.colorLoad = VK_ATTACHMENT_LOAD_OP_LOAD;
    rpKey.depthLoad = VK_ATTACHMENT_LOAD_OP_LOAD;
    rpKey.depthStore = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    rpKey.hasResolve = msaa ? 1 : 0;
    rlvkBeginScopeRenderPass(cmdBuffer, &rpKey, scopeViews, scopeViewCount,
                             RLVK.swapchainExtent.width, RLVK.swapchainExtent.height, NULL, NULL);
    RLVK.scope.fbSlot = 0;
    RLVK.scope.width = RLVK.swapchainExtent.width;
    RLVK.scope.height = RLVK.swapchainExtent.height;
    RLVK.scope.colorCount = 1;
    RLVK.scope.colorFormats[0] = RLVK.swapchainFormat;
    RLVK.scope.samples = (u32)RLVK.msaaSamples;
    RLVK.scope.flipY = false; // UNMIRRORED: swapchain-scope rendering matches GL memory orientation
    if (rlvkDebugFlag("RLVK_DEBUG_FBO", &s_dbgFbo))
        TRACELOG(RL_LOG_WARNING, "VKDBG frameOPEN fc=%llu img=%u", (ull)RLVK.frameCounter, RLVK.currentImageIndex);
}

// Submit the current recording, wait for it, resume recording in the same frame (same arena and
// swapchain image). Host reads of GPU-written resources need every RECORDED command executed
// first, matching GL's in-order reads; vkDeviceWaitIdle cannot run work that was never submitted.
// Wait the fences of every frame slot except the recording one: full completion of submitted
// work without vkDeviceWaitIdle's whole-device drain (the recording slot's fence is reset with
// nothing pending, so including it would deadlock)
static void rlvkWaitInFlightFrames(void)
{
    u32 recording = RLVK.frameActive ? (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT) : UINT32_MAX;
    for (u32 i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
        if ((i != recording) && (RLVK.frameFences[i] != VK_NULL_HANDLE))
            vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[i], VK_TRUE, UINT64_MAX);
}

static void rlvkFlushFrame(void)
{
    if (!RLVK.frameActive)
        return;
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];

    // Close any open FBO scope through its normal path (attachment layout transitions), then
    // suspend the swapchain scope
    u32 openFb = RLVK.scope.fbSlot;
    if (openFb)
        rlDisableFramebuffer();
    vkCmdEndRenderPass(cmdBuffer);
    vk.EndCommandBuffer(cmdBuffer);

    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){
                                               VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                               .waitSemaphoreInfoCount = RLVK.acquireWaited ? 0u : 1u,
                                               .pWaitSemaphoreInfos = &(VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = RLVK.acquireSemaphores[frameIndex], .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                                               .commandBufferInfoCount = 1,
                                               .pCommandBufferInfos = &(VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmdBuffer},
                                           },
                    RLVK.frameFences[frameIndex]);
    RLVK.acquireWaited = true;

    vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[frameIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(RLVK.device, 1, &RLVK.frameFences[frameIndex]);
    RLVK.arenaOffset[frameIndex] = 0; // the drained submission consumed all arena data: reuse from the start

    // Resume recording into the same frame: fresh command buffer, content preserved (LOAD)
    vkResetCommandPool(RLVK.device, RLVK.cmdPools[frameIndex], 0);
    vk.BeginCommandBuffer(cmdBuffer, &(VkCommandBufferBeginInfo){
                                         VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});
    RLVK.boundPipeline = VK_NULL_HANDLE;                 // pipeline binding is command-buffer state too
    memset(RLVK.pushedView, 0, sizeof(RLVK.pushedView)); // push-descriptor state resets with the command buffer
    memset(RLVK.pushedSsbo, 0xFF, sizeof(RLVK.pushedSsbo)); // graphics-SSBO pushes die with the cb too (0xFF = never pushed)
    s_pipelineFastValid = false;
    RLVK.State.cbEpoch++;
    memset(RLVK.pushedSampler, 0, sizeof(RLVK.pushedSampler));
    s_viewportValid = false;
    s_bindingValid = false;
    // Pool-ring fallback: the drained submission released every snapshot set; the arena reset
    // also invalidated the shadowed UBO regions
    memset(RLVK.shadowUbo, 0, sizeof(RLVK.shadowUbo));
    RLVK.set0Dirty = true;
    if (!RLVK.Caps.pushDescriptor && RLVK.descPools[frameIndex])
    {
        vkResetDescriptorPool(RLVK.device, RLVK.descPools[frameIndex], 0);
        RLVK.set0CacheCount[frameIndex] = 0; // pool reset freed every cached snapshot set
        RLVK.boundSet0 = VK_NULL_HANDLE;
    }
    if (RLVK.computeDescPools[frameIndex])
        vkResetDescriptorPool(RLVK.device, RLVK.computeDescPools[frameIndex], 0);

    // Host writes made after this point (arena bump continues) must be visible again
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                          .memoryBarrierCount = 1,
                                          .pMemoryBarriers = &(VkMemoryBarrier2){
                                              VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                              .srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
                                              .srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT,
                                              .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                              .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                          },
                                      });

    rlvkResumeSwapchainScope(cmdBuffer);
    if (openFb)
        rlEnableFramebuffer(openFb);
}
// return the active render texture (fbo)
unsigned int rlGetActiveFramebuffer(void) { return RLVK.State.currentFramebufferSlot; }
// Activate multiple draw color buffers
// NOTE: One color buffer is always active by default
void rlActiveDrawBuffers(int count) { (void)count; }
// Depth-only blit (GL_DEPTH_BUFFER_BIT) from the READ framebuffer's depth texture into the
// current scope's depth target - deferred rendering copies gbuffer depth to the backbuffer so
// forward geometry depth-tests against it. Color blits unimplemented (nothing exercises them).
void rlBlitFramebuffer(int srcX, int srcY, int srcWidth, int srcHeight, int dstX, int dstY, int dstWidth, int dstHeight, int bufferMask)
{
    (void)srcX;
    (void)srcY;
    (void)dstX;
    (void)dstY;
    if (!(bufferMask & 0x00000100))
        return; // GL_DEPTH_BUFFER_BIT only
    if (!RLVK.frameActive || RLVK.blitReadFb == 0 || RLVK.blitReadFb >= RLVK_MAX_FRAMEBUFFER_SLOTS)
        return;
    rlvkFramebufferSlot *src = &RLVK.fbSlots[RLVK.blitReadFb];
    if (!src->hasDepth)
        return;
    rlvkTextureSlot *srcDepth = &RLVK.textureSlots[src->depthTexture];
    if (!srcDepth->image)
        return;
    if (RLVK.scope.fbSlot != 0)
        return;

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];

    vkCmdEndRenderPass(cmdBuffer);

    // VÁ LỖI LOGIC: Lưu lại layout ban đầu của ảnh nguồn để trả về đúng trạng thái
    VkImageLayout originalSrcLayout = srcDepth->currentLayout;

    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                          .imageMemoryBarrierCount = 2,
                                          .pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
                                              {
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = (originalSrcLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) ? (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT) : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                  .srcAccessMask = (originalSrcLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                                  .oldLayout = originalSrcLayout,
                                                  .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                  .image = srcDepth->image,
                                                  .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
                                              },
                                              {
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                  .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                  .image = RLVK.depthImage[frameIndex],
                                                  .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
                                              },
                                          },
                                      });

    vk.CmdBlitImage(cmdBuffer, srcDepth->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    RLVK.depthImage[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                    &(VkImageBlit){
                        .srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                        .srcOffsets = {{0, 0, 0}, {srcWidth, srcHeight, 1}},
                        .dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                        .dstOffsets = {{0, 0, 0}, {dstWidth, dstHeight, 1}},
                    },
                    VK_FILTER_NEAREST);

    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                          .imageMemoryBarrierCount = 2,
                                          .pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
                                              {
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                                  // Trả về đúng Stage/Access dựa trên layout ban đầu
                                                  .dstStageMask = (originalSrcLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) ? (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT) : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                  .dstAccessMask = (originalSrcLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) ? (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                  .newLayout = originalSrcLayout,
                                                  .image = srcDepth->image,
                                                  .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
                                              },
                                              {
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                  .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                  .image = RLVK.depthImage[frameIndex],
                                                  .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
                                              },
                                          },
                                      });

    // Gán lại đúng layout cũ thay vì ép thành ATTACHMENT_OPTIMAL
    srcDepth->currentLayout = originalSrcLayout;

    rlvkResumeSwapchainScope(cmdBuffer);
    RLVK.blitReadFb = 0;
}
// Bind framebuffer object (fbo)
void rlBindFramebuffer(unsigned int target, unsigned int fb)
{
    if (target == RL_READ_FRAMEBUFFER)
        RLVK.blitReadFb = fb; // source for rlBlitFramebuffer
    else if (fb == 0)
        rlDisableFramebuffer();
    else
        rlEnableFramebuffer(fb);
}

// Enable color blending
void rlEnableColorBlend(void)
{
    RLVK.State.colorBlendEnabled = true;
    RLVK.State.stateGeneration++;
}
// Disable color blending
void rlDisableColorBlend(void)
{
    RLVK.State.colorBlendEnabled = false;
    RLVK.State.stateGeneration++;
}
// Enable depth test
void rlEnableDepthTest(void)
{
    RLVK.State.depthTest = true;
    RLVK.State.stateGeneration++;
}
// Disable depth test
void rlDisableDepthTest(void)
{
    RLVK.State.depthTest = false;
    RLVK.State.stateGeneration++;
}
// Enable depth write
void rlEnableDepthMask(void)
{
    RLVK.State.depthWrite = true;
    RLVK.State.stateGeneration++;
}
// Disable depth write
void rlDisableDepthMask(void)
{
    RLVK.State.depthWrite = false;
    RLVK.State.stateGeneration++;
}
// Enable backface culling
void rlEnableBackfaceCulling(void)
{
    RLVK.State.cullEnabled = true;
    RLVK.State.stateGeneration++;
}
// Disable backface culling
void rlDisableBackfaceCulling(void)
{
    RLVK.State.cullEnabled = false;
    RLVK.State.stateGeneration++;
}
// Set color mask active for screen read/draw
void rlColorMask(bool r, bool g, bool b, bool a)
{
    RLVK.State.colorMask[0] = r;
    RLVK.State.colorMask[1] = g;
    RLVK.State.colorMask[2] = b;
    RLVK.State.colorMask[3] = a;
}
// Set face culling mode
void rlSetCullFace(int mode)
{
    RLVK.State.cullMode = mode;
    RLVK.State.stateGeneration++;
}
// Enable scissor test
void rlEnableScissorTest(void)
{
    RLVK.State.scissorEnabled = true;
    RLVK.State.stateGeneration++;
}
// Disable scissor test
void rlDisableScissorTest(void)
{
    RLVK.State.scissorEnabled = false;
    RLVK.State.stateGeneration++;
}
// Scissor test
void rlScissor(int x, int y, int w, int h)
{
    RLVK.State.scissorX = x;
    RLVK.State.scissorY = y;
    RLVK.State.scissorW = w;
    RLVK.State.scissorH = h;
    RLVK.State.stateGeneration++;
}
// Enable point mode
void rlEnablePointMode(void)
{
    RLVK.State.pointMode = true;
    RLVK.State.stateGeneration++;
}
// Disable point mode
void rlDisablePointMode(void)
{
    RLVK.State.pointMode = false;
    RLVK.State.stateGeneration++;
}
// Set the point drawing size
void rlSetPointSize(f32 s) { RLVK.State.pointSize = s; }
// Get the point drawing size
f32 rlGetPointSize(void) { return RLVK.State.pointSize; }
// Enable wire mode
void rlEnableWireMode(void)
{
    RLVK.State.wireMode = true;
    RLVK.State.stateGeneration++;
}
// Disable wire mode
void rlDisableWireMode(void)
{
    RLVK.State.wireMode = false;
    RLVK.State.stateGeneration++;
}
// Set the line drawing width
void rlSetLineWidth(f32 w) { RLVK.State.lineWidth = w; }
// Get the line drawing width
f32 rlGetLineWidth(void) { return RLVK.State.lineWidth; }
// Enable line aliasing
void rlEnableSmoothLines(void) { RLVK.State.smoothLines = true; }
// Disable line aliasing
void rlDisableSmoothLines(void) { RLVK.State.smoothLines = false; }
// Enable stereo rendering
void rlEnableStereoRender(void) { RLVK.State.stereoRender = true; }
// Disable stereo rendering
void rlDisableStereoRender(void) { RLVK.State.stereoRender = false; }
// Check if stereo render is enabled
bool rlIsStereoRenderEnabled(void) { return RLVK.State.stereoRender; }

// Clear color buffer with color
void rlClearColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    RLVK.State.clearR = r;
    RLVK.State.clearG = g;
    RLVK.State.clearB = b;
    RLVK.State.clearA = a;
}

// Clear used screen buffers (color and depth)
void rlClearScreenBuffers(void)
{
    // Frame not open yet: the scope's loadOp CLEAR (with State.clear*) handles it lazily.
    // Inside an active scope (e.g. ClearBackground inside BeginTextureMode): clear explicitly.
    if (!RLVK.frameActive)
        return;
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
    if (hasColor)
        for (u32 c = 0; c < RLVK.scope.colorCount && c < 8; c++)
            clears[clearCount++] = (VkClearAttachment){
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .colorAttachment = c,
                .clearValue = {.color = {.float32 = {
                                             RLVK.State.clearR / 255.0f, RLVK.State.clearG / 255.0f,
                                             RLVK.State.clearB / 255.0f, RLVK.State.clearA / 255.0f}}}};
    if (hasDepth)
        clears[clearCount++] = (VkClearAttachment){
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .clearValue = {.depthStencil = {1.0f, 0}}};
    if (clearCount)
        vkCmdClearAttachments(cmdBuffer, clearCount, clears,
                              1, &(VkClearRect){{{0, 0}, {RLVK.scope.width, RLVK.scope.height}}, 0, 1});
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
        rlDrawRenderBatch(RLVK.currentBatch); // flush geometry drawn with the previous mode
        RLVK.State.blendMode = mode;
        RLVK.State.customBlendModified = false;
    }
}
// Set blending mode factor and equation
void rlSetBlendFactors(int srcF, int dstF, int eq)
{
    RLVK.State.stateGeneration++;
    RLVK.State.blendSrc = srcF;
    RLVK.State.blendDst = dstF;
    RLVK.State.blendEq = eq;
    RLVK.State.customBlendModified = true;
}
// Set blending mode factor and equation separately for RGB and alpha
void rlSetBlendFactorsSeparate(int srcRGB, int dstRGB, int srcA, int dstA, int eqRGB, int eqA)
{
    RLVK.State.stateGeneration++;
    RLVK.State.blendSrcRGB = srcRGB;
    RLVK.State.blendDstRGB = dstRGB;
    RLVK.State.blendSrcA = srcA;
    RLVK.State.blendDstA = dstA;
    RLVK.State.blendEqRGB = eqRGB;
    RLVK.State.blendEqA = eqA;
    RLVK.State.customBlendModified = true;
}
