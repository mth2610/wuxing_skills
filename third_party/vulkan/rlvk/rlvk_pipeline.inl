//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: VkPipeline state cache (GL-equivalent state baked into pipelines)
//
// [OPTIMIZED VERSION - FIXED LINKAGE]
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

// Pipeline cache management
//-----------------------------------------------------------------------------------------
// GL-equivalent state baked into VkPipelines keyed by rlvkPipelineKey, bound once per state
// combo; only viewport/scissor stay dynamic. The VkPipelineCache persists to disk across runs.

static u32 s_pipelineHashes[RLVK_MAX_PIPELINES] = {0}; // OPTIMIZATION: Cache hashes for fast lookup

// OPTIMIZATION: FNV-1a Hash function for fast pipeline key lookup
static inline u32 rlvkHashPipelineKey(const rlvkPipelineKey *key)
{
    const unsigned char *data = (const unsigned char *)key;
    u32 hash = 2166136261u;
    for (size_t i = 0; i < sizeof(rlvkPipelineKey); i++)
    {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

// Get the pipeline-cache file path (driver blob reused across runs for instant creation)
static const char *rlvkGetPipelineCachePath(void)
{
    static char path[512] = {0};
    if (path[0] == 0)
    {
        // OPTIMIZATION: Support TMPDIR for Linux/macOS environments before falling back
        const char *tmp = getenv("TMPDIR");
        if (tmp == NULL)
            tmp = getenv("TEMP");
        if (tmp == NULL)
            tmp = getenv("TMP");
        if (tmp == NULL)
            tmp = ".";
        snprintf(path, sizeof(path), "%s/raylib_rlvk.pipelinecache", tmp);
    }
    return path;
}

// Initialize the driver pipeline cache, seeding it with the previous run's data when present
static void rlvkInitPipelineCache(void)
{
    void *data = NULL;
    long dataSize = 0;
    FILE *file = fopen(rlvkGetPipelineCachePath(), "rb");
    if (file != NULL)
    {
        fseek(file, 0, SEEK_END);
        long pos = ftell(file); // OPTIMIZATION: Catch ftell errors (-1)
        if (pos >= 0)
        {
            dataSize = pos;
            fseek(file, 0, SEEK_SET);
            if (dataSize > 0)
            {
                data = RL_MALLOC((size_t)dataSize);
                if (fread(data, 1, (size_t)dataSize, file) != (size_t)dataSize)
                {
                    RL_FREE(data);
                    data = NULL;
                    dataSize = 0;
                }
            }
        }
        fclose(file);
    }

    // The driver validates the blob header (UUID) itself and falls back to empty on mismatch
    RLVK_CHECK(vkCreatePipelineCache(RLVK.device,
                                     &(VkPipelineCacheCreateInfo){
                                         VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
                                         .initialDataSize = (size_t)dataSize,
                                         .pInitialData = data,
                                     },
                                     RLVK_ALLOC, &RLVK.pipelineCache));
    if (data != NULL)
        RL_FREE(data);
    if (dataSize > 0)
        TRACELOG(RL_LOG_INFO, "RLVK: pipeline cache seeded from disk (%ld bytes)", dataSize);
}

// Save the driver pipeline cache to disk so the next run creates pipelines instantly
static void rlvkSavePipelineCache(void)
{
    if (RLVK.pipelineCache == VK_NULL_HANDLE)
        return;

    size_t dataSize = 0;
    vkGetPipelineCacheData(RLVK.device, RLVK.pipelineCache, &dataSize, NULL);
    if (dataSize == 0)
        return;

    void *data = RL_MALLOC(dataSize);
    if (vkGetPipelineCacheData(RLVK.device, RLVK.pipelineCache, &dataSize, data) == VK_SUCCESS)
    {
        FILE *file = fopen(rlvkGetPipelineCachePath(), "wb");
        if (file != NULL)
        {
            fwrite(data, 1, dataSize, file);
            fclose(file);
            TRACELOG(RL_LOG_INFO, "RLVK: pipeline cache saved to disk (%llu bytes)", (ull)dataSize);
        }
    }
    RL_FREE(data);
}

// Get the blend attachment state for a pipeline key (same factor mapping as the GL backend)
static VkPipelineColorBlendAttachmentState rlvkGetBlendAttachmentState(const rlvkPipelineKey *key, bool blendEnabled)
{
    VkPipelineColorBlendAttachmentState state = {
        .blendEnable = blendEnabled ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    switch (key->blendMode)
    {
    case RL_BLEND_ALPHA:
        break; // defaults above
    case RL_BLEND_ADDITIVE:
        state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    case RL_BLEND_MULTIPLIED:
        state.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case RL_BLEND_ADD_COLORS:
        state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    case RL_BLEND_SUBTRACT_COLORS:
        state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.colorBlendOp = VK_BLEND_OP_SUBTRACT;
        state.alphaBlendOp = VK_BLEND_OP_SUBTRACT;
        break;
    case RL_BLEND_ALPHA_PREMULTIPLY:
        state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case RL_BLEND_CUSTOM:
        state.srcColorBlendFactor = rlvkBlendFactorFromGL(key->blendSrcRGB);
        state.dstColorBlendFactor = rlvkBlendFactorFromGL(key->blendDstRGB);
        state.colorBlendOp = rlvkBlendOpFromGL(key->blendEqRGB);
        state.srcAlphaBlendFactor = state.srcColorBlendFactor;
        state.dstAlphaBlendFactor = state.dstColorBlendFactor;
        state.alphaBlendOp = state.colorBlendOp;
        break;
    case RL_BLEND_CUSTOM_SEPARATE:
        state.srcColorBlendFactor = rlvkBlendFactorFromGL(key->blendSrcRGB);
        state.dstColorBlendFactor = rlvkBlendFactorFromGL(key->blendDstRGB);
        state.colorBlendOp = rlvkBlendOpFromGL(key->blendEqRGB);
        state.srcAlphaBlendFactor = rlvkBlendFactorFromGL(key->blendSrcA);
        state.dstAlphaBlendFactor = rlvkBlendFactorFromGL(key->blendDstA);
        state.alphaBlendOp = rlvkBlendOpFromGL(key->blendEqA);
        break;
    default:
        break;
    }
    return state;
}

// Dummy-broadcast table for shader-declared attributes the layout doesn't feed: a pipeline
// must feed every consumed location, so uncovered ones get divisor-0 broadcasts (GL's generic
// vertex defaults); INSTANCE_TX exempt (instancing shaders always draw instanced in raylib)
static const struct
{
    unsigned char attrib;
    unsigned char dummyOffset;
    VkFormat format;
} rlvkDummyAttribs[] = {
    {RLVK_ATTRIB_TEXCOORD, 0, VK_FORMAT_R32G32_SFLOAT},           // vec2(0,0)
    {RLVK_ATTRIB_NORMAL, 12, VK_FORMAT_R32G32B32_SFLOAT},         // +Z
    {RLVK_ATTRIB_COLOR, 8, VK_FORMAT_R8G8B8A8_UNORM},             // opaque white
    {RLVK_ATTRIB_TANGENT, 28, VK_FORMAT_R32G32B32A32_SFLOAT},     // (0,0,0,1) GL default
    {RLVK_ATTRIB_TEXCOORD2, 0, VK_FORMAT_R32G32_SFLOAT},          // vec2(0,0)
    {RLVK_ATTRIB_BONEIDS, 24, VK_FORMAT_R8G8B8A8_USCALED},        // (0,0,0,1)
    {RLVK_ATTRIB_BONEWEIGHTS, 28, VK_FORMAT_R32G32B32A32_SFLOAT}, // (0,0,0,1)
};

// Which canonical attributes a vertex layout feeds, and the first binding index free for
// dummy broadcasts. Must mirror rlvkBuildVertexInput's binding assignment exactly.
static unsigned int rlvkLayoutCoverage(unsigned short vertexLayout, u32 *firstFreeBinding)
{
    if (vertexLayout == RLVK_VLAYOUT_NONE)
    {
        *firstFreeBinding = 0;
        return 0xFFFFFFFFu;
    }
    if (vertexLayout == RLVK_VLAYOUT_QUAD)
    {
        *firstFreeBinding = 1;
        return (1u << RLVK_ATTRIB_POSITION) | (1u << RLVK_ATTRIB_TEXCOORD);
    }
    unsigned int covered = (1u << RLVK_ATTRIB_POSITION) | (1u << RLVK_ATTRIB_TEXCOORD) |
                           (1u << RLVK_ATTRIB_NORMAL) | (1u << RLVK_ATTRIB_COLOR);
    if (vertexLayout == RLVK_VLAYOUT_BATCH)
    {
        *firstFreeBinding = 4;
        return covered;
    }

    u32 next = 4;
    if (vertexLayout & RLVK_VLAYOUT_MESH_INSTANCED)
    {
        covered |= (1u << RLVK_ATTRIB_INSTANCE_TX);
        next = 5;
    }
    else if (vertexLayout & (RLVK_VLAYOUT_MESH_BONES | RLVK_VLAYOUT_MESH_BONES_DUMMY))
    {
        covered |= (1u << RLVK_ATTRIB_BONEIDS) | (1u << RLVK_ATTRIB_BONEWEIGHTS);
        next = 6;
    }
    else
    {
        if (vertexLayout & RLVK_VLAYOUT_MESH_UV2)
        {
            covered |= (1u << RLVK_ATTRIB_TEXCOORD2);
            next++;
        }
        if (vertexLayout & RLVK_VLAYOUT_MESH_TANGENT)
        {
            covered |= (1u << RLVK_ATTRIB_TANGENT);
            next++;
        }
    }
    *firstFreeBinding = next;
    return covered;
}

static void rlvkAppendDummyAttribs(unsigned short vertexLayout, rlvkShaderSlot *shader,
                                   VkVertexInputBindingDescription *binds, u32 *bindCount,
                                   VkVertexInputAttributeDescription *attrs, u32 *attrCount)
{
    u32 dummyBinding = 0;
    unsigned int covered = rlvkLayoutCoverage(vertexLayout, &dummyBinding);
    for (int i = 0; i < (int)RLVK_COUNTOF(rlvkDummyAttribs); i++)
    {
        int idx = rlvkDummyAttribs[i].attrib;
        if ((covered & (1u << idx)) || (shader->attribLocs[idx] < 0))
            continue;
#if defined(__APPLE__)
        u32 stride = 44;
        VkVertexInputRate rate = VK_VERTEX_INPUT_RATE_INSTANCE;
#else
        u32 stride = 0;
        VkVertexInputRate rate = VK_VERTEX_INPUT_RATE_VERTEX;
#endif
        binds[*bindCount] = (VkVertexInputBindingDescription){.binding = dummyBinding, .stride = stride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[idx], .binding = dummyBinding, .format = rlvkDummyAttribs[i].format};
        (*bindCount)++;
        dummyBinding++;
    }
}

static void rlvkBuildVertexInput(const rlvkPipelineKey *key,
                                 VkVertexInputBindingDescription *binds, u32 *bindCount,
                                 VkVertexInputAttributeDescription *attrs, u32 *attrCount)
{
    *bindCount = 0;
    *attrCount = 0;
    if (key->vertexLayout == RLVK_VLAYOUT_NONE)
        return;

    rlvkShaderSlot *shader = &RLVK.shaderSlots[key->shaderSlot];

    if (key->vertexLayout == RLVK_VLAYOUT_QUAD)
    {
        binds[0] = (VkVertexInputBindingDescription){.binding = 0, .stride = 5 * sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        *bindCount = 1;
        if (shader->attribLocs[RLVK_ATTRIB_POSITION] >= 0)
            attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_POSITION], .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0};
        if (shader->attribLocs[RLVK_ATTRIB_TEXCOORD] >= 0)
            attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_TEXCOORD], .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 3 * sizeof(f32)};
        rlvkAppendDummyAttribs(key->vertexLayout, shader, binds, bindCount, attrs, attrCount);
        return;
    }

    if (key->vertexLayout == RLVK_VLAYOUT_BATCH)
    {
        binds[0] = (VkVertexInputBindingDescription){.binding = 0, .stride = 3 * sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        binds[1] = (VkVertexInputBindingDescription){.binding = 1, .stride = 2 * sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        binds[2] = (VkVertexInputBindingDescription){.binding = 2, .stride = 3 * sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        binds[3] = (VkVertexInputBindingDescription){.binding = 3, .stride = 4 * sizeof(unsigned char), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        *bindCount = 4;
        if (shader->attribLocs[RLVK_ATTRIB_POSITION] >= 0)
            attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_POSITION], .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT};
        if (shader->attribLocs[RLVK_ATTRIB_TEXCOORD] >= 0)
            attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_TEXCOORD], .binding = 1, .format = VK_FORMAT_R32G32_SFLOAT};
        if (shader->attribLocs[RLVK_ATTRIB_NORMAL] >= 0)
            attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_NORMAL], .binding = 2, .format = VK_FORMAT_R32G32B32_SFLOAT};
        if (shader->attribLocs[RLVK_ATTRIB_COLOR] >= 0)
            attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_COLOR], .binding = 3, .format = VK_FORMAT_R8G8B8A8_UNORM};
        rlvkAppendDummyAttribs(key->vertexLayout, shader, binds, bindCount, attrs, attrCount);
        return;
    }

    bool hasUV = (key->vertexLayout & RLVK_VLAYOUT_MESH_UV) != 0;
    bool hasNormal = (key->vertexLayout & RLVK_VLAYOUT_MESH_NORMAL) != 0;
    bool hasColor = (key->vertexLayout & RLVK_VLAYOUT_MESH_COLOR) != 0;

    binds[0] = (VkVertexInputBindingDescription){.binding = 0, .stride = 3 * sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    binds[1] = (VkVertexInputBindingDescription){.binding = 1, .stride = hasUV ? 2 * (u32)sizeof(f32) : 0u, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    binds[2] = (VkVertexInputBindingDescription){.binding = 2, .stride = hasNormal ? 3 * (u32)sizeof(f32) : 0u, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    binds[3] = (VkVertexInputBindingDescription){.binding = 3, .stride = hasColor ? 4 * (u32)sizeof(unsigned char) : 0u, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    *bindCount = 4;

    if (shader->attribLocs[RLVK_ATTRIB_POSITION] >= 0)
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_POSITION], .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT};
    if (shader->attribLocs[RLVK_ATTRIB_TEXCOORD] >= 0)
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_TEXCOORD], .binding = 1, .format = VK_FORMAT_R32G32_SFLOAT};
    if (shader->attribLocs[RLVK_ATTRIB_NORMAL] >= 0)
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_NORMAL], .binding = 2, .format = VK_FORMAT_R32G32B32_SFLOAT};
    if (shader->attribLocs[RLVK_ATTRIB_COLOR] >= 0)
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_COLOR], .binding = 3, .format = VK_FORMAT_R8G8B8A8_UNORM};

    u32 nextBinding = 4;
    if (key->vertexLayout & RLVK_VLAYOUT_MESH_UV2)
    {
        binds[*bindCount] = (VkVertexInputBindingDescription){.binding = nextBinding, .stride = 2 * sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_TEXCOORD2], .binding = nextBinding, .format = VK_FORMAT_R32G32_SFLOAT};
        (*bindCount)++;
        nextBinding++;
    }
    if (key->vertexLayout & RLVK_VLAYOUT_MESH_TANGENT)
    {
        binds[*bindCount] = (VkVertexInputBindingDescription){.binding = nextBinding, .stride = 4 * sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_TANGENT], .binding = nextBinding, .format = VK_FORMAT_R32G32B32A32_SFLOAT};
        (*bindCount)++;
        nextBinding++;
    }
    if (key->vertexLayout & (RLVK_VLAYOUT_MESH_BONES | RLVK_VLAYOUT_MESH_BONES_DUMMY))
    {
        bool realBones = (key->vertexLayout & RLVK_VLAYOUT_MESH_BONES) != 0;
        binds[*bindCount] = (VkVertexInputBindingDescription){.binding = 4, .stride = realBones ? 4u : 0u, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_BONEIDS], .binding = 4, .format = VK_FORMAT_R8G8B8A8_USCALED};
        (*bindCount)++;
        binds[*bindCount] = (VkVertexInputBindingDescription){.binding = 5, .stride = realBones ? 4 * (u32)sizeof(f32) : 0u, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = (u32)shader->attribLocs[RLVK_ATTRIB_BONEWEIGHTS], .binding = 5, .format = VK_FORMAT_R32G32B32A32_SFLOAT};
        (*bindCount)++;
    }
    if (key->vertexLayout & RLVK_VLAYOUT_MESH_INSTANCED)
    {
        u32 base = (u32)shader->attribLocs[RLVK_ATTRIB_INSTANCE_TX];
        binds[*bindCount] = (VkVertexInputBindingDescription){.binding = 4, .stride = 16 * sizeof(f32), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE};
        for (u32 c = 0; c < 4; c++)
            attrs[(*attrCount)++] = (VkVertexInputAttributeDescription){.location = base + c, .binding = 4, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = c * 4 * sizeof(f32)};
        (*bindCount)++;
    }

    rlvkAppendDummyAttribs(key->vertexLayout, shader, binds, bindCount, attrs, attrCount);
}

