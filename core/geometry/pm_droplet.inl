/* ===========================================================================
 * GIỌT NƯỚC — mesh độc lập
 *
 * Mũi nhọn ở ĐUÔI (luật luỹ thừa) nối liền CHỎM CẦU ở đầu. Bất đối xứng — đó là thứ làm nó là giọt nước. Tự khép hai đầu nên KHÔNG có nắp.
 *
 *      r(t) = (t/h)^p  |  sqrt(1-u^2)
 *
 * ĐỘC LẬP HOÀN TOÀN. File này không dùng chung gì với pm_tube/pm_droplet/
 * pm_capsule còn lại: cấu hình, dữ liệu mesh, dựng và vẽ đều của riêng nó. Một
 * đường bao duy nhất bị bẻ qua tham số để phục vụ cả ba hình chính là thứ vừa
 * bị thay thế — nó không cho ra hình nào đúng cả.
 *
 * KHÔNG CÓ NẮP. Nắp cũ là hai quạt tam giác có đỉnh đẩy ra theo tiếp tuyến,
 * tức hai hình NÓN — cái "đầu bút chì". Hình này tự khép bằng chính đường bao.
 * ===========================================================================*/

#include "pm_droplet_math.inl"

static float PMDropletDeformNoise(const PMDropletConfig *cfg, int radialSegs, int j,
                               float t, float time, Vector3 normal, Vector3 tangent,
                               Vector3 *outOffset) {
    outOffset->x = outOffset->y = outOffset->z = 0.0f;
    if (cfg->noiseAmp <= 0.0f && cfg->noiseField == NULL) return 0.0f;

    float uu = (float)j / (float)radialSegs;
    /* t + offset: chỗ phình chạy DỌC ống thay vì đứng ở một tỉ lệ cố định của
     * nó — toạ độ VẬT CHẤT, không phải toạ độ hình học. Thiếu nó thì ống trông
     * như một hình đã bị bóp sẵn rồi kéo lê. */
    float nv = t + cfg->noiseOffset;

    MeshDeformField local;
    const MeshDeformField *field = cfg->noiseField;
    if (field == NULL) {
        local = MeshDeform_CreatePreset(cfg->noisePixels != NULL
                                            ? MESH_DEFORM_PRESET_TUBE_CHURN
                                            : MESH_DEFORM_PRESET_BEAM_RIPPLE);
        local.amplitude = cfg->noiseAmp;
        local.timeScale = cfg->noiseSpeed;
        local.latticeAround = radialSegs;
        local.latticeAlong = (cfg->noiseScale > 0.5f) ? (int)cfg->noiseScale : 4;
        local.noisePixels = cfg->noisePixels;
        local.noiseImgW = cfg->noiseImgW;
        local.noiseImgH = cfg->noiseImgH;
        field = &local;
    }

    MeshDeformResult d = MeshDeform_Evaluate(field, (Vector2){uu, t},
                                             (Vector2){uu, nv}, time, normal,
                                             tangent, tangent);
    *outOffset = d.offset;
    /* radiusDelta chứ KHÔNG phải (radiusScale - 1.0f): phép trừ đó mất chữ số
     * thấp khi số hạng nhỏ so với 1, tức là hầu như luôn luôn. */
    return d.radiusDelta;
}

void PMDroplet_BuildBezier(PMDropletMesh *out, Vector3 p0, Vector3 p1,
                              Vector3 p2, Vector3 p3, float baseRadius,
                              float flowProgress, float time, int segments,
                              int radialSegs, const PMDropletConfig *cfg) {
    if (out == NULL) return;

    PMDropletConfig defaultCfg;
    if (cfg == NULL) { defaultCfg = PMDroplet_DefaultConfig(); cfg = &defaultCfg; }

    if (segments > TUBE_MESH_MAX_SEGMENTS) segments = TUBE_MESH_MAX_SEGMENTS;
    if (radialSegs > TUBE_MESH_MAX_RADIAL) radialSegs = TUBE_MESH_MAX_RADIAL;
    out->segments = segments;
    out->radialSegs = radialSegs;

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

        float baseCapsule = PMDropletRadius(t, cfg->tailSharp);
        float tailTaper = 1.0f;
        float capsuleCurve = baseCapsule * tailTaper;
        float headWeight = 1.0f;

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
            Vector3 dOffset;
            deform += PMDropletDeformNoise(cfg, radialSegs, j, t, time, normal,
                                        tangent, &dOffset);

            float finalRadius = baseRadius * capsuleCurve * headWeight * deform;
            out->rings[i][j] = Vector3Add(
                Vector3Add(pos, Vector3Scale(normal, finalRadius)), dOffset);
        }

        if (i == 0) out->tailRadius = baseRadius * capsuleCurve * headWeight;
        if (i == segments) out->headRadius = baseRadius * capsuleCurve * headWeight;
    }
}

