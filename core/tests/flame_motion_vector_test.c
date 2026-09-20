// core headless test — Flame Motion Vector optical flow subframe advection.
//
// Tests contract adherence for optical flow motion vectors in flipbooks:
// 1. Texture asset existence and valid 2048x2048 PNG header
// 2. VFX surface profile registration in JSON
// 3. VFX config and particle system integration (single quad CPU emission, subframeT encoding)
// 4. Particle lit shader optical flow forward/backward advection logic
// 5. Flame volume composition integration

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

static int FileContains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 2000000) { fclose(f); return 0; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t r = fread(buf, 1, sz, f);
    buf[r] = '\0';
    fclose(f);
    int found = (strstr(buf, needle) != NULL);
    free(buf);
    return found;
}

static void Test_TextureFile(void)
{
    const char *path = "assets/textures/pure_flame_puff_motion_8x8.png";
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "pure_flame_puff_motion_8x8.png exists");
    if (!f) return;

    uint8_t header[24];
    size_t read = fread(header, 1, sizeof(header), f);
    fclose(f);

    CHECK(read >= 24, "pure_flame_puff_motion_8x8.png has at least 24 bytes");
    // PNG signature: 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    const uint8_t pngSig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    CHECK(memcmp(header, pngSig, 8) == 0, "pure_flame_puff_motion_8x8.png is valid PNG");

    // IHDR dimensions at offset 16 (big endian)
    uint32_t width = ((uint32_t)header[16] << 24) | ((uint32_t)header[17] << 16) |
                     ((uint32_t)header[18] << 8)  | header[19];
    uint32_t height = ((uint32_t)header[20] << 24) | ((uint32_t)header[21] << 16) |
                      ((uint32_t)header[22] << 8)  | header[23];
    CHECK(width == 2048 && height == 2048, "pure_flame_puff_motion_8x8.png dimensions are 2048x2048 (8x8 256x256 cells)");
}

static void Test_SurfaceProfile(void)
{
    CHECK(FileContains("assets/vfx_surface_profiles.json", "pure_flame_puff_motion_8x8.png"),
          "vfx_surface_profiles.json registers pure_flame_puff_motion_8x8.png as flow texture for fire_volume");
    CHECK(FileContains("assets/vfx_surface_profiles.json",
                       "MOTION | R:flowx/CLAMP | G:flowy/CLAMP | B:speed/CLAMP | A:mask/CLAMP"),
          "motion atlas uses the optical-flow flipbook channel contract");
    CHECK(FileContains("scripts/validate_vfx_surface_registry.py", "\"MOTION\":"),
          "surface validator recognizes optical-flow atlases");
    CHECK(FileContains("scripts/validate_vfx_surface_registry.py",
                       "CELLED = (\"FLIPBOOK\", \"VOLUME\", \"MOTION\", \"LIGHT6\", \"SMOKE_EOO\", \"FIRE_EOO\", \"NORMAL_XY\")"),
          "surface validator permits cells only on celled motion atlases");
    CHECK(FileContains("core/vfx_surface_registry.generated.inl", "pure_flame_puff_motion_8x8.png"),
          "vfx_surface_registry.generated.inl has generated flowPath for fire_volume");
}

static void Test_EngineContracts(void)
{
    CHECK(FileContains("core/vfx_config.h", "Texture2D motionTex;"),
          "VFX_RenderConfig declares motionTex");
    CHECK(FileContains("core/vfx_config.h", "float motionWarpScale;"),
          "VFX_RenderConfig declares motionWarpScale");

    CHECK(FileContains("core/particles/particle_system.c", "u_useMotionVectors"),
          "particle_system.c manages u_useMotionVectors uniform location");
    CHECK(FileContains("core/particles/particle_system.c", "u_motionTex"),
          "particle_system.c manages u_motionTex uniform location");
    CHECK(FileContains("core/particles/particle_system.c", "u_motionWarp"),
          "particle_system.c manages u_motionWarp uniform location");
    CHECK(FileContains("core/particles/particle_system.c", "fbBlend = 0.0f;"),
          "particle_system.c suppresses second CPU quad when motion vectors are active");

    CHECK(FileContains("core/particles/shaders/particle_lit.fs", "u_useMotionVectors"),
          "particle_lit.fs declares u_useMotionVectors");
    CHECK(FileContains("core/particles/shaders/particle_lit.fs", "u_motionTex"),
          "particle_lit.fs declares u_motionTex");
    CHECK(FileContains("core/particles/shaders/particle_lit.fs", "clamp(fragColor.b, 0.0, 1.0)"),
          "particle_lit.fs unpacks subframeT from fragColor.b");
    CHECK(FileContains("core/particles/shaders/particle_lit.fs", "uvA_warped"),
          "particle_lit.fs computes forward advection uvA_warped");
    CHECK(FileContains("core/particles/shaders/particle_lit.fs", "uvB_warped"),
          "particle_lit.fs computes backward advection uvB_warped");

    CHECK(FileContains("core/composition/fire/flame_volume.inl", "s_fvolMotionTex"),
          "flame_volume.inl stores s_fvolMotionTex");
    CHECK(FileContains("core/composition/fire/flame_volume.inl", "flame_motion_warp"),
          "flame_volume.inl registers flame_motion_warp tunable");
    CHECK(FileContains("core/composition/fire/flame_volume.inl", ".render.motionTex = s_fvolMotionTex"),
          "flame_volume.inl binds motionTex to volume particle render config");
}

int main(void)
{
    printf("=== flame motion vector: optical flow subframe advection ===\n");
    Test_TextureFile();
    Test_SurfaceProfile();
    Test_EngineContracts();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
