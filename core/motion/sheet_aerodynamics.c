#include "core/motion/sheet_aerodynamics.h"
#include <math.h>
#include <stddef.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static inline float SheetAero_Clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline float SheetAero_Hash1(float n)
{
    float s = sinf(n * 127.1f + 311.7f) * 43758.5453123f;
    return s - floorf(s);
}

static inline float SheetAero_VecLen(Vector3 v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline Vector3 SheetAero_VecNorm(Vector3 v)
{
    float l = SheetAero_VecLen(v);
    if (l < 1e-6f) return (Vector3){ 0.0f, 0.0f, 1.0f };
    float inv = 1.0f / l;
    return (Vector3){ v.x * inv, v.y * inv, v.z * inv };
}

static inline Vector3 SheetAero_VecCross(Vector3 a, Vector3 b)
{
    return (Vector3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static inline Vector4 SheetAero_QuatFromBasis(Vector3 R, Vector3 U, Vector3 N)
{
    // Columns of rotation matrix are R (+X), U (+Y), N (+Z)
    float m00 = R.x, m01 = U.x, m02 = N.x;
    float m10 = R.y, m11 = U.y, m12 = N.y;
    float m20 = R.z, m21 = U.z, m22 = N.z;
    float tr = m00 + m11 + m22;
    Vector4 q;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    float ql = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (ql > 1e-6f) {
        float inv = 1.0f / ql;
        q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
    } else {
        q = (Vector4){ 0.0f, 0.0f, 0.0f, 1.0f };
    }
    return q;
}

SheetAeroParams SheetAero_DefaultPaper(void)
{
    SheetAeroParams p = {0};
    p.halfWidth         = 0.2175f; // 0.435m wide (incl. 8.5% bloom margin)
    p.halfHeight        = 0.5500f; // 1.100m tall
    p.dragNormal        = 5.8f;
    p.dragNormalQuad    = 3.2f;
    p.dragLength        = 0.28f;
    p.dragLengthQuad    = 0.14f;
    p.dragWidth         = 0.56f;
    p.dragWidthQuad     = 0.24f;
    p.liftCoeff         = 3.9f;
    p.glideBoost        = 1.15f;
    p.torquePitchCoeff  = 3.4f;
    p.torqueRollCoeff   = 4.4f;
    p.torqueYawCoeff    = 0.45f;
    p.gustTorqueAmp     = 0.38f;
    p.rotDamping        = 3.1f;
    p.rotDampingSpeed   = 0.45f;
    p.maxAngularSpeed   = 8.5f;
    p.gravity           = 3.15f;
    p.softness          = 0.55f;
    p.enableFloor       = true;
    p.floorY            = 0.0f;
    p.floorFriction     = 5.2f;
    p.floorAlignK       = 14.0f;
    p.peelWindThreshold = 2.2f;
    p.peelLiftGain      = 2.1f;
    p.tetherEnabled     = false;
    p.tetherStiffnessXZ = 14.0f;
    p.tetherStiffnessY  = 18.0f;
    p.tetherDamping     = 3.4f;
    p.uprightRestore    = 0.0f;
    return p;
}

SheetAeroParams SheetAero_DefaultLeaf(void)
{
    SheetAeroParams p = SheetAero_DefaultPaper();
    p.halfWidth        = 0.06f;
    p.halfHeight       = 0.11f;
    p.dragNormal       = 6.8f;
    p.dragNormalQuad   = 3.8f;
    p.liftCoeff        = 4.2f;
    p.torquePitchCoeff = 3.8f;
    p.torqueRollCoeff  = 5.0f;
    p.gravity          = 2.60f;
    p.softness         = 0.40f;
    return p;
}

void SheetAero_InitState(SheetAeroState *state, Vector3 pos, Vector4 quat,
                         float scale, float naturalCurl, float seed)
{
    if (!state) return;
    *state = (SheetAeroState){0};
    state->pos = pos;
    float qLen = sqrtf(quat.x * quat.x + quat.y * quat.y + quat.z * quat.z + quat.w * quat.w);
    if (qLen > 1e-6f) {
        float inv = 1.0f / qLen;
        state->quat = (Vector4){ quat.x * inv, quat.y * inv, quat.z * inv, quat.w * inv };
    } else {
        state->quat = (Vector4){ 0.0f, 0.0f, 0.0f, 1.0f };
    }
    state->scale       = (scale > 0.01f) ? scale : 1.0f;
    state->naturalCurl = naturalCurl;
    state->bend        = naturalCurl;
    state->seed        = seed;
}

void SheetAero_ExtractBasis(Vector4 q, Vector3 *outR, Vector3 *outU, Vector3 *outN)
{
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    if (outR) *outR = (Vector3){ 1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy) };
    if (outU) *outU = (Vector3){ 2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx) };
    if (outN) *outN = (Vector3){ 2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy) };
}

