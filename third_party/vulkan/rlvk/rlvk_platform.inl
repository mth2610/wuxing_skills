//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Platform-layer hooks (surface attach, present, swapchain recreate)
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

//----------------------------------------------------------------------------------
// Platform-layer hook (declared in rlvk.h, not rlgl.h).
//----------------------------------------------------------------------------------
VkInstance rlvkGetInstance(void) { return RLVK.instance; }

// Set the MSAA sample count, must be called before rlvkAttachSurface()
void rlvkSetMsaaSamples(int samples)
{
    if (RLVK.swapchain)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: rlvkSetMsaaSamples must be called before the swapchain exists");
        return;
    }
    RLVK.msaaSamples = (samples >= 4) ? 4 : 1;
    if (RLVK.msaaSamples > 1)
        TRACELOG(RL_LOG_INFO, "RLVK: MSAA x%d enabled (matches GL FLAG_MSAA_4X_HINT)", RLVK.msaaSamples);
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
        RLVK.depthView[i] = VK_NULL_HANDLE;
        RLVK.depthImage[i] = VK_NULL_HANDLE;
        RLVK.depthMemory[i] = VK_NULL_HANDLE;
        RLVK.interView[i] = VK_NULL_HANDLE;
        RLVK.interImage[i] = VK_NULL_HANDLE;
        RLVK.interMemory[i] = VK_NULL_HANDLE;
        RLVK.msaaView[i] = VK_NULL_HANDLE;
        RLVK.msaaImage[i] = VK_NULL_HANDLE;
        RLVK.msaaMemory[i] = VK_NULL_HANDLE;
    }
    for (int i = 0; i < RLVK_MAX_SWAPCHAIN_IMAGES; i++)
    {
        if (RLVK.swapchainViews[i])
            vkDestroyImageView(RLVK.device, RLVK.swapchainViews[i], RLVK_ALLOC);
        if (RLVK.renderSemaphores[i])
            vkDestroySemaphore(RLVK.device, RLVK.renderSemaphores[i], RLVK_ALLOC);
        RLVK.swapchainViews[i] = VK_NULL_HANDLE;
        RLVK.renderSemaphores[i] = VK_NULL_HANDLE;
        RLVK.swapchainImages[i] = VK_NULL_HANDLE;
    }
    if (RLVK.swapchain)
        vkDestroySwapchainKHR(RLVK.device, RLVK.swapchain, RLVK_ALLOC);
    RLVK.swapchain = VK_NULL_HANDLE;
    RLVK.swapchainImageCount = 0;
}

// Tear down the swapchain and the surface itself (Android APP_CMD_TERM_WINDOW: the
// ANativeWindow — and any VkSurfaceKHR built on it — becomes invalid the instant the
// callback returns, so this MUST run synchronously inside that callback, before returning).
// Safe to call with no surface attached (no-op). A later rlvkAttachSurface(newSurface) on
// resume (APP_CMD_INIT_WINDOW with a fresh ANativeWindow) rebuilds the swapchain from
// scratch — rlvkAttachSurface is already re-entrant for exactly this reason.
void rlvkDetachSurface(void)
{
    if (!isGpuReady || (RLVK.surface == VK_NULL_HANDLE))
        return;
    vkDeviceWaitIdle(RLVK.device); // nothing may still reference the surface/swapchain/images below
    rlvkDestroySwapchainSizedObjects();
    vkDestroySurfaceKHR(RLVK.instance, RLVK.surface, RLVK_ALLOC);
    RLVK.surface = VK_NULL_HANDLE;
    // Fail visibly-safe: a frame caught mid-recording by APP_CMD_TERM_WINDOW has nowhere to
    // present into. rlvkBeginFrame/rlvkPresent already no-op on !RLVK.swapchain, but clearing
    // frameActive too means the NEXT rlvkBeginFrame starts clean rather than thinking a frame
    // is still open.
    RLVK.frameActive = false;
    TRACELOG(RL_LOG_INFO, "RLVK: surface detached");
}

