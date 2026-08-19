#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;   
uniform sampler2D u_bloomTex; 
// Texel size of u_bloomTex (a QUARTER-resolution target), for the tent below.
uniform vec2 u_bloomTexel;

// Cấu hình
uniform float u_bloomEnabled;
uniform float u_bloomIntensity;

uniform float u_chromaticEnabled;
uniform float u_chromaticStrength;

uniform float u_vignetteEnabled;
uniform float u_vignetteRadius;
uniform float u_vignetteSoftness;

uniform float u_colorGradeEnabled;
uniform float u_contrast;
uniform float u_saturation;
uniform vec3 u_colorTint;

uniform vec3 u_shadowTint;
uniform vec3 u_highlightTint;

uniform float u_tonemapEnabled;
uniform float u_exposure;

// ── Colour-grade LUT (Đợt G5) ────────────────────────────────────────────────
// A 2D STRIP, not a sampler3D: GLES on Mali is a shipping target and 2D works
// everywhere without a capability query or a second shader path.
uniform sampler2D u_lutTex;
uniform float u_lutEnabled;
uniform float u_lutStrength;  // 0..1 blend toward the graded result
uniform vec2  u_lutParams;    // (1/stripWidth, 1/stripHeight)
uniform float u_lutSize;      // slices per axis (strip is size*size by size)

// Radial blur (Đợt E1a) — screen-space smear away from a focal point. This is
// what makes a burst read as VIOLENT rather than merely bright: brightness
// alone says "there is light here", a directional smear says "something just
// happened here". Folded into the composite rather than run as its own pass —
// one extra pass costs a full-screen read/write, this costs 8 taps only while
// it is on, and the whole loop sits behind a uniform branch so the OFF path is
// a single branch on a value that is constant across the frame.
uniform float u_radialBlurEnabled;
uniform vec2  u_radialBlurCenter;   // screen UV [0..1] of the focal point
uniform float u_radialBlurStrength; // sample offset scale (~0.15 = strong)
uniform float u_radialBlurFalloff;  // UV radius where the blur reaches full strength

out vec4 finalColor;

// Strip-LUT lookup. Red/green come free from hardware bilinear because they map
// to texel CENTRES inside a tile (0.5 .. size-0.5), which is also what keeps
// filtering from bleeding across a tile edge into the next blue slice. Blue is
// interpolated by hand between two adjacent slices — the one thing a 2D strip
// costs over a real sampler3D.
vec3 ApplyColorGradeLut(vec3 col) {
    float size = u_lutSize;
    vec3 c = clamp(col, 0.0, 1.0);

    float b = c.b * (size - 1.0);
    float slice0 = floor(b);
    float slice1 = min(slice0 + 1.0, size - 1.0);
    float blend = b - slice0;

    float uInTile = 0.5 + c.r * (size - 1.0);
    float v = (0.5 + c.g * (size - 1.0)) * u_lutParams.y;

    vec3 s0 = texture(u_lutTex, vec2((slice0 * size + uInTile) * u_lutParams.x, v)).rgb;
    vec3 s1 = texture(u_lutTex, vec2((slice1 * size + uInTile) * u_lutParams.x, v)).rgb;
    return mix(s0, s1, blend);
}

