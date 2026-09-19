#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;          // Linearized scene depth (world units) bound via DrawTexturePro
uniform sampler2D u_depthTex;        // Secondary alias
uniform sampler2D u_shadowMap;       // Directional shadow map
uniform mat4      u_invViewProj;     // Inverse view-projection matrix
uniform mat4      u_lightVP;         // Sun light-space matrix
uniform vec3      u_camPos;          // World-space camera position
uniform vec3      u_sunDir;          // Direction light travels (downward)
uniform vec3      u_sunColor;        // Sunlight linear color
uniform vec3      u_fogColor;        // Ambient fog color
uniform float     u_fogDensity;      // Base volume density
uniform float     u_fogStart;        // Near-camera exclusion distance
uniform float     u_heightFalloff;   // Exponential decay k_e
uniform float     u_baseAltitude;    // Reference altitude Y
uniform float     u_sigmoidEnabled;  // >0.5 enables sigmoid inversion blanket
uniform vec3      u_sigmoidParams;   // x: layerAltitude, y: layerThickness, z: layerDensity
uniform float     u_mieAnisotropy;   // Henyey-Greenstein g (~0.75-0.85)
uniform float     u_godRayIntensity; // Intensity multiplier for sunlight shafts
uniform float     u_maxDist;         // Max raymarch distance (e.g. 150.0)
uniform int       u_stepCount;       // 12 for MED, 24 for HIGH
uniform vec2      u_screenResolution;// Resolution of the downscaled target
uniform float     u_time;            // Animation clock for wind & sunbeams

// Local Fog Volumes (up to 4 active volumes)
uniform int       u_volumeCount;
uniform vec4      u_volPosShape[4];     // xyz = position, w = shape (0=box, 1=sphere, 2=cylinder)
uniform vec4      u_volExtentsDense[4]; // xyz = extents, w = density
uniform vec4      u_volColorSoft[4];    // rgb = color, a = edgeSoftness

// Reconstruct world-space ray direction from screen UV and inverse ViewProj
vec3 ReconstructRayDir(vec2 uv) {
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = u_invViewProj * clip;
    return normalize(world.xyz / world.w - u_camPos);
}

// 4x4 Bayer matrix for ordered dithering (jitter step offset without banding)
float Bayer4x4(vec2 uv, vec2 screenRes) {
    ivec2 p = ivec2(mod(uv * screenRes, 4.0));
    int bayer[16] = int[16](
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5
    );
    return float(bayer[p.y * 4 + p.x]) / 16.0;
}

// Henyey-Greenstein phase function for forward crepuscular scattering
float HenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 0.001), 1.5));
}

// Shadow factor from directional shadow map (1.0 = lit, 0.0 = shadowed)
float SampleShadowLS(vec4 posLS) {
    vec3 proj = posLS.xyz / max(posLS.w, 0.00001) * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) {
        return 1.0;
    }
    float shadowDepth = texture(u_shadowMap, proj.xy).r;
    return (proj.z <= shadowDepth + 0.0015) ? 1.0 : 0.0;
}

