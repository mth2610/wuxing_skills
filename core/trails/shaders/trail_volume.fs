#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/fx.glsl"
#include "core/uv/shaders/uv_field.glsl"

// ── TRAIL VOLUME — opacity for a swept tube that has to read as gas ─────────
//
// The geometry stage can only ever produce a lumpy SOLID. What separates a
// bent cylinder from smoke happens here, and it is three multiplies:
//
//     opacity = pattern * fade * edge * density
//
// MULTIPLIED, not layered. Two sheets composited alpha-over can only ever add
// coverage, so a second pass makes the body MORE opaque and the result reads
// as polished stone — which is exactly how this column shipped. A product of
// two masks is sparser than either factor, so the same two samples carve holes
// instead of filling them. That is the whole difference.
//
// SPACE — SETTLED 06/08/2026 BY A NUMERIC READBACK. RESOLVED.
//
// fragPosition and fragNormal are VIEW SPACE, and they get here WITHOUT going
// through matModel: trail_volume.vs deliberately does not call vs_header's
// VS_FinalOutput. Read that file's header for the why and the measured
// numbers; the one-paragraph version is that this tube is drawn in IMMEDIATE
// MODE, rlgl has already view-transformed its vertices and normals on the CPU,
// and matModel on that path is the view matrix again — so VS_FinalOutput was
// applying the view rotation TWICE.
//
// That double transform was the entire |N.V| inversion this shader spent a
// session chasing (the whole ruled-out list is in
// core/docs/VOLUME_SHADING_HANDOFF.md, now closed). The consequences here:
//   - the view vector is `normalize(-fragPosition)`; the camera is the origin
//     of view space and no uniform is involved
//   - `viewPos` is a WORLD coordinate and must NOT be mixed into it
//   - the attribute normal is fine — `rlNormal3f` was never the problem
// The regression tripwire is third_party/vulkan/tests/rlvk_visual_test.c
// scenario `imm_normal`; run it before believing any new claim about which
// space this shader is in.

in vec4 vColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// THE TWO-SHEET PAN — GENERALISED, 05/08/2026. This used to be a bespoke
// `u_volPan` uniform + hand-rolled `pan1/pan2/s1/s2` block, unique to this
// shader. It is now core/uv's `SurfaceFlow` (core/uv/surface_flow.h),
// applied through the packed uniform block in uv_field.glsl
// (u_flowLayer/u_flowMeta, declared by that include, not here) — the SAME
// "N-layer sample, tile-or-stretch, MUL/ADD/MAX blend" mechanism
// trail_deform.fs (ribbon) already uses. See core/uv/README.md:
// `mesh + UVDeformField + SurfaceFlow = effect` — this file's pan was never
// tube-specific, it was just never plumbed into the shared module. Reusing
// it here is the whole point: the NEXT consumer that needs "two sheets,
// multiplied, panned at different rates" (ribbon or volume or anything else)
// configures a SurfaceFlow instead of re-deriving this block.
//
// Values pushed from trail_system.c reproduce this file's OLD constants
// exactly (2 layers, MUL blend, sheet 2 scaled 1.63x along) — see the
// SurfaceFlow_Apply call site there for the exact numbers, including why the
// pan SIGN differs from the old `fragTexCoord + pan` code (SurfaceFlow's
// formula subtracts pan). AROUND pan stays zero for the same reason the old
// comment gave: panning u rotates the sheet about the tube's axis, and
// combined with any along-pan the net motion is a screw thread climbing the
// body — smoke rises, it does not spin.
//
// .x = ALONG tiling of the second sheet. Its AROUND tiling is fixed at 1 wrap
//      below and is not exposed.
// .y = depth power, .z = silhouette softness, .w = master density.
uniform vec4 u_volMask;
// DEBUG VIEW — 0 = off. Paints one intermediate quantity as opaque greyscale
// so a screenshot answers what the term actually does across the column,
// instead of another round of guessing at its constants. None of these sit
// below the CULL discard (facing < 0.0) below, so they see the WHOLE tube,
// front and back — the cull only applies to the real, non-debug render.
//   1 = the thickness term `edge`      2 = |facing| (= |N.V|)
//   3 = the sheet product `pattern`    4 = the vertex fade
//   5 = fragNormal as colour, so a wrong one is READABLE: a correct radial
//       normal sweeps the full hue circle around the body.
//   6 = the view vector V as colour — correct V barely changes across the body.
//   7 = |fragPosition| / 40, greyscale — WORLD space or VIEW space, decisively
//       (see the comment at that branch for the exact magnitudes expected).
//   8 = constant mid grey, 9 = constant red. Nothing computed, nothing
//       interpolated: if these do not arrive as written, no other reading from
//       this shader means anything. See sandbox/colour_probe.c.
//  10 = |N.V| as five DISCRETE colour bands. Mode 2 already paints |facing|,
//       but as a continuous grey ramp — and the eye cannot read a value off a
//       ramp. When the question is WHERE a feature sits (does the bright band
//       land where the formula puts it?), a band boundary is readable to a few
//       pixels and a grey ramp is not.
uniform float u_volDebug;
/* Hệ số CỘNG của số hạng tán xạ rìa pow(1 - |N.V|, p) — xem chỗ dùng. Là
 * uniform riêng chứ không nhét vào u_volMask vì cả 4 ô kia đều đang có chủ
 * (.x hệ số thân, .y số mũ, .z độ mềm viền, .w độ đậm). Đẩy MỘT LẦN cho cả
 * nhóm vẽ, không phải mỗi instance, nên không đụng landmine UBO của rlvk
 * (ENGINE_LANDMINES §8) — cùng đường với u_volDebug ngay trên. */