static void rlvkBindDummyAttribBuffers(VkCommandBuffer cmdBuffer, unsigned short vertexLayout, rlvkShaderSlot *shader)
{
    u32 dummyBinding = 0;
    unsigned int covered = rlvkLayoutCoverage(vertexLayout, &dummyBinding);
    VkBuffer dummyBufs[8];
    VkDeviceSize dummyOffs[8];
    u32 dummyCount = 0;
    for (int i = 0; i < (int)RLVK_COUNTOF(rlvkDummyAttribs); i++)
    {
        int idx = rlvkDummyAttribs[i].attrib;
        if ((covered & (1u << idx)) || (shader->attribLocs[idx] < 0))
            continue;
        dummyBufs[dummyCount] = RLVK.bufferSlots[RLVK.dummyAttribSlot].buffer;
        dummyOffs[dummyCount] = rlvkDummyAttribs[i].dummyOffset;
        dummyCount++;
    }
    if (dummyCount > 0)
        vkCmdBindVertexBuffers(cmdBuffer, dummyBinding, dummyCount, dummyBufs, dummyOffs);
}

static VkPipeline rlvkBuildPipeline(const rlvkPipelineKey *key)
{
    VkShaderModule vertMod = RLVK.shaderSlots[key->shaderSlot].vertMod;
    VkShaderModule fragMod = RLVK.shaderSlots[key->shaderSlot].fragMod;
    if (vertMod == VK_NULL_HANDLE || fragMod == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertMod, .pName = "main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragMod, .pName = "main"},
    };

    VkVertexInputBindingDescription binds[12];
    VkVertexInputAttributeDescription attrs[16];
    u32 bindCount = 0, attrCount = 0;
    rlvkBuildVertexInput(key, binds, &bindCount, attrs, &attrCount);

    VkPipelineVertexInputStateCreateInfo vertexInput = {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = bindCount,
        .pVertexBindingDescriptions = binds,
        .vertexAttributeDescriptionCount = attrCount,
        .pVertexAttributeDescriptions = attrs,
    };

    bool blendEnabled = (key->blendMode >= 0);
    VkPipelineColorBlendAttachmentState blendAttachments[8];
    for (u32 a = 0; a < key->colorCount && a < 8; a++)
    {
        VkFormat fmt = key->colorFormats[a];
        blendAttachments[a] = rlvkGetBlendAttachmentState(key, blendEnabled);
    }

    rlvkRenderPassKey rpKey;
    memset(&rpKey, 0, sizeof(rpKey));
    for (u32 a = 0; a < key->colorCount && a < 8; a++)
        rpKey.colorFormats[a] = key->colorFormats[a];
    rpKey.depthFormat = key->depthFormat;
    rpKey.colorCount = key->colorCount;
    rpKey.samples = (key->samples > 1) ? 4 : 1;
    rpKey.colorLoad = VK_ATTACHMENT_LOAD_OP_LOAD;
    rpKey.depthLoad = VK_ATTACHMENT_LOAD_OP_LOAD;
    rpKey.depthStore = VK_ATTACHMENT_STORE_OP_STORE;
    rpKey.hasResolve = (key->samples > 1) ? 1 : 0;
    VkRenderPass compatPass = rlvkGetRenderPass(&rpKey);

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(RLVK.device, RLVK.pipelineCache, 1,
                                                &(VkGraphicsPipelineCreateInfo){
                                                    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                    .stageCount = 2,
                                                    .pStages = stages,
                                                    .pVertexInputState = &vertexInput,
                                                    .pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo){
                                                        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                        .topology = (key->topology == 0) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : (key->topology == 2) ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                                                                                                                                                  : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                    },
                                                    .pViewportState = &(VkPipelineViewportStateCreateInfo){
                                                        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                        .viewportCount = 1,
                                                        .scissorCount = 1,
                                                    },
                                                    .pRasterizationState = &(VkPipelineRasterizationStateCreateInfo){
                                                        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                        RLVK.Caps.bresenhamLines ? (const void *)&(VkPipelineRasterizationLineStateCreateInfoEXT){
                                                                                       VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT,
                                                                                       .lineRasterizationMode = (key->samples > 1) ? VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT : VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT,
                                                                                   }
                                                                                 : NULL,
                                                        .polygonMode = (VkPolygonMode)key->polygonMode,
                                                        .cullMode = (VkCullModeFlags)key->cullMode,
                                                        .frontFace = VK_FRONT_FACE_CLOCKWISE,
                                                        .lineWidth = 1.0f,
                                                    },
                                                    .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo){
                                                        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                        .rasterizationSamples = (key->samples > 1) ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
                                                    },
                                                    .pDepthStencilState = &(VkPipelineDepthStencilStateCreateInfo){
                                                        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                                                        .depthTestEnable = key->depthTest ? VK_TRUE : VK_FALSE,
                                                        .depthWriteEnable = key->depthWrite ? VK_TRUE : VK_FALSE,
                                                        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
                                                    },
                                                    .pColorBlendState = &(VkPipelineColorBlendStateCreateInfo){
                                                        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                        .attachmentCount = key->colorCount,
                                                        .pAttachments = blendAttachments,
                                                    },
                                                    .pDynamicState = &(VkPipelineDynamicStateCreateInfo){
                                                        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                        .dynamicStateCount = 2,
                                                        .pDynamicStates = (VkDynamicState[]){
                                                            VK_DYNAMIC_STATE_VIEWPORT,
                                                            VK_DYNAMIC_STATE_SCISSOR,
                                                        },
                                                    },
                                                    .layout = RLVK.pipelineLayout,
                                                    .renderPass = compatPass,
                                                    .subpass = 0,
                                                },
                                                RLVK_ALLOC, &pipeline);
    return pipeline;
}

