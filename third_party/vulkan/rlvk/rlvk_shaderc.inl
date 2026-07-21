//----------------------------------------------------------------------------------
// rlvk.h implementation fragment: Runtime GLSL->SPIR-V compile (shaderc) + SPIR-V uniform reflection
//
// Part of the rlvk single-header backend. NOT a standalone header - it is textually
// included by rlvk.h inside the ONE RLVK_IMPLEMENTATION translation unit. No include
// guard: order is fixed by the #include chain in rlvk.h. Do not #include directly.
//----------------------------------------------------------------------------------

//----------------------------------------------------------------------------------
// Runtime GLSL compilation (shaderc, relaxed Vulkan rules) + SPIR-V uniform reflection.
// shaderc_shared.dll loads at RUNTIME (MSVC static libs are not MinGW-linkable); relaxed rules
// accept stock GL-dialect GLSL, reflection recovers glGetUniformLocation/glUniform* semantics
//----------------------------------------------------------------------------------

#define RLVK_SHADERC_FUNCS                                           \
    RLVK_SC_FUNC(shaderc_compiler_initialize)                        \
    RLVK_SC_FUNC(shaderc_compile_options_initialize)                 \
    RLVK_SC_FUNC(shaderc_compile_options_release)                    \
    RLVK_SC_FUNC(shaderc_compile_options_set_target_env)             \
    RLVK_SC_FUNC(shaderc_compile_options_set_auto_bind_uniforms)     \
    RLVK_SC_FUNC(shaderc_compile_options_set_auto_map_locations)     \
    RLVK_SC_FUNC(shaderc_compile_options_set_vulkan_rules_relaxed)   \
    RLVK_SC_FUNC(shaderc_compile_options_set_optimization_level)     \
    RLVK_SC_FUNC(shaderc_compile_options_set_generate_debug_info)    \
    RLVK_SC_FUNC(shaderc_compiler_release)                           \
    RLVK_SC_FUNC(shaderc_compile_options_set_binding_base_for_stage) \
    RLVK_SC_FUNC(shaderc_compile_into_spv)                           \
    RLVK_SC_FUNC(shaderc_result_get_compilation_status)              \
    RLVK_SC_FUNC(shaderc_result_get_bytes)                           \
    RLVK_SC_FUNC(shaderc_result_get_length)                          \
    RLVK_SC_FUNC(shaderc_result_get_error_message)                   \
    RLVK_SC_FUNC(shaderc_result_release)

#define RLVK_SC_FUNC(_func) static __typeof__(_func) *p_##_func;
RLVK_SHADERC_FUNCS
#undef RLVK_SC_FUNC

