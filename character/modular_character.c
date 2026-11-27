// character/modular_character.c
#include "character/modular_character.h"
#include "core/resource_manager.h"
#include "core/surface_material.h"
#include "environment/wind/wind_system.h"
#include "raymath.h"
#include "rlgl.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static ModularItemDef s_itemRegistry[MODULAR_MAX_ITEMS];
static int s_itemCount = 0;
static bool s_systemInitialized = false;

#define MODULAR_MAX_UNIQUE_INSTANCES 16
typedef struct {
    const ModularInstance *owner;
    int    slotSourceIdx[MODULAR_SLOT_COUNT];
    Model  slotClones[MODULAR_SLOT_COUNT];
    bool   hasBaseBodyFallback;
    Model  baseBodyFallback;
} ModularInstanceVboCache;

static ModularInstanceVboCache s_vboCache[MODULAR_MAX_UNIQUE_INSTANCES];

// ============================================================================
// Universal Runtime Body Auto-Fit (Grooming + Clothing + Rigid Sockets)
// ============================================================================
// Automatically adapts ALL modular slots (HAIR, EYEBROWS, BEARD, TORSO, LEGS,
// BOOTS, BRACERS, FULL_OUTFIT, plus HEADGEAR/MASK/WAIST/BACK sockets) to ANY
// arbitrary full-body GLB once on equip with 0 ms per-frame cost.

#define BODY_REFIT_SLOTS 8
typedef struct {
    bool    valid;
    Vector3 headBone;    // model-space bind position of mixamorig:Head
    Vector3 center;      // head-bone-LOCAL center of skull
    Vector3 halfExtents; // head-bone-LOCAL half extents (upper cranium X/Z, chin..top Y)
    float   chinY;       // head-bone-LOCAL chin Y
    // Torso & lower-body girth bounds (for clothing & waist/back sockets)
    bool    bodyValid;
    float   torsoHalfX, torsoHalfZ, torsoCenterZ;
    float   legHalfX, legHalfZ, legCenterZ;
} HeadBoundsInfo;

typedef struct {
    const ModularInstance *owner;
    int   bodyIdx;
    int   itemIdx[BODY_REFIT_SLOTS];
    Model fitted[BODY_REFIT_SLOTS];
    bool  hasOwnedVerts[BODY_REFIT_SLOTS];
} GroomRefitEntry;

static GroomRefitEntry s_groomRefit[MODULAR_MAX_UNIQUE_INSTANCES];

static int GroomSlotToIdx(int slot)
{
    switch (slot)
    {
    case MODULAR_SLOT_HAIR:        return 0;
    case MODULAR_SLOT_EYEBROWS:    return 1;
    case MODULAR_SLOT_BEARD:       return 2;
    case MODULAR_SLOT_TORSO:       return 3;
    case MODULAR_SLOT_LEGS:        return 4;
    case MODULAR_SLOT_BOOTS:       return 5;
    case MODULAR_SLOT_BRACERS:     return 6;
    case MODULAR_SLOT_FULL_OUTFIT: return 7;
    default:                       return -1;
    }
}

static void GroomRefit_UnloadSlot(GroomRefitEntry *e, int gi)
{
    if (!e || gi < 0 || gi >= BODY_REFIT_SLOTS)
        return;
    if (e->fitted[gi].meshCount > 0)
    {
        if (e->hasOwnedVerts[gi] && e->fitted[gi].meshes)
        {
            for (int m = 0; m < e->fitted[gi].meshCount; m++)
            {
                free(e->fitted[gi].meshes[m].vertices);
                e->fitted[gi].meshes[m].vertices = NULL;
            }
        }
        CharacterModel_UnloadSkinnedClone(&e->fitted[gi]);
    }
    e->itemIdx[gi] = -1;
    e->hasOwnedVerts[gi] = false;
}

static void GroomRefit_ClearAll(void)
{
    for (int i = 0; i < MODULAR_MAX_UNIQUE_INSTANCES; i++)
    {
        for (int g = 0; g < BODY_REFIT_SLOTS; g++)
            GroomRefit_UnloadSlot(&s_groomRefit[i], g);
        s_groomRefit[i].owner = NULL;
        s_groomRefit[i].bodyIdx = -1;
    }
}

static Vector3 FindHeadBoneBindPos(Model m)
{
    Vector3 hp = { 0.0f, 1.66f, 0.0f };
    if (m.skeleton.bones && m.skeleton.bindPose)
    {
        for (int b = 0; b < m.skeleton.boneCount; b++)
        {
            if (strstr(m.skeleton.bones[b].name, "Head") != NULL)
            {
                hp = m.skeleton.bindPose[b].translation;
                break;
            }
        }
    }
    return hp;
}

