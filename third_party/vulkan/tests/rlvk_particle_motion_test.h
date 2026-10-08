// Optional production-shader parity runner. Fixture bytes come from Core's
// production packers; this test deliberately has no duplicate Motion layout.
static double motionTestTime(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec * 1e-9;
}

static int motionTestReplace(char **source, const char *from, const char *to)
{
    char *found = strstr(*source, from);
    if (!found) return 0;
    size_t prefix = (size_t)(found - *source), replacement = strlen(to);
    size_t suffix = strlen(found + strlen(from));
    char *updated = malloc(prefix + replacement + suffix + 1);
    if (!updated) return 0;
    memcpy(updated, *source, prefix);
    memcpy(updated + prefix, to, replacement);
    memcpy(updated + prefix + replacement, found + strlen(from), suffix + 1);
    free(*source);
    *source = updated;
    return 1;
}

static unsigned char *motionReadFile(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    unsigned char *data = malloc((size_t)length + 1);
    if (!data) { fclose(file); return NULL; }
    if (fread(data, 1, (size_t)length, file) != (size_t)length)
    { free(data); fclose(file); return NULL; }
    fclose(file);
    data[length] = 0;
    *size = (size_t)length;
    return data;
}

// Expand quoted relative includes in the actual shader, before shaderc sees it.
static char *motionReadShader(const char *root, const char *path, int depth)
{
    size_t size;
    if (depth > 8) return NULL;
    char *source = (char *)motionReadFile(path, &size);
    if (!source) return NULL;
    char *directive;
    while ((directive = strstr(source, "#include \"")) != NULL)
    {
        char *name = directive + strlen("#include \"");
        char *end = strchr(name, '"');
        if (!end) { free(source); return NULL; }
        char includePath[2048];
        const char *slash = strrchr(path, '/');
        size_t prefix = slash ? (size_t)(slash - path + 1) : 0;
        size_t nameSize = (size_t)(end - name);
        if (prefix + nameSize >= sizeof(includePath)) { free(source); return NULL; }
        memcpy(includePath, path, prefix);
        memcpy(includePath + prefix, name, nameSize);
        includePath[prefix + nameSize] = 0;
        char *included = motionReadShader(root, includePath, depth + 1);
        if (!included)
        {
            int written = snprintf(includePath, sizeof(includePath), "%s/%.*s", root, (int)nameSize, name);
            if (written > 0 && (size_t)written < sizeof(includePath))
                included = motionReadShader(root, includePath, depth + 1);
        }
        if (!included) { free(source); return NULL; }
        size_t before = (size_t)(directive - source), includedSize = strlen(included);
        size_t after = strlen(end + 1);
        char *expanded = malloc(before + includedSize + after + 2);
        if (!expanded) { free(included); free(source); return NULL; }
        memcpy(expanded, source, before);
        memcpy(expanded + before, included, includedSize);
        expanded[before + includedSize] = '\n';
        memcpy(expanded + before + includedSize + 1, end + 1, after + 1);
        free(included);
        free(source);
        source = expanded;
    }
    return source;
}