// Ánh sáng qua kẽ lá (Canopy Foliage Light Shafts / God-Rays)
float ComputeCanopyGodRay(vec3 worldPos, vec3 sunDir, float time) {
    // Phạm vi độ cao: tia nắng rọi từ tán cây (Y ~ 12m) xuống mặt cỏ (Y ~ -2.5m)
    float heightFade = smoothstep(12.0, 5.0, worldPos.y);
    float groundFade = smoothstep(-2.5, 0.1, worldPos.y);
    float fade = heightFade * groundFade;
    if (fade <= 0.0001) return 0.0;

    // Chiếu ngược theo tia nắng lên độ cao tán cây (Canopy Y ~ 6.5m)
    float canopyY = 6.5;
    float distToCanopy = (canopyY - worldPos.y) / max(-sunDir.y, 0.05);
    vec3 canopyPos = worldPos - sunDir * max(distToCanopy, 0.0);

    // Gió lay động cành lá làm các luồng nắng lung linh
    vec2 wind = vec2(sin(time * 0.9 + canopyPos.x * 0.15), cos(time * 0.7 + canopyPos.z * 0.15)) * 0.35;
    vec2 uv = (canopyPos.xz + wind) * 0.18;

    // 1. Cành cây lớn (Branch clusters)
    float branch = sin(uv.x * 3.14 + sin(uv.y * 2.1)) * cos(uv.y * 2.8 + cos(uv.x * 1.9));
    branch = branch * 0.5 + 0.5;

    // 2. Kẽ lá và tán lá đan xen (Leaf gaps)
    vec2 leafUV = uv * 3.2 + vec2(time * 0.12, time * 0.08);
    float leafGaps = sin(leafUV.x * 6.28 + sin(leafUV.y * 4.2)) * cos(leafUV.y * 5.8 + cos(leafUV.x * 3.1));
    leafGaps = leafGaps * 0.5 + 0.5;

    // 3. Tia nắng sắc nét xuyên qua kẽ lá (Crepuscular beams)
    float pattern = branch * 0.55 + leafGaps * 0.45;
    float shaft = smoothstep(0.35, 0.75, pattern);
    shaft = pow(shaft, 1.4);

    return shaft * fade;
}

// Đánh giá các khối sương mù cục bộ (hồ nước, vạt hoa, hốc rừng)
float EvaluateLocalFog(vec3 worldPos, inout vec3 inoutFogColor) {
    float localDense = 0.0;
    for (int i = 0; i < u_volumeCount; i++) {
        vec3 center = u_volPosShape[i].xyz;
        int shape = int(u_volPosShape[i].w + 0.5);
        vec3 extents = u_volExtentsDense[i].xyz;
        float baseDen = u_volExtentsDense[i].w;
        vec3 col = u_volColorSoft[i].rgb;
        float softness = clamp(u_volColorSoft[i].a, 0.1, 0.99);

        vec3 delta = abs(worldPos - center);
        float dist = 0.0;

        if (shape == 2) {
            // Cylinder (lake surface mist)
            float r = length(worldPos.xz - center.xz) / max(extents.x, 0.001);
            float h = delta.y / max(extents.y, 0.001);
            dist = max(r, h);
        } else if (shape == 1) {
            // Sphere
            dist = length((worldPos - center) / max(extents, vec3(0.001)));
        } else {
            // Box
            vec3 d = delta / max(extents, vec3(0.001));
            dist = max(max(d.x, d.y), d.z);
        }

        if (dist < 1.0) {
            float fade = 1.0 - smoothstep(1.0 - softness, 1.0, dist);
            fade = smoothstep(0.0, 1.0, fade);
            float volumeVal = baseDen * fade;
            localDense += volumeVal;
            inoutFogColor = mix(inoutFogColor, col, clamp(fade * 0.85, 0.0, 1.0));
        }
    }
    return localDense;
}