// Recreate the swapchain after OUT_OF_DATE/SUBOPTIMAL (window resize, Android rotate/resume).
// Full device drain + rebuild: resize is rare, simplicity beats oldSwapchain retirement.
static void rlvkRecreateSwapchain(void)
{
    if (!isGpuReady || (RLVK.surface == VK_NULL_HANDLE))
        return;
    vkDeviceWaitIdle(RLVK.device);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RLVK.physicalDevice, RLVK.surface, &caps);

    // VÁ LỖI LOGIC: Khi window bị minimize (extent = 0x0), bắt buộc phải dọn dẹp
    // và gán swapchain = NULL để rlvkBeginFrame có thể rẽ nhánh bỏ qua (skip frame)
    // một cách an toàn thay vì lặp vô tận.
    if ((caps.currentExtent.width == 0) || (caps.currentExtent.height == 0))
    {
        rlvkDestroySwapchainSizedObjects();
        return;
    }

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
    if (!isGpuReady)
        return;

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
        extent.width = (u32)RLVK.State.framebufferWidth;
        extent.height = (u32)RLVK.State.framebufferHeight;
    }
    if (extent.width < caps.minImageExtent.width)
        extent.width = caps.minImageExtent.width;
    if (extent.height < caps.minImageExtent.height)
        extent.height = caps.minImageExtent.height;

    // TỐI ƯU HIỆU NĂNG: Yêu cầu minImageCount + 1 để kích hoạt Mailbox/Triple Buffering
    // Giúp GPU không bị nghẽn (stall) trong lúc chờ Display Engine trình xuất.
    u32 imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;
    if (imageCount > RLVK_MAX_SWAPCHAIN_IMAGES)
        imageCount = RLVK_MAX_SWAPCHAIN_IMAGES;

    // Pick a UNORM (non-sRGB) BGRA format so stored bytes match the GL default framebuffer
    u32 fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(RLVK.physicalDevice, surface, &fmtCount, NULL);
    VkSurfaceFormatKHR *fmts = (VkSurfaceFormatKHR *)RL_MALLOC(fmtCount * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(RLVK.physicalDevice, surface, &fmtCount, fmts);
    VkSurfaceFormatKHR chosen = fmts[0];
    for (u32 i = 0; i < fmtCount; i++)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM)
        {
            chosen = fmts[i];
            break;
        }
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
        if (pmCount > 16)
            pmCount = 16;
        vkGetPhysicalDeviceSurfacePresentModesKHR(RLVK.physicalDevice, surface, &pmCount, pms);
        bool hasImmediate = false, hasMailbox = false;
        for (u32 i = 0; i < pmCount; i++)
        {
            if (pms[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
                hasImmediate = true;
            if (pms[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                hasMailbox = true;
        }
        if (hasImmediate)
            presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        else if (hasMailbox)
            presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
#endif

    // §7.21 real pre-rotation (matching preTransform to currentTransform + compensating in
    // rlSetMatrixProjection) was tried and REVERTED on 2026-07-18: on-device it stopped the
    // swapchain-recreate spam as intended, but broke visual orientation (mirrored/rotated UI
    // text), and did NOT fix the separate UI/2D-overlay-vanishing bug it was meant to address -
    // the human confirmed that bug pre-dates this change and is unrelated to rotation. Back to
    // forcing IDENTITY (the known-good, visually-correct state from §7.12's original fix). See
    // RLVK_HANDOFF.md §7.21 for the full writeup of what was tried and why it was reverted.
    VkSurfaceTransformFlagBitsKHR preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    RLVK.preRotationQuarterTurns = 0;

    RLVK_CHECK(vkCreateSwapchainKHR(RLVK.device,
                                    &(VkSwapchainCreateInfoKHR){
                                        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                        .surface = surface,
                                        .minImageCount = imageCount,
                                        .imageFormat = chosen.format,
                                        .imageColorSpace = chosen.colorSpace,
                                        .imageExtent = extent,
                                        .imageArrayLayers = 1,
                                        // TRANSFER_DST: the unmirrored intermediate image is flip-blitted INTO the
                                        // swapchain image; TRANSFER_SRC: rlReadScreenPixels copies out of it
                                        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                        .preTransform = preTransform,
                                        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                        .presentMode = presentMode,
                                        .clipped = VK_TRUE,
                                    },
                                    RLVK_ALLOC, &RLVK.swapchain));

    vkGetSwapchainImagesKHR(RLVK.device, RLVK.swapchain, &RLVK.swapchainImageCount, NULL);
    if (RLVK.swapchainImageCount > RLVK_MAX_SWAPCHAIN_IMAGES)
        RLVK.swapchainImageCount = RLVK_MAX_SWAPCHAIN_IMAGES;
    vkGetSwapchainImagesKHR(RLVK.device, RLVK.swapchain, &RLVK.swapchainImageCount, RLVK.swapchainImages);

    for (u32 i = 0; i < RLVK.swapchainImageCount; i++)
    {
        RLVK_CHECK(vkCreateImageView(RLVK.device,
                                     &(VkImageViewCreateInfo){
                                         VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                         .image = RLVK.swapchainImages[i],
                                         .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                         .format = chosen.format,
                                         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                     },
                                     RLVK_ALLOC, &RLVK.swapchainViews[i]));

        RLVK_CHECK(vkCreateSemaphore(RLVK.device,
                                     &(VkSemaphoreCreateInfo){VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO},
                                     RLVK_ALLOC, &RLVK.renderSemaphores[i]));
    }

    // Once-only frame pacing objects (size-independent): survive swapchain recreation
    if (RLVK.frameFences[0] == VK_NULL_HANDLE)
    {
        for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
        {
            RLVK_CHECK(vkCreateSemaphore(RLVK.device,
                                         &(VkSemaphoreCreateInfo){VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO},
                                         RLVK_ALLOC, &RLVK.acquireSemaphores[i]));
            RLVK_CHECK(vkCreateFence(RLVK.device,
                                     &(VkFenceCreateInfo){VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT},
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
                                     .imageType = VK_IMAGE_TYPE_2D,
                                     .format = RLVK.depthFormat,
                                     .extent = {extent.width, extent.height, 1},
                                     .mipLevels = 1,
                                     .arrayLayers = 1,
                                     .samples = (RLVK.msaaSamples > 1) ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
                                     .tiling = VK_IMAGE_TILING_OPTIMAL,
                                     .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                     .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                 },
                                 RLVK_ALLOC, &RLVK.depthImage[i]));

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(RLVK.device, RLVK.depthImage[i], &memReq);
        RLVK.depthMemory[i] = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        RLVK_CHECK(vkBindImageMemory(RLVK.device, RLVK.depthImage[i], RLVK.depthMemory[i], 0));

        RLVK_CHECK(vkCreateImageView(RLVK.device,
                                     &(VkImageViewCreateInfo){
                                         VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                         .image = RLVK.depthImage[i],
                                         .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                         .format = RLVK.depthFormat,
                                         .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
                                     },
                                     RLVK_ALLOC, &RLVK.depthView[i]));
    }

    // 1x UNMIRRORED color targets (per frame-in-flight)
    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        RLVK_CHECK(vkCreateImage(RLVK.device,
                                 &(VkImageCreateInfo){
                                     VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                     .imageType = VK_IMAGE_TYPE_2D,
                                     .format = RLVK.swapchainFormat,
                                     .extent = {extent.width, extent.height, 1},
                                     .mipLevels = 1,
                                     .arrayLayers = 1,
                                     .samples = VK_SAMPLE_COUNT_1_BIT,
                                     .tiling = VK_IMAGE_TILING_OPTIMAL,
                                     .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                     .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                 },
                                 RLVK_ALLOC, &RLVK.interImage[i]));
        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(RLVK.device, RLVK.interImage[i], &memReq);
        RLVK.interMemory[i] = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        RLVK_CHECK(vkBindImageMemory(RLVK.device, RLVK.interImage[i], RLVK.interMemory[i], 0));
        RLVK_CHECK(vkCreateImageView(RLVK.device,
                                     &(VkImageViewCreateInfo){
                                         VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                         .image = RLVK.interImage[i],
                                         .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                         .format = RLVK.swapchainFormat,
                                         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                     },
                                     RLVK_ALLOC, &RLVK.interView[i]));
    }

    // 4x MSAA color targets
    if (RLVK.msaaSamples > 1)
    {
        for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
        {
            RLVK_CHECK(vkCreateImage(RLVK.device,
                                     &(VkImageCreateInfo){
                                         VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                         .imageType = VK_IMAGE_TYPE_2D,
                                         .format = RLVK.swapchainFormat,
                                         .extent = {extent.width, extent.height, 1},
                                         .mipLevels = 1,
                                         .arrayLayers = 1,
                                         .samples = VK_SAMPLE_COUNT_4_BIT,
                                         .tiling = VK_IMAGE_TILING_OPTIMAL,
                                         .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                     },
                                     RLVK_ALLOC, &RLVK.msaaImage[i]));
            VkMemoryRequirements memReq;
            vkGetImageMemoryRequirements(RLVK.device, RLVK.msaaImage[i], &memReq);
            RLVK.msaaMemory[i] = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            RLVK_CHECK(vkBindImageMemory(RLVK.device, RLVK.msaaImage[i], RLVK.msaaMemory[i], 0));
            RLVK_CHECK(vkCreateImageView(RLVK.device,
                                         &(VkImageViewCreateInfo){
                                             VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                             .image = RLVK.msaaImage[i],
                                             .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                             .format = RLVK.swapchainFormat,
                                             .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                         },
                                         RLVK_ALLOC, &RLVK.msaaView[i]));
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
    if (view)
        rlvkEvictFramebuffersForView(view);

    // Nothing recorded yet: no command buffer can reference these, destroy immediately
    if ((RLVK.frameCounter == 0) && !RLVK.frameActive)
    {
        if (buffer)
            vkDestroyBuffer(RLVK.device, buffer, RLVK_ALLOC);
        if (view)
            vkDestroyImageView(RLVK.device, view, RLVK_ALLOC);
        if (image)
            vkDestroyImage(RLVK.device, image, RLVK_ALLOC);
        if (sampler)
            vkDestroySampler(RLVK.device, sampler, RLVK_ALLOC);
        if (memory)
            vkFreeMemory(RLVK.device, memory, RLVK_ALLOC);
        if (pipeline)
            vkDestroyPipeline(RLVK.device, pipeline, RLVK_ALLOC);
        return;
    }

    // Queue on the most recent recording's frame slot (its fence covers the referencing work)
    u32 frameIndex = (u32)((RLVK.frameActive ? RLVK.frameCounter : (RLVK.frameCounter + RLVK_FRAME_INDEX_COUNT - 1)) % RLVK_FRAME_INDEX_COUNT);
    if (RLVK.deadResourceCount[frameIndex] >= RLVK_MAX_DEAD_RESOURCES)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: deferred-destruction queue full, leaking objects - raise RLVK_MAX_DEAD_RESOURCES");
        return;
    }
    RLVK.deadResources[frameIndex][RLVK.deadResourceCount[frameIndex]++] = (rlvkDeadResource){buffer, image, view, sampler, memory, pipeline, VK_NULL_HANDLE};
}

// Queue a cached framebuffer for fence-gated destruction (evicted when one of its views dies)
static void rlvkDeferDestroyFramebufferOnly(VkFramebuffer framebuffer)
{
    if (framebuffer == VK_NULL_HANDLE)
        return;
    if ((RLVK.frameCounter == 0) && !RLVK.frameActive)
    {
        vkDestroyFramebuffer(RLVK.device, framebuffer, RLVK_ALLOC);
        return;
    }
    u32 frameIndex = (u32)((RLVK.frameActive ? RLVK.frameCounter : (RLVK.frameCounter + RLVK_FRAME_INDEX_COUNT - 1)) % RLVK_FRAME_INDEX_COUNT);
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
    if (RLVK.frameActive || !isGpuReady || !RLVK.swapchain)
        return;

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    vkWaitForFences(RLVK.device, 1, &RLVK.frameFences[frameIndex], VK_TRUE, UINT64_MAX);

    // This slot's previous submission has fully executed: destroy its deferred objects
    for (int d = 0; d < RLVK.deadResourceCount[frameIndex]; d++)
    {
        rlvkDeadResource *r = &RLVK.deadResources[frameIndex][d];
        if (r->framebuffer)
            vkDestroyFramebuffer(RLVK.device, r->framebuffer, RLVK_ALLOC);
        if (r->buffer)
            vkDestroyBuffer(RLVK.device, r->buffer, RLVK_ALLOC);
        if (r->view)
            vkDestroyImageView(RLVK.device, r->view, RLVK_ALLOC);
        if (r->image)
            vkDestroyImage(RLVK.device, r->image, RLVK_ALLOC);
        if (r->sampler)
            vkDestroySampler(RLVK.device, r->sampler, RLVK_ALLOC);
        if (r->memory)
            vkFreeMemory(RLVK.device, r->memory, RLVK_ALLOC);
        if (r->pipeline)
            vkDestroyPipeline(RLVK.device, r->pipeline, RLVK_ALLOC);
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
        if (RLVK.swapchain == VK_NULL_HANDLE)
            return; // still minimized (0x0): skip the frame
        acq = vk.AcquireNextImageKHR(RLVK.device, RLVK.swapchain, UINT64_MAX,
                                     RLVK.acquireSemaphores[frameIndex], VK_NULL_HANDLE, &imageIndex);
    }
    // Any non-success acquire (device lost, surface lost, NOT_READY, ...) leaves imageIndex
    // untouched and the acquire semaphore unsignaled. Falling through would open the frame on
    // image 0 (never acquired) and submit a wait on a semaphore nothing can ever signal - the
    // GPU then stalls until the driver kills it (VK_TIMEOUT -> device lost), and the frame ring
    // desyncs behind it (fences/command buffers reused while still pending). Skip the frame:
    // frameActive stays false, so the counter does not advance and the next frame retries.
    // SUBOPTIMAL still acquired a real image and signalled - it is safe to render.
    if ((acq != VK_SUCCESS) && (acq != VK_SUBOPTIMAL_KHR))
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: vkAcquireNextImageKHR failed (VkResult %i), skipping frame", (int)acq);
        return;
    }
    RLVK.currentImageIndex = imageIndex;
    RLVK.acquireWaited = false;

    vkResetFences(RLVK.device, 1, &RLVK.frameFences[frameIndex]);

    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];
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
    // Pool-ring fallback: this slot's fence has signaled, its snapshot sets are reusable
    memset(RLVK.shadowUbo, 0, sizeof(RLVK.shadowUbo));
    RLVK.set0Dirty = true;
    if (!RLVK.Caps.pushDescriptor && RLVK.descPools[frameIndex])
    {
        vkResetDescriptorPool(RLVK.device, RLVK.descPools[frameIndex], 0);
        RLVK.set0CacheCount[frameIndex] = 0; // pool reset freed every cached snapshot set
        RLVK.boundSet0 = VK_NULL_HANDLE;
    }
    /* BEFORE the reset, not after: a pending batch's command buffer still
     * references descriptor sets allocated from this pool, and resetting it
     * would free them out from under work that has not been submitted yet. */
    rlvkComputeBatchFlush();
    if (RLVK.computeDescPools[frameIndex])
        vkResetDescriptorPool(RLVK.device, RLVK.computeDescPools[frameIndex], 0);

    RLVK.arenaOffset[frameIndex] = 0; // reset this frame's bump arena (the frame fence gates reuse)
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
            VkPhysicalDeviceProperties pdp;
            vkGetPhysicalDeviceProperties(RLVK.physicalDevice, &pdp);
            s_gpuPeriod = pdp.limits.timestampPeriod;
            vkCreateQueryPool(RLVK.device, &(VkQueryPoolCreateInfo){
                                               VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                               .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                               .queryCount = 3 * RLVK_FRAME_INDEX_COUNT,
                                           },
                              RLVK_ALLOC, &s_gpuPool);
        }
        else if (RLVK.frameCounter >= RLVK_FRAME_INDEX_COUNT)
        {
            u64 q[3] = {0};
            if (vkGetQueryPoolResults(RLVK.device, s_gpuPool, frameIndex * 3, 3, sizeof(q), q,
                                      sizeof(u64), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            {
                s_gpuScene += (f64)(q[1] - q[0]) * s_gpuPeriod * 1e-6; // ms
                s_gpuPresent += (f64)(q[2] - q[1]) * s_gpuPeriod * 1e-6;
                if (((++s_gpuFrames) & 511) == 0)
                    TRACELOG(RL_LOG_WARNING, "VKGPU frames=%d scene=%.3fms present=%.3fms (avg)",
                             s_gpuFrames, s_gpuScene / s_gpuFrames, s_gpuPresent / s_gpuFrames);
            }
        }
    }
    // Grow this frame's bump arena when its last use ran out (mid-frame drains recorded the
    // demanded size): 168 bytes per batch element, 2x headroom so growth converges in one step
    if (RLVK.arenaWanted[frameIndex] > RLVK.arena[frameIndex].sizeBytes)
    {
        rlvkBatchBackingBuffer *grown = &RLVK.arena[frameIndex];
        int elems = (int)(RLVK.arenaWanted[frameIndex] * 5 / 4 / 168) + 64; // 25% headroom over measured demand
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
                                              .srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
                                              .srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT,
                                              .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                              .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                          },
                                      });

    // The frame's render targets start UNDEFINED: the 1x unmirrored intermediate (also the
    // resolve destination under MSAA) and, when MSAA is on, the multisampled color target
    {
        VkImageMemoryBarrier2 targetBarriers[2];
        u32 targetCount = 0;
        targetBarriers[targetCount++] = (VkImageMemoryBarrier2){
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = RLVK.interImage[frameIndex],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        if (RLVK.msaaSamples > 1)
            targetBarriers[targetCount++] = (VkImageMemoryBarrier2){
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .image = RLVK.msaaImage[frameIndex],
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
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
                                              {
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  // srcStage matches the acquire-semaphore wait stage so the transition happens-after acquire
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                                  .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                  .image = RLVK.swapchainImages[imageIndex],
                                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                              },
                                              {
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                                  .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                  .image = RLVK.depthImage[frameIndex],
                                                  .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
                                              },
                                          },
                                      });

    bool msaa = (RLVK.msaaSamples > 1);
    {
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
        rpKey.colorLoad = VK_ATTACHMENT_LOAD_OP_CLEAR;
        rpKey.depthLoad = VK_ATTACHMENT_LOAD_OP_CLEAR;
        rpKey.depthStore = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        rpKey.hasResolve = msaa ? 1 : 0;
        rlvkBeginScopeRenderPass(cmdBuffer, &rpKey, scopeViews, scopeViewCount,
                                 RLVK.swapchainExtent.width, RLVK.swapchainExtent.height,
                                 &(VkClearValue){.color = {.float32 = {
                                                               RLVK.State.clearR / 255.0f, RLVK.State.clearG / 255.0f,
                                                               RLVK.State.clearB / 255.0f, RLVK.State.clearA / 255.0f}}},
                                 &(VkClearValue){.depthStencil = {1.0f, 0}});
    }
    RLVK.scope.fbSlot = 0;
    RLVK.scope.width = RLVK.swapchainExtent.width;
    RLVK.scope.height = RLVK.swapchainExtent.height;
    RLVK.scope.colorCount = 1;
    RLVK.scope.colorFormats[0] = RLVK.swapchainFormat;
    RLVK.scope.samples = (u32)RLVK.msaaSamples;
    RLVK.scope.flipY = false; // UNMIRRORED: swapchain-scope rendering matches GL memory orientation

    // GPU trace: frame-start stamp (query slots were harvested above, safe to reuse)
    if (rlvkDebugFlag("RLVK_GPU_TRACE", &s_dbgGpu) && (s_gpuPool != VK_NULL_HANDLE))
    {
        vkCmdResetQueryPool(cmdBuffer, s_gpuPool, frameIndex * 3, 3);
        vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, s_gpuPool, frameIndex * 3 + 0);
    }
    RLVK.frameActive = true;
}

