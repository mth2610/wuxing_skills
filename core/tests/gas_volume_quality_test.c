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
static TestFireOptics FireOptics(float heat, TestColor3 body,
                                 TestColor3 emission, float coarseNoise,
                                 float softLight) {
    float coolSmoke = 1.0f - SmoothStep(0.08f, 0.62f, heat);
    float bodyOpacity = 0.32f + (0.52f - 0.32f) * coolSmoke;
    TestColor3 smokeBody = Mix(body, (TestColor3){0.22f, 0.23f, 0.25f}, 0.92f);
    TestColor3 hotBody = Mix(body, Scale(emission, 0.55f), 0.82f);
    TestColor3 bodyTone = Mix(smokeBody, hotBody, 1.0f - coolSmoke);
    bodyTone = Scale(bodyTone, 0.88f + (1.16f - 0.88f) * coarseNoise);
    float litSmoke = fmaxf(softLight, 0.50f);
    float bodyLight = litSmoke + (1.0f - litSmoke) * (1.0f - coolSmoke);
    return (TestFireOptics){bodyOpacity, bodyTone, bodyLight};
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
    TestColor3 shoulder = Mix(outer, (TestColor3){1.0f, 0.58f, 0.10f}, 0.72f);
    TestColor3 core = Mix(shoulder, (TestColor3){1.0f, 0.92f, 0.72f}, 0.88f);
    CHECK(Luma(shoulder) > Luma(outer) * 1.45f);
    CHECK(Luma(core) > Luma(shoulder) * 1.35f);
    CHECK(Chroma(core) < Chroma(shoulder) * 0.55f);
    CHECK(core.r > core.g && core.g > core.b);
    return 0;
}

static float FireCoreWeight(float hotProduct, float rawDensity,
                            float detailNoise) {
    float hotGate = SmoothStep(0.20f, 0.72f, hotProduct);
    float densityGate = SmoothStep(0.30f, 0.75f, rawDensity);
    float detail = 0.35f + 0.65f * SmoothStep(0.40f, 0.75f, detailNoise);
    return fminf(hotGate, densityGate) * detail;
}

static float FireBloomSample(float transmittance, float densityAlpha,
                             float coreWeight) {
    return transmittance * densityAlpha * coreWeight;
}

static int TestFireBloomSeed(void) {
    const float bloomThreshold = 1.25f;
    float frontSample = FireBloomSample(0.82f, 0.18f, 0.75f);
    float backSample = FireBloomSample(0.12f, 0.18f, 0.75f);
    float coreSeed = (frontSample +
                      FireBloomSample(0.65f, 0.17f, 0.70f) +
                      FireBloomSample(0.45f, 0.16f, 0.65f)) * 7.20f;
    float edgeSeed = (FireBloomSample(0.90f, 0.10f, 0.04f) * 3.0f) * 7.20f;

    /* A rear hotspot must not paint over the visible front of the ray. The old
     * max-along-ray seed gave these two samples identical influence. */
    CHECK(frontSample > backSample * 6.0f);
    CHECK(coreSeed > bloomThreshold);
    CHECK(edgeSeed < bloomThreshold * 0.10f);
    CHECK(coreSeed > edgeSeed * 15.0f);
    return 0;
}