vec3 acesFilmic(vec3 x) {
    // Đã in-line các hằng số để tránh khai báo biến const trong hàm
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// ── Hue-preserving highlight restoration (CANDIDATE, default OFF) ─────────────
//
// Why: the ACES fit above is applied PER CHANNEL, so as one channel enters the
// shoulder the others keep climbing and the hue slides toward white. Measured on
// the bright_vfx chart, a saturated body over a white background loses chroma
// 0.539 -> 0.383 -> 0.222 across exposures 0.5/1/2. That is the single largest
// remaining obstacle to bright-background VFX, and no amount of authoring fixes it.
//
// How this stays approvable: it is a BUMP, not a replacement.
//   * peak < 1.0  -> w = 0 -> the branch is not taken and the result is BIT-IDENTICAL
//                    to the curve every already-approved material was authored against.
//                    This is what keeps a tone-mapper change from being a whole-scene
//                    change; rlvk's `tonemap_shoulder` scenario asserts it.
//   * 1.0 .. 5.0  -> hue restoration ramps in. This is where the saturated body and
//                    corona live (§5.4) and where the chroma is currently lost.
//   * above 5.0   -> it ramps back OUT, so a genuinely hot core still reaches white.
//                    Pure hue preservation would keep the core saturated forever,
//                    which contradicts §5.4's "the compact core may reach white".
//
// One knob only (`postfx_hue_restore`, 0..1) so the approval surface is one number.
float acesFilmicScalar(float x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

uniform float u_hueRestore;    // shipping default 0.6 (§12.1); 0 = the old per-channel curve
uniform float u_shoulderView;  // diagnostic, 0 = off

vec3 toneMapScene(vec3 x) {
    vec3 perChannel = acesFilmic(x);
    if (u_hueRestore <= 0.0) return perChannel;

    float peak = max(x.r, max(x.g, x.b));
    if (peak <= 0.0) return perChannel;

    // CANDIDATE H, APPLIED 18/08/2026. The weight is now CONSTANT in intensity, and the
    // whitening that used to come from ramping it back out is a monotone desaturation of
    // the hue-kept colour instead.
    //
    // WHY THE OLD FORM HAD TO GO. Hue keeping restores chroma by LOWERING the non-peak
    // channels. If the weight VARIES along an intensity ramp, those channels are pulled
    // down and then released — a trough with an edge on each side, which is a visible
    // colour band. Measured on a plain rectangle through this exact pipeline
    // (sandbox/gradient_probe.c): one hue at a rising level gave a G slope of
    // +26 -> +9 -> -10 -> +9, a stall, a reversal and a re-acceleration, on an input that
    // is smooth by construction. That is the "rainbow rim" reported on ShieldShell, and it
    // belongs to this function, not to any effect.
    //
    // AND THE BANDING CAME FROM THE *LOWER* BOUND, NOT THE UPPER ONE. That is the opposite
    // of what was assumed for two sessions. Any weight that transitions from 0 to non-zero
    // WHILE THE INPUT IS STILL CLIMBING produces the trough; whether it later ramps back
    // out is irrelevant. Verified by search over the whole family: widening the rise
    // (smoothstep(1,2) -> (1,7)) makes it WORSE (15 -> 30 reversals), because the drop is
    // ~max(w * (perChannel - hueKept)) and does not care how gently w got there; and
    // bolting a bottom gate back onto this candidate restores the banding exactly
    // (0 reversals -> 59). "Bit-identical below peak 1" and "monotone through the shoulder"
    // are therefore mutually exclusive, and this file now chooses monotone.
    //
    // THE PRICE, STATED PLAINLY: rlvk's `tonemap_shoulder` asserts bit-identity below
    // peak 1 and above peak 9, and it now FAILS by design. Every material below the
    // shoulder shifts by up to ~0.03 at u_hueRestore = 1.0 (~0.015, about 4/255, at the
    // shipping 0.5-0.6), and a hot core reaches white more completely. That is a
    // whole-scene tone-map change, taken deliberately by the owner rather than a bounded
    // bump. Reverting is this one block; the shader hot-loads, so no rebuild either way.
    float w = clamp(u_hueRestore, 0.0, 1.0);
    if (w <= 0.0) return perChannel;

    // Tone map the PEAK and carry the channel ratios through, desaturating them toward
    // white as the level climbs so a genuinely hot core still reaches white (§5.4) —
    // monotonically, which is the whole point.
    vec3 hueKept = mix(x / peak, vec3(1.0), smoothstep(5.0, 12.0, peak)) * acesFilmicScalar(peak);
    return mix(perChannel, hueKept, w);
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 toCenter = uv - vec2(0.5);

    // [TỐI ƯU 1]: Chỉ lấy mẫu texture0 MỘT LẦN duy nhất làm gốc.
    vec4 sceneCol = texture(texture0, uv);

    // [TỐI ƯU 2]: Uniform Branching (Rẽ nhánh Uniform).
    // Vì u_chromaticEnabled đồng nhất trên toàn màn hình, lệnh 'if' này có chi phí gần như bằng 0 
    // và cứu GPU khỏi 2 lần fetch texture thừa khi hiệu ứng tắt.
    if (u_chromaticEnabled > 0.5) {
        
        // [TỐI ƯU 3]: Thay length() bằng dot() để triệt tiêu hàm căn bậc 2 (sqrt) đắt đỏ.
        // Falloff bằng bình phương (distSq) sẽ tạo cảm giác méo dồn ra viền màn hình tự nhiên hơn.
        float distSq = dot(toCenter, toCenter);
        vec2 offset = toCenter * (u_chromaticStrength * distSq * 0.05);
        
        sceneCol.r = texture(texture0, uv - offset).r;
        // Kênh .g đã có sẵn trong sceneCol từ lần fetch trên cùng, KHÔNG FETCH LẠI!
        sceneCol.b = texture(texture0, uv + offset).b;
    }

    // 1b. Radial blur — AFTER chromatic (so it smears the aberrated image, not
    // the other way round, which would re-sharpen the fringes) and BEFORE bloom,
    // so the bloom that follows blooms the smear rather than the sharp source.
    if (u_radialBlurEnabled > 0.5) {
        vec2  dir = uv - u_radialBlurCenter;
        // The focal point stays SHARP and the smear grows outward. Blurring
        // uniformly reads as a camera fault; blurring around a fixed point
        // reads as force radiating from that point.
        float w = smoothstep(0.0, u_radialBlurFalloff, length(dir));

        // FADE OUT AN OFF-SCREEN FOCAL POINT. Two reasons, one physical and one
        // practical. Physical: a burst you cannot see should not smear the whole
        // frame — the smear is supposed to say "the force came from THERE", and
        // there is no there on screen. Practical: `dir` grows without bound as
        // the centre leaves the view, and long taps stop reading as a smear and
        // start reading as discrete GHOST COPIES of the frame. Both were found
        // by the E3 TAIJI_LOI port, whose strike lands away from the camera.
        vec2  c   = u_radialBlurCenter;
        float off = max(max(-c.x, c.x - 1.0), max(-c.y, c.y - 1.0)); // 0 inside the view
        w *= 1.0 - smoothstep(0.0, 0.35, off);

        if (w > 0.001) {
            // CAP THE SMEAR LENGTH. `dir` grows without bound as the focal point
            // moves off-screen — an effect 17 m to the side projects to a UV far
            // outside [0,1], and the 8 taps then land so far apart that they read
            // as discrete GHOST COPIES of the frame instead of a smear. Found by
            // the E3 TAIJI_LOI port, where the strike lands away from the camera
            // target. Capping keeps the tap spacing sane at any distance and
            // costs one clamp.
            vec2 smear = dir * (u_radialBlurStrength * w);
            float smearLen = length(smear);
            if (smearLen > 0.08) smear *= (0.08 / smearLen);
            vec3 acc = vec3(0.0);
            for (int i = 1; i <= 8; i++) {
                acc += texture(texture0, uv - smear * (float(i) / 8.0)).rgb;
            }
            sceneCol.rgb = mix(sceneCol.rgb, acc * 0.125, w);
        }
    }

    // 2. Bloom
    if (u_bloomEnabled > 0.5) {
        // TENT, NOT ONE TAP. u_bloomTex is a QUARTER-resolution target, so a single
        // bilinear fetch magnifies it 4x — and bilinear magnification of a buffer that
        // carries real detail reconstructs as piecewise-linear patches with a kink every
        // 4 pixels. Along a bright curved silhouette those kinks read as stair-steps:
        // the "hạt hạt pixel" report. It only surfaced once bloom_scatter went back to
        // 0.65: at 1.0 the pyramid collapsed to its coarsest mip, so bloomTex held
        // nothing but smooth low frequencies and there was no detail to alias.
        //
        // The same 3x3 tent the upsample chain already uses (bloom_upsample.fs) is the
        // standard final-upsample filter for exactly this reason. Eight extra taps in one
        // fullscreen pass, no new render targets, and the halo's total energy is
        // unchanged because the kernel is normalised.
        vec3 bloomSum = texture(u_bloomTex, uv + vec2(-1.0,  1.0) * u_bloomTexel).rgb * 1.0
                      + texture(u_bloomTex, uv + vec2( 0.0,  1.0) * u_bloomTexel).rgb * 2.0
                      + texture(u_bloomTex, uv + vec2( 1.0,  1.0) * u_bloomTexel).rgb * 1.0
                      + texture(u_bloomTex, uv + vec2(-1.0,  0.0) * u_bloomTexel).rgb * 2.0
                      + texture(u_bloomTex, uv                                  ).rgb * 4.0
                      + texture(u_bloomTex, uv + vec2( 1.0,  0.0) * u_bloomTexel).rgb * 2.0
                      + texture(u_bloomTex, uv + vec2(-1.0, -1.0) * u_bloomTexel).rgb * 1.0
                      + texture(u_bloomTex, uv + vec2( 0.0, -1.0) * u_bloomTexel).rgb * 2.0
                      + texture(u_bloomTex, uv + vec2( 1.0, -1.0) * u_bloomTexel).rgb * 1.0;
        sceneCol.rgb += (bloomSum / 16.0) * u_bloomIntensity;
    }

    // 2b. Tone mapping
    vec3 exposedScene = sceneCol.rgb * u_exposure;
    if (u_tonemapEnabled > 0.5) {
        sceneCol.rgb = toneMapScene(exposedScene);
    }

    // 3. Color Grading
    if (u_colorGradeEnabled > 0.5) {
        vec3 gradedCol = sceneCol.rgb;
        
        // Contrast
        gradedCol = (gradedCol - vec3(0.5)) * u_contrast + vec3(0.5);
        
        // [TỐI ƯU 4]: Tính toán Luminance (độ sáng) ĐÚNG 1 LẦN.
        // Chuyển vec3 magic number thành hằng số (const) để compiler tối ưu register.
        const vec3 LUMA_COEFF = vec3(0.2126, 0.7152, 0.0722);
        float luma = dot(gradedCol, LUMA_COEFF);
        
        // Saturation
        gradedCol = mix(vec3(luma), gradedCol, u_saturation);
        
        // Split-tone
        // [TỐI ƯU 5]: Xóa bỏ hàm clamp(). 
        // Hàm smoothstep nội bộ đã tự động clamp giá trị đầu vào trong khoảng [edge0, edge1] rồi.
        gradedCol *= mix(u_shadowTint, u_highlightTint, smoothstep(0.0, 1.0, luma));
        
        // Color Tint
        sceneCol.rgb = gradedCol * u_colorTint;
    }

    // 3b. Colour-grade LUT (Đợt G5). Deliberately LAST in the colour chain and
    // AFTER tone mapping: a LUT is a display-referred lookup, so feeding it HDR
    // values would sample outside its own domain and flatten every highlight
    // into the top blue slice. The formula grade above stays as the coarse
    // control; the LUT carries the "look".
    if (u_lutEnabled > 0.5 && u_lutStrength > 0.0) {
        vec3 graded = ApplyColorGradeLut(sceneCol.rgb);
        sceneCol.rgb = mix(sceneCol.rgb, graded, clamp(u_lutStrength, 0.0, 1.0));
    }

    // 4. Vignette
    if (u_vignetteEnabled > 0.5) {
        // [TỐI ƯU 6]: Tái sử dụng vector toCenter đã tính từ bước đầu tiên thay vì tính lại.
        float len = length(toCenter);
        float darkness = smoothstep(u_vignetteRadius - u_vignetteSoftness, u_vignetteRadius, len);
        sceneCol.rgb *= (1.0 - darkness);
    }

    // 5. Output dither.
    //
    // The swapchain is 8-bit. The widest, flattest gradients in this game are
    // exactly the things this pipeline is built to produce - a 32 px VFX halo and
    // the bloom skirt around it - and on a bright background those quantise into
    // visible concentric rings ("bet mau"). One LSB of triangular-PDF noise breaks
    // the ring into dither and costs two hashes.
    //
    // NO sin() IN THE HASH. fract(sin(x)) noise degenerates on Mali at large
    // domains (ENGINE_LANDMINES.md / the Mali hash landmine) and gl_FragCoord IS a
    // large domain; this is Jimenez's interleaved-gradient hash, which is sin-free.
    float d0 = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float d1 = fract(52.9829189 * fract(dot(gl_FragCoord.xy + 17.0, vec2(0.06711056, 0.00583715))));
    sceneCol.rgb = clamp(sceneCol.rgb + vec3((d0 + d1 - 1.0) * (1.0 / 255.0)), 0.0, 1.0);
    // Clamp AFTER the dither, not before: the noise is added on top of an already
    // tone-mapped value, so without this a channel that ACES had pinned at 1.0 comes
    // back at 1.004. The UNORM swapchain hides it, but an HDR probe reading this pass
    // sees post-tone-map values the curve cannot produce, which is exactly the kind of
    // "impossible number" that sent bright_vfx chasing a phantom.

    // ── Shoulder view (DIAGNOSTIC: tuning.cfg postfx_shoulder_view = 1) ──────
    //
    // Paints where the tone map's hue-restoration band is even allowed to act, so
    // "which materials would need re-approving" is one capture instead of a diff and a
    // judgement call. Anything not magenta is provably untouched.
    //   magenta = exposed peak in [1, 9)  -> the active band, i.e. the whole surface
    //   cyan    = exposed peak >= 9       -> above the band, the curve is identity
    //   grey    = below 1.0               -> identity, nothing to approve
    //
    // KEPT after §12.1 was decided, not deleted, because §11b's gate 3 EXPIRES: it
    // passed only because every map is night-time and exposure is pinned at 1.00. Add a
    // daylight arena or auto-exposure and ground and sky cross 1.0, which regrows the
    // approval surface — and this is how you find that out in one screenshot. It was a
    // negative value of postfx_hue_restore while that knob was being A/B'd (no rebuild
    // needed for a shader-only change); now that hue restore ships at 0.6 it gets its
    // own knob rather than riding on a shipping one.
    if (u_shoulderView > 0.5) {
        float peak = max(exposedScene.r, max(exposedScene.g, exposedScene.b));
        float g = dot(sceneCol.rgb, vec3(0.2126, 0.7152, 0.0722)) * 0.30;
        if      (peak >= 9.0) sceneCol.rgb = vec3(0.0, 0.9, 1.0);
        else if (peak >= 1.0) sceneCol.rgb = vec3(1.0, 0.0, 0.8);
        else                  sceneCol.rgb = vec3(g);
    }

    finalColor = sceneCol * fragColor;
}
