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
static Texture2D s_emberBodyTex = {0};
static Texture2D s_emberHaloTex = {0};

// Straight-alpha-safe radial mask. flare.png was authored for additive use:
// its dark RGB fringe is invisible under additive blending but becomes a grey
// outline when the same sheet is used by the bright-background alpha body.
static void EmberTrail_BuildTexture(void)
{
    const int size = 64;
    Image bodyImage = GenImageColor(size, size, BLANK);
    Image haloImage = GenImageColor(size, size, BLANK);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float u = (((float)x + 0.5f) / (float)size) * 2.0f - 1.0f;
            float v = (((float)y + 0.5f) / (float)size) * 2.0f - 1.0f;
            float r2 = u*u + v*v;
            // The body is deliberately tiny: it exists to replace the bright
            // destination at the centre, not to become an opaque orange disc.
            float body = fminf(fmaxf((0.42f - r2) * 3.0f, 0.0f), 1.0f);
            body = body * body * (3.0f - 2.0f * body);
            // The halo is a separate additive-only Gaussian shoulder. Its RGB
            // stays white so filtering cannot introduce a dark fringe.
            float halo = expf(-r2 * 3.2f) * fmaxf(0.0f, 1.0f - r2);
            unsigned char bodyAlpha = (unsigned char)(255.0f * body);
            unsigned char haloAlpha = (unsigned char)(255.0f * fminf(halo, 1.0f));
            ImageDrawPixel(&bodyImage, x, y, (Color){255, 255, 255, bodyAlpha});
            ImageDrawPixel(&haloImage, x, y, (Color){255, 255, 255, haloAlpha});
        }
    }
    s_emberBodyTex = LoadTextureFromImage(bodyImage);
    s_emberHaloTex = LoadTextureFromImage(haloImage);
    UnloadImage(bodyImage);
    UnloadImage(haloImage);
    if (s_emberBodyTex.id != 0)
        SetTextureFilter(s_emberBodyTex, TEXTURE_FILTER_BILINEAR);
    if (s_emberHaloTex.id != 0)
        SetTextureFilter(s_emberHaloTex, TEXTURE_FILTER_BILINEAR);
}

static void EmberTrail_Init(void)
{
    if (s_emberTrailInit) return;
    Tuning_RegisterFloat("ember_trail_rate", &s_emberTrailRate, 1.0f);
    EmberTrail_BuildTexture();
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
        const VFX_ElementMaterial *material = VFX_Material(e->mat);
        e->accum += dt * e->rate * s_emberTrailRate;
        int n = (int)e->accum; if (n > 5) n = 5;
        e->accum -= (float)n;
        for (int i = 0; i < n; ++i) {
            Vector3 jitter = {(Random01()-0.5f)*0.20f*e->scale,
                              (Random01()-0.5f)*0.20f*e->scale,
                              (Random01()-0.5f)*0.20f*e->scale};
            ParticleConfig body = (ParticleConfig){
                .position = Vector3Add(e->pos, jitter),
                .velocity = Vector3Add(Vector3Scale(e->velocity, 0.32f),
                            (Vector3){(Random01()-0.5f)*0.8f*e->scale,
                                      Math_Mix(0.45f, 1.25f, Random01())*e->scale,
                                      (Random01()-0.5f)*0.8f*e->scale}),
                .radius = Math_Mix(0.025f, 0.060f, Random01()) * e->scale * 0.30f,
                .lifetime = Math_Mix(0.40f, 0.80f, Random01()),
                .colorStart = VC_WithAlpha(VC_Whiten(material->glow, 0.08f), 255),
                .colorEnd = VC_WithAlpha(material->body, 0),
                .render.texture = s_emberBodyTex,
                .render.blendMode = VFX_BLEND_ALPHA,
                .render.contrastProfile = VFX_CONTRAST_FIRE,
                .render.unlit = 1,
            };

            // The compact alpha core both occludes the bright destination and
            // carries HDR values into bloom. Keeping it in BODY is what stops a
            // light map from diluting the centre into a pale additive smudge.
            ParticleConfig hotCore = body;
            hotCore.radius *= 0.52f;
            hotCore.lifetime *= 0.60f;
            hotCore.colorStart = VC_WithAlpha(VC_Whiten(material->glow, 0.92f), 255);
            hotCore.colorEnd = VC_WithAlpha(VC_Whiten(material->glow, 0.72f), 0);
            hotCore.render.emissiveBoost = 5.0f;

            // Bloom-only radiance is wider but lower coverage. PostFX expands
            // this shoulder around the already-readable body/hot-core pair.
            ParticleConfig halo = body;
            halo.radius *= 4.20f;
            halo.lifetime *= 0.75f;
            halo.colorStart = VC_WithAlpha(VC_Whiten(material->glow, 0.28f), 105);
            halo.colorEnd = VC_WithAlpha(material->glow, 0);
            halo.render.texture = s_emberHaloTex;
            halo.render.appearance = VFX_APPEARANCE_GLOW;

            SpawnParticle(body);
            SpawnParticle(hotCore);
            SpawnParticle(halo);
        }
    }
}
static void VC_EmberTrail_Draw3D(Camera3D cam) { (void)cam; }
