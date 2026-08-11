//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Types/structs, render-pass+framebuffer cache types, global state (RLVK)
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

//----------------------------------------------------------------------------------
// Module Types and Structures Definition
//----------------------------------------------------------------------------------

// Persistently-mapped buffer (one per frame-in-flight) backing ALL rlVertexBuffers of a render
// batch: the public rlVertexBuffer pointers alias mapped slices, so vertex writes land in place
typedef struct rlvkBatchBackingBuffer {
    VkBuffer            buffer;                 // Buffer handle
    VkDeviceMemory      memory;                 // Backing device memory
    void               *mapped;                 // Persistent host mapping
    u32                 sizeBytes;              // Buffer size in bytes
    u32                 freedFrame;             // Frame counter at unload (pooled-reuse fence gate)
} rlvkBatchBackingBuffer;

// Deferred GPU-object destruction: objects released while a command buffer may still reference
// them (recorded but not yet executed) are queued per frame-in-flight and destroyed once that
// frame slot's fence has been waited on, i.e. when the GPU is provably done with them
typedef struct rlvkDeadResource {
    VkBuffer            buffer;                 // Pooled buffer being evicted
    VkImage             image;
    VkImageView         view;
    VkSampler           sampler;
    VkDeviceMemory      memory;                 // Backing device memory
    VkPipeline          pipeline;               // Cached pipeline evicted by shader unload
    VkFramebuffer       framebuffer;            // Cached framebuffer evicted with a dying view
} rlvkDeadResource;

typedef struct rlvkTextureSlot {
    VkImage             image;
    VkImageView         view;
    VkSampler           sampler;
    VkDeviceMemory      memory;                 // Backing device memory
    VkFormat            format;
    int                 width, height;
    int                 mipCount;
    int                 rlFormat;
    VkImageLayout       currentLayout;         // Tracked image layout (barriers use it as oldLayout)
    // Sampleable depth twin (Caps.noSampledDepth only): the attachment `image` has no SAMPLED
    // usage on the quirk driver, so its depth is copied into this twin at FBO scope close and
    // rlvkPushTexture samples the twin instead (§7.1 shadow-copy). NULL on healthy drivers.
    // The twin is an R32_SFLOAT COLOR image, not a depth image: MoltenVK/Metal cannot sample a
    // depth-format texture through a plain GLSL sampler2D (returns garbage), so the raw NDC depth
    // is round-tripped depth-image -> sampleScratch buffer -> R32F color twin (buffer copies cross
    // the depth/color aspect that vkCmdCopyImage cannot). depth_copy.fs reads it as raw NDC depth.
    VkImage             sampleImage;
    VkImageView         sampleView;
    VkDeviceMemory      sampleMemory;
    VkImageLayout       sampleLayout;
    VkBuffer            sampleScratch;         // w*h*4 staging for the depth->color aspect bounce
    VkDeviceMemory      sampleScratchMemory;
    // `frameCounter + 1` of the last bind of this twin as a shader resource; 0 = never bound.
    // The depth->buffer->twin bounce at scope close only runs while a bind is RECENT (within
    // RLVK_TWIN_KEEPALIVE_FRAMES): a twin nobody reads costs w*h*4 bytes moved TWICE per pass,
    // ~7 ms/frame at 2048². A window rather than a sticky flag so a target that STOPS being
    // sampled (soft-particle depth while no soft particles are on screen) stops paying too; the
    // window is re-armed by every bind, so a continuous consumer never lapses. Layout bookkeeping
    // is identical whether or not the bounce runs, which is what makes this safe (§7.27/§7.29).
    u64                 sampleWantedFrame;
    VkFilter            minFilter, magFilter;  // Sampler filters (rlTextureParameters)
    VkSamplerMipmapMode mipMode;               // Sampler mipmap mode
    VkSamplerAddressMode wrapS, wrapT;         // Sampler wrap modes (GL default: repeat)
    bool                inUse;                 // Slot occupied
} rlvkTextureSlot;

// One reflected uniform: a member of a stage's default uniform block and/or a sampler.
typedef struct rlvkUniform {
    char                name[64];
    int                 vsOffset, fsOffset;     // byte offset in that stage's default block (-1 = absent)
    int                 samplerBinding;         // >= 0: sampler uniform at this set-0 binding
} rlvkUniform;

