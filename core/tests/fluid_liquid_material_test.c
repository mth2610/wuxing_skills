// core headless test — the SSF liquid table: per-pixel material id + the three
// optical classes (core/fluid/fluid_surface.{h,c}, core/fluid/shaders/*.fs).
//
// Until 2026-08-12 the surface carried ONE material globally
// (FluidSurface_SetMaterialColors), so two liquids of different colours could
// not exist in the same frame at all. The capture now rasterizes a liquid-table
// slot into the front target's B channel alongside the depth, and the composite
// looks the material up per pixel.
//
// What this mirrors numerically: the slot encode/round-trip, the LRU/dedup rule
// the C table uses, and the emissive class's thickness-driven saturation.
//
// What it CANNOT validate, stated so nobody reads a green run as more than it
// is: that the capture target is actually RGBA32F on the device, that the
// point-filter survives, that the GPU backend binds u_materialId to the right
// draw, or any pixel of the final image. Those need the sandbox fixture
// (NEW FX -> LIQUID BENCH), which puts water, lava and liquid metal on screen
// in one capture and renders them in one colour if this path regresses.
//
// In particular it does NOT assert that a tap landing between two materials
// resolves to either of them. It cannot: the id is POINT-filtered but the
// composite and the capture are the same size only at HIGH, so at MED/LOW a tap
// can fall between texels and round to a slot that is neither neighbour. That
// is a known one-texel band at a boundary between two liquids, not a promise
// the algorithm makes.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

#define SLOTS 4

/* Mirror of the composite's decode: `int slot = clamp(int(raw + 0.5), 0, N-1)`. */
static int DecodeSlot(float raw)
{
    int s = (int)floorf(raw + 0.5f);
    if (s < 0) s = 0;
    if (s > SLOTS - 1) s = SLOTS - 1;
    return s;
}

/* Mirror of FluidSurface_BindMaterial's slot allocation: content dedup first,
 * then a free slot, then least-recently-used. `use[]` is the recency stamp. */
static int BindSlot(int table[SLOTS], unsigned use[SLOTS], int *count,
                    unsigned *clock, int liquid)
{
    for (int i = 0; i < *count; ++i)
        if (table[i] == liquid) { use[i] = ++(*clock); return i; }
    int slot;
    if (*count < SLOTS) slot = (*count)++;
    else {
        slot = 0;
        for (int i = 1; i < SLOTS; ++i) if (use[i] < use[slot]) slot = i;
    }
    table[slot] = liquid;
    use[slot] = ++(*clock);
    return slot;
}

/* Mirror of the emissive branch's saturation: one FLUID_REFERENCE_DEPTH_M emits
 * half the source radiance, hence ln(2)/ref as the extinction. */
