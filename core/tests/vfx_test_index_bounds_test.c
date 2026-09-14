#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static char *ReadFile(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *text;

    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    text = malloc((size_t)size + 1u);
    if (text == NULL) { fclose(file); return NULL; }
    if (fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return NULL;
    }
    text[size] = '\0';
    fclose(file);
    return text;
}

static void Check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static int CountOccurrences(const char *text, const char *needle)
{
    int count = 0;
    size_t length = strlen(needle);
    const char *cursor = text;

    while ((cursor = strstr(cursor, needle)) != NULL) {
        count++;
        cursor += length;
    }
    return count;
}

int main(void)
{
    char *fixture = ReadFile("sandbox/vfx_test.c");

    Check(fixture != NULL, "sandbox fixture source must be readable");
    if (fixture != NULL) {
        Check(strstr(fixture, "VFXTest_IsNewFxNamed") != NULL,
              "fixture name comparisons must use one bounds-checked helper");
        Check(strstr(fixture, "s_testIndex < VFXTest_NewFxCount()") != NULL,
              "fixture helper must reject render-only indices above the catalog");
        Check(CountOccurrences(fixture, "strcmp(s_newFxNames[s_testIndex]") == 1,
              "only the bounds-checked helper may compare the selected fixture name");
        Check(strstr(fixture, "VFXTest_SetRenderTarget(-1, spawnPos)") != NULL,
              "neutral smoke must retain its negative no-fixture sentinel");
    }

    free(fixture);
    if (failures == 0) {
        puts("vfx test index bounds tests passed");
        return 0;
    }
    fprintf(stderr, "%d vfx test index bounds test(s) failed\n", failures);
    return 1;
}