typedef struct rlvkShaderSlot {
    VkShaderModule      vertMod;                // VS module for the static-pipeline (pipeline-cache) draw path
    VkShaderModule      fragMod;                // FS module for the static-pipeline (pipeline-cache) draw path
    VkShaderModule      compMod;                // CS module (compute programs)
    VkPipeline          computePipeline;        // Monolithic compute pipeline (one per program)
    char               *pendingCode;            // rlLoadShader stash until rlLoadShaderProgram* consumes it
    int                 pendingType;            // RL_VERTEX_SHADER / RL_FRAGMENT_SHADER / RL_COMPUTE_SHADER
    bool                isCompute;              // Slot holds a compute program (vsStage doubles as its uniform block)
    rlvkUniform        *uniforms;               // reflected uniform table ("location" = index here)
    unsigned char      *vsStage;                // CPU staging for the VS default uniform block
    unsigned char      *fsStage;                // CPU staging for the FS default uniform block
    int                 locs[RL_MAX_SHADER_LOCATIONS];  // Default shader locations table (rlgl semantics)
    VkPushConstantRange pcRange;                // Push-constant range (embedded default shader path)
    u32                 bindingTexture[RLVK_MAX_TEXTURE_UNITS]; // sampler binding -> explicit texture (rlSetUniformSampler)
    int                 bindingUnit[RLVK_MAX_TEXTURE_UNITS];    // sampler binding -> GL texture unit (glUniform1i)
    int                 attribLocs[RLVK_ATTRIB_COUNT];          // canonical attribute -> shader input location (-1 absent)
    u32                 vsBlockSize, fsBlockSize;   // Default uniform block sizes per stage
    u32                 vsWriteGen, fsWriteGen;     // Bumped when a stage block is written (rlSetUniform*)
    u32                 vsPushedGen, fsPushedGen;   // Write generation last snapshotted+pushed
    u32                 uboPushedEpoch;             // Command-buffer epoch of the last push (pushes die with the cb)
    int                 uniformCount;           // Entries in the reflected uniform table
    u32                 ssboMask;               // bit i: shader reads graphics SSBO index i (set0 binding RLVK_SSBO_BINDING_BASE+i)
    bool                usesUbo;                // runtime-compiled (reflected uniforms); false = embedded push-constant shader
    bool                inUse;                 // Slot occupied
} rlvkShaderSlot;

typedef struct rlvkFramebufferSlot {
    u32                 width, height;          // Framebuffer dimensions (from first attachment)
    u32                 colorCount;             // Color attachments in use (MRT)
    u32                 colorTextures[8];       // Color attachment texture slots
    u32                 depthTexture;           // Depth attachment texture slot (0 = none)
    u32                 stencilTexture;         // Stencil attachment texture slot (0 = none)
    bool                hasDepth, hasStencil;   // Attachment presence flags
    bool                inUse;                 // Slot occupied
} rlvkFramebufferSlot;

typedef struct rlvkBufferSlot {
    VkBuffer            buffer;                 // Buffer handle
    VkDeviceMemory      memory;                 // Backing device memory
    void               *mapped;                 // Persistent host mapping
    u32                 sizeBytes;              // Buffer size in bytes
    u32                 freedFrame;             // Frame counter at unload (pooled-reuse fence gate)
    int                 usageHint;              // rlgl usage hint (static/dynamic/stream)
    bool                isIndex;                // Created as an index buffer
    bool                inUse;                 // Slot occupied
} rlvkBufferSlot;

// Push-constant block, byte-for-byte matching the default shader's push_constant layout
// (src/shaders/rlvk_default.vert/.frag); serves both the batch and DrawMesh, like rlgl
typedef struct rlvkPushConstants {
    f32                 mvp[16];        // 0
    f32                 colDiffuse[4];  // 64
} rlvkPushConstants;                    // 80

