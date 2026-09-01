/* Headless contract for gas raymarch quality.
 *
 * The numeric mirrors below can distinguish coherent ray-step banding from
 * decorrelated integration error and a one-tone flame from a hot-core ramp.
 * They cannot render a Vulkan image, so the final block also pins the shader
 * expressions that carry those behaviours to the GPU implementation.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "gas_volume_quality_test: check failed at line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct TestColor3 {
    float r;
    float g;
    float b;
} TestColor3;

static float Fract(float value) {
    return value - floorf(value);
}

/* Must match Gas_RayJitter(). This hash varies in both screen axes and avoids
 * turning a fixed raymarch phase error into a row or column shared by pixels. */
static float RayJitter(int x, int y) {
    return Fract(52.9829189f * Fract((float)x * 0.06711056f +
                                    (float)y * 0.00583715f));
}

static float LayeredDensity(float z) {
    float broad = expf(-22.0f * (z - 0.47f) * (z - 0.47f));
    float detail = 0.68f + 0.32f * sinf(z * 91.0f + 0.7f);
    return broad * detail;
}

static float IntegrateLayeredDensity(float jitter) {
    const int steps = 16;
    const float stepLength = 1.0f / (float)steps;
    float sum = 0.0f;
    for (int i = 0; i < steps; ++i) {
        float z = ((float)i + jitter) * stepLength;
        sum += LayeredDensity(z) * stepLength;
    }
    return sum;
}

static float IntegrateReference(void) {
    const int steps = 4096;
    float sum = 0.0f;
    for (int i = 0; i < steps; ++i)
        sum += LayeredDensity(((float)i + 0.5f) / (float)steps) / (float)steps;
    return sum;
}

static float Luma(TestColor3 color) {
    return color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f;
}

static float Chroma(TestColor3 color) {
    float high = fmaxf(color.r, fmaxf(color.g, color.b));
    float low = fminf(color.r, fminf(color.g, color.b));
    return high - low;
}

static TestColor3 Mix(TestColor3 a, TestColor3 b, float t) {
    return (TestColor3){a.r + (b.r - a.r) * t,
                        a.g + (b.g - a.g) * t,
                        a.b + (b.b - a.b) * t};
}

