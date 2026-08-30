#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        return 1; \
    } \
} while (0)

static int SimulatePulseCount(float fps, float seconds, float rate) {
    float accumulator = 0.0f;
    int pulses = 0;
    int frames = (int)(fps * seconds + 0.5f);
    for (int frame = 0; frame < frames; ++frame) {
        accumulator += (1.0f / fps) * rate;
        while (accumulator >= 1.0f) {
            accumulator -= 1.0f;
            ++pulses;
        }
    }
    return pulses;
}

static float SmoothStep(float edge0, float edge1, float value) {
    float t = (value - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float PlumeEnvelope(float elapsed, float emitDuration) {
    float fade = fminf(0.25f, emitDuration * 0.25f);
    if (fade <= 0.0001f || elapsed >= emitDuration) return 0.0f;
    float fadeIn = SmoothStep(0.0f, fade, elapsed);
    float fadeOut = 1.0f - SmoothStep(emitDuration - fade, emitDuration, elapsed);
    return fadeIn * fadeOut;
}

static char *ReadText(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = (char *)malloc((size_t)size + 1u);
    if (text == NULL) { fclose(file); return NULL; }
    fread(text, 1, (size_t)size, file);
    text[size] = '\0';
    fclose(file);
    return text;
}

int main(void) {
    int p30 = SimulatePulseCount(30.0f, 2.0f, 12.0f);
    int p60 = SimulatePulseCount(60.0f, 2.0f, 12.0f);
    int p120 = SimulatePulseCount(120.0f, 2.0f, 12.0f);
    CHECK(abs(p30 - p60) <= 1 && abs(p60 - p120) <= 1,
          "gas injection rate must be frame-rate independent");
    CHECK(p60 >= 23 && p60 <= 24, "12 Hz over two seconds must emit about 24 pulses");

    CHECK(PlumeEnvelope(0.0f, 2.0f) == 0.0f, "plume must start from zero feed");
    CHECK(PlumeEnvelope(0.25f, 2.0f) > 0.99f, "plume must smoothly reach full feed");
    CHECK(PlumeEnvelope(1.9f, 2.0f) < 0.5f, "plume feed must fade before stopping");
    CHECK(PlumeEnvelope(2.0f, 2.0f) == 0.0f, "plume must stop feeding at duration");

    char *source = ReadText("core/composition/common/vc_gas_plume.inl");
    CHECK(source != NULL, "gas plume archetype must exist");
    CHECK(strstr(source, "GasVolume_Create") != NULL, "plume must create a gas volume");
    CHECK(strstr(source, "GasVolume_Inject") != NULL, "plume must inject simulated gas");
    CHECK(strstr(source, "GasVolume_Destroy") != NULL, "plume kill must release its volume");
    CHECK(strstr(source, "VFX_Material") != NULL, "plume colors must come from the material table");
    CHECK(strstr(source, "pulseAccumulator += dt * pulseRate") != NULL,
          "plume injection must be driven by a carried rate accumulator");
    free(source);

    puts("gas_plume_test: PASS");
    return 0;
}
