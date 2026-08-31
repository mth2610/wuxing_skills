#include <stdio.h>
#include <string.h>

#include "core/gas/gas_profile_internal.inl"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "gas_quality_profile_test: check failed at line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static unsigned long long TapBound(GasQualityProfile profile) {
    return (unsigned long long)profile.targetWidth *
           (unsigned long long)profile.targetHeight *
           (unsigned long long)profile.raymarchSteps * 4ull;
}

int main(void) {
    GasQualityProfile low = GasQualityProfile_Make(1, 1280, 720);
    GasQualityProfile med = GasQualityProfile_Make(2, 1280, 720);
    GasQualityProfile high = GasQualityProfile_Make(3, 1280, 720);
    GasQualityProfile unlit = GasQualityProfile_Make(0, 1280, 720);

    CHECK(low.gridWidth == 16 && low.gridHeight == 24 && low.gridDepth == 16);
    CHECK(med.gridWidth == 20 && med.gridHeight == 28 && med.gridDepth == 20);
    CHECK(high.gridWidth == 28 && high.gridHeight == 32 && high.gridDepth == 28);
    CHECK(unlit.effectiveTier == low.effectiveTier);
    CHECK(low.raymarchSteps == 16 && med.raymarchSteps == 24 &&
          high.raymarchSteps == 40);
    CHECK(low.targetWidth == 320 && med.targetWidth == 320 &&
          high.targetWidth == 426);
    CHECK(low.atlasWidth == 128 && low.atlasHeight == 48);
    CHECK(med.atlasWidth == 160 && med.atlasHeight == 84);
    CHECK(high.atlasWidth == 224 && high.atlasHeight == 128);
    CHECK(low.atlasBytes < med.atlasBytes && med.atlasBytes < high.atlasBytes);
    CHECK(low.noiseSamplesPerStep == 1 && med.noiseSamplesPerStep == 2 &&
          high.noiseSamplesPerStep == 2);
    CHECK(!GasQualityProfile_NeedsRebuild(low, 0));
    CHECK(!GasQualityProfile_NeedsRebuild(low, 1));
    CHECK(GasQualityProfile_NeedsRebuild(low, 2));
    CHECK(GasQualityProfile_NeedsRebuild(high, 2));
    CHECK(TapBound(low) < TapBound(med) && TapBound(med) < TapBound(high));

    FILE *file = fopen("core/gas/gas_system.c", "rb");
    CHECK(file != NULL);
    static char source[65536];
    size_t count = fread(source, 1, sizeof(source) - 1u, file);
    fclose(file);
    source[count] = '\0';
    CHECK(strstr(source, "GasSystem_ReconfigureIfIdle()") != NULL);
    CHECK(strstr(source, "deferred until the active volume retires") != NULL);
    puts("gas_quality_profile_test: PASS");
    return 0;
}
