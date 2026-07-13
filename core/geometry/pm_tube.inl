/* ===========================================================================
 * BEZIER CURVES & TUBE STREAM MESH
 * =========================================================================*/

Vector3 ProceduralMesh_BezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    float tt = t * t, uu = u * u;
    float uuu = uu * u, ttt = tt * t;

    Vector3 p = Vector3Scale(p0, uuu);
    p = Vector3Add(p, Vector3Scale(p1, 3.0f * uu * t));
    p = Vector3Add(p, Vector3Scale(p2, 3.0f * u * tt));
    p = Vector3Add(p, Vector3Scale(p3, ttt));
    return p;
}

Vector3 ProceduralMesh_BezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    Vector3 d = {0};
    d.x = 3.0f * u * u * (p1.x - p0.x) + 6.0f * u * t * (p2.x - p1.x) + 3.0f * t * t * (p3.x - p2.x);
    d.y = 3.0f * u * u * (p1.y - p0.y) + 6.0f * u * t * (p2.y - p1.y) + 3.0f * t * t * (p3.y - p2.y);
    d.z = 3.0f * u * u * (p1.z - p0.z) + 6.0f * u * t * (p2.z - p1.z) + 3.0f * t * t * (p3.z - p2.z);
    return d;
}

TubeMeshConfig ProceduralMesh_DefaultTubeConfig(void) {
    TubeMeshConfig cfg = {0};
    cfg.capsuleTailExp = 1.0f;
    cfg.tailTaperMin = 0.15f;
    cfg.tailTaperMax = 1.00f;
    cfg.headGrowth = 0.20f;
    cfg.wobbleAmplitude = 0.1f;
    cfg.wobbleFrequency = 4.0f;
    cfg.wobbleSpeed = 8.0f;
    cfg.deform1Amp = 0.12f;
    cfg.deform1FreqT = 18.0f;
    cfg.deform1FreqPhi = 3.0f;
    cfg.deform1Speed = 10.0f;
    cfg.deform2Amp = 0.08f;
    cfg.deform2FreqT = 9.0f;
    cfg.deform2FreqPhi = 5.0f;
    cfg.deform2Speed = 6.0f;
    cfg.tailApexFactor = 0.25f;
    cfg.headApexFactor = 0.80f;
    return cfg;
}

