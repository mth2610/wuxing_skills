/* FlameVolume's default body must retain contrast over a bright destination. */
#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb"); char b[1024]; size_t used = 0, n;
    if (!f) return 0;
    while ((n = fread(b + used, 1, sizeof(b) - 1 - used, f)) != 0) {
        used += n; b[used] = '\0';
        if (strstr(b, needle)) { fclose(f); return 1; }
        if (used > 256) { memmove(b, b + used - 256, 256); used = 256; }
    }
    fclose(f); return 0;
}

int main(void)
{
    const char *p = "core/composition/fire/flame_volume.inl";
    int bad = !Has(p, "static float s_fvolBodyBlend = 0.0f;") ||
              !Has(p, "Tuning_RegisterFloat(\"flame_body_blend\", &s_fvolBodyBlend, 0.0f);") ||
              !Has(p, ".colorStart = VC_WithAlpha(WHITE, (unsigned char)(255.0f * s_fvolBodyAlpha)),");
    puts(bad ? "flame background contract: FAIL" : "flame background contract: PASS");
    return bad;
}
