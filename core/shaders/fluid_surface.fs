#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;       // smoothed fluid depth, raw depth [0,1]
uniform sampler2D u_thicknessTex;
uniform sampler2D u_sceneTex;
uniform sampler2D u_sceneDepthTex;
uniform vec2 u_texel;
uniform int u_hasSceneDepth;
void main() {
    float d = texture(texture0, fragTexCoord).r;
    if (d >= 0.99999) discard;
    if (u_hasSceneDepth != 0 && d > texture(u_sceneDepthTex, fragTexCoord).r) discard;
    float l = texture(texture0, fragTexCoord - vec2(u_texel.x,0)).r;
    float r = texture(texture0, fragTexCoord + vec2(u_texel.x,0)).r;
    float b = texture(texture0, fragTexCoord - vec2(0,u_texel.y)).r;
    float t = texture(texture0, fragTexCoord + vec2(0,u_texel.y)).r;
    vec3 n = normalize(vec3((l-r)*45.0, (b-t)*45.0, 1.0));
    float fresnel = pow(1.0 - clamp(n.z, 0.0, 1.0), 3.0);
    float thick = clamp(texture(u_thicknessTex, fragTexCoord).r, 0.0, 1.0);
    vec2 refractUV = clamp(fragTexCoord + n.xy*(0.010 + thick*0.014), 0.0, 1.0);
    vec3 refracted = texture(u_sceneTex, refractUV).rgb;
    vec3 water = mix(refracted * vec3(0.68,0.88,0.98), vec3(0.78,0.94,1.0), fresnel);
    finalColor = vec4(water + fresnel*0.22, clamp(0.38 + thick*0.34 + fresnel*0.22, 0.0, 0.82));
}
