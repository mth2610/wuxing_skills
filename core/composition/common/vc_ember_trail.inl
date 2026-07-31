// P4 — EmberTrail. A handle-owned moving ember source, not a per-frame burst.
#include "core/tuning.h"

#define VFX_EMBER_TRAIL_MAX 12
typedef struct {
    bool active, stopping;
    Vector3 pos, velocity;
    VC_MaterialId mat;
    float scale, rate, accum;
} VC_EmberTrail;
static VC_EmberTrail s_emberTrails[VFX_EMBER_TRAIL_MAX];
static float s_emberTrailRate = 1.0f;
static bool s_emberTrailInit = false;
static Texture2D s_emberTrailTex = {0};

static void EmberTrail_Init(void)
{
    if (s_emberTrailInit) return;
    Tuning_RegisterFloat("ember_trail_rate", &s_emberTrailRate, 1.0f);
    // A neutral soft flare supplies only the falloff. The particle's absolute
    // white additive vertex core is what feeds bloom; this is never a star
    // glint and no material tint is multiplied into it.
    s_emberTrailTex = ResourceManager_LoadTexture("assets/textures/flare.png");
    if (s_emberTrailTex.id != 0)
        SetTextureFilter(s_emberTrailTex, TEXTURE_FILTER_BILINEAR);
    s_emberTrailInit = true;
}

int VFX_EmberTrail_Spawn(Vector3 pos, Vector3 velocity, VC_MaterialId mat,
                         float scale, float embersPerSecond)
{
    EmberTrail_Init();
    for (int i = 0; i < VFX_EMBER_TRAIL_MAX; ++i) if (!s_emberTrails[i].active) {
        s_emberTrails[i] = (VC_EmberTrail){true, false, pos, velocity, mat,
                                            scale > 0.0f ? scale : 1.0f,
                                            embersPerSecond > 0.0f ? embersPerSecond : 14.0f, 0.0f};
        return i;
    }
    return -1;
}

// Sandbox/score entry point. The Spawn name remains the lifecycle API; Compose
// exists so sync_vfx_test can bench the same persistent handle contract.
int VFX_ComposeEmberTrail(Vector3 pos, Vector3 velocity, VC_MaterialId mat,
                          float scale, float embersPerSecond)
{ return VFX_EmberTrail_Spawn(pos, velocity, mat, scale, embersPerSecond); }

void VFX_EmberTrail_SetTransform(int handle, Vector3 pos, Vector3 velocity)
{
    if (handle >= 0 && handle < VFX_EMBER_TRAIL_MAX && s_emberTrails[handle].active) {
        s_emberTrails[handle].pos = pos;
        s_emberTrails[handle].velocity = velocity;
    }
}
void VFX_EmberTrail_Stop(int handle)
{ if (handle >= 0 && handle < VFX_EMBER_TRAIL_MAX) s_emberTrails[handle].stopping = true; }
void VFX_KillEmberTrail(int handle)
{ if (handle >= 0 && handle < VFX_EMBER_TRAIL_MAX) s_emberTrails[handle].active = false; }

static void VC_EmberTrail_Update(float dt)
{
    for (int h = 0; h < VFX_EMBER_TRAIL_MAX; ++h) {
        VC_EmberTrail *e = &s_emberTrails[h];
        if (!e->active) continue;
        if (e->stopping) { e->active = false; continue; }
        e->accum += dt * e->rate * s_emberTrailRate;
        int n = (int)e->accum; if (n > 5) n = 5;
        e->accum -= (float)n;
        for (int i = 0; i < n; ++i) {
            Vector3 jitter = {(Random01()-0.5f)*0.20f*e->scale,
                              (Random01()-0.5f)*0.20f*e->scale,
                              (Random01()-0.5f)*0.20f*e->scale};
            SpawnParticle((ParticleConfig){
                .position = Vector3Add(e->pos, jitter),
                .velocity = Vector3Add(Vector3Scale(e->velocity, 0.32f),
                            (Vector3){(Random01()-0.5f)*0.8f*e->scale,
                                      Math_Mix(0.45f, 1.25f, Random01())*e->scale,
                                      (Random01()-0.5f)*0.8f*e->scale}),
                .radius = Math_Mix(0.025f, 0.060f, Random01()) * e->scale,
                .lifetime = Math_Mix(0.40f, 0.80f, Random01()),
                .colorStart = (Color){255, 255, 255, 240},
                .colorEnd = (Color){255, 255, 255, 0},
                .render.texture = s_emberTrailTex, .render.blendMode = VFX_BLEND_ADDITIVE,
                .render.unlit = 1,
            });
        }
    }
}
static void VC_EmberTrail_Draw3D(Camera3D cam) { (void)cam; }