// Get (or build and cache) the pipeline for a state key
static VkPipeline rlvkGetPipeline(const rlvkPipelineKey *key)
{
    // OPTIMIZATION: Hash-based early out to avoid expensive linear memcmp search
    u32 keyHash = rlvkHashPipelineKey(key);

    for (int i = 0; i < RLVK.pipelineCount; i++)
    {
        if (s_pipelineHashes[i] == keyHash)
        {
            if (memcmp(&RLVK.pipelines[i].key, key, sizeof(rlvkPipelineKey)) == 0)
                return RLVK.pipelines[i].pipeline;
        }
    }

    if (RLVK.pipelineCount >= RLVK_MAX_PIPELINES)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: pipeline cache table full - raise RLVK_MAX_PIPELINES");
        return VK_NULL_HANDLE;
    }

    VkPipeline pipeline = rlvkBuildPipeline(key);
    if (pipeline == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    RLVK.pipelines[RLVK.pipelineCount].key = *key;
    RLVK.pipelines[RLVK.pipelineCount].pipeline = pipeline;
    s_pipelineHashes[RLVK.pipelineCount] = keyHash; // OPTIMIZATION: Store the computed hash
    RLVK.pipelineCount++;
    return pipeline;
}

static bool rlvkBindPipeline(VkCommandBuffer cmdBuffer, unsigned char topology, unsigned short vertexLayout, u32 shaderSlot)
{
    if (s_pipelineFastValid && (RLVK.State.stateGeneration == s_lastGeneration) &&
        (shaderSlot == s_lastShaderSlot) && (vertexLayout == s_lastVertexLayout) && (topology == s_lastTopology))
        return true;

    bool scopeHasDepth = true;
    if (RLVK.scope.fbSlot != 0)
    {
        rlvkFramebufferSlot *fb = &RLVK.fbSlots[RLVK.scope.fbSlot];
        scopeHasDepth = fb->hasDepth && (RLVK.textureSlots[fb->depthTexture].image != VK_NULL_HANDLE);
    }

    rlvkPipelineKey key = {0};
    for (u32 a = 0; a < RLVK.scope.colorCount && a < 8; a++)
        key.colorFormats[a] = RLVK.scope.colorFormats[a];
    key.depthFormat = scopeHasDepth ? RLVK.depthFormat : VK_FORMAT_UNDEFINED;
    key.shaderSlot = shaderSlot;
    key.topology = topology;
    key.vertexLayout = vertexLayout;
    key.cullMode = (unsigned char)(RLVK.State.cullEnabled ? ((RLVK.State.cullMode == RL_CULL_FACE_FRONT) ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT) : VK_CULL_MODE_NONE);
    key.polygonMode = (unsigned char)((RLVK.Caps.fillModeNonSolid && RLVK.State.pointMode) ? VK_POLYGON_MODE_POINT : (RLVK.Caps.fillModeNonSolid && RLVK.State.wireMode) ? VK_POLYGON_MODE_LINE
                                                                                                                                                                         : VK_POLYGON_MODE_FILL);
    key.samples = (unsigned char)RLVK.scope.samples;
    key.colorCount = (unsigned char)RLVK.scope.colorCount;
    key.depthTest = RLVK.State.depthTest ? 1 : 0;
    key.depthWrite = RLVK.State.depthWrite ? 1 : 0;
    key.blendMode = RLVK.State.colorBlendEnabled ? RLVK.State.blendMode : -1;
    if (RLVK.State.blendMode == RL_BLEND_CUSTOM)
    {
        key.blendSrcRGB = RLVK.State.blendSrc;
        key.blendDstRGB = RLVK.State.blendDst;
        key.blendEqRGB = RLVK.State.blendEq;
    }
    else if (RLVK.State.blendMode == RL_BLEND_CUSTOM_SEPARATE)
    {
        key.blendSrcRGB = RLVK.State.blendSrcRGB;
        key.blendDstRGB = RLVK.State.blendDstRGB;
        key.blendEqRGB = RLVK.State.blendEqRGB;
        key.blendSrcA = RLVK.State.blendSrcA;
        key.blendDstA = RLVK.State.blendDstA;
        key.blendEqA = RLVK.State.blendEqA;
    }

    VkPipeline pipeline = rlvkGetPipeline(&key);
    if (pipeline == VK_NULL_HANDLE)
        return false;

    if (pipeline != RLVK.boundPipeline)
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        RLVK.boundPipeline = pipeline;
    }

    rlvkViewportSig vpSig;
    memset(&vpSig, 0, sizeof(vpSig));
    vpSig.vx = RLVK.State.viewportX;
    vpSig.vy = RLVK.State.viewportY;
    vpSig.vw = RLVK.State.viewportW;
    vpSig.vh = RLVK.State.viewportH;
    vpSig.scEn = RLVK.State.scissorEnabled ? 1 : 0;
    vpSig.scx = RLVK.State.scissorX;
    vpSig.scy = RLVK.State.scissorY;
    vpSig.scw = RLVK.State.scissorW;
    vpSig.sch = RLVK.State.scissorH;
    vpSig.scopeW = (int)RLVK.scope.width;
    vpSig.scopeH = (int)RLVK.scope.height;
    vpSig.flipY = RLVK.scope.flipY ? 1 : 0;
    s_pipelineFastValid = true;
    s_lastGeneration = RLVK.State.stateGeneration;
    s_lastShaderSlot = shaderSlot;
    s_lastVertexLayout = vertexLayout;
    s_lastTopology = topology;

    if (s_viewportValid && (memcmp(&vpSig, &s_viewportSig, sizeof(vpSig)) == 0))
        return true;
    s_viewportSig = vpSig;
    s_viewportValid = true;

    f32 vx = (f32)RLVK.State.viewportX, vy = (f32)RLVK.State.viewportY;
    f32 vw = (f32)RLVK.State.viewportW, vh = (f32)RLVK.State.viewportH;
    if (vw <= 0.0f || vh <= 0.0f)
    {
        vw = (f32)RLVK.scope.width;
        vh = (f32)RLVK.scope.height;
    }
    if (RLVK.scope.flipY)
    {
        vkCmdSetViewport(cmdBuffer, 0, 1, &(VkViewport){vx, vy + vh, vw, -vh, 0.0f, 1.0f});
    }
    else
        vkCmdSetViewport(cmdBuffer, 0, 1, &(VkViewport){vx, vy, vw, vh, 0.0f, 1.0f});

    if (RLVK.State.scissorEnabled)
    {
        int sx = RLVK.State.scissorX;
        int sy = RLVK.scope.flipY ? ((int)RLVK.scope.height - (RLVK.State.scissorY + RLVK.State.scissorH)) : RLVK.State.scissorY;
        int scw = RLVK.State.scissorW, sch = RLVK.State.scissorH;
        if (sx < 0)
        {
            scw += sx;
            sx = 0;
        }
        if (sy < 0)
        {
            sch += sy;
            sy = 0;
        }
        if (sx + scw > (int)RLVK.scope.width)
            scw = (int)RLVK.scope.width - sx;
        if (sy + sch > (int)RLVK.scope.height)
            sch = (int)RLVK.scope.height - sy;
        if (scw < 0)
            scw = 0;
        if (sch < 0)
            sch = 0;
        vkCmdSetScissor(cmdBuffer, 0, 1, &(VkRect2D){{sx, sy}, {(u32)scw, (u32)sch}});
    }
    else
        vkCmdSetScissor(cmdBuffer, 0, 1, &(VkRect2D){{0, 0}, {RLVK.scope.width, RLVK.scope.height}});

    return true;
}