void main() {
    float sceneDepth = texture(texture0, fragTexCoord).r;
    if (sceneDepth <= 0.001) sceneDepth = u_maxDist;

    float rayDist = min(sceneDepth, u_maxDist);
    float marchStart = max(u_fogStart, 0.8);
    float marchDist = rayDist - marchStart;

    // Tránh raymarch quá ngắn
    if (marchDist <= 0.2) {
        finalColor = vec4(0.0);
        return;
    }

    vec3 rayDir = ReconstructRayDir(fragTexCoord);
    int steps = clamp(u_stepCount, 8, 32);
    float stepSize = marchDist / float(steps);

    // Jitter with 4x4 Bayer dither to turn slice banding into high-frequency grain
    float dither = Bayer4x4(fragTexCoord, u_screenResolution);
    float startT = marchStart + stepSize * dither;

    // Mie phase function toward light source with multiple-scattering side floor
    vec3 sunToLight = normalize(-u_sunDir);
    float cosTheta = dot(rayDir, sunToLight);
    float miePhase = HenyeyGreenstein(cosTheta, clamp(u_mieAnisotropy, 0.4, 0.75));
    // Multiple scattering and side floor ensure sunlit shafts are visible from side/45-deg camera angles
    float shaftVisibility = max(miePhase, 0.32) + 0.35 * max(cosTheta * 0.5 + 0.5, 0.0);

    vec3 accumRadiance = vec3(0.0);
    float transmittance = 1.0;

    // Linear projection along ray in light homogeneous clip space
    vec4 rayStartLS = u_lightVP * vec4(u_camPos + rayDir * startT, 1.0);
    vec4 rayStepLS = u_lightVP * vec4(rayDir * stepSize, 0.0);

    for (int i = 0; i < steps; i++) {
        float t = startT + float(i) * stepSize;
        if (t >= rayDist) break;

        vec3 samplePos = u_camPos + rayDir * t;

        // Smooth camera near-fade (keeps lens clean while letting nearby mist show)
        float camFade = smoothstep(0.8, 2.0, t);

        // 1. Exponential ground mist (clings to low ground, decays upward)
        float h = max(samplePos.y - u_baseAltitude, 0.0);
        float heightCoeff = exp(-u_heightFalloff * h);

        // 2. Sigmoid inversion / lake vapor blanket
        float sigmoidDensity = 0.0;
        if (u_sigmoidEnabled > 0.5) {
            float diff = (samplePos.y - u_sigmoidParams.x) / max(u_sigmoidParams.y, 0.001);
            sigmoidDensity = (1.0 / (1.0 + diff * diff)) * u_sigmoidParams.z;
        }

        // 3. Local fog volumes (lake, meadow, forest hollows)
        vec3 fogColor = u_fogColor;
        float localDensity = 0.0;
        if (u_volumeCount > 0 && samplePos.y <= 6.0 && samplePos.y >= -3.0) {
            localDensity = EvaluateLocalFog(samplePos, fogColor);
        }

        // 4. Canopy sunbeam scattering particles (airborne dust & mist motes catching light)
        float canopyShaft = 0.0;
        if (samplePos.y <= 12.0 && samplePos.y >= -2.5) {
            canopyShaft = ComputeCanopyGodRay(samplePos, u_sunDir, u_time);
        }
        float sunbeamHaze = 0.038 * canopyShaft * smoothstep(12.0, 1.5, samplePos.y);

        float density = (u_fogDensity * (heightCoeff + sigmoidDensity) + localDensity + sunbeamHaze) * camFade;
        if (density <= 0.00001) continue;

        // Sunlight transmission & canopy shaft modulation
        vec4 posLS = rayStartLS + rayStepLS * float(i);
        float shadow = SampleShadowLS(posLS);
        float directLight = shadow * (0.20 + 0.80 * canopyShaft) * u_godRayIntensity;

        // Radiance calculation: warm golden sunlight in-scattering + luminous morning sky ambient
        vec3 directTerm = u_sunColor * (directLight * shaftVisibility * 3.0);
        vec3 ambientTerm = fogColor * 1.25;
        vec3 stepLight = directTerm + ambientTerm;

        float opticalThickness = density * stepSize;
        float stepTransmittance = exp(-opticalThickness);

        // In-scattering accumulation
        vec3 stepRadiance = stepLight * (1.0 - stepTransmittance);
        accumRadiance += transmittance * stepRadiance;
        transmittance *= stepTransmittance;

        // Soft floor so fog never completely blinds the player
        if (transmittance < 0.20) {
            transmittance = 0.20;
            break;
        }
    }

    // Output Premultiplied Alpha: RGB = In-scattering radiance, A = Opacity (1 - transmittance)
    finalColor = vec4(accumRadiance, 1.0 - transmittance);
}
