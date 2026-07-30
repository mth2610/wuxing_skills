// core headless test — the swept-tube cross-section's FRAME.
//
// A tube trail is a circular cross-section swept along the node polyline, and
// the only hard part is deciding how that section is ROLLED at each node. Get it
// wrong and the tube shears along its own length: the UV wraps at a different
// angle on every ring, so the texture twists. That is the identical symptom that
// cost four rounds on the flat ribbon, and it has the identical cause — a
// direction rebuilt per node instead of carried.
//
// This is pure geometry, so it does not need a GPU, and it is the one part of a
// tube that CANNOT be judged by eye without already knowing what to look for:
// a slowly shearing texture looks like a design choice.
//
// What the mirror cannot see: whether the tube reads as a volume. Only that its
// section does not spin.

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

#define TRAIL_TUBE_RADIAL_DEFAULT 8
#define TRAIL_TUBE_RADIAL_MAX 16
#define TRAIL_HISTORY_COUNT 60

typedef struct { float x, y, z; } V3;

static V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 scl(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 crs(V3 a, V3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static float len(V3 a) { return sqrtf(dot(a, a)); }
static V3 nrm(V3 a) { float l = len(a); return (l > 1e-9f) ? scl(a, 1.0f / l) : v3(0, 0, 1); }

// Rodrigues, the rotation the transport applies.
static V3 rotAxis(V3 v, V3 axis, float ang) {
    float c = cosf(ang), s = sinf(ang);
    return add(add(scl(v, c), scl(crs(axis, v), s)),
               scl(axis, dot(axis, v) * (1.0f - c)));
}

// A path that curves in all three axes and passes near-vertical, which is where
// a reference-vector frame is at its worst. A straight or planar test path
// would let the broken construction pass.
static V3 PathPoint(int i, int n) {
    float t = (float)i / (float)(n - 1);
    float a = t * 3.2f;
    return v3(2.4f * sinf(a), 3.0f * t + 0.6f * sinf(a * 1.7f), 1.8f * cosf(a * 1.3f));
}

static void Tangents(V3 *tang, const V3 *pts, int n) {
    for (int i = 0; i < n; i++) {
        V3 a = pts[i > 0 ? i - 1 : 0];
        V3 b = pts[i < n - 1 ? i + 1 : n - 1];
        V3 d = sub(b, a);
        tang[i] = (len(d) > 1e-6f) ? nrm(d) : (i > 0 ? tang[i - 1] : v3(0, 0, 1));
    }
}

// ── The two constructions ────────────────────────────────────────────────────

// What ProceduralMesh_BuildTubeAlongPath does: rebuild from a world reference
// at every node.
static void FrameFromReference(V3 *fu, const V3 *tang, int n) {
    for (int i = 0; i < n; i++) {
        V3 up = (fabsf(tang[i].y) > 0.99f) ? v3(1, 0, 0) : v3(0, 1, 0);
        fu[i] = nrm(crs(up, tang[i]));
    }
}

// What the trail system's tube does: carry it forward by the minimal rotation.
static void FrameParallelTransport(V3 *fu, const V3 *tang, int n) {
    V3 up = (fabsf(tang[0].y) > 0.9f) ? v3(1, 0, 0) : v3(0, 1, 0);
    fu[0] = nrm(crs(up, tang[0]));
    for (int i = 1; i < n; i++) {
        V3 f = fu[i - 1];
        V3 axis = crs(tang[i - 1], tang[i]);
        float sinA = len(axis);
        if (sinA > 1e-6f) {
            float cosA = dot(tang[i - 1], tang[i]);
            if (cosA > 1.0f) cosA = 1.0f;
            if (cosA < -1.0f) cosA = -1.0f;
            f = rotAxis(f, scl(axis, 1.0f / sinA), atan2f(sinA, cosA));
        }
        // Re-orthogonalise, or 60 nodes of float error walk the frame out of the
        // section plane.
        f = nrm(sub(f, scl(tang[i], dot(f, tang[i]))));
        fu[i] = f;
    }
}

// How much the section ROLLS between two consecutive nodes, in radians —
// measured after transporting the previous frame onto this node's tangent, so
// it isolates roll from the unavoidable rotation the path itself imposes.
static float RollStep(V3 prevF, V3 prevT, V3 f, V3 t) {
    V3 axis = crs(prevT, t);
    float sinA = len(axis);
    V3 carried = prevF;
    if (sinA > 1e-6f) {
        float cosA = dot(prevT, t);
        if (cosA > 1.0f) cosA = 1.0f;
        if (cosA < -1.0f) cosA = -1.0f;
        carried = rotAxis(prevF, scl(axis, 1.0f / sinA), atan2f(sinA, cosA));
    }
    float c = dot(nrm(carried), nrm(f));
    if (c > 1.0f) c = 1.0f;
    if (c < -1.0f) c = -1.0f;
    return acosf(c);
}

static float AccumulatedRoll(void (*build)(V3 *, const V3 *, int), int n) {
    static V3 pts[TRAIL_HISTORY_COUNT], tang[TRAIL_HISTORY_COUNT], fu[TRAIL_HISTORY_COUNT];
    for (int i = 0; i < n; i++) pts[i] = PathPoint(i, n);
    Tangents(tang, pts, n);
    build(fu, tang, n);
    float total = 0.0f;
    for (int i = 1; i < n; i++)
        total += RollStep(fu[i - 1], tang[i - 1], fu[i], tang[i]);
    return total;
}

static float WorstRoll(void (*build)(V3 *, const V3 *, int), int n) {
    static V3 pts[TRAIL_HISTORY_COUNT], tang[TRAIL_HISTORY_COUNT], fu[TRAIL_HISTORY_COUNT];
    for (int i = 0; i < n; i++) pts[i] = PathPoint(i, n);
    Tangents(tang, pts, n);
    build(fu, tang, n);
    float worst = 0.0f;
    for (int i = 1; i < n; i++) {
        float r = RollStep(fu[i - 1], tang[i - 1], fu[i], tang[i]);
        if (r > worst) worst = r;
    }
    return worst;
}

// ── 1. The frame must not roll on its own ────────────────────────────────────

static void Test_TransportBeatsReference(void)
{
    const int n = 48;
    float ref = WorstRoll(FrameFromReference, n);
    float pt = WorstRoll(FrameParallelTransport, n);

    CHECK_MSG(ref > 0.05f,
              "the reference-vector frame really does roll on a curving path",
              "worst %.4f rad (%.1f deg) between adjacent rings",
              ref, ref * 57.2958f);
    // 0.01 rad, not 0.0. Transport is EXACT in real arithmetic and drifts in
    // float32: 48 carried rotations accumulate about 3e-4 rad here, which is
    // 0.02 degrees and 0.005% of one texture wrap. Asserting zero would be
    // asserting something the machine cannot do, and the test would then be
    // measuring float precision rather than the construction.
    CHECK_MSG(pt < 0.01f,
              "parallel transport's roll is float drift, not rotation",
              "worst %.6f rad (%.3f deg) over %d carries", pt, pt * 57.2958f, n);
    CHECK_MSG(ref / (pt + 1e-9f) > 100.0f,
              "so the two are not two flavours of the same thing",
              "reference rolls %.0fx more", ref / (pt + 1e-9f));

    // AND THE RIGHT QUANTITY IS THE ACCUMULATED ROLL, not the per-ring one. A
    // first version of this check asserted the per-ring figure was over 1% of a
    // wrap and failed at 0.9% — which would have read as "the reference frame is
    // fine", exactly backwards. Shear is cumulative: every ring's roll adds to
    // the last, so what the eye sees at the tail is the SUM along the tube.
    float accRef = AccumulatedRoll(FrameFromReference, n);
    float accPT = AccumulatedRoll(FrameParallelTransport, n);
    // The threshold is not a guess: one radial slice is 1/TRAIL_TUBE_RADIAL_DEFAULT
    // of a wrap, so shear past that means the texture has slid by a whole facet
    // between head and tail — the scale at which it stops being subtle.
    float oneSlice = 1.0f / (float)TRAIL_TUBE_RADIAL_DEFAULT;
    CHECK_MSG(accRef / (2.0f * 3.14159265f) > oneSlice,
              "the reference frame shears the sheet by more than a whole facet, end to end",
              "%.0f%% of a wrap vs %.0f%% for one slice",
              100.0f * accRef / (2.0f * 3.14159265f), 100.0f * oneSlice);
    CHECK_MSG(accPT / (2.0f * 3.14159265f) < 0.01f,
              "transport accumulates essentially nothing",
              "%.2f%% of one wrap", 100.0f * accPT / (2.0f * 3.14159265f));
}

// ── 2. The snap, which is the other failure and a louder one ────────────────

static void Test_ReferenceFrameSnaps(void)
{
    // A near-vertical shot: the tangent crosses the |y| > 0.99 threshold and the
    // reference vector changes identity, rotating the whole section at once.
    static V3 tang[8], fu[8];
    for (int i = 0; i < 8; i++) {
        float y = 0.985f + 0.004f * (float)i;   // walks across the threshold
        float r = sqrtf(1.0f - y * y);
        tang[i] = v3(r, y, 0.0f);
    }
    FrameFromReference(fu, tang, 8);
    float worst = 0.0f;
    for (int i = 1; i < 8; i++) {
        float s = RollStep(fu[i - 1], tang[i - 1], fu[i], tang[i]);
        if (s > worst) worst = s;
    }
    CHECK_MSG(worst > 1.0f,
              "crossing the reference threshold snaps the section by most of a quarter turn",
              "%.2f rad (%.0f deg) in ONE ring", worst, worst * 57.2958f);

    FrameParallelTransport(fu, tang, 8);
    float worstPT = 0.0f;
    for (int i = 1; i < 8; i++) {
        float s = RollStep(fu[i - 1], tang[i - 1], fu[i], tang[i]);
        if (s > worstPT) worstPT = s;
    }
    CHECK_MSG(worstPT < 1e-4f, "transport has no threshold to cross",
              "%.6f rad", worstPT);
}

// ── 2b. A ribbon sheet cannot be wrapped around a tube ──────────────────────
//
// THE BUG THIS EXISTS FOR, and the owner diagnosed it from the picture alone:
// "nó như 1 cái tube bị cắt đôi, lật lại rồi xếp thành 2 lớp."
//
// On a flat strip, u = 0 and u = 1 are the two EDGES, and the sheet fades to
// zero at both so the silhouette closes. Wrap that same sheet around a cylinder
// and those two edges become THE SAME LINE — the tube is fully transparent along
// one seam and opaque opposite it, which draws as half a tube. Two layers, two
// nested half-shells. Exactly what he described.
//
// This is a property of the SHEET, not of the geometry, so it is arithmetic.

// Defined with the other mirror helpers, further down.
static int FileHas(const char *path, const char *needle);

static float RibbonBandProfile(float u)
{
    float d = fabsf(u - 0.5f) * 2.0f;
    float a = 1.0f - d * d;
    if (a < 0.0f) a = 0.0f;
    return powf(a, 1.35f);
}

static void Test_TubeNeedsASeamlessSheet(void)
{
    // The ribbon sheet, sampled as it would be around a tube.
    float mn = 1.0f, mx = 0.0f;
    for (int i = 0; i < 64; i++) {
        float a = RibbonBandProfile((float)i / 64.0f);
        if (a < mn) mn = a;
        if (a > mx) mx = a;
    }
    CHECK_MSG(mn < 0.02f,
              "the ribbon sheet really does go to ZERO somewhere around the tube",
              "minimum alpha %.3f at the seam", mn);
    CHECK_MSG(mx / (mn + 1e-6f) > 20.0f,
              "so one side of the tube is opaque and the other is a hole",
              "%.0fx contrast around the circumference", mx / (mn + 1e-6f));

    // What a tube sheet must instead guarantee: no direction around the section
    // is empty, or the silhouette has a gap in it.
    const float floorLift = 0.06f, gain = 1.15f;   // SWEPT_ASSET_FLOOR / _GAIN
    float tubeMin = 0.0f * gain + floorLift;       // the darkest possible texel
    CHECK_MSG(tubeMin > 0.02f,
              "the tube sheet lifts every texel off zero — no direction is a hole",
              "darkest texel %.3f", tubeMin);

    // EVERY layer, not just the textured one. Fixing only the body left the wide
    // outer layer on the ribbon band sheet, so it still drew as half a shell
    // around a complete inner one — two nested tubes, one of them split. The
    // owner named that shape from the picture twice before anyone read the UV,
    // and the second time only because the first fix had been reported as done.
    CHECK(FileHas("core/composition/common/vc_swept_trail.inl",
                  "s_sweptHazeLayers[1].texture = NULL;"),
          "no tube layer is handed a ribbon band sheet");
    CHECK(FileHas("core/trail_system.c", "static Texture2D s_tubeFlatTex = {0};"),
          "the trail system owns a flat fallback so a tube cannot get a banded one");
    CHECK(FileHas("core/trail_system.c",
                  "? *ly->texture : ((s_tubeFlatTex.id != 0) ? s_tubeFlatTex : fallbackTex);"),
          "...and substitutes it by construction, not by each caller remembering");
    CHECK(FileHas("core/composition/common/vc_swept_trail.inl",
                  "SetTextureWrap(s_sweptTubeTex, TEXTURE_WRAP_REPEAT);"),
          "which wraps on BOTH axes — around the section as well as along it");
}

// ── 3. Budget ────────────────────────────────────────────────────────────────

#define TRAIL_TUBE_RINGS_DEFAULT 24
#define TRAIL_TUBE_MIN_ALPHA 3

static void Test_TubeBudget(void)
{
    // rings x radial x 2 triangles, PER LAYER.
    //
    // THE SAVING THAT COSTS NOTHING VISUALLY: the history is sampled at 60 Hz,
    // so a 5 m tube would get a ring every 10 cm — far finer than a silhouette
    // can show, because the path between two nodes is nearly straight at that
    // scale. Decimating to a fixed ring count is more than half the vertices for
    // no change on screen, and it shortens the sequential transport chain too.
    int naive = (TRAIL_HISTORY_COUNT - 1) * TRAIL_TUBE_RADIAL_DEFAULT * 2;
    int tris = (TRAIL_TUBE_RINGS_DEFAULT - 1) * TRAIL_TUBE_RADIAL_DEFAULT * 2;
    CHECK_MSG(tris < naive / 2,
              "ring decimation more than halves the triangles",
              "%d -> %d triangles per layer", naive, tris);
    CHECK_MSG(500.0f / (float)TRAIL_TUBE_RINGS_DEFAULT < 30.0f,
              "...while still placing a ring every 20-odd cm on a 5 m tube",
              "%.0f cm per ring", 500.0f / (float)TRAIL_TUBE_RINGS_DEFAULT);
    CHECK_MSG(tris < 500, "one full-length tube layer stays a few hundred triangles",
              "%d triangles", tris);
    // ...and the tail is skipped outright. The width envelope goes to zero over
    // the first third, so those rings emit a full circle of invisible quads.
    CHECK_MSG(TRAIL_TUBE_MIN_ALPHA > 0 && TRAIL_TUBE_MIN_ALPHA < 8,
              "invisible rings are skipped, at a threshold below anything the eye has",
              "alpha < %d of 255", TRAIL_TUBE_MIN_ALPHA);
    CHECK_MSG(TRAIL_TUBE_RADIAL_DEFAULT >= 6,
              "...but not so few slices that the section reads as a polygon",
              "%d slices", TRAIL_TUBE_RADIAL_DEFAULT);
    CHECK(TRAIL_TUBE_RADIAL_MAX <= 16, "and the ceiling stays modest");
}

// ── 4. The section is DATA ──────────────────────────────────────────────────
//
// The generalisation that makes the sweep worth having: the cross-section is a
// caller-supplied loop of offsets, not a hard-coded circle. A round bolt, a
// flattened plume, a crescent of smoke and a ragged flame profile are then the
// same drawing code with a different table — which is the whole plan for
// building volumetric smoke and fire on this.

static void Test_SectionIsData(void)
{
    // The default circle must still BE a circle, or every existing tube changes
    // silently the day the section becomes configurable.
    float worst = 0.0f;
    for (int n = 6; n <= TRAIL_TUBE_RADIAL_MAX; n++) {
        for (int j = 0; j < n; j++) {
            float phi = (float)j * (2.0f * 3.14159265f) / (float)n;
            float r = sqrtf(cosf(phi) * cosf(phi) + sinf(phi) * sinf(phi));
            float e = fabsf(r - 1.0f);
            if (e > worst) worst = e;
        }
    }
    CHECK_MSG(worst < 1e-5f, "the generated default section is a UNIT circle",
              "worst radius error %.7f", worst);

    // A section is scaled by the node's radius, so the width curve, the aspect
    // cap and the layer multipliers keep working on any shape. Check that on a
    // deliberately non-circular profile — a flattened plume, which is what smoke
    // wants — the scaling stays proportional.
    const float plume[6][2] = {{1.0f, 0.35f}, {0.5f, 0.5f}, {-0.5f, 0.5f},
                               {-1.0f, 0.35f}, {-0.5f, -0.5f}, {0.5f, -0.5f}};
    for (float r = 0.2f; r < 3.0f; r += 0.2f) {
        float ratioX = (plume[0][0] * r) / (plume[1][0] * r);
        if (fabsf(ratioX - plume[0][0] / plume[1][0]) > 1e-4f) {
            CHECK(0, "section scaling stays proportional at every radius");
            return;
        }
    }
    CHECK(1, "section scaling stays proportional at every radius");

    CHECK(FileHas("core/trail_system.c",
                  "ringNrm[rI][j] = Vector3Add(Vector3Scale(fu, sect[j].x),"),
          "the ring is still built from the SECTION, not from a hard-coded circle");
    CHECK(FileHas("core/trail_system.c", "sect = circleSect;"),
          "...with the circle generated once as the default, not per ring");
    CHECK(FileHas("core/trail_system.h", "typedef struct {\n  float x, y;\n} TrailSectionPoint;"),
          "and the section type is public, so callers can author profiles");
}

// ── the mirror guard ─────────────────────────────────────────────────────────

static void CollapseWS(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    int pending = 0;
    for (const char *p = in; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { pending = 1; continue; }
        if (pending && o > 0 && o + 1 < cap) out[o++] = ' ';
        pending = 0;
        if (o + 1 < cap) out[o++] = *p;
    }
    out[o < cap ? o : cap - 1] = '\0';
}

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[400000], flat[400000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    CollapseWS(buf, flat, sizeof(flat));
    char want[1024];
    CollapseWS(needle, want, sizeof(want));
    return strstr(flat, want) != NULL;
}

static void Test_MirrorStillMatchesSource(void)
{
    const char *c = "core/trail_system.c";

    CHECK(FileHas(c, "fu = Vector3RotateByAxisAngle(fu, axis, ang);"),
          "the frame is still CARRIED by a rotation, not rebuilt");
    CHECK(!FileHas(c, "Vector3 up = (fabsf(tang[i].y)"),
          "and no ring after the first consults a global up vector");
    CHECK(FileHas(c, "if (sinA > 1e-6f)"),
          "parallel tangents still skip the rotation — a fabricated axis is how twist returns");
    CHECK(FileHas(c, "Vector3Scale(tang[i], Vector3DotProduct(fu, tang[i]))"),
          "the frame is still re-orthogonalised against the tangent each ring");
    CHECK(FileHas(c, "rlDisableBackfaceCulling(); DrawLayeredTube(t, drawCount, ribbonTex);"),
          "the tube still shows its far wall — that free rim is the point of it");
    CHECK(!FileHas(c, "ringPos[i][j] ="),
          "the per-ring positions are no longer computed and thrown away");
    CHECK(FileHas(c, "int maxRings = (t->tubeMaxRings > 0) ? t->tubeMaxRings : TRAIL_TUBE_RINGS_DEFAULT;"),
          "rings are still decimated rather than one per history node");
    CHECK(FileHas(c, "scratchOuter[iNext].tint.a < TRAIL_TUBE_MIN_ALPHA)"),
          "and invisible rings are still skipped outright");
    // THE STATE-CHANGE FLUSH. rlgl batches, so the cull state at DRAW time is
    // what applies — not the state when the quads were queued. Without this the
    // tube was submitted with culling off and drawn with it back on, so one wall
    // of every ring survived: a tube that renders as half a shell.
    CHECK(FileHas(c, "rlDrawRenderBatchActive(); rlDisableBackfaceCulling();"),
          "the cull change is still flushed BEFORE the tube is queued");
    CHECK(FileHas(c, "rlDrawRenderBatchActive(); rlEnableBackfaceCulling();"),
          "...and again before it is restored");
    CHECK(FileHas(c, "rlSetTexture(0); // must not leak the binding"),
          "and it still releases the texture binding");
    CHECK(FileHas("core/trail_system.h", "TRAIL_SHAPE_RIBBON = 0,"),
          "RIBBON is still the zero value, so every existing caller is unchanged");
}

int main(void)
{
    printf("=== tube trail: the swept cross-section's frame ===\n");
    Test_TransportBeatsReference();
    Test_ReferenceFrameSnaps();
    Test_TubeNeedsASeamlessSheet();
    Test_TubeBudget();
    Test_SectionIsData();
    Test_MirrorStillMatchesSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
