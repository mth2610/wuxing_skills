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
/* PHANH THỨ BA, 05/08/2026 — trần offset cũ chỉ đo theo KHOẢNG CÁCH HAI VÀNH,
 * không theo BÁN KÍNH CỤC BỘ. Trên một thân gần như đều (cột khói: 0.12x..1.0x
 * trên một trục KHÔNG di chuyển, đầu mỏng là nguồn phát — thường bị che/ít
 * soi) thì không sao. Nhưng đầu funnel của trail giờ neo ngược
 * (anchorAtTail) để mỏng đúng ở ĐẦU ĐƯỜNG ĐI — đầu dễ thấy nhất, luôn
 * hướng ra phía camera vì nó là đầu MỚI. Tại đó bán kính thật có thể chỉ còn
 * 0.12 x 0.35 m ~ 4 cm, trong khi offsetLimit vẫn tính theo ringGap của CẢ
 * THÂN dày — cùng một trần dịch chuyển đó giờ lớn hơn bán kính thật nhiều
 * lần, mặt cắt bị đẩy lộn qua tâm rồi ngược trở lại, vỡ thành mảng phẳng —
 * đúng "bậc thang" + "vuông vuông" quan sát được, và CHỈ lộ ra ở đầu mỏng vì
 * offsetLimit không hề biết bán kính tại đó đã co lại. */
#define PM_TUBE_MAX_OFFSET_RADIUS_FRAC 0.55f  /* HOẶC 55% bán kính cục bộ, lấy trần THẤP HƠN */

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

/* tGeo và tNoise TÁCH RIÊNG, sửa 05/08/2026 — landmine vừa dính khi bật
 * noiseWavelength cho smoke trail (xem core/deform/README.md's "the drive
 * coordinate is not the raw parametric position"). Trước đó hàm này chỉ
 * nhận MỘT giá trị `t` dùng cho CẢ HAI vai trò — surf.y (cổng envelope,
 * MeshDeformLayer.env/envStart/envEnd, PHẢI là phân số hình học [0,1] thật
 * dọc thân) VÀ vế của mat.y qua `nv` (toạ độ VẬT CHẤT lái trường nhiễu, thứ
 * noiseWavelength cố ý SCALE xuống dưới 1 để tách khỏi tổng chiều dài path
 * đang dao động). Với mọi caller cũ (noiseWavelength=0) hai giá trị đó luôn
 * bằng nhau nên không lộ ra gì. Bật noiseWavelength cho trail thì tNoise <
 * 1 (thường 0.4-0.9) — surf.y bị lây giá trị đó, và UV_ENV_HEAD_WELD_SQ =
 * smoothstep(start,end,c)*c*c bị CẢ MỘT LŨY THỪA BÌNH PHƯƠNG của cái c bị
 * co đó ăn vào: biên độ hiệu dụng còn scale² (~0.25-0.81), suốt DỌC THÂN,
 * không chỉ trễ điểm mở — đúng "biến đổi ít quá" quan sát được. Đo bằng số
 * ở core/tests/pm_tube_envelope_coordinate_test.c.
 *
 * tGeo = phân số HÌNH HỌC thật (caller đưa `tEnv`: i/segments, đảo chiều
 * nếu anchorAtTail — vẫn thuần hình học, không bao giờ là toạ độ vật chất)
 * — luôn dùng cho surf.y. tNoise = toạ độ đã qua noiseWavelength (hoặc bằng
 * t nếu tắt) — chỉ dùng cho mat.y qua `nv`. */