// Load the shaderc shared library at runtime and resolve the compiler entry points
static bool rlvkLoadShaderc(void)
{
#if defined(_WIN32)
    void *lib = LoadLibraryA("shaderc_shared.dll");
    if (!lib)
        return false;
#define RLVK_SC_FUNC(_func)                                       \
    p_##_func = (__typeof__(_func) *)GetProcAddress(lib, #_func); \
    if (!p_##_func)                                               \
        return false;
    RLVK_SHADERC_FUNCS
#undef RLVK_SC_FUNC
#elif defined(__ANDROID__)
    // The NDK ships shaderc only as SOURCE (Android.mk for ndk-build) - there is no prebuilt
    // .so to dlopen on-device, unlike desktop/Linux distros. Makefile.Android's
    // compile_shaderc_android target stages a copy of that source, applies
    // scripts/rlvk_patch_shaderc.py (adds the shaderc_compile_options_set_vulkan_rules_relaxed
    // C-API wrapper the NDK's bundled version lacks - the underlying glslang feature was
    // already there), builds libshaderc_combined.a via ndk-build, and links it straight into
    // lib<project>.so. Since the symbols are already resolved by the linker at build time,
    // just point the function pointers at the real (statically-linked) functions - no
    // dlopen/dlsym, and this branch cannot fail at runtime the way a missing shared lib could.
#define RLVK_SC_FUNC(_func) p_##_func = _func;
    RLVK_SHADERC_FUNCS
#undef RLVK_SC_FUNC
#else
    // Linux/macOS: try the common sonames (desktop SDK/distro builds ship libshaderc_shared)
    static const char *names[] = {
        "libshaderc_shared.so.1",
        "libshaderc_shared.so",
        "libshaderc.so",
        "libshaderc_shared.dylib",
        "libshaderc_shared.1.dylib",
    };
    void *lib = NULL;
    for (size_t i = 0; (i < RLVK_COUNTOF(names)) && !lib; i++)
        lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
    if (!lib)
        return false;
#define RLVK_SC_FUNC(_func)                              \
    p_##_func = (__typeof__(_func) *)dlsym(lib, #_func); \
    if (!p_##_func)                                      \
        return false;
    RLVK_SHADERC_FUNCS
#undef RLVK_SC_FUNC
#endif
    RLVK.shadercCompiler = p_shaderc_compiler_initialize();
    return RLVK.shadercCompiler != NULL;
}

// Stock raylib default shader, GLSL 330 (mirrors rlgl.h's defaultVShaderCode/defaultFShaderCode)
static const char *rlvkDefaultVShaderCode =
    "#version 330                       \n"
    "in vec3 vertexPosition;            \n"
    "in vec2 vertexTexCoord;            \n"
    "in vec4 vertexColor;               \n"
    "out vec2 fragTexCoord;             \n"
    "out vec4 fragColor;                \n"
    "uniform mat4 mvp;                  \n"
    "void main()                        \n"
    "{                                  \n"
    "    fragTexCoord = vertexTexCoord; \n"
    "    fragColor = vertexColor;       \n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0); \n"
    "}                                  \n";

static const char *rlvkDefaultFShaderCode =
    "#version 330       \n"
    "in vec2 fragTexCoord;              \n"
    "in vec4 fragColor;                 \n"
    "out vec4 finalColor;               \n"
    "uniform sampler2D texture0;        \n"
    "uniform vec4 colDiffuse;           \n"
    "void main()                        \n"
    "{                                  \n"
    "    vec4 texelColor = texture(texture0, fragTexCoord);   \n"
    "    finalColor = texelColor*colDiffuse*fragColor;        \n"
    "}                                  \n";

// GL GLSL 330 allows identifiers that Vulkan GLSL reserves ("sampler" as a struct member in
// fog.fs etc.). Rename whole-word occurrences so relaxed compilation accepts stock shaders.
// Returns NULL when nothing needed renaming; else an RL_MALLOC'd rewritten copy.
static char *rlvkSanitizeGlsl(const char *src)
{
    static const char *bad = "sampler";
    static const char *fix = "sampler_";
    size_t badLen = strlen(bad), fixLen = strlen(fix);
    size_t n = 0;

    for (const char *c = strstr(src, bad); c; c = strstr(c + badLen, bad))
    {
        char prev = (c == src) ? 0 : c[-1], next = c[badLen];
        bool prevId = (prev == '_') || isalnum((unsigned char)prev);
        bool nextId = (next == '_') || isalnum((unsigned char)next);
        if (!prevId && !nextId)
            n++;
    }
    if (n == 0)
        return NULL;

    char *out = (char *)RL_MALLOC(strlen(src) + n * (fixLen - badLen) + 1);
    char *w = out;
    const char *r = src;
    const char *nextMatch;

    while ((nextMatch = strstr(r, bad)) != NULL)
    {
        size_t chunk = nextMatch - r;
        memcpy(w, r, chunk);
        w += chunk;
        r = nextMatch;

        char prev = (r == src) ? 0 : r[-1], next = r[badLen];
        bool prevId = (prev == '_') || isalnum((unsigned char)prev);
        bool nextId = (next == '_') || isalnum((unsigned char)next);

        if (!prevId && !nextId)
        {
            memcpy(w, fix, fixLen);
            w += fixLen;
            r += badLen;
        }
        else
        {
            *w++ = *r++;
        }
    }
    strcpy(w, r);
    return out;
}

// Vulkan clip-z is [0,1] while GL-dialect shaders assume [-1,1]: rename the user's main()
// (whole-word: GLSL has no other legal whole-word `main` outside comments, where a rename is
// harmless) and append a wrapper that remaps gl_Position.z after it runs. One behavior on
// every device - the embedded default shader bakes the same epilogue; depth_clip_control is
// deliberately not used. Returns an RL_MALLOC'd rewritten copy.
static char *rlvkInjectClipZEpilogue(const char *src)
{
    static const char *epilogue =
        "\nvoid main() { rlvk_main_(); gl_Position.z = (gl_Position.z + gl_Position.w)*0.5; }\n";
    static const char *bad = "main";
    static const char *fix = "rlvk_main_";
    size_t badLen = strlen(bad), fixLen = strlen(fix);
    size_t n = 0;

    for (const char *c = strstr(src, bad); c; c = strstr(c + badLen, bad))
    {
        char prev = (c == src) ? 0 : c[-1], next = c[badLen];
        bool prevId = (prev == '_') || isalnum((unsigned char)prev);
        bool nextId = (next == '_') || isalnum((unsigned char)next);
        if (!prevId && !nextId)
            n++;
    }

    char *out = (char *)RL_MALLOC(strlen(src) + n * (fixLen - badLen) + strlen(epilogue) + 1);
    char *w = out;
    const char *r = src;
    const char *nextMatch;

    while ((nextMatch = strstr(r, bad)) != NULL)
    {
        size_t chunk = nextMatch - r;
        memcpy(w, r, chunk);
        w += chunk;
        r = nextMatch;

        char prev = (r == src) ? 0 : r[-1], next = r[badLen];
        bool prevId = (prev == '_') || isalnum((unsigned char)prev);
        bool nextId = (next == '_') || isalnum((unsigned char)next);

        if (!prevId && !nextId)
        {
            memcpy(w, fix, fixLen);
            w += fixLen;
            r += badLen;
        }
        else
        {
            *w++ = *r++;
        }
    }
    strcpy(w, r);
    strcat(w, epilogue);
    return out;
}

// GLSL -> SPIR-V through shaderc with relaxed Vulkan rules. Optimization stays OFF so OpName /
// OpMemberName debug info survives for reflection. Returned words are RL_MALLOC'd.
static bool rlvkCompileGlsl(const char *source, int stage /*0=vs 1=fs 2=cs*/, u32 **outWords, size_t *outWordCount)
{
    // Vertex stage: clip-z remap epilogue (see rlvkInjectClipZEpilogue)
    char *patched = (stage == 0) ? rlvkInjectClipZEpilogue(source) : NULL;
    if (patched)
        source = patched;

    shaderc_compile_options_t opts = p_shaderc_compile_options_initialize();
    // Baseline target: Vulkan 1.1 / SPIR-V 1.3 - modules stay loadable on every supported
    // device (a 1.3-era target would emit SPIR-V 1.6, invalid on 1.1 drivers)
    p_shaderc_compile_options_set_target_env(opts, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    p_shaderc_compile_options_set_vulkan_rules_relaxed(opts, true);
    // NOTE: spirv-opt stays OFF: it strips the symbol names reflection needs, and gains only
    // ~0.15% even with debug info kept. The residual ~2% fragment-ALU deficit vs rlgl was
    // isolated to NVIDIA's separate GL/Vulkan compiler backends (controlled three-way test,
    // all spec-level levers audited); not addressable from application code, recheck after
    // driver updates.
    p_shaderc_compile_options_set_auto_bind_uniforms(opts, true);
    p_shaderc_compile_options_set_auto_map_locations(opts, true);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_vertex_shader, shaderc_uniform_kind_buffer, RLVK_UBO_BINDING_VS);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_fragment_shader, shaderc_uniform_kind_buffer, RLVK_UBO_BINDING_FS);
    // Vertex-stage samplers start at binding 8 so they never collide with FS samplers (0..7):
    // both stages auto-bind from 0 otherwise (vertex texture fetch, e.g. displacement maps)
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_vertex_shader, shaderc_uniform_kind_texture, 8);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_vertex_shader, shaderc_uniform_kind_sampler, 8);
    // Compute stage: auto-bound resources land in the fixed compute set-0 layout ranges
    // (SSBOs declare explicit std430 bindings 0..7 themselves; images 8..11, samplers 12..13,
    // the implicit loose-uniform block at 14)
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_compute_shader, shaderc_uniform_kind_image, 8);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_compute_shader, shaderc_uniform_kind_texture, 12);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_compute_shader, shaderc_uniform_kind_sampler, 12);
    p_shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_compute_shader, shaderc_uniform_kind_buffer, 14);

    shaderc_shader_kind kind = (stage == 0) ? shaderc_vertex_shader : (stage == 1) ? shaderc_fragment_shader
                                                                                   : shaderc_compute_shader;
    shaderc_compilation_result_t res = p_shaderc_compile_into_spv(
        (shaderc_compiler_t)RLVK.shadercCompiler, source, strlen(source), kind, "rlvk", "main", opts);
    p_shaderc_compile_options_release(opts);
    if (patched)
        RL_FREE(patched);

    if (p_shaderc_result_get_compilation_status(res) != shaderc_compilation_status_success)
    {
        TRACELOG(RL_LOG_WARNING, "RLVK: GLSL compile failed:\n%s", p_shaderc_result_get_error_message(res));
        p_shaderc_result_release(res);
        return false;
    }
    size_t bytes = p_shaderc_result_get_length(res);
    *outWords = (u32 *)RL_MALLOC(bytes);
    memcpy(*outWords, p_shaderc_result_get_bytes(res), bytes);
    *outWordCount = bytes / 4;
    p_shaderc_result_release(res);
    if (getenv("RLVK_DUMP_SPV")) // debug: write the module for spirv-dis inspection
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/rlvk_dump_stage%d.spv", getenv("RLVK_DUMP_SPV"), stage);
        FILE *f = fopen(path, "wb");
        if (f)
        {
            fwrite(*outWords, 4, *outWordCount, f);
            fclose(f);
        }
    }
    return true;
}

