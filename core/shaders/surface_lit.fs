#version 330

// Đợt G2 — stylized-realism surface lighting (Thiên Nhai Minh Nguyệt Đao look).
// Beauty comes from idealized material response + atmosphere, NOT physically
// correct PBR: soft half-Lambert diffuse, a Blinn "moonlight" sheen, and a cool
// Fresnel rim that traces the silhouette — the signature wuxia-night edge light.
// Writes into the HDR float scene buffer, so spec/rim can exceed 1.0 and bloom
// naturally blooms them (the G1 HDR foundation compounds here).

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragWorldPos;

uniform sampler2D texture0; // albedo (raylib binds MATERIAL_MAP_DIFFUSE here)
uniform vec4 colDiffuse;    // per-draw tint (DrawModelEx tint / team color)

// Lighting from environment_system (set per frame by SurfaceMaterial_UpdateFrame)
uniform vec3  u_sunToLight;   // normalized dir from surface TOWARD the sun
uniform vec3  u_sunColor;
uniform vec3  u_ambientColor;
uniform vec3  u_viewPos;

// Stylized material controls
uniform vec3  u_rimColor;     // cool moonlight edge
uniform float u_rimPower;     // higher = thinner/sharper rim
uniform float u_rimStrength;
uniform float u_specStrength;
uniform float u_shininess;

// Distance fog (matches environment fog config)
uniform vec3  u_fogColor;
uniform float u_fogStart;
uniform float u_fogEnd;
uniform float u_fogEnabled;

out vec4 finalColor;

void main() {
    vec4 tex = texture(texture0, fragTexCoord);
    vec3 albedo = tex.rgb * colDiffuse.rgb * fragColor.rgb;

    vec3 N = normalize(fragNormal);
    vec3 L = normalize(u_sunToLight);
    vec3 V = normalize(u_viewPos - fragWorldPos);
    vec3 H = normalize(L + V);

    float ndl = dot(N, L);

    // Half-Lambert (Valve) — wraps light around the terminator for a soft,
    // stylized falloff with no harsh dark/light boundary on skin & cloth.
    float wrap = ndl * 0.5 + 0.5;
    wrap *= wrap;

    vec3 diffuse = albedo * u_sunColor * wrap;
    vec3 ambient = albedo * u_ambientColor;

    // Blinn specular sheen — gated by facing so back faces don't glint.
    float spec = pow(max(dot(N, H), 0.0), u_shininess) * u_specStrength;
    spec *= smoothstep(0.0, 0.15, ndl);
    vec3 specular = u_sunColor * spec;

    // Fresnel rim — the cool silhouette edge that reads as moonlight.
    float fres = pow(1.0 - max(dot(N, V), 0.0), u_rimPower);
    vec3 rim = u_rimColor * (fres * u_rimStrength);

    vec3 color = ambient + diffuse + specular + rim;

    if (u_fogEnabled > 0.5) {
        float dist = length(u_viewPos - fragWorldPos);
        float f = clamp((dist - u_fogStart) / max(u_fogEnd - u_fogStart, 0.001), 0.0, 1.0);
        color = mix(color, u_fogColor, f);
    }

    finalColor = vec4(color, tex.a * colDiffuse.a);
}