static float SmoothStep(float edge0, float edge1, float value) {
    float t = (value - edge0) / (edge1 - edge0);
    t = fmaxf(0.0f, fminf(1.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

static int CountSubstring(const char *text, const char *needle) {
    int count = 0;
    size_t length = strlen(needle);
    while ((text = strstr(text, needle)) != NULL) {
        ++count;
        text += length;
    }
    return count;
}

/* Mirrors the one-sample background response planned for gas_volume.fs. The
 * response shapes optical signals; it must not add a second raymarch tap. */
static float BackgroundAdapt(float luma) {
    return SmoothStep(0.22f, 0.88f, luma);
}

static float AdaptDensity(float density, float adapt) {
    float shape = 0.78f + (1.38f - 0.78f) *
                  SmoothStep(0.08f, 0.52f, density);
    return density * (1.0f + (shape - 1.0f) * adapt);
}

static float AdaptBodyTone(int kind, float adapt) {
    const float brightGain[3] = {0.72f, 0.78f, 0.66f};
    return 1.0f + (brightGain[kind] - 1.0f) * adapt;
}

static float AdaptEmission(int kind, float coreWeight, float adapt) {
    float floorGain = kind == 1 ? 0.48f : 0.62f;
    float brightScale = kind == 1 ? 0.50f : 0.68f;
    float broadGain = floorGain + (1.0f - floorGain) * coreWeight;
    return broadGain * (1.0f + (brightScale - 1.0f) * adapt);
}

static TestColor3 Scale(TestColor3 color, float scale) {
    return (TestColor3){color.r * scale, color.g * scale, color.b * scale};
}

typedef struct TestFireOptics {
    float bodyOpacity;
    TestColor3 bodyTone;
    float bodyLight;
} TestFireOptics;

/* Mirror of the GAS_FIRE body branch. This describes how much background the
 * carrier absorbs and prevents self-lit flame from inheriting smoke shadows. */
static TestFireOptics FireOptics(float heat, float reaction, TestColor3 body,
                                 TestColor3 emission, float coarseNoise,
                                 float softLight) {
    float flameBody = SmoothStep(0.05f, 0.55f, heat);
    float fireActivity = SmoothStep(0.10f, 0.60f, heat) *
                         SmoothStep(0.06f, 0.55f, reaction);
    float bodyOpacity = 0.26f * fireActivity;
    TestColor3 coolBody = Mix(body, Scale(emission, 0.46f), 0.68f);
    TestColor3 hotBody = Mix(body, Scale(emission, 0.72f), 0.88f);
    TestColor3 bodyTone = Mix(coolBody, hotBody, flameBody);
    bodyTone = Scale(bodyTone, 0.94f + (1.08f - 0.94f) * coarseNoise);
    float litResidue = fmaxf(softLight, 0.55f);
    float bodyLight = litResidue + (1.0f - litResidue) * flameBody;
    return (TestFireOptics){bodyOpacity, bodyTone, bodyLight};
}

static float EnergyBodyOpacity(float heat, float reaction) {
    float energyActivity = SmoothStep(0.08f, 0.52f, heat) *
                           SmoothStep(0.04f, 0.45f, reaction);
    return 0.28f * energyActivity;
}

/* Hot reaction must carry radiance independently from the smoke/body coverage.
 * Otherwise lowering fire opacity dims the flame by the same amount. */
static float FireEmissionAlpha(float densityAlpha, float bodyOpacity,
                               float heat, float reaction) {
    float flameTransport = SmoothStep(0.08f, 0.55f, heat) *
                           SmoothStep(0.06f, 0.60f, reaction);
    float bodyAlpha = densityAlpha * bodyOpacity;
    return bodyAlpha + (densityAlpha - bodyAlpha) * flameTransport;
}

static char *ReadFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = (char *)malloc((size_t)length + 1u);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    size_t count = fread(text, 1, (size_t)length, file);
    text[count] = '\0';
    fclose(file);
    return text;
}

static int TestRayPhaseDecorrelation(void) {
    float coherent = IntegrateLayeredDensity(0.5f);
    float mean = 0.0f;
    float variance = 0.0f;
    int distinct = 0;
    float previous = -1.0f;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float sample = IntegrateLayeredDensity(RayJitter(x, y));
            mean += sample;
            if (previous < 0.0f || fabsf(sample - previous) > 1.0e-5f) ++distinct;
            previous = sample;
        }
    }
    mean /= 64.0f;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float sample = IntegrateLayeredDensity(RayJitter(x, y));
            variance += (sample - mean) * (sample - mean);
        }
    }
    variance /= 64.0f;

    /* Fixed half-step sampling makes all 64 pixels carry the same integration
     * error. Jitter preserves the mean but breaks that error into fine noise. */
    CHECK(distinct > 48);
    CHECK(variance > 1.0e-6f);
    float reference = IntegrateReference();
    CHECK(fabsf(mean - reference) < fabsf(coherent - reference) * 0.10f);
    CHECK(fabsf(RayJitter(5, 2) - RayJitter(5, 3)) > 0.01f);
    CHECK(fabsf(RayJitter(5, 2) - RayJitter(6, 2)) > 0.01f);
    return 0;
}

static int TestFireCoreRamp(void) {
    TestColor3 outer = {1.0f, 0.22f, 0.025f};
    TestColor3 shoulder = Mix(outer, (TestColor3){1.0f, 0.62f, 0.12f}, 0.72f);
    TestColor3 core = Mix(shoulder, (TestColor3){1.0f, 0.88f, 0.55f}, 0.90f);
    CHECK(Luma(shoulder) > Luma(outer) * 1.45f);
    CHECK(Luma(core) > Luma(shoulder) * 1.35f);
    CHECK(Chroma(core) < Chroma(shoulder) * 0.55f);
    CHECK(core.r > core.g && core.g > core.b);
    return 0;
}

