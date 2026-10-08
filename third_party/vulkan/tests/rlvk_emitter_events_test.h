/* Execute the production emission shader against Core-owned binary fixtures.
 * Event/Body integer words compare exactly; physical values compare by tolerance.
 * Optional config steps exercise resident state without intermediate readback. */
static int emissionWordIsInteger(int binding, size_t word)
{
    if (binding == 5) return 1;
    if (binding == 3) return word % 8 < 4;
    if (binding == 2) return word % 48 < 4 ||
                             (word % 48 >= 28 && word % 48 < 32);
    return 0;
}

static void runEmissionChildIntegration(const char *root, const char *fixture,
                                       unsigned int children, unsigned int bodies,
                                       size_t childBytes, float tolerance)
{
    char path[2048];
    size_t expectedBytes = 0;
    snprintf(path, sizeof(path), "%s/expected_integrated1.bin", fixture);
    unsigned char *expected = motionReadFile(path, &expectedBytes);
    if (!expected) return;
    unsigned int buffers[8] = {0}, program = 0;
    unsigned char *actual = NULL;
    CHECK(expectedBytes == childBytes, "emitted child integration reference loaded");
    if (expectedBytes != childBytes) goto cleanup;
    buffers[0] = children; buffers[6] = bodies;
    for (int binding = 1; binding < 8; binding++)
    {
        if (binding == 6) continue;
        size_t size = 0;
        snprintf(path, sizeof(path), "%s/integration/ssbo%d.bin", fixture, binding);
        unsigned char *data = motionReadFile(path, &size);
        if (!data || !size)
        { free(data); CHECK(0, "child Motion input SSBO loaded"); goto cleanup; }
        buffers[binding] = rlLoadShaderBuffer(size, data, RL_DYNAMIC_COPY);
        free(data);
        if (!buffers[binding] || buffers[binding] == 0xffffffffu)
        { CHECK(0, "child Motion input SSBO allocated"); goto cleanup; }
    }
    snprintf(path, sizeof(path), "%s/core/particles/shaders/gpu/particle_gpu.comp", root);
    char *source = motionReadShader(root, path, 0);
    CHECK(source != NULL, "emitted child production Motion shader expanded");
    if (!source) goto cleanup;
    unsigned int shader = rlLoadShader(source, RL_COMPUTE_SHADER);
    program = rlLoadShaderProgramCompute(shader);
    free(source);
    CHECK(program && program != 0xffffffffu, "emitted child production Motion compiled");
    if (!program || program == 0xffffffffu) goto cleanup;
    rlEnableShader(program);
    int dtLocation = rlGetLocationUniform(program, "u_dt");
    int timeLocation = rlGetLocationUniform(program, "u_time");
    CHECK(dtLocation >= 0 && timeLocation >= 0, "child Motion uniforms reflected");
    if (dtLocation < 0 || timeLocation < 0) { rlDisableShader(); goto cleanup; }
    float dt = 1.0f / 60.0f, time = 0;
    rlSetUniform(dtLocation, &dt, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(timeLocation, &time, RL_SHADER_UNIFORM_FLOAT, 1);
    for (int binding = 0; binding < 8; binding++) rlBindShaderBuffer(buffers[binding], binding);
    rlComputeShaderDispatch((unsigned int)(childBytes / 144 + 255) / 256, 1, 1);
    rlDisableShader();
    actual = malloc(childBytes);
    if (!actual) { CHECK(0, "child Motion readback allocated"); goto cleanup; }
    rlReadShaderBuffer(children, actual, childBytes, 0);
    int close = 1;
    float maximumError = 0;
    for (size_t word = 0; word < childBytes / 4; word++)
    {
        float got, want;
        memcpy(&got, actual + word * 4, 4); memcpy(&want, expected + word * 4, 4);
        float error = fabsf(got - want);
        if (!isfinite(got) || !isfinite(want) || !(error <= tolerance)) close = 0;
        if (error > maximumError) maximumError = error;
    }
    printf("      resident emitted child Motion parity: maxAbsError=%g tolerance=%g\n",
           maximumError, tolerance);
    CHECK(close, "resident GPU emitted children advance through production Motion");
cleanup:
    free(actual); free(expected);
    if (program && program != 0xffffffffu) rlUnloadShaderProgram(program);
    for (int binding = 1; binding < 8; binding++)
        if (binding != 6 && buffers[binding] && buffers[binding] != 0xffffffffu)
            rlUnloadShaderBuffer(buffers[binding]);
}

static void runEmitterEventsParity(const char *root, const char *fixture)
{
    char path[2048];
    unsigned int buffers[6] = {0}, program = 0, childSnapshot = 0, bodySnapshot = 0;
    size_t sizes[6] = {0};
    unsigned int parentCount = 0, steps = 1;
    float dt = 0, tolerance = 0;
    snprintf(path, sizeof(path), "%s/config.txt", fixture);
    FILE *file = fopen(path, "r");
    int configured = file && fscanf(file, "%u %f %f", &parentCount, &dt, &tolerance) == 3;
    if (configured) (void)fscanf(file, "%u", &steps);
    if (file) fclose(file);
    configured = configured && parentCount > 0 && parentCount <= 65536 &&
                 steps > 0 && steps <= 1024 && isfinite(dt) && dt > 0 &&
                 isfinite(tolerance) && tolerance >= 0;
    CHECK(configured, "production emitter fixture configured");
    if (!configured) return;
    for (int binding = 0; binding < 6; binding++)
    {
        snprintf(path, sizeof(path), "%s/ssbo%d.bin", fixture, binding);
        unsigned char *data = motionReadFile(path, &sizes[binding]);
        if (!data || !sizes[binding])
        { free(data); CHECK(0, "emitter fixture SSBO loaded"); goto cleanup; }
        buffers[binding] = rlLoadShaderBuffer(sizes[binding], data, RL_DYNAMIC_COPY);
        free(data);
        if (!buffers[binding] || buffers[binding] == 0xffffffffu)
        { CHECK(0, "emitter fixture SSBO allocated"); goto cleanup; }
    }
    int bounded = sizes[0] >= parentCount * 144u && sizes[1] % 144 == 0 &&
                  sizes[2] == sizes[1] / 144 * 192 && sizes[3] >= parentCount * 32u &&
                  sizes[4] % 352 == 0 && sizes[5] == 16;
    CHECK(bounded, "emitter fixture ABI and capacities valid");
    if (!bounded) goto cleanup;
    snprintf(path, sizeof(path), "%s/core/emitter/shaders/emitter_gpu.comp", root);
    char *source = motionReadShader(root, path, 0);
    CHECK(source != NULL, "production emitter shader loaded");
    if (!source) goto cleanup;
    unsigned int shader = rlLoadShader(source, RL_COMPUTE_SHADER);
    program = rlLoadShaderProgramCompute(shader);
    free(source);
    CHECK(program != 0 && program != 0xffffffffu, "production emitter compute compiled");
    if (!program || program == 0xffffffffu) goto cleanup;
    rlEnableShader(program);
    int dtLocation = rlGetLocationUniform(program, "u_dt");
    int countLocation = rlGetLocationUniform(program, "u_parentCount");
    CHECK(dtLocation >= 0 && countLocation >= 0, "production emitter uniforms reflected");
    if (dtLocation < 0 || countLocation < 0) { rlDisableShader(); goto cleanup; }
    int count = (int)parentCount;
    rlSetUniform(dtLocation, &dt, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(countLocation, &count, RL_SHADER_UNIFORM_INT, 1);
    for (unsigned int step = 0; step < steps; step++)
    {
        for (int binding = 0; binding < 6; binding++)
            rlBindShaderBuffer(buffers[binding], binding);
        rlComputeShaderDispatch((parentCount + 255) / 256, 1, 1);
    }
    rlDisableShader();
    /* Snapshot entirely on GPU, then run Motion before the first readback.
     * Keeping snapshots makes both birth and integration independently reviewable. */
    snprintf(path, sizeof(path), "%s/expected_integrated1.bin", fixture);
    file = fopen(path, "rb");
    if (file)
    {
        fclose(file);
        childSnapshot = rlLoadShaderBuffer(sizes[1], NULL, RL_DYNAMIC_COPY);
        bodySnapshot = rlLoadShaderBuffer(sizes[2], NULL, RL_DYNAMIC_COPY);
        if (!childSnapshot || childSnapshot == 0xffffffffu ||
            !bodySnapshot || bodySnapshot == 0xffffffffu)
        { CHECK(0, "emission GPU snapshots allocated"); goto cleanup; }
        rlCopyShaderBuffer(childSnapshot, buffers[1], 0, 0, sizes[1]);
        rlCopyShaderBuffer(bodySnapshot, buffers[2], 0, 0, sizes[2]);
        runEmissionChildIntegration(root, fixture, buffers[1], buffers[2], sizes[1], tolerance);
    }
    for (int binding = 1; binding < 6; binding++)
    {
        if (binding == 4) continue;
        size_t expectedSize = 0;
        snprintf(path, sizeof(path), "%s/expected%d.bin", fixture, binding);
        unsigned char *expected = motionReadFile(path, &expectedSize);
        if (!expected || expectedSize != sizes[binding])
        { free(expected); CHECK(0, "emitter expected SSBO loaded"); goto cleanup; }
        unsigned char *actual = malloc(expectedSize);
        if (!actual) { free(expected); CHECK(0, "emitter readback allocated"); goto cleanup; }
        unsigned int readBuffer = binding == 1 && childSnapshot ? childSnapshot :
                                  binding == 2 && bodySnapshot ? bodySnapshot : buffers[binding];
        rlReadShaderBuffer(readBuffer, actual, expectedSize, 0);
        snprintf(path, sizeof(path), "%s/actual%d.bin", fixture, binding);
        FILE *output = fopen(path, "wb");
        if (output) { fwrite(actual, 1, expectedSize, output); fclose(output); }
        int close = 1;
        float maximumError = 0;
        for (size_t word = 0; word < expectedSize / 4; word++)
        {
            if (emissionWordIsInteger(binding, word))
            {
                if (memcmp(actual + word * 4, expected + word * 4, 4)) close = 0;
            }
            else
            {
                float got, want;
                memcpy(&got, actual + word * 4, 4);
                memcpy(&want, expected + word * 4, 4);
                float error = fabsf(got - want);
                if (!isfinite(got) || !isfinite(want) || !(error <= tolerance)) close = 0;
                if (error > maximumError) maximumError = error;
            }
        }
        printf("      emission SSBO %d parity: steps=%u maxAbsError=%g tolerance=%g\n",
               binding, steps, maximumError, tolerance);
        CHECK(close, "production emitter GPU agrees with Core reference");
        free(actual); free(expected);
    }
cleanup:
    if (childSnapshot && childSnapshot != 0xffffffffu) rlUnloadShaderBuffer(childSnapshot);
    if (bodySnapshot && bodySnapshot != 0xffffffffu) rlUnloadShaderBuffer(bodySnapshot);
    if (program && program != 0xffffffffu) rlUnloadShaderProgram(program);
    for (int binding = 0; binding < 6; binding++)
        if (buffers[binding] && buffers[binding] != 0xffffffffu) rlUnloadShaderBuffer(buffers[binding]);
}
