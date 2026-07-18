//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Shader module management (load/unload/uniforms/samplers)
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

// Shaders management (SPIR-V shader modules consumed by the cached-pipeline draw path)
//-----------------------------------------------------------------------------------------

// Load (compile) shader and return shader id
// Stage-compilation model: GL compiles stages separately and links them later. rlvk defers
// everything to the program step - rlLoadShader just stashes a copy of the GLSL source in
// the slot; rlLoadShaderProgramCompute (and, later, rlLoadShaderProgramEx) consumes it.
unsigned int rlLoadShader(const char *code, int type)
{
    if (!isGpuReady)
    {
        TRACELOG(RL_LOG_ERROR, "rlLoadShader failed: isGpuReady is false");
        return RLVK_INVALID_SLOT;
    }
    if (!code)
    {
        TRACELOG(RL_LOG_ERROR, "rlLoadShader failed: code is NULL");
        return RLVK_INVALID_SLOT;
    }
    u32 slot = rlvkAllocShaderSlot();
    if (slot == RLVK_INVALID_SLOT)
    {
        TRACELOG(RL_LOG_ERROR, "rlLoadShader failed: rlvkAllocShaderSlot returned 0");
        return RLVK_INVALID_SLOT;
    }

    rlvkShaderSlot *s = &RLVK.shaderSlots[slot];
    size_t len = strlen(code);
    s->pendingCode = (char *)RL_MALLOC(len + 1);
    memcpy(s->pendingCode, code, len + 1);
    s->pendingType = type;
    return slot;
}

