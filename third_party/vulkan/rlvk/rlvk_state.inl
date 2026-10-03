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
    bool                sampleDirty;          // depth was written since the sample twin was last refreshed
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

    // Offscreen MSAA (rlvkSetFramebufferSamples; 0/1 = off, the default). The attached
    // color/depth TEXTURES stay 1x and become RESOLVE destinations - everything that samples
    // the render texture keeps working unchanged. These privately-owned images are what the
    // scene actually rasterizes into. Colour resolves with the fixed-function subpass resolve;
    // depth needs VK_KHR_depth_stencil_resolve (Caps.depthResolve), which is why an FBO with a
    // depth attachment is refused MSAA when that capability is missing.
    // NOT transient/lazily-allocated: rlvk opens and closes an FBO scope many times per frame
    // and every reopen is loadOp LOAD, so the multisample contents must survive between passes.
    unsigned char       samples;                // 1 = off, else 2 or 4
    VkImage             msColorImage;           // multisample color target (colorCount == 1 only)
    VkImageView         msColorView;
    VkDeviceMemory      msColorMemory;
    VkImageLayout       msColorLayout;
    VkImage             msDepthImage;           // 4x depth target (resolved into depthTexture)
    VkImageView         msDepthView;
    VkDeviceMemory      msDepthMemory;
    VkImageLayout       msDepthLayout;
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
    unsigned char       depthResolve;           // Scope resolves MSAA depth (offscreen MSAA with a depth attachment):
                                                // a different render-pass compatibility class than the swapchain's
                                                // colour-only resolve, so it must key its own pipelines
    unsigned char       _pad[3];                // keep memcmp-comparable: always zero
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
#define RLVK_MAX_SCOPE_ATTACHMENTS      11      // 8 colors + colour resolve + depth + depth resolve
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
    unsigned char       hasDepthResolve;        // MSAA depth resolve into a 1x depth attachment (needs Caps.depthResolve;
                                                // forces the vkCreateRenderPass2 path). Part of the key because it changes
                                                // the attachment list, and pipeline render-pass COMPATIBILITY compares
                                                // resolve attachment references - a pipeline built against a shape without
                                                // it may not be bound inside a pass that has it.
    unsigned char       _pad[1];                // keep memcmp-comparable: always zero
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
        u32             samples;        // rasterization samples of the open scope (swapchain MSAA, or an FBO
                                        // that asked for it via rlvkSetFramebufferSamples)
        bool            depthResolve;   // the open scope resolves MSAA depth into a 1x depth attachment
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

    /* Out-of-frame compute dispatches accumulate here instead of each paying its
     * own command pool + submit + vkQueueWaitIdle + pool destroy. Measured at
     * ~0.6 ms PER DISPATCH (tests/rlvk_visual_test.c perf_dispatch_count), which
     * is what made core/fluid's 9-dispatch PBD solve cost 4.4 ms for 72
     * workgroups of actual work. The batch is flushed before anything that must
     * observe its results: frame begin (ahead of the descriptor-pool reset that
     * would free sets it still references), any other one-shot submission, and
     * teardown. */
    struct {
        VkCommandPool   pool;
        VkCommandBuffer cmd;
        bool            open;
    } computeBatch;

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
        // Offscreen MSAA (rlvkSetFramebufferSamples). Both are optional: 4x on a framebuffer
        // attachment is a limits bitmask the device may not set, and resolving a multisample
        // DEPTH attachment has no Vulkan 1.1-core path at all (vkCmdResolveImage is colour-only,
        // vkCmdCopyImage rejects samples > 1), so it needs VK_KHR_create_renderpass2 +
        // VK_KHR_depth_stencil_resolve. Missing either one degrades to 1 sample, never to
        // wrong pixels: rlvkSetFramebufferSamples returns the count actually in effect.
        bool        msaa4x;             // framebufferColor/DepthSampleCounts both include 4x
        bool        depthResolve;       // VK_KHR_depth_stencil_resolve with SAMPLE_ZERO + renderpass2
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