// Pipeline key: every GL-changeable state a pipeline bakes; equal keys share one pipeline.
// Viewport/scissor stay dynamic; constants that never vary are baked and not part of the key.
typedef struct rlvkPipelineKey {
    VkFormat            colorFormats[8];        // Attachment formats of the target scope
    VkFormat            depthFormat;            // VK_FORMAT_UNDEFINED = scope has no depth
    u32                 shaderSlot;             // Shader modules + reflected attribute locations
    int                 blendMode;              // raylib blend mode (custom factors below)
    int                 blendSrcRGB, blendDstRGB, blendEqRGB;   // RL_BLEND_CUSTOM* factors (GL enums), else 0
    int                 blendSrcA, blendDstA, blendEqA;
    unsigned char       topology;               // 0 = line list, 1 = triangle list, 2 = triangle strip
    unsigned short      vertexLayout;           // RLVK_VLAYOUT_* + mesh attribute presence mask
    unsigned char       cullMode;               // VK_CULL_MODE_*
    unsigned char       polygonMode;            // Fill / line / point
    unsigned char       samples;                // Rasterization samples
    unsigned char       colorCount;             // Color attachments in the scope
    unsigned char       depthTest, depthWrite;  // Depth state
} rlvkPipelineKey;

// One cached pipeline: the key it was built from and the pipeline itself
typedef struct rlvkPipelineEntry {
    VkPipeline          pipeline;               // Ready-to-bind pipeline
    rlvkPipelineKey     key;                    // State combo this pipeline bakes
} rlvkPipelineEntry;

//----------------------------------------------------------------------------------
// Render-pass + framebuffer caches (Vulkan 1.1 baseline)
//
// The dynamic-rendering scope model is kept conceptually (scopes open lazily, suspend
// around copies/blits, resume with LOAD), but every scope now begins a cached
// VkRenderPass into a cached VkFramebuffer. Load/store ops are part of the render-pass
// key; compatibility for pipelines only depends on formats/samples/counts, so pipeline
// creation requests a canonical LOAD-ops pass of the same shape.
// Attachment order convention everywhere: colors [0..colorCount), then the MSAA resolve
// target (when hasResolve), then depth last.
//----------------------------------------------------------------------------------
#define RLVK_MAX_RENDER_PASSES          32
#define RLVK_MAX_CACHED_FRAMEBUFFERS    64
#define RLVK_MAX_SCOPE_ATTACHMENTS      10      // 8 colors + resolve + depth
#define RLVK_DESC_SETS_PER_FRAME        1024    // pool-ring fallback: snapshot sets per frame slot
#define RLVK_COMPUTE_SETS_PER_FRAME     256     // compute dispatch snapshot sets per frame slot
#define RLVK_SET0_CACHE_SIZE            128     // pool-ring fallback: distinct set-0 snapshots reused within one frame

typedef struct rlvkRenderPassKey {
    VkFormat            colorFormats[8];        // [i] for i < colorCount
    VkFormat            depthFormat;            // VK_FORMAT_UNDEFINED = no depth attachment
    unsigned char       colorCount;
    unsigned char       samples;                // normalized: 1 or 4 (matches pipeline multisample state)
    unsigned char       colorLoad;              // VkAttachmentLoadOp shared by every color attachment
    unsigned char       depthLoad;              // VkAttachmentLoadOp of the depth attachment
    unsigned char       depthStore;             // VkAttachmentStoreOp of the depth attachment
    unsigned char       hasResolve;             // MSAA fixed-function resolve into a 1x attachment (colorCount == 1 only)
    unsigned char       _pad[2];                // keep memcmp-comparable: always zero
} rlvkRenderPassKey;

typedef struct rlvkRenderPassEntry {
    VkRenderPass        pass;
    rlvkRenderPassKey   key;
} rlvkRenderPassEntry;

typedef struct rlvkFramebufferEntry {
    VkFramebuffer       framebuffer;
    VkRenderPass        pass;                   // compatibility class it was created against
    VkImageView         views[RLVK_MAX_SCOPE_ATTACHMENTS];
    u32                 viewCount;
    u32                 width, height;
} rlvkFramebufferEntry;

