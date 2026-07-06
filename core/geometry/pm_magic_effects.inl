/* ===========================================================================
 * VORTEX FUNNEL & FISSURE
 * =========================================================================*/

VortexFunnelConfig ProceduralMesh_DefaultVortexFunnelConfig(void) {
    VortexFunnelConfig cfg = {0};
    cfg.topRadius = 140.0f; cfg.bottomRadius = 25.0f; cfg.height = 260.0f;
    cfg.twistAmount = 320.0f; cfg.ridgeCount = 5; cfg.ridgeAmount = 0.12f;
    return cfg;
}

void ProceduralMesh_BuildVortexFunnel(VortexFunnelMeshData *out, Vector3 center, const VortexFunnelConfig *cfg, int heightSegs, int radialSegs, float time) {
    if (out == NULL) return;
    VortexFunnelConfig defaultCfg;
    if (cfg == NULL) { defaultCfg = ProceduralMesh_DefaultVortexFunnelConfig(); cfg = &defaultCfg; }

    if (heightSegs > VORTEX_FUNNEL_MAX_HEIGHT_SEGS) heightSegs = VORTEX_FUNNEL_MAX_HEIGHT_SEGS;
    if (radialSegs > VORTEX_FUNNEL_MAX_RADIAL_SEGS) radialSegs = VORTEX_FUNNEL_MAX_RADIAL_SEGS;
    if (heightSegs < 1) heightSegs = 1; if (radialSegs < 3) radialSegs = 3;
    out->heightSegs = heightSegs; out->radialSegs = radialSegs;

    float twistRad = cfg->twistAmount * (PI / 180.0f);
    float sinPhi[VORTEX_FUNNEL_MAX_RADIAL_SEGS];
    float cosPhi[VORTEX_FUNNEL_MAX_RADIAL_SEGS];
    for (int j = 0; j < radialSegs; j++) {
        float phi = (float)j * (2.0f * PI) / radialSegs;
        sinPhi[j] = sinf(phi); cosPhi[j] = cosf(phi);
    }

    for (int i = 0; i <= heightSegs; i++) {
        float t = (float)i / heightSegs; 
        float ringRadius = cfg->bottomRadius + (cfg->topRadius - cfg->bottomRadius) * t;
        float twistAngle = twistRad * t + time * 2.0f;
        float tc = cosf(twistAngle), ts = sinf(twistAngle);

        for (int j = 0; j < radialSegs; j++) {
            float ridge = 1.0f + cfg->ridgeAmount * cosf((float)cfg->ridgeCount * (((float)j * 2.0f * PI / radialSegs) + twistAngle));
            float r = ringRadius * ridge;
            float lx = cosPhi[j] * r, lz = sinPhi[j] * r;

            out->rings[i][j] = (Vector3){center.x + (lx * tc - lz * ts), center.y + (t * cfg->height), center.z + (lx * ts + lz * tc)};
            out->normals[i][j] = (Vector3){cosPhi[j] * tc - sinPhi[j] * ts, 0.0f, cosPhi[j] * ts + sinPhi[j] * tc};
        }
    }
}

void ProceduralMesh_DrawVortexFunnel(const VortexFunnelMeshData *data, Color color) {
    if (data == NULL) return;
    rlCheckRenderBatchLimit(data->heightSegs * data->radialSegs * 4);
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < data->heightSegs; i++) {
        for (int j = 0; j < data->radialSegs; j++) {
            int nextJ = (j + 1) % data->radialSegs;
            rlNormal3f(data->normals[i][j].x, data->normals[i][j].y, data->normals[i][j].z);
            rlVertex3f(data->rings[i][j].x, data->rings[i][j].y, data->rings[i][j].z);
            rlNormal3f(data->normals[i][nextJ].x, data->normals[i][nextJ].y, data->normals[i][nextJ].z);
            rlVertex3f(data->rings[i][nextJ].x, data->rings[i][nextJ].y, data->rings[i][nextJ].z);
            rlNormal3f(data->normals[i + 1][nextJ].x, data->normals[i + 1][nextJ].y, data->normals[i + 1][nextJ].z);
            rlVertex3f(data->rings[i + 1][nextJ].x, data->rings[i + 1][nextJ].y, data->rings[i + 1][nextJ].z);
            rlNormal3f(data->normals[i + 1][j].x, data->normals[i + 1][j].y, data->normals[i + 1][j].z);
            rlVertex3f(data->rings[i + 1][j].x, data->rings[i + 1][j].y, data->rings[i + 1][j].z);
        }
    }
    rlEnd();
}

