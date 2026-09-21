// Generic mesh-surface particle emitter. Targets are only Raylib Mesh/Model
// data plus a caller-owned world transform; this file deliberately knows no
// character, agent, pickup, or gameplay state.

#define VFX_MESH_EMITTER_MAX 8
#define VFX_MESH_EMITTER_SPAWN_CAP 8
#define VFX_MESH_EMITTER_LIVE_MAX 12

typedef struct {
    bool active;
    const Mesh *mesh;
    const Model *model;
    Matrix transform;
    VFX_MeshParticleVariant variant;
    VC_MaterialId material;
    float intensity;
    float spawnCarry;
    unsigned int rng;
} VC_MeshParticleEmitter;

static VC_MeshParticleEmitter s_meshParticleEmitters[VFX_MESH_EMITTER_MAX];
static Texture2D s_meshEmitterBody[VFX_MESH_PARTICLE_VARIANT_COUNT];
static Texture2D s_meshEmitterNormal[VFX_MESH_PARTICLE_VARIANT_COUNT];
static SpriteAnim s_meshEmitterAnim[VFX_MESH_PARTICLE_VARIANT_COUNT];
static bool s_meshEmitterReady[VFX_MESH_PARTICLE_VARIANT_COUNT];
static Texture2D s_meshEmitterFireRamp = {0};

static const ParticleDynamicsProfile s_meshEmitterRiseDynamics = {
    .inverseMassKg = 1.0f,
    .gravityScale = 0.0f,
    .linearDragPerSecond = 1.8f,
    .terminalSpeedMps = 2.4f,
    .windCouplingHz = 0.8f,
    .windSusceptibility = 0.20f,
};