static HeadBoundsInfo MeasureBodyHeadBounds(Model bodyModel)
{
    HeadBoundsInfo hb = {0};
    if (bodyModel.meshCount <= 0 || !bodyModel.meshes || !bodyModel.meshes[0].vertices)
        return hb;

    Vector3 hpos = FindHeadBoneBindPos(bodyModel);
    hb.headBone = hpos;

    Mesh *bm = &bodyModel.meshes[0];
    int vc = bm->vertexCount;
    const float *verts = bm->vertices;

    // Strict head-bone-local mask: |lx| <= 0.14m excludes T-pose shoulders/arms!
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    int count = 0;
    for (int i = 0; i < vc; i++)
    {
        float lx = verts[i * 3 + 0] - hpos.x;
        float ly = verts[i * 3 + 1] - hpos.y;
        float lz = verts[i * 3 + 2] - hpos.z;
        if (ly < -0.125f || fabsf(lx) > 0.14f)
            continue;
        if (lx < minX) minX = lx;
        if (lx > maxX) maxX = lx;
        if (ly < minY) minY = ly;
        if (ly > maxY) maxY = ly;
        if (lz < minZ) minZ = lz;
        if (lz > maxZ) maxZ = lz;
        count++;
    }
    if (count < 24 || (maxY - minY) < 0.10f)
        return hb;

    // Refine local chinY from front-center lower head vertices
    float chinY = maxY;
    float midZ = 0.5f * (minZ + maxZ);
    for (int i = 0; i < vc; i++)
    {
        float lx = verts[i * 3 + 0] - hpos.x;
        float ly = verts[i * 3 + 1] - hpos.y;
        float lz = verts[i * 3 + 2] - hpos.z;
        if (ly >= -0.115f && fabsf(lx) < 0.035f && lz > midZ + 0.03f && ly < chinY)
            chinY = ly;
    }
    if (chinY >= maxY - 0.05f)
        chinY = minY;

    // Measure upper-cranium width & depth (above chin + 50% head height: excludes shoulders & jaw)
    float uMinX = 1e9f, uMaxX = -1e9f, uMinZ = 1e9f, uMaxZ = -1e9f;
    float upperCut = chinY + 0.50f * (maxY - chinY);
    for (int i = 0; i < vc; i++)
    {
        float lx = verts[i * 3 + 0] - hpos.x;
        float ly = verts[i * 3 + 1] - hpos.y;
        float lz = verts[i * 3 + 2] - hpos.z;
        if (ly < upperCut || fabsf(lx) > 0.14f)
            continue;
        if (lx < uMinX) uMinX = lx;
        if (lx > uMaxX) uMaxX = lx;
        if (lz < uMinZ) uMinZ = lz;
        if (lz > uMaxZ) uMaxZ = lz;
    }
    if (uMaxX <= uMinX) { uMinX = minX; uMaxX = maxX; }
    if (uMaxZ <= uMinZ) { uMinZ = minZ; uMaxZ = maxZ; }

    hb.chinY = chinY;
    hb.center = (Vector3){ 0.5f * (uMinX + uMaxX), 0.5f * (chinY + maxY), 0.5f * (uMinZ + uMaxZ) };
    hb.halfExtents = (Vector3){
        fmaxf(0.05f, 0.5f * (uMaxX - uMinX)),
        fmaxf(0.08f, 0.5f * (maxY - chinY)),
        fmaxf(0.06f, 0.5f * (uMaxZ - uMinZ)),
    };
    hb.valid = true;

    // Measure ribcage/waist (Y in [1.00..1.32], |x|<=0.24) & legs (Y in [0.15..0.96], |x|<=0.24)
    float tMinX = 1e9f, tMaxX = -1e9f, tMinZ = 1e9f, tMaxZ = -1e9f;
    float lMinX = 1e9f, lMaxX = -1e9f, lMinZ = 1e9f, lMaxZ = -1e9f;
    int tCnt = 0, lCnt = 0;
    for (int i = 0; i < vc; i++)
    {
        float x = verts[i * 3 + 0];
        float y = verts[i * 3 + 1];
        float z = verts[i * 3 + 2];
        if (fabsf(x) > 0.24f)
            continue;
        if (y >= 1.00f && y <= 1.32f)
        {
            if (x < tMinX) tMinX = x;
            if (x > tMaxX) tMaxX = x;
            if (z < tMinZ) tMinZ = z;
            if (z > tMaxZ) tMaxZ = z;
            tCnt++;
        }
        else if (y >= 0.15f && y <= 0.96f)
        {
            if (x < lMinX) lMinX = x;
            if (x > lMaxX) lMaxX = x;
            if (z < lMinZ) lMinZ = z;
            if (z > lMaxZ) lMaxZ = z;
            lCnt++;
        }
    }
    if (tCnt >= 16 && lCnt >= 16)
    {
        hb.torsoHalfX = fmaxf(0.08f, 0.5f * (tMaxX - tMinX));
        hb.torsoHalfZ = fmaxf(0.06f, 0.5f * (tMaxZ - tMinZ));
        hb.torsoCenterZ = 0.5f * (tMinZ + tMaxZ);
        hb.legHalfX = fmaxf(0.08f, 0.5f * (lMaxX - lMinX));
        hb.legHalfZ = fmaxf(0.06f, 0.5f * (lMaxZ - lMinZ));
        hb.legCenterZ = 0.5f * (lMinZ + lMaxZ);
        hb.bodyValid = true;
    }
    return hb;
}

static Model ModularVboCache_GetSlotModel(const ModularInstance *inst, int slot, int itemIdx, Model srcModel);