void SheetAero_ComputeLaunchBasis(Vector3 aimDir, float bankRad,
                                  Vector4 *outQuat, Vector3 *outSide,
                                  Vector3 *outLead, Vector3 *outNorm)
{
    Vector3 lead = aimDir;
    float lLen = SheetAero_VecLen(lead);
    if (lLen < 1e-5f) lead = (Vector3){ 0.0f, 0.0f, -1.0f };
    else lead = (Vector3){ lead.x / lLen, lead.y / lLen, lead.z / lLen };

    Vector3 side = SheetAero_VecCross(lead, (Vector3){ 0.0f, 1.0f, 0.0f });
    float sideLen2 = side.x * side.x + side.y * side.y + side.z * side.z;
    if (sideLen2 < 1e-4f) {
        side = (Vector3){ 1.0f, 0.0f, 0.0f };
    } else {
        side = SheetAero_VecNorm(side);
    }
    Vector3 norm = SheetAero_VecNorm(SheetAero_VecCross(side, lead));

    // Apply subtle bank angle around leading axis
    float cb = cosf(bankRad);
    float sb = sinf(bankRad);
    Vector3 bankedSide = SheetAero_VecNorm((Vector3){
        side.x * cb + norm.x * sb,
        side.y * cb + norm.y * sb,
        side.z * cb + norm.z * sb
    });
    Vector3 bankedNorm = SheetAero_VecNorm(SheetAero_VecCross(bankedSide, lead));

    if (outSide) *outSide = bankedSide;
    if (outLead) *outLead = lead;
    if (outNorm) *outNorm = bankedNorm;
    if (outQuat) *outQuat = SheetAero_QuatFromBasis(bankedSide, lead, bankedNorm);
}

