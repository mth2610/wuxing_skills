// game/horde_renderer.c
#include "game/horde_renderer.h"
#include "ai/ai.h"
#include "entities/entities.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define HORDE_MAX_INSTANCES 256

static const char *HORDE_VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in mat4 instanceTransform;\n"
    "uniform mat4 mvp;\n"
    "out vec3 fragNormal;\n"
    "out vec3 fragLocalPos;\n"
    "void main() {\n"
    "    mat3 normMat = mat3(instanceTransform);\n"
    "    fragNormal = normalize(normMat * vertexNormal);\n"
    "    fragLocalPos = vertexPosition;\n"
    "    gl_Position = mvp * instanceTransform * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *HORDE_FS =
    "#version 330\n"
    "in vec3 fragNormal;\n"
    "in vec3 fragLocalPos;\n"
    "uniform vec4 u_tint;\n"
    "uniform vec4 u_emissive;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 n = normalize(fragNormal);\n"
    "    vec3 lightDir = normalize(vec3(0.45, 0.82, 0.35));\n"
    "    float ndotl = dot(n, lightDir) * 0.5 + 0.5;\n"
    "    float heightGrad = clamp(fragLocalPos.y * 0.65 + 0.35, 0.35, 1.15);\n"
    "    vec3 col = u_tint.rgb * (0.42 + 0.68 * ndotl) * heightGrad + u_emissive.rgb;\n"
    "    finalColor = vec4(col, u_tint.a);\n"
    "}\n";

static bool     s_ready = false;
static Shader   s_shader = { 0 };
static Material s_mat = { 0 };
static int      s_locTint = -1;
static int      s_locEmissive = -1;

// 4 monster archetype meshes + shadow disc + bolt diamond
static Mesh s_meshBiter = { 0 };
static Mesh s_meshSpitter = { 0 };
static Mesh s_meshAlpha = { 0 };
static Mesh s_meshLeaper = { 0 };
static Mesh s_meshShadow = { 0 };
static Mesh s_meshBolt = { 0 };

// Build a clean low-poly faceted crystal/horn silhouette from a base prism + crest
static Mesh BuildLowPolySilhouette(float rx, float ry, float rz, float crestH, int sides) {
    // Bottom ring + mid ring + top apex + bottom apex -> closed faceted hull
    int triCount = sides * 4;
    int vertCount = triCount * 3;
    Mesh m = { 0 };
    m.vertexCount = vertCount;
    m.triangleCount = triCount;
    m.vertices = (float *)MemAlloc((unsigned int)vertCount * 3u * sizeof(float));
    m.normals  = (float *)MemAlloc((unsigned int)vertCount * 3u * sizeof(float));
    m.texcoords = (float *)MemAlloc((unsigned int)vertCount * 2u * sizeof(float));
    if (!m.vertices || !m.normals || !m.texcoords) {
        MemFree(m.vertices); MemFree(m.normals); MemFree(m.texcoords);
        return (Mesh){ 0 };
    }

    Vector3 bot = { 0.0f, 0.02f, 0.0f };
    Vector3 top = { 0.0f, ry + crestH, 0.12f * rz };
    int v = 0;

    for (int i = 0; i < sides; i++) {
        float a0 = ((float)i / (float)sides) * 6.2831853f;
        float a1 = ((float)(i + 1) / (float)sides) * 6.2831853f;
        Vector3 l0 = { cosf(a0) * rx * 0.72f, ry * 0.25f, sinf(a0) * rz * 0.72f };
        Vector3 l1 = { cosf(a1) * rx * 0.72f, ry * 0.25f, sinf(a1) * rz * 0.72f };
        Vector3 u0 = { cosf(a0) * rx, ry * 0.72f, sinf(a0) * rz + 0.08f * rz };
        Vector3 u1 = { cosf(a1) * rx, ry * 0.72f, sinf(a1) * rz + 0.08f * rz };

        Vector3 tris[4][3] = {
            { bot, l1, l0 },
            { l0, l1, u1 },
            { l0, u1, u0 },
            { u0, u1, top }
        };

        for (int t = 0; t < 4; t++) {
            Vector3 p0 = tris[t][0], p1 = tris[t][1], p2 = tris[t][2];
            Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p0)));
            Vector3 pts[3] = { p0, p1, p2 };
            for (int k = 0; k < 3; k++) {
                m.vertices[v * 3 + 0] = pts[k].x;
                m.vertices[v * 3 + 1] = pts[k].y;
                m.vertices[v * 3 + 2] = pts[k].z;
                m.normals[v * 3 + 0] = n.x;
                m.normals[v * 3 + 1] = n.y;
                m.normals[v * 3 + 2] = n.z;
                m.texcoords[v * 2 + 0] = 0.5f;
                m.texcoords[v * 2 + 1] = 0.5f;
                v++;
            }
        }
    }

    UploadMesh(&m, false);
    return m;
}

