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

vec2 ReprojectUV(vec2 uv)
{
    float sceneDepth = texture(u_depthTex, uv).r;
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

    vec2 prevUV = ReprojectUV(fragTexCoord);
    if (prevUV.x <= 0.001 || prevUV.x >= 0.999 || prevUV.y <= 0.001 || prevUV.y >= 0.999) {
        finalColor = vec4(currRGB, currSample.a);
        return;
    }

    // 3x3 neighborhood YCoCg variance AABB in tone-compressed space
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2 offset = vec2(float(dx), float(dy)) * u_texelSize;
            vec3 s = ToneCompress(max(texture(texture0, fragTexCoord + offset).rgb, vec3(0.0)));
            vec3 ycc = RGBToYCoCg(s);
            m1 += ycc;
            m2 += ycc * ycc;
        }
    }
    m1 /= 9.0;
    m2 /= 9.0;
    vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));

    float gamma = 1.25;
    vec3 boxMin = m1 - gamma * sigma;
    vec3 boxMax = m1 + gamma * sigma;

    vec3 histRGB = max(texture(u_historyTex, prevUV).rgb, vec3(0.0));
    vec3 histYCC = RGBToYCoCg(ToneCompress(histRGB));
    vec3 clippedYCC = clamp(histYCC, boxMin, boxMax);
    vec3 clippedRGB = YCoCgToRGB(clippedYCC);

    vec3 currComp = ToneCompress(currRGB);
    float velPx = length((fragTexCoord - prevUV) / max(u_texelSize, vec2(1e-5)));
    float blendAlpha = mix(0.11, 0.36, clamp(velPx * 0.25, 0.0, 1.0));

    vec3 resolvedComp = mix(clippedRGB, currComp, blendAlpha);
    finalColor = vec4(max(ToneDecompress(resolvedComp), vec3(0.0)), currSample.a);
}
