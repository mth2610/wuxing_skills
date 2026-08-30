#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

static int AtlasRowForGridY(int tileY, int height, int gridY) {
    return tileY * height + gridY;
}

int main(void) {
    int height = 28;
    int bottomRow = AtlasRowForGridY(0, height, 0);
    int topRow = AtlasRowForGridY(0, height, height - 1);
    CHECK(bottomRow == 0, "grid bottom must map to atlas v=0 row");
    CHECK(topRow == height - 1, "grid top must map to the highest atlas v row");
    CHECK(topRow > bottomRow, "positive buoyancy must increase sampled atlas v");

    FILE *file = fopen("core/gas/gas_system.c", "rb");
    CHECK(file != NULL, "gas renderer source must exist");
    static char source[65536];
    size_t count = fread(source, 1, sizeof(source) - 1u, file);
    fclose(file);
    source[count] = '\0';
    CHECK(strstr(source, "tileY * s_sim.height + y") != NULL,
          "atlas upload must preserve simulation Y for shader v sampling");
    CHECK(strstr(source, "s_sim.height - 1 - y") == NULL,
          "atlas upload must not add a second vertical inversion");

    puts("gas_atlas_orientation_test: PASS");
    return 0;
}
