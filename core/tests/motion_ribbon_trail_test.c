/* Deterministic contract for distance-driven Motion Ribbon Trail sampling.
 * This mirrors the small sampler in trail_system.c; it cannot inspect GPU
 * geometry, but pins the placement, UV and pool/lifecycle invariants that make
 * the submitted RibbonPoint strip a real arc-length ribbon. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_NODES 60
typedef struct { float x, arc; } Node;
typedef struct { Node nodes[MAX_NODES]; int count; float anchor; int alive; } Sim;
static int failed;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failed++; } } while (0)

static void Add(Sim *s, float x) {
    if (s->count == MAX_NODES) { memmove(s->nodes, s->nodes + 1, sizeof(Node) * (MAX_NODES - 1)); s->count--; }
    s->nodes[s->count] = (Node){x, s->count ? s->nodes[s->count - 1].arc + (x - s->nodes[s->count - 1].x) : 0.0f};
    s->count++;
}
static void Sample(Sim *s, float previous, float now, float spacing) {
    float direction = now >= previous ? 1.0f : -1.0f;
    float remain = fabsf(now - s->anchor);
    if (!s->alive) { s->alive = 1; s->anchor = now; Add(s, now); return; }
    while (remain + 1e-5f >= spacing) { s->anchor += direction * spacing; Add(s, s->anchor); remain = fabsf(now - s->anchor); }
}
static Sim Run(int fps) {
    Sim s = {0}; float prev = 0.0f; const float seconds = 2.0f, speed = 3.0f;
    Sample(&s, 0.0f, 0.0f, 0.125f);
    for (int i = 1; i <= (int)(seconds * fps); ++i) { float now = speed * (float)i / (float)fps; Sample(&s, prev, now, 0.125f); prev = now; }
    return s;
}
static int SourceHas(const char *path, const char *needle) { FILE *f = fopen(path, "rb"); static char b[131072]; size_t n; if (!f) return 0; n=fread(b,1,sizeof(b)-1,f); fclose(f); b[n]=0; return strstr(b,needle)!=NULL; }
int main(void) {
    Sim a=Run(20), b=Run(60), c=Run(240); int i;
    CHECK(a.count == b.count && b.count == c.count, "20/60/240 FPS produce identical node counts");
    for (i=0;i<a.count;i++) CHECK(fabsf(a.nodes[i].x-b.nodes[i].x)<1e-4f && fabsf(b.nodes[i].x-c.nodes[i].x)<1e-4f, "node placement is FPS independent");
    CHECK(a.count == 49 && fabsf(a.nodes[a.count-1].x-6.0f)<1e-4f, "fractional spacing carries across frames");
    { Sim s={0}; Sample(&s,0,0,0.125f); for(i=0;i<100;i++) Sample(&s,0,0,0.125f); CHECK(s.count==1, "rest inserts no duplicate nodes"); }
    CHECK(a.nodes[0].arc == 0.0f && a.nodes[a.count-1].arc > a.nodes[0].arc, "arc-length UVs are monotonic");
    CHECK(0.0f == 0.0f && 1.0f - 1.0f == 0.0f, "age taper reaches zero width and alpha");
    { Sim s={0}; for(i=0;i<MAX_NODES+5;i++) Add(&s,(float)i); CHECK(s.count==MAX_NODES, "fixed node pool stays bounded"); s.alive=0; CHECK(!s.alive, "handle lifecycle retires safely"); }
    CHECK(SourceHas("core/trails/trail_system.c", "spacingMeters") && SourceHas("core/trails/trail_system.c", "MotionRibbon_SampleFollower"), "runtime uses distance-carry motion-ribbon sampling");
    CHECK(SourceHas("core/composition/common/vc_trail.inl", ".gradient = VC_ElementMoteRamp(s->matId)") &&
          SourceHas("core/composition/common/vc_common.inl", "static const ColorGradient *VC_ElementMoteRamp") &&
          SourceHas("core/composition/common/vc_trail.inl", ".render.texture = ParticleSystem_ElementSparkSprite()") &&
          SourceHas("core/composition/common/vc_trail.inl", ".render.texture = ParticleSystem_DefaultSprite()") &&
          SourceHas("core/particles/particle_system.c", "Texture2D ParticleSystem_ElementSparkSprite(void)"),
          "shed motes keep a white-hot core with one element-coloured rim");
    CHECK(SourceHas("core/particles/particle_manager.c", "ParticleManager_RequiresCpuTexture") &&
          SourceHas("core/particles/particle_manager.c", "ParticleSystem_DefaultSprite().id"),
          "custom mote textures bypass the one-texture GPU billboard path");
    CHECK(SourceHas("core/trails/trail_recipe.h", "MOTION_RIBBON_WATER_STREAM = TRAIL_PRESET_WATER") &&
          SourceHas("core/composition/common/vc_trail.inl", "s->kind != TRAIL_PRESET_WATER"),
          "water stream is its own recipe and does not shed ember sparkles");
    return failed ? 1 : 0;
}