// Opt-in CPU recording/counter profile. These are host spans, never GPU pass timings.
static int s_dbgProfile = -1;
typedef struct rlvkProfileScope {
    u64 opens, colorResolves, depthResolves, draws, twinPixels;
    u32 width, height, samples;
    f64 recordMs;
} rlvkProfileScope;
static rlvkProfileScope s_profileScopes[RLVK_MAX_FRAMEBUFFER_SLOTS];
static u64 s_profileUboUploads, s_profileUboBytes, s_profileDispatches, s_profileDescriptors;
static u64 s_profileTextureUploads, s_profileTextureBytes, s_profileTextureAsync;
static u64 s_profileUniformSkipped;
static u32 s_profileFrames, s_profileScope;
static f64 s_profileScopeStart, s_profileFenceMs, s_profileAcquireMs, s_profileSubmitMs, s_profilePresentMs, s_profileIdleMs, s_profileEndIdleMs, s_profileFlushWaitMs;
static bool s_profileScopeActive;
static f64 rlvkProfileNow(void)
{
#if defined(__APPLE__)
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (f64)mach_absolute_time() * (f64)tb.numer / (f64)tb.denom * 1e-6;
#elif !defined(_WIN32)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (f64)ts.tv_sec * 1000.0 + (f64)ts.tv_nsec * 1e-6;
#else
    // Windows fallback is process CPU time; wall synchronization spans are unavailable.
    return (f64)clock() * 1000.0 / CLOCKS_PER_SEC;
#endif
}
static VkResult rlvkProfileWaitFences(VkDevice device, u32 count, const VkFence *fences, VkBool32 all, u64 timeout)
{
    bool profile = rlvkDebugFlag("RLVK_PROFILE", &s_dbgProfile);
    f64 t = profile ? rlvkProfileNow() : 0;
    VkResult result = vkWaitForFences(device, count, fences, all, timeout);
    if (profile) s_profileFlushWaitMs += rlvkProfileNow() - t;
    return result;
}
static void rlvkProfileEndScope(void)
{
    if (rlvkDebugFlag("RLVK_PROFILE", &s_dbgProfile) && s_profileScopeActive)
    {
        s_profileScopes[s_profileScope].recordMs += rlvkProfileNow() - s_profileScopeStart;
        s_profileScopeActive = false;
    }
}
static void rlvkProfileBeginScope(u32 slot, const rlvkRenderPassKey *key, u32 width, u32 height)
{
    if (!rlvkDebugFlag("RLVK_PROFILE", &s_dbgProfile)) return;
    rlvkProfileEndScope();
    if (slot >= RLVK_MAX_FRAMEBUFFER_SLOTS) return;
    rlvkProfileScope *p = &s_profileScopes[slot];
    p->opens++; p->colorResolves += key->hasResolve; p->depthResolves += key->hasDepthResolve;
    p->width = width; p->height = height; p->samples = key->samples;
    s_profileScope = slot; s_profileScopeStart = rlvkProfileNow(); s_profileScopeActive = true;
}
// Every draw site calls this, including draws outside profiling. A read-only depth test
// cannot change the sampled depth twin; preserving it avoids redundant aspect copies.
static void rlvkRecordDepthWrite(void)
{
    u32 id = RLVK.scope.fbSlot;
    if (id && id < RLVK_MAX_FRAMEBUFFER_SLOTS && RLVK.fbSlots[id].hasDepth)
        RLVK.textureSlots[RLVK.fbSlots[id].depthTexture].sampleDirty = true;
}
static void rlvkProfileDraw(void)
{
    if (RLVK.State.depthTest && RLVK.State.depthWrite) rlvkRecordDepthWrite();
    if (rlvkDebugFlag("RLVK_PROFILE", &s_dbgProfile) && s_profileScopeActive)
        s_profileScopes[s_profileScope].draws++;
}
static void rlvkProfileReport(void)
{
    if (!rlvkDebugFlag("RLVK_PROFILE", &s_dbgProfile) || ++s_profileFrames < 60) return;
    f64 n = s_profileFrames;
    (void)n;
    // Raylib's trace buffer is bounded: keep each independently useful row short.
    TRACELOG(RL_LOG_WARNING, "VKPROFILE frames=%u host_fence=%.3fms acquire=%.3fms submit=%.3fms present=%.3fms oneshot_idle=%.3fms oneshot_end_idle=%.3fms flush_fence=%.3fms",
             s_profileFrames, s_profileFenceMs/n, s_profileAcquireMs/n, s_profileSubmitMs/n, s_profilePresentMs/n, s_profileIdleMs/n, s_profileEndIdleMs/n, s_profileFlushWaitMs/n);
    TRACELOG(RL_LOG_WARNING, "VKPROFILE frames=%u ubo_uploads=%.1f ubo_bytes=%.1f descriptors=%.1f dispatches=%.1f uniform_stages_skipped=%.1f",
             s_profileFrames, s_profileUboUploads/n, s_profileUboBytes/n, s_profileDescriptors/n, s_profileDispatches/n, s_profileUniformSkipped/n);
    TRACELOG(RL_LOG_WARNING, "VKPROFILE frames=%u tex_uploads=%.1f tex_bytes=%.1f tex_async=%.1f",
             s_profileFrames, s_profileTextureUploads/n, s_profileTextureBytes/n, s_profileTextureAsync/n);
    for (u32 i = 0; i < RLVK_MAX_FRAMEBUFFER_SLOTS; i++)
    {
        rlvkProfileScope *p = &s_profileScopes[i];
        if (!p->opens) continue;
        TRACELOG(RL_LOG_WARNING, "VKPROFILE fb=%u %ux%u samples=%u opens=%.1f color_resolves=%.1f depth_resolves=%.1f draws=%.1f twin_pixels=%.1f host_record=%.3fms",
                 i, p->width, p->height, p->samples, p->opens/n, p->colorResolves/n, p->depthResolves/n, p->draws/n, p->twinPixels/n, p->recordMs/n);
    }
    memset(s_profileScopes, 0, sizeof(s_profileScopes));
    s_profileUboUploads = s_profileUboBytes = s_profileDispatches = s_profileDescriptors = 0;
    s_profileTextureUploads = s_profileTextureBytes = s_profileTextureAsync = s_profileUniformSkipped = 0;
    s_profileFenceMs = s_profileAcquireMs = s_profileSubmitMs = s_profilePresentMs = s_profileIdleMs = s_profileEndIdleMs = s_profileFlushWaitMs = 0;
    s_profileFrames = 0;
}