static float FireCoreWeight(float hotProduct, float rawDensity,
                            float coarseNoise, float detailNoise) {
    float hotGate = SmoothStep(0.34f, 0.78f, hotProduct);
    float densityGate = SmoothStep(0.30f, 0.72f, rawDensity);
    float detailCore = 0.58f + 0.42f *
                       SmoothStep(0.28f, 0.82f, detailNoise);
    float coarseCore = 0.72f + 0.28f *
                       SmoothStep(0.32f, 0.80f, coarseNoise);
    return hotGate * densityGate * detailCore * coarseCore * 0.42f;
}

static float EnergyCoreWeight(float heat, float reaction, float rawDensity,
                              float coarseNoise, float detailNoise) {
    float energyGate = SmoothStep(0.30f, 0.75f, heat) *
                       SmoothStep(0.16f, 0.58f, reaction);
    float densityGate = SmoothStep(0.25f, 0.68f, rawDensity);
    float detailCore = 0.72f + 0.28f *
                       SmoothStep(0.28f, 0.82f, detailNoise);
    float coarseCore = 0.80f + 0.20f *
                       SmoothStep(0.32f, 0.80f, coarseNoise);
    return energyGate * densityGate * detailCore * coarseCore * 0.56f;
}

static float ResolveCoreRadiance(float integratedCore, float gain) {
    return integratedCore * integratedCore * gain;
}

static float FireBloomSample(float transmittance, float densityAlpha,
                             float coreWeight) {
    return transmittance * densityAlpha * coreWeight;
}

static int TestFireBloomSeed(void) {
    const float bloomThreshold = 1.25f;
    float frontSample = FireBloomSample(0.82f, 0.18f, 0.75f);
    float backSample = FireBloomSample(0.12f, 0.18f, 0.75f);
    float coreIntegral = FireBloomSample(0.90f, 0.35f, 0.40f) +
                         FireBloomSample(0.82f, 0.34f, 0.39f) +
                         FireBloomSample(0.72f, 0.32f, 0.38f) +
                         FireBloomSample(0.62f, 0.30f, 0.37f) +
                         FireBloomSample(0.50f, 0.28f, 0.36f) +
                         FireBloomSample(0.40f, 0.24f, 0.34f);
    float edgeIntegral = FireBloomSample(0.90f, 0.10f, 0.04f) * 3.0f;
    float coreSeed = ResolveCoreRadiance(coreIntegral, 8.00f);
    float edgeSeed = ResolveCoreRadiance(edgeIntegral, 8.00f);

    /* A rear hotspot must not paint over the visible front of the ray. The old
     * max-along-ray seed gave these two samples identical influence. */
    CHECK(frontSample > backSample * 6.0f);
    CHECK(coreSeed > bloomThreshold);
    CHECK(edgeSeed < bloomThreshold * 0.10f);
    CHECK(coreSeed > edgeSeed * 15.0f);
    return 0;
}

static int TestHotCoreSelectivity(void) {
    /* Temperature/reaction selects the core; noise only breaks up its edge.
     * Making noise the selector prints random white islands through the plume. */
    float coolFire = FireCoreWeight(0.20f, 0.88f, 0.92f, 0.92f);
    float hotQuietFire = FireCoreWeight(0.92f, 0.88f, 0.60f, 0.20f);
    float coolEnergy = EnergyCoreWeight(0.20f, 0.10f, 0.88f, 0.92f, 0.92f);
    float hotQuietEnergy = EnergyCoreWeight(0.92f, 0.92f, 0.88f, 0.20f, 0.20f);
    float filamentFire = FireCoreWeight(0.92f, 0.88f, 0.92f, 0.92f);
    float filamentEnergy = EnergyCoreWeight(0.92f, 0.92f, 0.88f, 0.92f, 0.92f);
    CHECK(coolFire < 0.05f);
    CHECK(hotQuietFire > 0.14f);
    CHECK(filamentFire < hotQuietFire * 2.0f);
    CHECK(filamentFire <= 0.42f);
    CHECK(coolEnergy < 0.02f);
    CHECK(hotQuietEnergy > 0.18f);
    CHECK(filamentEnergy < hotQuietEnergy * 2.0f);
    CHECK(filamentEnergy <= 0.56f);

    /* The post-loop seed must reject a weak ray and keep a strong ray above the
     * bloom threshold. Linear gain makes both rays bloom and widens the core. */
    CHECK(ResolveCoreRadiance(0.14f, 8.00f) < 0.20f);
    CHECK(ResolveCoreRadiance(0.40f, 8.00f) > 1.25f);
    return 0;
}

