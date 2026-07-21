//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Vulkan init + frame lifecycle, push-descriptor fallback
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

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
        VkExtensionProperties *props = (VkExtensionProperties *)RL_MALLOC(propCount * sizeof(VkExtensionProperties));
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
                       .pSettings = &(VkLayerSettingEXT){
                           .pLayerName = "VK_LAYER_KHRONOS_validation",
                           .pSettingName = "message_id_filter",
                           .type = VK_LAYER_SETTING_TYPE_STRING_EXT,
                           .valueCount = RLVK_COUNTOF(messageIdFilter),
                           .pValues = messageIdFilter,
                       },
                   },
                   .flags = instanceFlags,
                   .pApplicationInfo = &(VkApplicationInfo){
                       VK_STRUCTURE_TYPE_APPLICATION_INFO,
                       .pApplicationName = "raylib",
                       .apiVersion = apiVersion,
                   },
                   .enabledExtensionCount = instanceExtensionCount,
                   .ppEnabledExtensionNames = instanceExtensions,
               },
               RLVK_ALLOC, &RLVK.instance) == VK_SUCCESS;
}

// Pick the physical device: best-scoring GPU supporting Vulkan 1.1+ (discrete first, newer API as tiebreak)
static bool rlvkPickPhysicalDevice(void)
{
    u32 count = 0;
    vkEnumeratePhysicalDevices(RLVK.instance, &count, NULL);
    if (count == 0)
        return false;

    VkPhysicalDevice *devs = (VkPhysicalDevice *)RL_MALLOC(count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(RLVK.instance, &count, devs);

    VkPhysicalDevice best = VK_NULL_HANDLE;

    // Optional override for cross-device/driver comparison (the same build can target a specific
    // GPU): RLVK_DEVICE_INDEX = enumeration index, or RLVK_DEVICE_NAME = case-sensitive substring
    // of the device name (e.g. "Intel", "RTX"). Falls back to automatic scoring when unset/unmatched.
    const char *envIdx = getenv("RLVK_DEVICE_INDEX");
    const char *envName = getenv("RLVK_DEVICE_NAME");
    if (envIdx != NULL)
    {
        int idx = atoi(envIdx);
        if ((idx >= 0) && (idx < (int)count))
            best = devs[idx];
    }
    if ((best == VK_NULL_HANDLE) && (envName != NULL) && (envName[0] != '\0'))
    {
        for (u32 i = 0; i < count; i++)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devs[i], &props);
            if (strstr(props.deviceName, envName) != NULL)
            {
                best = devs[i];
                break;
            }
        }
    }

    int bestScore = -1;
    if (best == VK_NULL_HANDLE)
    {
        for (u32 i = 0; i < count; i++)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devs[i], &props);
            if (props.apiVersion < VK_API_VERSION_1_1)
                continue; // 1.1 is the hard floor

            int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 100 : 50;
            if (props.apiVersion >= VK_API_VERSION_1_3)
                score += 10; // richer fast-path caps as tiebreak
            if (score > bestScore)
            {
                bestScore = score;
                best = devs[i];
            }
        }
    }
    RL_FREE(devs);
    if (!best)
        return false;
    RLVK.physicalDevice = best;

    // Log the selected device (mirrors the GL backend's device info output)
    // VkPhysicalDeviceDriverProperties needs 1.2 / VK_KHR_driver_properties; on a plain 1.1
    // device the struct must not be chained (driverName/driverInfo stay empty strings)
    VkPhysicalDeviceProperties baseProps;
    vkGetPhysicalDeviceProperties(best, &baseProps);
    VkPhysicalDeviceDriverProperties driverProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                          (baseProps.apiVersion >= VK_API_VERSION_1_2) ? &driverProps : NULL};
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
    VkQueueFamilyProperties *qfs = (VkQueueFamilyProperties *)RL_MALLOC(qfCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(best, &qfCount, qfs);
    RLVK.graphicsFamily = UINT32_MAX;
    RLVK.transferFamily = UINT32_MAX;
    for (u32 i = 0; i < qfCount; i++)
    {
        if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && RLVK.graphicsFamily == UINT32_MAX)
            RLVK.graphicsFamily = i;
        if ((qfs[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && !(qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && RLVK.transferFamily == UINT32_MAX)
            RLVK.transferFamily = i;
    }
    if (RLVK.transferFamily == UINT32_MAX)
        RLVK.transferFamily = RLVK.graphicsFamily;
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
        VkExtensionProperties *props = (VkExtensionProperties *)RL_MALLOC(propCount * sizeof(VkExtensionProperties));
        vkEnumerateDeviceExtensionProperties(RLVK.physicalDevice, NULL, &propCount, props);
        for (u32 i = 0; i < propCount; i++)
        {
            const char *n = props[i].extensionName;
            if (strcmp(n, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                hasSwapchain = true;
            else if (strcmp(n, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0)
                hasPushDesc = true;
            else if (strcmp(n, VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME) == 0)
                hasLineRasterEXT = true;
            else if (strcmp(n, VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME) == 0)
                hasLineRasterKHR = true;
            else if (strcmp(n, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME) == 0)
                hasPriority = true;
            else if (strcmp(n, VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME) == 0)
                hasPageable = true;
            else if (strcmp(n, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME) == 0)
                hasGpl = true;
            else if (strcmp(n, VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME) == 0)
                hasPipelineLibrary = true;
            else if (strcmp(n, "VK_KHR_portability_subset") == 0)
                hasPortabilitySubset = true;
        }
        RL_FREE(props);
    }
    if (!hasSwapchain)
    {
        TRACELOG(RL_LOG_FATAL, "RLVK: required device extension not supported: %s", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        return false;
    }

    // Feature query: chain each optional struct only when its extension exists (chaining a
    // struct of an unsupported extension is invalid); 1.3 core features only on 1.3+ devices
    VkPhysicalDeviceLineRasterizationFeaturesEXT qLine = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT};
    VkPhysicalDeviceVulkan13Features q13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 q2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    {
        void **qTail = &q2.pNext;
#define RLVK_CHAIN_IF(_cond, _s) \
    if (_cond)                   \
    {                            \
        *qTail = (void *)&_s;    \
        qTail = &_s.pNext;       \
    }
        RLVK_CHAIN_IF(hasLineRasterEXT || hasLineRasterKHR, qLine);
        RLVK_CHAIN_IF(RLVK.Caps.apiVersion >= VK_API_VERSION_1_3, q13);
#undef RLVK_CHAIN_IF
        vkGetPhysicalDeviceFeatures2(RLVK.physicalDevice, &q2);
    }

    RLVK.Caps.dynamicRendering = (RLVK.Caps.apiVersion >= VK_API_VERSION_1_3) && q13.dynamicRendering;
    RLVK.Caps.synchronization2 = (RLVK.Caps.apiVersion >= VK_API_VERSION_1_3) && q13.synchronization2;
    RLVK.Caps.pushDescriptor = hasPushDesc;
    // Test hook: force the pool-ring snapshot-descriptor fallback (the Mali/no-push-descriptor
    // path) even on a device that has VK_KHR_push_descriptor, so the desktop visual suite can
    // exercise it (§7.23 - no dev machine hits the fallback naturally). Installs the compat shim
    // (§ below), skips enabling the extension, and drops PUSH_DESCRIPTOR_BIT from the set0 layout.
    if (getenv("RLVK_FORCE_POOL_RING")) RLVK.Caps.pushDescriptor = false;
    RLVK.Caps.bresenhamLines = (hasLineRasterEXT || hasLineRasterKHR) && qLine.bresenhamLines;
    RLVK.Caps.wideLines = q2.features.wideLines;
    RLVK.Caps.fillModeNonSolid = q2.features.fillModeNonSolid;
    RLVK.Caps.memoryPriority = hasPriority;
    RLVK.Caps.pageableMemory = (hasPriority && hasPageable);
    RLVK.Caps.graphicsPipelineLibrary = (hasGpl && hasPipelineLibrary);
    RLVK.Caps.graphicsSsboStores = q2.features.vertexPipelineStoresAndAtomics;
    // MoltenVK on Intel GPUs: a depth image created with SAMPLED usage silently stops working
    // as a depth ATTACHMENT (test/write no-op, no validation error). Bisected empirically -
    // scripts/run_rlvk_visual_test.sh depth_rt reproduces. 0x8086 = Intel vendor id.
    {
        VkPhysicalDeviceProperties qprops;
        vkGetPhysicalDeviceProperties(RLVK.physicalDevice, &qprops);
        RLVK.Caps.noSampledDepth = hasPortabilitySubset && (qprops.vendorID == 0x8086);
    }
    if (RLVK.Caps.noSampledDepth)
        TRACELOG(RL_LOG_INFO, "RLVK: quirk noSampledDepth active (MoltenVK/Intel) - FBO depth textures are not sampleable");

    // Enable everything supported (spec: VK_KHR_portability_subset MUST be enabled when present)
    deviceExtensions[deviceExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (hasPortabilitySubset)
        deviceExtensions[deviceExtensionCount++] = "VK_KHR_portability_subset";
    if (RLVK.Caps.pushDescriptor)
        deviceExtensions[deviceExtensionCount++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
    if (RLVK.Caps.bresenhamLines)
        deviceExtensions[deviceExtensionCount++] = hasLineRasterKHR ? VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME : VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME;
    if (RLVK.Caps.memoryPriority)
        deviceExtensions[deviceExtensionCount++] = VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME;
    if (RLVK.Caps.pageableMemory)
        deviceExtensions[deviceExtensionCount++] = VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME;
    if (RLVK.Caps.graphicsPipelineLibrary)
    {
        deviceExtensions[deviceExtensionCount++] = VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME; // required by graphics_pipeline_library
        deviceExtensions[deviceExtensionCount++] = VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME;
    }

    f32 queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queues[RLVK_QUEUE_COUNT] = {
        [RLVK_QUEUE_GRAPHICS] = {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = RLVK.graphicsFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        },
        [RLVK_QUEUE_TRANSFER] = {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = RLVK.transferFamily,
            .queueCount = 1,
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
        .bresenhamLines = VK_TRUE, // match GL's 1px line pixel coverage
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
            // Graphics-stage SSBO writes (optional): without it SSBOs are read-only and
            // rlvkRebaseStorageBuffers injects NonWritable to satisfy VUID-RuntimeSpirv-06341
            .vertexPipelineStoresAndAtomics = RLVK.Caps.graphicsSsboStores ? VK_TRUE : VK_FALSE,
        },
    };
    {
        void **fTail = &features2.pNext;
#define RLVK_CHAIN_IF(_cond, _s) \
    if (_cond)                   \
    {                            \
        *fTail = (void *)&_s;    \
        fTail = &_s.pNext;       \
    }
        RLVK_CHAIN_IF(RLVK.Caps.memoryPriority, memoryPriorityFeatures);
        RLVK_CHAIN_IF(RLVK.Caps.pageableMemory, pageableMemoryFeatures);
        RLVK_CHAIN_IF(RLVK.Caps.graphicsPipelineLibrary, gplFeatures);
        RLVK_CHAIN_IF(RLVK.Caps.bresenhamLines, lineRasterizationFeatures);
        RLVK_CHAIN_IF(RLVK.Caps.dynamicRendering || RLVK.Caps.synchronization2, vulkan13Features);
#undef RLVK_CHAIN_IF
        vulkan13Features.dynamicRendering = RLVK.Caps.dynamicRendering ? VK_TRUE : VK_FALSE;
        vulkan13Features.synchronization2 = RLVK.Caps.synchronization2 ? VK_TRUE : VK_FALSE;
    }

    VkResult _cdResult = vkCreateDevice(RLVK.physicalDevice,
                                        &(VkDeviceCreateInfo){
                                            VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                            &features2,
                                            .queueCreateInfoCount = queueCount,
                                            .pQueueCreateInfos = queues,
                                            .enabledExtensionCount = deviceExtensionCount,
                                            .ppEnabledExtensionNames = deviceExtensions,
                                        },
                                        RLVK_ALLOC, &RLVK.device);
    if (_cdResult != VK_SUCCESS)
    {
        TRACELOG(RL_LOG_FATAL, "RLVK: vkCreateDevice failed VkResult=%d", (int)_cdResult);
        return false;
    }
    vkGetDeviceQueue(RLVK.device, RLVK.graphicsFamily, 0, &RLVK.graphicsQueue);
    vkGetDeviceQueue(RLVK.device, RLVK.transferFamily, 0, &RLVK.transferQueue);
    return true;
}

// Load the device-level entry points into the vk dispatch table
static void rlvkLoadEntrypoints(void)
{
#define RLVK_PFN_FUNC(_func) \
    vk._func = (PFN_vk##_func)vkGetDeviceProcAddr(RLVK.device, "vk" #_func);
    RLVK_PFN_FUNCS
#undef RLVK_PFN_FUNC

    // Vulkan 1.1 fallbacks: install compat shims where the native entry point is absent,
    // so every call site keeps its sync2/push-descriptor shape with zero branching
    if (!RLVK.Caps.synchronization2 || (vk.CmdPipelineBarrier2 == NULL))
        vk.CmdPipelineBarrier2 = rlvkCmdPipelineBarrier2Compat;
    if (!RLVK.Caps.synchronization2 || (vk.QueueSubmit2 == NULL))
        vk.QueueSubmit2 = rlvkQueueSubmit2Compat;
    if (!RLVK.Caps.pushDescriptor || (vk.CmdPushDescriptorSetKHR == NULL))
        vk.CmdPushDescriptorSetKHR = rlvkPushDescriptorSetCompat;

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
            .binding = i,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    bindings[RLVK_UBO_BINDING_VS] = (VkDescriptorSetLayoutBinding){
        .binding = RLVK_UBO_BINDING_VS, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT};
    bindings[RLVK_UBO_BINDING_FS] = (VkDescriptorSetLayoutBinding){
        .binding = RLVK_UBO_BINDING_FS, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    // Graphics-stage SSBOs (GPU-particle draws): GLSL bindings 0..3 rebased here by
    // rlvkRebaseStorageBuffers; fed from the shared rlBindShaderBuffer table at draw time
    for (u32 i = 0; i < RLVK_SET0_SSBO_COUNT; i++)
        bindings[RLVK_SSBO_BINDING_BASE + i] = (VkDescriptorSetLayoutBinding){
            .binding = RLVK_SSBO_BINDING_BASE + i, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT};
    RLVK_CHECK(vkCreateDescriptorSetLayout(RLVK.device,
                                           &(VkDescriptorSetLayoutCreateInfo){
                                               VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                               // Push-descriptor layouts need the flag; the pool-ring fallback (no push
                                               // descriptor support) allocates plain sets from this same layout instead
                                               .flags = RLVK.Caps.pushDescriptor ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0,
                                               .bindingCount = RLVK_SET0_BINDING_COUNT,
                                               .pBindings = bindings,
                                           },
                                           RLVK_ALLOC, &RLVK.set0Layout));
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
    (void)cmdBuffer;
    (void)bindPoint;
    (void)layout;
    (void)set;

    // TỐI ƯU: Chỉ đánh dấu dirty nếu dữ liệu truyền vào thực sự khác biệt
    // Tránh việc vắt kiệt Descriptor Pool khi Raylib đẩy lặp đi lặp lại cùng một texture/UBO
    bool stateChanged = false;

    for (uint32_t i = 0; i < writeCount; i++)
    {
        const VkWriteDescriptorSet *w = &writes[i];
        if (w->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        {
            if (w->dstBinding >= RLVK_MAX_TEXTURE_UNITS)
                continue;

            if (RLVK.pushedView[w->dstBinding] != w->pImageInfo->imageView ||
                RLVK.pushedSampler[w->dstBinding] != w->pImageInfo->sampler)
            {
                RLVK.pushedView[w->dstBinding] = w->pImageInfo->imageView;
                RLVK.pushedSampler[w->dstBinding] = w->pImageInfo->sampler;
                stateChanged = true;
            }
        }
        else if (w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        {
            u32 s = (w->dstBinding == (u32)RLVK_UBO_BINDING_VS) ? 0 : (w->dstBinding == (u32)RLVK_UBO_BINDING_FS) ? 1
                                                                                                                  : 2;

            if (s < 2)
            {
                if (RLVK.shadowUbo[s].buffer != w->pBufferInfo->buffer ||
                    RLVK.shadowUbo[s].offset != w->pBufferInfo->offset ||
                    RLVK.shadowUbo[s].range != w->pBufferInfo->range)
                {
                    RLVK.shadowUbo[s] = *w->pBufferInfo;
                    stateChanged = true;
                }
            }
        }
    }

    if (stateChanged)
        RLVK.set0Dirty = true;
}

// Bind a snapshot of the set-0 shadow before a draw (no-op with native push descriptors)
static void rlvkFlushSet0(VkCommandBuffer cmdBuffer)
{
    if (RLVK.Caps.pushDescriptor || !RLVK.set0Dirty)
        return;
    if (getenv("RLVK_DBG_FLUSHSET0")) { fprintf(stderr, "[FS0] enter fc=%llu\n", (unsigned long long)RLVK.frameCounter); fflush(stderr); }

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);

    // Snapshot the key this flush would produce, then look for an already-allocated+written set
    // for the identical state this frame. A hit skips the vkAllocateDescriptorSets + full
    // vkUpdateDescriptorSets rewrite (the Mali-path hot cost, §8.4b-3) - only the bind remains,
    // and even that is skipped when the same set is still bound.
    rlvkSet0CacheEntry key;
    memcpy(key.view, RLVK.pushedView, sizeof(key.view));
    memcpy(key.sampler, RLVK.pushedSampler, sizeof(key.sampler));
    for (u32 s = 0; s < 2; s++)
    {
        key.uboBuf[s]   = RLVK.shadowUbo[s].buffer;
        key.uboOff[s]   = RLVK.shadowUbo[s].offset;
        key.uboRange[s] = RLVK.shadowUbo[s].range;
    }
    for (u32 i = 0; i < RLVK_SET0_SSBO_COUNT; i++) key.ssboSlot[i] = RLVK.computeSSBO[i];

    static int s_noCache = -1;
    if (s_noCache < 0) s_noCache = getenv("RLVK_NO_SET0CACHE") ? 1 : 0;

    rlvkSet0CacheEntry *cache = RLVK.set0Cache[frameIndex];
    u32 count = s_noCache ? 0 : RLVK.set0CacheCount[frameIndex];
    for (u32 e = 0; e < count; e++)
    {
        rlvkSet0CacheEntry *c = &cache[e];
        if (memcmp(c->view, key.view, sizeof(key.view)) == 0 &&
            memcmp(c->sampler, key.sampler, sizeof(key.sampler)) == 0 &&
            memcmp(c->uboBuf, key.uboBuf, sizeof(key.uboBuf)) == 0 &&
            memcmp(c->uboOff, key.uboOff, sizeof(key.uboOff)) == 0 &&
            memcmp(c->uboRange, key.uboRange, sizeof(key.uboRange)) == 0 &&
            memcmp(c->ssboSlot, key.ssboSlot, sizeof(key.ssboSlot)) == 0)
        {
            if (RLVK.boundSet0 != c->set)
            {
                vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, RLVK.pipelineLayout, 0, 1, &c->set, 0, NULL);
                RLVK.boundSet0 = c->set;
            }
            RLVK.set0Dirty = false;
            return;
        }
    }

    VkDescriptorSet ds = VK_NULL_HANDLE;
    VkResult res = vkAllocateDescriptorSets(RLVK.device,
                                            &(VkDescriptorSetAllocateInfo){
                                                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                .descriptorPool = RLVK.descPools[frameIndex],
                                                .descriptorSetCount = 1,
                                                .pSetLayouts = &RLVK.set0Layout,
                                            },
                                            &ds);
    if (res != VK_SUCCESS)
    {
        // Pool exhausted: keep the previously bound set (stale textures beat a crash)
        TRACELOG(RL_LOG_WARNING, "RLVK: descriptor pool exhausted (VkResult %d) - raise RLVK_DESC_SETS_PER_FRAME", (int)res);
        RLVK.set0Dirty = false;
        return;
    }

    rlvkTextureSlot *def = &RLVK.textureSlots[RLVK.defaultTextureSlot];
    VkDescriptorImageInfo imageInfos[RLVK_MAX_TEXTURE_UNITS];
    VkWriteDescriptorSet writes[RLVK_SET0_BINDING_COUNT];
    u32 writeCount = 0;
    int sparse = getenv("RLVK_EXP_SPARSE_SET") ? 1 : 0;
    for (u32 b = 0; b < RLVK_MAX_TEXTURE_UNITS; b++)
    {
        if (sparse && b != 0 && !RLVK.pushedView[b])
            continue; // EXP: write only binding 0 + actually-used units (Intel BDW 16-sampler crash)
        // Unset shadow entries fall back to the default texture: every binding of the set
        // is valid regardless of which units the current shader statically uses
        imageInfos[b] = (VkDescriptorImageInfo){
            .sampler = RLVK.pushedSampler[b] ? RLVK.pushedSampler[b] : def->sampler,
            .imageView = RLVK.pushedView[b] ? RLVK.pushedView[b] : def->view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        writes[writeCount++] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ds,
            .dstBinding = b,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageInfos[b],
        };
    }
    for (u32 s = 0; s < 2; s++)
    {
        if (RLVK.shadowUbo[s].buffer == VK_NULL_HANDLE)
            continue; // shader without a UBO never binds these
        writes[writeCount++] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ds,
            .dstBinding = s ? (u32)RLVK_UBO_BINDING_FS : (u32)RLVK_UBO_BINDING_VS,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &RLVK.shadowUbo[s],
        };
    }
    // Graphics SSBO bindings mirror the shared bind table; unbound slots fall back to the
    // dummy attrib buffer (created with STORAGE usage) so every binding stays valid
    VkDescriptorBufferInfo ssboInfos[RLVK_SET0_SSBO_COUNT];
    for (u32 i = 0; i < RLVK_SET0_SSBO_COUNT; i++)
    {
        u32 slot = RLVK.computeSSBO[i];
        VkBuffer buf = (slot && slot < RLVK_MAX_BUFFER_SLOTS && RLVK.bufferSlots[slot].buffer)
                           ? RLVK.bufferSlots[slot].buffer
                           : RLVK.bufferSlots[RLVK.dummyAttribSlot].buffer;
        ssboInfos[i] = (VkDescriptorBufferInfo){buf, 0, VK_WHOLE_SIZE};
        writes[writeCount++] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ds,
            .dstBinding = RLVK_SSBO_BINDING_BASE + i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &ssboInfos[i],
        };
    }
    if (getenv("RLVK_DBG_FLUSHSET0")) {
        int nullv = 0; for (u32 b=0;b<RLVK_MAX_TEXTURE_UNITS;b++) if (!imageInfos[b].imageView) nullv++;
        fprintf(stderr, "[FS0] wc=%u ds=%p nullViews=%d defView=%p ubo0=%p ubo1=%p\n", writeCount,(void*)ds,nullv,(void*)def->view,(void*)RLVK.shadowUbo[0].buffer,(void*)RLVK.shadowUbo[1].buffer); fflush(stderr);
    }
    vkUpdateDescriptorSets(RLVK.device, writeCount, writes, 0, NULL);
    if (getenv("RLVK_DBG_FLUSHSET0")) { fprintf(stderr, "[FS0] updated, binding...\n"); fflush(stderr); }
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, RLVK.pipelineLayout, 0, 1, &ds, 0, NULL);
    if (getenv("RLVK_DBG_FLUSHSET0")) { fprintf(stderr, "[FS0] bound ok\n"); fflush(stderr); }
    RLVK.boundSet0 = ds;
    RLVK.set0Dirty = false;

    // Remember this snapshot for reuse by a later draw with the identical key this frame. Above the
    // cap we simply stop caching (still correct - the pool has RLVK_DESC_SETS_PER_FRAME sets and the
    // old allocate-every-flush path takes over); distinct combos per frame stay well under the cap.
    if (count < RLVK_SET0_CACHE_SIZE)
    {
        key.set = ds;
        cache[count] = key;
        RLVK.set0CacheCount[frameIndex] = count + 1;
    }
}

// Resolve the sampleable view+sampler for a texture slot, applying the depth-twin (§7.1) and the
// open-scope-attachment substitution. Shared by rlvkPushTexture and the coalesced set-0 push.
static void rlvkResolveTexBinding(u32 textureSlot, VkImageView *outView, VkSampler *outSampler)
{
    rlvkTextureSlot *t = &RLVK.textureSlots[textureSlot];
    // A non-sampleable depth attachment (Caps.noSampledDepth, §7.1) exposes a sampleable twin
    // filled at FBO scope close; sample the twin's view/layout so soft-particle / depth_copy
    // shaders read real depth instead of the substituted default.
    VkImageView view = t->sampleImage ? t->sampleView : t->view;
    VkImageLayout layout = t->sampleImage ? t->sampleLayout : t->currentLayout;
    VkSampler sampler = t->sampler;
    // An attachment of the open scope can't be sampled (GL feedback loop, undefined there
    // too): substitute the default texture - same "undefined" class, but layout-legal
    if (layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || !view)
    {
        rlvkTextureSlot *d = &RLVK.textureSlots[RLVK.defaultTextureSlot];
        view = d->view;
        sampler = d->sampler;
    }
    *outView = view;
    *outSampler = sampler;
}

// Push one texture at a GL-texture-unit binding of set 0
static void rlvkPushTexture(VkCommandBuffer cmdBuffer, u32 binding, u32 textureSlot)
{
    VkImageView view;
    VkSampler sampler;
    rlvkResolveTexBinding(textureSlot, &view, &sampler);
    // Skip the push when this binding already holds exactly this view+sampler (consecutive
    // batch draws almost always share one texture: font atlas, white texture, one material)
    if ((RLVK.pushedView[binding] == view) && (RLVK.pushedSampler[binding] == sampler))
        return;
    RLVK.pushedView[binding] = view;
    RLVK.pushedSampler[binding] = sampler;
    // Pool-ring fallback (no VK_KHR_push_descriptor, e.g. Mali): the compat shim detects changes
    // by comparing the incoming view against RLVK.pushedView - but we just overwrote pushedView
    // above, so it would see "no change" and never set set0Dirty. Reaching here means the binding
    // DID change (the dedup above returned otherwise), so mark the snapshot set dirty directly.
    // Without this, a second batch draw with a different texture (sprite after background) kept the
    // previous texture bound -> "sprite core not white" / the Android dim-2D + garbled-text bug.
    // Harmless with native push descriptors (rlvkFlushSet0 early-outs on Caps.pushDescriptor).
    RLVK.set0Dirty = true;
    vk.CmdPushDescriptorSetKHR(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, RLVK.pipelineLayout, 0, 1,
                               &(VkWriteDescriptorSet){
                                   VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                   .dstBinding = binding,
                                   .descriptorCount = 1,
                                   .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   .pImageInfo = &(VkDescriptorImageInfo){
                                       .sampler = sampler,
                                       .imageView = view,
                                       .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   },
                               });
}

// Coalesced set-0 push for the immediate-mode BATCH path when the draw's shader uses a UBO.
//
// §7.26 (MoltenVK push-descriptor): when a render pass already holds a prior 3D draw, a draw that
// issues TWO separate CmdPushDescriptorSetKHR calls (a UBO push, then a later binding-0 texture
// push) loses the SECOND call - the fragment shader samples the default white texture. The mesh
// path usually escapes this because its UBO push is cached (no push that draw), leaving the sampler
// push as the only call. The batch ground-shadow receiver pushes a fresh UBO AND its texture0 every
// draw, so its texture was dropped. Fix: build the shader's ENTIRE set-0 (UBO stages + binding-0
// texture + declared samplers + SSBOs) and issue it as ONE CmdPushDescriptorSetKHR after the
// pipeline bind, so no binding depends on a second push surviving. tex0Slot is the draw's texture0.
//
// Works on both descriptor paths: with native push descriptors it is the one atomic set-0 write;
// on the compat pool-ring shim each write updates the CPU shadow and rlvkFlushSet0 binds the set.
static void rlvkPushSet0Batch(VkCommandBuffer cmdBuffer, rlvkShaderSlot *shader, u32 tex0Slot)
{
    VkDescriptorBufferInfo uboInfos[2];
    VkDescriptorImageInfo  imgInfos[RLVK_MAX_TEXTURE_UNITS];
    VkDescriptorBufferInfo ssboInfos[RLVK_SET0_SSBO_COUNT];
    VkWriteDescriptorSet   writes[2 + RLVK_MAX_TEXTURE_UNITS + RLVK_SET0_SSBO_COUNT];

    u32 n = rlvkAppendUboWrites(shader, uboInfos, writes); // 0..2 UBO stage writes
    u32 img = 0;

    // binding 0 = this draw's texture0 (rlSetTexture / drawCall texture), always (re)written so it
    // never rides on a prior push surviving.
    {
        VkImageView v; VkSampler s;
        rlvkResolveTexBinding(tex0Slot, &v, &s);
        if (getenv("RLVK_EXP_DEFAULT_SAMPLER")) s = RLVK.textureSlots[RLVK.defaultTextureSlot].sampler;
        VkImageLayout l0 = getenv("RLVK_EXP_BARRIER_BEFORE") ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfos[img] = (VkDescriptorImageInfo){.sampler = s, .imageView = v, .imageLayout = l0};
        writes[n++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1,
                                             .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &imgInfos[img]};
        img++;
        RLVK.pushedView[0] = v;
        RLVK.pushedSampler[0] = s;
    }

    // Samplers the shader declares at bindings > 0 (resolve exactly like rlvkBindShaderSamplers).
    for (int i = 0; i < shader->uniformCount; i++)
    {
        int b = shader->uniforms[i].samplerBinding;
        if (b <= 0 || b >= RLVK_MAX_TEXTURE_UNITS)
            continue;
        u32 tex = shader->bindingTexture[b];
        if (tex == 0)
        {
            int unit = shader->bindingUnit[b];
            if (unit >= 0 && unit < RLVK_MAX_TEXTURE_UNITS)
                tex = RLVK.State.activeTextureSlots[unit];
        }
        if (tex == 0 || tex >= RLVK_MAX_TEXTURE_SLOTS || !RLVK.textureSlots[tex].view)
            tex = RLVK.defaultTextureSlot;
        VkImageView v; VkSampler s;
        rlvkResolveTexBinding(tex, &v, &s);
        imgInfos[img] = (VkDescriptorImageInfo){.sampler = s, .imageView = v, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[n++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = (u32)b, .descriptorCount = 1,
                                             .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &imgInfos[img]};
        img++;
        RLVK.pushedView[b] = v;
        RLVK.pushedSampler[b] = s;
    }

    // EXP: also (re)write every OTHER texture unit to its current shadow value (a full set-0
    // replacement) - tests whether MoltenVK needs a complete push after a prior sparse binding-0
    // push in the pass rather than a sparse one.
    if (getenv("RLVK_EXP_FULL_TEX"))
    {
        rlvkTextureSlot *def = &RLVK.textureSlots[RLVK.defaultTextureSlot];
        for (u32 b = 1; b < RLVK_MAX_TEXTURE_UNITS; b++)
        {
            bool already = false;
            for (u32 k = 0; k < img; k++) if (writes[(n - img) + k].dstBinding == b) already = true;
            if (already) continue;
            imgInfos[img] = (VkDescriptorImageInfo){
                .sampler = RLVK.pushedSampler[b] ? RLVK.pushedSampler[b] : def->sampler,
                .imageView = RLVK.pushedView[b] ? RLVK.pushedView[b] : def->view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes[n++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = b, .descriptorCount = 1,
                                                 .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &imgInfos[img]};
            img++;
        }
    }

    // Graphics SSBOs the shader reads (GPU-particle-in-batch is rare, but keep set-0 complete).
    for (u32 i = 0; i < RLVK_SET0_SSBO_COUNT; i++)
    {
        if (!(shader->ssboMask & (1u << i)))
            continue;
        u32 slot = RLVK.computeSSBO[i];
        VkBuffer buf = (slot && slot < RLVK_MAX_BUFFER_SLOTS && RLVK.bufferSlots[slot].buffer)
                           ? RLVK.bufferSlots[slot].buffer
                           : RLVK.bufferSlots[RLVK.dummyAttribSlot].buffer;
        ssboInfos[i] = (VkDescriptorBufferInfo){buf, 0, VK_WHOLE_SIZE};
        writes[n++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = RLVK_SSBO_BINDING_BASE + i, .descriptorCount = 1,
                                             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &ssboInfos[i]};
        RLVK.pushedSsbo[i] = slot;
    }

    if (getenv("RLVK_DBG_SET0BATCH")) {
        rlvkTextureSlot *ts = &RLVK.textureSlots[tex0Slot];
        TRACELOG(RL_LOG_WARNING, "SET0BATCH tex0Slot=%u slotView=%p slotImg=%p layout=%d sampleImg=%p pushView=%p defView=%p n=%u",
                 tex0Slot, (void*)ts->view, (void*)ts->image, (int)ts->currentLayout, (void*)ts->sampleImage,
                 (void*)imgInfos[0].imageView, (void*)RLVK.textureSlots[RLVK.defaultTextureSlot].view, n);
    }
    if (getenv("RLVK_EXP_BARRIER_BEFORE")) {
        rlvkTextureSlot *ct = &RLVK.textureSlots[tex0Slot];
        if (ct->image)
        vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                              VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                              .imageMemoryBarrierCount = 1,
                                              .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                  .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                                  .image = ct->image,
                                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                              }});
    }
    // Reaching here means a binding changed for the compat shim's benefit (rlvkFlushSet0 rebinds).
    RLVK.set0Dirty = true;
    vk.CmdPushDescriptorSetKHR(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, RLVK.pipelineLayout, 0, n, writes);
    if (getenv("RLVK_EXP_DOUBLE_PUSH"))
        vk.CmdPushDescriptorSetKHR(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, RLVK.pipelineLayout, 0, n, writes);
    if (getenv("RLVK_EXP_REBIND_PIPE") && RLVK.boundPipeline)
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, RLVK.boundPipeline);
    shader->uboPushedEpoch = RLVK.State.cbEpoch;
    RLVK.lastUboShader = shader;
}

// Initialize the pipeline layout and the embedded default shader
static bool rlvkInitDefaultShader(void)
{
    VkPushConstantRange pcRange = {
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(rlvkPushConstants)};

    RLVK_CHECK(vkCreatePipelineLayout(RLVK.device,
                                      &(VkPipelineLayoutCreateInfo){
                                          VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                          .setLayoutCount = 1,
                                          .pSetLayouts = &RLVK.set0Layout,
                                          .pushConstantRangeCount = 1,
                                          .pPushConstantRanges = &pcRange,
                                      },
                                      RLVK_ALLOC, &RLVK.pipelineLayout));

    u32 slot = rlvkAllocShaderSlot();
    if (slot == RLVK_INVALID_SLOT)
        return false;
    for (int i = 0; i < RL_MAX_SHADER_LOCATIONS; i++)
        RLVK.shaderSlots[slot].locs[i] = -1;

    // VÁ LỖI MEMORY: Ép kiểu tường minh (const uint32_t*) để ngăn lỗi Alignment/Segmentation Fault
    // khi chạy trên các dòng chip ARM (Android, Mac M1/M2) nếu mảng raw được sinh ra dạng unsigned char.
    vkCreateShaderModule(RLVK.device, &(VkShaderModuleCreateInfo){
                                          VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                          .codeSize = sizeof(rlvkDefaultVertSpv),
                                          .pCode = (const uint32_t *)rlvkDefaultVertSpv,
                                      },
                         RLVK_ALLOC, &RLVK.shaderSlots[slot].vertMod);

    vkCreateShaderModule(RLVK.device, &(VkShaderModuleCreateInfo){
                                          VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                          .codeSize = sizeof(rlvkDefaultFragSpv),
                                          .pCode = (const uint32_t *)rlvkDefaultFragSpv,
                                      },
                         RLVK_ALLOC, &RLVK.shaderSlots[slot].fragMod);

    int *L = RLVK.shaderSlots[slot].locs;
    L[SHADER_LOC_VERTEX_POSITION] = RLVK_ALOC_POSITION;
    L[SHADER_LOC_VERTEX_TEXCOORD01] = RLVK_ALOC_TEXCOORD;
    L[SHADER_LOC_VERTEX_NORMAL] = RLVK_ALOC_NORMAL;
    L[SHADER_LOC_VERTEX_COLOR] = RLVK_ALOC_COLOR;
    L[SHADER_LOC_MATRIX_MVP] = RLVK_ULOC_MVP;
    L[SHADER_LOC_COLOR_DIFFUSE] = RLVK_ULOC_COLDIFFUSE;
    L[SHADER_LOC_MAP_DIFFUSE] = RLVK_ULOC_TEXTURE0;

    rlvkShaderSlot *shader = &RLVK.shaderSlots[slot];
    for (int i = 0; i < RLVK_ATTRIB_COUNT; i++)
        shader->attribLocs[i] = -1;
    shader->attribLocs[RLVK_ATTRIB_POSITION] = 0;
    shader->attribLocs[RLVK_ATTRIB_TEXCOORD] = 1;
    shader->attribLocs[RLVK_ATTRIB_COLOR] = 3;
    for (int i = 0; i < RLVK_MAX_TEXTURE_UNITS; i++)
    {
        shader->bindingUnit[i] = i;
        shader->bindingTexture[i] = 0;
    }

    RLVK.defaultShaderSlot = slot;
    return true;
}