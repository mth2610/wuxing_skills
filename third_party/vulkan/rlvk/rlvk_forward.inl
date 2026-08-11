//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Internal (static) function forward declarations
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
static Matrix       rlvkMatrixIdentity (void);                     // Get identity matrix
static Matrix       rlvkMatrixMultiply (Matrix left, Matrix right); // Get two matrix multiplication result
static Matrix       rlvkMatrixTranspose(Matrix mat);               // Get transposed input matrix
static Matrix       rlvkMatrixInvert   (Matrix mat);               // Get inverted input matrix

static int          rlvkGetPixelDataSize     (int width, int height, int format);  // Get pixel data size in bytes
static VkFormat     rlvkGetVkTextureFormat(int rlFormat);          // Get Vulkan format for a raylib pixel format
static VkFormatFeatureFlags rlvkQueryFormatFeatures(int rlFormat); // optimalTilingFeatures of the selected device for a raylib pixel format

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
static void         rlvkPushSet0Batch     (VkCommandBuffer cmdBuffer, rlvkShaderSlot *shader, u32 tex0Slot); // Coalesced set-0 push for the batch UBO path (§7.26)
static u32          rlvkCreateVBO         (const void *data, int size, bool isIndex, bool dynamic); // Create a buffer slot (static: device-local, dynamic: host-mapped)
static void         rlvkUploadBuffer      (VkBuffer dst, u32 dstOffset, const void *data, u32 size); // Copy into a device-local buffer (in-frame or one-shot)
static void         rlvkShaderWriteUniform(rlvkShaderSlot *shader, int loc, const void *data, u32 bytes); // Write a uniform into the shader staging blocks
static void         rlvkBindShaderUbos    (VkCommandBuffer cmdBuffer, rlvkShaderSlot *shader); // Snapshot uniform staging and push UBO descriptors
static void         rlvkBindShaderSsbos   (VkCommandBuffer cmdBuffer, rlvkShaderSlot *shader); // Push graphics-SSBO descriptors from the shared bind table (native push path)
static void         rlvkRebaseStorageBuffers(u32 **pSpv, size_t *pWordCount, bool injectNonWritable, u32 *outMask); // Rewrite SSBO bindings 0..3 -> set0 18..21 (+NonWritable)
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

