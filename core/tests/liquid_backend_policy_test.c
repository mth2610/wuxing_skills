/* Wiring guard: PBD must be explicitly selected and never prewarmed by Update.
 * This checks dispatch ownership, not GPU execution or visual quality. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char source[65536];
    FILE *f = fopen("core/liquid/liquid_impact.c", "rb");
    if (!f) return 1;
    size_t size = fread(source, 1, sizeof(source) - 1, f);
    fclose(f);
    source[size] = 0;
    int bad = 0;
    char *spawn = strstr(source, "void LiquidImpact_SpawnWater(");
    char *update = strstr(source, "void LiquidImpact_Update(");
    char *draw = strstr(source, "void LiquidImpact_Draw(");
    char *init = spawn ? strstr(spawn, "LiquidPBDGPU_Init()") : NULL;
    char *external = spawn ? strstr(spawn, "if (event->externalBody) return;") : NULL;
    char *optIn = spawn ? strstr(spawn, "event->backend == LIQUID_IMPACT_BACKEND_PBD") : NULL;
    if (!spawn || !update || !draw || !init || !external || !optIn ||
        external > init || optIn > init || init > update) {
        puts("FAIL: external bodies must return before explicit PBD opt-in/init");
        bad++;
    }
    char *prewarm = update ? strstr(update, "LiquidPBDGPU_Init()") : NULL;
    if (!update || !draw || (prewarm && prewarm < draw)) {
        puts("FAIL: ordinary Update must not initialize PBD");
        bad++;
    }
    printf("fluid backend policy: %s\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}
