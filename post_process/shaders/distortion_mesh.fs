#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 v_clipPos;

// Messiah Engine / Where Winds Meet Style Screen-Space Mesh Distortion Pass
// Supersonic sword shockwave & martial arts air slash refraction
// High-energy optical lens warping, heat turbulence, and chromatic dispersion.

uniform sampler2D texture0;            // Flow / Normal map texture
uniform sampler2D u_sceneTex;          // Background scene snapshot texture
uniform int       u_hasScene;
uniform float     u_distortionStrength;// Displacement amplitude
uniform vec2      u_flowSpeed;         // UV scroll rate
uniform vec4      u_tintColor;         // RGB tint, A = tint blend factor
uniform float     u_edgeFade;          // Global edge fade multiplier
uniform float     u_time;              // Animation time
uniform vec2      u_resolution;        // Scene target resolution

out vec4 finalColor;

void main() {
    // Bảo vệ chia phối cảnh khi mesh cắt qua mặt phẳng cận của camera (w <= 0)
    float invW = 1.0 / max(v_clipPos.w, 0.0001);
    vec2 screenUV = (v_clipPos.xy * invW) * 0.5 + 0.5;

    // True screen-space forward cutting direction via partial derivatives:
    vec2 gradY = vec2(dFdx(fragTexCoord.y), dFdy(fragTexCoord.y));
    float gradYLenSq = dot(gradY, gradY);
    vec2 shockDir = (gradYLenSq > 1e-10) ? (gradY * inversesqrt(gradYLenSq)) : vec2(0.0, 1.0);

    // Strictly orthogonal lateral direction: dot(shockDir, perpDir) == 0.0
    vec2 perpDir = vec2(-shockDir.y, shockDir.x);

    float x = clamp(fragTexCoord.x, 0.0, 1.0);
    float y = clamp(fragTexCoord.y, 0.0, 1.0);

    // Wave profile: strong compression at the leading edge (y -> 1.0), smooth aerodynamic wake
    float sinY = sin(y * 3.14159265);
    float wave = sinY * (0.45 + 0.55 * y);

    // Smooth, low-frequency dynamic heat turbulence (no speckle noise)
    float ripple = sin(x * 4.0 - u_time * 8.0 + y * 2.5) * 0.12;

    // Lateral expansion along the arc: -1.0 on left wing, +1.0 on right wing
    float lateralCurve = (x - 0.5) * 2.0;

    // Orthogonal displacement field: forward shock + symmetric lateral bow
    vec2 lensVector = shockDir * (wave + ripple) + perpDir * (lateralCurve * wave * 0.22);

    // Flow map texture sampling (optional)
    vec2 flowUV = fragTexCoord + u_flowSpeed * u_time;
    vec4 flowSample = texture(texture0, flowUV);
    if (flowSample.a > 0.05) {
        lensVector += (flowSample.rg * 2.0 - 1.0) * (0.20 * flowSample.a);
    }

    // Arc span envelope + soft screen-border guard so UV clamping never stretches edge texels
    float tipFade = smoothstep(0.0, 0.08, x) * smoothstep(1.0, 0.92, x);
    float spanFactor = mix(0.75, 1.0, sin(x * 3.14159265));
    vec2 borderDist = min(screenUV, 1.0 - screenUV);
    float borderFade = smoothstep(0.0, 0.04, min(borderDist.x, borderDist.y));
    float fadeMul = (u_edgeFade > 0.0) ? clamp(u_edgeFade, 0.0, 1.0) : 1.0;
    float envelope = tipFade * spanFactor * fadeMul;

    // Pronounced, cinematic displacement magnitude
    float strength = (u_distortionStrength > 0.0001) ? u_distortionStrength : 0.11;
    vec2 disp = lensVector * (strength * envelope * borderFade);

    // Clean, high-fidelity chromatic dispersion (tight 2.5% spread: crisp, optical, zero noise)
    vec3 sceneColor;
    if (u_hasScene != 0) {
        vec2 uvMin = (u_resolution.x > 0.0) ? (0.5 / u_resolution) : vec2(0.002);
        vec2 uvMax = 1.0 - uvMin;
        vec2 uvR = clamp(screenUV + disp * 1.025, uvMin, uvMax);
        vec2 uvG = clamp(screenUV + disp,         uvMin, uvMax);
        vec2 uvB = clamp(screenUV + disp * 0.975, uvMin, uvMax);
        sceneColor.r = texture(u_sceneTex, uvR).r;
        sceneColor.g = texture(u_sceneTex, uvG).g;
        sceneColor.b = texture(u_sceneTex, uvB).b;
        sceneColor = clamp(sceneColor, vec3(0.0), vec3(64.0));
    } else {
        sceneColor = vec3(0.06, 0.10, 0.18);
    }

    // Fast polynomial powers replacing pow(y, 10.0) and pow(1.0 - y, 6.0)
    float y2 = y * y;
    float y4 = y2 * y2;
    float caustic = (y4 * y4 * y2) * envelope;
    vec3 causticGlow = vec3(0.95, 0.98, 1.0) * (caustic * 0.65);

    float iy = 1.0 - y;
    float iy2 = iy * iy;
    float wakeShadow = (iy2 * iy2 * iy2) * envelope * 0.12;
    sceneColor *= (1.0 - wakeShadow);

    // Ethereal sword qi tint (crystal clear, transparent)
    vec3 qiTint = u_tintColor.rgb * (clamp(u_tintColor.a, 0.0, 1.0) * 0.10 * envelope * sinY);

    // Refractive output: warped background + Schlieren caustic edge + translucent sword qi
    finalColor = vec4(sceneColor + causticGlow + qiTint, 1.0);
}