void ProceduralMesh_BuildTube(TubeMeshData *out, Vector3 p0, Vector3 p1,
                              Vector3 p2, Vector3 p3, float baseRadius,
                              float flowProgress, float time, int segments,
                              int radialSegs, const TubeMeshConfig *cfg) {
    if (out == NULL) return;

    TubeMeshConfig defaultCfg;
    if (cfg == NULL) { defaultCfg = ProceduralMesh_DefaultTubeConfig(); cfg = &defaultCfg; }

    if (segments > TUBE_MESH_MAX_SEGMENTS) segments = TUBE_MESH_MAX_SEGMENTS;
    if (radialSegs > TUBE_MESH_MAX_RADIAL) radialSegs = TUBE_MESH_MAX_RADIAL;
    out->segments = segments;
    out->radialSegs = radialSegs;
    out->tailApexFactor = cfg->tailApexFactor;
    out->headApexFactor = cfg->headApexFactor;

    float sinPhi[TUBE_MESH_MAX_RADIAL];
    float cosPhi[TUBE_MESH_MAX_RADIAL];
    for (int j = 0; j < radialSegs; j++) {
        float phi = (float)j * (2.0f * PI) / (float)radialSegs;
        sinPhi[j] = sinf(phi); cosPhi[j] = cosf(phi);
    }

    for (int i = 0; i <= segments; i++) {
        float t = (float)i / (float)segments;
        float currentT = t * flowProgress;

        Vector3 pos = ProceduralMesh_BezierPoint(p0, p1, p2, p3, currentT);
        Vector3 tangent = Vector3Normalize(ProceduralMesh_BezierTangent(p0, p1, p2, p3, currentT));

        Vector3 up = (Vector3){0.0f, 1.0f, 0.0f};
        if (fabsf(tangent.y) > 0.99f) up = (Vector3){1.0f, 0.0f, 0.0f};
        Vector3 right = Vector3Normalize(Vector3CrossProduct(up, tangent));
        up = Vector3CrossProduct(tangent, right);

        float baseCapsule = 0.3f + 0.7f * sqrtf(fmaxf(0.0f, sinf(t * PI))) * cfg->capsuleTailExp;
        float tailTaper = cfg->tailTaperMin + (cfg->tailTaperMax - cfg->tailTaperMin) * t;
        float capsuleCurve = baseCapsule * tailTaper;
        float headWeight = 1.0f + cfg->headGrowth * t;

        if (i == 0) { out->tailTangent = tangent; out->tailCenter = pos; }
        if (i == segments) { out->headTangent = tangent; out->headCenter = pos; }

        float wobble = cfg->wobbleAmplitude * sinf(t * PI * cfg->wobbleFrequency + time * cfg->wobbleSpeed);
        Vector3 twistedUp = Vector3Add(Vector3Scale(up, cosf(wobble)), Vector3Scale(right, sinf(wobble)));
        Vector3 twistedRight = Vector3Normalize(Vector3CrossProduct(twistedUp, tangent));

        for (int j = 0; j < radialSegs; j++) {
            Vector3 normal = Vector3Add(Vector3Scale(twistedRight, cosPhi[j]), Vector3Scale(twistedUp, sinPhi[j]));
            out->normals[i][j] = normal;

            float phi = (float)j * (2.0f * PI) / (float)radialSegs;
            float deform1 = sinf(t * cfg->deform1FreqT + phi * cfg->deform1FreqPhi + time * cfg->deform1Speed);
            float deform2 = sinf(t * cfg->deform2FreqT - phi * cfg->deform2FreqPhi - time * cfg->deform2Speed);
            float deform = 1.0f + deform1 * cfg->deform1Amp + deform2 * cfg->deform2Amp;

            float finalRadius = baseRadius * capsuleCurve * headWeight * deform;
            out->rings[i][j] = Vector3Add(pos, Vector3Scale(normal, finalRadius));
        }

        if (i == 0) out->tailRadius = baseRadius * capsuleCurve * headWeight;
        if (i == segments) out->headRadius = baseRadius * capsuleCurve * headWeight;
    }
}

static void SamplePathPositionAndTangent(const Vector3 *path, int pathCount, float t, Vector3 *outPos, Vector3 *outTangent) {
    if (pathCount <= 0) return;
    if (pathCount == 1) {
        *outPos = path[0];
        *outTangent = (Vector3){0.0f, 1.0f, 0.0f};
        return;
    }
    
    float segLengths[128];
    float totalLength = 0.0f;
    int limit = pathCount - 1;
    if (limit > 127) limit = 127;
    
    for (int i = 0; i < limit; i++) {
        segLengths[i] = Vector3Distance(path[i], path[i+1]);
        totalLength += segLengths[i];
    }
    
    if (totalLength <= 0.0f) {
        *outPos = path[0];
        *outTangent = (Vector3){0.0f, 1.0f, 0.0f};
        return;
    }
    
    float targetDist = t * totalLength;
    float currentDist = 0.0f;
    
    for (int i = 0; i < limit; i++) {
        if (currentDist + segLengths[i] >= targetDist) {
            float localT = (segLengths[i] > 0.0f) ? ((targetDist - currentDist) / segLengths[i]) : 0.0f;
            *outPos = Vector3Lerp(path[i], path[i+1], localT);
            *outTangent = Vector3Normalize(Vector3Subtract(path[i+1], path[i]));
            return;
        }
        currentDist += segLengths[i];
    }
    
    *outPos = path[pathCount - 1];
    *outTangent = Vector3Normalize(Vector3Subtract(path[pathCount - 1], path[pathCount - 2]));
}

