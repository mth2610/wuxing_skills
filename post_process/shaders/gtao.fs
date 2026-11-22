#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0; // Linear view-Z depth (meters)
uniform vec2 u_texelSize;
uniform vec2 u_projScale;   // (tan(fovY/2) * aspect, tan(fovY/2))
uniform float u_radius;     // World-space AO radius (meters)
uniform float u_bias;

vec3 ReconstructViewPos(vec2 uv, float z)
{
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc * u_projScale * z, -z);
}

vec3 ReconstructViewNormal(vec2 uv, vec3 posC, float zC)
{
    float zL = texture(texture0, uv - vec2(u_texelSize.x, 0.0)).r;
    float zR = texture(texture0, uv + vec2(u_texelSize.x, 0.0)).r;
    float zD = texture(texture0, uv - vec2(0.0, u_texelSize.y)).r;
    float zU = texture(texture0, uv + vec2(0.0, u_texelSize.y)).r;

    vec3 pL = ReconstructViewPos(uv - vec2(u_texelSize.x, 0.0), zL);
    vec3 pR = ReconstructViewPos(uv + vec2(u_texelSize.x, 0.0), zR);
    vec3 pD = ReconstructViewPos(uv - vec2(0.0, u_texelSize.y), zD);
    vec3 pU = ReconstructViewPos(uv + vec2(0.0, u_texelSize.y), zU);

    vec3 dX = (abs(zR - zC) < abs(zC - zL)) ? (pR - posC) : (posC - pL);
    vec3 dY = (abs(zU - zC) < abs(zC - zD)) ? (pU - posC) : (posC - pD);
    vec3 n = cross(dX, dY);
    return n * inversesqrt(max(dot(n, n), 1e-8));
}

void main()
{
    float zC = texture(texture0, fragTexCoord).r;
    if (zC <= 0.01 || zC >= 220.0) {
        finalColor = vec4(1.0);
        return;
    }

    vec3 posC = ReconstructViewPos(fragTexCoord, zC);
    vec3 norm = ReconstructViewNormal(fragTexCoord, posC, zC);
    vec3 viewDir = normalize(-posC);

    // Distance fade so distant mountains don't darken into fog
    float distFade = 1.0 - smoothstep(45.0, 140.0, zC);
    if (distFade <= 0.001) {
        finalColor = vec4(1.0);
        return;
    }

    // Screen-space radius projected from world-space u_radius
    float pxRadius = clamp(u_radius / max(zC * u_projScale.y, 0.05), 0.006, 0.065);

    // Interleaved Gradient Noise for per-pixel rotation & step jitter
    float ign0 = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float ign1 = fract(52.9829189 * fract(dot(gl_FragCoord.xy + 19.0, vec2(0.06711056, 0.00583715))));
    float baseAngle = ign0 * 3.14159265;

    float ao = 0.0;
    const int SLICES = 2;
    const int STEPS = 3;
    float invR2 = 1.0 / max(u_radius * u_radius, 1e-4);

    for (int s = 0; s < SLICES; s++) {
        float angle = baseAngle + float(s) * 1.5707963;
        vec2 dir = vec2(cos(angle), sin(angle));
        float maxCos1 = -1.0;
        float maxCos2 = -1.0;

        for (int st = 1; st <= STEPS; st++) {
            float t = (float(st) - 0.5 + ign1 * 0.5) / float(STEPS);
            vec2 off = dir * (t * pxRadius);

            vec2 uv1 = clamp(fragTexCoord + off, vec2(0.002), vec2(0.998));
            float z1 = texture(texture0, uv1).r;
            vec3 d1 = ReconstructViewPos(uv1, z1) - posC;
            float l1Sq = dot(d1, d1);
            if (l1Sq > 1e-5) {
                float falloff = clamp(1.0 - l1Sq * invR2, 0.0, 1.0);
                float c1 = dot(d1, viewDir) * inversesqrt(l1Sq) - u_bias;
                maxCos1 = max(maxCos1, mix(-1.0, c1, falloff));
            }

            vec2 uv2 = clamp(fragTexCoord - off, vec2(0.002), vec2(0.998));
            float z2 = texture(texture0, uv2).r;
            vec3 d2 = ReconstructViewPos(uv2, z2) - posC;
            float l2Sq = dot(d2, d2);
            if (l2Sq > 1e-5) {
                float falloff = clamp(1.0 - l2Sq * invR2, 0.0, 1.0);
                float c2 = dot(d2, viewDir) * inversesqrt(l2Sq) - u_bias;
                maxCos2 = max(maxCos2, mix(-1.0, c2, falloff));
            }
        }

        float h1 = acos(clamp(maxCos1, -1.0, 1.0));
        float h2 = acos(clamp(maxCos2, -1.0, 1.0));
        float sliceAO = 0.5 * (0.25 * (-cos(2.0 * h1) + 2.0 * h1 + 1.0) +
                               0.25 * (-cos(2.0 * h2) + 2.0 * h2 + 1.0));
        ao += clamp(sliceAO, 0.0, 1.0);
    }

    ao /= float(SLICES);
    ao = mix(1.0, clamp(ao * 1.15, 0.25, 1.0), distFade);
    finalColor = vec4(ao, ao, ao, 1.0);
}
