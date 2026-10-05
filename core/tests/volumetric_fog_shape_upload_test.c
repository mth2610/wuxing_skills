#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "environment/environment_system.h"
#include "core/volumetric/volumetric_fog_volume.inl"

static int failures;
#define CHECK(c, label) do { if (!(c)) { puts("FAIL: " label); ++failures; } } while (0)

static int ReadSource(const char *path, char *buffer, size_t capacity) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    size_t length = fread(buffer, 1, capacity-1, file);
    buffer[length] = 0;
    fclose(file);
    return 1;
}

int main(void) {
    const FogVolumeShape shapes[] = {FOG_SHAPE_SPHERE, FOG_SHAPE_BOX, FOG_SHAPE_CYLINDER,
                                    (FogVolumeShape)99, (FogVolumeShape)-1};
    const float shaderIds[] = {1, 0, 2, 0, 0};
    for (int i = 0; i < 5; ++i) {
        LocalFogVolume volume = {.position = {3, -2, 8}, .shape = shapes[i]};
        Vector4 packed = VolumetricFog_PackPositionShape(&volume);
        CHECK(packed.x == 3 && packed.y == -2 && packed.z == 8, "upload packet preserves world position");
        CHECK(packed.w == shaderIds[i], "sphere/box/cylinder/fallback use shader wire ids");
    }
    /* Execute the same private packet constructor used in rendering. Source
       guards connect both upload branches and the actual shader upload; this
       headless test does not validate GPU evaluation or volume appearance. */
    char source[24000] = {0};
    CHECK(ReadSource("core/volumetric/volumetric_fog.c", source, sizeof(source)), "fog upload source readable");
    const char *first = strstr(source, "volPosShape[volCount] = VolumetricFog_PackPositionShape(v)");
    const char *second = first ? strstr(first+1, "volPosShape[volCount] = VolumetricFog_PackPositionShape(v)") : NULL;
    CHECK(first && second, "both transient and permanent uploads use the production packet constructor");
    CHECK(strstr(source, "SetShaderValueV(s_raymarchShader, s_locVolPosShape, volPosShape, SHADER_UNIFORM_VEC4, 4)"),
          "encoded packets reach the vec4 shape uniform");
    CHECK(ReadSource("core/volumetric/shaders/volumetric_fog.fs", source, sizeof(source)), "fog shader readable");
    CHECK(strstr(source, "if (shape == 2)") && strstr(source, "else if (shape == 1)") &&
          strstr(source, "0=box, 1=sphere, 2=cylinder"), "shader shape dispatch agrees with encoded ids");
    if (failures) return 1;
    puts("PASS: production fog shape packets match shader ids for all shapes and fallback");
    return 0;
}
