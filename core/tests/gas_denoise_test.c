/* Headless contract for the low-resolution gas reconstruction filter.
 *
 * The numeric mirror proves the 3x3 tent removes a one-pixel checker while
 * preserving constants and total premultiplied energy. It cannot render the
 * Vulkan target, so the source checks pin the four bilinear taps and host pass.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "gas_denoise_test: check failed at line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static float TentAt(const float image[7][7], int x, int y) {
    static const float weights[3][3] = {
        {1.0f, 2.0f, 1.0f},
        {2.0f, 4.0f, 2.0f},
        {1.0f, 2.0f, 1.0f}
    };
    float sum = 0.0f;
    for (int oy = -1; oy <= 1; ++oy)
        for (int ox = -1; ox <= 1; ++ox)
            sum += image[y + oy][x + ox] * weights[oy + 1][ox + 1];
    return sum / 16.0f;
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

static int Count(const char *text, const char *needle) {
    int count = 0;
    size_t length = strlen(needle);
    while ((text = strstr(text, needle)) != NULL) {
        ++count;
        text += length;
    }
    return count;
}

static int TestTentResponse(void) {
    float constant[7][7];
    float checker[7][7];
    float impulse[7][7] = {{0}};
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            constant[y][x] = 0.37f;
            checker[y][x] = (float)((x + y) & 1);
        }
    }
    impulse[3][3] = 1.0f;

    CHECK(fabsf(TentAt(constant, 3, 3) - 0.37f) < 1.0e-6f);
    CHECK(fabsf(TentAt(checker, 3, 3) -
                TentAt(checker, 4, 3)) < 1.0e-6f);

    float energy = 0.0f;
    for (int y = 1; y < 6; ++y)
        for (int x = 1; x < 6; ++x)
            energy += TentAt(impulse, x, y);
    CHECK(fabsf(energy - 1.0f) < 1.0e-6f);
    return 0;
}

static int TestSourceContract(void) {
    char *shader = ReadFile("core/gas/shaders/gas_denoise.fs");
    CHECK(shader != NULL);
    CHECK(Count(shader, "texture(texture0") == 4);
    CHECK(strstr(shader, "vec2(-0.5, -0.5)") != NULL);
    CHECK(strstr(shader, "vec2( 0.5,  0.5)") != NULL);
    CHECK(strstr(shader, "sum * 0.25") != NULL);
    free(shader);

    char *host = ReadFile("core/gas/gas_system.c");
    CHECK(host != NULL);
    CHECK(strstr(host, "s_denoiseTarget") != NULL);
    CHECK(strstr(host, "core/gas/shaders/gas_denoise.fs") != NULL);
    CHECK(strstr(host, "GasSystem_DenoiseRaymarch()") != NULL);
    CHECK(strstr(host, "s_denoiseTarget.texture") != NULL);
    free(host);
    return 0;
}

int main(void) {
    if (TestTentResponse() != 0) return 1;
    if (TestSourceContract() != 0) return 1;
    puts("gas_denoise_test: PASS");
    return 0;
}
