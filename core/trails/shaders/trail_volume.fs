#version 330
#include "core/shaders/common/fs_header.glsl"
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
// SPACE. fragPosition/fragNormal come through vs_header's VS_FinalOutput.
// `viewPos - fragPosition` was suspect for a session (ENGINE_LANDMINES §9:
// matModel = model×view inside a 3D pass, so fragPosition is view space, and
// subtracting a world-space viewPos from it is wrong) — confirmed for one
// draw path and NOT the other (postscript, 04/08/2026, sandbox/fresnel_probe.c):
// `DrawMesh` (crystal's real draw call) genuinely gives view-space
// fragPosition, so crystal's `viewPos - fragPosition` IS broken. Immediate-mode
// (`rlBegin`/`rlVertex3f` — 8 of the 9 files in core/geometry/pm_*.inl,
// including PMTube_DrawFaded, what draws THIS column) gives WORLD-space
// fragPosition instead, so `viewPos - fragPosition` (both world) is correct
// here. The two draw paths do not share a matModel convention; do not port a
// fix from one to the other by analogy.

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
uniform float u_volDebug;

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
    if (u_volDebug > 8.5) {
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
    // this time — see ENGINE_LANDMINES §9's postscript (04/08/2026) for how
    // that reading was obtained: sandbox/fresnel_probe.c, drawn via DrawMesh
    // (matching this file's own DrawMesh comparison point, crystal) AND via
    // immediate-mode (matching PMTube_DrawFaded, what actually draws this
    // column). The two draw paths do not share a `matModel` convention:
    // immediate-mode's fragPosition reads as WORLD space, so `viewPos -
    // fragPosition` (both world) is correct HERE — do not "fix" it to
    // `-fragPosition` by analogy with crystal, which is DrawMesh and genuinely
    // different.
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPosition);
    float facing = dot(N, V);
    float d = abs(facing);
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
    float thickBase = mix(1.0 - d, d, clamp(u_volMask.x, 0.0, 1.0));
    float depth = pow(clamp(thickBase, 0.0, 1.0), max(u_volMask.y, 0.001));
    // rim vẫn giữ: cùng chiều tăng với depth nên không đục lỗ ở giữa, chỉ
    // làm mềm thêm đúng vùng sát viền. u_volMask.z vẫn là "độ mềm viền".
    float rim = smoothstep(0.0, max(u_volMask.z, 0.001), d);
    float edge = depth * rim;

    // The vertical fade arrives as vertex alpha (PMTube_DrawFaded), already
    // multiplied by the layer's own alpha.
    float fade = vColor.a * colDiffuse.a;

    // Painted BEFORE the alpha gate, and fully opaque, so a term that is
    // silently zero still shows as black rather than as nothing at all — the
    // two are the same picture otherwise.
    if (u_volDebug > 0.5) {
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
        if (u_volDebug > 6.5) {
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
    if (facing < 0.0) discard;

    float alpha = pattern * fade * edge * u_volMask.w;
    if (u_volDebug < 0.5 && alpha < 0.003) discard;

    vec3 colour = s1.rgb * vColor.rgb * colDiffuse.rgb;
    finalColor = vec4(colour, alpha);
}
