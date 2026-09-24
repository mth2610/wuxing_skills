#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

uniform sampler2D texture0;         // Riverbed pebble/stone diffuse texture
uniform sampler2D u_causticTex;     // Dual-layer caustics texture

uniform float u_time;
uniform float u_waterHeight;        // Absolute world Y of the water surface
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;

uniform float u_causticsStrength;
uniform float u_causticsScale;
uniform vec3 u_absorption;          // Beer-Lambert beta_e(R, G, B)
uniform vec3 u_deepColor;
uniform vec3 u_shallowColor;

out vec4 finalColor;

void main()
{
    // Physical water column depth directly above this bed fragment
    float waterDepth = max(0.0, u_waterHeight - fragPosition.y);

    // Multi-scale riverbed stone & gravel sampling using world coordinates
    vec2 uvBase = fragPosition.xz * 1.45;
    vec2 uvFine = fragPosition.xz * 4.30;
    vec4 stoneBase = texture(texture0, uvBase);
    vec4 stoneFine = texture(texture0, uvFine);
    vec3 stoneRgb = mix(stoneBase.rgb, stoneFine.rgb, 0.35);

    // Bed albedo: rich earthy river stones and gravel pebbles
    vec3 bedAlbedo = stoneRgb * (fragColor.rgb * 1.35) * 0.92;

    // Dual-layer animated caustics dancing on the lake bed
    float t = u_time * 0.72;
    vec2 uv0 = fragPosition.xz * u_causticsScale * 0.34 + vec2(t * 0.038, t * 0.024);
    vec2 uv1 = fragPosition.xz * u_causticsScale * 0.48 + vec2(-t * 0.029, t * 0.043);
    float c0 = texture(u_causticTex, uv0).r;
    float c1 = texture(u_causticTex, uv1).r;
    // Keep only the focused ridge tips; the original broad response covered
    // almost every pixel with bright white cells.
    float causticWave = pow(smoothstep(0.32, 0.78, min(c0, c1)), 1.5);

    // Caustics are tightly focused at shallow depths (0.02m - 0.7m)
    float causticFade = smoothstep(0.01, 0.06, waterDepth) * (1.0 - smoothstep(0.72, 1.35, waterDepth));
    vec3 causticLight = u_lightColor * causticWave * (u_causticsStrength * 0.82) * causticFade * max(u_lightDir.y, 0.25);

    // Terrain lighting on the lake bed
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(u_lightDir);
    float NdotL = max(dot(N, L), 0.0);
    vec3 directLight = u_lightColor * (NdotL * 0.90) + causticLight;
    vec3 ambientLight = u_ambientColor * 0.95;
    vec3 litBed = bedAlbedo * (directLight + ambientLight);

    // Beer-Lambert optical attenuation through the crystal clear water column
    vec3 V = normalize(u_viewPos - fragPosition);
    float cosV = max(dot(vec3(0.0, 1.0, 0.0), V), 0.25);
    float opticalPath = waterDepth * (1.0 + 1.0 / cosV);
    vec3 transmittance = exp(-u_absorption * opticalPath);

    // Volume in-scattering through the shallow column
    vec3 waterScatter = u_deepColor * (vec3(1.0) - transmittance) * (0.50 + 0.50 * u_ambientColor);

    // Composite: Submerged bed visible through crystal clear water
    vec3 finalRgb = litBed * transmittance + waterScatter;

    finalColor = vec4(finalRgb, 1.0);
}
