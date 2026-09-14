#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core/vfx_config.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(expr, msg) do { \
    g_checks++; \
    if (!(expr)) { \
        printf("FAIL: %s\n", (msg)); \
        g_failures++; \
    } else { \
        printf("PASS: %s\n", (msg)); \
    } \
} while (0)

static int FileContains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t readCount = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[readCount] = '\0';
    int found = (strstr(buf, needle) != NULL);
    free(buf);
    return found;
}

static void Test_WindInfluenceContracts(void)
{
    VFX_PhysicsConfig phys;
    memset(&phys, 0, sizeof(phys));
    phys.windInfluence = 0.85f;
    CHECK(fabsf(phys.windInfluence - 0.85f) < 1e-5f,
          "VFX_PhysicsConfig exposes windInfluence field");

    const char *ps_h = "core/particles/particle_system.h";
    CHECK(FileContains(ps_h, "float windInfluence;") == 1,
          "particle_system.h exposes windInfluence on ParticleConfig");
    CHECK(FileContains(ps_h, "cfg->physics.windInfluence = cfg->windInfluence;") == 1,
          "ParticleConfig_Unify propagates windInfluence from flat to unified physics config");

    const char *gpu_h = "core/particles/gpu/particle_gpu_legacy.h";
    CHECK(FileContains(gpu_h, "float   windInfluence;") == 1,
          "particle_gpu_legacy.h exposes windInfluence on GpuParticleConfig");
}

static void Test_CompositionFilesCoupledToWind(void)
{
    CHECK(FileContains("core/composition/common/vc_smoke_puff.inl", ".windInfluence = 0.85f") == 1,
          "vc_smoke_puff.inl sets windInfluence for smoke puffs");
    CHECK(FileContains("core/composition/common/vc_ember_trail.inl", ".windInfluence = 1.0f") == 1,
          "vc_ember_trail.inl sets windInfluence for airborne embers");
    CHECK(FileContains("core/composition/fire/flame_volume.inl", ".windInfluence = 0.65f") == 1,
          "flame_volume.inl sets windInfluence for flame body");
    CHECK(FileContains("core/composition/fire/flame_volume.inl", ".windInfluence = 0.85f") == 1,
          "flame_volume.inl sets windInfluence for combustion smoke");
}

static void Test_ShaderOpticalModels(void)
{
    const char *fs = "core/particles/shaders/particle_lit.fs";
    const char *comp = "core/particles/shaders/gpu/particle_gpu.comp";

    CHECK(FileContains(fs, "Two-lobe Henyey-Greenstein") == 1,
          "particle_lit.fs implements Two-Lobe Henyey-Greenstein scattering model");
    CHECK(FileContains(fs, "CompressFlameRadiance") == 1,
          "particle_lit.fs applies Planck chromaticity-preserving radiance compression");
    CHECK(FileContains(fs, "Alpha erosion gradient perturbation") == 1,
          "particle_lit.fs implements alpha erosion normal perturbation (Ghost of Tsushima n ~ grad rho)");
    CHECK(FileContains(fs, "ParticleTangentBasis") == 1,
          "particle_lit.fs implements screen derivative tangent basis reconstruction");

    CHECK(FileContains(comp, "wind_influence") == 1,
          "particle_gpu.comp documents impact_data.z as wind_influence in GpuParticleData");
    CHECK(FileContains(comp, "evalWindVelocity(samplePos, u_time)") == 1,
          "particle_gpu.comp integrates evalWindVelocity for wind-influenced particles");
}

int main(void)
{
    printf("=== core headless test: smoke & fire wind & optical shading ===\n");
    Test_WindInfluenceContracts();
    Test_CompositionFilesCoupledToWind();
    Test_ShaderOpticalModels();

    printf("---\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
