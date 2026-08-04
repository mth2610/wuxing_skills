/* ===========================================================================
 * ỐNG NƯỚC — mesh độc lập
 *
 * Bán kính hằng, hai đầu MỞ. Mọi hình dạng nhìn thấy đều đến từ deform.
 *
 *      r(t) = 1
 *
 * ĐỘC LẬP HOÀN TOÀN. File này không dùng chung gì với pm_tube/pm_droplet/
 * pm_capsule còn lại: cấu hình, dữ liệu mesh, dựng và vẽ đều của riêng nó. Một
 * đường bao duy nhất bị bẻ qua tham số để phục vụ cả ba hình chính là thứ vừa
 * bị thay thế — nó không cho ra hình nào đúng cả.
 *
 * KHÔNG CÓ NẮP. Nắp cũ là hai quạt tam giác có đỉnh đẩy ra theo tiếp tuyến,
 * tức hai hình NÓN — cái "đầu bút chì". Hình này mở hai đầu theo định nghĩa.
 * ===========================================================================*/

/* Hai cái phanh của biến dạng, và cả hai đều đo bằng đơn vị của HÌNH HỌC chứ
 * không phải của trường noise. Trường không biết gì về cái lưới nó đang bẻ, nên
 * nếu không có hai số này thì mesh tự cắt chính nó và không có triệu chứng nào
 * ngoài "trông kỳ lạ". */
#define PM_TUBE_MIN_RADIUS_FRAC 0.25f   /* mặt cắt không bao giờ dưới 25% danh nghĩa */
#define PM_TUBE_MAX_OFFSET_RINGS 0.60f  /* đỉnh dịch tối đa 60% khoảng cách hai vành */

/* Sheet khối quấn quanh thân ĐÚNG MỘT VÒNG.
 *
 * Đây không phải vật liệu lát đi lát lại — nó là DA của cái cột, và số lưỡi
 * khói trong tấm được tác giả chọn cho CẢ chu vi. Bản tham chiếu có 4 lưỡi và
 * render ra 2, vì camera chỉ thấy nửa hình trụ; nửa kia quay đi. Quấn hai vòng
 * cho ra 8 quanh thân và 4 nhìn thấy — gấp đôi, và đó là thứ đã chạy.
 *
 * ĐÁNH ĐỔI, nói rõ ở đây: một vòng cố định thì texel bị kéo ngang theo bán
 * kính, và trên phễu (chu vi đổi 3.3 lần) nó là biến dạng trượt — thứ từng
 * đọc ra thành xoáy. Nó chịu được vì tấm giờ gần như thuần dọc: biến dạng
 * trượt cần vân chéo mới thấy, mà vân chéo thì không còn. Phép khớp mét cũ
 * sửa đúng biến dạng đó nhưng phá số lưỡi, và số lưỡi mới là thứ nhìn thấy. */
#define PM_TUBE_UV_AROUND_WRAPS 1

static float PMTubeShapeDeformNoise(const PMTubeConfig *cfg, int radialSegs, int j,
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
    /* noiseAmp LÀ biên độ, và nó phải áp cho cả trường mà caller đưa vào.
     *
     * Nhánh `field != NULL` trước đây bỏ qua nó hoàn toàn. Cột khói đặt
     * tubeNoiseAmp = 0.34, đẩy lại mỗi khung hình trong Update, và còn phơi ra
     * thành tunable `smokecolumn_noise` — cả ba đều không nối vào đâu, trường
     * chạy nguyên biên độ 1.0, tức gấp ba. Đo được ở đúng cấu hình đó:
     * radiusDelta chạm -1.000 (mặt cắt co về ĐÚNG 0) và đỉnh bị đẩy xa 0.505 m
     * = 4.04 lần khoảng cách giữa hai vành. Đó là chỗ mặt lộn ngược và vỡ ra
     * thành những mảng phẳng.
     *
     * Nhánh preset vẫn nhân noiseAmp qua `local.amplitude` như cũ, nên extAmp
     * phải là 1 ở đó — nhân hai lần là đổi kết quả của mọi trail đang chạy. */
    float extAmp = 1.0f;
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
    } else if (cfg->noiseAmp > 0.0f) {
        extAmp = cfg->noiseAmp;
    }

    MeshDeformResult d = MeshDeform_Evaluate(field, (Vector2){uu, t},
                                             (Vector2){uu, nv}, time, normal,
                                             tangent, tangent);
    outOffset->x = d.offset.x * extAmp;
    outOffset->y = d.offset.y * extAmp;
    outOffset->z = d.offset.z * extAmp;
    /* radiusDelta chứ KHÔNG phải (radiusScale - 1.0f): phép trừ đó mất chữ số
     * thấp khi số hạng nhỏ so với 1, tức là hầu như luôn luôn. */
    return d.radiusDelta * extAmp;
}

