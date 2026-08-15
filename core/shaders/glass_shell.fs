#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"

uniform vec4  u_bodyColor;
uniform vec4  u_rimColor;
uniform float u_opacity;
uniform float u_fresnelPower;
uniform float u_rimStrength;
uniform int   u_emissionOnly;
uniform vec3  u_lightDirView;
uniform int   u_wallPass;

const float GLASS_IOR = 1.50;

float glassFresnel(float cosTheta) {
    float eta = (1.0 - GLASS_IOR) / (1.0 + GLASS_IOR);
    float f0 = eta * eta;
    float m = 1.0 - clamp(cosTheta, 0.0, 1.0);
    return f0 + (1.0 - f0) * m * m * m * m * m;
}

// Fallback environment until the render graph exposes a scene cubemap. This
// is deliberately directional (sky/ground plus a soft sun lobe), not a flat
// tint, so reflection still conveys curvature on a black backdrop.
vec3 glassEnvironment(vec3 direction, vec3 lightDir) {
    float sky = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.035, 0.045, 0.075);
    vec3 zenith = vec3(0.16, 0.20, 0.30);
    vec3 ground = vec3(0.012, 0.008, 0.010);
    vec3 env = mix(ground, mix(horizon, zenith, sky), step(0.0, direction.y));
    float sun = pow(max(dot(direction, lightDir), 0.0), 48.0);
    return env + vec3(1.0, 0.42, 0.12) * sun * 1.8;
}

void main() {
    // The immediate-mode sphere supplies view-space positions (see glass_shell.vs).
    // In that space the camera is at the origin; using world-space viewPos here
    // mixes coordinate systems and shifts the rim into a broad crescent.
    vec3 viewDir = normalize(-fragPosition);
    vec3 normal = normalize(fragNormal);
    // The sphere can be seen through either wall. Orient the interpolated
    // normal toward the camera before evaluating Fresnel; feeding a back-face
    // normal directly to calcFresnel() makes dot(N,V) negative and turns the
    // entire back hemisphere into a bright crescent instead of a silhouette rim.
    vec3 facingNormal = faceforward(normal, -viewDir, normal);
    float cosTheta = clamp(dot(facingNormal, viewDir), 0.0, 1.0);
    float fresnel = glassFresnel(cosTheta);
    // Keep the shared helper in the path as the artistic rim shaping term;
    // the IOR-based Schlick value above controls energy split.
    float rimFresnel = calcFresnel(facingNormal, viewDir, u_fresnelPower);
    fresnel = max(fresnel, rimFresnel * 0.22);
    float rim = smoothstep(0.18, 0.92, fresnel);
    vec3 lightDir = normalize(u_lightDirView);
    float diffuse = calcDiffuse(facingNormal, lightDir, 0.18);
    float specular = calcSpecular(facingNormal, lightDir, viewDir, 32.0);
    // A sphere is optically thickest when viewed face-on. Keep that carrier
    // density separate from Fresnel (which is edge-on), otherwise the body
    // disappears and only a flat circular outline remains.
    float facing = cosTheta;
    float thickness = mix(0.10, 1.0, pow(facing, 0.65));
    // Base colour is a tint, not an extinction coefficient. Deriving sigma_t
    // from (1 - baseColor) killed the green/blue channels of fire materials and
    // made the whole sphere read as a flat black disk. Use a restrained,
    // independent absorption profile instead.
    vec3 absorption = vec3(0.16, 0.055, 0.025);
    vec3 transmission = exp(-absorption * thickness);
    vec3 reflection = glassEnvironment(reflect(-viewDir, facingNormal), lightDir);
    vec3 refractedDir = refract(-viewDir, facingNormal, 1.0 / GLASS_IOR);
    // Keep the ray available for a future scene-color snapshot path.
    // No fake environment in the carrier: the actual scene behind the shell
    // must arrive through alpha blending. A fixed analytic background is
    // visibly wrong over grass, stone, or any other live scene texture.
    vec3 transmittedLight = transmission;

    if (u_emissionOnly != 0) {
        vec3 glow = u_rimColor.rgb * fresnel * (0.40 + u_rimStrength * 0.55);
        glow += reflection * fresnel * 0.12;
        glow += u_rimColor.rgb * rim * 0.62;
        glow += u_rimColor.rgb * specular * 1.35;
        finalColor = vec4(glow, u_opacity * rim * 0.72);
        return;
    }

    // Keep the carrier almost transparent. A visible alpha floor here would
    // turn the sphere into a dark opaque blob before the Fresnel rim appears.
    float wallWeight = (u_wallPass == 0) ? 0.48 : 1.0;
    float bodyAlpha = u_opacity * wallWeight *
                      (0.022 + thickness * 0.045 + fresnel * 0.03);
    // Curvature is the primary volume cue: face-on pixels carry the coloured
    // carrier, while grazing pixels fall back to the transparent rim.
    vec3 tintedTransmission = transmittedLight *
                              mix(vec3(0.72), u_bodyColor.rgb, 0.82);
    vec3 body = tintedTransmission * (0.10 + thickness * 0.18 + diffuse * 0.12);
    if (u_wallPass == 0)
        body = mix(body, u_rimColor.rgb * 0.22, 0.28);
    body += reflection * fresnel * 0.12;
    body += u_rimColor.rgb * specular * 0.95;
    finalColor = vec4(body, bodyAlpha);
}