static void rlvkFlipToSwapchain(VkCommandBuffer cmdBuffer)
{
    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    u32 imageIndex = RLVK.currentImageIndex;
    int w = (int)RLVK.swapchainExtent.width, h = (int)RLVK.swapchainExtent.height;

    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                          .imageMemoryBarrierCount = 2,
                                          .pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
                                              {
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                  .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                  .image = RLVK.interImage[frameIndex],
                                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                              },
                                              {
                                                  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                  .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                  .image = RLVK.swapchainImages[imageIndex],
                                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                              },
                                          },
                                      });

    vk.CmdBlitImage(cmdBuffer, RLVK.interImage[frameIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    RLVK.swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                    &(VkImageBlit){
                        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .srcOffsets = {{0, h, 0}, {w, 0, 1}},
                        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .dstOffsets = {{0, 0, 0}, {w, h, 1}},
                    },
                    VK_FILTER_NEAREST);

    vk.CmdPipelineBarrier2(cmdBuffer, &(VkDependencyInfo){
                                          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                          .imageMemoryBarrierCount = 1,
                                          .pImageMemoryBarriers = &(VkImageMemoryBarrier2){
                                              VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                              .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                                              .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                              .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                              .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                              .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                              .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                              .image = RLVK.swapchainImages[imageIndex],
                                              .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                          },
                                      });
}