static int TestFireBodyOptics(void) {
    TestColor3 darkCarrier = {0.08f, 0.04f, 0.02f};
    TestColor3 orangeEmission = {1.0f, 0.40f, 0.07f};
    TestFireOptics hot = FireOptics(1.0f, 1.0f, darkCarrier, orangeEmission, 0.5f,
                                    0.20f);
    TestFireOptics cool = FireOptics(0.0f, 0.0f, darkCarrier, orangeEmission, 0.5f,
                                     0.20f);

    CHECK(cool.bodyOpacity == 0.0f);
    CHECK(hot.bodyOpacity >= 0.24f && hot.bodyOpacity <= 0.28f);
    CHECK(EnergyBodyOpacity(0.0f, 0.0f) == 0.0f);
    CHECK(EnergyBodyOpacity(1.0f, 1.0f) >= 0.26f);
    CHECK(Luma(hot.bodyTone) * hot.bodyLight >
          Luma(cool.bodyTone) * cool.bodyLight * 1.35f);
    CHECK(hot.bodyTone.r > hot.bodyTone.g * 1.8f);
    CHECK(cool.bodyTone.r > cool.bodyTone.g * 1.7f);
    CHECK(cool.bodyTone.g > cool.bodyTone.b * 1.5f);
    CHECK(hot.bodyLight >= 0.99f);
    CHECK(cool.bodyLight >= 0.50f);

    /* A hot reaction restores full emission transport while inactive FIRE
     * density contributes no smoke-like body coverage. */
    float densityAlpha = 0.75f;
    float hotBodyAlpha = densityAlpha * hot.bodyOpacity;
    float hotEmissionAlpha = FireEmissionAlpha(densityAlpha, hot.bodyOpacity,
                                                1.0f, 1.0f);
    float coolEmissionAlpha = FireEmissionAlpha(densityAlpha, cool.bodyOpacity,
                                                 0.0f, 0.0f);
    CHECK(hotEmissionAlpha > hotBodyAlpha * 2.9f);
    CHECK(coolEmissionAlpha == 0.0f);

    float outerEnergy = hotEmissionAlpha * 0.34f;
    float coreEnergy = hotEmissionAlpha * 1.85f;
    CHECK(coreEnergy > outerEnergy * 5.4f);
    CHECK(coreEnergy > densityAlpha * 1.8f);
    return 0;
}

static int TestWhiteSmokePalette(void) {
    TestColor3 smoke = {214.0f / 255.0f, 218.0f / 255.0f, 224.0f / 255.0f};
    CHECK(Luma(smoke) > 0.84f);
    CHECK(Chroma(smoke) < 0.05f);
    return 0;
}

static int TestFireChannelPersistence(void) {
    /* Emission must remain coupled to a useful share of the density for at
     * least one second, while still decaying faster so fire becomes smoke. */
    float plumeRelative = expf(-(0.95f - 0.35f));
    float jetRelative = expf(-(0.95f - 0.48f));
    CHECK(plumeRelative > 0.50f && plumeRelative < 0.80f);
    CHECK(jetRelative > 0.60f && jetRelative < 0.80f);
    return 0;
}