/* Một vô hướng khử tương quan lấy từ chính trường của caller, tại một lát u
 * CỐ ĐỊNH. Cố định là điều kiện để cả vành cùng dịch một lượng — lấy theo j thì
 * mỗi đỉnh đi một hướng và mặt cắt méo đi thay vì trôi ngang. */
static float PMTubeAxisScalar(const MeshDeformField *field, float uu, float t,
                              float nv, float time, Vector3 axis) {
    MeshDeformResult d = MeshDeform_Evaluate(field, (Vector2){uu, t},
                                             (Vector2){uu, nv}, time, axis, axis, axis);
    return d.radiusDelta;
}

static void PMTubeSamplePath(const Vector3 *path, int pathCount, float t, Vector3 *outPos, Vector3 *outTangent) {
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

void PMTube_BuildAlongPath(PMTubeMesh *out, const Vector3 *pathPoints,
                                      int pathCount, float baseRadius,
                                      float startT, float endT, float time,
                                      int segments, int radialSegs,
                                      const PMTubeConfig *cfg) {
    if (out == NULL || pathPoints == NULL || pathCount <= 0) return;

    PMTubeConfig defaultCfg;
    if (cfg == NULL) { defaultCfg = PMTube_DefaultConfig(); cfg = &defaultCfg; }

    if (segments > TUBE_MESH_MAX_SEGMENTS) segments = TUBE_MESH_MAX_SEGMENTS;
    if (radialSegs > TUBE_MESH_MAX_RADIAL) radialSegs = TUBE_MESH_MAX_RADIAL;
    out->segments = segments;
    out->radialSegs = radialSegs;

    /* KHOẢNG CÁCH GIỮA HAI VÀNH — cái thước để chặn biến dạng. Một trường noise
     * không biết gì về lưới nó đang bẻ: nó trả về [-1,1] dù vành cách nhau 5 cm
     * hay 5 m. Chặn theo bán kính là chặn sai thứ, vì cái vỡ không phải mặt cắt
     * mà là quan hệ giữa HAI vành liền nhau. */
    float pathLen = 0.0f;
    for (int k = 0; k + 1 < pathCount && k < 127; k++)
        pathLen += Vector3Distance(pathPoints[k], pathPoints[k + 1]);
    float spanLen = pathLen * fabsf(endT - startT);
    float ringGap = (segments > 0) ? (spanLen / (float)segments) : baseRadius;
    if (ringGap <= 1e-5f) ringGap = baseRadius;
    float offsetLimit = ringGap * PM_TUBE_MAX_OFFSET_RINGS;

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
        PMTubeSamplePath(pathPoints, pathCount, currentT, &pos, &tangent);

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

        /* r(t) = tailFrac + (1 - tailFrac) * t^pow. Với mặc định (tailFrac 1)
         * biểu thức rút gọn về đúng 1 và ống thẳng như cũ. */
        float capsuleCurve = 1.0f;
        if (cfg->radiusTailFrac > 0.0f && cfg->radiusTailFrac < 1.0f) {
            float p = (cfg->radiusPow > 0.0f) ? cfg->radiusPow : 1.0f;
            float grow = (p == 1.0f) ? t : powf(t, p);
            capsuleCurve = cfg->radiusTailFrac + (1.0f - cfg->radiusTailFrac) * grow;
        }
        float headWeight = 1.0f;

        if (i == 0) { out->tailTangent = tangent; out->tailCenter = pos; }
        if (i == segments) { out->headTangent = tangent; out->headCenter = pos; }

        /* UỐN TRỤC, trước khi dựng vành — cả mặt cắt trôi theo.
         *
         * Nhân t*t để chân cột đứng yên tuyệt đối: nguồn phát không di chuyển,
         * và một cái gốc lắc lư đọc ra là vật thể đang bay chứ không phải thứ
         * đang bốc lên. Envelope của trường cũng làm việc đó, nhưng nó là tham
         * số của caller còn cái này là ràng buộc của hình. */
        if (cfg->centerlineAmp > 0.0f && cfg->noiseField != NULL) {
            /* HAI TẦN SỐ, và TRÔI CHẬM. Một cung đơn tần số thấp trượt cứng dọc
             * thân đọc ra là con rắn bằng dây thép — đúng cái "lắc qua lắc lại
             * hơi cứng". Khói uốn ở nhiều cỡ cùng lúc: một khúc lớn suốt thân
             * cộng một khúc nhỏ hơn chồng lên.
             *
             * 0.45 là phần của noiseOffset được dùng. Trường có 3 ô dọc thân,
             * nên ở hệ số 1.0 một ô chạy hết cột trong 1.2 s — nhanh và đều,
             * tức máy móc. Giảm phần TRÔI xuống làm trục W (trường tự biến đổi
             * TẠI CHỖ) chiếm ưu thế, và biến đổi tại chỗ mới là thứ trông như
             * chất khí. */
            float nvC = t + cfg->noiseOffset * 0.45f;
            float bendA = 0.70f * PMTubeAxisScalar(cfg->noiseField, 0.13f, t, nvC, time, right)
                        + 0.30f * PMTubeAxisScalar(cfg->noiseField, 0.41f, t, nvC * 2.6f, time, right);
            float bendB = 0.70f * PMTubeAxisScalar(cfg->noiseField, 0.61f, t, nvC, time, up)
                        + 0.30f * PMTubeAxisScalar(cfg->noiseField, 0.87f, t, nvC * 2.6f, time, up);
            float w = cfg->centerlineAmp * t * t;
            pos = Vector3Add(pos, Vector3Add(Vector3Scale(right, bendA * w),
                                             Vector3Scale(up, bendB * w)));
        }

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
            deform += PMTubeShapeDeformNoise(cfg, radialSegs, j, t, time, normal,
                                        tangent, &dOffset);

            /* SÀN BÁN KÍNH. deform là 1 + nhiễu, và không có gì chặn nhiễu ở
             * dưới: đo được -1.000, tức mặt cắt thắt về đúng một điểm rồi lộn
             * ngược ra ngoài. Một cái ống bị thắt nút không phải khói, nó là
             * chuỗi hạt cườm. */
            if (deform < PM_TUBE_MIN_RADIUS_FRAC) deform = PM_TUBE_MIN_RADIUS_FRAC;

            /* TRẦN OFFSET, đo bằng khoảng cách giữa hai vành chứ không phải
             * bằng bán kính. Đỉnh bị đẩy xa hơn vành kế tiếp thì hai vành CẮT
             * NHAU, mặt lộn ra và vẽ thành những mảng phẳng bay ngang — đúng
             * cái nhìn thấy ở đỉnh cột khói. */
            float offSqr = dOffset.x * dOffset.x + dOffset.y * dOffset.y +
                           dOffset.z * dOffset.z;
            if (offSqr > offsetLimit * offsetLimit) {
                float s = offsetLimit / sqrtf(offSqr);
                dOffset.x *= s; dOffset.y *= s; dOffset.z *= s;
            }

            float finalRadius = baseRadius * capsuleCurve * headWeight * deform;
            out->rings[i][j] = Vector3Add(
                Vector3Add(pos, Vector3Scale(normal, finalRadius)), dOffset);
        }

        out->ringRadius[i] = baseRadius * capsuleCurve * headWeight;
        if (i == 0) out->tailRadius = baseRadius * capsuleCurve * headWeight;
        if (i == segments) out->headRadius = baseRadius * capsuleCurve * headWeight;
    }

    /* PHÁP TUYẾN PHẢI DỰNG LẠI SAU KHI BIẾN DẠNG.
     *
     * Ở trên, normals[i][j] được ghi TRƯỚC khi deform chạy — nó là pháp tuyến
     * của cái hình trụ chưa bị bẻ. Đèn vì thế chiếu lên một hình trụ nhẵn trong
     * khi bóng của vật là một khối lồi lõm: đổ bóng nói một đằng, đường bao nói
     * một nẻo. Kết quả là bề mặt sáng đều như đá mài, và mọi cái bướu chỉ hiện
     * ra ở rìa. Đó là nửa còn lại của "biến dạng nhìn kỳ lạ" — nửa mà chỉnh
     * biên độ bao nhiêu cũng không sửa được.
     *
     * Dựng lại bằng tích có hướng của hai sai phân trung tâm ngay trên lưới đã
     * biến dạng, rồi ép về cùng phía với pháp tuyến cũ: sai phân không biết
     * chiều nào là "ra ngoài", và một mặt bị lộn pháp tuyến thì tối đen. */
    for (int i = 0; i <= segments; i++) {
        int ip = (i > 0) ? (i - 1) : i;
        int in = (i < segments) ? (i + 1) : i;
        for (int j = 0; j < radialSegs; j++) {
            int jp = (j + radialSegs - 1) % radialSegs;
            int jn = (j + 1) % radialSegs;
            Vector3 along = Vector3Subtract(out->rings[in][j], out->rings[ip][j]);
            Vector3 around = Vector3Subtract(out->rings[i][jn], out->rings[i][jp]);
            Vector3 n = Vector3CrossProduct(along, around);
            /* Suy biến ở hai đầu bẹt hoặc khi hai vành trùng nhau — giữ pháp
             * tuyến hình trụ thay vì chuẩn hoá một vector gần bằng không. */
            if (Vector3LengthSqr(n) <= 1e-12f) continue;
            n = Vector3Normalize(n);
            if (Vector3DotProduct(n, out->normals[i][j]) < 0.0f) n = Vector3Negate(n);
            out->normals[i][j] = n;
        }
    }
}