// LINKAGE FIX: Xóa từ khóa static để rcore.c có thể gọi được hàm này
void rlvkFinishSwapchainImage(VkCommandBuffer cmdBuffer)
{
    u32 gpuFrame = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    bool gpuTrace = rlvkDebugFlag("RLVK_GPU_TRACE", &s_dbgGpu) && (s_gpuPool != VK_NULL_HANDLE);
    if (gpuTrace)
        vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, s_gpuPool, gpuFrame * 3 + 1);

    rlvkFlipToSwapchain(cmdBuffer);

    if (gpuTrace)
        vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, s_gpuPool, gpuFrame * 3 + 2);
}

// LINKAGE FIX: Xóa từ khóa static
bool rlvkInitFrameRing(void)
{
    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
    {
        RLVK_CHECK(vkCreateCommandPool(RLVK.device,
                                       &(VkCommandPoolCreateInfo){
                                           VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                           .queueFamilyIndex = RLVK.graphicsFamily,
                                       },
                                       RLVK_ALLOC, &RLVK.cmdPools[i]));

        RLVK_CHECK(vkAllocateCommandBuffers(RLVK.device,
                                            &(VkCommandBufferAllocateInfo){
                                                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                .commandPool = RLVK.cmdPools[i],
                                                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                .commandBufferCount = 1,
                                            },
                                            &RLVK.cmdBuffers[i]));

        if (!RLVK.Caps.pushDescriptor)
        {
            RLVK_CHECK(vkCreateDescriptorPool(RLVK.device,
                                              &(VkDescriptorPoolCreateInfo){
                                                  VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                                  .maxSets = RLVK_DESC_SETS_PER_FRAME,
                                                  .poolSizeCount = 3,
                                                  .pPoolSizes = (VkDescriptorPoolSize[]){
                                                      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RLVK_DESC_SETS_PER_FRAME * RLVK_MAX_TEXTURE_UNITS},
                                                      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, RLVK_DESC_SETS_PER_FRAME * 2},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RLVK_DESC_SETS_PER_FRAME * RLVK_SET0_SSBO_COUNT},
                                                  },
                                              },
                                              RLVK_ALLOC, &RLVK.descPools[i]));
        }
    }
    return true;
}