// Minimal SPIR-V reflection: default-uniform-block members (name/offset), block binding/size,
// samplers (name/binding), and vertex-stage inputs (name/location).
typedef struct rlvkSpvReflection
{
    struct
    {
        char name[64];
        u32 offset;
    } members[RLVK_MAX_SHADER_UNIFORMS];
    struct
    {
        char name[64];
        int binding;
    } samplers[RLVK_MAX_TEXTURE_UNITS];
    struct
    {
        char name[64];
        int location;
    } inputs[16];
    struct
    {
        char name[64];
        int location;
    } outputs[16];
    u32 blockBinding, blockSize;
    int memberCount, samplerCount, inputCount, outputCount;
    bool hasBlock;
} rlvkSpvReflection;

// Reflect a SPIR-V module: default uniform block members, samplers and vertex inputs
static void rlvkReflectSpv(const u32 *spv, size_t wordCount, rlvkSpvReflection *out)
{
    enum
    {
        SpvOpName = 5,
        SpvOpMemberName = 6,
        SpvOpTypeStruct = 30,
        SpvOpTypeArray = 28,
        SpvOpConstant = 43,
        SpvOpTypePointer = 32,
        SpvOpVariable = 59,
        SpvOpDecorate = 71,
        SpvOpMemberDecorate = 72,
        SpvDecorationArrayStride = 6,
        SpvDecorationLocation = 30,
        SpvDecorationBinding = 33,
        SpvDecorationOffset = 35,
        SpvStorageUniformConstant = 0,
        SpvStorageInput = 1,
        SpvStorageUniform = 2,
        SpvStorageOutput = 3,
    };
    memset(out, 0, sizeof(*out));
    if (wordCount < 5 || spv[0] != 0x07230203)
        return;
    u32 bound = spv[3];

    const char **idName = (const char **)RL_CALLOC(bound, sizeof(char *));
    int *idBinding = (int *)RL_MALLOC(bound * sizeof(int));
    int *idLoc = (int *)RL_MALLOC(bound * sizeof(int));
    u32 *ptrType = (u32 *)RL_CALLOC(bound, sizeof(u32));
    u32 *arrElem = (u32 *)RL_CALLOC(bound, sizeof(u32));   // OpTypeArray: element type
    u32 *arrLenId = (u32 *)RL_CALLOC(bound, sizeof(u32));  // OpTypeArray: length const id
    u32 *arrStride = (u32 *)RL_CALLOC(bound, sizeof(u32)); // ArrayStride decoration
    u32 *constVal = (u32 *)RL_CALLOC(bound, sizeof(u32));  // OpConstant value (word 3)
    for (u32 k = 0; k < bound; k++)
    {
        idBinding[k] = -1;
        idLoc[k] = -1;
    }

    struct
    {
        u32 structId, member;
        const char *name;
    } mnames[256];
    int mnameCount = 0;
    struct
    {
        u32 structId, member, offset;
    } moffs[256];
    int moffCount = 0;
    struct
    {
        u32 id, ptrTypeId, storage;
    } vars[128];
    int varCount = 0;
    struct
    {
        u32 id;
        u32 members[32];
        u32 count;
    } structs[32];
    int structCount = 0;

    size_t i = 5;
    while (i < wordCount)
    {
        u32 w = spv[i];
        u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount)
            break;
        const u32 *a = spv + i + 1;
        switch (op)
        {
        case SpvOpName:
            if (a[0] < bound)
                idName[a[0]] = (const char *)&a[1];
            break;
        case SpvOpMemberName:
            if (mnameCount < 256)
            {
                mnames[mnameCount].structId = a[0];
                mnames[mnameCount].member = a[1];
                mnames[mnameCount].name = (const char *)&a[2];
                mnameCount++;
            }
            break;
        case SpvOpTypeStruct:
            if (structCount < 32)
            {
                structs[structCount].id = a[0];
                structs[structCount].count = (len - 2 < 32) ? (len - 2) : 32;
                for (u32 m = 0; m < structs[structCount].count; m++)
                    structs[structCount].members[m] = a[1 + m];
                structCount++;
            }
            break;
        case SpvOpTypeArray:
            if (a[0] < bound)
            {
                arrElem[a[0]] = a[1];
                arrLenId[a[0]] = a[2];
            }
            break;
        case SpvOpConstant:
            if (a[1] < bound)
                constVal[a[1]] = a[2];
            break;
        case SpvOpTypePointer:
            if (a[0] < bound)
                ptrType[a[0]] = a[2];
            break;
        case SpvOpVariable:
            if (varCount < 128)
            {
                vars[varCount].id = a[1];
                vars[varCount].ptrTypeId = a[0];
                vars[varCount].storage = a[2];
                varCount++;
            }
            break;
        case SpvOpDecorate:
            if (a[1] == SpvDecorationBinding && a[0] < bound)
                idBinding[a[0]] = (int)a[2];
            if (a[1] == SpvDecorationLocation && a[0] < bound)
                idLoc[a[0]] = (int)a[2];
            if (a[1] == SpvDecorationArrayStride && a[0] < bound)
                arrStride[a[0]] = a[2];
            break;
        case SpvOpMemberDecorate:
            if (a[2] == SpvDecorationOffset && moffCount < 256)
            {
                moffs[moffCount].structId = a[0];
                moffs[moffCount].member = a[1];
                moffs[moffCount].offset = a[3];
                moffCount++;
            }
            break;
        default:
            break;
        }
        i += len;
    }