static Model GroomRefit_GetModel(const ModularInstance *inst, int slot, int itemIdx,
                                 Model srcModel, int bodyIdx, Model bodyModel)
{
    int gi = GroomSlotToIdx(slot);
    if (gi < 0 || !inst || srcModel.meshCount <= 0 || bodyModel.meshCount <= 0)
        return ModularVboCache_GetSlotModel(inst, slot, itemIdx, srcModel);

    // Exact canonical male_body.glb reference measurements
    const Vector3 refCenter = { 0.0000f,  0.0107f, 0.0244f };
    const Vector3 refHalf   = { 0.0800f,  0.1210f, 0.1036f };
    const float   refChinY  = -0.1104f;
    const float   refTorsoHalfX = 0.2225f, refTorsoHalfZ = 0.1353f, refTorsoCZ =  0.0170f;
    const float   refLegHalfX   = 0.1940f, refLegHalfZ   = 0.1180f, refLegCZ   = -0.0102f;

    bool isHeadSlot = (slot == MODULAR_SLOT_HAIR || slot == MODULAR_SLOT_EYEBROWS || slot == MODULAR_SLOT_BEARD);

    HeadBoundsInfo tgt = MeasureBodyHeadBounds(bodyModel);
    if (isHeadSlot ? !tgt.valid : !tgt.bodyValid)
        return ModularVboCache_GetSlotModel(inst, slot, itemIdx, srcModel);

    // Fast path: target body already matches canonical male_body.glb within 5 mm -> untouched!
    if (isHeadSlot)
    {
        float dCenter = fabsf(tgt.center.x - refCenter.x) + fabsf(tgt.center.y - refCenter.y) + fabsf(tgt.center.z - refCenter.z);
        float dHalf   = fabsf(tgt.halfExtents.x - refHalf.x) + fabsf(tgt.halfExtents.y - refHalf.y) + fabsf(tgt.halfExtents.z - refHalf.z);
        if (dCenter < 0.005f && dHalf < 0.005f)
            return ModularVboCache_GetSlotModel(inst, slot, itemIdx, srcModel);
    }
    else
    {
        float dBody = fabsf(tgt.torsoHalfX - refTorsoHalfX) + fabsf(tgt.torsoHalfZ - refTorsoHalfZ)
                    + fabsf(tgt.torsoCenterZ - refTorsoCZ)  + fabsf(tgt.legHalfX - refLegHalfX)
                    + fabsf(tgt.legHalfZ - refLegHalfZ)     + fabsf(tgt.legCenterZ - refLegCZ);
        if (dBody < 0.008f)
            return ModularVboCache_GetSlotModel(inst, slot, itemIdx, srcModel);
    }

    // Locate or allocate per-instance refit entry
    int slotIdx = -1;
    for (int i = 0; i < MODULAR_MAX_UNIQUE_INSTANCES; i++)
    {
        if (s_groomRefit[i].owner == inst) { slotIdx = i; break; }
        if (s_groomRefit[i].owner == NULL && slotIdx < 0) slotIdx = i;
    }
    if (slotIdx < 0)
        return ModularVboCache_GetSlotModel(inst, slot, itemIdx, srcModel);

    GroomRefitEntry *e = &s_groomRefit[slotIdx];
    if (e->owner != inst || e->bodyIdx != bodyIdx)
    {
        for (int g = 0; g < BODY_REFIT_SLOTS; g++)
            GroomRefit_UnloadSlot(e, g);
        e->owner = inst;
        e->bodyIdx = bodyIdx;
    }

    if (e->itemIdx[gi] == itemIdx && e->fitted[gi].meshCount > 0)
        return e->fitted[gi];

    GroomRefit_UnloadSlot(e, gi);
    Model clone = CharacterModel_CloneSkinnedModel(srcModel);
    if (clone.meshCount <= 0 || !clone.meshes)
        return ModularVboCache_GetSlotModel(inst, slot, itemIdx, srcModel);

    // Body-clothing refit path (TORSO, LEGS, BOOTS, BRACERS, FULL_OUTFIT)
    // Uses gentle non-destructive alignment + ONE-WAY OUTWARD anti-penetration push-out
    // against the target body's actual mesh so shoulders/back/deltoids NEVER poke through!
    if (!isHeadSlot)
    {
        // Allow slimmer tailoring (min 0.80) for slender female bodies — safe because
        // one-way outward anti-penetration push-out right below prevents any poke-through!
        float tsx = Clamp(tgt.torsoHalfX / refTorsoHalfX, 0.80f, 1.24f);
        float tsz = Clamp(tgt.torsoHalfZ / refTorsoHalfZ, 0.84f, 1.24f);
        float tdz = Clamp((tgt.torsoCenterZ - refTorsoCZ) * 0.5f, -0.020f, 0.020f);
        float lsx = Clamp(tgt.legHalfX / refLegHalfX, 0.84f, 1.22f);
        float lsz = Clamp(tgt.legHalfZ / refLegHalfZ, 0.84f, 1.22f);
        float ldz = Clamp((tgt.legCenterZ - refLegCZ) * 0.5f, -0.020f, 0.020f);

        // Collect target body surface points in the relevant vertical band for outward push-out
        float bandYMin = (slot == MODULAR_SLOT_TORSO || slot == MODULAR_SLOT_BRACERS) ? 0.85f : 0.02f;
        float bandYMax = (slot == MODULAR_SLOT_LEGS || slot == MODULAR_SLOT_BOOTS) ? 1.05f : 1.56f;
        int bodySurfCnt = 0;
        Vector3 *bPos = NULL;
        Vector3 *bNrm = NULL;
        if (bodyModel.meshes[0].vertices && bodyModel.meshes[0].normals)
        {
            Mesh *bm = &bodyModel.meshes[0];
            int bvc = bm->vertexCount;
            // Subsample slightly if body mesh is very dense (>6000 verts) to keep equip < 3 ms
            int step = (bvc > 6000) ? (bvc / 4000) : 1;
            if (step < 1) step = 1;
            int cap = (bvc + step - 1) / step;
            bPos = (Vector3 *)malloc((size_t)cap * sizeof(Vector3));
            bNrm = (Vector3 *)malloc((size_t)cap * sizeof(Vector3));
            if (bPos && bNrm)
            {
                for (int i = 0; i < bvc; i += step)
                {
                    float by = bm->vertices[i * 3 + 1];
                    if (by < bandYMin || by > bandYMax)
                        continue;
                    bPos[bodySurfCnt] = (Vector3){ bm->vertices[i * 3 + 0], by, bm->vertices[i * 3 + 2] };
                    bNrm[bodySurfCnt] = Vector3Normalize((Vector3){
                        bm->normals[i * 3 + 0], bm->normals[i * 3 + 1], bm->normals[i * 3 + 2]
                    });
                    bodySurfCnt++;
                }
            }
        }

        const float minClr = (slot == MODULAR_SLOT_TORSO || slot == MODULAR_SLOT_FULL_OUTFIT) ? 0.012f : 0.008f;
        const float maxSearchD2 = 0.0016f; // 4.0 cm search radius

        bool allOwned = true;
        int pushedCount = 0;
        for (int m = 0; m < clone.meshCount; m++)
        {
            Mesh *mesh = &clone.meshes[m];
            int vc = mesh->vertexCount;
            if (vc <= 0 || !srcModel.meshes[m].vertices)
                continue;
            float *newV = (float *)malloc((size_t)vc * 3 * sizeof(float));
            if (!newV) { allOwned = false; continue; }
            const float *srcV = srcModel.meshes[m].vertices;
            for (int i = 0; i < vc; i++)
            {
                float vx = srcV[i * 3 + 0];
                float vy = srcV[i * 3 + 1];
                float vz = srcV[i * 3 + 2];

                if (slot == MODULAR_SLOT_TORSO || slot == MODULAR_SLOT_FULL_OUTFIT)
                {
                    float upW = Clamp((vy - 0.86f) / 0.14f, 0.0f, 1.0f);
                    float trunkW = 1.0f - Clamp((fabsf(vx) - 0.22f) / 0.14f, 0.0f, 1.0f);
                    float effSx = 1.0f + ((lsx * (1.0f - upW) + tsx * upW) - 1.0f) * trunkW;
                    float effSz = lsz * (1.0f - upW) + tsz * upW;
                    float refCz = refLegCZ * (1.0f - upW) + refTorsoCZ * upW;
                    float effDz = ldz * (1.0f - upW) + tdz * upW;
                    vx = vx * effSx;
                    vz = refCz + (vz - refCz) * effSz + effDz;
                }
                else if (slot == MODULAR_SLOT_LEGS || slot == MODULAR_SLOT_BOOTS)
                {
                    vx = vx * lsx;
                    vz = refLegCZ + (vz - refLegCZ) * lsz + ldz;
                }
                else if (slot == MODULAR_SLOT_BRACERS)
                {
                    vz = refTorsoCZ + (vz - refTorsoCZ) * tsz + tdz;
                }

                // One-way outward anti-penetration push-out against target body surface
                if (bodySurfCnt > 0 && vy >= bandYMin && vy <= bandYMax)
                {
                    int best = -1;
                    float bestD2 = maxSearchD2;
                    for (int b = 0; b < bodySurfCnt; b++)
                    {
                        float dy = vy - bPos[b].y;
                        if (dy * dy >= bestD2)
                            continue;
                        float dx = vx - bPos[b].x;
                        float dz = vz - bPos[b].z;
                        float d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 < bestD2) { bestD2 = d2; best = b; }
                    }
                    if (best >= 0)
                    {
                        Vector3 pn = bPos[best];
                        Vector3 nn = bNrm[best];
                        float signedD = (vx - pn.x) * nn.x + (vy - pn.y) * nn.y + (vz - pn.z) * nn.z;
                        if (signedD < minClr)
                        {
                            float push = minClr - signedD;
                            vx += nn.x * push;
                            vy += nn.y * push;
                            vz += nn.z * push;
                            pushedCount++;
                        }
                    }
                }

                newV[i * 3 + 0] = vx;
                newV[i * 3 + 1] = vy;
                newV[i * 3 + 2] = vz;
            }
            mesh->vertices = newV;
            if (mesh->animVertices)
                memcpy(mesh->animVertices, newV, (size_t)vc * 3 * sizeof(float));
            if (mesh->vboId && mesh->vboId[0] > 0)
                rlUpdateVertexBuffer(mesh->vboId[0], newV, vc * 3 * (int)sizeof(float), 0);
        }

        free(bPos);
        free(bNrm);

        e->fitted[gi] = clone;
        e->itemIdx[gi] = itemIdx;
        e->hasOwnedVerts[gi] = allOwned;
        TraceLog(LOG_INFO, "MODULAR: Auto-fitted clothing slot %d (item %d) to body %d (pushed %d verts out, minClr=%.1fmm)",
                 slot, itemIdx, bodyIdx, pushedCount, minClr * 1000.0f);
        return e->fitted[gi];
    }

    Vector3 groomHeadBone = FindHeadBoneBindPos(srcModel);

    float sx = Clamp(tgt.halfExtents.x / refHalf.x, 0.72f, 1.32f);
    float sy = Clamp(tgt.halfExtents.y / refHalf.y, 0.72f, 1.32f);
    float sz = Clamp(tgt.halfExtents.z / refHalf.z, 0.72f, 1.32f);
    float sUni = (sx + sy + sz) / 3.0f;

    // Collect target front-face vertices in head-bone-LOCAL space for EYEBROWS / BEARD shrink-wrap
    bool doShrinkWrap = (slot == MODULAR_SLOT_EYEBROWS || slot == MODULAR_SLOT_BEARD);
    int faceCount = 0;
    Vector3 *facePos = NULL;
    Vector3 *faceNrm = NULL;
    if (doShrinkWrap && bodyModel.meshes[0].vertices && bodyModel.meshes[0].normals)
    {
        Mesh *bm = &bodyModel.meshes[0];
        int bvc = bm->vertexCount;
        facePos = (Vector3 *)malloc((size_t)bvc * sizeof(Vector3));
        faceNrm = (Vector3 *)malloc((size_t)bvc * sizeof(Vector3));
        if (facePos && faceNrm)
        {
            for (int i = 0; i < bvc; i++)
            {
                float lx = bm->vertices[i * 3 + 0] - tgt.headBone.x;
                float ly = bm->vertices[i * 3 + 1] - tgt.headBone.y;
                float lz = bm->vertices[i * 3 + 2] - tgt.headBone.z;
                float nz = bm->normals[i * 3 + 2];
                if (fabsf(lx) <= 0.14f && ly >= tgt.chinY - 0.03f && lz >= tgt.center.z - 0.02f && nz > -0.20f)
                {
                    facePos[faceCount] = (Vector3){ lx, ly, lz };
                    faceNrm[faceCount] = Vector3Normalize((Vector3){ bm->normals[i * 3 + 0], bm->normals[i * 3 + 1], nz });
                    faceCount++;
                }
            }
        }
    }

    // Clone independent bind-pose vertices[] per mesh and apply Stage 1 (head-local warp) + Stage 2 (shrink-wrap)
    bool allOwned = true;
    for (int m = 0; m < clone.meshCount; m++)
    {
        Mesh *mesh = &clone.meshes[m];
        int vc = mesh->vertexCount;
        if (vc <= 0 || !srcModel.meshes[m].vertices)
            continue;
        float *newV = (float *)malloc((size_t)vc * 3 * sizeof(float));
        if (!newV)
        {
            allOwned = false;
            continue;
        }
        const float *srcV = srcModel.meshes[m].vertices;
        for (int i = 0; i < vc; i++)
        {
            // Express vertex in grooming head-bone-LOCAL coordinates
            float lx = srcV[i * 3 + 0] - groomHeadBone.x;
            float ly = srcV[i * 3 + 1] - groomHeadBone.y;
            float lz = srcV[i * 3 + 2] - groomHeadBone.z;

            // Stage 1: Proportions warp from canonical head-local box to target head-local box
            float wx, wy, wz;
            if (slot == MODULAR_SLOT_HAIR && ly < refChinY)
            {
                wx = tgt.center.x + (lx - refCenter.x) * sx;
                wy = tgt.chinY + (ly - refChinY) * sUni;
                wz = tgt.center.z + (lz - refCenter.z) * sz;
            }
            else
            {
                wx = tgt.center.x + (lx - refCenter.x) * sx;
                wy = tgt.center.y + (ly - refCenter.y) * sy;
                wz = tgt.center.z + (lz - refCenter.z) * sz;
            }

            // Stage 2: Conformal surface shrink-wrap in head-local space
            if (doShrinkWrap && faceCount > 0)
            {
                bool freeHangingBeard = (slot == MODULAR_SLOT_BEARD && wy < tgt.chinY - 0.008f);
                if (!freeHangingBeard)
                {
                    int best = -1;
                    float bestD2 = 1e9f;
                    for (int f = 0; f < faceCount; f++)
                    {
                        float dx = wx - facePos[f].x;
                        float dy = wy - facePos[f].y;
                        float dz = wz - facePos[f].z;
                        float d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 < bestD2) { bestD2 = d2; best = f; }
                    }
                    if (best >= 0 && bestD2 < 0.000625f) // within 2.5 cm of target face surface
                    {
                        Vector3 pn = facePos[best];
                        Vector3 nn = faceNrm[best];
                        float signedD = (wx - pn.x) * nn.x + (wy - pn.y) * nn.y + (wz - pn.z) * nn.z;
                        float minClr = (slot == MODULAR_SLOT_EYEBROWS) ? 0.0014f : 0.0008f;
                        float maxClr = (slot == MODULAR_SLOT_EYEBROWS) ? 0.0046f : 0.0055f;
                        if (signedD < minClr)
                        {
                            float push = minClr - signedD;
                            wx += nn.x * push;
                            wy += nn.y * push;
                            wz += nn.z * push;
                        }
                        else if (signedD > maxClr)
                        {
                            float pull = signedD - maxClr;
                            wx -= nn.x * pull;
                            wy -= nn.y * pull;
                            wz -= nn.z * pull;
                        }
                    }
                }
            }

            // Convert back into grooming model's bind-pose space (so inv(bindPose[Head]) * pose[Head] aligns!)
            newV[i * 3 + 0] = groomHeadBone.x + wx;
            newV[i * 3 + 1] = groomHeadBone.y + wy;
            newV[i * 3 + 2] = groomHeadBone.z + wz;
        }
        mesh->vertices = newV;
        if (mesh->animVertices)
            memcpy(mesh->animVertices, newV, (size_t)vc * 3 * sizeof(float));
        if (mesh->vboId && mesh->vboId[0] > 0)
            rlUpdateVertexBuffer(mesh->vboId[0], newV, vc * 3 * (int)sizeof(float), 0);
    }

    free(facePos);
    free(faceNrm);

    e->fitted[gi] = clone;
    e->itemIdx[gi] = itemIdx;
    e->hasOwnedVerts[gi] = allOwned;
    TraceLog(LOG_INFO, "MODULAR: Auto-fitted slot %d (item %d) to body %d (scale %.2fx%.2fx%.2f, shift %+.3fY %+.3fZ)",
             slot, itemIdx, bodyIdx, sx, sy, sz, tgt.center.y - refCenter.y, tgt.center.z - refCenter.z);
    return e->fitted[gi];
}