// Close the render scope, transition to PRESENT_SRC, submit and present. Called from the platform's
// SwapScreenBuffer. If nothing drew this frame, still present a cleared frame.
void rlvkPresent(void)
{
    if (rlvkDebugFlag("RLVK_DEBUG_FBO", &s_dbgFbo))
        TRACELOG(RL_LOG_WARNING, "VKDBG present FA=%d consumed=%d fc=%llu", (int)RLVK.frameActive, (int)RLVK.frameConsumed, (ull)RLVK.frameCounter);
    if (!isGpuReady || !RLVK.swapchain)
        return;
    if (RLVK.frameConsumed)
    {
        RLVK.frameConsumed = false;
        RLVK.frameCounter++;
        return;
    } // rlReadScreenPixels already presented
    if (rlvkDebugFlag("RLVK_MEM_REPORT", &s_dbgMem) && ((RLVK.frameCounter & 2047) == 2047))
        TRACELOG(RL_LOG_WARNING, "VKMEM local=%lldKB host=%lldKB allocs=%d vboCreate=%d vboReuse=%d", s_memLocalBytes / 1024, s_memHostBytes / 1024, s_memAllocCount, s_vboCreateCount, s_vboReuseCount);
    if (!RLVK.frameActive)
        rlvkBeginFrame();
    if (!RLVK.frameActive)
        return; // acquire failed (e.g. out-of-date)

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    u32 imageIndex = RLVK.currentImageIndex;
    VkCommandBuffer cmdBuffer = RLVK.cmdBuffers[frameIndex];

    vkCmdEndRenderPass(cmdBuffer);
    rlvkFinishSwapchainImage(cmdBuffer); // flip-blit the frame into the swapchain

    // COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC
    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                          .imageMemoryBarrierCount = 1,
                                          .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                                              VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                              .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                              .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                              .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                                              .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                              .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                              .image = RLVK.swapchainImages[imageIndex],
                                              .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                          },
                                      });

    vk.EndCommandBuffer(cmdBuffer);

    vk.QueueSubmit2(RLVK.graphicsQueue, 1, &(VkSubmitInfo2){
                                               VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                               .waitSemaphoreInfoCount = RLVK.acquireWaited ? 0u : 1u, // a mid-frame flush may have consumed it
                                               .pWaitSemaphoreInfos = &(VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = RLVK.acquireSemaphores[frameIndex], .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                                               .commandBufferInfoCount = 1,
                                               .pCommandBufferInfos = &(VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmdBuffer},
                                               .signalSemaphoreInfoCount = 1,
                                               .pSignalSemaphoreInfos = &(VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = RLVK.renderSemaphores[imageIndex],
                                                                                                 // Signal after all work (incl. the ->PRESENT_SRC transition) so the present waits for it
                                                                                                 .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT},
                                           },
                    RLVK.frameFences[frameIndex]);

    VkResult pres = vk.QueuePresentKHR(RLVK.graphicsQueue, &(VkPresentInfoKHR){
                                                               VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                                               .waitSemaphoreCount = 1,
                                                               .pWaitSemaphores = &RLVK.renderSemaphores[imageIndex],
                                                               .swapchainCount = 1,
                                                               .pSwapchains = &RLVK.swapchain,
                                                               .pImageIndices = &imageIndex,
                                                           });

    RLVK.frameActive = false;
    RLVK.frameCounter++;

    // The frame's work is already submitted (its fence gates the drain inside the rebuild);
    // rebuild now so the NEXT acquire starts from a valid swapchain.
    //
    // Recreate ONLY on OUT_OF_DATE, never on SUBOPTIMAL. SUBOPTIMAL means "presented fine, but
    // not ideal for the current surface transform" - and on this project's target (Samsung A33,
    // Mali-G68, portrait-native panel locked to landscape) the driver reports SUBOPTIMAL on
    // EVERY present, because we deliberately request preTransform=IDENTITY (which composits with
    // correct on-screen orientation here) while the surface's currentTransform is ROTATE_90.
    // That mismatch is permanent: recreating the swapchain produces the identical IDENTITY
    // swapchain and the identical SUBOPTIMAL next frame, so recreating-on-SUBOPTIMAL is an
    // every-frame infinite rebuild loop. On-device that loop thrashed Android's gralloc
    // allocator (per-frame GraphicBufferAllocator failures) and stalled in-game frames so the
    // 2D UI never rendered (the whole "HUD vanishes on in-game screens" bug, 2026-07-18). The
    // acquire path (above) already treats SUBOPTIMAL as safe-to-render; the present path must be
    // just as tolerant. A genuine surface invalidation (resize/resume/real rotation) still
    // surfaces as OUT_OF_DATE on the next acquire, so nothing that actually needs a rebuild is
    // missed. See RLVK_HANDOFF.md §7.22.
    if (pres == VK_ERROR_OUT_OF_DATE_KHR)
        rlvkRecreateSwapchain();
}
