/* Actual production ribbon compute, supplied with Core-packed fixture bytes.
 * No ribbon solver or field formulas are duplicated in this renderer test. */
static void runRibbonMotionParity(const char *root, const char *fixture)
{
    char path[2048];
    unsigned int buffers[7] = {0}, program = 0;
    unsigned char *actual = NULL, *expected = NULL;
    unsigned int count = 0, steps = 0;
    float dt = 0, time = 0, tolerance = 0;
    snprintf(path, sizeof(path), "%s/config.txt", fixture);
    FILE *config = fopen(path, "r");
    int configured = config && fscanf(config, "%u %u %f %f %f", &count, &steps,
                                     &dt, &time, &tolerance) == 5;
    if (config) fclose(config);
    CHECK(configured && count > 0 && count <= 64 && steps > 0 &&
          isfinite(dt) && dt > 0 && isfinite(time) && isfinite(tolerance) && tolerance >= 0,
          "production ribbon fixture configured");
    if (!configured || !count || count > 64 || !steps || !isfinite(dt) ||
        !(dt > 0) || !isfinite(time) || !isfinite(tolerance) || !(tolerance >= 0)) return;
    size_t nodeBytes = 0;
    for (int binding = 0; binding < 7; binding++)
    {
        if (binding == 2) continue;
        size_t size = 0;
        snprintf(path, sizeof(path), "%s/ssbo%d.bin", fixture, binding);
        unsigned char *data = motionReadFile(path, &size);
        if (!data || !size) { free(data); CHECK(0, "ribbon fixture SSBO loaded"); goto cleanup; }
        buffers[binding] = rlLoadShaderBuffer(size, data, RL_DYNAMIC_COPY);
        free(data);
        if (!buffers[binding] || buffers[binding] == 0xffffffffu)
        { CHECK(0, "ribbon fixture SSBO allocated"); goto cleanup; }
        if (binding == 0) nodeBytes = size;
    }
    CHECK(nodeBytes == (size_t)count * 60 * 48, "ribbon fixture bounded node stride and capacity");
    if (nodeBytes != (size_t)count * 60 * 48) goto cleanup;
    const char *shaderOverride = getenv("RLVK_RIBBON_SHADER");
    snprintf(path, sizeof(path), "%s/core/trails/shaders/trail_ribbon.comp", root);
    size_t overrideSize = 0;
    char *source = shaderOverride ? (char *)motionReadFile(shaderOverride, &overrideSize)
                                  : motionReadShader(root, path, 0);
    CHECK(source != NULL, "production ribbon shader includes expanded");
    if (!source) goto cleanup;
    double compileStart = motionTestTime();
    unsigned int shader = rlLoadShader(source, RL_COMPUTE_SHADER);
    program = rlLoadShaderProgramCompute(shader);
    free(source);
    printf("      production ribbon shader compile wall time: %.3f s\n", motionTestTime() - compileStart);
    CHECK(program != 0 && program != 0xffffffffu, "production ribbon compute compiled");
    if (!program || program == 0xffffffffu) goto cleanup;
    rlEnableShader(program);
    int dtLocation = rlGetLocationUniform(program, "u_dt");
    int timeLocation = rlGetLocationUniform(program, "u_time");
    int slotLocation = rlGetLocationUniform(program, "u_slot");
    CHECK(dtLocation >= 0 && timeLocation >= 0 && slotLocation >= 0,
          "production ribbon uniforms reflected");
    if (dtLocation < 0 || timeLocation < 0 || slotLocation < 0) { rlDisableShader(); goto cleanup; }
    int batch = getenv("RLVK_RIBBON_BATCH") && atoi(getenv("RLVK_RIBBON_BATCH")) != 0;
    double dispatchStart = motionTestTime();
    for (unsigned int step = 0; step < steps; step++)
    {
        float sampleTime = time + step * dt;
        rlSetUniform(dtLocation, &dt, RL_SHADER_UNIFORM_FLOAT, 1);
        rlSetUniform(timeLocation, &sampleTime, RL_SHADER_UNIFORM_FLOAT, 1);
        for (int slot = batch ? -1 : 0; slot < (batch ? 0 : (int)count); slot++)
        {
            rlSetUniform(slotLocation, &slot, RL_SHADER_UNIFORM_INT, 1);
            for (int binding = 0; binding < 7; binding++)
                if (binding != 2) rlBindShaderBuffer(buffers[binding], binding);
            rlComputeShaderDispatch(batch ? count : 1, 1, 1);
        }
    }
    rlDisableShader();
    actual = malloc(nodeBytes);
    if (!actual) { CHECK(0, "ribbon readback allocated"); goto cleanup; }
    rlReadShaderBuffer(buffers[0], actual, nodeBytes, 0);
    printf("      ribbon dispatch and synchronized readback wall time: %.3f ms\n",
           (motionTestTime() - dispatchStart) * 1000);
    snprintf(path, sizeof(path), "%s/actual0.bin", fixture);
    FILE *output = fopen(path, "wb");
    if (output) { fwrite(actual, 1, nodeBytes, output); fclose(output); }
    size_t expectedSize = 0;
    snprintf(path, sizeof(path), "%s/expected0.bin", fixture);
    expected = motionReadFile(path, &expectedSize);
    CHECK(expected && expectedSize == nodeBytes, "production ribbon CPU reference loaded");
    if (!expected || expectedSize != nodeBytes) goto cleanup;
    int finite = 1, close = 1;
    float maximumError = 0;
    for (size_t node = 0; node < nodeBytes / 48; node++)
    {
        const float *got = (const float *)(actual + node * 48);
        const float *want = (const float *)(expected + node * 48);
        for (int component = 0; component < 8; component++)
        {
            if (component == 3 || component == 7) continue;
            if (!isfinite(got[component]) || !isfinite(want[component])) finite = 0;
            float error = fabsf(got[component] - want[component]);
            if (error > maximumError) maximumError = error;
            if (!(error <= tolerance)) close = 0;
        }
    }
    printf("      ribbon CPU/GPU parity: ribbons=%u steps=%u maxAbsError=%g tolerance=%g\n",
           count, steps, maximumError, tolerance);
    CHECK(finite, "production ribbon positions and velocities finite");
    CHECK(close, "production ribbon GPU agrees with CPU solver");
    const char *iterationsEnv = getenv("RLVK_RIBBON_BENCHMARK");
    unsigned int iterations = iterationsEnv ? (unsigned int)atoi(iterationsEnv) : 0;
    if (finite && close && iterations > 0 && iterations <= 1000)
    {
        unsigned int dispatchesPerFrame = batch ? 1 : count;
        unsigned int capacityFrames = (RLVK_COMPUTE_SETS_PER_FRAME - 8) / dispatchesPerFrame;
        if (iterations > capacityFrames) iterations = capacityFrames;
        // Resident state, fixed dt, no per-frame uploads/readbacks. A final drain
        // includes device execution; wall time is a host proxy, not GPU timing.
        for (int sample = 0; sample < 6; sample++)
        {
            // The previous synchronized readback completed every descriptor user.
            // Headless batches have no present/fence lifecycle to reset this pool.
            VkResult reset = vkResetDescriptorPool(RLVK.device,
                RLVK.computeDescPools[RLVK.frameCounter % RLVK_FRAME_INDEX_COUNT], 0);
            CHECK(reset == VK_SUCCESS, "drained ribbon benchmark descriptor pool reset");
            if (reset != VK_SUCCESS) break;
            rlEnableShader(program);
            double start = motionTestTime();
            for (unsigned int frame = 0; frame < iterations; frame++)
            {
                float sampleTime = time + (steps + sample * iterations + frame) * dt;
                rlSetUniform(dtLocation, &dt, RL_SHADER_UNIFORM_FLOAT, 1);
                rlSetUniform(timeLocation, &sampleTime, RL_SHADER_UNIFORM_FLOAT, 1);
                for (int slot = batch ? -1 : 0; slot < (batch ? 0 : (int)count); slot++)
                {
                    rlSetUniform(slotLocation, &slot, RL_SHADER_UNIFORM_INT, 1);
                    for (int binding = 0; binding < 7; binding++)
                        if (binding != 2) rlBindShaderBuffer(buffers[binding], binding);
                    rlComputeShaderDispatch(batch ? count : 1, 1, 1);
                }
            }
            rlDisableShader();
            double recordingEnd = motionTestTime();
            rlReadShaderBuffer(buffers[0], actual, nodeBytes, 0);
            printf("      ribbon resident host proxy: ribbons=%u frames=%u batch=%d sample=%d warmup=%d wall=%.6f ms/frame recording=%.6f ms/frame drain=%.3f ms\n",
                   count, iterations, batch, sample, sample == 0,
                   (motionTestTime() - start) * 1000 / iterations,
                   (recordingEnd - start) * 1000 / iterations,
                   (motionTestTime() - recordingEnd) * 1000);
        }
    }
cleanup:
    free(actual); free(expected);
    if (program && program != 0xffffffffu) rlUnloadShaderProgram(program);
    for (int binding = 0; binding < 7; binding++)
        if (buffers[binding] && buffers[binding] != 0xffffffffu) rlUnloadShaderBuffer(buffers[binding]);
}
