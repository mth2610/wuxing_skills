#define ARCH_MAX_CROWN_SPLASHES 8

typedef struct {
    bool active;
    Vector3 position;
    float maxRadius;
    float maxHeight;
    float duration;
    float elapsed;
    float rotationAngle;
    float randomScale;
    float randomPhase;
    VC_MaterialId matId;
} Arch_CrownSplash;

static Arch_CrownSplash s_archCrownSplashes[ARCH_MAX_CROWN_SPLASHES];

void VFX_ComposeCrownSplash(Vector3 pos, float radius, float height, float duration, VC_MaterialId matId)
{
    if (duration <= 0.0f)
        return;

    int slot = -1;
    for (int i = 0; i < ARCH_MAX_CROWN_SPLASHES; i++) {
        if (!s_archCrownSplashes[i].active) {
            slot = i;
            break;
        }
    }
    if (slot != -1) {
        s_archCrownSplashes[slot].active = true;
        s_archCrownSplashes[slot].position = pos;
        s_archCrownSplashes[slot].maxRadius = radius;
        s_archCrownSplashes[slot].maxHeight = height;
        s_archCrownSplashes[slot].duration = duration;
        s_archCrownSplashes[slot].elapsed = 0.0f;
        s_archCrownSplashes[slot].rotationAngle = (float)(rand() % 360);
        s_archCrownSplashes[slot].randomScale = 0.85f + ((rand() % 100) / 100.0f) * 0.3f;
        s_archCrownSplashes[slot].randomPhase = (rand() % 1000) / 100.0f;
        s_archCrownSplashes[slot].matId = matId;
    }

    const VFX_ElementMaterial *eMat = VFX_Material(matId);
    Color col = eMat ? eMat->body : ELEMENT_COLOR_WATER;

    ScreenDistort_Add(pos, radius * 1.3f, 0.35f, duration * 0.7f, 2.5f);
    VFX_ComposeShockwaveRing(pos, radius * 0.8f, duration * 0.9f, col);

    if (eMat && eMat->grad) {
        int count = 6 + (rand() % 3);
        for (int p = 0; p < count; p++) {
            float a = ((float)rand() / (float)RAND_MAX) * 2.0f * PI;
            float r = radius * 0.2f * ((float)rand() / (float)RAND_MAX);
            Vector3 partPos = { pos.x + r * cosf(a), pos.y + 0.05f, pos.z + r * sinf(a) };
            Vector3 vel = {
                cosf(a) * (0.2f + ((float)rand() / (float)RAND_MAX) * 0.5f),
                1.5f + ((float)rand() / (float)RAND_MAX) * 1.5f,
                sinf(a) * (0.2f + ((float)rand() / (float)RAND_MAX) * 0.5f)
            };
            SpawnParticle((ParticleConfig){
                .position = partPos,
                .velocity = vel,
                .radius = 0.04f + ((float)rand() / (float)RAND_MAX) * 0.04f,
                .lifetime = duration * (0.8f + ((float)rand() / (float)RAND_MAX) * 0.5f),
                .gradient = eMat->grad
            });
        }
    }
}

static void VC_CrownSplash_Update(float dt)
{
    for (int i = 0; i < ARCH_MAX_CROWN_SPLASHES; i++) {
        if (!s_archCrownSplashes[i].active) continue;
        s_archCrownSplashes[i].elapsed += dt;
        if (s_archCrownSplashes[i].elapsed >= s_archCrownSplashes[i].duration) {
            s_archCrownSplashes[i].active = false;
        }
    }
}

static void VC_CrownSplash_Draw3D(Camera3D cam)
{
    (void)cam;
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    for (int i = 0; i < ARCH_MAX_CROWN_SPLASHES; i++) {
        if (!s_archCrownSplashes[i].active) continue;

        float progress = s_archCrownSplashes[i].elapsed / s_archCrownSplashes[i].duration;

        const VFX_ElementMaterial *eMat = VFX_Material(s_archCrownSplashes[i].matId);
        if (!eMat) continue;

        EffectMaterialParams p = {0};
        p.baseColor = ColorAlpha(eMat->body, 1.0f);
        p.rimStrength = 3.5f;       
        p.fresnelPower = 2.0f;      
        p.emissiveIntensity = 0.2f; 
        p.translucency = 0.70f * sqrtf(1.0f - progress); 
        p.customParam1 = progress;
        p.customParam2 = s_archCrownSplashes[i].randomPhase;

        EffectMaterial mat = Material_LoadCustomShader(p, "core/shaders/water_splash.vs", "core/shaders/effect_material.fs");

        float maxR = s_archCrownSplashes[i].maxRadius;
        float baseScale = maxR * 0.04f * s_archCrownSplashes[i].randomScale;
        
        float scaleX = baseScale;
        float scaleY = baseScale;
        float scaleZ = baseScale;
        
        float t = progress;
        float easeY = 4.0f * t * (1.0f - t);
        float invT = 1.0f - t;
        float easeXZ = 1.0f - (invT * invT * invT); 
        
        easeXZ *= 1.5f; 
        easeXZ += 0.4f; 
        
        if (t > 0.8f) {
            float squash = (t - 0.8f) / 0.2f; 
            easeXZ += squash * 0.3f; 
        }
        
        scaleX *= easeXZ;
        scaleZ *= easeXZ;
        
        Model splashModel = ResourceManager_LoadModel("assets/models/water_splash.glb");
        if (splashModel.meshCount > 0) {
            Matrix matScale = MatrixScale(scaleX, scaleY, scaleZ);
            Matrix matRot = MatrixRotateY(s_archCrownSplashes[i].rotationAngle * DEG2RAD);
            Matrix matTrans = MatrixTranslate(s_archCrownSplashes[i].position.x, s_archCrownSplashes[i].position.y, s_archCrownSplashes[i].position.z);
            Matrix matModel = MatrixMultiply(matScale, matRot);
            matModel = MatrixMultiply(matModel, matTrans);
            
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
            rlEnableBackfaceCulling();
            
            Material_Begin(mat);
            
            Material originalMat = splashModel.materials[0];
            splashModel.materials[0].shader = mat.shader;
            splashModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = p.texture1;
            
            DrawMesh(splashModel.meshes[0], splashModel.materials[0], matModel);
            splashModel.materials[0] = originalMat;
            Material_End();
        }
    }
    
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();
}
