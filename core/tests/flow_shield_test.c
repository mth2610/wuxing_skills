// Headless source contract — textured FlowShield must remain a separate VFX.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures = 0;
#define CHECK(condition, name) do { \
    if (condition) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); s_failures++; } \
} while (0)

static char *ReadFile(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    char *text = malloc((size_t)length + 1u);
    if (text) {
        fread(text, 1, (size_t)length, file);
        text[length] = '\0';
    }
    fclose(file);
    return text;
}

static int Has(const char *path, const char *needle)
{
    char *text = ReadFile(path);
    int found = text && strstr(text, needle) != NULL;
    free(text);
    return found;
}

int main(void)
{
    const char *shader = "core/shaders/shield_flow_shell.fs";
    const char *recipe = "core/composition/common/vc_flow_shield.inl";

    CHECK(Has(recipe, "cloud_noise.png"),
          "FlowShield uses the project low-frequency liquid-density texture");
    CHECK(Has(recipe, "energy_volume_flow.png"),
          "FlowShield binds a dedicated RG flow map");
    CHECK(Has(recipe, "VFX_ComposeFlowShield") && Has(recipe, "VFX_KillFlowShield"),
          "FlowShield is separately spawnable and killable");
    CHECK(Has(recipe, "rlDisableBackfaceCulling") && Has(recipe, "DrawCoreSphere"),
          "FlowShield submits both faces of its sphere");
    CHECK(Has(shader, "sampler2D texture0") && Has(shader, "u_flowTex") &&
          Has(recipe, "rlSetTexture(s_flowShieldMembrane.id)"),
          "fragment shader uses the mesh UV body-sheet contract plus a flow map");
    CHECK(Has(shader, "float phaseA") && Has(shader, "float phaseB") &&
          Has(shader, "mix(membraneA, membraneB, flowLerp)"),
          "fragment shader crossfades two flow phases without a scrolling seam");
    CHECK(Has(recipe, "SceneTargets_RequestSceneSnapshot") &&
          Has(recipe, "VFX_FlowShield_DrawRefraction") &&
          Has(shader, "u_sceneTex") && Has(shader, "refractionUV"),
          "liquid shell refracts the post-3D scene snapshot");
    CHECK(Has(recipe, "SceneTargets_RequestSoftDepthRegion") &&
          Has(shader, "DepthContact") && Has(shader, "u_depthTex"),
          "FlowShield uses the scene-depth intersection for its contact rim");
    CHECK(Has(shader, "float liquidCarrier = 0.34 + 1.28 * flowDetail") &&
          Has(shader, "float bodyCoverage = u_opacity * interfaceWeight") &&
          Has(shader, "float lowerVolume") && Has(shader, "liquidDensity") &&
          Has(shader, "brightCurrent") && Has(shader, "crestWater") &&
          Has(shader, "edgeGradient") && Has(shader, "rimCore") &&
          Has(shader, "contactCore") &&
          Has(shader, "VFX_ResolvePremultiplied") && !Has(shader, "VFX_ResolveEmission"),
          "flow decorates a continuous premultiplied liquid carrier with lower-volume energy");
    CHECK(Has(recipe, "flow_shield_glow") &&
          Has(recipe, "appearance.emissionIntensity * s_flowShieldGlow"),
          "filament emission has an independent HDR glow control");
    CHECK(!Has(shader, "angularGradient") && !Has(shader, "windowStart"),
          "FlowShield has no camera-centred transparency window");
    CHECK(Has("assets/INDEX.md", "energy_volume.png"),
          "membrane asset is catalogued");

    return s_failures == 0 ? 0 : 1;
}