uniform float u_volRim;
/* 1 = bỏ mặt quay đi (mặc định, hành vi cũ), 0 = VẼ CẢ HAI MẶT.
 *
 * Thành công tắc 06/08/2026 sau khi core/tests/silhouette_test.c lần đầu đo
 * số hạng ĐANG SHIP thay vì chỉ |N.V|^p. Kết quả (p=2, hardness biên, ngưỡng
 * "còn cứng" = 0.15):
 *                     một mặt   hai mặt
 *     RIM  (1-N.V)^p   0.252     0.384
 *     NDOTV  (N.V)^p   0.119     0.153
 * Hai điều lật lại giả định cũ: (a) cấu hình ĐANG CHẠY vượt ngưỡng ngay cả
 * khi đã cull — đó là "răng cưa", không phải khử răng cưa hình học; (b) vẽ
 * HAI mặt kèm số hạng độ dày quang học MỀM HƠN cấu hình đang chạy, vì mọi
 * lần cắt thêm ở gần rìa đều mang giá trị ~0. "Bắt buộc phải cull" hoá ra là
 * tính chất của SỐ HẠNG RIM, không phải của hình học hai mặt.
 * Hai knob này phải đi cùng nhau: hai mặt + rim là tệ nhất bảng. */
uniform float u_volCull;
/* 0 = pháp tuyến từ attribute fragNormal, 1 = từ dFdx/dFdy của fragPosition.
 * Xem chỗ dùng. Đây vừa là phép thử ("attribute có tới nơi không") vừa là bản
 * sửa ứng viên, nên nó là công tắc chứ không phải hai lần build. */
uniform float u_volNormalSrc;
/* Kỹ thuật 2 — NOISE EROSION của silhouette (thêm 06/08/2026).
 *
 * Biên mềm smooth `rim` để lại một gradient mượt bám đúng đường cong dẹt của
 * mesh ("foggy blur") — bản này xói mòn nó bằng noise đang cuộn: ngưỡng
 * dissolve dao động theo một mẫu texture, scale về 0 ở thân và đầy ở rìa, nên
 * đường bao bị "cắn" lõm chỗ này chỗ kia thay vì một dải mờ đều — khói tưa ra
 * không khí. Cùng hình dạng với trail_deform.fs's u_edgeTear, trên toạ độ b/R
 * (1 = rìa silhouette) thay vì across-UV.
 * Đẩy MỘT LẦN cho cả nhóm vẽ (cùng đường u_volRim/u_volDebug — xem landmine
 * UBO của rlvk, ENGINE_LANDMINES §8). */
uniform float u_volErode;       /* 0 = tắt (mặc định); 1 = cắn mạnh nhất */
uniform float u_volErodeBand;   /* bề rộng vùng tưa tính ngược vào trong, theo b/R */

