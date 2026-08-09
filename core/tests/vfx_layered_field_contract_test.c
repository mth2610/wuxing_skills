/* Contract for coherent effect-level fields: explicit masks, no particle core. */
#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    char buffer[1024];
    size_t used = 0, count;
    if (file == NULL) return 0;
    while ((count = fread(buffer + used, 1, sizeof(buffer) - 1 - used, file)) != 0) {
        used += count;
        buffer[used] = '\0';
        if (strstr(buffer, needle) != NULL) { fclose(file); return 1; }
        if (used > 256) { memmove(buffer, buffer + used - 256, 256); used = 256; }
    }
    fclose(file);
    return 0;
}

int main(void)
{
    int bad = 0;
    bad += !Has("core/vfx_layered_field.h", "VFXLayeredAnnulus_DrawBody");
    bad += !Has("core/vfx_layered_field.h", "VFXLayeredAnnulus_DrawEmission");
    bad += !Has("core/shaders/vfx_layered_annulus.fs", "float lobes = 0.0;");
    bad += !Has("core/shaders/vfx_layered_annulus.fs", "float accent = mass * lobes * innerRidge * filaments;");
    bad += !Has("core/shaders/vfx_layered_annulus.fs", "u_debugLayer");
    bad += Has("core/shaders/vfx_layered_annulus.fs", "texture(u_vfxBodyTex");
    bad += !Has("core/shaders/distortion.fs", "bodySupport");
    bad += !Has("CMakeLists.txt", "core/vfx_layered_field.c");
    bad += !Has("CMakeLists.txt", "core/shaders/vfx_layered_annulus.fs");
    puts(bad ? "vfx layered field: FAIL" : "vfx layered field: PASS");
    return bad;
}
