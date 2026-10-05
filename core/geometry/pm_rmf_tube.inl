/* ===========================================================================
 * PM_RMF_TUBE.INL — Procedural Wood Growth Tube with Rotation Minimizing Frames
 *
 * Implements:
 *   1. Wang et al. 2008 Double Reflection RMF to eliminate Frenet flipping.
 *   2. Botanical radius model: taper (1-t)^k + root flare + bark knot bumps.
 *   3. Vertex shader & CPU growth dynamics (aArc, aBirth, aSpan, tip profile).
 *   4. Zero dynamic allocation (static buffers, batch-safe rlgl submission).
 * ===========================================================================*/

#ifndef PI
#define PI 3.14159265358979323846f
#endif

PMRmfTubeConfig PMRmfTube_DefaultConfig(void)
{
    PMRmfTubeConfig cfg;
    cfg.baseRadius = 0.08f;
    cfg.taperPow = 1.0f;
    cfg.tipRadiusFrac = 0.05f;
    cfg.rootFlare = 0.40f;
    cfg.knotAmp = 0.12f;
    cfg.knotFreq = 6.0f;
    cfg.noiseAmp = 0.06f;
    cfg.segments = 24;
    cfg.radialSegs = 6;
    cfg.growth = 1.0f;
    cfg.birth = 0.0f;
    cfg.span = 1.0f;
    cfg.tipLength = 0.08f;
    cfg.swayAmp = 0.0f;
    cfg.time = 0.0f;
    cfg.seed = 12345;
    return cfg;
}

/* Sample a piecewise Catmull-Rom or linear path at arc parameter t in [0, 1] */
static void PMRmf_SamplePath(const Vector3 *pts, int count, float t,
                             Vector3 *outPos, Vector3 *outTangent)
{
    if (count <= 1 || pts == NULL)
    {
        if (pts != NULL && count == 1) {
            *outPos = pts[0];
            *outTangent = (Vector3){0.0f, 1.0f, 0.0f};
        } else {
            *outPos = (Vector3){0};
            *outTangent = (Vector3){0.0f, 1.0f, 0.0f};
        }
        return;
    }

    if (t <= 0.0f) {
        *outPos = pts[0];
        *outTangent = Vector3Normalize(Vector3Subtract(pts[1], pts[0]));
        return;
    }
    if (t >= 1.0f) {
        *outPos = pts[count - 1];
        *outTangent = Vector3Normalize(Vector3Subtract(pts[count - 1], pts[count - 2]));
        return;
    }

    // Map t [0..1] across (count - 1) segments
    float fSeg = t * (float)(count - 1);
    int idx = (int)fSeg;
    if (idx >= count - 1) idx = count - 2;
    float localT = fSeg - (float)idx;

    // Catmull-Rom 4-point neighborhood
    Vector3 p0 = (idx > 0) ? pts[idx - 1] : Vector3Subtract(pts[idx], Vector3Subtract(pts[idx + 1], pts[idx]));
    Vector3 p1 = pts[idx];
    Vector3 p2 = pts[idx + 1];
    Vector3 p3 = (idx + 2 < count) ? pts[idx + 2] : Vector3Add(pts[idx + 1], Vector3Subtract(pts[idx + 1], pts[idx]));

    // Hermite evaluation for position
    float t2 = localT * localT;
    float t3 = t2 * localT;

    float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 = t3 - 2.0f * t2 + localT;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 = t3 - t2;

    Vector3 m1 = Vector3Scale(Vector3Subtract(p2, p0), 0.5f);
    Vector3 m2 = Vector3Scale(Vector3Subtract(p3, p1), 0.5f);

    outPos->x = h00 * p1.x + h10 * m1.x + h01 * p2.x + h11 * m2.x;
    outPos->y = h00 * p1.y + h10 * m1.y + h01 * p2.y + h11 * m2.y;
    outPos->z = h00 * p1.z + h10 * m1.z + h01 * p2.z + h11 * m2.z;

    // Hermite derivative for tangent
    float dh00 = 6.0f * t2 - 6.0f * localT;
    float dh10 = 3.0f * t2 - 4.0f * localT + 1.0f;
    float dh01 = -6.0f * t2 + 6.0f * localT;
    float dh11 = 3.0f * t2 - 2.0f * localT;

    Vector3 d;
    d.x = dh00 * p1.x + dh10 * m1.x + dh01 * p2.x + dh11 * m2.x;
    d.y = dh00 * p1.y + dh10 * m1.y + dh01 * p2.y + dh11 * m2.y;
    d.z = dh00 * p1.z + dh10 * m1.z + dh01 * p2.z + dh11 * m2.z;

    float lenSqr = Vector3LengthSqr(d);
    if (lenSqr > 1e-8f) {
        *outTangent = Vector3Scale(d, 1.0f / sqrtf(lenSqr));
    } else {
        *outTangent = Vector3Normalize(Vector3Subtract(p2, p1));
    }
}

