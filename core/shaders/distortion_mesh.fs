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
uniform float     u_time;              // Animation time

out vec4 finalColor;

void main() {
    // Exact perspective-correct screen-space UV (0.0 to 1.0) derived directly from clip-space:
    vec2 screenUV = (v_clipPos.xy / v_clipPos.w) * 0.5 + 0.5;

    // True screen-space forward cutting direction via partial derivatives:
    vec2 gradY = vec2(dFdx(fragTexCoord.y), dFdy(fragTexCoord.y));
    float gradYLen = length(gradY);
    vec2 shockDir = (gradYLen > 0.00001) ? (gradY / gradYLen) : vec2(0.0, 1.0);

    // Strictly orthogonal lateral direction: dot(shockDir, perpDir) == 0.0.
    // Mathematical guarantee: perpendicular vectors CANNOT cancel each other out!
    vec2 perpDir = vec2(-shockDir.y, shockDir.x);

    float x = clamp(fragTexCoord.x, 0.0, 1.0);
    float y = clamp(fragTexCoord.y, 0.0, 1.0);

    // Wave profile: strong compression at the leading edge (y -> 1.0), smooth aerodynamic wake
    float wave = sin(y * 3.14159265) * (0.45 + 0.55 * y);

    // Smooth, low-frequency dynamic heat turbulence (no speckle noise)
    float ripple = sin(x * 4.0 - u_time * 8.0 + y * 2.5) * 0.12;

    // Lateral expansion along the arc: -1.0 on left wing, +1.0 on right wing
    float lateralCurve = (x - 0.5) * 2.0;

    // Orthogonal displacement field: forward shock + symmetric lateral bow.
    // Length is identical on both wings: length(a*shockDir + b*perpDir) == length(a*shockDir - b*perpDir)
    vec2 lensVector = shockDir * (wave + ripple) + perpDir * (lateralCurve * wave * 0.22);

    // Flow map texture sampling (optional)
    vec2 flowUV = fragTexCoord + u_flowSpeed * u_time;
    vec4 flowSample = texture(texture0, flowUV);
    if (flowSample.a > 0.05) {
        lensVector += (flowSample.rg * 2.0 - 1.0) * 0.20;
    }

    // Arc span envelope: maintains 75% - 100% displacement across the entire blade length.
    // Only smoothly fades the extreme 8% tips so the wings never vanish or weaken prematurely!
    float tipFade = smoothstep(0.0, 0.08, x) * smoothstep(1.0, 0.92, x);
    float spanFactor = mix(0.75, 1.0, sin(x * 3.14159265));
    float envelope = tipFade * spanFactor;

    // Pronounced, cinematic displacement magnitude
    float strength = (u_distortionStrength > 0.0001) ? u_distortionStrength : 0.11;
    vec2 disp = lensVector * strength * envelope;

    // Clean, high-fidelity chromatic dispersion (tight 2.5% spread: crisp, optical, zero noise)
    vec3 sceneColor;
    if (u_hasScene != 0) {
        vec2 uvR = clamp(screenUV + disp * 1.025, vec2(0.002), vec2(0.998));
        vec2 uvG = clamp(screenUV + disp * 1.000, vec2(0.002), vec2(0.998));
        vec2 uvB = clamp(screenUV + disp * 0.975, vec2(0.002), vec2(0.998));
        sceneColor.r = texture(u_sceneTex, uvR).r;
        sceneColor.g = texture(u_sceneTex, uvG).g;
        sceneColor.b = texture(u_sceneTex, uvB).b;
    } else {
        sceneColor = vec3(0.06, 0.10, 0.18);
    }

    // Schlieren caustic compression edge at the Mach front (visible on any surface, including flat arena floor)
    float caustic = pow(y, 10.0) * envelope;
    vec3 causticGlow = vec3(0.95, 0.98, 1.0) * (caustic * 0.65);

    // Subtle optical rarefaction shadow along the trailing wake for 3D depth
    float wakeShadow = pow(1.0 - y, 6.0) * envelope * 0.12;
    sceneColor *= (1.0 - wakeShadow);

    // Ethereal sword qi tint (crystal clear, transparent)
    vec3 qiTint = u_tintColor.rgb * (clamp(u_tintColor.a, 0.0, 1.0) * 0.10 * envelope * sin(y * 3.14159265));

    // Refractive output: warped background + Schlieren caustic edge + translucent sword qi
    finalColor = vec4(sceneColor + causticGlow + qiTint, 1.0);
}
