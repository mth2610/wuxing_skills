#ifndef VC_WOOD_SOCKET_INL
#define VC_WOOD_SOCKET_INL

#include "raylib.h"
#include "raymath.h"
#include <math.h>

/* Sample natural botanical attachment sockets along any 3D spine curve using
 * Rotation Minimizing Frame (RMF) and golden-angle phyllotaxis. */
static int Botanical_SampleSocketsOnSpine(const Vector3 *spinePts, int ptCount, float baseRadius,
                                         VFX_BotanicalSocket *outSockets, int maxSockets,
                                         unsigned int seed, float spacing)
{
    if (spinePts == NULL || ptCount < 2 || outSockets == NULL || maxSockets <= 0) return 0;

    if (spacing < 0.05f) spacing = 0.22f; // Default ~22cm between leaf nodes

    // Compute cumulative segment lengths
    float segLengths[64];
    float totalLen = 0.0f;
    int segCount = (ptCount - 1 < 63) ? (ptCount - 1) : 63;

    for (int i = 0; i < segCount; i++)
    {
        float d = Vector3Distance(spinePts[i], spinePts[i + 1]);
        segLengths[i] = d;
        totalLen += d;
    }
    if (totalLen < 1e-4f) return 0;

    // RMF Initial Reference Vector
    Vector3 v0 = Vector3Subtract(spinePts[1], spinePts[0]);
    float v0Len = Vector3Length(v0);
    Vector3 t0 = (v0Len > 1e-4f) ? Vector3Scale(v0, 1.0f / v0Len) : (Vector3){0.0f, 1.0f, 0.0f};

    Vector3 upGuide = (fabsf(t0.y) < 0.92f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 r0 = Vector3Normalize(Vector3CrossProduct(upGuide, t0));

    // Stride along spine at intervals
    int socketCount = 0;
    float currentDist = spacing * 0.5f; // Start half-step from root
    int curSeg = 0;
    float distInSeg = currentDist;

    // Golden angle for biological leaf phyllotaxis
    const float GOLDEN_ANGLE = 137.5077f * DEG2RAD;
    float seedPhase = (float)(seed % 360) * DEG2RAD;

    Vector3 prevT = t0;
    Vector3 prevR = r0;
    Vector3 prevP = spinePts[0];

    while (currentDist < totalLen - spacing * 0.35f && socketCount < maxSockets)
    {
        // Find segment containing currentDist
        while (curSeg < segCount && distInSeg > segLengths[curSeg])
        {
            distInSeg -= segLengths[curSeg];

            // Propagate RMF to next point
            Vector3 p1 = spinePts[curSeg + 1];
            Vector3 v1 = Vector3Subtract(p1, prevP);
            float c1 = Vector3DotProduct(v1, v1);
            if (c1 > 1e-6f)
            {
                Vector3 nextT = (curSeg + 1 < segCount)
                    ? Vector3Normalize(Vector3Subtract(spinePts[curSeg + 2], p1))
                    : prevT;

                Vector3 rL = Vector3Subtract(prevR, Vector3Scale(v1, 2.0f / c1 * Vector3DotProduct(v1, prevR)));
                Vector3 tL = Vector3Subtract(prevT, Vector3Scale(v1, 2.0f / c1 * Vector3DotProduct(v1, prevT)));
                Vector3 v2 = Vector3Subtract(nextT, tL);
                float c2 = Vector3DotProduct(v2, v2);
                if (c2 > 1e-6f)
                {
                    prevR = Vector3Subtract(rL, Vector3Scale(v2, 2.0f / c2 * Vector3DotProduct(v2, rL)));
                }
                else
                {
                    prevR = rL;
                }
                prevT = nextT;
                prevP = p1;
            }

            curSeg++;
        }

        if (curSeg >= segCount) break;

        float segT = (segLengths[curSeg] > 1e-4f) ? (distInSeg / segLengths[curSeg]) : 0.0f;
        Vector3 spineP = Vector3Lerp(spinePts[curSeg], spinePts[curSeg + 1], segT);
        Vector3 spineTan = (segLengths[curSeg] > 1e-4f)
            ? Vector3Scale(Vector3Subtract(spinePts[curSeg + 1], spinePts[curSeg]), 1.0f / segLengths[curSeg])
            : prevT;

        float arc = currentDist / totalLen;
        // Natural taper of parent branch
        float localRadius = baseRadius * (1.0f - arc * 0.65f);
        if (localRadius < 0.015f) localRadius = 0.015f;

        // Circumferential angle from phyllotaxis
        float theta = seedPhase + (float)socketCount * GOLDEN_ANGLE;
        Vector3 binormal = Vector3CrossProduct(spineTan, prevR);
        Vector3 surfNorm = Vector3Normalize(Vector3Add(Vector3Scale(prevR, cosf(theta)), Vector3Scale(binormal, sinf(theta))));

        // Surface point where stem emerges
        Vector3 surfPos = Vector3Add(spineP, Vector3Scale(surfNorm, localRadius));

        outSockets[socketCount].pos = surfPos;
        outSockets[socketCount].normal = surfNorm;
        outSockets[socketCount].tangent = spineTan;
        outSockets[socketCount].arc = arc;
        outSockets[socketCount].stemRadius = localRadius;

        socketCount++;
        currentDist += spacing;
        distInSeg += spacing;
    }

    return socketCount;
}

#endif // VC_WOOD_SOCKET_INL
