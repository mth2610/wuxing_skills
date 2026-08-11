// core headless test — the PBD neighbour grid's generation stamp.
//
// Clearing the grid was 8.9x the workgroups of the actual particle solve: 32,768
// cells wiped five times a frame (once before every Jacobi rebuild) for a
// population that can occupy at most 6.2% of them. Stamping the head makes a
// stale cell identify itself, so the clear happens once per impact instead.
//
// That trade is only safe if the encoding is exact, and the encoding is pure
// integer arithmetic — so it is checkable here rather than by watching a splash.
// Mirrors encodeHead/decodeHead in core/fluid/shaders/fluid_pbd_gpu.comp.
//
// What it cannot check: that the dispatch ORDER bumps the stamp at the right
// moments (the .c side), or that the solve still looks right. The sandbox
// fixture for that is NEW FX tab, FLUID IMPACT.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

#define PBD_HEAD_STRIDE 4096
#define PBD_MAX_PARTICLES 2048
#define PBD_STAMP_WRAP 500000

static int EncodeHead(int stamp, int particleId) { return stamp * PBD_HEAD_STRIDE + particleId + 1; }

static int DecodeHead(int stamp, int stored)
{
    return (stored / PBD_HEAD_STRIDE == stamp) ? (stored % PBD_HEAD_STRIDE - 1) : -1;
}

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *t = (char *)malloc((size_t)n + 1);
    if (!t) { fclose(f); return NULL; }
    size_t got = fread(t, 1, (size_t)n, f); t[got] = '\0'; fclose(f); return t;
}

int main(void)
{
    int bad = 0;

    /* A head written under the current stamp round-trips, for every id the pool
     * can hold — including 0, which is why the encoding carries a +1. */
    for (int stamp = 1; stamp <= 5; ++stamp)
    {
        CHECK(DecodeHead(stamp, EncodeHead(stamp, 0)) == 0);
        CHECK(DecodeHead(stamp, EncodeHead(stamp, PBD_MAX_PARTICLES - 1)) == PBD_MAX_PARTICLES - 1);
        CHECK(DecodeHead(stamp, EncodeHead(stamp, 977)) == 977);
    }

    /* A head from ANY older generation is an empty cell. This is the whole
     * point: it is what replaces wiping the buffer. */
    for (int stamp = 2; stamp <= 40; ++stamp)
        for (int older = 1; older < stamp; ++older)
            CHECK(DecodeHead(stamp, EncodeHead(older, older % PBD_MAX_PARTICLES)) == -1);

    /* The cleared value must read empty under every stamp the run can reach —
     * phase 0 writes 0, and stamps start at 1. */
    for (int stamp = 1; stamp <= PBD_STAMP_WRAP; stamp += 4999)
        CHECK(DecodeHead(stamp, 0) == -1);

    /* No id may collide with the stride, or a particle index would leak into the
     * generation field and a stale cell would read as live. */
    CHECK(EncodeHead(1, PBD_MAX_PARTICLES - 1) < 2 * PBD_HEAD_STRIDE);
    CHECK(PBD_MAX_PARTICLES < PBD_HEAD_STRIDE);

    /* The wrap threshold must keep the packed value inside a signed 32-bit int,
     * with room for the id — this is the arithmetic that decides when the one
     * real clear has to run again. */
    {
        long long worst = (long long)PBD_STAMP_WRAP * PBD_HEAD_STRIDE + PBD_MAX_PARTICLES;
        printf("      widest packed head at the wrap threshold: %lld (int max %d)\n", worst, 2147483647);
        CHECK(worst < 2147483647LL);
    }

    char *comp = ReadFile("core/fluid/shaders/fluid_pbd_gpu.comp");
    char *c = ReadFile("core/fluid/fluid_pbd_gpu.c");
    if (!comp || !c) { printf("FAIL: cannot read the PBD compute shader or its host\n"); bad++; }
    else
    {
        CHECK(strstr(comp, "#define PBD_HEAD_STRIDE 4096") != NULL);
        CHECK(strstr(comp, "stored / PBD_HEAD_STRIDE == u_gridStamp") != NULL);
        /* Every read of the head table must decode; a raw read would treat a
         * stale generation's index as a live neighbour. */
        CHECK(strstr(comp, "int n=decodeHead(heads[") != NULL);
        CHECK(strstr(comp, "next[id]=atomicExchange(heads[cell],int(id))") == NULL);
        /* And the host must bump the generation before each rebuild, or two
         * rebuilds share one grid. */
        CHECK(strstr(c, "s_gridStamp++") != NULL);
        CHECK(strstr(c, "if(s_gridStamp>PBD_STAMP_WRAP) FluidPBDGPU_ClearGrid();") != NULL);
        /* The per-rebuild clear is what this replaces: it must be gone from the
         * solve loop and survive only in the once-per-impact helper. */
        int clears = 0;
        for (const char *p = c; (p = strstr(p, "rlComputeShaderDispatch((cells+255)/256")) != NULL; p += 4) clears++;
        printf("      grid-clear dispatch sites in the host: %d (was 2, per frame)\n", clears);
        CHECK(clears == 1);
        free(comp); free(c);
    }

    printf("%s: fluid_pbd_grid_stamp_test (%d failures)\n", bad ? "FAIL" : "PASS", bad);
    return bad ? 1 : 0;
}