// LINKAGE FIX: Xóa từ khóa static
bool rlvkCreateBatchBacking(int bufferElements, rlvkBatchBackingBuffer *out)
{
    size_t posBytes = (size_t)bufferElements * 3 * 4 * sizeof(f32);
    size_t uvBytes = (size_t)bufferElements * 2 * 4 * sizeof(f32);
    size_t nrmBytes = (size_t)bufferElements * 3 * 4 * sizeof(f32);
    size_t colBytes = (size_t)bufferElements * 4 * 4 * sizeof(unsigned char);
    size_t idxBytes = (size_t)bufferElements * 6 * sizeof(unsigned int);
    VkDeviceSize total = (VkDeviceSize)(posBytes + uvBytes + nrmBytes + colBytes + idxBytes);

    RLVK_CHECK(vkCreateBuffer(RLVK.device,
                              &(VkBufferCreateInfo){
                                  VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                  .size = total,
                                  .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                              },
                              RLVK_ALLOC, &out->buffer));

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(RLVK.device, out->buffer, &memReq);

    out->memory = rlvkAllocMemory(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    RLVK_CHECK(vkBindBufferMemory(RLVK.device, out->buffer, out->memory, 0));
    RLVK_CHECK(vkMapMemory(RLVK.device, out->memory, 0, total, 0, &out->mapped));

    out->sizeBytes = (u32)total;
    return true;
}

// LINKAGE FIX: Xóa từ khóa static
void rlvkDestroyBatchBacking(rlvkBatchBackingBuffer *b)
{
    if (b->memory)
    {
        vkUnmapMemory(RLVK.device, b->memory);
        vkFreeMemory(RLVK.device, b->memory, RLVK_ALLOC);
    }
    if (b->buffer)
        vkDestroyBuffer(RLVK.device, b->buffer, RLVK_ALLOC);
    memset(b, 0, sizeof(*b));
}

static u32 rlvkFindMemoryType(u32 typeBits, VkMemoryPropertyFlags wanted)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(RLVK.physicalDevice, &props);
    for (u32 i = 0; i < props.memoryTypeCount; i++)
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & wanted) == wanted)
            return i;
    return UINT32_MAX;
}