#define RLVK_FIND_STRUCT(_sid) ({ int _f = -1; for (int _t = 0; _t < structCount; _t++) if (structs[_t].id == (_sid)) { _f = _t; break; } _f; })
#define RLVK_MEMBER_NAME(_sid, _m) ({ const char *_n = NULL; for (int _t = 0; _t < mnameCount; _t++) if (mnames[_t].structId == (_sid) && mnames[_t].member == (_m)) { _n = mnames[_t].name; break; } _n; })
#define RLVK_MEMBER_OFF(_sid, _m) ({ u32 _o = 0; for (int _t = 0; _t < moffCount; _t++) if (moffs[_t].structId == (_sid) && moffs[_t].member == (_m)) { _o = moffs[_t].offset; break; } _o; })

    for (int v = 0; v < varCount; v++)
    {
        const char *name = (vars[v].id < bound) ? idName[vars[v].id] : NULL;
        if (vars[v].storage == SpvStorageUniform)
        {
            u32 structId = (vars[v].ptrTypeId < bound) ? ptrType[vars[v].ptrTypeId] : 0;
            const char *structName = (structId && structId < bound) ? idName[structId] : NULL;
            if (!structName || strcmp(structName, "gl_DefaultUniformBlock") != 0)
                continue;

            out->hasBlock = true;
            out->blockBinding = (idBinding[vars[v].id] >= 0) ? (u32)idBinding[vars[v].id] : 0;
            int bs = RLVK_FIND_STRUCT(structId);
            u32 maxOff = 0;
            if (bs >= 0)
                for (u32 m = 0; m < structs[bs].count; m++)
                {
                    const char *mn = RLVK_MEMBER_NAME(structId, m);
                    u32 mo = RLVK_MEMBER_OFF(structId, m);
                    if (mo > maxOff)
                        maxOff = mo;
                    if (!mn)
                        continue;
                    u32 T = structs[bs].members[m];

                    // Array of structs (e.g. "uniform Light lights[4]"): flatten to "lights[i].member",
                    // the composite names GL's glGetUniformLocation exposes and rlights.h queries.
                    u32 elemT = (T < bound) ? arrElem[T] : 0;
                    int es = elemT ? RLVK_FIND_STRUCT(elemT) : -1;
                    if (es >= 0)
                    {
                        u32 stride = arrStride[T];
                        u32 count = (arrLenId[T] < bound) ? constVal[arrLenId[T]] : 0;
                        if (count > 16)
                            count = 16;
                        for (u32 e = 0; e < count; e++)
                            for (u32 sm = 0; sm < structs[es].count; sm++)
                            {
                                const char *sn = RLVK_MEMBER_NAME(elemT, sm);
                                if (!sn || out->memberCount >= RLVK_MAX_SHADER_UNIFORMS)
                                    continue;
                                snprintf(out->members[out->memberCount].name, 64, "%s[%u].%s", mn, e, sn);
                                out->members[out->memberCount].offset = mo + e * stride + RLVK_MEMBER_OFF(elemT, sm);
                                out->memberCount++;
                            }
                        u32 arrEnd = mo + count * stride;
                        if (arrEnd > maxOff)
                            maxOff = arrEnd;
                        continue;
                    }
                    // Plain struct member: flatten to "name.member"
                    int ms = (T < bound) ? RLVK_FIND_STRUCT(T) : -1;
                    if (ms >= 0 && idName[T] && strcmp(idName[T], "gl_DefaultUniformBlock") != 0)
                    {
                        for (u32 sm = 0; sm < structs[ms].count; sm++)
                        {
                            const char *sn = RLVK_MEMBER_NAME(T, sm);
                            if (!sn || out->memberCount >= RLVK_MAX_SHADER_UNIFORMS)
                                continue;
                            snprintf(out->members[out->memberCount].name, 64, "%s.%s", mn, sn);
                            out->members[out->memberCount].offset = mo + RLVK_MEMBER_OFF(T, sm);
                            out->memberCount++;
                        }
                        continue;
                    }
                    // Scalar/vector/matrix or array thereof
                    if (out->memberCount < RLVK_MAX_SHADER_UNIFORMS)
                    {
                        strncpy(out->members[out->memberCount].name, mn, 63);
                        out->members[out->memberCount].name[63] = '\0'; // VÁ LỖI AN TOÀN BỘ NHỚ
                        out->members[out->memberCount].offset = mo;
                        out->memberCount++;
                    }

                    if (elemT) // plain array: extend the block size to cover every element
                    {
                        u32 stride = arrStride[T];
                        u32 count = (arrLenId[T] < bound) ? constVal[arrLenId[T]] : 0;
                        u32 arrEnd = mo + count * stride;
                        if (arrEnd > maxOff)
                            maxOff = arrEnd;
                    }
                }
            out->blockSize = maxOff + 64; // conservative tail padding (largest member is a mat4)
        }
        else if (vars[v].storage == SpvStorageUniformConstant && idBinding[vars[v].id] >= 0)
        {
            if (name && out->samplerCount < RLVK_MAX_TEXTURE_UNITS)
            {
                strncpy(out->samplers[out->samplerCount].name, name, 63);
                out->samplers[out->samplerCount].name[63] = '\0'; // VÁ LỖI AN TOÀN BỘ NHỚ
                out->samplers[out->samplerCount].binding = idBinding[vars[v].id];
                out->samplerCount++;
            }
        }
        else if (vars[v].storage == SpvStorageInput && idLoc[vars[v].id] >= 0)
        {
            if (name && name[0] && strncmp(name, "gl_", 3) != 0 && out->inputCount < 16)
            {
                strncpy(out->inputs[out->inputCount].name, name, 63);
                out->inputs[out->inputCount].name[63] = '\0'; // VÁ LỖI AN TOÀN BỘ NHỚ
                out->inputs[out->inputCount].location = idLoc[vars[v].id];
                out->inputCount++;
            }
        }
        else if (vars[v].storage == SpvStorageOutput && idLoc[vars[v].id] >= 0)
        {
            if (name && name[0] && strncmp(name, "gl_", 3) != 0 && out->outputCount < 16)
            {
                strncpy(out->outputs[out->outputCount].name, name, 63);
                out->outputs[out->outputCount].name[63] = '\0'; // VÁ LỖI AN TOÀN BỘ NHỚ
                out->outputs[out->outputCount].location = idLoc[vars[v].id];
                out->outputCount++;
            }
        }
    }
#undef RLVK_FIND_STRUCT
#undef RLVK_MEMBER_NAME
#undef RLVK_MEMBER_OFF

    RL_FREE(idName);
    RL_FREE(idBinding);
    RL_FREE(idLoc);
    RL_FREE(ptrType);
    RL_FREE(arrElem);
    RL_FREE(arrLenId);
    RL_FREE(arrStride);
    RL_FREE(constVal);
}

// GL blend enums -> Vulkan (rlSetBlendFactors* pass GL enums per the rlgl API contract)
static VkBlendFactor rlvkBlendFactorFromGL(int glf)
{
    switch (glf)
    {
    case 0:
        return VK_BLEND_FACTOR_ZERO; // GL_ZERO
    case 1:
        return VK_BLEND_FACTOR_ONE; // GL_ONE
    case 0x0300:
        return VK_BLEND_FACTOR_SRC_COLOR; // GL_SRC_COLOR
    case 0x0301:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 0x0302:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case 0x0303:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 0x0304:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case 0x0305:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 0x0306:
        return VK_BLEND_FACTOR_DST_COLOR;
    case 0x0307:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 0x0308:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case 0x8001:
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 0x8002:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case 0x8003:
        return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case 0x8004:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    default:
        return VK_BLEND_FACTOR_ONE;
    }
}
// Translate a GL blend equation to the equivalent Vulkan blend operation
static VkBlendOp rlvkBlendOpFromGL(int gleq)
{
    switch (gleq)
    {
    case 0x8006:
        return VK_BLEND_OP_ADD; // GL_FUNC_ADD
    case 0x8007:
        return VK_BLEND_OP_MIN; // GL_MIN
    case 0x8008:
        return VK_BLEND_OP_MAX; // GL_MAX
    case 0x800A:
        return VK_BLEND_OP_SUBTRACT; // GL_FUNC_SUBTRACT
    case 0x800B:
        return VK_BLEND_OP_REVERSE_SUBTRACT; // GL_FUNC_REVERSE_SUBTRACT
    default:
        return VK_BLEND_OP_ADD;
    }
}

