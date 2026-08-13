/* Contract for the reusable, one-shot lightning-arc composition.  This test
 * deliberately checks the render-graph split as well as the geometry policy:
 * a bolt that is only additive will wash out on a bright scene even when its
 * path generation is correct. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *s = (char *)malloc((size_t)n + 1u);
    if (s == NULL) { fclose(f); return NULL; }
    size_t got = fread(s, 1, (size_t)n, f);
    fclose(f);
    s[got] = '\0';
    return s;
}

static int Require(const char *source, const char *needle, const char *message)
{
    if (source != NULL && strstr(source, needle) != NULL) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int RequireNot(const char *source, const char *needle, const char *message)
{
    if (source != NULL && strstr(source, needle) == NULL) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void)
{
    int failed = 0;
    char *arc = ReadFile("core/composition/common/vc_lightning_arc.inl");
    char *strokeHeader = ReadFile("core/lightning/lightning_stroke.h");
    char *strokeSource = ReadFile("core/lightning/lightning_stroke.c");
    char *shader = ReadFile("core/lightning/shaders/lightning_stroke.fs");
    char *ribbonHeader = ReadFile("core/ribbon_strip.h");
    char *ribbonSource = ReadFile("core/ribbon_strip.c");
    char *api = ReadFile("core/composition/visual_composer.h");
    char *cmake = ReadFile("CMakeLists.txt");
    char *mainSource = ReadFile("main.c");
    char *fixture = ReadFile("sandbox/vfx_test.c");

    failed += Require(strokeHeader, "LightningStroke_Spawn", "dedicated lightning primitive is missing");
    failed += Require(strokeHeader, "LightningStroke_DrawLayer", "dedicated lightning renderer is missing");
    failed += Require(strokeHeader, "travelDuration", "lightning stroke needs a configurable source-to-target travel phase");
    failed += Require(strokeHeader, "postImpactDuration", "lightning needs configurable animated time after impact");
    failed += Require(strokeHeader, "coreEmission", "lightning stroke needs an HDR core-emission control");
    failed += Require(strokeHeader, "haloEmission", "lightning stroke needs a separate low-energy halo control");
    failed += Require(strokeSource, "LightningStroke_BuildPath", "stroke must build a geometric polyline");
    failed += Require(ribbonHeader, "Ribbon_GenerateMidpointDisplacement",
                      "Core must expose reusable midpoint displacement");
    failed += Require(ribbonSource, "outPoints[0] = from;", "midpoint source endpoint must remain exact");
    failed += Require(ribbonSource, "outPoints[1] = to;", "midpoint target endpoint must remain exact");
    failed += Require(ribbonSource, "amplitude *= resolved.amplitudeDecay;",
                      "each midpoint generation must reduce displacement amplitude");
    failed += Require(ribbonSource, "for (int segment = count - 2; segment >= 0; --segment)",
                      "midpoint generation must expand a fixed caller buffer in place");
    failed += Require(strokeSource, "Ribbon_GenerateMidpointDisplacement(",
                      "lightning must consume the shared midpoint primitive");
    failed += Require(strokeSource, "path.levels = length > 3.5f ? 4 : 3",
                      "optional lightning branches must use bounded midpoint detail");
    failed += RequireNot(strokeSource, "? 5 :", "midpoint branch path must not become a 32-segment cable");
    failed += Require(strokeSource, "branch.levels = 3;", "secondary branches must be cheaper than the trunk");
    failed += Require(strokeSource, "LIGHTNING_STROKE_MAX_BRANCHES", "secondary-branch budget is missing");
    failed += Require(arc, "ScreenDistort_BeginVFXBody();", "coloured body layer is missing");
    failed += Require(arc, "BeginBlendMode(BLEND_ALPHA);", "body must alpha blend");
    failed += Require(arc, "ScreenDistort_BeginVFXEmission();", "emission layer is missing");
    failed += Require(arc, "BeginBlendMode(BLEND_ADDITIVE);", "halo/core must additive blend");
    failed += RequireNot(arc, "VFXLight_Spawn(",
                         "reusable lightning must leave contact-light decisions to its owning skill");
    failed += RequireNot(arc, "Vector3 midpoint =",
                         "lightning must never place an area light in the middle of its air-gap");
    failed += Require(strokeSource, "LightningStroke_DrawWarpedPath",
                      "multi-point lightning needs a continuous path carrier");
    failed += Require(strokeSource, "Ribbon_ComputeArcLengthUV(carrier",
                      "path carrier must use one normalized arc-length UV domain");
    failed += Require(strokeSource, "LightningStroke_DrawWarpedSheet",
                      "two-point lightning must retain its proven canvas renderer");
    failed += Require(strokeSource, "LightningStroke_DrawWarpedSheet",
                      "lightning must own an endpoint-pinned procedural sheet renderer");
    failed += Require(cmake, "core/lightning/lightning_stroke.c", "dedicated lightning module is not built");
    failed += Require(strokeSource, "rlBegin(RL_QUADS)", "stroke canvas must use portable camera-facing geometry");
    failed += Require(strokeSource, "float canvasHalfWidth", "stroke needs a canvas around its warped filament");
    failed += Require(strokeSource, "rlDisableBackfaceCulling();",
                      "camera-facing lightning segments must not vanish to winding culling");
    failed += Require(strokeSource, "rlEnableBackfaceCulling();",
                      "lightning must restore culling after its segment pass");
    failed += Require(arc, "VFX_CONTRAST_ENERGY", "arc adapter must opt into shared energy contrast");
    failed += Require(arc, ".jaggedness = 0.80f", "default arc needs readable geometric displacement");
    failed += Require(arc, ".branchCount = 0", "minor branches must be opt-in, not a default hook");
    failed += Require(arc, "config.width = fmaxf(width, 0.075f)",
                      "convenience arc must not submit a sub-pixel gameplay ribbon");
    failed += Require(strokeSource, "core/lightning/shaders/lightning_stroke.fs",
                      "stroke must load its dedicated shader from the lightning module");
    failed += Require(strokeSource, "GetShaderLocation(s_shader.shader, \"u_travel\")",
                      "travel uniform must be cached with the lightning shader");
    failed += Require(strokeSource, "stroke->elapsed / stroke->config.travelDuration",
                      "travel progress must advance from elapsed stroke time");
    failed += Require(strokeSource, "resolved.lifetime = resolved.travelDuration + resolved.postImpactDuration",
                      "zero post-impact time must end the bolt exactly on target arrival");
    failed += Require(strokeSource, "settledTime * 9.0f",
                      "the bolt must keep electrically moving after impact");
    failed += Require(strokeSource, "LightningStroke_SmoothStep(0.58f, 1.0f, hold01)",
                      "the post-impact hold must fade smoothly rather than pop out");
    failed += Require(shader, "smoothstep", "arc shader needs an anti-aliased cross-strip silhouette");
    failed += Require(shader, "u_mode", "arc shader needs independent body and emission shaping");
    failed += Require(shader, "u_travel", "arc shader must receive source-to-target travel progress");
    failed += Require(shader, "u_lifeFade", "arc shader must fade the post-impact hold coherently");
    failed += Require(shader, "u_coreEmission", "arc shader must receive HDR core energy");
    failed += Require(shader, "u_haloEmission", "arc shader must receive low-energy halo strength");
    failed += Require(shader, "travelCoverage", "arc shader must reveal the bolt from source to target");
    failed += Require(shader, "travelHead", "arc shader must brighten the advancing discharge head");
    failed += Require(shader, "LightningStroke_EndpointTaper", "lightning needs a shared rounded endpoint profile");
    failed += Require(shader, "tipTaper", "all lightning layers must taper into their endpoints");
    failed += Require(shader, "endpointContact", "source and target must receive a compact contact glint");
    failed += Require(shader, "targetContact", "target glint must wait for the travelling head");
    failed += Require(shader, "emissionCore", "endpoint contact must contribute to the HDR core, not the blue body");
    failed += Require(shader, "u_lineWidth * 0.18", "blue body must remain much thinner than its energy field");
    failed += Require(arc, ".travelDuration = 0.10f",
                      "arc default must leave enough frames to read source-to-target travel");
    failed += Require(shader, "uv.x * 2.0 - 1.0", "shader must shape across the canvas width");
    failed += Require(shader, "LightningStroke_FilamentDistance", "shader must measure distance from its warped centreline");
    failed += Require(shader, "fbm2N(domain, 4)", "shader needs macro FBM domain warp");
    failed += Require(shader, "fbm2N(domain * 3.7", "shader needs micro FBM filament detail");
    failed += Require(shader, "innerCorona", "emission must bridge the white core into the blue field");
    failed += Require(shader, "innerColour", "corona must blend core and field colours continuously");
    failed += Require(shader, "outerColour", "far energy field must desaturate independently from the inner corona");
    failed += Require(shader, "coronaEnergy", "inner saturated corona must remain visible beside the HDR core");
    failed += Require(shader, "float coronaEnergy = 2.05",
                      "near-core field needs enough energy to read without widening into a band");
    failed += Require(shader, "halo *= halo * halo", "outer field must fade faster than the inner corona");
    failed += Require(shader, "endpointPin", "procedural warp must preserve exact source and target");
    failed += Require(shader, "u_lineWidth * 1.52", "halo must use the authored soft-field radius");
    failed += Require(arc, ".postImpactDuration = 0.30f",
                      "default lightning must visibly arc after it reaches target");
    failed += RequireNot(shader, "EnergyField_ArcProfile", "lightning must not widen itself as a ribbon field");
    failed += Require(api, "VFX_LightningArc_Spawn", "public one-shot lightning API is missing");
    failed += Require(api, "VFX_ComposeLightningArc", "generated-fixture entry point is missing");
    failed += Require(api, "VFX_LightningArcConfig", "lightning config contract is missing");
    failed += Require(api, "travelDuration", "composition API must expose the lightning travel phase");
    failed += Require(mainSource, "Shared composition pools (including one-shot LightningArc)",
                      "game scene does not submit shared composition pools");
    failed += Require(fixture, "Vector3 castSocket = Vector3Add(playerPos",
                      "click fixture does not start from the character socket");
    failed += Require(fixture, "VFX_ComposeLightningArc(castSocket, mouseTarget3D, VC_MAT_LIGHTNING, 0.055f)",
                      "click fixture does not use the live world target and lightning profile");

    free(arc);
    free(shader);
    free(strokeHeader);
    free(strokeSource);
    free(ribbonHeader);
    free(ribbonSource);
    free(api);
    free(cmake);
    free(mainSource);
    free(fixture);
    if (failed != 0) return 1;
    puts("PASS: lightning arc geometry and bright-background render contract are present");
    return 0;
}
