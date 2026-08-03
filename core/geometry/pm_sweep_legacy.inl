/* ===========================================================================
 * BỘ MÁY QUÉT CŨ — ĐANG BỊ THAY THẾ, ĐỪNG THÊM CONSUMER MỚI
 *
 * File này từng tên là pm_tube.inl và phục vụ MỌI hình: ống, giọt nước, con
 * nhộng — bằng một đường bao duy nhất bị bẻ qua tham số, cộng hai nắp nón dán
 * vào hai đầu. Kết quả là không hình nào đúng cả.
 *
 * Đang được thay bằng ba module ĐỘC LẬP, mỗi cái một loại mesh, không dùng
 * chung gì:
 *     pm_tube.inl      ống nước
 *     pm_droplet.inl   giọt nước
 *     pm_capsule.inl   con nhộng
 *
 * File này sống tới khi consumer cuối cùng rời đi, rồi bị xoá. Giữ nguyên từng
 * float trong lúc đó — đó là điều khiến việc chuyển từng hình một là an toàn.
 * ===========================================================================*/

/* TẠM THỜI: bộ máy cũ vẫn phục vụ PM_PROFILE_DROPLET/CAPSULE cho các consumer
 * chưa chuyển. Khi consumer cuối rời sang module độc lập, cả hai include này và
 * cả file này biến mất cùng nhau. */
#include "pm_droplet_math.inl"
#include "pm_capsule_math.inl"

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

/* Biến dạng bề mặt bằng noise — nay thuộc core/deform/mesh_deform.h.
 *
 * Bốn hàm băm/lấy mẫu từng nằm ở đây đã CHUYỂN sang module, không phải sao
 * chép: hình trụ không còn là thứ duy nhất dùng được chúng. Công thức không
 * đổi một phép tính nào, và core/tests/mesh_deform_test.c chứng minh từng float
 * ra giống hệt cho CẢ hai nguồn — ảnh (trail tube) và lattice thủ tục (beam).
 *
 * Khối này trước đây được COPY y hệt ở hai builder bên dưới. Giờ một chỗ. */
