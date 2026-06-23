#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform float u_time;
uniform vec3 viewPos;
uniform float u_uvLength;

out vec4 finalColor;

// Đồng bộ sóng dài từ Vertex Shader
float getIrregularity(vec2 uv) {
    float t = uv.y / u_uvLength;
    float phi = uv.x * 6.28318;
    
    float swell = sin(t * 6.0 - u_time * 10.0) * cos(t * 2.0 - u_time * 4.0);
    float bump = sin(phi * 2.0 + u_time * 4.0) * cos(t * 8.0 - u_time * 8.0);
    
    float irregularity = swell * 0.6 + bump * 0.4;
    float dampen = smoothstep(0.02, 0.15, t) * smoothstep(0.98, 0.85, t);
    
    return irregularity * dampen * 3.5; 
}

void main() {
    float eps = 0.02; 
    float dL = getIrregularity(fragTexCoord - vec2(eps, 0.0));
    float dR = getIrregularity(fragTexCoord + vec2(eps, 0.0));
    float dD = getIrregularity(fragTexCoord - vec2(0.0, eps));
    float dU = getIrregularity(fragTexCoord + vec2(0.0, eps));
    
    vec3 dNormal = vec3(dL - dR, dD - dU, 0.0);
    
    vec3 tangent = normalize(cross(vec3(0.0, 1.0, 0.0), fragNormal));
    if (length(tangent) < 0.1) tangent = normalize(cross(vec3(1.0, 0.0, 0.0), fragNormal));
    vec3 bitangent = cross(fragNormal, tangent);
    
    // Normal mềm lại để bóng hắt không gắt
    vec3 normal = normalize(fragNormal + (tangent * dNormal.x + bitangent * dNormal.y) * 0.8);

    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));

    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 2.5); // Fresnel dịu lại một xíu

    // Lõi nước nâng sáng nhẹ để bớt giống mực đen, giữ màu xanh biển thẳm
    vec3 waterCore = vec3(0.05, 0.35, 0.80); 
    vec3 waterEdge = vec3(0.40, 0.95, 1.00); 
    vec3 baseColor = mix(waterCore, waterEdge, fresnel);

    float fakeSubsurface = max(dot(normal, -viewDir), 0.0) * 0.3;
    baseColor += waterCore * fakeSubsurface;

    // --- SỬA VÂN CAUSTICS THÀNH CÁC DẢI SỌC DÀI CHẢY DỌC ---
    // Ép UV trục Y (dọc thân) nhỏ lại, trục X (ngang) to ra để tạo sọc
    vec2 scroll1 = vec2(fragTexCoord.x * 2.0, fragTexCoord.y * 0.5) - vec2(u_time * 1.5, u_time * 5.0);
    vec2 scroll2 = vec2(fragTexCoord.x * 3.0, fragTexCoord.y * 0.4) + vec2(u_time * 1.0, -u_time * 6.0);
    
    float caustics = sin(scroll1.x) * cos(scroll1.y) + sin(scroll2.x + scroll2.y);
    caustics = pow(max(caustics, 0.0), 1.5); 
    baseColor += vec3(0.4, 0.8, 1.0) * caustics * 0.3; 

    // --- MỞ RỘNG VỆT PHẢN QUANG ---
    vec3 halfVector = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVector), 0.0);
    // Ép điểm sáng to và trượt dài ra (hạ pow từ 90 xuống 60), hạ cường độ bớt chói lóa
    float specular = pow(NdotH, 60.0) * 1.6; 

    vec3 finalRGB = baseColor + vec3(specular);

    float normalizedT = fragTexCoord.y / u_uvLength;
    // Nâng độ đục của lõi lên một chút (0.25) để ánh sáng xuyên thấu mượt hơn
    float alphaFresnel = mix(0.25, 0.95, fresnel); 
    
    float headGlow = smoothstep(0.85, 1.0, normalizedT);
    finalRGB = mix(finalRGB, vec3(0.60, 0.85, 0.95), headGlow * 0.4);
    alphaFresnel = mix(alphaFresnel, 1.0, headGlow * 0.8);

    float edgeFade = smoothstep(0.0, 0.02, normalizedT) * (1.0 - smoothstep(1.02, 1.05, normalizedT));

    finalColor = vec4(finalRGB, alphaFresnel * edgeFade);
}