static Mesh BuildShadowDiscMesh(int sides) {
    Mesh m = { 0 };
    m.vertexCount = sides * 3;
    m.triangleCount = sides;
    m.vertices  = (float *)MemAlloc((unsigned int)m.vertexCount * 3u * sizeof(float));
    m.normals   = (float *)MemAlloc((unsigned int)m.vertexCount * 3u * sizeof(float));
    m.texcoords = (float *)MemAlloc((unsigned int)m.vertexCount * 2u * sizeof(float));
    if (!m.vertices || !m.normals || !m.texcoords) {
        MemFree(m.vertices); MemFree(m.normals); MemFree(m.texcoords);
        return (Mesh){ 0 };
    }

    int v = 0;
    for (int i = 0; i < sides; i++) {
        float a0 = ((float)i / (float)sides) * 6.2831853f;
        float a1 = ((float)(i + 1) / (float)sides) * 6.2831853f;
        Vector3 pts[3] = {
            { 0.0f, 0.0f, 0.0f },
            { cosf(a1) * 0.45f, 0.0f, sinf(a1) * 0.45f },
            { cosf(a0) * 0.45f, 0.0f, sinf(a0) * 0.45f }
        };
        for (int k = 0; k < 3; k++) {
            m.vertices[v * 3 + 0] = pts[k].x;
            m.vertices[v * 3 + 1] = pts[k].y;
            m.vertices[v * 3 + 2] = pts[k].z;
            m.normals[v * 3 + 0] = 0.0f;
            m.normals[v * 3 + 1] = 1.0f;
            m.normals[v * 3 + 2] = 0.0f;
            m.texcoords[v * 2 + 0] = 0.5f;
            m.texcoords[v * 2 + 1] = 0.5f;
            v++;
        }
    }
    UploadMesh(&m, false);
    return m;
}

static Color ElementTint(int elem) {
    switch (elem) {
        case 0:  return (Color){ 80, 185, 255, 255 };  // Thủy
        case 1:  return (Color){ 95, 215, 115, 255 };  // Mộc
        case 2:  return (Color){ 255, 110, 65, 255 };  // Hỏa
        case 3:  return (Color){ 220, 170, 90, 255 };  // Thổ
        case 4:  return (Color){ 215, 225, 240, 255 }; // Kim
        default: return (Color){ 200, 180, 245, 255 };
    }
}

static void DrawInstancedBatch(Mesh mesh, const Matrix *xf, int count, Vector4 tint, Vector4 emissive) {
    if (count <= 0 || mesh.vaoId == 0) return;
    BeginShaderMode(s_shader);
    SetShaderValue(s_shader, s_locTint, &tint, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_shader, s_locEmissive, &emissive, SHADER_UNIFORM_VEC4);
    DrawMeshInstanced(mesh, s_mat, xf, count);
    EndShaderMode();
}

void HordeRenderer_Init(void) {
    if (s_ready) return;
    s_shader = LoadShaderFromMemory(HORDE_VS, HORDE_FS);
    s_shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(s_shader, "instanceTransform");
    s_locTint     = GetShaderLocation(s_shader, "u_tint");
    s_locEmissive = GetShaderLocation(s_shader, "u_emissive");
    s_mat = LoadMaterialDefault();
    s_mat.shader = s_shader;

    // 4 distinct lightweight low-poly silhouettes
    s_meshBiter   = BuildLowPolySilhouette(0.28f, 0.55f, 0.34f, 0.16f, 6); // 24 tris: horned biter
    s_meshSpitter = BuildLowPolySilhouette(0.24f, 0.72f, 0.24f, 0.28f, 5); // 20 tris: floating spire
    s_meshAlpha   = BuildLowPolySilhouette(0.42f, 0.92f, 0.38f, 0.25f, 8); // 32 tris: hulking brute
    s_meshLeaper  = BuildLowPolySilhouette(0.24f, 0.44f, 0.48f, 0.14f, 6); // 24 tris: elongated pouncer
    s_meshShadow  = BuildShadowDiscMesh(8);                                 // 8 tris: shadow disc
    s_meshBolt    = BuildLowPolySilhouette(0.14f, 0.14f, 0.36f, 0.04f, 4); // 16 tris: dart bolt

    s_ready = true;
}