// Canonical raylib attribute LOCATIONS (mirrors rlgl's glBindAttribLocation calls)
static int rlvkCanonicalAttribLocation(const char *name)
{
    if (strcmp(name, "vertexPosition") == 0)
        return 0; // RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION
    if (strcmp(name, "vertexTexCoord") == 0)
        return 1;
    if (strcmp(name, "vertexNormal") == 0)
        return 2;
    if (strcmp(name, "vertexColor") == 0)
        return 3;
    if (strcmp(name, "vertexTangent") == 0)
        return 4;
    if (strcmp(name, "vertexTexCoord2") == 0)
        return 5;
    if (strcmp(name, "vertexBoneIds") == 0)
        return 7; // RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEINDICES
    if (strcmp(name, "vertexBoneIndices") == 0)
        return 7; // alternate spelling used by stock skinning.vs
    if (strcmp(name, "vertexBoneWeights") == 0)
        return 8;
    if (strcmp(name, "instanceTransform") == 0)
        return 9; // mat4: locations 9..12
    return -1;
}

// Rewrite the VS SPIR-V's input Location decorations so named raylib attributes land at their
// canonical locations - the exact equivalent of rlgl's glBindAttribLocation before linking.
// (glslang's auto-map assigns declaration-order locations, which differ per shader.)
static void rlvkCanonicalizeInputLocations(u32 *spv, size_t wordCount)
{
    enum
    {
        SpvOpName = 5,
        SpvOpTypePointer = 32,
        SpvOpVariable = 59,
        SpvOpDecorate = 71,
        SpvDecorationLocation = 30,
        SpvStorageInput = 1
    };
    if (wordCount < 5 || spv[0] != 0x07230203)
        return;
    u32 bound = spv[3];
    const char **idName = (const char **)RL_CALLOC(bound, sizeof(char *));
    unsigned char *isInput = (unsigned char *)RL_CALLOC(bound, 1);

    size_t i = 5;
    while (i < wordCount)
    {
        u32 w = spv[i];
        u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount)
            break;
        const u32 *a = spv + i + 1;
        if (op == SpvOpName && a[0] < bound)
            idName[a[0]] = (const char *)&a[1];
        else if (op == SpvOpVariable && a[2] == SpvStorageInput && a[1] < bound)
            isInput[a[1]] = 1;
        i += len;
    }
    i = 5;
    while (i < wordCount)
    {
        u32 w = spv[i];
        u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount)
            break;
        u32 *a = spv + i + 1;
        if (op == SpvOpDecorate && a[1] == SpvDecorationLocation && a[0] < bound && isInput[a[0]] && idName[a[0]])
        {
            int loc = rlvkCanonicalAttribLocation(idName[a[0]]);
            if (loc >= 0)
                a[2] = (u32)loc;
        }
        i += len;
    }
    RL_FREE(idName);
    RL_FREE(isInput);
}

// Graphics-stage storage buffers: GLSL declares std430 bindings 0..3 (GL habit), but set 0
// already uses 0..15 for sampler units - rewrite each SSBO variable's Binding decoration to
// RLVK_SSBO_BINDING_BASE + N and report the used indices in outMask. When the device lacks
// vertexPipelineStoresAndAtomics, also inject a NonWritable decoration per SSBO variable
// (VUID-RuntimeSpirv-NonWritable-06341) - graphics SSBOs are read-only on such devices.
// May grow the SPIR-V (decoration insertion); buffer passed by reference.
static void rlvkRebaseStorageBuffers(u32 **pSpv, size_t *pWordCount, bool injectNonWritable, u32 *outMask)
{
    enum { SpvOpTypePointer = 32, SpvOpVariable = 59, SpvOpDecorate = 71,
           SpvDecorationBufferBlock = 3, SpvDecorationNonWritable = 24, SpvDecorationBinding = 33,
           SpvStorageUniform = 2, SpvStorageStorageBuffer = 12 };
    u32 *spv = *pSpv; size_t wordCount = *pWordCount;
    *outMask = 0;
    if (wordCount < 5 || spv[0] != 0x07230203) return;
    u32 bound = spv[3];
    unsigned char *isBufferBlock = (unsigned char *)RL_CALLOC(bound, 1);   // type ids decorated BufferBlock
    u32 *ptrPointee = (u32 *)RL_CALLOC(bound, sizeof(u32));                // pointer type -> pointee type
    unsigned char *ptrStorage = (unsigned char *)RL_CALLOC(bound, 1);     // pointer type -> storage class
    u32 ssboVars[RLVK_SET0_SSBO_COUNT]; int ssboVarCount = 0;
    size_t firstDecorateAt = 0;

    // Pass 1: collect BufferBlock types, pointer map, and the first decoration offset
    size_t i = 5;
    while (i < wordCount)
    {
        u32 w = spv[i]; u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount) break;
        const u32 *a = spv + i + 1;
        if (op == SpvOpDecorate)
        {
            if (!firstDecorateAt) firstDecorateAt = i;
            if ((a[1] == SpvDecorationBufferBlock) && (a[0] < bound)) isBufferBlock[a[0]] = 1;
        }
        else if (op == SpvOpTypePointer && a[0] < bound)
        {
            ptrStorage[a[0]] = (unsigned char)a[1];
            ptrPointee[a[0]] = a[2];
        }
        i += len;
    }
    // Pass 2: find SSBO variables (StorageBuffer class, or Uniform class pointing at a
    // BufferBlock struct - shaderc's SPIR-V 1.3 output uses the latter)
    i = 5;
    while (i < wordCount)
    {
        u32 w = spv[i]; u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount) break;
        const u32 *a = spv + i + 1;
        if (op == SpvOpVariable && a[0] < bound)
        {
            u32 sc = a[2];
            bool ssbo = (sc == SpvStorageStorageBuffer) ||
                        ((sc == SpvStorageUniform) && (ptrStorage[a[0]] == SpvStorageUniform) &&
                         (ptrPointee[a[0]] < bound) && isBufferBlock[ptrPointee[a[0]]]);
            if (ssbo && (ssboVarCount < (int)RLVK_SET0_SSBO_COUNT)) ssboVars[ssboVarCount++] = a[1];
            else if (ssbo) TRACELOG(RL_LOG_WARNING, "RLVK: shader uses more than %d graphics SSBOs - extras ignored", (int)RLVK_SET0_SSBO_COUNT);
        }
        i += len;
    }
    RL_FREE(isBufferBlock); RL_FREE(ptrPointee); RL_FREE(ptrStorage);
    if (ssboVarCount == 0) return;

    // Pass 3: rewrite each SSBO variable's Binding decoration in place
    i = 5;
    while (i < wordCount)
    {
        u32 w = spv[i]; u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount) break;
        u32 *a = spv + i + 1;
        if (op == SpvOpDecorate && a[1] == SpvDecorationBinding)
            for (int v = 0; v < ssboVarCount; v++)
                if (a[0] == ssboVars[v])
                {
                    u32 idx = a[2];
                    if (idx >= RLVK_SET0_SSBO_COUNT)
                    {
                        TRACELOG(RL_LOG_WARNING, "RLVK: graphics SSBO binding %u out of range (max %d), clamping", idx, (int)RLVK_SET0_SSBO_COUNT - 1);
                        idx = RLVK_SET0_SSBO_COUNT - 1;
                    }
                    *outMask |= (1u << idx);
                    a[2] = RLVK_SSBO_BINDING_BASE + idx;
                }
        i += len;
    }
    // Pass 4: inject NonWritable decorations (3 words each) at the decoration section head
    if (injectNonWritable && firstDecorateAt)
    {
        size_t grow = (size_t)ssboVarCount * 3;
        u32 *grown = (u32 *)RL_MALLOC((wordCount + grow) * sizeof(u32));
        memcpy(grown, spv, firstDecorateAt * sizeof(u32));
        u32 *ins = grown + firstDecorateAt;
        for (int v = 0; v < ssboVarCount; v++)
        {
            *ins++ = (3u << 16) | SpvOpDecorate;
            *ins++ = ssboVars[v];
            *ins++ = SpvDecorationNonWritable;
        }
        memcpy(ins, spv + firstDecorateAt, (wordCount - firstDecorateAt) * sizeof(u32));
        RL_FREE(spv);
        *pSpv = grown;
        *pWordCount = wordCount + grow;
    }
}

