#include "core/particles/gpu/particle_gpu_work_gate.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/* Exercises the same admission/clock function used by the renderer. It cannot
 * observe GPU fences or image output; runtime counters/captures cover those. */
#define CHECK(x) do { if (!(x)) { bad++; printf("FAIL: %s\n", #x); } } while (0)
int main(void)
{
    int bad = 0;
    float elapsed = 0.0f;
    CHECK(!GpuParticleWork_BeginUpdate(false, true, false, 0.25f, &elapsed));
    CHECK(elapsed == 0.0f);
    for (int i = 0; i < 4; ++i)
        CHECK(!GpuParticleWork_BeginUpdate(true, true, false, 0.25f, &elapsed));
    CHECK(elapsed == 1.0f);
    CHECK(GpuParticleWork_BeginUpdate(true, true, true, 0.25f, &elapsed));
    CHECK(elapsed == 1.25f);
    /* Paused and post-spawn updates remain admitted; no CPU/GPU lifetime
     * approximation is allowed to drop delayed collision or burst events. */
    CHECK(GpuParticleWork_BeginUpdate(true, true, true, 0.0f, &elapsed));
    CHECK(elapsed == 1.25f);
    CHECK(!GpuParticleWork_BeginUpdate(true, false, false, 0.25f, &elapsed));
    CHECK(elapsed == 1.25f);
    CHECK(GpuParticleWork_BeginUpdate(true, false, true, 0.25f, &elapsed));
    CHECK(elapsed == 1.25f);
    /* Init resets spawn state and elapsed; the next first burst is admitted. */
    elapsed = 0.0f;
    CHECK(!GpuParticleWork_BeginUpdate(true, true, false, 0.25f, &elapsed));
    CHECK(GpuParticleWork_BeginUpdate(true, true, true, 0.25f, &elapsed));
    CHECK(elapsed == 0.5f);
    /* The behavioral probe is meaningful only while the production backend
     * uses it before issuing GPU work and wires the lifecycle flags. */
    static char host[128 * 1024];
    FILE *file = fopen("core/particles/gpu/particle_gpu_backend.c", "rb");
    CHECK(file != NULL);
    if (file) {
        size_t count = fread(host, 1, sizeof(host) - 1, file);
        host[count] = '\0';
        CHECK(feof(file));
        fclose(file);
        const char *update = strstr(host, "void GpuParticleSystem_Update(float dt)");
        const char *gate = update ? strstr(update, "GpuParticleWork_BeginUpdate(") : NULL;
        const char *upload = update ? strstr(update, "rlUpdateShaderBuffer(") : NULL;
        CHECK(gate != NULL && upload != NULL && gate < upload);
        CHECK(strstr(host, "s_hasSpawned = false;") != NULL);
        CHECK(strstr(host, "s_hasSpawned = true;") != NULL);
        CHECK(strstr(host, "if (!s_initialized || !s_hasSpawned)") != NULL);
        CHECK(strstr(host, "WUXING_GPU_IDLE_CONTROL") == NULL);
    }
    printf("GPU idle work gate: %s\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}