static void runParticleMotionParity(const char *root, const char *fixture)
{
    char path[2048];
    unsigned int buffers[8] = {0};
    unsigned int program = 0;
    unsigned char *expected = NULL, *actual = NULL;
    unsigned int count = 0, steps = 0, benchmarkIterations = 0;
    unsigned int workgroup = 256;
    const char *workgroupEnv = getenv("RLVK_MOTION_WORKGROUP");
    if (workgroupEnv) workgroup = (unsigned int)atoi(workgroupEnv);
    CHECK(workgroup == 32 || workgroup == 64 || workgroup == 128 || workgroup == 256,
          "Motion test workgroup supported");
    if (workgroup != 32 && workgroup != 64 && workgroup != 128 && workgroup != 256) return;
    const char *capacityEnv = getenv("RLVK_MOTION_CONTROLLER_CAPACITY");
    unsigned int capacity = capacityEnv ? (unsigned int)atoi(capacityEnv) : 8;
    CHECK(capacity >= 1 && capacity <= 8, "Motion test controller capacity supported");
    if (capacity < 1 || capacity > 8) return;
    const char *ablation = getenv("RLVK_MOTION_ABLATION");
    if (ablation && strcmp(ablation, "no-curl") && strcmp(ablation, "free-flight"))
    { CHECK(0, "Motion ablation must be no-curl or free-flight"); return; }
    float dt = 0, time = 0, tolerance = 0;
    snprintf(path, sizeof(path), "%s/config.txt", fixture);
    FILE *config = fopen(path, "r");
    int configured = config && fscanf(config, "%u %u %f %f %f", &count, &steps,
                                     &dt, &time, &tolerance) == 5;
    if (configured) (void)fscanf(config, "%u", &benchmarkIterations);
    if (config) fclose(config);
    CHECK(configured && count > 0 && steps > 0 && dt > 0 && tolerance >= 0,
          "production Motion GPU fixture configured");
    if (!configured || !count || !steps || !(dt > 0) || !(tolerance >= 0)) return;
    size_t particleSize = 0;
    for (int binding = 0; binding < 8; binding++)
    {
        size_t size = 0;
        snprintf(path, sizeof(path), "%s/ssbo%d.bin", fixture, binding);
        unsigned char *data = motionReadFile(path, &size);
        if (binding == 7 && !data)
        {
            size = (size_t)count * 8 * 64;
            data = calloc(1, size);
        }
        if (!data || !size) { free(data); CHECK(0, "Motion fixture SSBO loaded"); goto cleanup; }
        buffers[binding] = rlLoadShaderBuffer(size, data, RL_DYNAMIC_COPY);
        free(data);
        if (!buffers[binding] || buffers[binding] == 0xffffffffu)
        { CHECK(0, "Motion fixture SSBO allocated"); goto cleanup; }
        if (binding == 0) particleSize = size;
    }
    CHECK(particleSize == (size_t)count * 144, "legacy particle stride stays 144 bytes");
    if (particleSize != (size_t)count * 144) goto cleanup;
    snprintf(path, sizeof(path), "%s/core/particles/shaders/gpu/particle_gpu.comp", root);
    const char *shaderOverride = getenv("RLVK_MOTION_SHADER");
    char *source = motionReadShader(root, shaderOverride ? shaderOverride : path, 0);
    CHECK(source != NULL, "production particle shader includes expanded");
    if (!source) goto cleanup;
    if (shaderOverride) printf("      Motion shader override: %s\n", shaderOverride);
    char replacement[128];
    snprintf(replacement, sizeof(replacement), "local_size_x = %u", workgroup);
    int transformed = motionTestReplace(&source, "local_size_x = 256", replacement);
    if (capacity != 8)
    {
        snprintf(replacement, sizeof(replacement), "MController controllers[%u]", capacity);
        transformed &= motionTestReplace(&source, "MController controllers[8]", replacement);
        snprintf(replacement, sizeof(replacement), "s.count<%u", capacity);
        transformed &= motionTestReplace(&source, "s.count<8", replacement);
        snprintf(replacement, sizeof(replacement), "bool limited[%u];vec3 boundedForces[%u]", capacity, capacity);
        transformed &= motionTestReplace(&source, "bool limited[8];vec3 boundedForces[8]", replacement);
    }
    if (ablation && !strcmp(ablation, "no-curl"))
        transformed &= motionTestReplace(&source,
            "vec3 mCurl(int fi,vec3 p,vec3 nearest,float age,float speed,float eddy) {",
            "vec3 mCurl(int fi,vec3 p,vec3 nearest,float age,float speed,float eddy) { return vec3(0);");
    if (ablation && !strcmp(ablation, "free-flight"))
        transformed &= motionTestReplace(&source, "motionPhysicalNoise=true;motionIntegrate(idx,p);",
            "p.pos_radius.xyz+=p.vel_drag.xyz*u_dt;");
    if (getenv("RLVK_MOTION_SAMPLE_REFERENCE"))
        transformed &= motionTestReplace(&source, "vec3 mSolveControllers(MSample s,",
            "vec3 mSolveControllers(inout MSample s,");
    CHECK(transformed, "Motion profiling shader variant applied");
    if (!transformed) { free(source); goto cleanup; }
    printf("      Motion variant: workgroup=%u controllerCapacity=%u ablation=%s\n",
           workgroup, capacity, ablation ? ablation : "none");
    if (getenv("RLVK_MOTION_SAMPLE_REFERENCE")) printf("      Motion sample passed by reference\n");
    double compileStart = motionTestTime();
    unsigned int shader = rlLoadShader(source, RL_COMPUTE_SHADER);
    program = rlLoadShaderProgramCompute(shader);
    printf("      production Motion shader/program compile wall time: %.3f s\n",
           motionTestTime() - compileStart);
    free(source);
    CHECK(program != 0 && program != 0xffffffffu, "production Motion compute compiled");
    if (!program || program == 0xffffffffu) goto cleanup;
    rlEnableShader(program);
    int dtLocation = rlGetLocationUniform(program, "u_dt");
    int timeLocation = rlGetLocationUniform(program, "u_time");
    CHECK(dtLocation >= 0 && timeLocation >= 0, "production particle uniforms reflected");
    double dispatchStart = motionTestTime();
    for (unsigned int step = 0; step < steps; step++)
    {
        float sampleTime = time + step * dt;
        rlSetUniform(dtLocation, &dt, RL_SHADER_UNIFORM_FLOAT, 1);
        rlSetUniform(timeLocation, &sampleTime, RL_SHADER_UNIFORM_FLOAT, 1);
        for (int binding = 0; binding < 8; binding++) rlBindShaderBuffer(buffers[binding], binding);
        rlComputeShaderDispatch((count + workgroup - 1) / workgroup, 1, 1);
    }
    rlDisableShader();
    actual = malloc(particleSize);
    if (!actual) { CHECK(0, "Motion readback allocated"); goto cleanup; }
    rlReadShaderBuffer(buffers[0], actual, particleSize, 0);
    printf("      production Motion dispatch + synchronized readback wall time: %.3f ms\n",
           (motionTestTime() - dispatchStart) * 1000);
    snprintf(path, sizeof(path), "%s/actual0.bin", fixture);
    FILE *output = fopen(path, "wb");
    if (output) { fwrite(actual, 1, particleSize, output); fclose(output); }
    size_t sidecarSize = rlGetShaderBufferSize(buffers[6]);
    unsigned char *sidecar = malloc(sidecarSize);
    if (sidecar)
    {
        rlReadShaderBuffer(buffers[6], sidecar, sidecarSize, 0);
        snprintf(path, sizeof(path), "%s/actual6.bin", fixture);
        output = fopen(path, "wb");
        if (output) { fwrite(sidecar, 1, sidecarSize, output); fclose(output); }
        free(sidecar);
    }
    size_t expectedSize = 0;
    snprintf(path, sizeof(path), "%s/expected0.bin", fixture);
    expected = motionReadFile(path, &expectedSize);
    CHECK(expected && expectedSize == particleSize, "production CPU reference loaded");
    if (!expected || expectedSize != particleSize) goto cleanup;
    int finite = 1, close = 1;
    float maximumError = 0;
    for (unsigned int particle = 0; particle < count; particle++)
    {
        const float *got = (const float *)(actual + particle * 144);
        const float *want = (const float *)(expected + particle * 144);
        for (int component = 0; component < 8; component++)
        {
            if (component == 3 || component == 7) continue;
            if (!isfinite(got[component]) || !isfinite(want[component])) finite = 0;
            float error = fabsf(got[component] - want[component]);
            if (error > maximumError) maximumError = error;
            if (!(error <= tolerance)) close = 0;
        }
    }
    printf("      production Motion parity: particles=%u steps=%u maxAbsError=%g tolerance=%g\n",
           count, steps, maximumError, tolerance);
    CHECK(finite, "production Motion GPU positions and velocities finite");
    if (!ablation) CHECK(close, "production Motion GPU agrees with CPU reference");
    else printf("      Ablation intentionally changes motion; CPU agreement is not asserted.\n");
    if (benchmarkIterations > 0 && finite && (close || ablation))
    {
        // A resident batch measures submission + execution + one final drain.
        // It is deliberately labelled host wall time, not GPU timestamp time.
        rlEnableShader(program);
        VkQueryPool queryPool = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(RLVK.physicalDevice, &properties);
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(RLVK.physicalDevice, &familyCount, NULL);
        VkQueueFamilyProperties *families = calloc(familyCount, sizeof(*families));
        uint32_t validBits = 0;
        if (families)
        {
            vkGetPhysicalDeviceQueueFamilyProperties(RLVK.physicalDevice, &familyCount, families);
            if (RLVK.graphicsFamily < familyCount) validBits = families[RLVK.graphicsFamily].timestampValidBits;
            free(families);
        }
        if (validBits > 0 && properties.limits.timestampPeriod > 0)
            vkCreateQueryPool(RLVK.device, &(VkQueryPoolCreateInfo){
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 }, NULL, &queryPool);
        double benchmarkStart = motionTestTime();
        if (queryPool != VK_NULL_HANDLE)
        {
            VkCommandBuffer command = rlvkComputeBatchAcquire();
            vkCmdResetQueryPool(command, queryPool, 0, 2);
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 0);
        }
        for (unsigned int iteration = 0; iteration < benchmarkIterations; iteration++)
        {
            float sampleTime = time + (steps + iteration) * dt;
            rlSetUniform(timeLocation, &sampleTime, RL_SHADER_UNIFORM_FLOAT, 1);
            rlComputeShaderDispatch((count + workgroup - 1) / workgroup, 1, 1);
        }
        if (queryPool != VK_NULL_HANDLE)
            vkCmdWriteTimestamp(rlvkComputeBatchAcquire(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 1);
        rlDisableShader();
        double submissionEnd = motionTestTime();
        rlReadShaderBuffer(buffers[0], actual, particleSize, 0);
        printf("      production Motion resident batch host wall: particles=%u iterations=%u dt=%g average=%.3f ms/dispatch\n",
               count, benchmarkIterations, dt,
               (motionTestTime() - benchmarkStart) * 1000 / benchmarkIterations);
        printf("      Motion host recording=%.3f ms/dispatch finalDrain=%.3f ms\n",
               (submissionEnd - benchmarkStart) * 1000 / benchmarkIterations,
               (motionTestTime() - submissionEnd) * 1000);
        if (queryPool != VK_NULL_HANDLE)
        {
            uint64_t query[4] = {0};
            VkResult result = vkGetQueryPoolResults(RLVK.device, queryPool, 0, 2,
                sizeof(query), query, 2 * sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            if (result == VK_SUCCESS && query[1] && query[3] && query[2] > query[0])
                printf("      Motion resident GPU timestamps: average=%.3f ms/dispatch period=%g bits=%u\n",
                    (query[2] - query[0]) * properties.limits.timestampPeriod * 1e-6 / benchmarkIterations,
                    properties.limits.timestampPeriod, validBits);
            else printf("      Motion GPU timestamps rejected: result=%d available=%llu/%llu span=%llu/%llu\n",
                    result, (unsigned long long)query[1], (unsigned long long)query[3],
                    (unsigned long long)query[0], (unsigned long long)query[2]);
            vkDestroyQueryPool(RLVK.device, queryPool, NULL);
        }
        else printf("      Motion GPU timestamps unavailable: bits=%u\n", validBits);
    }
cleanup:
    if (program && program != 0xffffffffu) rlUnloadShader(program);
    for (int i = 0; i < 8; i++) if (buffers[i] && buffers[i] != 0xffffffffu) rlUnloadShaderBuffer(buffers[i]);
    free(actual);
    free(expected);
}
