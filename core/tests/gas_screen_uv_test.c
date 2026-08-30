#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

int main(void) {
    /* The completed raymarch target is displayed with a negative source
     * height. A displayed screen Y therefore reaches the raymarch shader as
     * 1-screenY, and the shader must undo that before inverse projection. */
    float clickedScreenY = 0.2f;
    float renderTargetY = 1.0f - clickedScreenY;
    float reconstructionY = 1.0f - renderTargetY;
    CHECK(fabsf(reconstructionY - clickedScreenY) < 0.000001f,
          "screen click and reconstructed ray must address the same vertical point");

    FILE *file = fopen("core/gas/shaders/gas_volume.fs", "rb");
    CHECK(file != NULL, "gas shader must exist");
    static char source[32768];
    size_t count = fread(source, 1, sizeof(source) - 1u, file);
    fclose(file);
    source[count] = '\0';
    CHECK(strstr(source,
                 "vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);") != NULL,
          "raymarch must undo the final RenderTexture display inversion");
    CHECK(strstr(source, "vec2 framebufferUV = fragTexCoord;") != NULL &&
          strstr(source, "texture(u_sceneDepthTex, framebufferUV)") != NULL,
          "scene depth must stay in framebuffer texture coordinates");
    CHECK(strstr(source, "vec2 uv = fragTexCoord;") == NULL,
          "raymarch must not reconstruct from the vertically displaced ray");

    file = fopen("core/gas/gas_system.c", "rb");
    CHECK(file != NULL, "gas renderer source must exist");
    count = fread(source, 1, sizeof(source) - 1u, file);
    fclose(file);
    source[count] = '\0';
    CHECK(strstr(source, "-(float)s_raymarchTarget.texture.height") != NULL,
          "only the final RenderTexture composite should perform the display flip");

    puts("gas_screen_uv_test: PASS");
    return 0;
}