// A "VAO": records buffer slot + byte offset per vertex attribute plus the index buffer slot.
// Built by rlLoadVertexArray + rlLoadVertexBuffer + rlSetVertexAttribute at model-load time;
// consumed by rlvkDrawMesh as real vertex-buffer bindings (vkCmdSetVertexInputEXT).
typedef struct rlvkVertexArray {
    u32                 posSlot,    posOffset;
    u32                 uvSlot,     uvOffset;
    u32                 normalSlot, normalOffset;
    u32                 colorSlot,  colorOffset;
    u32                 tangentSlot, tangentOffset; // vertexTangent (normal mapping / PBR)
    u32                 uv2Slot,    uv2Offset;    // vertexTexCoord2 (lightmaps)
    u32                 instSlot,   instOffset;   // mat4 instanceTransform stream (instance rate)
    u32                 boneIdSlot, boneIdOffset; // vertexBoneIds (u8x4, unscaled)
    u32                 boneWtSlot, boneWtOffset; // vertexBoneWeights (f32x4)
    u32                 indexSlot;      // bufferSlots[] id holding the index buffer (0 = none)
    bool                inUse;                 // Slot occupied
} rlvkVertexArray;

// Pool-ring fallback: one cached set-0 snapshot, keyed by the full bindable state it captures.
// Within a frame many draws revisit the same (texture, UBO, SSBO) combo (font atlas + one UBO,
// the white texture, the scene RT rebound after each PostFX pass) - caching lets those reuse the
// already-allocated+written set instead of paying vkAllocateDescriptorSets + a full rewrite each
// time. The default-texture/dummy-buffer fallbacks in rlvkFlushSet0 are deterministic from these
// keys, so equal keys always produce an identical set. Reset whenever the frame's pool is reset.
typedef struct rlvkSet0CacheEntry {
    VkImageView     view   [RLVK_MAX_TEXTURE_UNITS];
    VkSampler       sampler[RLVK_MAX_TEXTURE_UNITS];
    VkBuffer        uboBuf  [2];
    VkDeviceSize    uboOff  [2];
    VkDeviceSize    uboRange[2];
    u32             ssboSlot[RLVK_SET0_SSBO_COUNT];
    VkDescriptorSet set;
} rlvkSet0CacheEntry;

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
typedef struct rlvkData {
    // 8B-aligned tier - slot tables holding 8B handles, descending by total size
    rlvkTextureSlot         textureSlots[RLVK_MAX_TEXTURE_SLOTS];
    rlvkBufferSlot          bufferSlots [RLVK_MAX_BUFFER_SLOTS];
    rlvkShaderSlot          shaderSlots [RLVK_MAX_SHADER_SLOTS];
    rlvkVertexArray         vertexArrays[RLVK_MAX_VAO_SLOTS];

    // 8B-aligned nested struct (contains pointers + matrix arrays)
    struct {
        // Pointers (8B)
        Matrix         *currentMatrix;
        int            *currentShaderLocs;

        // 4B-aligned, large Matrix arrays
        Matrix          stack[RL_MAX_MATRIX_STACK_SIZE];
        Matrix          projectionStereo[2];
        Matrix          viewOffsetStereo[2];
        Matrix          modelview;
        Matrix          projection;
        Matrix          transform;
        Matrix          meshMVP;                    // captured from rlSetUniformMatrix(MVP) by DrawMesh

        // 4B scalars and 4B-aligned arrays
        u32             activeTextureSlots[RLVK_MAX_TEXTURE_UNITS];   // GL texture units 0..15 (material maps use up to 10)
        int             scissorX, scissorY, scissorW, scissorH;     // Scissor rectangle (GL bottom-left origin)
        int             viewportX, viewportY, viewportW, viewportH; // Viewport rectangle (rlViewport)
        int             framebufferWidth, framebufferHeight;        // Current render dimensions
        int             blendSrcRGB, blendDstRGB, blendSrcA, blendDstA, blendEqRGB, blendEqA;  // Custom separate blend factors (GL enums)
        int             blendSrc, blendDst, blendEq;                // Custom blend factors (GL enums)
        int             blendMode;              // Current raylib blend mode
        int             cullMode;               // Face culling mode (front/back)
        int             stackCounter;           // Matrix stack depth
        int             mvStackDepth;           // MODELVIEW-only push depth (transformRequired reset, §7.26)
        int             vertexCounter;          // Vertices written into the current batch buffer
        int             currentMatrixMode;      // Current matrix mode (modelview/projection)
        u32             currentTextureSlot;     // Batch draw texture (rlSetTexture)
        u32             stateGeneration;        // Bumped by every setter feeding the pipeline key or viewport/scissor
        u32             cbEpoch;                // Bumped at every command-buffer restart (push descriptors reset)
        u32             currentShaderSlot;          // BATCH shader (rlgl currentShaderId): rlSetShader only
        u32             activeShaderSlot;           // "glUseProgram" shader: uniform writes + mesh/quad draws
        u32             currentFramebufferSlot;     // 0 = swapchain
        u32             currentVAO;                 // mesh path: bound vertex array (0 = none)
        int             activeTextureUnit;          // GL glActiveTexture unit (rlActiveTextureSlot)
        u32             samplerTextures[4];         // rlSetUniformSampler registrations (units 1..4, rlgl semantics)
        u32             currentVBO;                 // last bound vertex buffer (for rlSetVertexAttribute)
        f32             meshColDiffuse[4];          // captured from rlSetUniform(COLOR_DIFFUSE) by DrawMesh
        f32             texcoordx, texcoordy;
        f32             normalx, normaly, normalz;
        f32             pointSize;
        f32             lineWidth;

        // 1B (chars and bools)
        unsigned char   colorr, colorg, colorb, colora;
        unsigned char   clearR, clearG, clearB, clearA;
        bool            colorMask[4];
        bool            transformRequired;
        bool            stereoRender;
        bool            depthTest, depthWrite;
        bool            cullEnabled;
        bool            scissorEnabled;
        bool            colorBlendEnabled;
        bool            customBlendModified;
        bool            wireMode;
        bool            pointMode;
        bool            smoothLines;
    } State;

    // Current dynamic-rendering scope (0 = swapchain/backbuffer, else fbSlots[] id). GL render
    // textures are BOTTOM-UP, so FBO scopes render without the Y-flip (flipY=false, CCW front);
    // raylib's negative-source-rect convention then displays them correctly.
    struct rlvkScope {
        VkFormat        colorFormats[8];    // attachment formats (float formats disable blending)
        u32             fbSlot;
        u32             width, height;
        u32             colorCount;     // color attachments in the open scope (swapchain = 1)
        u32             samples;        // rasterization samples of the open scope (MSAA on swapchain only)
        bool            flipY;
    } scope;
    int                     deadResourceCount[RLVK_FRAME_INDEX_COUNT];
    int                     pipelineCount;      // entries used in pipelines[]
    int                     renderPassCount;    // entries used in renderPasses[]
    int                     framebufferCount;   // entries used in framebuffers[]
    int                     msaaSamples;        // requested via rlvkSetMsaaSamples (1 = off)
    u32                     blitReadFb;         // rlBindFramebuffer(RL_READ_FRAMEBUFFER, ...) source

    bool                    frameActive;        // a swapchain image is acquired and the render scope is open
    bool                    frameConsumed;      // rlReadScreenPixels already ended+presented this frame
    bool                    acquireWaited;      // this frame's acquire semaphore was consumed by an earlier submit (mid-frame flush)

    // 8B-aligned smaller arrays
    rlvkBatchBackingBuffer  batchBacking[RLVK_FRAME_INDEX_COUNT];
    rlvkBatchBackingBuffer  arena       [RLVK_FRAME_INDEX_COUNT];   // per-frame bump arena for flush data
    VkDeviceSize            arenaOffset [RLVK_FRAME_INDEX_COUNT];   // reset each frame in rlvkBeginFrame
    VkDeviceSize            arenaWanted [RLVK_FRAME_INDEX_COUNT];   // total bytes the frame demanded (grow arena when it exceeds capacity)
    VkCommandPool           cmdPools    [RLVK_FRAME_INDEX_COUNT];
    VkCommandBuffer         cmdBuffers     [RLVK_FRAME_INDEX_COUNT];

    // 8B-aligned struct (contains pointers internally)
    rlRenderBatch           defaultBatch;

    // 8B handles
    VkInstance              instance;
    VkPhysicalDevice        physicalDevice;
    VkDevice                device;
    VkQueue                 graphicsQueue;
    VkQueue                 transferQueue;
    VkSurfaceKHR            surface;
    VkDescriptorSetLayout   set0Layout;         // push-descriptor layout: texture units + per-stage UBOs
    VkPipelineLayout        pipelineLayout;     // shared by every pipeline: set 0 + push-constant range

    // Swapchain + per-frame present synchronization
    VkSwapchainKHR          swapchain;
    VkImage                 swapchainImages   [RLVK_MAX_SWAPCHAIN_IMAGES];
    VkImageView             swapchainViews    [RLVK_MAX_SWAPCHAIN_IMAGES];
    VkSemaphore             renderSemaphores  [RLVK_MAX_SWAPCHAIN_IMAGES]; // signaled by submit, waited by present (per image)
    VkSemaphore             acquireSemaphores [RLVK_FRAME_INDEX_COUNT];    // signaled by acquire (per frame-in-flight)
    VkFence                 frameFences       [RLVK_FRAME_INDEX_COUNT];    // CPU wait before reusing a frame's resources
    VkImage                 depthImage [RLVK_FRAME_INDEX_COUNT];   // one depth buffer per frame-in-flight
    VkImageView             depthView  [RLVK_FRAME_INDEX_COUNT];
    VkDeviceMemory          depthMemory[RLVK_FRAME_INDEX_COUNT];
    VkImage                 msaaImage  [RLVK_FRAME_INDEX_COUNT];   // 4x color target (resolved into interImage)
    VkImageView             msaaView   [RLVK_FRAME_INDEX_COUNT];
    VkDeviceMemory          msaaMemory [RLVK_FRAME_INDEX_COUNT];
    VkImage                 interImage [RLVK_FRAME_INDEX_COUNT];   // 1x UNMIRRORED color target (flip-blitted to swapchain)
    VkImageView             interView  [RLVK_FRAME_INDEX_COUNT];
    VkDeviceMemory          interMemory[RLVK_FRAME_INDEX_COUNT];
    rlvkDeadResource    deadResources[RLVK_FRAME_INDEX_COUNT][RLVK_MAX_DEAD_RESOURCES];   // deferred destruction, fence-gated
    rlvkPipelineEntry       pipelines[RLVK_MAX_PIPELINES];  // cached pipelines by baked-state key
    rlvkRenderPassEntry     renderPasses[RLVK_MAX_RENDER_PASSES];        // cached render passes by scope-shape key
    rlvkFramebufferEntry    framebuffers[RLVK_MAX_CACHED_FRAMEBUFFERS];  // cached framebuffers by pass + view set
    VkPipelineCache         pipelineCache;      // driver pipeline cache, persisted to disk across runs
    VkPipeline              boundPipeline;      // currently bound pipeline (skip redundant binds)
    rlvkShaderSlot         *lastUboShader;      // shader whose blocks hold UBO bindings 16/17 (they overwrite each other)
    VkImageView             pushedView   [RLVK_MAX_TEXTURE_UNITS];  // last view pushed per unit binding (skip redundant pushes; doubles as the set-0 shadow on the pool-ring path)
    VkSampler               pushedSampler[RLVK_MAX_TEXTURE_UNITS];  // last sampler pushed per unit binding
    u32                     pushedSsbo[RLVK_SET0_SSBO_COUNT];       // buffer slot last pushed per graphics-SSBO binding (0xFFFFFFFF = never; reset with the cb)

    // Pool-ring fallback state (devices without VK_KHR_push_descriptor): CPU shadow of the
    // UBO bindings + per-frame descriptor pools; a fresh set is allocated, fully written and
    // bound at the next draw whenever the shadow changed (rlvkFlushSet0)
    VkDescriptorBufferInfo  shadowUbo[2];                       // [0]=VS binding, [1]=FS binding
    VkDescriptorPool        descPools[RLVK_FRAME_INDEX_COUNT];  // reset with each frame slot's fence
    bool                    set0Dirty;                          // shadow changed since the last bound set
    VkDescriptorSet         boundSet0;                          // last set-0 actually bound (skip redundant vkCmdBindDescriptorSets on a cache hit)
    rlvkSet0CacheEntry      set0Cache[RLVK_FRAME_INDEX_COUNT][RLVK_SET0_CACHE_SIZE]; // per-frame snapshot reuse cache
    u32                     set0CacheCount[RLVK_FRAME_INDEX_COUNT];                  // live entries this frame slot (cleared on pool reset)

    // Compute state (core Vulkan 1.0/1.1 features only). Fixed set-0 layout for every compute
    // program: bindings 0..7 SSBO, 8..11 storage image, 12..13 combined sampler, 14 the
    // implicit loose-uniform block. GL-style bind-then-dispatch: rlBindShaderBuffer /
    // rlBindImageTexture record here; rlComputeShaderDispatch snapshots into a fresh set.
    VkDescriptorSetLayout   computeSetLayout;
    VkPipelineLayout        computePipelineLayout;
    VkDescriptorPool        computeDescPools[RLVK_FRAME_INDEX_COUNT];   // reset with each frame slot's fence
    u32                     computeSSBO[8];                     // buffer slots bound per SSBO index (0 = unbound); shared GL-style
                                                                // bind table: compute dispatch reads 0..7, graphics draws read 0..3
                                                                // into set0 bindings RLVK_SSBO_BINDING_BASE+i
    u32                     computeImage[4];                    // texture slots bound per image unit (0 = unbound)
    VkExtent2D              swapchainExtent;
    VkFormat                swapchainFormat;
    VkFormat                depthFormat;
    u32                     swapchainImageCount;
    u32                     currentImageIndex;
    // Android Vulkan pre-rotation (see rlvkAttachSurface + rlSetMatrixProjection in
    // rlvk_compute.inl): quarter-turns [0..3] the app must compensate for in its own clip-space
    // output because preTransform is now set to match the device's real currentTransform
    // (rather than always IDENTITY) - some Android/Mali drivers otherwise treat a
    // preTransform/currentTransform mismatch as perpetually suboptimal and keep signaling
    // VK_ERROR_OUT_OF_DATE_KHR every single frame.
    int                     preRotationQuarterTurns;

    // 8B pointers
    rlRenderBatch          *currentBatch;
    int                    *defaultShaderLocs;

    // 8B scalar and pointer
    u64                     frameCounter;
    void                   *shadercCompiler;    // shaderc_compiler_t (shaderc_shared.dll), NULL if unavailable

    // 4B-aligned tier - fbSlots (4B inner alignment)
    rlvkFramebufferSlot     fbSlots[RLVK_MAX_FRAMEBUFFER_SLOTS];

    // 4B-aligned nested struct
    struct {
        u32         apiVersion;         // Selected device's VkPhysicalDeviceProperties.apiVersion
        bool        memoryPriority;     // VK_EXT_memory_priority available (optional)
        bool        pageableMemory;     // VK_EXT_pageable_device_local_memory available (optional)
        bool        graphicsPipelineLibrary;    // VK_EXT_graphics_pipeline_library available (fast-linked pipelines)
        // Vulkan 1.1 retarget: every former 1.3-floor requirement is a queried capability.
        // Each flag's 1.1-core fallback lands milestone by milestone; once a fallback is in,
        // the flag either becomes a pure fast-path gate or is deleted along with the old path.
        bool        dynamicRendering;   // 1.3 core / VK_KHR_dynamic_rendering (unused: render-pass cache is the single path)
        bool        synchronization2;   // 1.3 core / VK_KHR_synchronization2 (fallback: sync1 shim)
        bool        pushDescriptor;     // VK_KHR_push_descriptor (fallback: per-frame descriptor-pool ring)
        bool        bresenhamLines;     // VK_EXT/KHR_line_rasterization (fallback: default line raster, cosmetic delta)
        bool        wideLines;          // Core optional feature (fallback: clamp rlSetLineWidth to 1.0)
        bool        fillModeNonSolid;   // Core optional feature (fallback: rlEnableWireMode/PointMode no-op)
        bool        graphicsSsboStores; // vertexPipelineStoresAndAtomics: graphics-stage SSBOs may be written.
                                        // Absent -> rlvkRebaseStorageBuffers injects NonWritable (read-only SSBOs,
                                        // which is all the GPU-particle path needs; optional on many 1.1 devices)
        // Optional format features. The spec makes SAMPLED_IMAGE_FILTER_LINEAR and
        // COLOR_ATTACHMENT_BLEND mandatory for R16_SFLOAT but NOT for R32_SFLOAT, so an
        // additively-blended or bilinearly-sampled R32F target is optional behaviour that
        // desktop drivers happen to provide. Detected at init so the log says so once,
        // rather than the caller discovering it as corrupt pixels on another device.
        // Callers query per format through rlvkFormatSupports*(); these two are cached
        // because R32F render targets are the case the engine actually leans on.
        bool        floatBlendR32;      // R32_SFLOAT supports COLOR_ATTACHMENT_BLEND
        bool        floatFilterR32;     // R32_SFLOAT supports SAMPLED_IMAGE_FILTER_LINEAR
        // Driver quirks (empirically bisected, see tests/rlvk_visual_test.c depth_rt scenario)
        bool        noSampledDepth;     // MoltenVK/Intel: SAMPLED usage on a depth image silently
                                        // disables depth test/write on that attachment. When set,
                                        // FBO depth images drop SAMPLED (depth-sampling shaders
                                        // like soft particles/screen distortion lose their input).
    } Caps;

    // 4B scalars
    u32                     graphicsFamily;
    u32                     transferFamily;
    u32                     frameIndex;
    u32                     defaultTextureSlot;
    u32                     defaultShaderSlot;
    u32                     dummyAttribSlot;    // divisor-0 broadcast buffer: [0]=vec2(0,0) uv, [8]=white color
} rlvkData;