void ProceduralMesh_BuildTubeAlongPath(TubeMeshData *out, const Vector3 *pathPoints,
                                      int pathCount, float baseRadius,
                                      float startT, float endT, float time,
                                      int segments, int radialSegs,
                                      const TubeMeshConfig *cfg) {
    if (out == NULL || pathPoints == NULL || pathCount <= 0) return;

    TubeMeshConfig defaultCfg;
    if (cfg == NULL) { defaultCfg = ProceduralMesh_DefaultTubeConfig(); cfg = &defaultCfg; }

    if (segments > TUBE_MESH_MAX_SEGMENTS) segments = TUBE_MESH_MAX_SEGMENTS;
    if (radialSegs > TUBE_MESH_MAX_RADIAL) radialSegs = TUBE_MESH_MAX_RADIAL;
    out->segments = segments;
    out->radialSegs = radialSegs;
    out->tailApexFactor = cfg->tailApexFactor;
    out->headApexFactor = cfg->headApexFactor;

    float sinPhi[TUBE_MESH_MAX_RADIAL];
    float cosPhi[TUBE_MESH_MAX_RADIAL];
    for (int j = 0; j < radialSegs; j++) {
        float phi = (float)j * (2.0f * PI) / (float)radialSegs;
        sinPhi[j] = sinf(phi); cosPhi[j] = cosf(phi);
    }

    for (int i = 0; i <= segments; i++) {
        float t = (float)i / (float)segments;
        float currentT = startT + t * (endT - startT);
        if (currentT < 0.0f) currentT = 0.0f;
        if (currentT > 1.0f) currentT = 1.0f;

        Vector3 pos;
        Vector3 tangent;
        SamplePathPositionAndTangent(pathPoints, pathCount, currentT, &pos, &tangent);

        Vector3 up = (Vector3){0.0f, 1.0f, 0.0f};
        if (fabsf(tangent.y) > 0.99f) up = (Vector3){1.0f, 0.0f, 0.0f};
        Vector3 right = Vector3Normalize(Vector3CrossProduct(up, tangent));
        up = Vector3CrossProduct(tangent, right);

        float baseCapsule = 0.3f + 0.7f * sqrtf(fmaxf(0.0f, sinf(t * PI))) * cfg->capsuleTailExp;
        float tailTaper = cfg->tailTaperMin + (cfg->tailTaperMax - cfg->tailTaperMin) * t;
        float capsuleCurve = baseCapsule * tailTaper;
        float headWeight = 1.0f + cfg->headGrowth * t;

        if (i == 0) { out->tailTangent = tangent; out->tailCenter = pos; }
        if (i == segments) { out->headTangent = tangent; out->headCenter = pos; }

        float wobble = cfg->wobbleAmplitude * sinf(t * PI * cfg->wobbleFrequency + time * cfg->wobbleSpeed);
        Vector3 twistedUp = Vector3Add(Vector3Scale(up, cosf(wobble)), Vector3Scale(right, sinf(wobble)));
        Vector3 twistedRight = Vector3Normalize(Vector3CrossProduct(twistedUp, tangent));

        for (int j = 0; j < radialSegs; j++) {
            Vector3 normal = Vector3Add(Vector3Scale(twistedRight, cosPhi[j]), Vector3Scale(twistedUp, sinPhi[j]));
            out->normals[i][j] = normal;

            float phi = (float)j * (2.0f * PI) / (float)radialSegs;
            float deform1 = sinf(t * cfg->deform1FreqT + phi * cfg->deform1FreqPhi + time * cfg->deform1Speed);
            float deform2 = sinf(t * cfg->deform2FreqT - phi * cfg->deform2FreqPhi - time * cfg->deform2Speed);
            float deform = 1.0f + deform1 * cfg->deform1Amp + deform2 * cfg->deform2Amp;

            float finalRadius = baseRadius * capsuleCurve * headWeight * deform;
            out->rings[i][j] = Vector3Add(pos, Vector3Scale(normal, finalRadius));
        }

        if (i == 0) out->tailRadius = baseRadius * capsuleCurve * headWeight;
        if (i == segments) out->headRadius = baseRadius * capsuleCurve * headWeight;
    }
}