void PMTube_Draw(const PMTubeMesh *data, float uvLengthScale) {
    PMTube_DrawEx(data, uvLengthScale, 0.0f);
}

static float PMTubeSmoothStep(float e0, float e1, float x) {
    float span = e1 - e0;
    if (fabsf(span) < 1e-6f) return (x < e0) ? 0.0f : 1.0f;
    float k = (x - e0) / span;
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    return k * k * (3.0f - 2.0f * k);
}

/* Như DrawEx, nhưng alpha tắt dần ở hai đầu thân.
 *
 * TẠI SAO LÀ THUỘC TÍNH ĐỈNH CHỨ KHÔNG PHẢI UNIFORM. Mặt nạ này là hàm thuần
 * của chiều cao chuẩn hoá, mà chiều cao đó fragment shader KHÔNG đọc được:
 * fragTexCoord.y của ống là toạ độ đã lát theo mét và đang cuộn, không phải
 * 0..1. Đưa nó vào uniform thì phải nạp lại giữa từng lượt vẽ — đúng cái mẫu
 * làm rỗng UBO arena của rlvk (ENGINE_LANDMINES §8), và luật ở đó nói thẳng:
 * mang biến thiên theo instance bằng THUỘC TÍNH ĐỈNH. Vành nào cũng đã biết t
 * của nó rồi, nên tính trên CPU là xong.
 *
 * Công thức đúng như tài liệu tham chiếu: smoothstep(0, fadeIn, V) *
 * (1 - smoothstep(fadeOut, 1, V)). */
