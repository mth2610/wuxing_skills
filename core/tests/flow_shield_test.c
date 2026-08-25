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
    /* This assertion used to pin `float liquidCarrier = 0.34 + 1.28 * flowDetail`
     * by its literal text, along with two variable names that no longer exist.
     * A test that spells out a tuning constant does not protect a look, it
     * freezes a revision — and this one froze the exact coefficient that made
     * the shell an opaque ball (a single layer reached ~0.70 coverage, twice,
     * for ~0.91 total). It failed the moment the shell was made transparent,
     * which was the whole point of the change. Assert the STRUCTURE instead. */
    CHECK(Has(shader, "float bodyCoverage = u_opacity * interfaceWeight") &&
          Has(shader, "float veinCoverage") && Has(shader, "float rimCoverage") &&
          Has(shader, "edgeGradient") && Has(shader, "rimCore") &&
          Has(shader, "contactCore") &&
          Has(shader, "VFX_ResolvePremultiplied") && !Has(shader, "VFX_ResolveEmission"),
          "coverage is built from separable body / vein / rim terms on one "
          "premultiplied carrier");

    /* The three things that make it a bubble rather than a planet, each of
     * which was measured absent before 25/08/2026. */
    CHECK(Has(shader, "ShieldVeins") && Has(shader, "ShieldRidge"),
          "structure is a ridged-noise filament network — an area fbm gives "
          "evenly-spread blobs, which is what the cloud sheet was doing");
    /* NOT asserted: that the network is clumped. It was, briefly; the owner
     * judged it against the reference and chose an even distribution, which is
     * a look decision and not this file's to pin. Nor that sparks exist —
     * specks are a particle job and were deliberately removed from the shader.
     * Both are recorded in the shader's own comments instead. */
    CHECK(Has(shader, "ShieldDir") && Has(shader, "vnoise3"),
          "the noise is anchored to the sphere's own direction in 3D: view-space "
          "fragNormal swims under camera rotation, and 2D noise on the raw UV "
          "seams at u = 0 and pinwheels at the poles");
    CHECK(Has(shader, "veinInk"),
          "veins carry saturated element pigment in the BODY pass — tinting them "
          "white there is invisible against white scenery");
    CHECK(Has(recipe, "VFXLight_Spawn"),
          "the shell lights the ground it sits on");
    CHECK(Has(recipe, "flow_shield_glow") &&
          Has(recipe, "appearance.emissionIntensity * s_flowShieldGlow"),
          "filament emission has an independent HDR glow control");
    CHECK(!Has(shader, "angularGradient") && !Has(shader, "windowStart"),
          "FlowShield has no camera-centred transparency window");
    CHECK(Has("assets/INDEX.md", "energy_volume.png"),
          "membrane asset is catalogued");

    return s_failures == 0 ? 0 : 1;
}