// GPU timestamp trace (RLVK_GPU_TRACE env): three timestamps per frame measure the GPU span
// of scene rendering vs the present chain (resolve/flip blit + layout transitions), read back
// one frame ring behind. Completed-frame windows print every 512 frames (60 with RLVK_PROFILE).
static VkQueryPool s_gpuPool = VK_NULL_HANDLE;
static int         s_dbgGpu = -1;
static f32         s_gpuPeriod;      // nanoseconds per timestamp tick
typedef struct rlvkGpuTraceWindow {
    f64 sceneMs, presentMs;
    u32 frames, valid, unavailable, invalid, fragmented;
} rlvkGpuTraceWindow;
static rlvkGpuTraceWindow s_gpuWindow;
static bool s_gpuFragmented[RLVK_FRAME_INDEX_COUNT];
static bool rlvkGpuTraceRecord(rlvkGpuTraceWindow *window, const u64 q[3], f32 period,
                               bool available, bool fragmented)
{
    window->frames++;
    if (!available) { window->unavailable++; return false; }
    if (fragmented) { window->fragmented++; return false; }
    // Availability proves completion, not usable clock resolution. In particular,
    // zero spans must not dilute one startup sample into a fictitious steady average.
    if (q[1] <= q[0] || q[2] <= q[1] || period <= 0)
    { window->invalid++; return false; }
    window->sceneMs += (f64)(q[1] - q[0]) * period * 1e-6;
    window->presentMs += (f64)(q[2] - q[1]) * period * 1e-6;
    window->valid++;
    return true;
}
static u32            s_lastGeneration;
static u32            s_lastShaderSlot;
static unsigned short s_lastVertexLayout;
static unsigned char  s_lastTopology;
static f64          rlCullDistanceNear = RL_CULL_DISTANCE_NEAR;
static f64          rlCullDistanceFar  = RL_CULL_DISTANCE_FAR;