static void ModularVboCache_ClearAll(void)
{
    for (int i = 0; i < MODULAR_MAX_UNIQUE_INSTANCES; i++)
    {
        for (int s = 0; s < MODULAR_SLOT_COUNT; s++)
        {
            if (s_vboCache[i].slotClones[s].meshCount > 0)
                CharacterModel_UnloadSkinnedClone(&s_vboCache[i].slotClones[s]);
            s_vboCache[i].slotSourceIdx[s] = -1;
        }
        if (s_vboCache[i].hasBaseBodyFallback)
        {
            CharacterModel_UnloadSkinnedClone(&s_vboCache[i].baseBodyFallback);
            s_vboCache[i].hasBaseBodyFallback = false;
        }
        s_vboCache[i].owner = NULL;
    }
}

static Model ModularVboCache_GetSlotModel(const ModularInstance *inst, int slot, int itemIdx, Model srcModel)
{
    if (!inst || !inst->useUniqueGpuBuffers || srcModel.meshCount <= 0)
        return srcModel;

    int slotIdx = -1;
    for (int i = 0; i < MODULAR_MAX_UNIQUE_INSTANCES; i++)
    {
        if (s_vboCache[i].owner == inst) { slotIdx = i; break; }
        if (s_vboCache[i].owner == NULL && slotIdx < 0) slotIdx = i;
    }
    if (slotIdx < 0)
        return srcModel;

    ModularInstanceVboCache *c = &s_vboCache[slotIdx];
    if (c->owner != inst)
    {
        c->owner = inst;
        for (int s = 0; s < MODULAR_SLOT_COUNT; s++)
            c->slotSourceIdx[s] = -1;
        c->hasBaseBodyFallback = false;
    }

    if (slot < 0)
    {
        if (!c->hasBaseBodyFallback)
        {
            c->baseBodyFallback = CharacterModel_CloneSkinnedModel(srcModel);
            c->hasBaseBodyFallback = (c->baseBodyFallback.meshCount > 0);
        }
        return c->hasBaseBodyFallback ? c->baseBodyFallback : srcModel;
    }

    if (c->slotSourceIdx[slot] != itemIdx || c->slotClones[slot].meshCount <= 0)
    {
        if (c->slotClones[slot].meshCount > 0)
            CharacterModel_UnloadSkinnedClone(&c->slotClones[slot]);
        c->slotClones[slot] = CharacterModel_CloneSkinnedModel(srcModel);
        c->slotSourceIdx[slot] = itemIdx;
    }
    return (c->slotClones[slot].meshCount > 0) ? c->slotClones[slot] : srcModel;
}