static float PMTubeDeformNoise(const TubeMeshConfig *cfg, int radialSegs, int j,
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

/* Đường bao bán kính theo t (0 = đuôi, 1 = đầu), trả về hệ số 0..1.
 *
 * GIỌT NƯỚC = luỹ thừa thon tới một MŨI ở đuôi, nối liền vào một CHỎM CẦU ở
 * đầu. Hai mảnh gặp nhau tại 1.0 nên đường bao liên tục, và chỏm cầu tự khép
 * lại — nên giọt nước không cần nắp, khác hẳn cái chóp nón dán thêm mà nó thay
 * thế. Đây là hình một giọt nước rơi thật: đầu tròn đầy, đuôi vuốt nhọn.
 *
 * ỐNG = hằng số 1. Hai đầu mở; mọi hình dạng khác đến từ deform. */
static float PMTubeProfile(const TubeMeshConfig *cfg, float t) {
    switch (cfg->profile) {
    case PM_PROFILE_TUBE:
        return 1.0f;
    case PM_PROFILE_DROPLET:
        /* pm_droplet.inl */
        return PMDropletRadius(t, cfg->dropletTailSharp, cfg->dropletHeadFrac);
    case PM_PROFILE_CAPSULE:
        /* pm_capsule.inl */
        return PMCapsuleRadius(t, cfg->capsuleCapFrac);
    default: {
        float capFloor = (cfg->capsuleFloor > 0.0f) ? cfg->capsuleFloor : 0.3f;
        return capFloor + (1.0f - capFloor) *
               sqrtf(fmaxf(0.0f, sinf(t * PI))) * cfg->capsuleTailExp;
    }
    }
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
    /* Nắp là HỆ QUẢ của hình, không phải một cờ. Ống thì hai đầu mở; giọt nước
     * và con nhộng tự khép bằng chính đường bao của chúng. Bịt nắp lên một hình
     * đã khép là dựng hai hình nón bên trong nó — đó là cái "đầu bút chì". Chỉ
     * hồ sơ CŨ mới cần nắp, vì đường bao của nó dừng ở 0.3 và để hở hai lỗ. */
    out->suppressCaps = cfg->suppressCaps ||
                        (cfg->profile != PM_PROFILE_LEGACY_CAPSULE);
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

        float baseCapsule = PMTubeProfile(cfg, t);
        /* Chỉ hồ sơ CŨ mới chồng thêm taper/headGrowth. DROPLET và TUBE đã là
         * đường bao hoàn chỉnh; nhân thêm nữa là bẻ lại đúng hình vừa định
         * nghĩa. */
        bool ownsSilhouette = (cfg->profile != PM_PROFILE_LEGACY_CAPSULE);
        float tailTaper = ownsSilhouette ? 1.0f
                        : cfg->tailTaperMin + (cfg->tailTaperMax - cfg->tailTaperMin) * t;
        float capsuleCurve = baseCapsule * tailTaper;
        float headWeight = ownsSilhouette ? 1.0f : (1.0f + cfg->headGrowth * t);

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
            deform += PMTubeDeformNoise(cfg, radialSegs, j, t, time, normal,
                                        tangent, &dOffset);

            float finalRadius = baseRadius * capsuleCurve * headWeight * deform;
            out->rings[i][j] = Vector3Add(
                Vector3Add(pos, Vector3Scale(normal, finalRadius)), dOffset);
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
    out->suppressCaps = cfg->suppressCaps ||
                        (cfg->profile == PM_PROFILE_TUBE) ||
                        (cfg->profile == PM_PROFILE_DROPLET);
    out->headApexFactor = cfg->headApexFactor;

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
        SamplePathPositionAndTangent(pathPoints, pathCount, currentT, &pos, &tangent);

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

        float baseCapsule = PMTubeProfile(cfg, t);
        /* Chỉ hồ sơ CŨ mới chồng thêm taper/headGrowth. DROPLET và TUBE đã là
         * đường bao hoàn chỉnh; nhân thêm nữa là bẻ lại đúng hình vừa định
         * nghĩa. */
        bool ownsSilhouette = (cfg->profile != PM_PROFILE_LEGACY_CAPSULE);
        float tailTaper = ownsSilhouette ? 1.0f
                        : cfg->tailTaperMin + (cfg->tailTaperMax - cfg->tailTaperMin) * t;
        float capsuleCurve = baseCapsule * tailTaper;
        float headWeight = ownsSilhouette ? 1.0f : (1.0f + cfg->headGrowth * t);

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
            deform += PMTubeDeformNoise(cfg, radialSegs, j, t, time, normal,
                                        tangent, &dOffset);

            float finalRadius = baseRadius * capsuleCurve * headWeight * deform;
            out->rings[i][j] = Vector3Add(
                Vector3Add(pos, Vector3Scale(normal, finalRadius)), dOffset);
        }

        if (i == 0) out->tailRadius = baseRadius * capsuleCurve * headWeight;
        if (i == segments) out->headRadius = baseRadius * capsuleCurve * headWeight;
    }
}


void ProceduralMesh_DrawTube(const TubeMeshData *data, float uvLengthScale) {
    ProceduralMesh_DrawTubeEx(data, uvLengthScale, 0.0f);
}

void ProceduralMesh_DrawTubeEx(const TubeMeshData *data, float uvLengthScale,
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

    /* Ống MỞ hai đầu. Một cột khói không có nắp — chóp nón ở đỉnh đọc ra là
     * cái phễu úp ngược, không phải khói.
     *
     * BAO KHỐI, KHÔNG return. Hàm này mở bằng rlPushMatrix() và đóng bằng
     * rlPopMatrix() ở cuối; thoát sớm ở giữa sẽ bỏ qua cái pop, và vì hàm chạy
     * mỗi frame nên ngăn xếp ma trận tràn sau vài chục frame —
     * "RLVK: Matrix stack overflow", màn hình đen, và không có gì trỏ về cái
     * ống. Đã xảy ra đúng một lần, ngay tại dòng này. */
    if (!data->suppressCaps) {
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
    } /* !suppressCaps */
    rlPopMatrix();
}