static int TestBackgroundAdaptiveOptics(void) {
    float dark = BackgroundAdapt(0.02f);
    float bright = BackgroundAdapt(1.0f);
    CHECK(dark == 0.0f);
    CHECK(bright == 1.0f);

    /* Bright scenes separate wispy edges from dense structure instead of
     * raising all coverage through one nonlinear alpha curve. */
    CHECK(AdaptDensity(0.04f, bright) < 0.04f);
    CHECK(AdaptDensity(0.75f, bright) > 0.75f * 1.30f);
    CHECK(AdaptDensity(0.75f, dark) == 0.75f);

    CHECK(AdaptBodyTone(0, bright) <= 0.72f);
    CHECK(AdaptBodyTone(1, bright) <= 0.78f);
    CHECK(AdaptBodyTone(2, bright) <= 0.66f);

    /* Broad radiance stays bounded even on dark scenes and falls farther on a
     * bright plate. The independently integrated core owns the HDR range. */
    CHECK(AdaptEmission(1, 0.0f, dark) <= 0.48f);
    CHECK(AdaptEmission(2, 0.0f, dark) <= 0.62f);
    CHECK(AdaptEmission(1, 0.0f, bright) <= 0.24f);
    CHECK(AdaptEmission(2, 0.0f, bright) <= 0.43f);
    CHECK(AdaptEmission(1, 1.0f, bright) <= 0.50f);
    CHECK(AdaptEmission(2, 1.0f, bright) <= 0.68f);
    return 0;
}

