#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;       // Current jittered HDR scene
uniform sampler2D u_historyTex;   // Previous resolved HDR history
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

void main()
{
    vec4 currSample = texture(texture0, fragTexCoord);
    vec3 currRGB = max(currSample.rgb, vec3(0.0));

    if (u_firstFrame > 0.5) {
        finalColor = vec4(currRGB, currSample.a);
        return;
    }

    // The camera is stable. The history texel shares the current pixel's
    // footprint, so it needs neither depth reprojection nor reconstruction.
    vec3 currComp = ToneCompress(currRGB);
    vec3 currYCC = RGBToYCoCg(currComp);
    vec3 m1 = currYCC;
    vec3 m2 = currYCC * currYCC;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0)
                continue;
            vec2 offset = vec2(float(dx), float(dy)) * u_texelSize;
            vec2 sampleUV = fragTexCoord + offset;
            vec3 s = ToneCompress(max(texture(texture0, sampleUV).rgb, vec3(0.0)));
            vec3 ycc = RGBToYCoCg(s);
            m1 += ycc;
            m2 += ycc * ycc;
        }
    }
    m1 /= 9.0;
    m2 /= 9.0;
    vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));

    float currPeak = max(currRGB.r, max(currRGB.g, currRGB.b));
    float emissiveReactive = smoothstep(1.25, 3.50, currPeak);
    float gamma = mix(1.25, 0.72, emissiveReactive * 0.8);

    vec3 histRGB = texture(u_historyTex, fragTexCoord).rgb;
    vec3 histYCC = RGBToYCoCg(ToneCompress(histRGB));
    vec3 clippedYCC = clamp(histYCC, m1 - gamma * sigma, m1 + gamma * sigma);
    float blendAlpha = max(0.11, emissiveReactive * 0.55);
    vec3 resolvedComp = mix(YCoCgToRGB(clippedYCC), currComp, blendAlpha);
    finalColor = vec4(max(ToneDecompress(resolvedComp), vec3(0.0)), currSample.a);
}