// Load shader program from code strings
// NOTE: If shader string is NULL, using default vertex/fragment shaders
unsigned int rlLoadShaderProgram(const char *vsCode, const char *fsCode)
{
    if (!isGpuReady)
        return 0;
    // shaderc loads lazily on the first custom shader: apps using only the default shader
    // never pay its module footprint or load time
    if (!RLVK.shadercCompiler && !rlvkLoadShaderc())
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: custom shaders need shaderc (not found/failed to load) - using default shader");
        return RLVK.defaultShaderSlot;
    }
    if (!vsCode)
        vsCode = rlvkDefaultVShaderCode;
    if (!fsCode)
        fsCode = rlvkDefaultFShaderCode;

    // Rename identifiers Vulkan GLSL reserves (GL 330 allows them)
    char *sanVs = rlvkSanitizeGlsl(vsCode);
    char *sanFs = rlvkSanitizeGlsl(fsCode);
    if (sanVs)
        vsCode = sanVs;
    if (sanFs)
        fsCode = sanFs;

    // gl_FragCoord needs NO wrapper: every scope rasterizes in GL's memory orientation
    // (positive viewport, final flip at present), so Vulkan's framebuffer-row gl_FragCoord.y
    // equals GL's bottom-left window y numerically.

    u32 *vsSpv = NULL, *fsSpv = NULL;
    size_t vsWords = 0, fsWords = 0;
    bool vsOk = rlvkCompileGlsl(vsCode, 0, &vsSpv, &vsWords);
    bool fsOk = vsOk && rlvkCompileGlsl(fsCode, 1, &fsSpv, &fsWords);
    if (sanVs)
        RL_FREE(sanVs);
    if (sanFs)
        RL_FREE(sanFs);
    if (!fsOk)
    {
        if (vsSpv)
            RL_FREE(vsSpv);
        return RLVK.defaultShaderSlot;
    }

    // Force raylib's canonical attribute locations (the Vulkan glBindAttribLocation), reflect the
    // VS, then rewrite the FS's input locations to match the VS outputs BY NAME (GL link rules)
    rlvkCanonicalizeInputLocations(vsSpv, vsWords);
    // Storage buffers: rebase GLSL bindings 0..3 to set0's SSBO range (18..21) in both stages;
    // read-only (NonWritable injected) when the device lacks vertexPipelineStoresAndAtomics
    u32 vsSsboMask = 0, fsSsboMask = 0;
    rlvkRebaseStorageBuffers(&vsSpv, &vsWords, !RLVK.Caps.graphicsSsboStores, &vsSsboMask);
    rlvkRebaseStorageBuffers(&fsSpv, &fsWords, !RLVK.Caps.graphicsSsboStores, &fsSsboMask);
    if (getenv("RLVK_DUMP_SPV") && vsSsboMask)   // debug: post-rebase module for spirv-dis
    {
        char path[256]; snprintf(path, sizeof(path), "%s/rlvk_rebased_vs.spv", getenv("RLVK_DUMP_SPV"));
        FILE *df = fopen(path, "wb"); if (df) { fwrite(vsSpv, 4, vsWords, df); fclose(df); }
    }
    rlvkSpvReflection vsRef, fsRef;
    rlvkReflectSpv(vsSpv, vsWords, &vsRef);
    rlvkMatchStageInterface(&fsSpv, &fsWords, &vsRef);
    rlvkReflectSpv(fsSpv, fsWords, &fsRef);

    // Compile both stages to VkShaderModules; pipelines consume them at creation time
    VkShaderModule vsMod = VK_NULL_HANDLE, fsMod = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = vsWords * 4;
    smi.pCode = vsSpv;
    VkResult r = vkCreateShaderModule(RLVK.device, &smi, RLVK_ALLOC, &vsMod);

    smi.codeSize = fsWords * 4;
    smi.pCode = fsSpv;
    if (r == VK_SUCCESS)
        r = vkCreateShaderModule(RLVK.device, &smi, RLVK_ALLOC, &fsMod);

    RL_FREE(vsSpv);
    RL_FREE(fsSpv);

    if (r != VK_SUCCESS)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: vkCreateShaderModule (custom shader) => %d", (int)r);
        // VÁ LỖI LOGIC: Dọn dẹp module đã tạo nếu một trong hai thất bại
        if (vsMod)
            vkDestroyShaderModule(RLVK.device, vsMod, RLVK_ALLOC);
        if (fsMod)
            vkDestroyShaderModule(RLVK.device, fsMod, RLVK_ALLOC);
        return RLVK.defaultShaderSlot;
    }

    u32 slot = rlvkAllocShaderSlot();
    if (slot == RLVK_INVALID_SLOT)
    {
        // VÁ LỖI LOGIC: Tránh rò rỉ khi hết slot trống
        vkDestroyShaderModule(RLVK.device, vsMod, RLVK_ALLOC);
        vkDestroyShaderModule(RLVK.device, fsMod, RLVK_ALLOC);
        return RLVK.defaultShaderSlot;
    }

    rlvkShaderSlot *shader = &RLVK.shaderSlots[slot];
    shader->vertMod = vsMod;
    shader->fragMod = fsMod;
    for (int i = 0; i < RL_MAX_SHADER_LOCATIONS; i++)
        shader->locs[i] = -1;
    for (int i = 0; i < RLVK_MAX_TEXTURE_UNITS; i++)
    {
        shader->bindingUnit[i] = i;
        shader->bindingTexture[i] = 0;
    }
    for (int i = 0; i < RLVK_ATTRIB_COUNT; i++)
        shader->attribLocs[i] = -1;

    // Merge the two stages' reflections into one uniform table ("location" = table index)
    shader->uniforms = (rlvkUniform *)RL_CALLOC(RLVK_MAX_SHADER_UNIFORMS, sizeof(rlvkUniform));
    shader->uniformCount = 0;
    for (int stage = 0; stage < 2; stage++)
    {
        rlvkSpvReflection *ref = stage ? &fsRef : &vsRef;
        for (int m = 0; m < ref->memberCount; m++)
        {
            int found = -1;
            for (int u = 0; u < shader->uniformCount; u++)
                if (strcmp(shader->uniforms[u].name, ref->members[m].name) == 0)
                {
                    found = u;
                    break;
                }
            if (found < 0 && shader->uniformCount < RLVK_MAX_SHADER_UNIFORMS)
            {
                found = shader->uniformCount++;
                strncpy(shader->uniforms[found].name, ref->members[m].name, 63);
                shader->uniforms[found].name[63] = '\0'; // VÁ LỖI OVERFLOW
                shader->uniforms[found].vsOffset = -1;
                shader->uniforms[found].fsOffset = -1;
                shader->uniforms[found].samplerBinding = -1;
            }
            if (found >= 0)
            {
                if (stage)
                    shader->uniforms[found].fsOffset = (int)ref->members[m].offset;
                else
                    shader->uniforms[found].vsOffset = (int)ref->members[m].offset;
            }
        }
        for (int sm = 0; sm < ref->samplerCount; sm++)
        {
            int found = -1;
            for (int u = 0; u < shader->uniformCount; u++)
                if (strcmp(shader->uniforms[u].name, ref->samplers[sm].name) == 0)
                {
                    found = u;
                    break;
                }
            if (found < 0 && shader->uniformCount < RLVK_MAX_SHADER_UNIFORMS)
            {
                found = shader->uniformCount++;
                strncpy(shader->uniforms[found].name, ref->samplers[sm].name, 63);
                shader->uniforms[found].name[63] = '\0'; // VÁ LỖI OVERFLOW
                shader->uniforms[found].vsOffset = -1;
                shader->uniforms[found].fsOffset = -1;
                shader->uniforms[found].samplerBinding = -1;
            }
            if (found >= 0)
                shader->uniforms[found].samplerBinding = ref->samplers[sm].binding;
        }
    }
    // Vertex attribute locations by canonical raylib name
    for (int a = 0; a < vsRef.inputCount; a++)
    {
        int c = rlvkCanonicalAttribIndex(vsRef.inputs[a].name);
        if (c >= 0)
            shader->attribLocs[c] = vsRef.inputs[a].location;
    }

    // Per-stage default-uniform-block staging
    shader->vsBlockSize = vsRef.hasBlock ? vsRef.blockSize : 0;
    shader->fsBlockSize = fsRef.hasBlock ? fsRef.blockSize : 0;
    shader->vsStage = shader->vsBlockSize ? (unsigned char *)RL_CALLOC(1, shader->vsBlockSize) : NULL;
    shader->fsStage = shader->fsBlockSize ? (unsigned char *)RL_CALLOC(1, shader->fsBlockSize) : NULL;
    shader->usesUbo = true;
    shader->ssboMask = vsSsboMask | fsSsboMask;
    if (getenv("RLVK_DEBUG_SSBO")) TRACELOG(RL_LOG_WARNING, "VKSSBO compile slot=%u vsMask=0x%x fsMask=0x%x", slot, vsSsboMask, fsSsboMask);

    TRACELOG(RL_LOG_INFO, "RLVK: [ID %u] shader program compiled (%d uniforms, VS block %uB at %u, FS block %uB at %u)",
             slot, shader->uniformCount, shader->vsBlockSize, vsRef.blockBinding, shader->fsBlockSize, fsRef.blockBinding);
    return slot;
}