// GL links varyings BY NAME, SPIR-V stages match BY LOCATION: rewrite each FS input's Location
// to the same-named VS output's location. FS inputs with no matching VS output are demoted to
// Private storage (valid-but-undefined in GL; a Vulkan violation of VUID-RuntimeSpirv-
// OpEntryPoint-08743 that also broke real drivers). May grow the SPIR-V; buffer by reference.
static void rlvkMatchStageInterface(u32 **pFsSpv, size_t *pWordCount, const rlvkSpvReflection *vsRef)
{
    enum
    {
        SpvOpNop = 0,
        SpvOpName = 5,
        SpvOpEntryPoint = 15,
        SpvOpTypePointer = 32,
        SpvOpVariable = 59,
        SpvOpDecorate = 71,
        SpvDecorationLocation = 30,
        SpvStorageInput = 1,
        SpvStoragePrivate = 6
    };
    u32 *fsSpv = *pFsSpv;
    size_t wordCount = *pWordCount;
    if (wordCount < 5 || fsSpv[0] != 0x07230203)
        return;
    u32 bound = fsSpv[3];
    const char **idName = (const char **)RL_CALLOC(bound, sizeof(char *));
    unsigned char *isInput = (unsigned char *)RL_CALLOC(bound, 1);

    size_t i = 5;
    while (i < wordCount)
    {
        u32 w = fsSpv[i];
        u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount)
            break;
        const u32 *a = fsSpv + i + 1;
        if (op == SpvOpName && a[0] < bound)
            idName[a[0]] = (const char *)&a[1];
        else if (op == SpvOpVariable && a[2] == SpvStorageInput && a[1] < bound)
            isInput[a[1]] = 1;
        i += len;
    }
    u32 unmatched[16];
    size_t unmatchedDecoAt[16];
    u32 unmatchedDecoLen[16];
    int unmatchedCount = 0;
    i = 5;
    while (i < wordCount)
    {
        u32 w = fsSpv[i];
        u32 op = w & 0xFFFF, len = w >> 16;
        if (len == 0 || i + len > wordCount)
            break;
        u32 *a = fsSpv + i + 1;
        if (op == SpvOpDecorate && a[1] == SpvDecorationLocation && a[0] < bound && isInput[a[0]] &&
            idName[a[0]] && strncmp(idName[a[0]], "gl_", 3) != 0)
        {
            int matched = -1;
            for (int o = 0; o < vsRef->outputCount; o++)
                if (strcmp(vsRef->outputs[o].name, idName[a[0]]) == 0)
                {
                    matched = vsRef->outputs[o].location;
                    break;
                }
            if (matched >= 0)
                a[2] = (u32)matched;
            else if (unmatchedCount < 16)
            {
                unmatched[unmatchedCount] = a[0];
                unmatchedDecoAt[unmatchedCount] = i;
                unmatchedDecoLen[unmatchedCount] = len;
                unmatchedCount++;
            }
        }
        i += len;
    }
    RL_FREE(idName);
    RL_FREE(isInput);

    if (unmatchedCount == 0)
        return;

    // Remove the demoted inputs' Location decorations (OpNop is only legal inside a function
    // body, so the instructions must be compacted out, back-to-front to keep positions valid)
    for (int u = unmatchedCount - 1; u >= 0; u--)
    {
        size_t at = unmatchedDecoAt[u];
        u32 len = unmatchedDecoLen[u];
        memmove(fsSpv + at, fsSpv + at + len, (wordCount - at - len) * sizeof(u32));
        wordCount -= len;
    }

    // Grow once: each demotion inserts one 4-word OpTypePointer Private right before its OpVariable
    fsSpv = (u32 *)RL_REALLOC(fsSpv, (wordCount + (size_t)unmatchedCount * 4) * sizeof(u32));
    *pFsSpv = fsSpv;

    for (int u = 0; u < unmatchedCount; u++)
    {
        u32 varId = unmatched[u];
        // Locate the variable, its Input pointer type, and that type's pointee
        size_t varAt = 0;
        u32 ptrType = 0, pointee = 0;
        i = 5;
        while (i < wordCount)
        {
            u32 w = fsSpv[i];
            u32 op = w & 0xFFFF, len = w >> 16;
            if (len == 0 || i + len > wordCount)
                break;
            u32 *a = fsSpv + i + 1;
            if (op == SpvOpVariable && a[1] == varId)
            {
                varAt = i;
                ptrType = a[0];
            }
            else if (op == SpvOpTypePointer && ptrType && a[0] == ptrType)
                pointee = a[2];
            i += len;
        }
        if (!varAt)
            continue;
        if (!pointee) // pointer type declared before the variable: rescan for it
        {
            i = 5;
            while (i < wordCount)
            {
                u32 w = fsSpv[i];
                u32 op = w & 0xFFFF, len = w >> 16;
                if (len == 0 || i + len > wordCount)
                    break;
                u32 *a = fsSpv + i + 1;
                if (op == SpvOpTypePointer && a[0] == ptrType)
                {
                    pointee = a[2];
                    break;
                }
                i += len;
            }
        }
        if (!pointee)
            continue;

        u32 newPtr = fsSpv[3]++; // fresh id (bump the module bound)
        memmove(fsSpv + varAt + 4, fsSpv + varAt, (wordCount - varAt) * sizeof(u32));
        fsSpv[varAt + 0] = (4u << 16) | SpvOpTypePointer;
        fsSpv[varAt + 1] = newPtr;
        fsSpv[varAt + 2] = SpvStoragePrivate;
        fsSpv[varAt + 3] = pointee;
        wordCount += 4;
        u32 *va = fsSpv + varAt + 4 + 1;
        va[0] = newPtr;            // result type -> Private pointer
        va[2] = SpvStoragePrivate; // storage class -> Private

        // Pre-1.4 SPIR-V lists only Input/Output in the entry-point interface: remove the id.
        // 1.4+ lists ALL globals, so a Private variable stays listed.
        if (fsSpv[1] < 0x00010400)
        {
            i = 5;
            while (i < wordCount)
            {
                u32 w = fsSpv[i];
                u32 op = w & 0xFFFF, len = w >> 16;
                if (len == 0 || i + len > wordCount)
                    break;
                if (op == SpvOpEntryPoint)
                {
                    // VÁ LỖI LOGIC: Phải bỏ qua Execution Model (word 1), Entry Point ID (word 2)
                    // và chuỗi Tên Hàm (word 3 trở đi cho đến ký tự null) trước khi tìm Interface ID.
                    u32 interfaceStart = 3;
                    while (interfaceStart < len)
                    {
                        u32 word = fsSpv[i + interfaceStart];
                        interfaceStart++;
                        // Dừng lại nếu tìm thấy NULL terminator trong word này
                        if ((word & 0xFF) == 0 || ((word >> 8) & 0xFF) == 0 ||
                            ((word >> 16) & 0xFF) == 0 || ((word >> 24) & 0xFF) == 0)
                        {
                            break;
                        }
                    }

                    for (u32 n = interfaceStart; n < len; n++)
                    {
                        if (fsSpv[i + n] == varId)
                        {
                            memmove(fsSpv + i + n, fsSpv + i + n + 1, (wordCount - i - n - 1) * sizeof(u32));
                            fsSpv[i] = ((len - 1) << 16) | SpvOpEntryPoint;
                            wordCount -= 1;
                            break;
                        }
                    }
                    break;
                }
                i += len;
            }
        }
    }
    *pWordCount = wordCount;
}

