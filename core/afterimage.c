#include "core/afterimage.h"
#include "core/resource_manager.h"
#include "core/skill_helper.h"
#include "core/vfx_render.h"
#include "rlgl.h"
#include <math.h>

typedef struct {
    bool   active;
    Model  model;       // reference only — do not unload
    Matrix transform;
    Color  tint;
    float  life;
    float  maxLife;
} AfterimageSlot;

static AfterimageSlot s_pool[MAX_AFTERIMAGES];
static EffectMaterial s_mat;
static bool s_matLoaded = false;

void Afterimage_Init(void) {
    for (int i = 0; i < MAX_AFTERIMAGES; i++) s_pool[i].active = false;
    // Translucent, high-emissive, dissolve-ready material
    EffectMaterialParams p = {0};
    p.baseColor          = WHITE;
    p.rimStrength        = 0.3f;
    p.fresnelPower       = 2.0f;
    p.emissiveIntensity  = 0.8f;
    p.distortionStrength = 0.0f;
    p.translucency       = 0.9f;
    Material_LoadCustom(&s_mat, &p);
    s_matLoaded = true;
}

void Afterimage_Spawn(Model model, Matrix transform, Color tint, float life) {
    if (life <= 0.0f) return;
    for (int i = 0; i < MAX_AFTERIMAGES; i++) {
        if (!s_pool[i].active) {
            s_pool[i].active    = true;
            s_pool[i].model     = model;
            s_pool[i].transform = transform;
            s_pool[i].tint      = tint;
            s_pool[i].life      = life;
            s_pool[i].maxLife   = life;
            return;
        }
    }
}

void Afterimage_Update(float dt) {
    for (int i = 0; i < MAX_AFTERIMAGES; i++) {
        if (!s_pool[i].active) continue;
        s_pool[i].life -= dt;
        if (s_pool[i].life <= 0.0f) s_pool[i].active = false;
    }
}

void Afterimage_Draw(void) {
    if (!s_matLoaded) return;

    VFXRenderScope scope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);

    for (int i = 0; i < MAX_AFTERIMAGES; i++) {
        if (!s_pool[i].active) continue;
        float t = s_pool[i].life / s_pool[i].maxLife; // 1 = fresh, 0 = gone
        float dissolve = 1.0f - t;                    // ramp 0→1 over life
        // Tint alpha encodes opacity
        Color tint = s_pool[i].tint;
        tint.a = (unsigned char)(tint.a * t * 0.7f);

        Material_SetFloat(&s_mat, "u_dissolve", dissolve);
        for (int m = 0; m < s_pool[i].model.meshCount; m++) {
            Material mat = s_pool[i].model.materials[s_pool[i].model.meshMaterial[m]];
            if (s_mat.shader.id != 0) {
                mat.shader = s_mat.shader;
                mat.maps[MATERIAL_MAP_DIFFUSE].color = tint;
            }
            DrawMesh(s_pool[i].model.meshes[m], mat, s_pool[i].transform);
        }
    }

    VFXRender_EndDraw(&scope);
}

void Afterimage_GetStats(int *active, int *max) {
    int n = 0;
    for (int i = 0; i < MAX_AFTERIMAGES; i++)
        if (s_pool[i].active) n++;
    *active = n;
    *max = MAX_AFTERIMAGES;
}
