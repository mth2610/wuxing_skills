#include "core/atmosphere.h"
#include "core/vfx_render.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>

#define ATMO_MAX_MOTES 512

// Dust / spirit motes — additive glints drifting in the air. (Ground mist was
// prototyped here too but removed: camera-facing billboards clip hard where
// they intersect the floor, which needs soft-particle depth fade to fix — not
// worth it for now.)
typedef struct {
    Vector3 pos;
    Vector3 drift;
    float   phase;
    float   swaySpd;
    float   size;
    float   bright;
} Mote;

static Mote      s_motes[ATMO_MAX_MOTES];
static int       s_count  = 0;
static Texture2D s_dotTex = {0};
static Vector3   s_center = { 6.0f, 3.0f, 4.4f };
static Vector3   s_extent = { 15.0f, 5.0f, 15.0f };
static Color     s_tint   = { 160, 190, 235, 255 };
static bool      s_ready  = false;

static inline float Rand01(void) { return (float)rand() / (float)RAND_MAX; }
static inline float RandRange(float a, float b) { return a + (b - a) * Rand01(); }

static inline float WrapAxis(float p, float center, float ext) {
    float rel = p - center;
    if (rel >  ext) rel -= 2.0f * ext;
    if (rel < -ext) rel += 2.0f * ext;
    return center + rel;
}

static void SeedMote(Mote *m) {
    m->pos = (Vector3){
        s_center.x + RandRange(-s_extent.x, s_extent.x),
        s_center.y + RandRange(-s_extent.y, s_extent.y),
        s_center.z + RandRange(-s_extent.z, s_extent.z),
    };
    m->drift   = (Vector3){ RandRange(-0.05f, 0.05f), RandRange(0.02f, 0.10f), RandRange(-0.05f, 0.05f) };
    m->phase   = RandRange(0.0f, 6.2831853f);
    m->swaySpd = RandRange(0.3f, 0.9f);
    m->size    = RandRange(0.035f, 0.10f);
    m->bright  = RandRange(0.5f, 1.0f);
}

void Atmosphere_Init(void) {
    // Soft round dot with a tight, punchy core — reads as a glint when additive.
    const int R = 32;
    Image img = GenImageColor(R, R, (Color){0, 0, 0, 0});
    for (int y = 0; y < R; y++)
        for (int x = 0; x < R; x++) {
            float dx = (x - R * 0.5f + 0.5f) / (R * 0.5f);
            float dy = (y - R * 0.5f + 0.5f) / (R * 0.5f);
            float a = 1.0f - sqrtf(dx * dx + dy * dy);
            if (a < 0.0f) a = 0.0f;
            a = a * a * a;
            ImageDrawPixel(&img, x, y, (Color){255, 255, 255, (unsigned char)(a * 255.0f)});
        }
    s_dotTex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(s_dotTex, TEXTURE_FILTER_BILINEAR);

    s_count = 340;
    for (int i = 0; i < s_count; i++) SeedMote(&s_motes[i]);
    s_ready = true;
    if (getenv("WUXING_NO_ATMO") != NULL) s_ready = false; // perf-diagnostic toggle
}

void Atmosphere_Configure(Vector3 center, Vector3 extent, int count, Color tint) {
    s_center = center;
    s_extent = extent;
    s_tint   = tint;
    if (count < 0) count = 0;
    if (count > ATMO_MAX_MOTES) count = ATMO_MAX_MOTES;
    s_count = count;
    for (int i = 0; i < s_count; i++) SeedMote(&s_motes[i]);
}

void Atmosphere_Update(float dt, Camera3D camera) {
    if (!s_ready) return;
    // Volume locked to the look-at point so the field always fills the view.
    s_center = camera.target;
    for (int i = 0; i < s_count; i++) {
        Mote *m = &s_motes[i];
        m->phase += dt * m->swaySpd;
        Vector3 sway = { cosf(m->phase) * 0.03f, 0.0f, sinf(m->phase * 0.8f) * 0.03f };
        m->pos.x = WrapAxis(m->pos.x + (m->drift.x + sway.x) * dt, s_center.x, s_extent.x);
        m->pos.y = WrapAxis(m->pos.y + (m->drift.y + sway.y) * dt, s_center.y, s_extent.y);
        m->pos.z = WrapAxis(m->pos.z + (m->drift.z + sway.z) * dt, s_center.z, s_extent.z);
    }
}

void Atmosphere_Draw(Camera3D camera) {
    if (!s_ready || s_count <= 0) return;

    // Low-energy background radiance still uses the common scope; semantic
    // passes share the authoritative HDR scene and allocate no extra target.
    VFXRenderScope scope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);

    for (int i = 0; i < s_count; i++) {
        const Mote *m = &s_motes[i];
        float tw = 0.7f + 0.3f * sinf(m->phase * 1.7f);
        float b = m->bright * tw;
        Color c = { (unsigned char)(s_tint.r * b), (unsigned char)(s_tint.g * b),
                    (unsigned char)(s_tint.b * b), 255 };
        DrawBillboard(camera, s_dotTex, m->pos, m->size, c);
    }

    VFXRender_EndDraw(&scope);
}

void Atmosphere_Unload(void) {
    if (s_dotTex.id != 0) UnloadTexture(s_dotTex);
    s_dotTex = (Texture2D){0};
    s_ready = false;
}