static rlvkData     RLVK = { 0 };
static bool         isGpuReady = false;

// Binding signature: the shader + mesh vertex-buffer slots/offsets + texture bound by a mesh
// draw. When it matches the previous draw the texture push and buffer bindings are redundant
// and skipped (invalidated on every command-buffer restart, same as above).
typedef struct rlvkBindingSig {
    int shaderSlot, texSlot;
    int posSlot, uvSlot, normalSlot, colorSlot, uv2Slot, tangentSlot, boneIdSlot, boneWtSlot;
    ull posOff, uvOff, normalOff, colorOff, uv2Off, tangentOff, boneIdOff, boneWtOff;
} rlvkBindingSig;
static bool           s_bindingValid = false;
static rlvkBindingSig s_bindingSig;

// Viewport/scissor signature: the only dynamic pipeline state. Consecutive draws with the
// same inputs skip the two set commands (invalidated on every command-buffer restart).
typedef struct rlvkViewportSig {
    int vx, vy, vw, vh;
    int scEn, scx, scy, scw, sch;
    int scopeW, scopeH, flipY;
} rlvkViewportSig;
static bool            s_viewportValid = false;
static rlvkViewportSig s_viewportSig;

// Pipeline fast path: when no key-feeding state changed since the last draw, the previous
// pipeline and viewport/scissor are still exactly right and the whole bind path is skipped
static bool           s_pipelineFastValid = false;