// Get the canonical attribute table index for a vertex attribute name
static int rlvkCanonicalAttribIndex(const char *name)
{
    if (strcmp(name, "vertexPosition") == 0)
        return RLVK_ATTRIB_POSITION;
    if (strcmp(name, "vertexTexCoord") == 0)
        return RLVK_ATTRIB_TEXCOORD;
    if (strcmp(name, "vertexNormal") == 0)
        return RLVK_ATTRIB_NORMAL;
    if (strcmp(name, "vertexColor") == 0)
        return RLVK_ATTRIB_COLOR;
    if (strcmp(name, "vertexTangent") == 0)
        return RLVK_ATTRIB_TANGENT;
    if (strcmp(name, "vertexTexCoord2") == 0)
        return RLVK_ATTRIB_TEXCOORD2;
    if (strcmp(name, "instanceTransform") == 0)
        return RLVK_ATTRIB_INSTANCE_TX;
    if (strcmp(name, "vertexBoneIds") == 0)
        return RLVK_ATTRIB_BONEIDS;
    if (strcmp(name, "vertexBoneIndices") == 0)
        return RLVK_ATTRIB_BONEIDS;
    if (strcmp(name, "vertexBoneWeights") == 0)
        return RLVK_ATTRIB_BONEWEIGHTS;
    return -1;
}

static void rlvkShaderWriteMatrixUniform(rlvkShaderSlot *shader, int loc, Matrix mat);

// Write into the CPU staging of whichever stage blocks contain this uniform
static void rlvkShaderWriteUniform(rlvkShaderSlot *shader, int loc, const void *data, u32 bytes)
{
    if (!shader->usesUbo || loc < 0 || loc >= shader->uniformCount || !data)
        return;
    rlvkUniform *u = &shader->uniforms[loc];
    if (u->vsOffset >= 0 && shader->vsStage && (u32)u->vsOffset + bytes <= shader->vsBlockSize)
    {
        memcpy(shader->vsStage + u->vsOffset, data, bytes);
        shader->vsWriteGen++;
    }
    if (u->fsOffset >= 0 && shader->fsStage && (u32)u->fsOffset + bytes <= shader->fsBlockSize)
    {
        memcpy(shader->fsStage + u->fsOffset, data, bytes);
        shader->fsWriteGen++;
    }
}

// Write a Matrix uniform in rlMatrixToFloat (column-major/std140) order
static void rlvkShaderWriteMatrixUniform(rlvkShaderSlot *shader, int loc, Matrix mat)
{
    f32 f[16] = {
        mat.m0, mat.m1, mat.m2, mat.m3, mat.m4, mat.m5, mat.m6, mat.m7,
        mat.m8, mat.m9, mat.m10, mat.m11, mat.m12, mat.m13, mat.m14, mat.m15};
    rlvkShaderWriteUniform(shader, loc, f, sizeof(f));
}

