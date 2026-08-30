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

static int TestShaderContract(void) {
    char *shader = ReadFile("core/gas/shaders/gas_volume.fs");
    CHECK(shader != NULL);
    CHECK(strstr(shader, "float Gas_RayJitter(vec2 pixel)") != NULL);
    CHECK(strstr(shader, "travel = nearHit + stepLength * Gas_RayJitter(gl_FragCoord.xy)") != NULL);
    CHECK(strstr(shader, "fbm3(") != NULL);
    CHECK(strstr(shader, "float coreWeight") != NULL);
    CHECK(strstr(shader, "vec3(1.0, 0.92, 0.72)") != NULL);
    CHECK(strstr(shader, "if (u_kind == 1)") != NULL);
    free(shader);
    return 0;
}

int main(void) {
    if (TestRayPhaseDecorrelation() != 0) return 1;
    if (TestFireCoreRamp() != 0) return 1;
    if (TestShaderContract() != 0) return 1;
    puts("gas_volume_quality_test: PASS");
    return 0;
}