#define REF_DEPTH 0.20f
static float EmissionDepth(float opticalPath)
{
    return 1.0f - expf(-(0.693147f / REF_DEPTH) * opticalPath);
}

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int main(void)
{
    int bad = 0;

    /* --- 1. Slot round-trip. Every slot the C side can hand out must survive
     *        the float trip through the capture's B channel. */
    for (int slot = 0; slot < SLOTS; ++slot) CHECK(DecodeSlot((float)slot) == slot);

    /* An id that arrives slightly off (float target, then a tap at a texel
     * centre) still resolves to its own slot rather than a neighbour. */
    CHECK(DecodeSlot(1.999f) == 2);
    CHECK(DecodeSlot(2.001f) == 2);
    /* Out of range is clamped, never indexed past the table. */
    CHECK(DecodeSlot(-3.0f) == 0);
    CHECK(DecodeSlot(99.0f) == SLOTS - 1);

    /* --- 2. Table allocation. */
    {
        int table[SLOTS] = {0}; unsigned use[SLOTS] = {0}; int count = 0; unsigned clock = 0;
        /* Distinct liquids get distinct slots — the whole point. */
        CHECK(BindSlot(table, use, &count, &clock, 100) == 0);
        CHECK(BindSlot(table, use, &count, &clock, 200) == 1);
        CHECK(BindSlot(table, use, &count, &clock, 300) == 2);
        /* Re-binding the same liquid REUSES its slot: a composer that binds
         * every frame must not consume the table. */
        CHECK(BindSlot(table, use, &count, &clock, 100) == 0);
        CHECK(BindSlot(table, use, &count, &clock, 200) == 1);
        CHECK(count == 3);
        /* Overflow evicts the least recently used, never a live one. Slot 2
         * (liquid 300) has the oldest stamp after the re-binds above. */
        CHECK(BindSlot(table, use, &count, &clock, 400) == 3);
        CHECK(BindSlot(table, use, &count, &clock, 500) == 2);
        CHECK(table[0] == 100 && table[1] == 200);
    }

    /* --- 3. Emissive saturation.
     *        Zero thickness emits nothing — a body's silhouette must not glow
     *        where there is no body. This is exact, not a tolerance. */
    CHECK(EmissionDepth(0.0f) == 0.0f);
    /* One reference depth emits half. This is the STATED contract; the first
     * version used 2.0/ref ("86% at one reference depth"), which saturated by
     * 0.3 m — and an authored body is 0.3-0.6 m through the middle, so the
     * whole thing sat at full emission with no gradient and clipped to white. */
    CHECK(fabsf(EmissionDepth(REF_DEPTH) - 0.5f) < 1e-5f);
    /* Monotone and bounded: thicker always glows more, and never runs away. */
    {
        float previous = -1.0f;
        for (float t = 0.0f; t <= 1.0f; t += 0.05f)
        {
            float e = EmissionDepth(t);
            CHECK(e > previous);
            CHECK(e < 1.0f);
            previous = e;
        }
    }
    /* A rim one kernel thick is still visibly cooler than the core — the
     * "thin = cooled crust" the material is for. Over the authored range this
     * must be a real gradient, not two values a few percent apart. */
    CHECK(EmissionDepth(0.55f) - EmissionDepth(0.10f) > 0.20f);

    /* --- 4. The load-bearing shader expressions still exist, so the mirrors
     *        above cannot silently drift away from the GLSL. */
    {
        char *fs = ReadFile("core/fluid/shaders/fluid_surface.fs");
        if (!fs) { printf("FAIL: cannot read fluid_surface.fs\n"); bad++; }
        else
        {
            CHECK(strstr(fs, "u_materialIdTex") != NULL);
            CHECK(strstr(fs, "clamp(int(rawSlot + 0.5), 0, FLUID_MATERIAL_SLOTS - 1)") != NULL);
            CHECK(strstr(fs, "0.693147 / FLUID_REFERENCE_DEPTH_M") != NULL);
            /* The three classes must all still be reachable. */
            CHECK(strstr(fs, "FLUID_CLASS_EMISSIVE") != NULL);
            CHECK(strstr(fs, "FLUID_CLASS_CONDUCTOR") != NULL);
            /* A conductor has NO transmission: its base must not be the
             * dielectric assembly (transmitted + inScatter under Fresnel). */
            CHECK(strstr(fs, "base = reflection * conductorFresnel") != NULL);
            /* Water's IOR must not be hardcoded again. It used to appear as
             * two 0.02037 literals and one 1.0/1.333, which is precisely why a
             * liquid that is not water could not be expressed. The patterns
             * below are the CODE shapes, not the bare number: this file's
             * comments discuss the old constant by name and must stay legal. */
            CHECK(strstr(fs, "waterF0 = 0.02037") == NULL);
            CHECK(strstr(fs, "0.02037 + (1.0 - 0.02037)") == NULL);
            CHECK(strstr(fs, "N, 1.0 / 1.333") == NULL);
            /* ...and the parameterized forms that replaced them are present. */
            CHECK(strstr(fs, "refract(incident, N, 1.0 / materialIor)") != NULL);
            CHECK(strstr(fs, "IorToF0(materialIor)") != NULL);
            CHECK(strstr(fs, "FresnelSchlick(vdh, f0)") != NULL);
            free(fs);
        }
    }
    {
        /* The id must ride the winning fragment of the depth test, in the same
         * write — a separate pass could disagree with the depth it labels. */
        char *cap = ReadFile("core/fluid/shaders/fluid_capture_particle.fs");
        if (!cap) { printf("FAIL: cannot read fluid_capture_particle.fs\n"); bad++; }
        else
        {
            CHECK(strstr(cap, "vec4(depth, coverage, u_materialId, 1.0)") != NULL);
            free(cap);
        }
    }

    printf(bad ? "fluid_liquid_material_test: %d failure(s)\n"
               : "PASS: fluid_liquid_material_test (0 failures)\n", bad);
    return bad ? 1 : 0;
}