// Snapshot the shader's dirty UBO stage blocks into the per-frame arena (glUniform semantics: each
// draw sees the values current at record time) and APPEND the resulting descriptor writes to the
// caller's arrays. Returns how many writes were appended (0..2). `bufferInfos`/`writes` must have
// room for 2 entries and stay alive until the caller issues its CmdPushDescriptorSetKHR. The
// pushed-gen bookkeeping is advanced exactly as a standalone push would; the caller is responsible
// for setting uboPushedEpoch/lastUboShader after the actual push.
static u32 rlvkAppendUboWrites(rlvkShaderSlot *shader, VkDescriptorBufferInfo *bufferInfos, VkWriteDescriptorSet *writes)
{
    if (!shader->usesUbo)
        return 0;

    // Snapshot a stage's block ONLY when its uniforms changed since the last push in this command
    // buffer (pushes persist until overwritten); both stages ride the caller's one push call.
    bool cbFresh = (shader->uboPushedEpoch != RLVK.State.cbEpoch) || (RLVK.lastUboShader != shader);
    bool wantVs = shader->vsBlockSize && shader->vsStage && (cbFresh || (shader->vsPushedGen != shader->vsWriteGen));
    bool wantFs = shader->fsBlockSize && shader->fsStage && (cbFresh || (shader->fsPushedGen != shader->fsWriteGen));
    if (!wantVs && !wantFs)
        return 0;

    u32 frameIndex = (u32)(RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT);
    rlvkBatchBackingBuffer *arena = &RLVK.arena[frameIndex];
    u32 writeCount = 0;
    for (int stage = 0; stage < 2; stage++)
    {
        if (stage ? !wantFs : !wantVs)
            continue;
        u32 size = stage ? shader->fsBlockSize : shader->vsBlockSize;
        unsigned char *src = stage ? shader->fsStage : shader->vsStage;
        VkDeviceSize off = (RLVK.arenaOffset[frameIndex] + 255) & ~(VkDeviceSize)255; // minUniformBufferOffsetAlignment
        if (off + size > arena->sizeBytes)
        {
            // Cannot drain here (this draw's binds would be lost): request growth, skip this stage
            RLVK.arenaWanted[frameIndex] += size + 256; // demand grows even when the push is skipped
            return writeCount;
        }
        memcpy((char *)arena->mapped + off, src, size);
        RLVK.arenaOffset[frameIndex] = off + size;
        RLVK.arenaWanted[frameIndex] += size + 256;
        bufferInfos[writeCount] = (VkDescriptorBufferInfo){arena->buffer, off, size};
        writes[writeCount] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = stage ? (u32)RLVK_UBO_BINDING_FS : (u32)RLVK_UBO_BINDING_VS,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bufferInfos[writeCount],
        };
        writeCount++;
        if (stage)
            shader->fsPushedGen = shader->fsWriteGen;
        else
            shader->vsPushedGen = shader->vsWriteGen;
    }
    return writeCount;
}

// Snapshot the shader's uniform staging and push the UBO descriptors as one call (mesh path).
static void rlvkBindShaderUbos(VkCommandBuffer cmdBuffer, rlvkShaderSlot *shader)
{
    if (!shader->usesUbo)
        return;
    VkDescriptorBufferInfo bufferInfos[2];
    VkWriteDescriptorSet writes[2];
    u32 writeCount = rlvkAppendUboWrites(shader, bufferInfos, writes);
    if (writeCount == 0)
        return;
    vk.CmdPushDescriptorSetKHR(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, RLVK.pipelineLayout, 0, writeCount, writes);
    shader->uboPushedEpoch = RLVK.State.cbEpoch;
    RLVK.lastUboShader = shader;
}

// Push the graphics-SSBO descriptors a shader reads (GPU-particle draw path). Sources the
// shared rlBindShaderBuffer table (indices 0..3 -> set0 bindings RLVK_SSBO_BINDING_BASE+i).
// Native push descriptors only: on the pool-ring fallback rlvkFlushSet0 writes every SSBO
// binding from the same table (rlBindShaderBuffer marks set0Dirty), so this is a no-op there.
static void rlvkBindShaderSsbos(VkCommandBuffer cmdBuffer, rlvkShaderSlot *shader)
{
    static int s_dbgSsbo = -1;
    if (rlvkDebugFlag("RLVK_DEBUG_SSBO", &s_dbgSsbo)) TRACELOG(RL_LOG_WARNING, "VKSSBO bind mask=0x%x pushDesc=%d slot0=%u", shader->ssboMask, (int)RLVK.Caps.pushDescriptor, RLVK.computeSSBO[0]);
    if (!shader->ssboMask || !RLVK.Caps.pushDescriptor)
        return;
    VkDescriptorBufferInfo infos[RLVK_SET0_SSBO_COUNT];
    VkWriteDescriptorSet writes[RLVK_SET0_SSBO_COUNT];
    u32 writeCount = 0;
    for (u32 i = 0; i < RLVK_SET0_SSBO_COUNT; i++)
    {
        if (!(shader->ssboMask & (1u << i)))
            continue;
        u32 slot = RLVK.computeSSBO[i];
        if (RLVK.pushedSsbo[i] == slot)
            continue;   // unchanged since the last push in this command buffer
        VkBuffer buf = (slot && slot < RLVK_MAX_BUFFER_SLOTS && RLVK.bufferSlots[slot].buffer)
                           ? RLVK.bufferSlots[slot].buffer
                           : RLVK.bufferSlots[RLVK.dummyAttribSlot].buffer;
        infos[writeCount] = (VkDescriptorBufferInfo){buf, 0, VK_WHOLE_SIZE};
        writes[writeCount] = (VkWriteDescriptorSet){
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = RLVK_SSBO_BINDING_BASE + i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &infos[writeCount],
        };
        writeCount++;
        RLVK.pushedSsbo[i] = slot;
    }
    if (writeCount)
        vk.CmdPushDescriptorSetKHR(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, RLVK.pipelineLayout, 0, writeCount, writes);
}

// Push the shader's sampler bindings: rlSetUniformSampler's explicit texture wins, else the GL
// texture unit's. Batch flush pushes binding 0 itself (includeBinding0=false); mesh path resolves
// binding 0 here (a samplerCube at binding 0 gets the cubemap unit, not the diffuse map)
static void rlvkBindShaderSamplers(VkCommandBuffer cmdBuffer, rlvkShaderSlot *shader, bool includeBinding0)
{
    if (!shader->usesUbo)
        return;
    for (int i = 0; i < shader->uniformCount; i++)
    {
        int b = shader->uniforms[i].samplerBinding;
        if (b < 0 || b >= RLVK_MAX_TEXTURE_UNITS || (b == 0 && !includeBinding0))
            continue;
        u32 tex = shader->bindingTexture[b];
        if (tex == 0)
        {
            int unit = shader->bindingUnit[b];
            if (unit >= 0 && unit < RLVK_MAX_TEXTURE_UNITS)
                tex = RLVK.State.activeTextureSlots[unit];
            if (tex == 0 && b == 0)
                tex = RLVK.State.currentTextureSlot; // mesh diffuse fallback
        }
        if (tex == 0 || tex >= RLVK_MAX_TEXTURE_SLOTS || !RLVK.textureSlots[tex].view)
            tex = RLVK.defaultTextureSlot;
        if (rlvkDebugFlag("RLVK_DEBUG_SAMPLERS", &s_dbgSamplers))
            TRACELOG(RL_LOG_WARNING, "VKDBG sampler %s b=%d bindTex=%u unit=%d unitTex=%u -> %u",
                     shader->uniforms[i].name, b, shader->bindingTexture[b], shader->bindingUnit[b],
                     (shader->bindingUnit[b] >= 0 && shader->bindingUnit[b] < RL_DEFAULT_BATCH_MAX_TEXTURE_UNITS) ? RLVK.State.activeTextureSlots[shader->bindingUnit[b]] : 9999, tex);
        rlvkPushTexture(cmdBuffer, (u32)b, tex);
    }
}