// Debug trace flags, resolved once: getenv scans the environment block and is far too slow
// for per-draw paths
static int rlvkDebugFlag(const char *name, int *cache)
{
    if (*cache < 0) *cache = (getenv(name) != NULL);
    return *cache;
}
static int s_dbgSamplers = -1, s_dbgFbo = -1, s_dbgFlush = -1, s_dbgVao = -1, s_dbgPipe = -1, s_dbgVtx = -1;
static ill s_memLocalBytes, s_memHostBytes; static int s_memAllocCount, s_vboCreateCount, s_vboReuseCount, s_dbgMem = -1;   // RLVK_MEM_REPORT accounting

// GPU timestamp trace (RLVK_GPU_TRACE env): three timestamps per frame measure the GPU span
// of scene rendering vs the present chain (resolve/flip blit + layout transitions), read back
// one frame ring behind. Averages print every 512 frames.
static VkQueryPool s_gpuPool = VK_NULL_HANDLE;
static int         s_dbgGpu = -1;
static f32         s_gpuPeriod;      // nanoseconds per timestamp tick
static f64         s_gpuScene, s_gpuPresent;
static int         s_gpuFrames;
static u32            s_lastGeneration;
static u32            s_lastShaderSlot;
static unsigned short s_lastVertexLayout;
static unsigned char  s_lastTopology;
static f64          rlCullDistanceNear = RL_CULL_DISTANCE_NEAR;
static f64          rlCullDistanceFar  = RL_CULL_DISTANCE_FAR;

