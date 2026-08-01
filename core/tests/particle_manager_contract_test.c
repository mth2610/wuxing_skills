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
    bad += !Has("core/particles/particle_manager.c", "void ParticleManager_DrawBody(Camera3D c, Texture2D t)");
    bad += !Has("core/particles/particle_manager.c", "void ParticleManager_DrawEmission(Camera3D c, Texture2D t)");
    bad += !Has("core/particles/particle_manager.c", "GpuParticleSystem_Draw(c, t);");
    bad += !Has("main.c", "ScreenDistort_BeginVFXBody();");
    bad += !Has("main.c", "ScreenDistort_BeginVFXEmission();");
    bad += !Has("main.c", "ParticleManager_DrawBody(camera, globalParticleTex);");
    bad += !Has("main.c", "ParticleManager_DrawEmission(camera, globalParticleTex);");
    bad += !Has("core/particles/particle_system.c", "ParticleManager_SpawnCompatibility(config)");
    bad += !Has("core/particles/gpu/particle_gpu_backend.c", "ScreenDistort_BindDepthForSoftParticles(drawShader, GPU_PARTICLE_SOFT_DEPTH_SLOT)");
    bad += !Has("core/particles/gpu/particle_gpu_backend.c", "rlDisableDepthMask();");
    bad += !Has("core/particles/gpu/particle_gpu_backend.c", "rlDisableDepthTest();");
    bad += !Has("core/particles/shaders/gpu/particle_gpu.fs", "SoftParticle_Factor(u_softFade)");
    printf("particle manager contract: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