// LINKAGE FIX: Xóa từ khóa static
VkDeviceMemory rlvkAllocMemory(VkMemoryRequirements memReq, VkMemoryPropertyFlags props)
{
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        if (rlvkFindMemoryType(memReq.memoryTypeBits, props | VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != UINT32_MAX)
            props |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }

    VkMemoryPriorityAllocateInfoEXT priorityInfo = {
        VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT,
        .priority = (props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? 1.0f : 0.5f,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    RLVK_CHECK(vkAllocateMemory(RLVK.device,
                                &(VkMemoryAllocateInfo){
                                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                    RLVK.Caps.memoryPriority ? (void *)&priorityInfo : NULL,
                                    .allocationSize = memReq.size,
                                    .memoryTypeIndex = rlvkFindMemoryType(memReq.memoryTypeBits, props),
                                },
                                RLVK_ALLOC, &memory));
    if (props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        s_memLocalBytes += (ill)memReq.size;
    else
        s_memHostBytes += (ill)memReq.size;
    s_memAllocCount++;
    return memory;
}

// LINKAGE FIX: Xóa từ khóa static
u32 rlvkAllocTextureSlot(void)
{
    for (u32 i = 1; i < RLVK_MAX_TEXTURE_SLOTS; i++)
        if (!RLVK.textureSlots[i].inUse)
        {
            RLVK.textureSlots[i].inUse = true;
            return i;
        }
    return RLVK_INVALID_SLOT;
}

// LINKAGE FIX: Xóa từ khóa static
u32 rlvkAllocShaderSlot(void)
{
    for (u32 i = 1; i < RLVK_MAX_SHADER_SLOTS; i++)
        if (!RLVK.shaderSlots[i].inUse)
        {
            RLVK.shaderSlots[i].inUse = true;
            return i;
        }
    return RLVK_INVALID_SLOT;
}

// LINKAGE FIX: Xóa từ khóa static
u32 rlvkAllocFramebufferSlot(void)
{
    for (u32 i = 1; i < RLVK_MAX_FRAMEBUFFER_SLOTS; i++)
        if (!RLVK.fbSlots[i].inUse)
        {
            RLVK.fbSlots[i].inUse = true;
            return i;
        }
    return RLVK_INVALID_SLOT;
}

// LINKAGE FIX: Xóa từ khóa static
u32 rlvkAllocBufferSlot(void)
{
    for (u32 i = 1; i < RLVK_MAX_BUFFER_SLOTS; i++)
        if (!RLVK.bufferSlots[i].inUse && (RLVK.bufferSlots[i].buffer == VK_NULL_HANDLE))
        {
            RLVK.bufferSlots[i].inUse = true;
            return i;
        }
    for (u32 i = 1; i < RLVK_MAX_BUFFER_SLOTS; i++)
        if (!RLVK.bufferSlots[i].inUse)
        {
            RLVK.bufferSlots[i].inUse = true;
            return i;
        }
    return RLVK_INVALID_SLOT;
}