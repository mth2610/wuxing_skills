#version 330
#ifdef GL_ES
precision highp float;
#endif

// Cohesive Volumetric Sand / Dust Funnel Vertex Shader
// Uses explicit world-space u_view / u_projection (never matModel view-space ambiguity)
// and shares the exact dual-harmonic sinuous spine + 3-zone bell-waist-anvil profile
// with Motion_SampleTornadoSpine / Motion_TornadoFunnelRadius.

in vec3 vertexPosition; // .xz = unit circle, .y = normalized height h in [0..1]
in vec2 vertexTexCoord; // .x = angle u in [0..1], .y = h in [0..1]

uniform mat4  u_view;
uniform mat4  u_projection;
uniform float u_time;
uniform float u_spineTime;
uniform vec3  u_basePos;
uniform float u_height;
uniform float u_baseRadius;
uniform float u_topRadius;
uniform float u_swayAmp;
uniform vec2  u_tilt;
uniform float u_power;
uniform float u_implosion;
uniform float u_radiusMul;
uniform float u_spinSpeed;
uniform float u_waveAmp;

out vec2  v_uv;
out float v_h;
out vec3  v_worldPos;
out vec3  v_worldNormal;

vec3 sf_sampleSpine(float h)
{
    float hc = max(h, 0.0001);
    float h13 = pow(hc, 1.3);
    float h17 = pow(hc, 1.7);
    float sway = u_swayAmp * (1.0 - 0.45 * clamp(u_implosion, 0.0, 1.0));
    float sx = (sin(u_spineTime * 1.05 + h * 2.8) * 0.72 +
                sin(u_spineTime * 1.85 - h * 4.4) * 0.28) * sway * h13 + u_tilt.x * h17;
    float sz = (cos(u_spineTime * 0.86 + h * 2.4 + 0.7) * 0.72 +
                cos(u_spineTime * 1.55 + h * 3.8) * 0.28) * sway * h13 + u_tilt.y * h17;
    return vec3(u_basePos.x + sx, u_basePos.y + h * u_height, u_basePos.z + sz);
}

float sf_funnelRadius(float h)
{
    float hc = max(h, 0.0001);
    float baseSkirt = exp(-h * 10.5) * u_baseRadius * 1.45;
    float waist = mix(u_baseRadius * 0.72, u_topRadius * 0.88, pow(hc, 1.18));
    float crownS = smoothstep(0.64, 1.0, h);
    float anvilCrown = pow(crownS, 1.85) * u_topRadius * 0.45;
    float implScale = mix(1.0, 0.48, clamp(u_implosion, 0.0, 1.0));
    return (waist + baseSkirt + anvilCrown) * implScale;
}

void main()
{
    float h = clamp(vertexPosition.y, 0.0, 1.0);
    v_uv = vertexTexCoord;
    v_h = h;

    float ang = atan(vertexPosition.z, vertexPosition.x);
    float spinRate = u_spinSpeed * (1.0 + 0.65 * clamp(u_implosion, 0.0, 1.0));
    float twistedAng = ang + h * 3.8 - u_time * spinRate * 0.45;
    vec3 radDir = vec3(cos(twistedAng), 0.0, sin(twistedAng));

    float wave = (sin(ang * 3.0 + h * 11.0 - u_time * spinRate * 1.35) * 0.6 +
                  cos(ang * 5.0 - h * 7.5 + u_time * spinRate * 0.9) * 0.4) * u_waveAmp * (0.25 + 0.85 * h);

    float growth = mix(0.28, 1.0, smoothstep(0.0, 0.48, u_power));
    float r = max(0.08, (sf_funnelRadius(h) * u_radiusMul + wave) * growth);

    vec3 center = sf_sampleSpine(h);
    vec3 wp = center + radDir * r;

    float hUp = min(1.0, h + 0.025);
    vec3 cUp = sf_sampleSpine(hUp);
    float rUp = sf_funnelRadius(hUp) * u_radiusMul * growth;
    vec3 pUp = cUp + radDir * rUp;
    vec3 tangY = normalize(pUp - wp + vec3(0.0, 0.0001, 0.0));
    vec3 tangT = normalize(cross(tangY, radDir) + vec3(0.0001, 0.0, 0.0));
    v_worldNormal = normalize(cross(tangT, tangY));
    v_worldPos = wp;

    gl_Position = u_projection * (u_view * vec4(wp, 1.0));
}
