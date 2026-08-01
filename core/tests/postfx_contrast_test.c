/* Headless guard for the global HDR composite's contrast budget. */
#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    char buf[1024];
    size_t used = 0, n;
    if (!f) return 0;
    while ((n = fread(buf + used, 1, sizeof(buf) - 1 - used, f)) != 0)
    {
        used += n;
        buf[used] = '\0';
        if (strstr(buf, needle) != NULL) { fclose(f); return 1; }
        if (used > 256) { memmove(buf, buf + used - 256, 256); used = 256; }
    }
    fclose(f);
    return 0;
}

int main(void)
{
    int bad = 0;
    bad += !Has("main.c", ".bloomThreshold = 1.25f");
    bad += !Has("main.c", ".bloomIntensity = 0.12f");
    bad += !Has("main.c", ".exposure = 1.00f");
    bad += !Has("core/shaders/bloom_bright.fs", "vec3 brightColor = col.rgb * weight * 2.2");
    printf("postfx contrast budget: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
