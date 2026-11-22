#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;       // Current jittered HDR scene
uniform sampler2D u_historyTex;   // Previous resolved HDR history
uniform sampler2D u_depthTex;     // Linearized view-Z scene depth (meters)

uniform mat4  u_invCurrViewProj;
uniform mat4  u_prevViewProj;
uniform vec3  u_camPos;
uniform vec3  u_viewForward;
uniform vec2  u_texelSize;
uniform float u_firstFrame;

vec3 RGBToYCoCg(vec3 c)
{
    float Y  = dot(c, vec3( 0.25, 0.50,  0.25));
    float Co = dot(c, vec3( 0.50, 0.00, -0.50));
    float Cg = dot(c, vec3(-0.25, 0.50, -0.25));
    return vec3(Y, Co, Cg);
}

vec3 YCoCgToRGB(vec3 ycc)
{
    float Y  = ycc.x;
    float Co = ycc.y;
    float Cg = ycc.z;
    return vec3(Y + Co - Cg, Y + Cg, Y - Co - Cg);
}

// Reinhard tone-weight keeps bright HDR spikes from dominating neighborhood variance
vec3 ToneCompress(vec3 c)
{
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c / (1.0 + max(luma, 0.0));
}

vec3 ToneDecompress(vec3 c)
{
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c / max(1.0 - luma, 0.001);
}

// 5-tap Catmull-Rom history filter preserves grass blade crispness vs bilinear blur
vec3 SampleHistoryCatmullRom(sampler2D tex, vec2 uv, vec2 texelSize)
{
    vec2 texSize = 1.0 / max(texelSize, vec2(1e-5));
    vec2 samplePos = uv * texSize;
    vec2 texPos1 = floor(samplePos - 0.5) + 0.5;
    vec2 f = samplePos - texPos1;

    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / max(w12, vec2(1e-5));

    vec2 texPos0 = (texPos1 - 1.0) * texelSize;
    vec2 texPos3 = (texPos1 + 2.0) * texelSize;
    vec2 texPos12 = (texPos1 + offset12) * texelSize;

    vec3 result = vec3(0.0);
    result += texture(tex, vec2(texPos12.x, texPos0.y)).rgb * (w12.x * w0.y);
    result += texture(tex, vec2(texPos0.x,  texPos12.y)).rgb * (w0.x  * w12.y);
    result += texture(tex, vec2(texPos12.x, texPos12.y)).rgb * (w12.x * w12.y);
    result += texture(tex, vec2(texPos3.x,  texPos12.y)).rgb * (w3.x  * w12.y);
    result += texture(tex, vec2(texPos12.x, texPos3.y)).rgb * (w12.x * w3.y);

    float wSum = (w12.x * w0.y) + (w0.x * w12.y) + (w12.x * w12.y) + (w3.x * w12.y) + (w12.x * w3.y);
    return max(result / max(wSum, 1e-4), vec3(0.0));
}

vec2 ReprojectUV(vec2 uv, out float outDepth)
{
    float sceneDepth = texture(u_depthTex, uv).r;
    outDepth = sceneDepth;
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 worldFar = u_invCurrViewProj * clip;
    vec3 rayDir = normalize(worldFar.xyz / worldFar.w - u_camPos);

    vec3 worldPos;
    if (sceneDepth <= 0.001 || sceneDepth >= 900.0) {
        worldPos = u_camPos + rayDir * 500.0;
    } else {
        float cosView = max(dot(rayDir, u_viewForward), 0.001);
        worldPos = u_camPos + rayDir * (sceneDepth / cosView);
    }

    vec4 prevClip = u_prevViewProj * vec4(worldPos, 1.0);
    return (prevClip.xy / max(prevClip.w, 0.0001)) * 0.5 + 0.5;
}

void main()
{
    vec4 currSample = texture(texture0, fragTexCoord);
    vec3 currRGB = max(currSample.rgb, vec3(0.0));

    if (u_firstFrame > 0.5) {
        finalColor = vec4(currRGB, currSample.a);
        return;
    }

    float centerDepth = 0.0;
    vec2 prevUV = ReprojectUV(fragTexCoord, centerDepth);
    if (prevUV.x <= 0.001 || prevUV.x >= 0.999 || prevUV.y <= 0.001 || prevUV.y >= 0.999) {
        finalColor = vec4(currRGB, currSample.a);
        return;
    }

    // 3x3 neighborhood YCoCg variance AABB in tone-compressed space + depth edge check
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    float minD = 1e9;
    float maxD = 0.0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2 offset = vec2(float(dx), float(dy)) * u_texelSize;
            vec2 sampleUV = fragTexCoord + offset;
            vec3 s = ToneCompress(max(texture(texture0, sampleUV).rgb, vec3(0.0)));
            vec3 ycc = RGBToYCoCg(s);
            m1 += ycc;
            m2 += ycc * ycc;
            float nd = texture(u_depthTex, sampleUV).r;
            minD = min(minD, nd);
            maxD = max(maxD, nd);
        }
    }
    m1 /= 9.0;
    m2 /= 9.0;
    vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));

    // Tighten variance box on depth silhouettes & bright HDR emissives to suppress ghosting
    float depthSpan = maxD - minD;
    float edgeFactor = smoothstep(0.45, 2.50, depthSpan);
    float currPeak = max(currRGB.r, max(currRGB.g, currRGB.b));
    float emissiveReactive = smoothstep(1.25, 3.50, currPeak);
    float gamma = mix(1.25, 0.72, max(edgeFactor, emissiveReactive * 0.8));

    vec3 boxMin = m1 - gamma * sigma;
    vec3 boxMax = m1 + gamma * sigma;

    vec3 histRGB = SampleHistoryCatmullRom(u_historyTex, prevUV, u_texelSize);
    vec3 histYCC = RGBToYCoCg(ToneCompress(histRGB));
    vec3 clippedYCC = clamp(histYCC, boxMin, boxMax);
    vec3 clippedRGB = YCoCgToRGB(clippedYCC);

    vec3 currComp = ToneCompress(currRGB);
    float velPx = length((fragTexCoord - prevUV) / max(u_texelSize, vec2(1e-5)));
    float blendAlpha = mix(0.11, 0.36, clamp(velPx * 0.25, 0.0, 1.0));
    blendAlpha = max(blendAlpha, max(edgeFactor * 0.42, emissiveReactive * 0.55));

    vec3 resolvedComp = mix(clippedRGB, currComp, clamp(blendAlpha, 0.10, 0.85));
    finalColor = vec4(max(ToneDecompress(resolvedComp), vec3(0.0)), currSample.a);
}
