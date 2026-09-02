// The global particle fallback must be a committed neutral image, not a
// runtime-generated surface whose profile can silently differ by launch path.
#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char text[300000];
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    FILE *asset = fopen("assets/textures/particle_default.png", "rb");
    int hasAsset = asset != NULL;
    if (asset) fclose(asset);
    int hasLoad = Has("main.c", "LoadTexture(\"assets/textures/particle_default.png\")");
    printf("%s: global particle PNG exists\n", hasAsset ? "PASS" : "FAIL");
    printf("%s: main loads the committed particle PNG\n", hasLoad ? "PASS" : "FAIL");
    return hasAsset && hasLoad ? 0 : 1;
}