// Load shader program from already loaded shader ids
unsigned int rlLoadShaderProgramEx(unsigned int vsId, unsigned int fsId)
{
    (void)vsId;
    (void)fsId;
    return RLVK_INVALID_SLOT;
}

// Lazily create the fixed compute set-0 layout + pipeline layout + per-frame pools
static bool rlvkInitComputeLayout(void)
{
    if (RLVK.computePipelineLayout != VK_NULL_HANDLE)
        return true;

    // NOTE: NO storage-image bindings. Empirically (raw-Vulkan bisect on MoltenVK 1.2.11 /
    // Intel Iris 6000): merely DECLARING storage-image bindings 8..11 in this layout makes
    // the UBO at binding 14 read as zeros in the shader, even when the images are never
    // written or statically used. rlBindImageTexture is a recorded no-op until a dedicated
    // storage-image path exists (textures lack STORAGE usage anyway); when that lands, put
    // the images in their own descriptor SET rather than re-adding them here.
    VkDescriptorSetLayoutBinding bindings[11];
    for (u32 b = 0; b < 8; b++)
        bindings[b] = (VkDescriptorSetLayoutBinding){.binding = b, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
    bindings[8] = (VkDescriptorSetLayoutBinding){.binding = 12, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
    bindings[9] = (VkDescriptorSetLayoutBinding){.binding = 13, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
    bindings[10] = (VkDescriptorSetLayoutBinding){.binding = 14, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
    RLVK_CHECK(vkCreateDescriptorSetLayout(RLVK.device,
                                           &(VkDescriptorSetLayoutCreateInfo){
                                               VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                               .bindingCount = 11,
                                               .pBindings = bindings,
                                           },
                                           RLVK_ALLOC, &RLVK.computeSetLayout));
    RLVK_CHECK(vkCreatePipelineLayout(RLVK.device,
                                      &(VkPipelineLayoutCreateInfo){
                                          VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                          .setLayoutCount = 1,
                                          .pSetLayouts = &RLVK.computeSetLayout,
                                      },
                                      RLVK_ALLOC, &RLVK.computePipelineLayout));
    for (int i = 0; i < RLVK_FRAME_INDEX_COUNT; i++)
        RLVK_CHECK(vkCreateDescriptorPool(RLVK.device,
                                          &(VkDescriptorPoolCreateInfo){
                                              VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                              .maxSets = RLVK_COMPUTE_SETS_PER_FRAME,
                                              .poolSizeCount = 3,
                                              .pPoolSizes = (VkDescriptorPoolSize[]){
                                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RLVK_COMPUTE_SETS_PER_FRAME * 8},
                                                  {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RLVK_COMPUTE_SETS_PER_FRAME * 2},
                                                  {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, RLVK_COMPUTE_SETS_PER_FRAME},
                                              },
                                          },
                                          RLVK_ALLOC, &RLVK.computeDescPools[i]));
    return true;
}

// Load compute shader program: compile the stashed GLSL (shaderc, Vulkan 1.1 target),
// reflect the loose-uniform block, build the monolithic compute pipeline. Returns csId
// itself - the stage slot becomes the program slot, mirroring GL id semantics closely
// enough for raylib's LoadComputeShaderProgram flow.
unsigned int rlLoadShaderProgramCompute(unsigned int csId)
{
    if (!isGpuReady || csId == 0 || csId >= RLVK_MAX_SHADER_SLOTS)
        return RLVK_INVALID_SLOT;
    rlvkShaderSlot *shader = &RLVK.shaderSlots[csId];
    if (!shader->inUse || !shader->pendingCode)
        return RLVK_INVALID_SLOT;
    if (!RLVK.shadercCompiler && !rlvkLoadShaderc())
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: compute shader needs shaderc (GLSL compiler) - not available");
        return RLVK_INVALID_SLOT;
    }
    if (!rlvkInitComputeLayout())
        return RLVK_INVALID_SLOT;

    char *sanitized = rlvkSanitizeGlsl(shader->pendingCode);
    u32 *spv = NULL;
    size_t words = 0;
    bool ok = rlvkCompileGlsl(sanitized ? sanitized : shader->pendingCode, 2, &spv, &words);
    if (sanitized)
        RL_FREE(sanitized);
    RL_FREE(shader->pendingCode);
    shader->pendingCode = NULL;
    if (!ok)
        return RLVK_INVALID_SLOT;

    // Reflect the implicit loose-uniform block (binding 14) + samplers for rlGetLocationUniform
    rlvkSpvReflection ref;
    rlvkReflectSpv(spv, words, &ref);

    VkResult r = vkCreateShaderModule(RLVK.device,
                                      &(VkShaderModuleCreateInfo){VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                                                  .codeSize = words * 4, .pCode = spv},
                                      RLVK_ALLOC, &shader->compMod);
    RL_FREE(spv);
    if (r != VK_SUCCESS)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: vkCreateShaderModule (compute) => %d", (int)r);
        return RLVK_INVALID_SLOT;
    }

    r = vkCreateComputePipelines(RLVK.device, RLVK.pipelineCache, 1,
                                 &(VkComputePipelineCreateInfo){
                                     VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                     .stage = (VkPipelineShaderStageCreateInfo){
                                         VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                         .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader->compMod, .pName = "main"},
                                     .layout = RLVK.computePipelineLayout,
                                 },
                                 RLVK_ALLOC, &shader->computePipeline);

    if (r != VK_SUCCESS)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: vkCreateComputePipelines => %d", (int)r);
        // VÁ LỖI LOGIC: Dọn dẹp compMod nếu tạo Pipeline thất bại
        vkDestroyShaderModule(RLVK.device, shader->compMod, RLVK_ALLOC);
        shader->compMod = VK_NULL_HANDLE;
        return RLVK_INVALID_SLOT;
    }

    for (int i = 0; i < RL_MAX_SHADER_LOCATIONS; i++)
        shader->locs[i] = -1;
    for (int i = 0; i < RLVK_MAX_TEXTURE_UNITS; i++)
    {
        shader->bindingUnit[i] = i;
        shader->bindingTexture[i] = 0;
    }
    shader->uniforms = (rlvkUniform *)RL_CALLOC(RLVK_MAX_SHADER_UNIFORMS, sizeof(rlvkUniform));
    shader->uniformCount = 0;
    for (int m = 0; m < ref.memberCount && shader->uniformCount < RLVK_MAX_SHADER_UNIFORMS; m++)
    {
        rlvkUniform *u = &shader->uniforms[shader->uniformCount++];
        strncpy(u->name, ref.members[m].name, 63);
        u->vsOffset = (int)ref.members[m].offset; // compute block rides the VS staging fields
        u->fsOffset = -1;
        u->samplerBinding = -1;
    }
    for (int sm = 0; sm < ref.samplerCount && shader->uniformCount < RLVK_MAX_SHADER_UNIFORMS; sm++)
    {
        rlvkUniform *u = &shader->uniforms[shader->uniformCount++];
        strncpy(u->name, ref.samplers[sm].name, 63);
        u->vsOffset = -1;
        u->fsOffset = -1;
        u->samplerBinding = ref.samplers[sm].binding;
    }
    shader->vsBlockSize = ref.hasBlock ? ref.blockSize : 0;
    shader->vsStage = shader->vsBlockSize ? (unsigned char *)RL_CALLOC(1, shader->vsBlockSize) : NULL;
    shader->usesUbo = (shader->vsBlockSize > 0);
    shader->isCompute = true;

    TRACELOG(RL_LOG_INFO, "RLVK: [ID %u] compute program compiled (%d uniforms, block %uB at binding %u)",
             csId, shader->uniformCount, shader->vsBlockSize, ref.blockBinding);
    return csId;
}