void ModularSystem_Init(void)
{
    if (s_systemInitialized)
        return;
    s_itemCount = 0;
    memset(s_itemRegistry, 0, sizeof(s_itemRegistry));
    s_systemInitialized = true;
    TraceLog(LOG_INFO, "MODULAR_SYSTEM: Initialized modular character management system.");
}

void ModularSystem_Shutdown(void)
{
    GroomRefit_ClearAll();
    ModularVboCache_ClearAll();
    s_itemCount = 0;
    memset(s_itemRegistry, 0, sizeof(s_itemRegistry));
    s_systemInitialized = false;
    TraceLog(LOG_INFO, "MODULAR_SYSTEM: Shutdown modular system.");
}

int ModularSystem_RegisterItem(const char *id, ModularSlot slot, bool isSkinned,
                               const char *boneSocket, Vector3 offsetPos, Vector3 offsetRotEuler,
                               const char *modelGlbPath)
{
    if (!id || !id[0] || !modelGlbPath || s_itemCount >= MODULAR_MAX_ITEMS)
        return -1;

    int existing = ModularSystem_FindItem(id);
    if (existing >= 0)
        return existing;

    int idx = s_itemCount++;
    ModularItemDef *item = &s_itemRegistry[idx];
    memset(item, 0, sizeof(ModularItemDef));
    strncpy(item->id, id, sizeof(item->id) - 1);
    item->slot = slot;
    item->isSkinned = isSkinned;
    if (boneSocket && boneSocket[0] != '\0')
        strncpy(item->boneSocket, boneSocket, sizeof(item->boneSocket) - 1);

    // Compute local socket offset matrix (row-vector convention: rotate then translate)
    Matrix rotX = MatrixRotateX(offsetRotEuler.x * DEG2RAD);
    Matrix rotY = MatrixRotateY(offsetRotEuler.y * DEG2RAD);
    Matrix rotZ = MatrixRotateZ(offsetRotEuler.z * DEG2RAD);
    Matrix rot = MatrixMultiply(MatrixMultiply(rotZ, rotY), rotX);
    Matrix trans = MatrixTranslate(offsetPos.x, offsetPos.y, offsetPos.z);
    item->localOffset = MatrixMultiply(rot, trans);

    // Load 3D model if path is valid (do not gate with FileExists: fails on Android APK assets)
    if (modelGlbPath[0] != '\0')
    {
        item->model = ResourceManager_LoadModel(modelGlbPath);
        if (item->model.meshCount > 0)
        {
            SurfaceMaterial_Apply(&item->model);
            item->loaded = true;
            TraceLog(LOG_INFO, "MODULAR_SYSTEM: Registered item [%2d] '%s' (slot %d, %s, %d meshes)",
                     idx, id, (int)slot, isSkinned ? "SKINNED" : "SOCKET", item->model.meshCount);
        }
        else
        {
            item->loaded = false;
            TraceLog(LOG_WARNING, "MODULAR_SYSTEM: Failed to load model for item '%s' from '%s'", id, modelGlbPath);
        }
    }
    else
    {
        item->model = (Model){0};
        item->loaded = false;
    }

    return idx;
}

int ModularSystem_FindItem(const char *id)
{
    if (!id || !id[0])
        return -1;
    char c0 = id[0];
    for (int i = 0; i < s_itemCount; i++)
    {
        if (s_itemRegistry[i].id[0] == c0 && strcmp(s_itemRegistry[i].id, id) == 0)
            return i;
    }
    return -1;
}

const ModularItemDef *ModularSystem_GetItem(int index)
{
    if (index < 0 || index >= s_itemCount)
        return NULL;
    return &s_itemRegistry[index];
}

void ModularInstance_Init(ModularInstance *inst, ModularTier tier)
{
    if (!inst)
        return;
    memset(inst, 0, sizeof(ModularInstance));
    inst->tier = tier;
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++)
    {
        inst->loadout.itemIndices[s] = -1;
    }
    CharacterModel_ResetState(&inst->animState);
    SpringBone_Init(&inst->springBones);
    WeaponTrail_Init(&inst->weaponTrail);
    inst->scale = 1.0f;
    inst->tint = WHITE;
    inst->lastClothStepTime = -1.0;
    inst->active = true;
}

static float s_windClock = 0.0f;

static Vector3 ModularInstance_SampleWind(Vector3 worldPos, void *userData)
{
    (void)userData;
    return Wind_EvaluateVelocity(worldPos, s_windClock);
}

static int RegisterSlotClothIfActive(ModularInstance *inst, ModularSlot slot, int *garmentCount)
{
    int idx = inst->loadout.itemIndices[slot];
    if (idx < 0 || idx >= s_itemCount)
        return 0;
    ModularItemDef *item = &s_itemRegistry[idx];
    if (!item->loaded || !item->isSkinned || item->model.meshCount <= 0)
        return 0;

    int reg = SpringBone_RegisterModelCloth(&inst->springBones, &item->model);
    if (reg > 0 && garmentCount)
        (*garmentCount)++;
    return reg;
}

// Rebuilds the secondary-motion rig from active worn garments only (ignoring
// base body and slots suppressed by a full outfit).
void ModularInstance_RebuildCloth(ModularInstance *inst)
{
    if (!inst)
        return;

    SpringBone_Init(&inst->springBones);

    int registered = 0;
    int garments = 0;
    int fullOutfitIdx = inst->loadout.itemIndices[MODULAR_SLOT_FULL_OUTFIT];
    bool hasFullOutfit = (fullOutfitIdx >= 0 && fullOutfitIdx < s_itemCount && s_itemRegistry[fullOutfitIdx].loaded);

    // 1. Register Full Outfit first so its primary cloth chain dimensions take priority
    if (hasFullOutfit)
    {
        registered += RegisterSlotClothIfActive(inst, MODULAR_SLOT_FULL_OUTFIT, &garments);
    }

    // 2. Register remaining visible skinned slots
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++)
    {
        if (s == MODULAR_SLOT_BASE_BODY || s == MODULAR_SLOT_FULL_OUTFIT)
            continue;
        if (hasFullOutfit && (inst->occupiedSlotsMask & (1 << s)))
            continue;
        registered += RegisterSlotClothIfActive(inst, (ModularSlot)s, &garments);
    }

    if (registered == 0)
    {
        SpringBone_SetupWuxiaDefaults(&inst->springBones);
    }
    else
    {
        TraceLog(LOG_INFO, "MODULAR: Cloth rig rebuilt with %d simulated segments from %d garment(s).",
                 registered, garments);
    }
}

