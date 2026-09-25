#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int contains(const char *path, const char *expression)
{
    static char source[65536];
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t length = fread(source, 1, sizeof(source) - 1, file);
    assert(!ferror(file) && feof(file));
    source[length] = '\0';
    fclose(file);
    return strstr(source, expression) != NULL;
}

int main(void)
{
    // raylib can fold the view into matModel. A character at world x=50 then
    // arrives in shader space near x=0; comparing it to a world camera at 50
    // makes the fog believe a nearby character is more than 50 m away.
    float cameraX = 50.0f, cameraZ = 41.5f;
    float shaderX = 0.0f, shaderZ = -4.0f;
    float recoveredX = shaderX + 50.0f;
    float recoveredZ = shaderZ + 41.5f;
    float wrongDistance = hypotf(cameraX - shaderX, cameraZ - shaderZ);
    float correctDistance = hypotf(cameraX - recoveredX, cameraZ - recoveredZ);
    assert(wrongDistance > 60.0f && fabsf(correctDistance - 4.0f) < 0.0001f);

    assert(contains("core/shaders/surface_lit.vs",
                    "fragWorldPos = vec3(u_worldFromShaderSpace * vec4(shaderPosition, 1.0));"));
    assert(contains("core/surface_material.c",
                    "Matrix worldFromShaderSpace = MatrixInvert(rlGetMatrixTransform());"));
    assert(contains("core/surface_material.c",
                    "SetShaderValueMatrix(s_shader, s_locWorldFromShaderSpace, worldFromShaderSpace);"));
    puts("surface world space: model, camera, fog and shadows share coordinates");
    return 0;
}