static int TestFireBodyOptics(void) {
    TestColor3 darkCarrier = {0.08f, 0.04f, 0.02f};
    TestColor3 orangeEmission = {1.0f, 0.40f, 0.07f};
    TestFireOptics hot = FireOptics(1.0f, darkCarrier, orangeEmission, 0.5f,
                                    0.20f);
    TestFireOptics cool = FireOptics(0.0f, darkCarrier, orangeEmission, 0.5f,
                                     0.20f);

    CHECK(hot.bodyOpacity < cool.bodyOpacity * 0.65f);
    CHECK(cool.bodyOpacity <= 0.52f);
    CHECK(Luma(hot.bodyTone) * hot.bodyLight >
          Luma(cool.bodyTone) * cool.bodyLight * 1.35f);
    CHECK(hot.bodyTone.r > hot.bodyTone.g * 1.8f);
    CHECK(hot.bodyLight >= 0.99f);
    CHECK(cool.bodyLight >= 0.50f);

    /* A hot reaction restores full emission transport while cold density keeps
     * only its smoke coverage. This is the separation the old coupling lost. */
    float densityAlpha = 0.75f;
    float hotBodyAlpha = densityAlpha * hot.bodyOpacity;
    float hotEmissionAlpha = FireEmissionAlpha(densityAlpha, hot.bodyOpacity,
                                                1.0f, 1.0f);
    float coolEmissionAlpha = FireEmissionAlpha(densityAlpha, cool.bodyOpacity,
                                                 0.0f, 0.0f);
    CHECK(hotEmissionAlpha > hotBodyAlpha * 2.9f);
    CHECK(fabsf(coolEmissionAlpha - densityAlpha * cool.bodyOpacity) < 1.0e-6f);

    float outerEnergy = hotEmissionAlpha * 0.34f;
    float coreEnergy = hotEmissionAlpha * 1.85f;
    CHECK(coreEnergy > outerEnergy * 5.4f);
    CHECK(coreEnergy > densityAlpha * 1.8f);
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

static int TestShaderContract(void) {
    char *shader = ReadFile("core/gas/shaders/gas_volume.fs");
    CHECK(shader != NULL);
    CHECK(strstr(shader, "float Gas_RayJitter(vec2 pixel)") != NULL);
    CHECK(strstr(shader, "travel = nearHit + stepLength * Gas_RayJitter(gl_FragCoord.xy)") != NULL);
    CHECK(strstr(shader, "fbm3(") != NULL);
    CHECK(strstr(shader, "u_qualityTier >= 3") != NULL);
    CHECK(strstr(shader, ": vnoise3(coarseDomain)") != NULL);
    CHECK(strstr(shader, ": vnoise3(detailDomain)") != NULL);
    CHECK(strstr(shader, "float coreWeight") != NULL);
    CHECK(strstr(shader, "float hotGate = smoothstep(0.20, 0.72, hotProduct)") != NULL);
    CHECK(strstr(shader, "float densityGate = smoothstep(0.30, 0.75, rawDensity)") != NULL);
    CHECK(strstr(shader, "float coreWeight = min(hotGate, densityGate)") != NULL);
    CHECK(strstr(shader, "accumulatedCoreRadiance += transmittance * densityAlpha * coreWeight") != NULL);
    CHECK(strstr(shader, "max(bloomSeed") == NULL);
    CHECK(strstr(shader, "accumulatedEmission += bloomColor * accumulatedCoreRadiance * 7.20") != NULL);
    CHECK(strstr(shader, "vec3(1.0, 0.92, 0.72)") != NULL);
    CHECK(strstr(shader, "float bodyOpacity = mix(0.32, 0.52, coolSmoke)") != NULL);
    CHECK(strstr(shader, "vec3 hotBody = mix(u_bodyColor, u_emissionColor * 0.55, 0.82)") != NULL);
    CHECK(strstr(shader, "float litSmoke = max(softLight, 0.50)") != NULL);
    CHECK(strstr(shader, "float flameTransport = smoothstep(0.08, 0.55, heat)") != NULL);
    CHECK(strstr(shader, "smoothstep(0.06, 0.60, gas.b)") != NULL);
    CHECK(strstr(shader, "float emissionAlpha = mix(sampleAlpha, densityAlpha, flameTransport)") != NULL);
    CHECK(strstr(shader, "emittedHeat *= mix(0.34, 1.85, coreWeight)") != NULL);
    CHECK(strstr(shader, "if (u_kind == 1)") != NULL);
    free(shader);

    char *host = ReadFile("core/gas/gas_system.c");
    CHECK(host != NULL);
    CHECK(strstr(host, "GetShaderLocation(s_raymarchShader, \"u_qualityTier\")") != NULL);
    CHECK(strstr(host, "int qualityTier = (int)GfxQuality_Get()") != NULL);
    CHECK(strstr(host, "s_locations.qualityTier, &qualityTier") != NULL);
    CHECK(strstr(host, "desc.reactionDissipation = 0.95f") != NULL);
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
    if (TestFireBodyOptics() != 0) return 1;
    if (TestFireChannelPersistence() != 0) return 1;
    if (TestShaderContract() != 0) return 1;
    puts("gas_volume_quality_test: PASS");
    return 0;
}