void ProceduralMesh_BuildFissure(FissureMeshData *out, const Vector3 *pathPoints, int pathPointCount, float width, float depth, float jaggedness, int seed) {
    if (out == NULL || pathPoints == NULL || pathPointCount < 2) return;

    Vector3 centerline[FISSURE_MAX_SEGMENTS + 1];
    int sampleCount = SamplePath(pathPoints, pathPointCount, fmaxf(width * 0.5f, 1.0f), centerline, FISSURE_MAX_SEGMENTS + 1);
    if (sampleCount < 2) { out->segments = 0; return; }
    out->segments = sampleCount - 1;

    float hw = width * 0.5f;

    for (int i = 0; i <= out->segments; i++) {
        Vector3 tangent = Vector3Normalize(Vector3Subtract(centerline[(i < out->segments) ? i + 1 : i], centerline[(i > 0) ? i - 1 : i]));
        if (Vector3LengthSqr(tangent) < 0.0001f) tangent = (Vector3){1.0f, 0.0f, 0.0f};
        Vector3 across = Vector3Normalize(Vector3CrossProduct((Vector3){0.0f, 1.0f, 0.0f}, tangent));

        Vector3 centerJ = Vector3Add(centerline[i], Vector3Scale(across, ProceduralMesh__Noise2(i, 0, seed) * hw * 0.3f * jaggedness));
        float dBottom = -depth + ProceduralMesh__Noise2(i, 3, seed) * depth * 0.3f * jaggedness;
        float dShoulder = -depth * 0.35f;

        out->verts[i][0] = Vector3Add(centerJ, Vector3Scale(across, -hw - ProceduralMesh__Noise2(i, 1, seed) * hw * 0.25f * jaggedness));
        out->verts[i][1] = Vector3Add(centerJ, Vector3Scale(across, -hw * 0.45f)); out->verts[i][1].y += dShoulder + ProceduralMesh__Noise2(i, 4, seed) * depth * 0.2f * jaggedness;
        out->verts[i][2] = centerJ; out->verts[i][2].y += dBottom;
        out->verts[i][3] = Vector3Add(centerJ, Vector3Scale(across, hw * 0.45f)); out->verts[i][3].y += dShoulder + ProceduralMesh__Noise2(i, 5, seed) * depth * 0.2f * jaggedness;
        out->verts[i][4] = Vector3Add(centerJ, Vector3Scale(across, hw + ProceduralMesh__Noise2(i, 2, seed) * hw * 0.25f * jaggedness));
    }

    for (int i = 0; i <= out->segments; i++) {
        for (int c = 0; c < FISSURE_CROSS_VERTS; c++) {
            Vector3 tangentC = Vector3Subtract(out->verts[i][(c < FISSURE_CROSS_VERTS - 1) ? c + 1 : c], out->verts[i][(c > 0) ? c - 1 : c]);
            Vector3 tangentI = Vector3Subtract(out->verts[(i < out->segments) ? i + 1 : i][c], out->verts[(i > 0) ? i - 1 : i][c]);
            Vector3 normal = Vector3Normalize(Vector3CrossProduct(tangentI, tangentC));
            if (normal.y < 0.0f) normal = Vector3Negate(normal);
            out->normals[i][c] = normal;
        }
    }
}

void ProceduralMesh_DrawFissure(const FissureMeshData *data, Color color) {
    if (data == NULL || data->segments < 1) return;
    rlCheckRenderBatchLimit(data->segments * (FISSURE_CROSS_VERTS - 1) * 4);
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < data->segments; i++) {
        for (int c = 0; c < FISSURE_CROSS_VERTS - 1; c++) {
            rlNormal3f(data->normals[i][c].x, data->normals[i][c].y, data->normals[i][c].z); rlVertex3f(data->verts[i][c].x, data->verts[i][c].y, data->verts[i][c].z);
            rlNormal3f(data->normals[i][c + 1].x, data->normals[i][c + 1].y, data->normals[i][c + 1].z); rlVertex3f(data->verts[i][c + 1].x, data->verts[i][c + 1].y, data->verts[i][c + 1].z);
            rlNormal3f(data->normals[i + 1][c + 1].x, data->normals[i + 1][c + 1].y, data->normals[i + 1][c + 1].z); rlVertex3f(data->verts[i + 1][c + 1].x, data->verts[i + 1][c + 1].y, data->verts[i + 1][c + 1].z);
            rlNormal3f(data->normals[i + 1][c].x, data->normals[i + 1][c].y, data->normals[i + 1][c].z); rlVertex3f(data->verts[i + 1][c].x, data->verts[i + 1][c].y, data->verts[i + 1][c].z);
        }
    }
    rlEnd();
}