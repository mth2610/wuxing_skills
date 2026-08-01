/* Headless contract test: backend choice must stay per-emitter and must not
 * regress into graphics-capability queries in Update/Draw. Run standalone. */
#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    char text[1024]; size_t used = 0, got;
    if (!f) return 0;
    while ((got = fread(text + used, 1, sizeof(text) - 1 - used, f)) != 0) {
        used += got; text[used] = '\0';
        if (strstr(text, needle)) { fclose(f); return 1; }
        /* Retain enough overlap for the longest tested token. */
        if (used > 256) { memmove(text, text + used - 256, 256); used = 256; }
    }
    fclose(f); return 0;
}

int main(void)
{
    int bad = 0;
    bad += !Has("core/particles/particle_manager.c", "ParticleManager_GPUCanRun");
    bad += !Has("core/particles/particle_manager.c", "PARTICLE_SIM_GPU_ONLY && !gpuOK");
    bad += !Has("core/particles/particle_manager.c", "ParticleManager_Update(float dt) { if (!s_initialized) return; UpdateParticles(dt); GpuParticleSystem_Update(dt);");
    bad += !Has("core/particles/particle_manager.c", "ParticleManager_Draw(Camera3D c, Texture2D t) { if (!s_initialized) return; DrawParticles(c, t); GpuParticleSystem_Draw(c, t);");
    bad += !Has("core/particles/particle_system.c", "ParticleManager_SpawnCompatibility(config)");
    printf("particle manager contract: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