static void PMDropletSamplePath(const Vector3 *path, int pathCount, float t, Vector3 *outPos, Vector3 *outTangent) {
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

void PMDroplet_BuildAlongPath(PMDropletMesh *out, const Vector3 *pathPoints,
                                      int pathCount, float baseRadius,
                                      float startT, float endT, float time,
                                      int segments, int radialSegs,
                                      const PMDropletConfig *cfg) {
    if (out == NULL || pathPoints == NULL || pathCount <= 0) return;

    PMDropletConfig defaultCfg;
    if (cfg == NULL) { defaultCfg = PMDroplet_DefaultConfig(); cfg = &defaultCfg; }

    if (segments > TUBE_MESH_MAX_SEGMENTS) segments = TUBE_MESH_MAX_SEGMENTS;
    if (radialSegs > TUBE_MESH_MAX_RADIAL) radialSegs = TUBE_MESH_MAX_RADIAL;
    out->segments = segments;
    out->radialSegs = radialSegs;

    float sinPhi[TUBE_MESH_MAX_RADIAL];
    float cosPhi[TUBE_MESH_MAX_RADIAL];
    for (int j = 0; j < radialSegs; j++) {
        float phi = (float)j * (2.0f * PI) / (float)radialSegs;
        sinPhi[j] = sinf(phi); cosPhi[j] = cosf(phi);
    }

    Vector3 carriedRight = (Vector3){0};
    Vector3 prevTangent = (Vector3){0};
    bool haveCarried = false;

    for (int i = 0; i <= segments; i++) {
        float t = (float)i / (float)segments;
        float currentT = startT + t * (endT - startT);
        if (currentT < 0.0f) currentT = 0.0f;
        if (currentT > 1.0f) currentT = 1.0f;

        Vector3 pos;
        Vector3 tangent;
        PMDropletSamplePath(pathPoints, pathCount, currentT, &pos, &tangent);

        Vector3 right, up;
        if (cfg->useTransportFrame && haveCarried) {
            /* Mang khung tới bằng phép quay NHỎ NHẤT đưa tangent trước sang
             * tangent này. Bỏ qua khi hai tangent song song: trục quay khi đó
             * vô nghĩa, và quay quanh một trục bịa ra chính là đường xoắn quay
             * trở lại. */
            Vector3 axis = Vector3CrossProduct(prevTangent, tangent);
            float sinA = Vector3Length(axis);
            right = carriedRight;
            if (sinA > 1e-6f) {
                float cosA = Vector3DotProduct(prevTangent, tangent);
                if (cosA > 1.0f) cosA = 1.0f;
                if (cosA < -1.0f) cosA = -1.0f;
                axis = Vector3Scale(axis, 1.0f / sinA);
                right = Vector3RotateByAxisAngle(right, axis, atan2f(sinA, cosA));
            }
            /* Trực giao hoá lại mỗi lát, nếu không sai số float sẽ đẩy khung
             * ra khỏi mặt phẳng mặt cắt sau vài chục lát.
             *
             * VÀ PHẢI CHẶN TRƯỜNG HỢP SUY BIẾN. Nếu khung được mang tới trở nên
             * gần song song với tangent — xảy ra khi đường đi gập ngược, hoặc khi
             * hai node trùng nhau làm tangent thành rác — thì phép trừ ở trên cho
             * ra một vector gần bằng KHÔNG, và Vector3Normalize của nó là rác.
             * Cả mặt cắt khi đó sụp thành một ĐƯỜNG THẲNG: ống vẽ ra phẳng lì.
             *
             * Đây là một lỗi im lặng đúng nghĩa — không NaN, không crash, chỉ là
             * hình học sai. Rơi về khung tham chiếu ở đúng những lát đó: nó không
             * ổn định bằng, nhưng nó luôn cho ra một mặt cắt thật. */
            Vector3 ortho = Vector3Subtract(
                right, Vector3Scale(tangent, Vector3DotProduct(right, tangent)));
            if (Vector3LengthSqr(ortho) > 1e-8f) {
                right = Vector3Normalize(ortho);
            } else {
                Vector3 ref = (fabsf(tangent.y) > 0.9f) ? (Vector3){1.0f, 0.0f, 0.0f}
                                                        : (Vector3){0.0f, 1.0f, 0.0f};
                right = Vector3Normalize(Vector3CrossProduct(ref, tangent));
            }
            up = Vector3CrossProduct(tangent, right);
        } else {
            up = (Vector3){0.0f, 1.0f, 0.0f};
            if (fabsf(tangent.y) > 0.99f) up = (Vector3){1.0f, 0.0f, 0.0f};
            right = Vector3Normalize(Vector3CrossProduct(up, tangent));
            up = Vector3CrossProduct(tangent, right);
        }
        carriedRight = right;
        prevTangent = tangent;
        haveCarried = true;

        float baseCapsule = PMDropletRadius(t, cfg->tailSharp);
        float tailTaper = 1.0f;
        float capsuleCurve = baseCapsule * tailTaper;
        float headWeight = 1.0f;

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
            Vector3 dOffset;
            deform += PMDropletDeformNoise(cfg, radialSegs, j, t, time, normal,
                                        tangent, &dOffset);

            float finalRadius = baseRadius * capsuleCurve * headWeight * deform;
            out->rings[i][j] = Vector3Add(
                Vector3Add(pos, Vector3Scale(normal, finalRadius)), dOffset);
        }

        if (i == 0) out->tailRadius = baseRadius * capsuleCurve * headWeight;
        if (i == segments) out->headRadius = baseRadius * capsuleCurve * headWeight;
    }
}