void PMTube_DrawFaded(const PMTubeMesh *data, float uvLengthScale, float uvOffset,
                      Color base, float fadeInEnd, float fadeOutStart,
                      float metresPerTile) {
    if (data == NULL) return;

    const int segments = data->segments;
    const int radialSegs = data->radialSegs;

    /* Một vòng quanh thân — xem PM_TUBE_UV_AROUND_WRAPS ở đầu file. Số lưỡi
     * khói nằm trong TẤM, và tấm phủ trọn chu vi. */
    (void)metresPerTile; /* chỉ chi phối trục DỌC, do caller tính vào uvLengthScale */
    const int aroundTiles = PM_TUBE_UV_AROUND_WRAPS;

    rlPushMatrix();
    rlCheckRenderBatchLimit(segments * radialSegs * 4);
    rlBegin(RL_QUADS);
    for (int i = 0; i < segments; i++) {
        float t1 = (float)i / (float)segments;
        float t2 = (float)(i + 1) / (float)segments;
        float v1 = t1 * uvLengthScale + uvOffset;
        float v2 = t2 * uvLengthScale + uvOffset;
        float m1 = PMTubeSmoothStep(0.0f, fadeInEnd, t1) *
                   (1.0f - PMTubeSmoothStep(fadeOutStart, 1.0f, t1));
        float m2 = PMTubeSmoothStep(0.0f, fadeInEnd, t2) *
                   (1.0f - PMTubeSmoothStep(fadeOutStart, 1.0f, t2));
        unsigned char a1 = (unsigned char)((float)base.a * m1);
        unsigned char a2 = (unsigned char)((float)base.a * m2);

        for (int j = 0; j < radialSegs; j++) {
            int nextJ = (j + 1) % radialSegs;
            float u1 = (float)j * (float)aroundTiles / (float)radialSegs;
            float u2 = (float)(j + 1) * (float)aroundTiles / (float)radialSegs;

            rlColor4ub(base.r, base.g, base.b, a1);
            rlNormal3f(data->normals[i][j].x, data->normals[i][j].y, data->normals[i][j].z);
            rlTexCoord2f(u1, v1); rlVertex3f(data->rings[i][j].x, data->rings[i][j].y, data->rings[i][j].z);

            rlNormal3f(data->normals[i][nextJ].x, data->normals[i][nextJ].y, data->normals[i][nextJ].z);
            rlTexCoord2f(u2, v1); rlVertex3f(data->rings[i][nextJ].x, data->rings[i][nextJ].y, data->rings[i][nextJ].z);

            rlColor4ub(base.r, base.g, base.b, a2);
            rlNormal3f(data->normals[i + 1][nextJ].x, data->normals[i + 1][nextJ].y, data->normals[i + 1][nextJ].z);
            rlTexCoord2f(u2, v2); rlVertex3f(data->rings[i + 1][nextJ].x, data->rings[i + 1][nextJ].y, data->rings[i + 1][nextJ].z);

            rlNormal3f(data->normals[i + 1][j].x, data->normals[i + 1][j].y, data->normals[i + 1][j].z);
            rlTexCoord2f(u1, v2); rlVertex3f(data->rings[i + 1][j].x, data->rings[i + 1][j].y, data->rings[i + 1][j].z);
        }
    }
    rlEnd();

    rlPopMatrix();
}

void PMTube_DrawEx(const PMTubeMesh *data, float uvLengthScale,
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
PMTubeConfig PMTube_DefaultConfig(void) {
  PMTubeConfig cfg = {0};
  cfg.useTransportFrame = true;
  return cfg;
}
