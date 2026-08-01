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
    vec3 n = normalize(vec3((l-r)*18.0, (b-t)*18.0, 1.0));
    float fresnel = pow(1.0 - clamp(n.z, 0.0, 1.0), 4.0);
    float thick = clamp(texture(u_thicknessTex, fragTexCoord).r, 0.0, 3.0);
    vec2 refractUV = clamp(fragTexCoord + n.xy*(0.010 + thick*0.014), 0.0, 1.0);
    vec3 refracted = texture(u_sceneTex, refractUV).rgb;
    // Beer-Lambert: thin water remains transparent, accumulated volume gains
    // a saturated blue body instead of the uniform plastic tint of alpha fog.
    vec3 transmittance = exp(-thick * vec3(1.65, 0.48, 0.22));
    vec3 waterBody = vec3(0.03, 0.34, 0.62);
    vec3 water = refracted * transmittance + waterBody * (1.0 - transmittance);
    water = mix(water, vec3(0.76, 0.93, 1.0), fresnel);
    finalColor = vec4(water + fresnel*0.10, clamp(0.16 + (1.0 - transmittance.b)*0.70 + fresnel*0.12, 0.0, 0.84));
}