void ProceduralMesh_DrawTube(const TubeMeshData *data, float uvLengthScale) {
    if (data == NULL) return;

    const int segments = data->segments;
    const int radialSegs = data->radialSegs;

    rlPushMatrix();
    rlCheckRenderBatchLimit(segments * radialSegs * 4);
    rlBegin(RL_QUADS);
    for (int i = 0; i < segments; i++) {
        float v1 = (float)i / (float)segments * uvLengthScale;
        float v2 = (float)(i + 1) / (float)segments * uvLengthScale;

        for (int j = 0; j < radialSegs; j++) {
            int nextJ = (j + 1) % radialSegs;
            float u1 = (float)j / (float)radialSegs;
            float u2 = (float)nextJ / (float)radialSegs;

            rlNormal3f(data->normals[i][j].x, data->normals[i][j].y, data->normals[i][j].z);
            rlTexCoord2f(u1, v1); rlVertex3f(data->rings[i][j].x, data->rings[i][j].y, data->rings[i][j].z);

            rlNormal3f(data->normals[i][nextJ].x, data->normals[i][nextJ].y, data->normals[i][nextJ].z);
            rlTexCoord2f(u2, v1); rlVertex3f(data->rings[i][nextJ].x, data->rings[i][nextJ].y, data->rings[i][nextJ].z);

            rlNormal3f(data->normals[i + 1][nextJ].x, data->normals[i + 1][nextJ].y, data->normals[i + 1][nextJ].z);
            rlTexCoord2f(u2, v2); rlVertex3f(data->rings[i + 1][nextJ].x, data->rings[i + 1][nextJ].y, data->rings[i + 1][nextJ].z);

            rlNormal3f(data->normals[i + 1][j].x, data->normals[i + 1][j].y, data->normals[i + 1][j].z);
            rlTexCoord2f(u1, v2); rlVertex3f(data->rings[i + 1][j].x, data->rings[i + 1][j].y, data->rings[i + 1][j].z);
        }
    }
    rlEnd();

    rlCheckRenderBatchLimit(radialSegs * 6);
    rlBegin(RL_TRIANGLES);

    Vector3 tailApex = Vector3Subtract(data->tailCenter, Vector3Scale(data->tailTangent, data->tailRadius * data->tailApexFactor));
    float tailV_apex = -0.1f;
    for (int j = 0; j < radialSegs; j++) {
        int nextJ = (j + 1) % radialSegs;
        float uCenter = ((float)j / radialSegs + (float)nextJ / radialSegs) * 0.5f;

        rlNormal3f(-data->tailTangent.x, -data->tailTangent.y, -data->tailTangent.z);
        rlTexCoord2f(uCenter, tailV_apex); rlVertex3f(tailApex.x, tailApex.y, tailApex.z);
        rlNormal3f(data->normals[0][j].x, data->normals[0][j].y, data->normals[0][j].z);
        rlTexCoord2f((float)j / radialSegs, 0.0f); rlVertex3f(data->rings[0][j].x, data->rings[0][j].y, data->rings[0][j].z);
        rlNormal3f(data->normals[0][nextJ].x, data->normals[0][nextJ].y, data->normals[0][nextJ].z);
        rlTexCoord2f((float)nextJ / radialSegs, 0.0f); rlVertex3f(data->rings[0][nextJ].x, data->rings[0][nextJ].y, data->rings[0][nextJ].z);
    }

    Vector3 headApex = Vector3Add(data->headCenter, Vector3Scale(data->headTangent, data->headRadius * data->headApexFactor));
    float headV_apex = uvLengthScale + 0.1f;
    for (int j = 0; j < radialSegs; j++) {
        int nextJ = (j + 1) % radialSegs;
        float uCenter = ((float)j / radialSegs + (float)nextJ / radialSegs) * 0.5f;

        Vector3 avgNormal1 = Vector3Normalize(Vector3Add(data->normals[segments][j], data->headTangent));
        Vector3 avgNormal2 = Vector3Normalize(Vector3Add(data->normals[segments][nextJ], data->headTangent));

        rlNormal3f(data->headTangent.x, data->headTangent.y, data->headTangent.z);
        rlTexCoord2f(uCenter, headV_apex); rlVertex3f(headApex.x, headApex.y, headApex.z);
        rlNormal3f(avgNormal1.x, avgNormal1.y, avgNormal1.z);
        rlTexCoord2f((float)j / radialSegs, uvLengthScale); rlVertex3f(data->rings[segments][j].x, data->rings[segments][j].y, data->rings[segments][j].z);
        rlNormal3f(avgNormal2.x, avgNormal2.y, avgNormal2.z);
        rlTexCoord2f((float)nextJ / radialSegs, uvLengthScale); rlVertex3f(data->rings[segments][nextJ].x, data->rings[segments][nextJ].y, data->rings[segments][nextJ].z);
    }
    rlEnd();
    rlPopMatrix();
}