static unsigned int MeshEmitter_Next(unsigned int *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static float MeshEmitter_Random01(unsigned int *state)
{
    return (float)(MeshEmitter_Next(state) >> 8) * (1.0f / 16777216.0f);
}

static bool MeshEmitter_Sample(const Mesh *mesh, Matrix transform, unsigned int *rng,
                               Vector3 *outPos, Vector3 *outNormal)
{
    const float *vertices;
    const float *normals;
    Vector3 p, n = {0.0f, 1.0f, 0.0f};
    if (!mesh || mesh->vertexCount <= 0) return false;
    vertices = mesh->animVertices ? mesh->animVertices : mesh->vertices;
    normals = mesh->animNormals ? mesh->animNormals : mesh->normals;
    if (!vertices) return false;
    if (mesh->indices && mesh->triangleCount > 0) {
        int tri = (int)(MeshEmitter_Next(rng) % (unsigned int)mesh->triangleCount);
        int i0 = mesh->indices[tri*3], i1 = mesh->indices[tri*3 + 1], i2 = mesh->indices[tri*3 + 2];
        float u = MeshEmitter_Random01(rng), v = MeshEmitter_Random01(rng);
        float a, b, c;
        if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
        a = 1.0f - u - v; b = u; c = v;
        p = (Vector3){a*vertices[i0*3] + b*vertices[i1*3] + c*vertices[i2*3],
                      a*vertices[i0*3+1] + b*vertices[i1*3+1] + c*vertices[i2*3+1],
                      a*vertices[i0*3+2] + b*vertices[i1*3+2] + c*vertices[i2*3+2]};
        if (normals) n = Vector3Normalize((Vector3){a*normals[i0*3] + b*normals[i1*3] + c*normals[i2*3],
                                                     a*normals[i0*3+1] + b*normals[i1*3+1] + c*normals[i2*3+1],
                                                     a*normals[i0*3+2] + b*normals[i1*3+2] + c*normals[i2*3+2]});
    } else {
        int i = (int)(MeshEmitter_Next(rng) % (unsigned int)mesh->vertexCount);
        p = (Vector3){vertices[i*3], vertices[i*3+1], vertices[i*3+2]};
        if (normals) n = Vector3Normalize((Vector3){normals[i*3], normals[i*3+1], normals[i*3+2]});
    }
    *outPos = Vector3Transform(p, transform);
    *outNormal = Vector3Normalize((Vector3){transform.m0*n.x + transform.m4*n.y + transform.m8*n.z,
                                              transform.m1*n.x + transform.m5*n.y + transform.m9*n.z,
                                              transform.m2*n.x + transform.m6*n.y + transform.m10*n.z});
    return true;
}

static VFX_SurfaceId MeshEmitter_Surface(VFX_MeshParticleVariant variant)
{
    switch (variant) {
    case VFX_MESH_PARTICLE_VARIANT_SMOKE_LIGHT_RISE: return VFX_SURFACE_SMOKE_PUFF_LIGHT_NIAGARA;
    case VFX_MESH_PARTICLE_VARIANT_SMOKE_DARK_RISE: return VFX_SURFACE_SMOKE_PUFF_DARK_NIAGARA;
    case VFX_MESH_PARTICLE_VARIANT_FIRE_ROIL: return VFX_SURFACE_FIRE_ROIL_NIAGARA;
    default: return VFX_SURFACE_PLASMA_WISPS_NIAGARA;
    }
}

static void MeshEmitter_EnsureVariant(VFX_MeshParticleVariant variant)
{
    const VFX_SurfaceProfile *profile;
    if (variant < 0 || variant >= VFX_MESH_PARTICLE_VARIANT_COUNT || s_meshEmitterReady[variant]) return;
    profile = VFX_SurfaceRegistry_Get(MeshEmitter_Surface(variant));
    if (profile && profile->body.id) {
        s_meshEmitterBody[variant] = profile->body;
        s_meshEmitterNormal[variant] = profile->normalMap;
        SpriteAnim_Init(&s_meshEmitterAnim[variant], profile->flipbookColumns, profile->flipbookRows,
                        profile->flipbookFrames, (float)profile->flipbookFrames / 1.35f, ANIM_ONCE);
    }
    if (variant == VFX_MESH_PARTICLE_VARIANT_FIRE_ROIL && s_meshEmitterFireRamp.id == 0) {
        ColorGradient gradient = {0};
        ColorGradient_AddStop(&gradient, 0.0f, (Color){80, 10, 2, 255});
        ColorGradient_AddStop(&gradient, 0.55f, (Color){255, 92, 8, 255});
        ColorGradient_AddStop(&gradient, 1.0f, (Color){255, 244, 190, 255});
        s_meshEmitterFireRamp = ColorGradient_BakeLUT(&gradient, 64);
    }
    s_meshEmitterReady[variant] = true;
}

static float MeshEmitter_Rate(VFX_MeshParticleVariant variant)
{
    // Rate × bounded lifetime stays below VFX_MESH_EMITTER_LIVE_MAX per emitter;
    // the per-update cap also prevents a frame hitch from bypassing that budget.
    return variant == VFX_MESH_PARTICLE_VARIANT_PLASMA_WISP_STATIC ? 36.0f : 16.0f;
}

static void MeshEmitter_SpawnOne(VC_MeshParticleEmitter *emitter)
{
    const Mesh *mesh = emitter->mesh;
    Vector3 pos, normal, velocity = {0};
    ParticleConfig p = {0};
    VFX_MeshParticleVariant variant = emitter->variant;
    if (!mesh && emitter->model && emitter->model->meshCount > 0)
        mesh = &emitter->model->meshes[MeshEmitter_Next(&emitter->rng) % (unsigned int)emitter->model->meshCount];
    if (!MeshEmitter_Sample(mesh, emitter->transform, &emitter->rng, &pos, &normal)) return;
    MeshEmitter_EnsureVariant(variant);
    if (s_meshEmitterBody[variant].id == 0) return;
    if (variant == VFX_MESH_PARTICLE_VARIANT_PLASMA_WISP_RISE)
        velocity = Vector3Add(Vector3Scale(normal, 0.20f + 0.20f*MeshEmitter_Random01(&emitter->rng)), (Vector3){0, 0.45f, 0});
    else if (variant == VFX_MESH_PARTICLE_VARIANT_SMOKE_LIGHT_RISE || variant == VFX_MESH_PARTICLE_VARIANT_SMOKE_DARK_RISE || variant == VFX_MESH_PARTICLE_VARIANT_FIRE_ROIL)
        velocity = Vector3Add(Vector3Scale(normal, 0.10f), (Vector3){0, 0.55f, 0});
    else if (variant == VFX_MESH_PARTICLE_VARIANT_EMBER_SPARK_LIFT)
        velocity = Vector3Add(Vector3Scale(normal, 0.35f), (Vector3){0, 1.15f, 0});
    p.position = pos; p.velocity = velocity;
    p.radius = 0.090f + 0.13f*MeshEmitter_Random01(&emitter->rng);
    // Static wisps are intentionally short-lived: each new sample follows a
    // moving target transform while never acquiring a physical rise velocity.
    p.lifetime = variant == VFX_MESH_PARTICLE_VARIANT_PLASMA_WISP_STATIC ? 0.33f : 0.75f;
    p.colorStart = VC_WithAlpha(VFX_Material(emitter->material)->glow, (unsigned char)(240.0f*emitter->intensity));
    if (variant == VFX_MESH_PARTICLE_VARIANT_SMOKE_LIGHT_RISE)
        p.colorStart = (Color){220, 220, 220, (unsigned char)(205.0f*emitter->intensity)};
    else if (variant == VFX_MESH_PARTICLE_VARIANT_SMOKE_DARK_RISE)
        p.colorStart = (Color){58, 58, 58, (unsigned char)(225.0f*emitter->intensity)};
    else if (variant == VFX_MESH_PARTICLE_VARIANT_FIRE_ROIL)
        p.colorStart = (Color){255, 255, 255, (unsigned char)(230.0f*emitter->intensity)};
    p.colorEnd = VC_WithAlpha(p.colorStart, 0);
    p.render.texture = s_meshEmitterBody[variant];
    p.spriteAnim = &s_meshEmitterAnim[variant]; p.spriteAnimPhase = MeshEmitter_Random01(&emitter->rng); p.spriteAnimRate = 0.85f + 0.25f*MeshEmitter_Random01(&emitter->rng);
    p.rotation = MeshEmitter_Random01(&emitter->rng)*2.0f*PI;
    if (variant == VFX_MESH_PARTICLE_VARIANT_FIRE_ROIL) {
        p.render.normalTex = s_meshEmitterNormal[variant]; p.render.volumeSheet = 4;
        p.render.rampLUT = s_meshEmitterFireRamp; p.render.heatGain = 1.25f;
        p.render.emissiveBoost = 1.15f; p.render.blendMode = VFX_BLEND_PREMULTIPLIED;
    } else if (variant == VFX_MESH_PARTICLE_VARIANT_SMOKE_LIGHT_RISE || variant == VFX_MESH_PARTICLE_VARIANT_SMOKE_DARK_RISE) {
        p.render.normalTex = s_meshEmitterNormal[variant]; p.render.volumeSheet = 3;
        p.render.blendMode = VFX_BLEND_ALPHA;
    } else if (variant == VFX_MESH_PARTICLE_VARIANT_EMBER_SPARK_LIFT) {
        p.render.blendMode = VFX_BLEND_ADDITIVE; p.render.unlit = 1; p.render.emissiveBoost = 1.4f;
    } else { p.render.blendMode = VFX_BLEND_ADDITIVE; p.render.unlit = 1; p.render.emissiveBoost = 1.2f; }
    if (variant != VFX_MESH_PARTICLE_VARIANT_PLASMA_WISP_STATIC) {
        p.physics.dynamics = &s_meshEmitterRiseDynamics;
        p.physics.initialAccelerationMps2 = (Vector3){0, 0.35f, 0};
    }
    SpawnParticle(p);
}

int VFX_MeshParticleEmitter_Spawn(const VFX_MeshParticleEmitterDesc *desc)
{
    int slot = -1;
    if (!desc || (!desc->mesh && !desc->model) || desc->variant < 0 || desc->variant >= VFX_MESH_PARTICLE_VARIANT_COUNT) return -1;
    for (int i = 0; i < VFX_MESH_EMITTER_MAX; ++i) if (!s_meshParticleEmitters[i].active) { slot = i; break; }
    if (slot < 0) return -1;
    s_meshParticleEmitters[slot] = (VC_MeshParticleEmitter){.active=true, .mesh=desc->mesh, .model=desc->model,
        .transform=desc->transform, .variant=desc->variant, .material=desc->material,
        .intensity=Clamp(desc->intensity, 0.0f, 1.0f), .rng=desc->seed ? desc->seed : (unsigned int)(slot + 1)};
    return slot;
}
int VFX_ComposeMeshParticleEmitter(const VFX_MeshParticleEmitterDesc *desc) { return VFX_MeshParticleEmitter_Spawn(desc); }
void VFX_MeshParticleEmitter_SetTransform(int handle, Matrix transform) { if (handle >= 0 && handle < VFX_MESH_EMITTER_MAX && s_meshParticleEmitters[handle].active) s_meshParticleEmitters[handle].transform = transform; }
void VFX_MeshParticleEmitter_SetVariant(int handle, VFX_MeshParticleVariant variant) { if (handle >= 0 && handle < VFX_MESH_EMITTER_MAX && s_meshParticleEmitters[handle].active && variant >= 0 && variant < VFX_MESH_PARTICLE_VARIANT_COUNT && s_meshParticleEmitters[handle].variant != variant) { s_meshParticleEmitters[handle].variant = variant; s_meshParticleEmitters[handle].spawnCarry = 1.0f; } }
void VFX_MeshParticleEmitter_SetIntensity(int handle, float intensity01) { if (handle >= 0 && handle < VFX_MESH_EMITTER_MAX && s_meshParticleEmitters[handle].active) s_meshParticleEmitters[handle].intensity = Clamp(intensity01, 0.0f, 1.0f); }
void VFX_MeshParticleEmitter_Kill(int handle) { if (handle >= 0 && handle < VFX_MESH_EMITTER_MAX) s_meshParticleEmitters[handle].active = false; }
void VFX_KillMeshParticleEmitter(int handle) { VFX_MeshParticleEmitter_Kill(handle); }
const char *VFX_MeshParticleVariant_Name(VFX_MeshParticleVariant variant) { static const char *names[] = {"PLASMA WISP STATIC", "PLASMA WISP RISE", "LIGHT SMOKE RISE", "DARK SMOKE RISE", "FIRE ROIL", "EMBER SPARK LIFT"}; return (variant >= 0 && variant < VFX_MESH_PARTICLE_VARIANT_COUNT) ? names[variant] : "INVALID"; }
static void VC_MeshParticleEmitter_Update(float dt) { for (int i=0;i<VFX_MESH_EMITTER_MAX;++i) { VC_MeshParticleEmitter *e=&s_meshParticleEmitters[i]; if (!e->active) continue; e->spawnCarry += dt*MeshEmitter_Rate(e->variant)*e->intensity; int count=(int)e->spawnCarry; if(count>VFX_MESH_EMITTER_SPAWN_CAP) count=VFX_MESH_EMITTER_SPAWN_CAP; e->spawnCarry -= (float)count; while(count--) MeshEmitter_SpawnOne(e); } }
static void VC_MeshParticleEmitter_Draw3D(Camera3D cam) { (void)cam; }