bool ModularInstance_Equip(ModularInstance *inst, ModularSlot slot, const char *itemId)
{
    if (!inst || slot < 0 || slot >= MODULAR_SLOT_COUNT)
        return false;
    int idx = ModularSystem_FindItem(itemId);
    return ModularInstance_EquipIndex(inst, slot, idx);
}

bool ModularInstance_EquipIndex(ModularInstance *inst, ModularSlot slot, int itemIndex)
{
    if (!inst || slot < 0 || slot >= MODULAR_SLOT_COUNT)
        return false;
    if (itemIndex >= 0 && itemIndex < s_itemCount)
    {
        if (inst->loadout.itemIndices[slot] == itemIndex)
            return true;
        inst->loadout.itemIndices[slot] = itemIndex;
        ModularInstance_RebuildCloth(inst);
        return true;
    }
    else if (itemIndex == -1)
    {
        if (inst->loadout.itemIndices[slot] == -1)
            return true;
        inst->loadout.itemIndices[slot] = -1;
        ModularInstance_RebuildCloth(inst);
        return true;
    }
    return false;
}

void ModularInstance_Update(ModularInstance *inst, float dt, bool isMoving)
{
    if (!inst || !inst->active)
        return;
    CharacterModel_Update(&inst->animState, dt, isMoving);
    ModularInstance_UpdateCloth(inst, dt);
}

void ModularInstance_UpdateCloth(ModularInstance *inst, float dt)
{
    if (!inst || !inst->active)
        return;
    if (dt <= 1.0e-5f)
        return;

    // Prevent double-stepping in the same frame when both Update() and Draw() run
    double now = GetTime();
    if (inst->lastClothStepTime >= 0.0 && (now - inst->lastClothStepTime) < 0.0001)
        return;
    inst->lastClothStepTime = now;

    // Re-seat cloth chains on teleport (> 2.0m jump; squared threshold = 4.0)
    if (inst->springBones.initialized &&
        Vector3DistanceSqr(inst->position, inst->springBones.prevCharPos) > 4.0f)
    {
        SpringBone_Reset(&inst->springBones);
    }

    float safeScale = (inst->scale > 0.0001f) ? inst->scale : 1.0f;
    s_windClock = (float)now;

    SpringBone_UpdateWithWind(&inst->springBones, &inst->animState, inst->position, inst->yaw,
                              safeScale, dt, ModularInstance_SampleWind, NULL);
}

static inline Matrix ComposeModelToWorldMatrix(Vector3 charPos, float charYaw, float charScale)
{
    float c = cosf(charYaw) * charScale;
    float s = sinf(charYaw) * charScale;
    return (Matrix){
        .m0 = c, .m4 = 0.0f, .m8 = s, .m12 = charPos.x, .m1 = 0.0f, .m5 = charScale, .m9 = 0.0f, .m13 = charPos.y, .m2 = -s, .m6 = 0.0f, .m10 = c, .m14 = charPos.z, .m3 = 0.0f, .m7 = 0.0f, .m11 = 0.0f, .m15 = 1.0f};
}

static Matrix ComputeSocketModelMatrix(ModularSlot slot, const char *boneSocket,
                                       const CharacterAnimState *animState, Matrix localOffset)
{
    Matrix socketModelMat = MatrixIdentity();
    bool trackedBone = false;

    if (boneSocket && boneSocket[0] != '\0')
    {
        trackedBone = CharacterModel_GetBoneTransform(boneSocket, animState, &socketModelMat);
    }

    if (!trackedBone)
    {
        Vector3 anchorPos = {0.0f, 0.0f, 0.0f};
        switch (slot)
        {
        case MODULAR_SLOT_HEADGEAR:
            anchorPos = (Vector3){0.0f, 1.74f, 0.0f};
            break;
        case MODULAR_SLOT_MASK:
            anchorPos = (Vector3){0.0f, 1.68f, 0.08f};
            break;
        case MODULAR_SLOT_HAIR:
            anchorPos = (Vector3){0.0f, 1.72f, 0.0f};
            break;
        case MODULAR_SLOT_EYEBROWS:
            anchorPos = (Vector3){0.0f, 1.69f, 0.11f};
            break;
        case MODULAR_SLOT_BEARD:
            anchorPos = (Vector3){0.0f, 1.60f, 0.11f};
            break;
        case MODULAR_SLOT_WAIST:
            anchorPos = (Vector3){0.12f, 0.95f, 0.08f};
            break;
        case MODULAR_SLOT_WEAPON_R:
            anchorPos = (Vector3){0.24f, 0.85f, 0.10f};
            break;
        case MODULAR_SLOT_WEAPON_L:
            anchorPos = (Vector3){-0.24f, 0.85f, 0.10f};
            break;
        case MODULAR_SLOT_BACK:
            anchorPos = (Vector3){0.0f, 1.25f, -0.16f};
            break;
        default:
            anchorPos = (Vector3){0.0f, 0.0f, 0.0f};
            break;
        }
        socketModelMat = MatrixTranslate(anchorPos.x, anchorPos.y, anchorPos.z);
    }

    return MatrixMultiply(localOffset, socketModelMat);
}

void ModularInstance_SetWeaponTrailElement(ModularInstance *inst, int element)
{
    if (!inst) return;
    static const Color kGlow[6] = {
        {  65, 195, 255, 255 }, // Water
        {  75, 225, 130, 255 }, // Wood
        { 255, 110,  45, 255 }, // Fire
        { 235, 155,  55, 255 }, // Earth
        { 160, 220, 255, 255 }, // Metal
        { 190, 125, 255, 255 }  // Taiji
    };
    static const Color kCore[6] = {
        { 235, 252, 255, 255 },
        { 235, 255, 235, 255 },
        { 255, 245, 200, 255 },
        { 255, 240, 200, 255 },
        { 255, 255, 255, 255 },
        { 250, 235, 255, 255 }
    };
    int idx = (element >= 0 && element < 6) ? element : 4;
    WeaponTrail_SetColors(&inst->weaponTrail, kGlow[idx], kCore[idx]);
}