// Delete shader
void rlUnloadShader(unsigned int id)
{
    if (id == 0 || id >= RLVK_MAX_SHADER_SLOTS)
        return;
    rlvkShaderSlot *s = &RLVK.shaderSlots[id];
    if (!s->inUse)
        return;
    // GL semantics: glDeleteShader on a stage object AFTER linking is harmless - the linked
    // program lives on. In rlvk the compute STAGE slot becomes the PROGRAM slot
    // (rlLoadShaderProgramCompute returns csId itself), so honoring the delete here would
    // destroy the live program AND free the slot for the next shader load to recycle -
    // rlEnableShader(prog) then activates an unrelated shader and rlComputeShaderDispatch
    // silently no-ops (frozen particles; found via the GPU-particle hunt). Ignore the
    // stage-handle delete; rlUnloadShaderProgram() performs the real destroy.
    if (s->isCompute && (s->computePipeline != VK_NULL_HANDLE))
    {
        TRACELOG(RL_LOG_DEBUG, "RLVK: rlUnloadShader(%u) on a linked compute program - ignored (GL stage-delete semantics)", id);
        return;
    }

    // Evict this slot's cached pipelines (slot numbers recycle; a stale pipeline would draw
    // the next shader with this one's modules): pipelines go through the fence-gated dead ring,
    // modules destroy immediately (spec-legal after pipeline creation)
    for (int i = RLVK.pipelineCount - 1; i >= 0; i--)
    {
        if (RLVK.pipelines[i].key.shaderSlot != id)
            continue;
        if (RLVK.pipelines[i].pipeline == RLVK.boundPipeline)
            RLVK.boundPipeline = VK_NULL_HANDLE;
        rlvkDeferDestroy(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, RLVK.pipelines[i].pipeline);
        RLVK.pipelines[i] = RLVK.pipelines[RLVK.pipelineCount - 1];
        RLVK.pipelineCount--;
    }
    s_pipelineFastValid = false;
    if (s->vertMod)
        vkDestroyShaderModule(RLVK.device, s->vertMod, RLVK_ALLOC);
    if (s->fragMod)
        vkDestroyShaderModule(RLVK.device, s->fragMod, RLVK_ALLOC);
    if (s->compMod)
        vkDestroyShaderModule(RLVK.device, s->compMod, RLVK_ALLOC);
    if (s->computePipeline)
        rlvkDeferDestroy(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, s->computePipeline);
    if (s->pendingCode)
        RL_FREE(s->pendingCode);
    if (s->uniforms)
        RL_FREE(s->uniforms);
    if (s->vsStage)
        RL_FREE(s->vsStage);
    if (s->fsStage)
        RL_FREE(s->fsStage);
    memset(s, 0, sizeof(rlvkShaderSlot));
}
// Unload shader program: the REAL destroy for compute programs (rlUnloadShader ignores
// them per GL stage-delete semantics - see the guard there). Clears the guard, then reuses
// rlUnloadShader's full teardown (pipeline eviction + modules + tables).
void rlUnloadShaderProgram(unsigned int id)
{
    if (id != 0 && id < RLVK_MAX_SHADER_SLOTS && RLVK.shaderSlots[id].inUse)
        RLVK.shaderSlots[id].isCompute = false;   // drop the stage-delete protection
    rlUnloadShader(id);
}