static float PMTubeShapeDeformNoise(const PMTubeConfig *cfg, int radialSegs, int j,
                               float tGeo, float tNoise, float time, Vector3 normal,
                               Vector3 tangent, Vector3 *outOffset) {
    outOffset->x = outOffset->y = outOffset->z = 0.0f;
    if (cfg->noiseAmp <= 0.0f && cfg->noiseField == NULL) return 0.0f;

    float uu = (float)j / (float)radialSegs;
    /* tNoise + offset: chỗ phình chạy DỌC ống thay vì đứng ở một tỉ lệ cố
     * định của nó — toạ độ VẬT CHẤT, không phải toạ độ hình học. Thiếu nó
     * thì ống trông như một hình đã bị bóp sẵn rồi kéo lê. */
    float nv = tNoise + cfg->noiseOffset;

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

    MeshDeformResult d = MeshDeform_Evaluate(field, (Vector2){uu, tGeo},
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

        /* Toạ độ nhiễu tính theo MÉT THẬT, không theo phân số của tổng chiều
         * dài path hiện tại — xem doc field noiseWavelength ở
         * procedural_mesh_utils.h. 0 (mặc định) giữ nguyên hành vi cũ (= t),
         * nên mọi caller khác không set field này không đổi gì cả.
         *
         * noiseSpanLenOverride thay spanLen THÔ bằng bản đã làm mịn của caller
         * nếu có (xem doc field) — chỉ cho TỌA ĐỘ NHIỄU. spanLen thô vẫn dùng
         * nguyên cho ringGap/offsetLimit ở trên, vì kẹp hình học cần chính xác
         * ngay khung này. */
        float noiseSpanLen = (cfg->noiseSpanLenOverride > 0.0f) ? cfg->noiseSpanLenOverride : spanLen;
        float tNoise = (cfg->noiseWavelength > 0.0f) ? ((t * noiseSpanLen) / cfg->noiseWavelength) : t;

        /* TOẠ ĐỘ "TÍNH TỪ NGUỒN PHÁT", 05/08/2026 — MỘT biến cho CẢ BA chỗ
         * neo vào nguồn phát: đường bao bán kính, cổng envelope của deform,
         * và trọng số uốn trục. Xem doc field `anchorAtTail` ở
         * procedural_mesh_utils.h: để cờ neo bán kính mà KHÔNG neo envelope
         * thì hai neo chạy ngược nhau và biên độ phình tuyệt đối tụt còn
         * 1/5 — đúng lỗi vừa dính của smoke trail. Không tách ba biến: ba
         * chỗ đó là ba hệ quả của MỘT sự thật, tách ra là mời lỗi cũ quay
         * lại. tGeo vẫn là phân số HÌNH HỌC thật (chỉ đảo chiều), không bao
         * giờ là toạ độ vật chất tNoise. */
        float tEnv = cfg->anchorAtTail ? (1.0f - t) : t;

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
         * biểu thức rút gọn về đúng 1 và ống thẳng như cũ.
         *
         * anchorAtTail lật NEO của công thức từ t=1 (mặc định) sang
         * t=0: chạy trên (1-t) thay vì t, nên headR — bán kính người gọi xin
         * — luôn nằm ở t=0 (đuôi) và t=1 (đầu ĐƯỜNG ĐI) co lại còn
         * tailFrac x headR. Có cờ này để tailFrac chỉ cần ở miền [0,1] tự
         * nhiên (xem doc field ở procedural_mesh_utils.h) — không phải bẻ nó
         * vượt 1 để ép đầu đường đi hẹp lại, cách đó thổi phồng đầu kia vượt
         * xa headR mà người gọi xin. */
        float capsuleCurve = 1.0f;
        if (cfg->radiusTailFrac > 0.0f && cfg->radiusTailFrac != 1.0f) {
            float p = (cfg->radiusPow > 0.0f) ? cfg->radiusPow : 1.0f;
            float grow = (p == 1.0f) ? tEnv : powf(tEnv, p);
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
            // tEnv (hình học thật, đã theo neo của caller), KHÔNG PHẢI
            // tNoise, cho tham số surf.y — cùng lỗi/cùng sửa 05/08/2026 như
            // PMTubeShapeDeformNoise ở trên (xem comment ở đó). Chưa có caller nào bật cả
            // centerlineAmp lẫn noiseWavelength cùng lúc nên nhánh này
            // hiện không lộ triệu chứng, nhưng cùng landmine nếu sau này
            // có — sửa ngay trong lúc đang ở đây, bit-identical với mọi
            // caller cũ (tNoise == t khi noiseWavelength == 0).
            float nvC = tNoise + cfg->noiseOffset * 0.45f;
            float bendA = 0.70f * PMTubeAxisScalar(cfg->noiseField, 0.13f, tEnv, nvC, time, right)
                        + 0.30f * PMTubeAxisScalar(cfg->noiseField, 0.41f, tEnv, nvC * 2.6f, time, right);
            float bendB = 0.70f * PMTubeAxisScalar(cfg->noiseField, 0.61f, tEnv, nvC, time, up)
                        + 0.30f * PMTubeAxisScalar(cfg->noiseField, 0.87f, tEnv, nvC * 2.6f, time, up);
            float w = cfg->centerlineAmp * tEnv * tEnv;
            pos = Vector3Add(pos, Vector3Add(Vector3Scale(right, bendA * w),
                                             Vector3Scale(up, bendB * w)));
        }

        /* Bán kính DANH NGHĨA của vành — tính MỘT lần trước vòng j, không lặp
         * lại ở ba chỗ. Cũng là mẫu số để đo biên độ churn tách khỏi taper. */
        float nominalRadius = baseRadius * capsuleCurve * headWeight;

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
            deform += PMTubeShapeDeformNoise(cfg, radialSegs, j, tEnv, tNoise, time, normal,
                                        tangent, &dOffset);

            /* SÀN BÁN KÍNH. deform là 1 + nhiễu, và không có gì chặn nhiễu ở
             * dưới: đo được -1.000, tức mặt cắt thắt về đúng một điểm rồi lộn
             * ngược ra ngoài. Một cái ống bị thắt nút không phải khói, nó là
             * chuỗi hạt cườm. */
            /* DEBUG TẠM THỜI — đọc TRƯỚC khi kẹp, và cố ý KHÔNG gộp vào câu
             * lệnh kẹp bên dưới: ba test khác nhau khoá đúng dòng kẹp đó
             * theo văn bản, và giàn giáo chẩn đoán tạm thời không có quyền
             * làm dịch chuyển thứ đang được canh. */
            bool floored = (deform < PM_TUBE_MIN_RADIUS_FRAC);
            if (deform < PM_TUBE_MIN_RADIUS_FRAC) deform = PM_TUBE_MIN_RADIUS_FRAC;
            float finalRadius = nominalRadius * deform;

            /* TRẦN OFFSET — PMSweptSection_ClampOffset (procedural_mesh_utils.c),
             * DÙNG CHUNG với pm_droplet/pm_capsule khi cần, xem doc đầy đủ ở
             * khai báo trong procedural_mesh_utils.h. Trần THẤP HƠN giữa
             * "60% khoảng cách hai vành" (offsetLimit, chặn hai vành CẮT
             * NHAU) và "55% bán kính ĐÃ BIẾN DẠNG tại đúng đỉnh này" (chặn
             * mặt cắt tự lộn qua tâm) — cố ý truyền `finalRadius` (đã qua sàn
             * ở trên), KHÔNG phải bán kính danh nghĩa của vành: đó chính là
             * điều làm bất biến "bán kính hiệu dụng luôn > 0" đúng theo cấu
             * trúc công thức, không phải nhờ may mắn không rơi đúng trường
             * hợp xấu — xem comment tại định nghĩa hàm. */
            float rawOffLen = Vector3Length(dOffset);
            dOffset = PMSweptSection_ClampOffset(dOffset, finalRadius,
                                                 PM_TUBE_MAX_OFFSET_RADIUS_FRAC,
                                                 offsetLimit);
            /* DEBUG TẠM THỜI (xem khối log bên dưới) — "kẹp" tính là mất từ 5%
             * chiều dài trở lên, không phải bất kỳ thay đổi nào: soft-knee bo
             * mượt nên gần như mọi đỉnh đều lệch một chút. */
            bool offsetWasClamped = (rawOffLen > 1e-6f) &&
                                    (Vector3Length(dOffset) < rawOffLen * 0.95f);

            out->rings[i][j] = Vector3Add(
                Vector3Add(pos, Vector3Scale(normal, finalRadius)), dOffset);

            // DEBUG TẠM THỜI, 05/08/2026 (vòng 3) — chẩn đoán "vẫn phẳng dù
            // đã sửa 2 lỗi" cho smoke trail. XOÁ khi chẩn đoán xong.
            //
            // ĐO ĐỘ LỆCH TUYỆT ĐỐI |eff - danh nghĩa| (mét), KHÔNG phải bán
            // kính hiệu dụng: bán kính hiệu dụng bị taper (0.12x..1.0x) chi
            // phối nên min/max/stdev của nó gần như chỉ đo cái phễu, không
            // phân biệt được churn mạnh hay yếu. Tỉ lệ eff/danh-nghĩa cũng
            // KHÔNG phân biệt được đúng lỗi neo envelope: lật neo chỉ đảo
            // chiều envelope nên phân bố tương đối giữ nguyên — thứ thay đổi
            // là churn có rơi vào chỗ ống DÀY hay không, tức độ lệch TUYỆT
            // ĐỐI. Dự đoán trước khi sửa (bộ số hiện tại, headR 0.55):
            // đỉnh capsuleCurve x env = 0.197; sau khi sửa = 1.000, tức max
            // phải tăng khoảng 5 lần và mean khoảng 2.2 lần. Nếu log KHÔNG
            // tăng như vậy thì bản sửa này không phải nguyên nhân — số này
            // là để bác bỏ, không phải để xác nhận.
            if (cfg->noiseWavelength > 0.0f) {
                static float s_max = -1e9f, s_sum = 0.0f;
                static int s_n = 0, s_floored = 0, s_clampedOff = 0;
                static int s_calls = 0;
                float dev = fabsf(Vector3Distance(pos, out->rings[i][j]) - nominalRadius);
                if (dev > s_max) s_max = dev;
                s_sum += dev; s_n++;
                if (floored) s_floored++;
                if (offsetWasClamped) s_clampedOff++;
                if (i == segments && j == radialSegs - 1) {
                    s_calls++;
                    if (s_calls % 30 == 0)
                        TraceLog(LOG_INFO,
                            "TUBE_CHURN_DEV_DEBUG: baseRadius=%.3f n=%d "
                            "meanDev=%.4f m  maxDev=%.4f m  floored=%.1f%%  "
                            "offsetClamped=%.1f%%",
                            baseRadius, s_n, s_sum / (float)s_n, s_max,
                            100.0f * (float)s_floored / (float)s_n,
                            100.0f * (float)s_clampedOff / (float)s_n);
                    s_max = -1e9f; s_sum = 0.0f; s_n = 0;
                    s_floored = 0; s_clampedOff = 0;
                }
            }
        }

        out->ringRadius[i] = nominalRadius;
        if (i == 0) out->tailRadius = nominalRadius;
        if (i == segments) out->headRadius = nominalRadius;
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
  // Đặt RÕ, không dựa vào {0}: 0 nghĩa là TẮT cuộn theo thời gian thật (xem
  // doc field ở procedural_mesh_utils.h) — mặc định phải là 1.0 để mọi
  // caller có sẵn (cột khói, spark trail, ember trail...) không đổi hành vi.
  cfg.noiseOffsetScrollMul = 1.0f;
  return cfg;
}
