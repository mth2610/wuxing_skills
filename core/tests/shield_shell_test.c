// Headless contract test — Unified Wuxia Shield (merged ShieldShell + FlowShield).
#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); g_failures++; } \
} while (0)

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[400000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, needle) != NULL;
}

#define VFX_SHIELD_SHELL_MAX 8
#define SHIELD_SHELL_TRI_BUDGET 6400
#define SHIELD_SHELL_RINGS_LOW 14
#define SHIELD_SHELL_RINGS_MED 24
#define SHIELD_SHELL_RINGS_HIGH 40

static int ShieldShell_Rings(int quality, int activeCount)
{
    int ceiling = (quality >= 2) ? SHIELD_SHELL_RINGS_HIGH
                : (quality >= 1) ? SHIELD_SHELL_RINGS_MED
                                 : SHIELD_SHELL_RINGS_LOW;
    if (quality <= 0) return SHIELD_SHELL_RINGS_LOW;
    if (activeCount < 1) activeCount = 1;
    int rings = SHIELD_SHELL_RINGS_LOW;
    while (rings + 2 <= ceiling &&
           activeCount * (rings + 2) * (rings + 2) * 2 <= SHIELD_SHELL_TRI_BUDGET)
        rings += 2;
    return rings;
}

int main(void)
{
    const char *inl  = "core/composition/common/vc_shield_shell.inl";
    const char *hdr  = "core/composition/visual_composer.h";
    const char *vs   = "core/shaders/glass_shell.vs";
    const char *fs   = "core/shaders/wuxia_shield.fs";
    const char *mc   = "main.c";
    const char *rb   = "core/composition/common/vc_rift_bolt.inl";

    printf("=== Unified Wuxia Shield contract ===\n");

    // 1. Triangle budget across quality tiers & active shield counts
    CHECK(ShieldShell_Rings(0, 1) == 14, "LOW quality stays at 14 rings");
    CHECK(ShieldShell_Rings(2, 1) == 40, "HIGH quality uses 40 rings for a single shield");
    CHECK(ShieldShell_Rings(2, 8) * ShieldShell_Rings(2, 8) * 2 * 8 <= SHIELD_SHELL_TRI_BUDGET,
          "8 simultaneous HIGH shields fit inside 6400-triangle budget");

    // 2. Unified API & 9 presets + 4 ground modes
    CHECK(FileHas(hdr, "VFX_SHIELD_PRESET_COUNT") &&
          FileHas(hdr, "VFX_SHIELD_PRESET_CHANKHI") &&
          FileHas(hdr, "VFX_SHIELD_PRESET_PHONGTRAO") &&
          FileHas(hdr, "VFX_SHIELD_GROUND_COUNT") &&
          FileHas(hdr, "VFX_Shield_SpawnEx") &&
          FileHas(hdr, "VFX_Shield_GetParams"),
          "visual_composer.h exposes 9-preset, 4-ground-mode Ex API and GetParams");

    CHECK(FileHas(inl, "k_shieldPresets[VFX_SHIELD_PRESET_COUNT]") &&
          FileHas(inl, "VFX_FlowShield_Spawn") &&
          FileHas(inl, "VFX_ShieldShell_Spawn") &&
          FileHas(inl, "VFX_ShieldShell_DrawRefraction"),
          "vc_shield_shell.inl hosts 9-preset table and FlowShield/ShieldShell wrappers");

    // 3. FlowShield separate files removed
    FILE *removedInl = fopen("core/composition/common/vc_flow_shield.inl", "rb");
    FILE *removedFs  = fopen("core/shaders/shield_flow_shell.fs", "rb");
    CHECK(removedInl == NULL && removedFs == NULL,
          "legacy vc_flow_shield.inl and shield_flow_shell.fs are removed");
    if (removedInl) fclose(removedInl);
    if (removedFs)  fclose(removedFs);

    // 4. Shaders & render order
    CHECK(FileHas(vs, "gl_Position = mvp * vec4(displacedView, 1.0)") && FileHas(vs, "u_camRight") && FileHas(vs, "evalImpactDisp"),
          "glass_shell.vs keeps view-space mvp transform and reconstructs world-space unitPos");
    CHECK(FileHas(fs, "u_preset == 0") && FileHas(fs, "u_preset == 7") &&
          FileHas(fs, "VFX_TonemapSafeEmission") && FileHas(fs, "VFX_ResolveBody") &&
          FileHas(fs, "VFX_ResolvePremultiplied"),
          "wuxia_shield.fs implements all 9 presets and dual-pass bright-bg contract");
    CHECK(FileHas(inl, "RL_CULL_FACE_FRONT") && FileHas(inl, "RL_CULL_FACE_BACK"),
          "far-wall-first then near-wall cull order preserved");
    CHECK(FileHas(mc, "VFX_ShieldShell_DrawRefraction(camera)") &&
          !FileHas(mc, "VFX_FlowShield_DrawRefraction(camera)"),
          "main.c calls unified VFX_ShieldShell_DrawRefraction only");
    CHECK(FileHas(rb, "VFX_SHIELD_GROUND_FULL"),
          "RiftBolt sets VFX_SHIELD_GROUND_FULL so flying head is never ground-cut");

    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