// Get shader location uniform
// NOTE: First parameter refers to shader program id
int rlGetLocationUniform(unsigned int id, const char *name)
{
    if (id == 0 || id >= RLVK_MAX_SHADER_SLOTS || !name)
        return -1;
    rlvkShaderSlot *shader = &RLVK.shaderSlots[id];
    for (int i = 0; i < shader->uniformCount; i++)
        if (strcmp(shader->uniforms[i].name, name) == 0)
            return i;
    return -1;
}
// Get shader location attribute
// NOTE: First parameter refers to shader program id
int rlGetLocationAttrib(unsigned int id, const char *name)
{
    if (id == 0 || id >= RLVK_MAX_SHADER_SLOTS || !name)
        return -1;
    int c = rlvkCanonicalAttribIndex(name);
    return (c >= 0) ? RLVK.shaderSlots[id].attribLocs[c] : -1;
}

// Set shader value uniform
void rlSetUniform(int loc, const void *value, int uniformType, int count)
{
    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];
    if (!shader->usesUbo)
    {
        if (loc == RLVK_ULOC_COLDIFFUSE && value)
            memcpy(RLVK.State.meshColDiffuse, value, sizeof(f32) * 4);
        return;
    }
    if (loc < 0 || loc >= shader->uniformCount || !value)
        return;
    if (shader->uniforms[loc].samplerBinding >= 0 &&
        (uniformType == RL_SHADER_UNIFORM_INT || uniformType == RL_SHADER_UNIFORM_SAMPLER2D))
    {
        int b = shader->uniforms[loc].samplerBinding;
        if (b >= 0 && b < RLVK_MAX_TEXTURE_UNITS)
            shader->bindingUnit[b] = *(const int *)value;
        return;
    }

    u32 elemBytes = 0;
    switch (uniformType)
    {
    case RL_SHADER_UNIFORM_FLOAT:
    case RL_SHADER_UNIFORM_INT:
    case RL_SHADER_UNIFORM_UINT:
        elemBytes = 4;
        break;
    case RL_SHADER_UNIFORM_VEC2:
    case RL_SHADER_UNIFORM_IVEC2:
    case RL_SHADER_UNIFORM_UIVEC2:
        elemBytes = 8;
        break;
    case RL_SHADER_UNIFORM_VEC3:
    case RL_SHADER_UNIFORM_IVEC3:
    case RL_SHADER_UNIFORM_UIVEC3:
        elemBytes = 12;
        break;
    case RL_SHADER_UNIFORM_VEC4:
    case RL_SHADER_UNIFORM_IVEC4:
    case RL_SHADER_UNIFORM_UIVEC4:
        elemBytes = 16;
        break;
    case RL_SHADER_UNIFORM_SAMPLER2D:
    {
        int b = shader->uniforms[loc].samplerBinding;
        if (b >= 0 && b < RLVK_MAX_TEXTURE_UNITS)
            shader->bindingUnit[b] = *(const int *)value;
        return;
    }
    default:
        return;
    }

    if (count <= 1)
        rlvkShaderWriteUniform(shader, loc, value, elemBytes);
    else
    {
        // TỐI ƯU HIỆU NĂNG: Kéo Bounds Checking ra khỏi vòng lặp và dùng pointer direct access
        rlvkUniform *u = &shader->uniforms[loc];
        bool writeVs = (u->vsOffset >= 0 && shader->vsStage && (u32)u->vsOffset + (count - 1) * 16 + elemBytes <= shader->vsBlockSize);
        bool writeFs = (u->fsOffset >= 0 && shader->fsStage && (u32)u->fsOffset + (count - 1) * 16 + elemBytes <= shader->fsBlockSize);

        if (writeVs || writeFs)
        {
            unsigned char *vsPtr = writeVs ? (shader->vsStage + u->vsOffset) : NULL;
            unsigned char *fsPtr = writeFs ? (shader->fsStage + u->fsOffset) : NULL;
            const unsigned char *src = (const unsigned char *)value;

            for (int i = 0; i < count; i++)
            {
                if (writeVs)
                {
                    memcpy(vsPtr, src, elemBytes);
                    vsPtr += 16;
                }
                if (writeFs)
                {
                    memcpy(fsPtr, src, elemBytes);
                    fsPtr += 16;
                }
                src += elemBytes;
            }

            if (writeVs)
                shader->vsWriteGen++;
            if (writeFs)
                shader->fsWriteGen++;
        }
    }
}
// Set shader value uniform matrix
void rlSetUniformMatrix(int loc, Matrix mat)
{
    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];
    if (!shader->usesUbo)
    {
        if (loc == RLVK_ULOC_MVP)
            RLVK.State.meshMVP = mat; // fallback DrawMesh capture
        return;
    }
    // rlMatrixToFloat order (column-major) == std140 mat4 memory layout
    f32 f[16] = {
        mat.m0, mat.m1, mat.m2, mat.m3, mat.m4, mat.m5, mat.m6, mat.m7,
        mat.m8, mat.m9, mat.m10, mat.m11, mat.m12, mat.m13, mat.m14, mat.m15};
    rlvkShaderWriteUniform(shader, loc, f, sizeof(f));
}
// Set shader value uniform matrix
void rlSetUniformMatrices(int loc, const Matrix *mat, int count)
{
    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];
    if (!shader->usesUbo || loc < 0 || loc >= shader->uniformCount || !mat || count <= 0)
        return;

    rlvkUniform *u = &shader->uniforms[loc];

    // TỐI ƯU HIỆU NĂNG: Tính toán độ an toàn của vùng nhớ 1 lần duy nhất cho toàn bộ mảng
    bool writeVs = (u->vsOffset >= 0 && shader->vsStage && (u32)u->vsOffset + count * 64 <= shader->vsBlockSize);
    bool writeFs = (u->fsOffset >= 0 && shader->fsStage && (u32)u->fsOffset + count * 64 <= shader->fsBlockSize);

    if (!writeVs && !writeFs)
        return;

    unsigned char *vsPtr = writeVs ? (shader->vsStage + u->vsOffset) : NULL;
    unsigned char *fsPtr = writeFs ? (shader->fsStage + u->fsOffset) : NULL;

    for (int i = 0; i < count; i++)
    {
        // Thực hiện transpose thành định dạng std140 column-major
        f32 f[16] = {
            mat[i].m0, mat[i].m1, mat[i].m2, mat[i].m3,
            mat[i].m4, mat[i].m5, mat[i].m6, mat[i].m7,
            mat[i].m8, mat[i].m9, mat[i].m10, mat[i].m11,
            mat[i].m12, mat[i].m13, mat[i].m14, mat[i].m15};

        if (writeVs)
        {
            memcpy(vsPtr, f, 64);
            vsPtr += 64;
        }
        if (writeFs)
        {
            memcpy(fsPtr, f, 64);
            fsPtr += 64;
        }
    }

    if (writeVs)
        shader->vsWriteGen++;
    if (writeFs)
        shader->fsWriteGen++;
}
// Set shader value uniform sampler
void rlSetUniformSampler(int loc, unsigned int textureId)
{
    rlvkShaderSlot *shader = &RLVK.shaderSlots[RLVK.State.activeShaderSlot];
    if (!shader->usesUbo || loc < 0 || loc >= shader->uniformCount)
        return;
    int b = shader->uniforms[loc].samplerBinding;
    if (b < 0 || b >= RLVK_MAX_TEXTURE_UNITS)
        return;

    // Some callers pass a texture UNIT index (rlSetUniformSampler(loc, 1) after binding at unit 1
    // - GL resolves this by binding-model coincidence): a small value naming a unit that has a
    // texture bound aliases that unit LIVE. Anything else is a texture id, bound directly.
    if (textureId >= 1 && textureId <= 4 && RLVK.State.activeTextureSlots[textureId] != 0)
    {
        shader->bindingUnit[b] = (int)textureId;
        shader->bindingTexture[b] = 0;
    }
    else
    {
        shader->bindingTexture[b] = textureId;
    }
}

// Set shader currently active (id and locations)
void rlSetShader(unsigned int id, int *locs)
{
    u32 slot = (id == 0) ? RLVK.defaultShaderSlot : id;
    if (RLVK.State.currentShaderSlot != slot)
        rlDrawRenderBatch(RLVK.currentBatch); // flush old-shader geometry (mirrors rlgl)
    if (locs && id != 0 && id < RLVK_MAX_SHADER_SLOTS)
        memcpy(RLVK.shaderSlots[id].locs, locs, sizeof(int) * RL_MAX_SHADER_LOCATIONS);
    RLVK.State.currentShaderSlot = slot;
    RLVK.State.activeShaderSlot = slot;
    RLVK.State.currentShaderLocs = (id != 0 && id < RLVK_MAX_SHADER_SLOTS) ? RLVK.shaderSlots[id].locs : RLVK.defaultShaderLocs;
}