/* Wang et al. 2008 Double Reflection Rotation Minimizing Frame */
void PMRmf_BuildFrames(const Vector3 *positions, const Vector3 *tangents, int count,
                       Vector3 *outRight, Vector3 *outUp)
{
    if (count <= 0) return;

    // Initial frame at i = 0
    Vector3 t0 = tangents[0];
    Vector3 ref = (fabsf(t0.y) < 0.90f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
    outRight[0] = Vector3Normalize(Vector3CrossProduct(ref, t0));
    outUp[0] = Vector3CrossProduct(t0, outRight[0]);

    for (int i = 0; i < count - 1; i++)
    {
        Vector3 x0 = positions[i];
        Vector3 x1 = positions[i + 1];
        Vector3 t_prev = tangents[i];
        Vector3 t_next = tangents[i + 1];
        Vector3 r_prev = outRight[i];

        Vector3 v1 = Vector3Subtract(x1, x0);
        float c1 = Vector3DotProduct(v1, v1);

        if (c1 > 1e-8f)
        {
            // Reflection 1: across bisector of (x1 - x0)
            float k1 = 2.0f / c1;
            Vector3 rL = Vector3Subtract(r_prev, Vector3Scale(v1, k1 * Vector3DotProduct(v1, r_prev)));
            Vector3 tL = Vector3Subtract(t_prev, Vector3Scale(v1, k1 * Vector3DotProduct(v1, t_prev)));

            // Reflection 2: across bisector of (t_next - tL)
            Vector3 v2 = Vector3Subtract(t_next, tL);
            float c2 = Vector3DotProduct(v2, v2);

            Vector3 r_next;
            if (c2 > 1e-8f) {
                float k2 = 2.0f / c2;
                r_next = Vector3Subtract(rL, Vector3Scale(v2, k2 * Vector3DotProduct(v2, rL)));
            } else {
                r_next = rL;
            }

            // Gram-Schmidt orthogonalization to ensure stability
            Vector3 ortho = Vector3Subtract(r_next, Vector3Scale(t_next, Vector3DotProduct(r_next, t_next)));
            float orthoLen = Vector3LengthSqr(ortho);
            if (orthoLen > 1e-8f) {
                outRight[i + 1] = Vector3Scale(ortho, 1.0f / sqrtf(orthoLen));
            } else {
                outRight[i + 1] = r_prev;
            }
        }
        else
        {
            outRight[i + 1] = r_prev;
        }

        outUp[i + 1] = Vector3Normalize(Vector3CrossProduct(tangents[i + 1], outRight[i + 1]));
    }
}

/* Build complete wood tube mesh using RMF and botanical growth formulas */
void PMRmf_BuildTube(const Vector3 *pathPoints, int pathCount,
                     const PMRmfTubeConfig *cfg, PMRmfTubeMesh *outMesh)
{
    if (pathPoints == NULL || pathCount < 2 || outMesh == NULL || cfg == NULL) return;

    int segments = cfg->segments;
    if (segments < 4) segments = 4;
    if (segments > TUBE_MESH_MAX_SEGMENTS) segments = TUBE_MESH_MAX_SEGMENTS;

    int radialSegs = cfg->radialSegs;
    if (radialSegs < 3) radialSegs = 3;
    if (radialSegs > TUBE_MESH_MAX_RADIAL) radialSegs = TUBE_MESH_MAX_RADIAL;

    outMesh->segments = segments;
    outMesh->radialSegs = radialSegs;

    // 1. Sample spine positions and tangents along path
    Vector3 spinePos[TUBE_MESH_MAX_SEGMENTS + 1];
    Vector3 spineTan[TUBE_MESH_MAX_SEGMENTS + 1];
    Vector3 rmfRight[TUBE_MESH_MAX_SEGMENTS + 1];
    Vector3 rmfUp[TUBE_MESH_MAX_SEGMENTS + 1];

    for (int i = 0; i <= segments; i++)
    {
        float arc = (float)i / (float)segments;
        outMesh->arcs[i] = arc;
        PMRmf_SamplePath(pathPoints, pathCount, arc, &spinePos[i], &spineTan[i]);
    }

    // 2. Generate Rotation Minimizing Frames along entire spine
    PMRmf_BuildFrames(spinePos, spineTan, segments + 1, rmfRight, rmfUp);

    // 3. Precompute radial unit angles
    float cosPhi[TUBE_MESH_MAX_RADIAL];
    float sinPhi[TUBE_MESH_MAX_RADIAL];
    for (int j = 0; j < radialSegs; j++)
    {
        float phi = (float)j * (2.0f * PI) / (float)radialSegs;
        cosPhi[j] = cosf(phi);
        sinPhi[j] = sinf(phi);
    }

    // 4. Compute rings, centers, and vertex normals
    float birth = cfg->birth;
    float span = (cfg->span > 0.001f) ? cfg->span : 0.001f;
    float growth = cfg->growth;
    float tipLength = (cfg->tipLength > 0.001f) ? cfg->tipLength : 0.05f;

    for (int i = 0; i <= segments; i++)
    {
        float arc = outMesh->arcs[i];
        
        // Growth factor [0..1]
        float localProgress = (growth - birth) / span;
        if (localProgress < 0.0f) localProgress = 0.0f;
        if (localProgress > 1.0f) localProgress = 1.0f;

        float k = 0.0f;
        if (arc > localProgress) {
            k = 0.0f;
        } else if (arc < localProgress - tipLength) {
            k = 1.0f;
        } else {
            float tNorm = (arc - (localProgress - tipLength)) / tipLength;
            k = 1.0f - (tNorm * tNorm * (3.0f - 2.0f * tNorm)); // smoothstep
        }

        // Sway displacement
        Vector3 sway = (Vector3){0};
        if (cfg->swayAmp > 1e-4f)
        {
            float arcWeight = arc * arc;
            float phase = cfg->time * 2.2f - arc * 3.5f;
            sway.x = (sinf(phase) * 0.7f + sinf(phase * 0.5f + 1.2f) * 0.3f) * (arcWeight * cfg->swayAmp);
            sway.z = cosf(phase * 0.8f + 0.5f) * (arcWeight * cfg->swayAmp);
        }

        Vector3 center = Vector3Add(spinePos[i], sway);
        outMesh->centers[i] = center;

        // Radius profile computation
        // a) Taper: (1 - arc)^taperPow
        float taper = cfg->tipRadiusFrac + (1.0f - cfg->tipRadiusFrac) * powf(1.0f - arc, cfg->taperPow);
        // b) Root flare: 1.0 + rootFlare * (1 - arc / 0.15)^2 when arc < 0.15
        float flare = 1.0f;
        if (arc < 0.15f) {
            float f = (0.15f - arc) / 0.15f;
            flare = 1.0f + cfg->rootFlare * f * f;
        }
        // c) Bark knots: 1.0 + knotAmp * sin^4(knotFreq * 2*PI * arc)
        float sKnot = sinf(cfg->knotFreq * (2.0f * PI) * arc);
        float sKnot4 = sKnot * sKnot * sKnot * sKnot;
        float knot = 1.0f + cfg->knotAmp * sKnot4;

        float nominalRadius = cfg->baseRadius * taper * flare * knot;
        if (nominalRadius < 0.001f) nominalRadius = 0.001f;

        // Growth shrinkage: tip collapses quadratically towards center
        float grownRadius = nominalRadius * (k * k);

        Vector3 R = rmfRight[i];
        Vector3 U = rmfUp[i];

        for (int j = 0; j < radialSegs; j++)
        {
            float phi = (float)j * (2.0f * PI) / (float)radialSegs;
            // Botanical corded sinews (demon vine fluting: 3 twisted cords spiral along length)
            float fluting = 1.0f + 0.12f * cosf(phi * 3.0f + arc * 14.0f);
            float dFluting_dPhi = -0.36f * sinf(phi * 3.0f + arc * 14.0f);

            // Organic micro-roughness per vertex
            float noiseVal = ProceduralMesh__Noise2(i, j, (int)cfg->seed);
            float rEff = grownRadius * fluting * (1.0f + cfg->noiseAmp * noiseVal);
            if (rEff < 0.0f) rEff = 0.0f;

            Vector3 radialDir;
            radialDir.x = R.x * cosPhi[j] + U.x * sinPhi[j];
            radialDir.y = R.y * cosPhi[j] + U.y * sinPhi[j];
            radialDir.z = R.z * cosPhi[j] + U.z * sinPhi[j];
            radialDir = Vector3Normalize(radialDir);

            // Tangent along circumferential ring
            Vector3 ringTan;
            ringTan.x = -R.x * sinPhi[j] + U.x * cosPhi[j];
            ringTan.y = -R.y * sinPhi[j] + U.y * cosPhi[j];
            ringTan.z = -R.z * sinPhi[j] + U.z * cosPhi[j];
            ringTan = Vector3Normalize(ringTan);

            // Fluted surface normal: N ~ radialDir - (dFluting/dPhi / fluting) * ringTan
            Vector3 perturbedN = Vector3Subtract(radialDir, Vector3Scale(ringTan, dFluting_dPhi / fluting));
            outMesh->normals[i][j] = Vector3Normalize(perturbedN);
            outMesh->rings[i][j] = Vector3Add(center, Vector3Scale(radialDir, rEff));
        }
    }
}

/* Immediate rlgl drawing for the RMF wood tube */
void PMRmf_Draw(const PMRmfTubeMesh *mesh, Color tint, float uvVScale, float uvVOffset)
{
    if (mesh == NULL || mesh->segments < 1 || mesh->radialSegs < 3) return;

    int segments = mesh->segments;
    int radialSegs = mesh->radialSegs;

    rlPushMatrix();
    rlCheckRenderBatchLimit(segments * radialSegs * 4);
    rlBegin(RL_QUADS);

    for (int i = 0; i < segments; i++)
    {
        float arc1 = mesh->arcs[i];
        float arc2 = mesh->arcs[i + 1];
        float v1 = arc1 * uvVScale + uvVOffset;
        float v2 = arc2 * uvVScale + uvVOffset;

        for (int j = 0; j < radialSegs; j++)
        {
            int nextJ = (j + 1) % radialSegs;
            float u1 = (float)j / (float)radialSegs;
            float u2 = (float)(j + 1) / (float)radialSegs;

            // Vertex 0: (i, j)
            rlColor4ub(tint.r, tint.g, tint.b, tint.a);
            rlNormal3f(mesh->normals[i][j].x, mesh->normals[i][j].y, mesh->normals[i][j].z);
            rlTexCoord2f(u1, v1);
            rlVertex3f(mesh->rings[i][j].x, mesh->rings[i][j].y, mesh->rings[i][j].z);

            // Vertex 1: (i, nextJ)
            rlNormal3f(mesh->normals[i][nextJ].x, mesh->normals[i][nextJ].y, mesh->normals[i][nextJ].z);
            rlTexCoord2f(u2, v1);
            rlVertex3f(mesh->rings[i][nextJ].x, mesh->rings[i][nextJ].y, mesh->rings[i][nextJ].z);

            // Vertex 2: (i + 1, nextJ)
            rlNormal3f(mesh->normals[i + 1][nextJ].x, mesh->normals[i + 1][nextJ].y, mesh->normals[i + 1][nextJ].z);
            rlTexCoord2f(u2, v2);
            rlVertex3f(mesh->rings[i + 1][nextJ].x, mesh->rings[i + 1][nextJ].y, mesh->rings[i + 1][nextJ].z);

            // Vertex 3: (i + 1, j)
            rlNormal3f(mesh->normals[i + 1][j].x, mesh->normals[i + 1][j].y, mesh->normals[i + 1][j].z);
            rlTexCoord2f(u1, v2);
            rlVertex3f(mesh->rings[i + 1][j].x, mesh->rings[i + 1][j].y, mesh->rings[i + 1][j].z);
        }
    }

    rlEnd();
    rlPopMatrix();
}