void HordeRenderer_Unload(void) {
    if (!s_ready) return;
    UnloadMesh(s_meshBiter);
    UnloadMesh(s_meshSpitter);
    UnloadMesh(s_meshAlpha);
    UnloadMesh(s_meshLeaper);
    UnloadMesh(s_meshShadow);
    UnloadMesh(s_meshBolt);
    UnloadShader(s_shader);
    s_ready = false;
}

void HordeRenderer_Draw3D(Camera3D cam) {
    if (!s_ready) HordeRenderer_Init();

    static AIMonsterRenderInfo monsters[AI_MAX_CAMP_MONSTERS];
    int mCount = AI_QueryMonsterRenderData(monsters, AI_MAX_CAMP_MONSTERS);

    // Also include boss-wave non-camp ARCH_MINIONs
    static AIMonsterRenderInfo waveMinions[64];
    int wCount = 0;
    for (int i = 0; i < MAX_AGENTS && wCount < 64; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (!a || a->archetype != ARCH_MINION || AI_IsCampMonster(i)) continue;
        waveMinions[wCount++] = (AIMonsterRenderInfo){
            .agentId       = i,
            .pos           = a->position,
            .yaw           = 0.0f,
            .scale         = 0.85f,
            .animPhase     = (float)GetTime() * 4.5f + (float)i,
            .telegraphFrac = 0.0f,
            .hitFlash      = (a->hitFlashTimer > 0.0f) ? (a->hitFlashTimer / 0.12f) : 0.0f,
            .element       = a->currentElement,
            .kind          = MONSTER_RANGED_CASTER,
            .state         = MON_STATE_CHASE
        };
    }

    static Matrix xfKind[4][HORDE_MAX_INSTANCES];
    static int    cntKind[4];
    static Matrix xfFlash[HORDE_MAX_INSTANCES];
    static int    cntFlash = 0;
    static Matrix xfTelegraph[HORDE_MAX_INSTANCES];
    static int    cntTelegraph = 0;
    static Matrix xfShadow[HORDE_MAX_INSTANCES];
    static int    cntShadow = 0;

    for (int k = 0; k < 4; k++) cntKind[k] = 0;
    cntFlash = 0;
    cntTelegraph = 0;
    cntShadow = 0;

    int domElem = 3;
    const float maxDrawDistSq = 72.0f * 72.0f;

    // Pack monster transforms (distance culled)
    for (int pass = 0; pass < 2; pass++) {
        const AIMonsterRenderInfo *arr = (pass == 0) ? monsters : waveMinions;
        int n = (pass == 0) ? mCount : wCount;
        for (int i = 0; i < n; i++) {
            const AIMonsterRenderInfo *m = &arr[i];
            float dx = m->pos.x - cam.position.x;
            float dz = m->pos.z - cam.position.z;
            if (dx * dx + dz * dz > maxDrawDistSq) continue;

            domElem = m->element;
            float hop = fabsf(sinf(m->animPhase)) * 0.14f;
            float sy = 1.0f + 0.12f * sinf(m->animPhase * 2.0f);
            float sxz = 1.0f / sqrtf(sy);
            float pullback = 0.0f;

            if (m->kind == MONSTER_RANGED_CASTER) {
                hop += 0.22f + 0.06f * sinf(m->animPhase * 0.7f);
            }
            if (m->state == MON_STATE_TELEGRAPH) {
                sxz *= (1.0f + 0.22f * m->telegraphFrac);
                sy  *= (1.0f - 0.22f * m->telegraphFrac);
                pullback = -0.18f * m->telegraphFrac;
            } else if (m->state == MON_STATE_STRIKE) {
                sxz *= 0.86f;
                sy  *= 1.18f;
            }

            float sc = m->scale;
            Matrix S = MatrixScale(sc * sxz, sc * sy, sc * sxz);
            Matrix R = MatrixRotateY(m->yaw);
            Vector3 drawPos = {
                m->pos.x + sinf(m->yaw) * pullback,
                m->pos.y + hop,
                m->pos.z + cosf(m->yaw) * pullback
            };
            Matrix T = MatrixTranslate(drawPos.x, drawPos.y, drawPos.z);
            Matrix M = MatrixMultiply(MatrixMultiply(S, R), T);

            if (m->hitFlash > 0.15f && cntFlash < HORDE_MAX_INSTANCES) {
                xfFlash[cntFlash++] = M;
            } else if (m->telegraphFrac > 0.20f && cntTelegraph < HORDE_MAX_INSTANCES) {
                xfTelegraph[cntTelegraph++] = M;
            } else {
                int k = (int)m->kind;
                if (k < 0 || k > 3) k = 0;
                if (cntKind[k] < HORDE_MAX_INSTANCES) {
                    xfKind[k][cntKind[k]++] = M;
                }
            }

            if (cntShadow < HORDE_MAX_INSTANCES) {
                Matrix SS = MatrixScale(sc * 0.95f, 1.0f, sc * 0.95f);
                Matrix TS = MatrixTranslate(m->pos.x, m->pos.y + 0.04f, m->pos.z);
                xfShadow[cntShadow++] = MatrixMultiply(SS, TS);
            }
        }
    }

    // 1. Batched ground shadow discs (1 draw call for all 200 monsters!)
    DrawInstancedBatch(s_meshShadow, xfShadow, cntShadow,
                       (Vector4){ 0.05f, 0.04f, 0.03f, 0.55f },
                       (Vector4){ 0.0f, 0.0f, 0.0f, 0.0f });

    // 2. 4 Archetype silhouette batches
    Color ec = ElementTint(domElem);
    Vector4 baseTint = { (float)ec.r / 255.0f, (float)ec.g / 255.0f, (float)ec.b / 255.0f, 1.0f };
    Mesh meshes[4] = { s_meshBiter, s_meshSpitter, s_meshAlpha, s_meshLeaper };
    Vector4 kindTints[4] = {
        { baseTint.x * 0.85f, baseTint.y * 0.72f, baseTint.z * 0.65f, 1.0f },
        { baseTint.x * 0.75f, baseTint.y * 0.95f, baseTint.z * 1.05f, 1.0f },
        { baseTint.x * 0.55f + 0.25f, baseTint.y * 0.35f, baseTint.z * 0.30f, 1.0f },
        { baseTint.x * 0.95f, baseTint.y * 0.88f, baseTint.z * 0.55f, 1.0f }
    };
    for (int k = 0; k < 4; k++) {
        DrawInstancedBatch(meshes[k], xfKind[k], cntKind[k], kindTints[k],
                           (Vector4){ 0.02f, 0.02f, 0.02f, 0.0f });
    }

    // 3. Telegraph warning batch (crimson glow so attack tells read clearly)
    DrawInstancedBatch(s_meshBiter, xfTelegraph, cntTelegraph,
                       (Vector4){ 1.0f, 0.28f, 0.18f, 1.0f },
                       (Vector4){ 0.45f, 0.08f, 0.04f, 0.0f });

    // 4. Hit-flash white crunch batch (1 draw call)
    DrawInstancedBatch(s_meshBiter, xfFlash, cntFlash,
                       (Vector4){ 1.0f, 1.0f, 1.0f, 1.0f },
                       (Vector4){ 0.75f, 0.75f, 0.75f, 0.0f });

    // 5. Batched Minion Bolts (1 draw call)
    static AIMinionBoltRenderInfo bolts[AI_MAX_MINION_BOLTS];
    static Matrix xfBolts[AI_MAX_MINION_BOLTS];
    int bCount = AI_QueryMinionBolts(bolts, AI_MAX_MINION_BOLTS);
    if (bCount > 0) {
        int bElem = bolts[0].element;
        for (int i = 0; i < bCount; i++) {
            float yaw = atan2f(bolts[i].vel.x, bolts[i].vel.z);
            Matrix S = MatrixScale(1.0f, 1.0f, 1.4f);
            Matrix R = MatrixRotateY(yaw);
            Matrix T = MatrixTranslate(bolts[i].pos.x, bolts[i].pos.y, bolts[i].pos.z);
            xfBolts[i] = MatrixMultiply(MatrixMultiply(S, R), T);
        }
        Color bc = ElementTint(bElem);
        DrawInstancedBatch(s_meshBolt, xfBolts, bCount,
                           (Vector4){ (float)bc.r / 255.0f, (float)bc.g / 255.0f, (float)bc.b / 255.0f, 1.0f },
                           (Vector4){ (float)bc.r / 510.0f, (float)bc.g / 510.0f, (float)bc.b / 510.0f, 0.0f });
    }
}
