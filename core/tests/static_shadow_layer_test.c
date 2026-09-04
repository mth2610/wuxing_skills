// Headless contract guard for the two-layer directional shadow path.
// This cannot rasterize either map; it verifies the load-bearing GLSL/C
// interface that otherwise fails silently when one sampler or matrix is missed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *Slurp(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1u);
    if (!text) { fclose(file); return NULL; }
    size_t read = fread(text, 1u, (size_t)size, file);
    text[read] = '\0';
    fclose(file);
    return text;
}

static int Has(const char *text, const char *needle)
{
    return text != NULL && strstr(text, needle) != NULL;
}

int main(void)
{
    char *shader = Slurp("core/shaders/surface_lit.fs");
    char *wiring = Slurp("core/surface_material.c");
    int failures = 0;
#define CHECK(expr, label) do { \
    if (expr) printf("PASS: %s\n", label); \
    else { printf("FAIL: %s\n", label); failures++; } \
} while (0)

    CHECK(shader != NULL && wiring != NULL, "sources load");
    CHECK(Has(shader, "uniform sampler2D staticShadowMap"),
          "surface shader declares the explicitly-bound static sampler");
    CHECK(Has(shader, "uniform mat4      u_staticLightVP"),
          "surface shader declares a separate static projection");
    CHECK(Has(shader, "min(dynamicShadow, staticShadow)"),
          "static and dynamic visibility compose by nearest occlusion");
    CHECK(Has(wiring, "GetShaderLocation(s_shader, \"staticShadowMap\")"),
          "C resolves the static sampler location");
    CHECK(Has(wiring, "SetShaderValueTexture(s_shader, s_locStaticShadowMap"),
          "C explicitly binds the static sampler");
    CHECK(Has(wiring, "EnvShadow_GetStaticLightVP()"),
          "C uploads the static projection");

    free(shader);
    free(wiring);
    printf("---\nstatic shadow contract: %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