bool ModularInstance_GetBladeKinematics(const ModularInstance *inst,
                                        Vector3 *outWorldBase, Vector3 *outWorldTip,
                                        Vector3 *outSwingDir, Vector3 *outPlaneNormal)
{
    if (!inst) return false;
    int itemIdx = inst->loadout.itemIndices[MODULAR_SLOT_WEAPON_R];
    const char *boneSocket = "mixamorig:RightHand";
    Matrix localOffset = MatrixIdentity();
    if (itemIdx >= 0 && itemIdx < s_itemCount && s_itemRegistry[itemIdx].loaded)
    {
        if (s_itemRegistry[itemIdx].boneSocket[0] != '\0')
            boneSocket = s_itemRegistry[itemIdx].boneSocket;
        localOffset = s_itemRegistry[itemIdx].localOffset;
    }

    Matrix weaponWorldMat;
    if (!ModularSystem_GetSocketMatrixEx(inst->position, inst->yaw, inst->scale,
                                         MODULAR_SLOT_WEAPON_R, boneSocket,
                                         &inst->animState, localOffset, &weaponWorldMat))
    {
        return false;
    }

    Vector3 worldBase = Vector3Transform((Vector3){0.0f, 0.10f, 0.0f}, weaponWorldMat);
    Vector3 worldTip  = Vector3Transform((Vector3){0.0f, 0.85f, 0.0f}, weaponWorldMat);
    Vector3 bladeVec  = Vector3Subtract(worldTip, worldBase);
    if (outWorldBase) *outWorldBase = worldBase;
    if (outWorldTip)  *outWorldTip  = worldTip;

    Vector3 swingDir = (Vector3){ sinf(inst->yaw), 0.0f, cosf(inst->yaw) };
    Vector3 planeNormal = (Vector3){ 0.0f, 1.0f, 0.0f };

    if (inst->weaponTrail.count >= 2)
    {
        Vector3 prevTip = inst->weaponTrail.points[1].tip;
        Vector3 tipDelta = Vector3Subtract(worldTip, prevTip);
        if (Vector3LengthSqr(tipDelta) > 1e-5f)
        {
            swingDir = Vector3Normalize(tipDelta);
            Vector3 crossN = Vector3CrossProduct(bladeVec, tipDelta);
            if (Vector3LengthSqr(crossN) > 1e-5f)
                planeNormal = Vector3Normalize(crossN);
        }
    }
    else if (inst->weaponTrail.hasPrev)
    {
        Vector3 tipDelta = Vector3Subtract(worldTip, inst->weaponTrail.prevTip);
        if (Vector3LengthSqr(tipDelta) > 1e-5f)
        {
            swingDir = Vector3Normalize(tipDelta);
            Vector3 crossN = Vector3CrossProduct(bladeVec, tipDelta);
            if (Vector3LengthSqr(crossN) > 1e-5f)
                planeNormal = Vector3Normalize(crossN);
        }
    }

    if (outSwingDir)    *outSwingDir    = swingDir;
    if (outPlaneNormal) *outPlaneNormal = planeNormal;
    return true;
}

bool ModularSystem_GetSocketMatrixEx(Vector3 charPos, float charYaw, float charScale,
                                     ModularSlot slot, const char *boneSocket,
                                     const CharacterAnimState *animState,
                                     Matrix localOffset, Matrix *outMatrix)
{
    if (!outMatrix)
        return false;

    Matrix localToModel = ComputeSocketModelMatrix(slot, boneSocket, animState, localOffset);
    Matrix modelToWorld = ComposeModelToWorldMatrix(charPos, charYaw, charScale);
    *outMatrix = MatrixMultiply(localToModel, modelToWorld);
    return true;
}

bool ModularSystem_GetSocketMatrix(Vector3 charPos, float charYaw, float charScale,
                                   ModularSlot slot, Matrix localOffset, Matrix *outMatrix)
{
    return ModularSystem_GetSocketMatrixEx(charPos, charYaw, charScale, slot, NULL, NULL, localOffset, outMatrix);
}

#include "rlgl.h"

extern bool EnvShadow_IsCapturing(void);
extern Shader EnvShadow_GetDepthShader(void);