void main()
{
    // THE CONSTANT DEBUG MODES COME FIRST, ABOVE EVERYTHING.
    //
    // They used to sit near the bottom, below two `discard`s — so a fragment
    // that the shader was going to throw away never reached them. The colour
    // probe caught it: mode 8 asks for flat grey and the framebuffer came back
    // with the background, because the fragment was discarded before the branch
    // was ever evaluated. Every debug image taken this session therefore showed
    // only the fragments that SURVIVED the discards, not the surface.
    //
    // A debug view has to short-circuit the shader, not live inside it.
    if (u_volDebug > 7.5 && u_volDebug < 8.5) {
        finalColor = vec4(0.502, 0.502, 0.502, 1.0);
        return;
    }
    // CAN TREN LA BAT BUOC — sua 06/08/2026, va thieu no da tra gia bang bon
    // vong chan doan.
    //
    // Nhanh nay tung la `> 8.5` khong co can tren. No la nhanh CUOI vao thoi
    // diem viet, nen "moi thu tu 8.5 tro len" la dung. Khi mode 10 va 11 duoc
    // them vao sau, `volume_debug = 10` roi thang vao day: ve DO THUAN roi
    // return, va mode 10 o duoi khong bao gio chay.
    //
    // Trieu chung la thu te nhat co the: mot man hinh TOAN DO khong doi khi
    // bat/tat cull, doi nguon phap tuyen, doi nguon vector nhin, doi viewPos.
    // Su BAT BIEN do bi doc thanh "loi he thong o ca N lan V" trong khi no
    // dung nghia la "phep do khong chay". Mot hang so thi khong the phan biet
    // voi mot dai luong that luon bang hang so do.
    //
    // LUAT: moi nhanh debug phai co CA HAI can. Nhanh cuoi cung khong duoc
    // huong dac quyen "mo ve phia tren", vi nguoi them mode tiep theo se
    // khong doc lai no. Khoa boi core/tests/beam_geometry_test.c.
    if (u_volDebug > 8.5 && u_volDebug < 9.5) {
        finalColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }
    // TWO SHEETS, TILED, PANNED AT DIFFERENT RATES, MULTIPLIED — now
    // core/uv's SurfaceFlow (u_flowLayer/u_flowMeta from uv_field.glsl,
    // pushed once per group from trail_system.c) instead of a hand-rolled
    // pan1/pan2/s1/s2 block. See u_volMask's own comment above for why this
    // exists and why MUL, not add. The fold-before-upload discipline
    // (ENGINE_LANDMINES §8: an unbounded u_time degenerates a noise domain
    // into flat blocks) is SurfaceFlow_PackGPU's job now, on the C side —
    // same rule, moved to where the fields it protects are packed.
    //
    // Sheet 2 wraps ONCE around too — same as sheet 1, deliberately. It used
    // to scale u by 2, which doubles the tongue count for that layer: 4
    // tongues around became 8, and since only half a cylinder faces the
    // camera the render showed 4 where the reference shows 2. The two layers
    // are decorrelated by their ALONG scale and their pan instead (pushed as
    // each SurfaceFlowLayer's own tiling/pan), which leaves the tongues
    // lined up in u and lets the product make them breathe — which is what
    // the reference's two strands actually do.
    vec2 mat = fragTexCoord;
    vec4 flowSample = SurfaceFlow_FieldSample(texture0, texture0, fragTexCoord, mat, u_time);
    // A = coverage in the OPAQUE layout; RGB is grey luminance kept grey so
    // the caller's tint survives.
    float pattern = flowSample.a;

    // COLOUR comes from sheet 1 ALONE, unmultiplied by sheet 2 — the blended
    // `flowSample` above multiplies rgb together too (SurfaceFlow's MUL
    // blend is component-wise on the whole vec4), which is right for
    // `pattern` (the alpha product IS the effect) but would tint the colour
    // by the second sheet's own grey value, which this file has never done.
    // Sample layer 0's own uv directly instead of reusing flowSample.rgb.
    vec4 s1 = texture(texture0, SurfaceFlow_FieldLayerUV(fragTexCoord, mat, 0));

    // EDGE — an optical-depth term times a silhouette rolloff.
    //
    // core/tests/silhouette_test.c established two things a per-fragment term
    // alone cannot fix: a grazing ray crosses an unbounded number of facets on
    // TWO-SIDED geometry (fixed below with `facing`/discard, not GL backface
    // culling — see there), and the power has to be at least 2 because |N.V|
    // leaves the silhouette with an infinite derivative (fixed in
    // trail_system.c's mask.y). Both are applied now, against a clean reading
    // this time.
    //
    // THE VIEW VECTOR. SETTLED 06/08/2026 BY MEASUREMENT — do not re-open it
    // from a debug view.
    //
    // fragPosition arrives from trail_volume.vs, which writes the vertex
    // attribute straight through WITHOUT matModel. On this draw path
    // (PMTube_DrawFaded = immediate mode, inside main.c's MyBeginMode3D) rlgl
    // has already transformed every vertex into VIEW space on the CPU. Read
    // trail_volume.vs's header for the numbers; the tripwire that proves it is
    // third_party/vulkan/tests/rlvk_visual_test.c `imm_normal`.
    //
    // In view space the camera sits at the origin, so the view vector is
    // -fragPosition and NO uniform is involved — nothing here can "fail to
    // arrive". `viewPos` is a WORLD coordinate and subtracting a view-space
    // position from it is what produced the inverted |N.V| this file spent a
    // session on. There used to be a `u_volViewSrc` switch between the two;
    // it is gone, because one of its two options is now known-wrong and a
    // permanent switch over a settled question only rots.
    vec3 V = normalize(-fragPosition);
    // NGUON PHAP TUYEN — cong tac, them 06/08/2026.
    //
    // 0 = attribute `fragNormal` (duong cu). 1 = dao ham man hinh cua chinh
    // fragPosition.
    //
    // Vi sao co cong tac nay: PMTube_DrawFaded ve bang IMMEDIATE MODE
    // (rlBegin/rlVertex3f/rlNormal3f), va mot attribute khong toi noi thi IM
    // LANG — fragNormal thanh hang so, dot(N,V) chi con bien thien qua V, va
    // ca than doc ra mot mau phang. core/tests/silhouette_test.c da neu dung
    // gia thuyet nay bang chu, va Test_FacetNormalsAreEnough do duoc rang
    // phap tuyen MAT PHANG (dung dFdx, khong can attribute nao) du de lam tan
    // bien — hardness xuong duoi nguong o >= 24 vanh.
    //
    // dFdx/dFdy cua fragPosition cho phap tuyen cua dung tam giac dang raster.
    // No khong the "khong toi noi": khong co attribute nao tham gia.
    //
    // HAI VAI, HAI VECTOR — sua 06/08/2026. Truoc do ca hai deu lay tu MOT
    // bien N, va khi nhanh dFdx them dong ep dau
    //     if (dot(N, V) < 0.0) N = -N;
    // thi `facing` (tinh tu chinh N do) tro thanh LUON >= 0, nen
    // `if (facing < 0.0) discard` khong bao gio chay: cull bi vo hieu hoa
    // boi chinh dong "sua" nam ngay tren no. Moi debug view duoi discard —
    // ke ca mode 12, cai duoc them ra de KHONG chong hai mat — van chong ca
    // hai mat.
    //
    // facing = CULL. Luon lay tu attribute, vi pm_tube.inl da ep no huong RA
    // NGOAI mot cach nhat quan (vong dung lai phap tuyen o cuoi
    // PMTube_BuildAlongPath), nen dau cua no co nghia. dFdx thi khong: tich
    // co huong khong biet chieu nao la "ngoai", va do la ly do dong ep dau
    // ton tai ngay tu dau.
    //
    // d = SHADING. Lay tu nguon duoc chon, va abs() lo phan dau — nen nhanh
    // dFdx khong con can ep dau nua.
    vec3 Nattr = normalize(fragNormal);
    float facing = dot(Nattr, V);
    vec3 N = (u_volNormalSrc > 0.5)
                 ? normalize(cross(dFdx(fragPosition), dFdy(fragPosition)))
                 : Nattr;
    float d = clamp(abs(dot(N, V)), 0.0, 1.0);
    // `d`, NOT `1.0 - d` — sửa 06/08/2026, và cái dấu đó là cả con bọ.
    //
    // Đây là ĐỘ DÀY QUANG HỌC, không phải fresnel. Với một hình trụ nhìn từ
    // xa, đoạn tia nằm TRONG khối dài nhất ở CHÍNH GIỮA thân (tia xuyên qua
    // đường kính) và ngắn dần ra rìa (tia chỉ sượt mép) — và với mặt trụ,
    // chiều dài đoạn đó tỉ lệ ĐÚNG với |N.V|. `1.0 - d` là công thức lớp vỏ
    // phát sáng ở viền (bong bóng xà phòng), tức ngược hẳn chiều.
    //
    // Đo được (core/tests/volume_optical_depth_test.c): bản `1.0 - d` cho
    // alpha = 0.000 ở ĐÚNG TÂM thân ống và đỉnh 0.318 ở 0.9 bán kính — một
    // cái VÀNH rỗng ruột. Đó chính là "khói tập trung ở 2 bên, mật độ thưa"
    // người dùng chụp lại được, và cũng là lý do phải kéo alpha lên mãi mà
    // vẫn mờ: chỗ đáng lẽ đặc nhất đang bị nhân với 0.
    //
    // core/tests/silhouette_test.c đã đo đúng dạng `|N.V|^p` này ngay từ
    // đầu (EDGE_NDOTV = powf(f, g_power), f = |dot(N,V)|) và chứng minh p>=2
    // + cull làm tan được viền. File đó tự ghi ở cuối là nó KHÔNG khoá vào
    // shader, vì lần áp trước bị revert do các quan sát kèm theo bị nhiễm
    // (debug view chỉ vẽ những fragment đã lọt qua hai discard). Lần này số
    // đọc đến từ ảnh chụp thật của người dùng, không qua debug view nào —
    // và giờ thì CÓ khoá, ở volume_optical_depth_test.c.
    // CHỌN ĐƯỢC, 06/08/2026, qua u_volMask.x (ô này đang BỎ TRỐNG — pan của
    // sheet 2 đã dọn sang s_volFlow, xem comment ở trail_system.c; tái dùng
    // ô cũ thay vì nới mảng để không đụng layout UBO của rlvk).
    //   0 = (1 - |N.V|), dạng VIỀN cũ — cột khói đã được chỉnh nhiều vòng
    //       quanh nó, và đổi đi làm cột "không tự nhiên như trước"
    //   1 = |N.V|, ĐỘ DÀY QUANG HỌC đúng vật lý — thứ smoke trail cần, xem
    //       core/tests/volume_optical_depth_test.c
    // Mặc định 0: một bản sửa đúng về vật lý vẫn là hồi quy nếu nó lấy mất
    // cái nhìn mà người dùng đã ưng. Bật `vol_depth_mode = 1` trong
    // tuning.cfg (kèm hạ vol_density) để lấy bản khối đặc ruột.
    // CỘNG, KHÔNG PHẢI mix — sửa 06/08/2026 sau khi công tắc nhị phân cho ra
    // hai lựa chọn đều không dùng được: mode 0 "tự nhiên nhưng thưa và dồn
    // sang bên", mode 1 "đều nhưng không còn giống khói".
    //
    // Lý do mix không cứu được: hai số hạng ĐỐI NHAU, nên nội suy giữa chúng
    // ở m=0.5 cho ra một hàm gần như PHẲNG — tức mất luôn cả hai đặc tính
    // thay vì có cả hai. Cộng thì giữ được cả hai, và cả hai đều có nghĩa vật
    // lý riêng trong render khói:
    //     pow(d, p)        THÂN — độ dày quang học, tia xuyên qua tâm dài nhất
    //     pow(1 - d, p)    RÌA — tán xạ ở mép mỏng, thứ làm mọi cục phình tự
    //                      vẽ ra đường viền và đọc ra "có khối"
    //
    // Tương thích ngược ĐÚNG TỪNG BIT: u_volMask.x = 0 và u_volRim = 1 cho
    // lại chính xác công thức cũ pow(1 - d, p). Đó là mặc định.
    //   mode 1 / rim 0    -> khối đặc thuần, không viền (cái "như sáp")
    //   mode 1 / rim ~0.4 -> khối CÓ viền, vùng mà công tắc nhị phân bỏ trống
    float body = calcOpticalDepthBody(d, u_volMask.y);
    float rimTerm = calcOpticalDepthRim(d, u_volMask.y);
    float thickBase = combineOpticalDepth(body, rimTerm, u_volMask.x, u_volRim);
    float depth = thickBase;
    // rim vẫn giữ: cùng chiều tăng với depth nên không đục lỗ ở giữa, chỉ
    // làm mềm thêm đúng vùng sát viền. u_volMask.z vẫn là "độ mềm viền".
    float rim = smoothstep(0.0, max(u_volMask.z, 0.001), d);
    float edge = depth * rim;

    // TECHNIQUE 2 — noise erosion of the silhouette (added 06/08/2026). See
    // the u_volErode uniform comment. `bR` = distance from the tube axis in
    // units of radius (1 = rim); the torn band reaches inward by
    // u_volErodeBand. The noise sample is the sheet itself at a higher
    // tiling + its own scroll, so the tear moves decorrelated from `pattern`.
    if (u_volErode > 0.001) {
        float bR = sqrt(max(0.0, 1.0 - d * d));
        float edgeBias = smoothstep(1.0 - u_volErodeBand, 1.0, bR);  // 0 thân, 1 rìa
        vec2 nuv = fragTexCoord * 3.0 + vec2(0.15, 0.09) * u_time;
        float n = texture(texture0, nuv).a;
        edge *= EDGE_EROSION_MASK(n, edgeBias, u_volErode);
    }

    // The vertical fade arrives as vertex alpha (PMTube_DrawFaded), already
    // multiplied by the layer's own alpha.
    float fade = vColor.a * colDiffuse.a;

    // Painted BEFORE the alpha gate, and fully opaque, so a term that is
    // silently zero still shows as black rather than as nothing at all — the
    // two are the same picture otherwise.
    // CAN TREN CHO CA KHOI, khong chi cho tung nhanh — sua 06/08/2026, LAN
    // THU HAI cua cung mot loi trong cung mot phien.
    //
    // Khoi nay ket thuc bang mot fallback khong can tren:
    //     float q = ... : fade;  finalColor = vec4(q,q,q,1); return;
    // nen MOI gia tri u_volDebug > 0.5 khong khop mode nao ben trong deu bi
    // no nuot va return — ke ca cac mode 11/12 nam DUOI discard. Trieu chung:
    // volume_debug = 12 ve ra anh XAM (fade) thay vi dai mau.
    //
    // Lan truoc la nhanh `> 8.5` khong can tren nuot mode 10. Lan nay la ca
    // KHOI khong can tren nuot mode 11 va 12. Cung mot hinh dang loi: mot
    // pham vi mo "bat het phan con lai" viet dung vao luc no la thu cuoi
    // cung, roi im lang nuot moi thu them sau no. Danh so mode ma khong dong
    // khung tung so la mot cai bay tich luy.
    if (u_volDebug > 0.5 && u_volDebug < 10.5) {
        // Directions as colour, so a wrong one is READABLE rather than merely
        // wrong: on a cylinder a correct radial normal sweeps the full hue
        // circle around the body, and a correct V barely changes across it.
        if (u_volDebug > 4.5 && u_volDebug < 5.5) {
            finalColor = vec4(N * 0.5 + 0.5, 1.0); return;
        }
        if (u_volDebug > 5.5 && u_volDebug < 6.5) {
            finalColor = vec4(V * 0.5 + 0.5, 1.0); return;
        }
        // THE DECISIVE ONE. fragPosition comes through matModel, and whether
        // that matrix is identity or model x view (ENGINE_LANDMINES §9) decides
        // whether a world-space camera minus this position means anything at
        // all. The two cases
        // differ by a MAGNITUDE, not by a pattern: the column stands near the
        // arena centre (6, 0, 4.4), so |P| is about 7 in world space and about
        // the camera distance — tens of metres — in view space. Scaled by 40 a
        // world-space read is dark grey and a view-space read is near white.
        //
        // A debug view that cannot tell the failure from success is worse than
        // none (core/CLAUDE.md §6), which is exactly why this paints a distance
        // to a known point and not fract() of a position.
        // MODE 10 — |N.V| thanh 5 DAI MAU roi rac, them 06/08/2026.
        //
        // Mode 2 da ve |facing| roi, nhung no la mot dai XAM LIEN TUC va mat
        // nguoi khong doc duoc "cho nay la 0.35 hay 0.45" tren mot dai nhu vay.
        // Cau hoi dang can tra loi lai chinh la mot cau hoi VI TRI: vet sang
        // that nam o |N.V| bao nhieu, va no co trung voi cho cong thuc dinh
        // dat no khong. Ranh gioi giua hai mau roi rac thi mat doc chinh xac
        // den vai pixel — day la ly do doi sang bands thay vi lam mode 2 sang
        // hon. Mot debug view khong phan dinh duoc thi te hon la khong co
        // (core/CLAUDE.md §6).
        //
        //   xanh duong 0.8-1.0  = nhin thang vao mat -> TAM cua silhouette
        //   xanh la    0.6-0.8
        //   vang       0.4-0.6
        //   cam        0.2-0.4
        //   do         0.0-0.2  = suot qua -> RIA hinh hoc
        if (u_volDebug > 9.5 && u_volDebug < 10.5) {
            vec3 band;
            // NaN TRUOC MOI THU KHAC — 06/08/2026, va thieu no la ly do ba
            // vong do vua roi khong phan dinh duoc gi.
            //
            // Voi NaN, MOI phep so sanh `>` deu false, nen chuoi if/else duoi
            // day roi thang xuong nhanh cuoi: "d rat nho" va "d vo nghia" ve
            // ra CUNG MOT MAU DO. Ma NaN la ket qua rat de xay ra o day —
            // normalize(vec3(0)) sinh ra no, va ca hai nguon phap tuyen deu co
            // the cho vector 0 (attribute khong toi noi; hoac dFdx cua mot
            // fragPosition khong bien thien tren be mat). Mot debug view khong
            // phan biet duoc thanh cong voi that bai thi te hon la khong co
            // (core/CLAUDE.md §6) — va no da im lang gop hai chan doan hoan
            // toan khac nhau lam mot trong suot ba vong.
            //
            // TRANG = NaN. `!(d >= 0.0)` la cach kiem NaN khong can isnan(),
            // vi moi so sanh voi NaN deu false.
            if (!(d >= 0.0)) { finalColor = vec4(1.0, 1.0, 1.0, 1.0); return; }
            if      (d > 0.8) band = vec3(0.20, 0.35, 1.00);
            else if (d > 0.6) band = vec3(0.20, 0.85, 0.30);
            else if (d > 0.4) band = vec3(0.95, 0.90, 0.20);
            else if (d > 0.2) band = vec3(1.00, 0.55, 0.10);
            else              band = vec3(1.00, 0.15, 0.15);
            finalColor = vec4(band, 1.0);
            return;
        }
        if (u_volDebug > 6.5 && u_volDebug < 7.5) {
            float q7 = clamp(length(fragPosition) / 40.0, 0.0, 1.0);
            finalColor = vec4(q7, q7, q7, 1.0); return;
        }
        float q = (u_volDebug < 1.5) ? edge
                : (u_volDebug < 2.5) ? abs(facing)
                : (u_volDebug < 3.5) ? pattern
                                     : fade;
        finalColor = vec4(q, q, q, 1.0);
        return;
    }

    // CULL — drop the fragment whose GEOMETRIC normal faces away from the
    // camera, exactly what core/tests/silhouette_test.c proved is required:
    // without it, a ray near the silhouette grazes the tube and crosses an
    // unbounded number of two-sided facets, and accumulated alpha ends up
    // HIGHER at the rim than at the centre — no per-fragment term can survive
    // that. NOT GL backface culling (winding-based): PMTube_DrawFaded's
    // winding is inward, so `rlEnableBackfaceCulling` would keep the INSIDE
    // of the tube and discard the outside — the shader checks the real
    // surface normal instead, which cannot be fooled by winding.
    //
    // Only in the non-debug path: a discard here would hide the back-facing
    // half of the tube from every debug view above, which is exactly the
    // "measuring what survived a discard" trap this file's header warns about.
    if (u_volCull > 0.5 && facing < 0.0) discard;

    // MODE 8 — pháp tuyến vẽ SAU discard, 06/08/2026. Cặp phân định với mode
    // 5, và lý do nó phải nằm ở ĐÂY chứ không cùng khối với các mode kia:
    // mọi debug view phía trên cố ý chạy TRƯỚC discard (để thấy trọn ống),
    // nghĩa là chúng vẽ CẢ HAI mặt đè lên nhau. Ở đoạn ống mảnh hai mặt chỉ
    // cách nhau vài cm nên rasteriser chọn mặt nào là tuỳ độ sâu — ra sọc
    // xen kẽ, và sọc đó ĐỔI THEO GÓC CAMERA. Đúng triệu chứng đang nghi.
    //   mode 5 có sọc, mode 11 KHÔNG -> sọc là của debug view, hình học sạch
    //   cả hai đều có sọc            -> pháp tuyến thật sự loạn
    // Một debug view không phân định được thành công với thất bại thì tệ hơn
    // là không có (core/CLAUDE.md §6) — mode 5 một mình chính là loại đó.
    if (u_volDebug > 10.5 && u_volDebug < 11.5) {
        finalColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }
    // MODE 12 — DAI MAU |N.V| VE SAU DISCARD. Ban dung de doc VI TRI BIEN.
    //
    // Mode 10 ve TRUOC discard cung moi debug view khac, va do la mot loi
    // thiet ke cho cau hoi nay: truoc discard thi CA HAI mat ong deu ve, voi
    // alpha = 1.0 duc va khong ghi depth, nen cai raster SAU de len cai
    // truoc. Thu tu do doi theo goc camera — nen anh doi "tum lum" ngay ca
    // khi N va V hoan toan dung, va moi ket luan rut ra tu no deu vo nghia.
    // Cung nguyen nhan voi nhung "vay" cyan/cam va nhung "dia tron / rang
    // nhon" quan sat duoc o mode 5 va mode 7.
    //
    // O day, sau discard, chi con mat huong ve camera. Voi mot hinh tru thi
    // ket qua phai BAT BIEN theo goc: xanh duong o tam silhouette, do o ria.
    // Neu no VAN doi theo goc thi luc do moi la loi that.
    // MODE 13 — VE THANG DUONG BIEN LEN TRU. Y cua chu du an, va no la cach
    // don gian nhat de doi chieu, thay vi doc mot dai mau roi suy dien.
    //
    // core/tests/silhouette_test.c do duoc: so hang RIM dang ship dat diem
    // sang nhat o b/R = 0.960 (b = khoang cach tu truc, 1.0 = ria hinh hoc),
    // gia tri tai truc = 0.000 dung bang khong. Voi mot hinh tru nhin tu xa,
    // b/R = 0.960 tuong ung |N.V| = sqrt(1 - 0.96^2) = 0.280.
    //
    // Nen: to TRANG dung cai vanh |N.V| ~ 0.280, con lai to xam dam. Duong
    // trang do LA vi tri ma cong thuc dat bien. Chup mode 13 va mode 0 o
    // CUNG mot goc roi dat canh nhau:
    //   vet sang that TRUNG duong trang -> bien dung cho, van de la thu khac
    //   vet sang that LECH duong trang  -> bien that su sai vi tri
    // Khong con gi de suy dien: hai anh, mot cau tra loi.
    // MODE 14 — GOC GIUA hai nguon phap tuyen. Phep thu tu than: mot anh, khong
    // phu thuoc camera, khong phu thuoc V, khong phu thuoc uniform nao.
    //
    //   TRANG = |dot(Nattr, Ndfdx)| ~ 1  -> hai vector TRUNG nhau (khac dau
    //           cung duoc, abs lo). Attribute dung.
    //   DEN   = ~ 0                      -> VUONG GOC. Attribute khong phai
    //           phap tuyen ma la mot TIEP TUYEN cua be mat.
    //
    // Ndfdx la phap tuyen hinh hoc that cua chinh tam giac dang raster: no
    // duoc dung tu dao ham cua fragPosition, khong co attribute nao tham gia,
    // nen no khong the "khong toi noi" va khong the sai huong (chi co the sai
    // DAU, ma abs() lo roi).
    //
    // Ly do can mode nay: mode 13 do duoc b/R DAO NGUOC (do o TAM, xanh o
    // MEP), tuc |N.V| nho nhat o dung cho no phai lon nhat. Cach duy nhat de
    // dieu do xay ra la N vuong goc voi phap tuyen that o moi diem.
    // MODE 15/16 — DO DO LON cua viewPos va fragPosition thanh DAI MAU.
    //
    // TUBE_NDOTV_CPU (do tren CPU, khong qua shader) vua cho: |N.V| quet
    // 0.049..0.990 — mesh va phap tuyen LANH. Nen loi nam giua CPU va shader.
    // Cung dong log do: ring0 = (5.3, 1.2, 4.7) nen |P_world| = 7.2, va
    // camera o (9.4, 4.8, 9.4) nen khoang cach toi ring0 CUNG = 7.2. Tuc
    // |fragPosition| phai bang 7.2 du la world hay view — mot con so KIEM
    // DUOC, khac han viec doan khong gian tu mot vet xam.
    //
    //   do   < 5      cam  5-10     vang 10-20     luc 20-40     xanh > 40
    // Ky vong: mode 16 (fragPosition) ra CAM. Neu ra mau khac thi
    // fragPosition khong phai vi tri dinh o BAT KY khong gian nao, va do la
    // goc cua moi thu.
    // Ky vong: mode 15 (viewPos) ra VANG (|cam| = 14.1). Neu ra DO tham/den
    // thi viewPos = (0,0,0), tuc uniform khong toi noi du GetShaderLocation
    // tra ve mot loc hop le.
    if (u_volDebug > 14.5 && u_volDebug < 16.5) {
        float mag = (u_volDebug < 15.5) ? length(viewPos) : length(fragPosition);
        vec3 c;
        if      (mag <  5.0) c = vec3(1.00, 0.15, 0.15);
        else if (mag < 10.0) c = vec3(1.00, 0.55, 0.10);
        else if (mag < 20.0) c = vec3(0.95, 0.90, 0.20);
        else if (mag < 40.0) c = vec3(0.20, 0.85, 0.30);
        else                 c = vec3(0.20, 0.35, 1.00);
        finalColor = vec4(c, 1.0);
        return;
    }
    if (u_volDebug > 13.5 && u_volDebug < 14.5) {
        vec3 Ng = normalize(cross(dFdx(fragPosition), dFdy(fragPosition)));
        float agree = abs(dot(Nattr, Ng));
        finalColor = vec4(vec3(agree), 1.0);
        return;
    }
    if (u_volDebug > 12.5 && u_volDebug < 13.5) {
        // THANG DONG MUC theo b/R, khong phai mot vanh don.
        //
        // Ban dau mode nay ve DUNG MOT vanh tai |N.V| = 0.280 va no "luc co
        // luc khong": voi flat normal (vol_normal_src = 1) moi mat phang cua
        // ong co MOT normal hang so, nen tren 16 canh thi d chi nhan 16 gia
        // tri ROI RAC. Mot dai rong 0.06 quanh 0.280 chi bat duoc khi tinh co
        // co mat nao roi vao do — camera xoay thi cac gia tri dich di va vanh
        // bien mat. Do la hien vat cua DUNG CU, khong phai cua bien.
        //
        // Hai thay doi de het mo ho:
        //  1. LUON dung phap tuyen NOI SUY (Nattr) o day, bat ke
        //     vol_normal_src. Muc dich cua mode nay la doc VI TRI tren mot be
        //     mat lien tuc; flat normal bien no thanh 16 bac thang.
        //  2. Ve DONG MUC moi 0.2 cua b/R thay vi mot vanh. Mot vanh don co
        //     the truot khoi khung hinh hoac roi vao khe giua hai bac ma
        //     nguoi xem khong biet; mot thang thi luon co mat va dem duoc.
        //
        // b = khoang cach tu truc, 1.0 = ria hinh hoc. Voi hinh tru nhin tu
        // xa: b/R = sqrt(1 - (N.V)^2).
        float ds = abs(dot(Nattr, V));
        float bR = sqrt(max(0.0, 1.0 - ds * ds));

        // Vach TRANG DAM tai b/R = 0.960 — noi silhouette_test.c do duoc so
        // hang dang ship dat diem sang nhat. Day la duong PHAI trung voi vet
        // sang trong anh volume_debug = 0 cung goc.
        float peakLine = 1.0 - smoothstep(0.0, 0.012, abs(bR - 0.960));

        // Dong muc moi 0.2 (0.2 / 0.4 / 0.6 / 0.8) lam thuoc do, mo hon.
        // Dong muc 0.2/0.4/0.6/0.8. KHONG ve o b/R = 0: dai den rong o giua
        // (dong muc 0, tuc TAM silhouette) bi doc nham thanh "bien" — dung
        // loi trinh bay, vi bien la b/R = 1.0 chu khong phai 0.
        float g = abs(fract(bR * 5.0) - 0.5) - 0.47;
        float grid = 1.0 - smoothstep(0.0, 0.010, g);
        if (bR < 0.10) grid = 1.0;   // bo dong muc 0

        // BIEN HINH CHIEU, b/R = 1.0 — dinh nghia cua chu du an: duong bao
        // cua hinh chieu 2D. To DO de doi chieu truc tiep: vach TRANG (0.960)
        // phai nam NGAY SAT vach DO, va la vach ngoai cung trong thang.
        float rimLine = smoothstep(0.975, 1.0, bR);

        vec3 col = vec3(0.06, 0.06, 0.10);
        col = mix(col, vec3(0.35, 0.45, 0.60), grid);
        col = mix(col, vec3(1.0, 1.0, 1.0), peakLine);
        col = mix(col, vec3(1.0, 0.15, 0.15), rimLine);
        finalColor = vec4(col, 1.0);
        return;
    }
    if (u_volDebug > 11.5 && u_volDebug < 12.5) {
        vec3 band;
        if (!(d >= 0.0)) { finalColor = vec4(1.0, 1.0, 1.0, 1.0); return; }
        if      (d > 0.8) band = vec3(0.20, 0.35, 1.00);
        else if (d > 0.6) band = vec3(0.20, 0.85, 0.30);
        else if (d > 0.4) band = vec3(0.95, 0.90, 0.20);
        else if (d > 0.2) band = vec3(1.00, 0.55, 0.10);
        else              band = vec3(1.00, 0.15, 0.15);
        finalColor = vec4(band, 1.0);
        return;
    }

    float alpha = pattern * fade * edge * u_volMask.w;
    if (u_volDebug < 0.5 && alpha < 0.003) discard;

    vec3 colour = s1.rgb * vColor.rgb * colDiffuse.rgb;
    finalColor = vec4(colour, alpha);
}