void PMDroplet_Draw(const PMDropletMesh *data, float uvLengthScale) {
    PMDroplet_DrawEx(data, uvLengthScale, 0.0f);
}

void PMDroplet_DrawEx(const PMDropletMesh *data, float uvLengthScale,
                               float uvOffset) {
    if (data == NULL) return;

    const int segments = data->segments;
    const int radialSegs = data->radialSegs;

    rlPushMatrix();
    rlCheckRenderBatchLimit(segments * radialSegs * 4);
    rlBegin(RL_QUADS);
    for (int i = 0; i < segments; i++) {
        float v1 = (float)i / (float)segments * uvLengthScale + uvOffset;
        float v2 = (float)(i + 1) / (float)segments * uvLengthScale + uvOffset;

        for (int j = 0; j < radialSegs; j++) {
            int nextJ = (j + 1) % radialSegs;
            float u1 = (float)j / (float)radialSegs;
            /* (j + 1), KHÔNG phải nextJ. nextJ đã wrap về 0 ở quad khép vòng,
             * nên u2 = 0 thay vì 1 và cả texture bị nén ngược lại trong đúng
             * một mặt — một sọc dọc suốt chiều dài ống. Vị trí đỉnh vẫn phải
             * dùng nextJ (nó là đỉnh đầu tiên), chỉ toạ độ UV mới cần chạy
             * tiếp. Vô hình khi sheet trắng phẳng, và sẽ bị đổ cho flow map
             * ngay khi có texture. */
            float u2 = (float)(j + 1) / (float)radialSegs;

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

    rlPopMatrix();
}
PMDropletConfig PMDroplet_DefaultConfig(void) {
  PMDropletConfig cfg = {0};
  cfg.tailSharp = 1.6f;
  cfg.useTransportFrame = true;
  return cfg;
}