void SheetAero_Step(SheetAeroState *s, const SheetAeroParams *p,
                    Vector3 windVel, float gustMag, float dt, float time)
{
    if (!s || !p || dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;

    float soft    = SheetAero_Clampf(p->softness, 0.10f, 1.0f);
    float springK = 74.0f - soft * 34.0f;
    float springD = 10.5f - soft * 3.0f;
    float flexGain = 0.45f + soft * 1.15f;

    // 1. Extract local orthonormal frame (R = width X, U = length Y, N = normal Z)
    Vector3 R, U, N;
    SheetAero_ExtractBasis(s->quat, &R, &U, &N);

    // 2. Relative air velocity in paper local frame
    float rvx = s->vel.x - windVel.x;
    float rvy = s->vel.y - windVel.y;
    float rvz = s->vel.z - windVel.z;

    float vR = rvx * R.x + rvy * R.y + rvz * R.z;
    float vU = rvx * U.x + rvy * U.y + rvz * U.z;
    float vN = rvx * N.x + rvy * N.y + rvz * N.z;
    float speed  = sqrtf(vR * vR + vU * vU + vN * vN);
    float vPlane = sqrtf(vR * vR + vU * vU);

    // 3. Anisotropic air drag + thin-plate aerodynamic lift & glide
    float fDragN = -(p->dragNormal + p->dragNormalQuad * fabsf(vN)) * vN;
    float fDragU = -(p->dragLength + p->dragLengthQuad * fabsf(vU)) * vU;
    float fDragR = -(p->dragWidth  + p->dragWidthQuad  * fabsf(vR)) * vR;

    float fLiftN = 0.0f, fGlideU = 0.0f, fGlideR = 0.0f;
    if (speed > 0.06f) {
        float invSpd = 1.0f / speed;
        float sinA   = vN * invSpd;
        float cosA   = vPlane * invSpd;
        float spdCap = (speed < 8.0f) ? speed : 8.0f;
        float liftScale = p->liftCoeff * speed * sinA * cosA * (1.0f + 0.35f * spdCap);
        fLiftN = -liftScale;
        if (vPlane > 0.04f) {
            float gBoost = fabsf(sinA) * cosA * p->glideBoost;
            fGlideU = (vU / vPlane) * gBoost;
            fGlideR = (vR / vPlane) * gBoost * 0.65f;
        }
    }

    float fLocR = fDragR + fGlideR;
    float fLocU = fDragU + fGlideU;
    float fLocN = fDragN + fLiftN;

    float fx = R.x * fLocR + U.x * fLocU + N.x * fLocN;
    float fy = R.y * fLocR + U.y * fLocU + N.y * fLocN;
    float fz = R.z * fLocR + U.z * fLocU + N.z * fLocN;

    // 4. Gravity or tether suspension
    if (p->tetherEnabled) {
        float bobX = sinf(time * 0.55f + s->seed) * 0.09f;
        float bobY = cosf(time * 0.72f + s->seed) * 0.07f;
        float bobZ = sinf(time * 0.42f + s->seed) * 0.08f;
        fx += (p->tetherPos.x + bobX - s->pos.x) * p->tetherStiffnessXZ - s->vel.x * p->tetherDamping;
        fy += (p->tetherPos.y + bobY - s->pos.y) * p->tetherStiffnessY  - s->vel.y * (p->tetherDamping + 0.4f);
        fz += (p->tetherPos.z + bobZ - s->pos.z) * p->tetherStiffnessXZ - s->vel.z * p->tetherDamping;
    } else {
        fy -= p->gravity;
    }

    // 5. Shifting Center-of-Pressure aerodynamic torque
    float sd = s->seed;
    float windMod = 0.4f + 0.6f * SheetAero_Clampf(SheetAero_VecLen(windVel) * 0.5f, 0.0f, 2.0f);
    float tauLocR = -vU * vN * p->torquePitchCoeff + sinf(time * 2.4f + sd) * p->gustTorqueAmp * windMod;
    float tauLocU =  vR * vN * p->torqueRollCoeff  + cosf(time * 2.9f + sd * 1.7f) * (p->gustTorqueAmp * 1.1f) * windMod;
    float tauLocN =  vR * vU * p->torqueYawCoeff   + sinf(time * 1.6f + sd * 2.3f) * (p->gustTorqueAmp * 0.58f);

    if (p->uprightRestore > 0.0f) {
        float kUp = p->uprightRestore;
        float kFwd = p->uprightRestore * 0.62f;
        s->angVel.x += (-U.z * kUp + N.y * kFwd) * dt;
        s->angVel.y += (-N.x * kFwd) * dt;
        s->angVel.z += ( U.x * kUp) * dt;
        tauLocR *= 0.45f;
        tauLocU *= 0.55f;
    }

    float txW = R.x * tauLocR + U.x * tauLocU + N.x * tauLocN;
    float tyW = R.y * tauLocR + U.y * tauLocU + N.y * tauLocN;
    float tzW = R.z * tauLocR + U.z * tauLocU + N.z * tauLocN;

    float spdCap = (speed < 8.0f) ? speed : 8.0f;
    float rotDamp = expf(-(p->rotDamping + spdCap * p->rotDampingSpeed) * dt);
    s->angVel.x = (s->angVel.x + txW * dt) * rotDamp;
    s->angVel.y = (s->angVel.y + tyW * dt) * rotDamp;
    s->angVel.z = (s->angVel.z + tzW * dt) * rotDamp;

    float wMag = SheetAero_VecLen(s->angVel);
    if (wMag > p->maxAngularSpeed && p->maxAngularSpeed > 0.0f) {
        float invW = p->maxAngularSpeed / wMag;
        s->angVel.x *= invW;
        s->angVel.y *= invW;
        s->angVel.z *= invW;
    }

    // 6. Integrate linear velocity & position
    s->vel.x += fx * dt;
    s->vel.y += fy * dt;
    s->vel.z += fz * dt;

    float nx = s->pos.x + s->vel.x * dt;
    float ny = s->pos.y + s->vel.y * dt;
    float nz = s->pos.z + s->vel.z * dt;

    // 7. Integrate orientation quaternion: q' = q + 0.5 * dt * (0, w) * q
    float hdt = 0.5f * dt;
    float dwx = s->angVel.x, dwy = s->angVel.y, dwz = s->angVel.z;
    float xq = s->quat.x, yq = s->quat.y, zq = s->quat.z, wq = s->quat.w;
    float nqx = xq + hdt * ( dwx * wq + dwy * zq - dwz * yq);
    float nqy = yq + hdt * ( dwy * wq + dwz * xq - dwx * zq);
    float nqz = zq + hdt * ( dwz * wq + dwx * yq - dwy * xq);
    float nqw = wq + hdt * (-dwx * xq - dwy * yq - dwz * zq);
    float qLen = sqrtf(nqx * nqx + nqy * nqy + nqz * nqz + nqw * nqw);
    float qInv = (qLen > 1e-6f) ? (1.0f / qLen) : 1.0f;
    nqx *= qInv; nqy *= qInv; nqz *= qInv; nqw *= qInv;

    // 8. Floor contact, damp sliding friction, flat alignment, and wind peel-up
    float tiltExtent = (fabsf(U.y) * p->halfHeight * 0.80f + fabsf(R.y) * p->halfWidth * 0.78f) * s->scale;
    float minY = p->floorY + 0.022f + tiltExtent * 0.58f;
    bool onFloor = false;

    if (p->enableFloor && !p->tetherEnabled && ny <= minY) {
        ny = minY;
        onFloor = true;
        if (s->vel.y < 0.0f) s->vel.y *= -0.06f;

        float slideFric = expf(-p->floorFriction * dt);
        s->vel.x *= slideFric;
        s->vel.z *= slideFric;

        // Smoothly align face normal toward +Y or -Y (face-up or face-down)
        float targetNy = (N.y >= 0.0f) ? 1.0f : -1.0f;
        float alignK   = p->floorAlignK * dt;
        float flatDamp = expf(-8.0f * dt);
        s->angVel.x = s->angVel.x * flatDamp + (-N.z * targetNy) * alignK;
        s->angVel.y *= expf(-7.5f * dt);
        s->angVel.z = s->angVel.z * flatDamp + ( N.x * targetNy) * alignK;

        float horizWind = sqrtf(windVel.x * windVel.x + windVel.z * windVel.z);
        if (horizWind > p->peelWindThreshold || gustMag > 1.35f) {
            float liftAmt = fmaxf(horizWind - (p->peelWindThreshold - 0.3f), gustMag) * p->peelLiftGain;
            s->vel.y += liftAmt * dt * 8.5f;
            s->vel.x += windVel.x * dt * 3.0f;
            s->vel.z += windVel.z * dt * 3.0f;
            float r1 = SheetAero_Hash1(time * 13.7f + sd) - 0.5f;
            float r2 = SheetAero_Hash1(time * 19.3f + sd * 2.1f) - 0.5f;
            s->angVel.x += r1 * liftAmt * 1.4f;
            s->angVel.z += r2 * liftAmt * 1.4f;
            s->restTime = 0.0f;
        } else {
            s->restTime += dt;
        }
    } else {
        s->restTime = fmaxf(0.0f, s->restTime - dt * 2.0f);
    }

    s->pos = (Vector3){ nx, ny, nz };
    s->quat = (Vector4){ nqx, nqy, nqz, nqw };
    s->onFloor = onFloor;

    // 9. Coupled 4-mode spring-damper deformation
    float wLocR = s->angVel.x * R.x + s->angVel.y * R.y + s->angVel.z * R.z;
    float wLocU = s->angVel.x * U.x + s->angVel.y * U.y + s->angVel.z * U.z;

    float tBend  = s->naturalCurl * 0.7f - SheetAero_Clampf(vN * 0.34f, -1.1f, 1.1f) * flexGain;
    float tSBend = SheetAero_Clampf((wLocR * 0.24f + vU * vN * 0.085f) * flexGain, -0.9f, 0.9f);
    float tTwist = SheetAero_Clampf((wLocU * 0.28f + vR * vN * 0.11f) * flexGain, -1.2f, 1.2f);
    float tCup   = s->naturalCurl * 0.5f - SheetAero_Clampf(vN * 0.22f * flexGain, -0.55f, 0.55f);

    if (onFloor) {
        tBend  = s->naturalCurl * 0.22f;
        tSBend = 0.0f;
        tTwist = 0.0f;
        tCup   = s->naturalCurl * 0.18f;
    }

    s->bendV  += ((tBend  - s->bend)  * springK - s->bendV  * springD) * dt;
    s->bend   += s->bendV * dt;

    s->sBendV += ((tSBend - s->sBend) * springK - s->sBendV * springD) * dt;
    s->sBend  += s->sBendV * dt;

    s->twistV += ((tTwist - s->twist) * springK - s->twistV * springD) * dt;
    s->twist  += s->twistV * dt;

    s->cupV   += ((tCup   - s->cup)   * springK - s->cupV   * springD) * dt;
    s->cup    += s->cupV * dt;

    // Airspeed-scaled continuous trailing-edge flutter
    float targetFlut = onFloor ? 0.0f : fminf(0.11f, (speed * speed * 0.0042f + fabsf(vN) * 0.016f) * flexGain);
    float flutLerp   = fminf(1.0f, dt * 10.0f);
    s->flutterAmp   += (targetFlut - s->flutterAmp) * flutLerp;
    float spdFlut    = (speed < 14.0f) ? speed : 14.0f;
    s->flutterPhase += dt * (4.2f + spdFlut * 4.8f);
    if (s->flutterPhase > 628.3185f) s->flutterPhase -= 628.3185f;
}

static Vector3 SheetAero_EvalDeformOnly(Vector3 p, float halfW, float halfH,
                                        float bend, float sBend, float twist, float cup,
                                        float flutterAmp, float flutterPhase)
{
    float u = (halfW > 1e-5f) ? (p.x / halfW) : 0.0f;
    float v = (halfH > 1e-5f) ? (p.y / halfH) : 0.0f;

    // Length foreshortening when bent so sheet stays inextensible
    float archShrink = fminf(0.18f, bend * bend * 0.042f + sBend * sBend * 0.032f);
    p.y *= (1.0f - archShrink);

    // Longitudinal arch + stall S-curve + transverse cupping
    float bendZ  = bend  * (v * v - 0.34f) * 0.24f;
    float sBendZ = sBend * (v * v * v - 0.56f * v) * 0.26f;
    float cupZ   = cup   * (u * u - 0.35f) * 0.14f;

    // Traveling aerodynamic trailing-edge flutter
    float tailWeight = 0.16f + 0.84f * SheetAero_Clampf(0.5f - 0.5f * v, 0.0f, 1.0f);
    float edgeWeight = 0.72f + 0.28f * u * u;
    float w1 = sinf(v * 6.0f  + flutterPhase) * 0.72f;
    float w2 = sinf(v * 11.4f + flutterPhase * 1.68f + u * 2.2f) * 0.28f;
    float flutZ = flutterAmp * (w1 + w2) * tailWeight * edgeWeight;

    p.z += bendZ + sBendZ + cupZ + flutZ;

    // Helical torsional twist along spine
    float tw = twist * v * 0.56f;
    float cs = cosf(tw);
    float sn = sinf(tw);
    float rx = cs * p.x - sn * p.z;
    float rz = sn * p.x + cs * p.z;
    p.x = rx;
    p.z = rz;
    return p;
}

void SheetAero_DeformPoint(Vector3 localPos, float halfW, float halfH,
                           float bend, float sBend, float twist, float cup,
                           float flutterAmp, float flutterPhase,
                           Vector3 *outPos, Vector3 *outNormal)
{
    Vector3 p0 = SheetAero_EvalDeformOnly(localPos, halfW, halfH, bend, sBend, twist, cup, flutterAmp, flutterPhase);
    if (outPos) *outPos = p0;

    if (outNormal) {
        const float eps = 0.004f;
        Vector3 px = SheetAero_EvalDeformOnly((Vector3){ localPos.x + eps, localPos.y, localPos.z },
                                              halfW, halfH, bend, sBend, twist, cup, flutterAmp, flutterPhase);
        Vector3 py = SheetAero_EvalDeformOnly((Vector3){ localPos.x, localPos.y + eps, localPos.z },
                                              halfW, halfH, bend, sBend, twist, cup, flutterAmp, flutterPhase);
        Vector3 dx = (Vector3){ px.x - p0.x, px.y - p0.y, px.z - p0.z };
        Vector3 dy = (Vector3){ py.x - p0.x, py.y - p0.y, py.z - p0.z };
        *outNormal = SheetAero_VecNorm(SheetAero_VecCross(dx, dy));
    }
}