static int TestShaderContract(void) {
    char *shader = ReadFile("core/gas/shaders/gas_volume.fs");
    CHECK(shader != NULL);
    CHECK(strstr(shader, "float Gas_RayJitter(vec2 pixel)") != NULL);
    CHECK(strstr(shader, "travel = nearHit + stepLength * Gas_RayJitter(gl_FragCoord.xy)") != NULL);
    CHECK(strstr(shader, "fbm3(") != NULL);
    CHECK(strstr(shader, "u_qualityTier >= 3") != NULL);
    CHECK(strstr(shader, "coarseNoise = vnoise3(coarseDomain)") != NULL);
    CHECK(strstr(shader, "detailNoise = vnoise3(detailDomain)") != NULL);
    CHECK(strstr(shader, "if (u_qualityTier <= 1)") != NULL);
    CHECK(strstr(shader, "detailNoise = fract(coarseNoise * 1.618") != NULL);
    CHECK(strstr(shader, "float coreWeight") != NULL);
    CHECK(strstr(shader, "float hotGate = smoothstep(0.34, 0.78, hotProduct)") != NULL);
    CHECK(strstr(shader, "float densityGate = smoothstep(0.30, 0.72, rawDensity)") != NULL);
    CHECK(strstr(shader, "float detailCore = mix(0.58, 1.0,") != NULL);
    CHECK(strstr(shader, "float coreWeight = hotGate * densityGate * detailCore * coarseCore * 0.42") != NULL);
    CHECK(strstr(shader, "accumulatedCoreRadiance += transmittance * densityAlpha * coreWeight") != NULL);
    CHECK(strstr(shader, "max(bloomSeed") == NULL);
    CHECK(strstr(shader, "float resolvedCoreRadiance = accumulatedCoreRadiance * accumulatedCoreRadiance") != NULL);
    CHECK(strstr(shader, "bloomColor * resolvedCoreRadiance * 8.00") != NULL);
    CHECK(strstr(shader, "vec3(1.0, 0.88, 0.55)") != NULL);
    CHECK(strstr(shader, "bodyOpacity = 0.26 * fireActivity") != NULL);
    CHECK(strstr(shader, "float energyActivity = smoothstep(0.08, 0.52, heat)") != NULL);
    CHECK(strstr(shader, "bodyOpacity = 0.28 * energyActivity") != NULL);
    CHECK(strstr(shader, "bodyTone = mix(u_emissionColor * 0.28") != NULL);
    CHECK(strstr(shader, "vec3 hotBody = mix(u_bodyColor, u_emissionColor * 0.72, 0.88)") != NULL);
    CHECK(strstr(shader, "float litResidue = max(softLight, 0.55)") != NULL);
    CHECK(strstr(shader, "vec3 smokeBody") == NULL);
    CHECK(strstr(shader, "vec3(0.22, 0.23, 0.25)") == NULL);
    CHECK(strstr(shader, "float flameTransport = smoothstep(0.08, 0.55, heat)") != NULL);
    CHECK(strstr(shader, "smoothstep(0.06, 0.60, gas.b)") != NULL);
    CHECK(strstr(shader, "float emissionAlpha = mix(sampleAlpha, densityAlpha, flameTransport)") != NULL);
    CHECK(strstr(shader, "emittedHeat *= mix(0.34, 1.85, coreWeight)") != NULL);
    CHECK(strstr(shader, "uniform sampler2D u_bgLuma") != NULL);
    CHECK(strstr(shader, "uniform int u_hasBgLuma") != NULL);
    CHECK(strstr(shader, "uniform float u_bgAdapt") != NULL);
    CHECK(strstr(shader, "uniform float u_detailStrength") != NULL);
    CHECK(strstr(shader, "uniform float u_shadowStrength") != NULL);
    CHECK(strstr(shader, "smoothstep(0.22, 0.88, backgroundLuma)") != NULL);
    CHECK(strstr(shader, "mix(0.78, 1.38, smoothstep(0.08, 0.52, density))") != NULL);
    CHECK(strstr(shader, "const vec3 GAS_BRIGHT_BODY_GAIN = vec3(0.72, 0.78, 0.66)") != NULL);
    CHECK(strstr(shader, "float broadEmissionGain") != NULL);
    CHECK(strstr(shader, "float energyCoreWeight") != NULL);
    CHECK(strstr(shader, "float energyDetailCore = mix(0.72, 1.0,") != NULL);
    CHECK(strstr(shader, "float energyCoreWeight = energyGate * energyDensityGate") != NULL);
    CHECK(strstr(shader, "accumulatedCoreRadiance += transmittance * densityAlpha * energyCoreWeight") != NULL);
    CHECK(strstr(shader, "energyBloomColor * resolvedCoreRadiance * 5.00") != NULL);
    CHECK(strstr(shader, "bodyLight = max(softLight, 0.62)") != NULL);
    CHECK(CountSubstring(shader, "texture(u_bgLuma") == 1);
    CHECK(strstr(shader, "if (u_kind == 1)") != NULL);
    free(shader);

    char *host = ReadFile("core/gas/gas_system.c");
    CHECK(host != NULL);
    CHECK(strstr(host, "GetShaderLocation(s_raymarchShader, \"u_qualityTier\")") != NULL);
    CHECK(strstr(host, "int qualityTier = s_profile.effectiveTier") != NULL);
    CHECK(strstr(host, "s_locations.qualityTier, &qualityTier") != NULL);
    CHECK(strstr(host, "SceneTargets_GetBackgroundLuma()") != NULL);
    CHECK(strstr(host, "Tuning_RegisterFloat(\"gas_bg_adapt\"") != NULL);
    CHECK(strstr(host, "GasOpticalControls_Resolve") != NULL);
    CHECK(strstr(host, "desc.reactionDissipation = 0.95f") != NULL);
    CHECK(strstr(host, "desc.bodyColor = (Color){214, 218, 224, 255}") != NULL);
    free(host);

    char *jet = ReadFile("core/composition/common/vc_flame_jet.inl");
    CHECK(jet != NULL);
    CHECK(strstr(jet, "volume.reactionDissipation = 0.95f") != NULL);
    free(jet);
    return 0;
}

int main(void) {
    if (TestRayPhaseDecorrelation() != 0) return 1;
    if (TestFireCoreRamp() != 0) return 1;
    if (TestFireBloomSeed() != 0) return 1;
    if (TestHotCoreSelectivity() != 0) return 1;
    if (TestFireBodyOptics() != 0) return 1;
    if (TestWhiteSmokePalette() != 0) return 1;
    if (TestFireChannelPersistence() != 0) return 1;
    if (TestBackgroundAdaptiveOptics() != 0) return 1;
    if (TestShaderContract() != 0) return 1;
    puts("gas_volume_quality_test: PASS");
    return 0;
}