void ModularInstance_Draw(ModularInstance *inst)
{
    if (!inst || !inst->active)
        return;

    const bool isShadow = EnvShadow_IsCapturing();
    const Shader depthShader = isShadow ? EnvShadow_GetDepthShader() : (Shader){0};

    static int s_clothEnabled = -1;
    if (s_clothEnabled < 0)
    {
        const char *env = getenv("WUXING_CLOTH");
        s_clothEnabled = (env && env[0] == '0') ? 0 : 1;
    }
    const SpringBoneSystem *cloth = s_clothEnabled ? &inst->springBones : NULL;

    // 1. Draw base body with CPU skinning (only validate masterModel when body is visible)
    int activeBodyIdx = inst->loadout.itemIndices[MODULAR_SLOT_BASE_BODY];
    Model activeBodySrc = (activeBodyIdx >= 0 && activeBodyIdx < s_itemCount && s_itemRegistry[activeBodyIdx].loaded)
                              ? s_itemRegistry[activeBodyIdx].model
                              : CharacterModel_GetModel();
    if (!inst->hideBaseBody)
    {
        int bodyIdx = activeBodyIdx;
        Model masterModel;
        if (bodyIdx >= 0 && bodyIdx < s_itemCount && s_itemRegistry[bodyIdx].loaded)
        {
            masterModel = ModularVboCache_GetSlotModel(inst, MODULAR_SLOT_BASE_BODY, bodyIdx, s_itemRegistry[bodyIdx].model);
        }
        else
        {
            masterModel = ModularVboCache_GetSlotModel(inst, -1, -1, CharacterModel_GetModel());
        }

        if (masterModel.meshCount > 0 && masterModel.meshes != NULL)
        {
            bool hasTorso = (inst->loadout.itemIndices[MODULAR_SLOT_TORSO] >= 0) ||
                            (inst->loadout.itemIndices[MODULAR_SLOT_FULL_OUTFIT] >= 0);
            bool hasLegs = (inst->loadout.itemIndices[MODULAR_SLOT_LEGS] >= 0) ||
                           (inst->loadout.itemIndices[MODULAR_SLOT_FULL_OUTFIT] >= 0) ||
                           hasTorso;
            CharacterModel_DrawCustomEx(masterModel, &inst->animState, inst->position, inst->yaw, inst->scale, inst->tint, hasTorso, hasLegs);
        }
    }

    // 2. Full Outfit pass (if equipped)
    int fullOutfitIdx = inst->loadout.itemIndices[MODULAR_SLOT_FULL_OUTFIT];
    bool hasFullOutfit = (fullOutfitIdx >= 0 && fullOutfitIdx < s_itemCount && s_itemRegistry[fullOutfitIdx].loaded);
    if (hasFullOutfit)
    {
        ModularItemDef *foItem = &s_itemRegistry[fullOutfitIdx];
        if (foItem->model.meshCount > 0 && foItem->model.meshes != NULL)
        {
            Model foModel = GroomRefit_GetModel(inst, MODULAR_SLOT_FULL_OUTFIT, fullOutfitIdx, foItem->model, activeBodyIdx, activeBodySrc);
            CharacterModel_DrawCustomCloth(foModel, &inst->animState,
                                           inst->position, inst->yaw, inst->scale, inst->tint,
                                           cloth);
        }
    }

    float dt = GetFrameTime();
    if (dt > 0.05f)
        dt = 0.05f;

    Matrix modelToWorld = ComposeModelToWorldMatrix(inst->position, inst->yaw, inst->scale);

    // 3. Draw remaining equipped items (skipping slots suppressed by full outfit)
    bool weaponSampled = false;
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++)
    {
        if (s == MODULAR_SLOT_BASE_BODY || s == MODULAR_SLOT_FULL_OUTFIT)
            continue;
        if (hasFullOutfit && (inst->occupiedSlotsMask & (1 << s)))
            continue;

        int itemIdx = inst->loadout.itemIndices[s];
        if (itemIdx >= 0 && itemIdx < s_itemCount && s_itemRegistry[itemIdx].loaded)
        {
            ModularItemDef *item = &s_itemRegistry[itemIdx];
            if (item->model.meshCount <= 0 || item->model.meshes == NULL)
                continue;

            if (item->isSkinned)
            {
                bool deflateUnderRobe = (s == MODULAR_SLOT_LEGS) &&
                                        ((inst->loadout.itemIndices[MODULAR_SLOT_TORSO] >= 0) || hasFullOutfit);
                Model drawModel = (GroomSlotToIdx(s) >= 0)
                                      ? GroomRefit_GetModel(inst, s, itemIdx, item->model, activeBodyIdx, activeBodySrc)
                                      : ModularVboCache_GetSlotModel(inst, s, itemIdx, item->model);
                CharacterModel_DrawCustomClothEx(drawModel, &inst->animState,
                                                 inst->position, inst->yaw, inst->scale, inst->tint,
                                                 false, deflateUnderRobe, cloth);
            }
            else
            {
                // Auto-fit rigid head/waist/back sockets to active body's measured head/torso bounds
                Matrix fitLocalOffset = item->localOffset;
                if (s == MODULAR_SLOT_HEADGEAR || s == MODULAR_SLOT_MASK ||
                    s == MODULAR_SLOT_WAIST || s == MODULAR_SLOT_BACK)
                {
                    HeadBoundsInfo tb = MeasureBodyHeadBounds(activeBodySrc);
                    if ((s == MODULAR_SLOT_HEADGEAR || s == MODULAR_SLOT_MASK) && tb.valid)
                    {
                        float sh = Clamp(tb.halfExtents.x / 0.0800f, 0.78f, 1.28f);
                        Vector3 dh = { tb.center.x, tb.center.y - 0.0107f, tb.center.z - 0.0244f };
                        if (fabsf(sh - 1.0f) > 0.02f || fabsf(dh.y) > 0.005f || fabsf(dh.z) > 0.005f)
                        {
                            Matrix adj = MatrixMultiply(MatrixScale(sh, sh, sh), MatrixTranslate(dh.x, dh.y, dh.z));
                            fitLocalOffset = MatrixMultiply(fitLocalOffset, adj);
                        }
                    }
                    else if ((s == MODULAR_SLOT_WAIST || s == MODULAR_SLOT_BACK) && tb.bodyValid)
                    {
                        float dz = Clamp(tb.torsoCenterZ - 0.0170f, -0.045f, 0.045f);
                        if (fabsf(dz) > 0.005f)
                            fitLocalOffset = MatrixMultiply(fitLocalOffset, MatrixTranslate(0.0f, 0.0f, dz));
                    }
                }
                Matrix localToModel = ComputeSocketModelMatrix((ModularSlot)s, item->boneSocket,
                                                               &inst->animState, fitLocalOffset);
                Matrix finalMat = MatrixMultiply(localToModel, modelToWorld);

                if (isShadow)
                {
                    rlDisableBackfaceCulling();
                    for (int m = 0; m < item->model.meshCount; m++)
                    {
                        int matIdx = (item->model.meshMaterial && m < item->model.meshCount) ? item->model.meshMaterial[m] : 0;
                        if (matIdx < 0 || matIdx >= item->model.materialCount)
                            matIdx = 0;
                        Material shadowMat = item->model.materials[matIdx];
                        shadowMat.shader = depthShader;
                        DrawMesh(item->model.meshes[m], shadowMat, finalMat);
                    }
                    rlEnableBackfaceCulling();
                }
                else
                {
                    for (int m = 0; m < item->model.meshCount; m++)
                    {
                        DrawMesh(item->model.meshes[m], item->model.materials[m], finalMat);
                    }
                }

                if (!isShadow && s == MODULAR_SLOT_WEAPON_R && dt > 1.0e-5f)
                {
                    bool isAttackAnim = (inst->animState.currentSlot == CHAR_ANIM_SWORD_SLASH ||
                                         inst->animState.currentSlot == CHAR_ANIM_SWORD_THRUST ||
                                         inst->animState.currentSlot == CHAR_ANIM_SWORD_PARRY ||
                                         inst->animState.currentSlot == CHAR_ANIM_SWORD_BLOCK);
                    Vector3 bladeBase = (Vector3){0.0f, 0.10f, 0.0f};
                    Vector3 bladeTip = (Vector3){0.0f, 0.85f, 0.0f};
                    WeaponTrail_Sample(&inst->weaponTrail, finalMat, bladeBase, bladeTip, dt, isAttackAnim);
                    weaponSampled = true;
                }
            }
        }
    }

    // 4. Fade and draw weapon trail in emission pass (never during shadow capture)
    if (!isShadow)
    {
        if (!weaponSampled && dt > 1.0e-5f)
        {
            WeaponTrail_Fade(&inst->weaponTrail, dt);
        }
        WeaponTrail_Draw(&inst->weaponTrail);
    }
}

void ModularInstance_DrawWithState(ModularInstance *inst, const CharacterAnimState *state,
                                   Vector3 position, float yawRadians, float scale, Color tint)
{
    if (!inst || !inst->active)
        return;
    inst->position = position;
    inst->yaw = yawRadians;
    inst->scale = scale;
    inst->tint = tint;
    if (state)
        inst->animState = *state;
    ModularInstance_UpdateCloth(inst, GetFrameTime());
    ModularInstance_Draw(inst);
}

void ModularSystem_DrawMinionBatch(int baseBodyItemIndex, int weaponItemIndex,
                                   const CharacterAnimState *sharedAnimState,
                                   const ModularMinionBatchItem *items, int itemCount)
{
    if (baseBodyItemIndex < 0 || baseBodyItemIndex >= s_itemCount || !items || itemCount <= 0)
        return;
    ModularItemDef *bodyDef = &s_itemRegistry[baseBodyItemIndex];
    if (!bodyDef->loaded || bodyDef->model.meshCount == 0)
        return;

    // 1. Evaluate skeletal CPU skinning once for the entire squad
    if (sharedAnimState && CharacterModel_IsLoaded())
    {
        int animIdx = sharedAnimState->activeAnimIndex;
        if (animIdx >= 0 && animIdx < CharacterModel_GetTotalAnimCount())
        {
            CharacterModel_DrawCustom(bodyDef->model, sharedAnimState,
                                      items[0].position, items[0].yaw, items[0].scale, items[0].tint);
        }
    }

    // 2. Render all remaining minions using the already-deformed vertex buffer
    for (int i = 1; i < itemCount; i++)
    {
        float yawDeg = items[i].yaw * RAD2DEG;
        DrawModelEx(bodyDef->model, items[i].position, (Vector3){0.0f, 1.0f, 0.0f}, yawDeg,
                    (Vector3){items[i].scale, items[i].scale, items[i].scale}, items[i].tint);
    }

    // 3. Render socketed weapons tracked to the shared hand bone pose
    if (weaponItemIndex >= 0 && weaponItemIndex < s_itemCount)
    {
        ModularItemDef *wepDef = &s_itemRegistry[weaponItemIndex];
        if (wepDef->loaded && wepDef->model.meshCount > 0)
        {
            Matrix localToModel = ComputeSocketModelMatrix(MODULAR_SLOT_WEAPON_R, wepDef->boneSocket,
                                                           sharedAnimState, wepDef->localOffset);
            for (int i = 0; i < itemCount; i++)
            {
                Matrix modelToWorld = ComposeModelToWorldMatrix(items[i].position, items[i].yaw, items[i].scale);
                Matrix wepWorld = MatrixMultiply(localToModel, modelToWorld);

                for (int m = 0; m < wepDef->model.meshCount; m++)
                {
                    DrawMesh(wepDef->model.meshes[m], wepDef->model.materials[m], wepWorld);
                }
            }
        }
    }
}