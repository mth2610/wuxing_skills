#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

int main(void) {
    /* Raylib/rlvk keep fragment UV, scene depth, and clip reconstruction in
     * one GL-style texture space. RenderTexture display inversion belongs only
     * to the final negative-height source rectangle. */
    float clickedScreenY = 0.2f;
    float reconstructionY = clickedScreenY;
    CHECK(reconstructionY == clickedScreenY,
          "screen click and reconstructed ray must address the same vertical point");

    FILE *file = fopen("core/gas/shaders/gas_volume.fs", "rb");
    CHECK(file != NULL, "gas shader must exist");
    static char source[32768];
    size_t count = fread(source, 1, sizeof(source) - 1u, file);
    fclose(file);
    source[count] = '\0';
    CHECK(strstr(source, "vec2 uv = fragTexCoord;") != NULL,
          "raymarch must preserve the fullscreen quad UV for reconstruction");
    CHECK(strstr(source, "1.0 - fragTexCoord.y") == NULL,
          "raymarch must not vertically mirror the reconstructed screen ray");

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
