#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;       // smoothed nearest fluid depth
uniform sampler2D u_thicknessTex; // additive optical path proxy
uniform sampler2D u_sceneTex;
uniform sampler2D u_sceneDepthTex;
uniform vec2 u_texel;
uniform int u_hasSceneDepth;
uniform int u_qualityTier;

uniform mat4 u_inverseProjection;
uniform mat4 u_viewToWorld;
uniform vec3 u_sunDirectionView;  // surface -> sun, view space
uniform vec3 u_sunColor;
uniform vec3 u_skyAmbient;
uniform vec3 u_groundAmbient;
uniform vec3 u_materialBody;
uniform vec3 u_materialGlow;
uniform vec3 u_materialSoft;
uniform float u_time;

#define FLUID_POINT_LIGHTS 4
uniform int u_pointLightCount;
uniform vec4 u_pointLightPosRadius[FLUID_POINT_LIGHTS];
uniform vec4 u_pointLightColor[FLUID_POINT_LIGHTS];

vec3 ReconstructViewPosition(vec2 uv, float depth) {
    vec4 clip=vec4(uv*2.0-1.0,depth*2.0-1.0,1.0);
    vec4 view=u_inverseProjection*clip;
    return view.xyz/max(abs(view.w),1e-6)*sign(view.w);
}

float FresnelSchlick(float ndv) {
    const float waterF0=0.02037; // ((1.333 - 1)/(1.333 + 1))^2
    return waterF0+(1.0-waterF0)*pow(1.0-ndv,5.0);
}

float WaterSpecularBRDF(vec3 N, vec3 V, vec3 L, float roughness) {
    float ndl=max(dot(N,L),0.0);
    float ndv=max(dot(N,V),0.0);
    if(ndl<=0.0 || ndv<=0.0) return 0.0;
    vec3 H=normalize(V+L);
    float ndh=max(dot(N,H),0.0);
    float vdh=max(dot(V,H),0.0);
    float a=roughness*roughness;
    float a2=a*a;
    float denominator=ndh*ndh*(a2-1.0)+1.0;
    float D=a2/max(3.14159265*denominator*denominator,1e-5);
    float k=(roughness+1.0);
    k=k*k*0.125;
    float gv=ndv/(ndv*(1.0-k)+k);
    float gl=ndl/(ndl*(1.0-k)+k);
    float F=0.02037+(1.0-0.02037)*pow(1.0-vdh,5.0);
    return min(D*gv*gl*F/max(4.0*ndl*ndv,1e-4),8.0);
}

bool SceneSampleIsBehindFluid(vec2 uv, float fluidDistance) {
    if(u_hasSceneDepth==0) return true;
    float sceneDepth=texture(u_sceneDepthTex,uv).r;
    if(sceneDepth>=0.99999) return true;
    vec3 scenePosition=ReconstructViewPosition(uv,sceneDepth);
    return -scenePosition.z>fluidDistance+0.002;
}

void main() {
    float fluidDepth=texture(texture0,fragTexCoord).r;
    if(fluidDepth>=0.99999) discard;

    vec3 positionView=ReconstructViewPosition(fragTexCoord,fluidDepth);
    vec3 V=normalize(-positionView);

    /* Reconstruct a perspective-correct normal from four fluid neighbours.
     * A background texel is placed at the centre depth instead of depth 1:
     * otherwise every tiny droplet gets a near-90-degree normal at its edge
     * and Fresnel turns it into the old white particle outline. */
    float depthLeft=texture(texture0,fragTexCoord-vec2(u_texel.x,0.0)).r;
    float depthRight=texture(texture0,fragTexCoord+vec2(u_texel.x,0.0)).r;
    float depthDown=texture(texture0,fragTexCoord-vec2(0.0,u_texel.y)).r;
    float depthUp=texture(texture0,fragTexCoord+vec2(0.0,u_texel.y)).r;
    depthLeft=depthLeft>=0.99999?fluidDepth:depthLeft;
    depthRight=depthRight>=0.99999?fluidDepth:depthRight;
    depthDown=depthDown>=0.99999?fluidDepth:depthDown;
    depthUp=depthUp>=0.99999?fluidDepth:depthUp;
    vec3 positionLeft=ReconstructViewPosition(
        fragTexCoord-vec2(u_texel.x,0.0),depthLeft);
    vec3 positionRight=ReconstructViewPosition(
        fragTexCoord+vec2(u_texel.x,0.0),depthRight);
    vec3 positionDown=ReconstructViewPosition(
        fragTexCoord-vec2(0.0,u_texel.y),depthDown);
    vec3 positionUp=ReconstructViewPosition(
        fragTexCoord+vec2(0.0,u_texel.y),depthUp);
    vec3 N=normalize(cross(positionRight-positionLeft,
                          positionUp-positionDown));
    if(dot(N,V)<0.0) N=-N;

    /* Every spherical splat otherwise receives the same straight highlight.
     * When equal kernels pack into rows those identical strokes join into
     * screen-wide bands. Add a small animated world-space ripple: it is stable
     * under camera zoom and changes highlight orientation without moving mass. */
    vec3 worldPosition=(u_viewToWorld*vec4(positionView,1.0)).xyz;
    vec3 worldNormal=normalize(mat3(u_viewToWorld)*N);
    vec3 rippleVector=vec3(
        sin(dot(worldPosition,vec3(19.3,7.1,-13.7))+u_time*1.65),
        sin(dot(worldPosition,vec3(-11.9,17.3,8.7))-u_time*1.27),
        sin(dot(worldPosition,vec3(7.7,-9.1,23.9))+u_time*1.43));
    rippleVector-=worldNormal*dot(rippleVector,worldNormal);
    worldNormal=normalize(worldNormal+rippleVector*0.025);
    N=normalize(transpose(mat3(u_viewToWorld))*worldNormal);
    if(dot(N,V)<0.0) N=-N;
    float ndv=clamp(dot(N,V),0.0,1.0);

    float thicknessProxy=clamp(texture(u_thicknessTex,fragTexCoord).r,0.0,4.0);
    /* The thickness pass stores world path length multiplied by 16.  Recover
     * metres before applying Beer-Lambert, then cap it by the actual distance
     * from the visible water surface to opaque scene geometry. */
    float kernelThickness=clamp(thicknessProxy/16.0,0.001,0.35);
    float geometricThickness=kernelThickness;
    if(u_hasSceneDepth!=0) {
        float sceneDepth=texture(u_sceneDepthTex,fragTexCoord).r;
        if(sceneDepth<0.99999) {
            vec3 scenePosition=ReconstructViewPosition(fragTexCoord,sceneDepth);
            float depthGap=max((-scenePosition.z)-(-positionView.z),0.0);
            /* A receiver directly beneath the reconstructed surface can have
             * a sub-centimetre depth gap even though several optical kernels
             * overlap.  Preserve a thin 2.2 cm sheet instead of making the
             * puddle nearly invisible. */
            geometricThickness=min(kernelThickness,max(0.022,depthGap*1.25));
        }
    }
    float opticalPath=min(geometricThickness/max(ndv,0.28),0.42);

    /* Snell refraction in view space.  Offset is expressed in pixels so the
     * distortion remains stable across resolution changes. */
    vec3 incident=-V;
    vec3 refractedDirection=refract(incident,N,1.0/1.333);
    vec2 incidentSlope=incident.xy/max(abs(incident.z),0.25);
    vec2 refractedSlope=refractedDirection.xy/max(abs(refractedDirection.z),0.25);
    float travelPixels=clamp(opticalPath*64.0,0.45,7.0);
    vec2 refractOffset=(refractedSlope-incidentSlope)*travelPixels*u_texel;
    vec2 refractUV=clamp(fragTexCoord+refractOffset,u_texel,vec2(1.0)-u_texel);
    float fluidDistance=-positionView.z;
    if(!SceneSampleIsBehindFluid(refractUV,fluidDistance)) refractUV=fragTexCoord;

    vec3 refractedScene;
    if(u_qualityTier>=2) {
        // Tiny wavelength split; it is driven by the physical bend, not noise.
        vec2 dispersion=refractOffset*0.055;
        refractedScene.r=texture(u_sceneTex,
            clamp(refractUV+dispersion,u_texel,vec2(1.0)-u_texel)).r;
        refractedScene.g=texture(u_sceneTex,refractUV).g;
        refractedScene.b=texture(u_sceneTex,
            clamp(refractUV-dispersion,u_texel,vec2(1.0)-u_texel)).b;
    } else {
        refractedScene=texture(u_sceneTex,refractUV).rgb;
    }
    if(u_qualityTier>=3) {
        float blurRadius=1.0+smoothstep(0.045,0.22,opticalPath)*2.0;
        vec3 blur=texture(u_sceneTex,refractUV+vec2(u_texel.x,0.0)*blurRadius).rgb
                 +texture(u_sceneTex,refractUV-vec2(u_texel.x,0.0)*blurRadius).rgb
                 +texture(u_sceneTex,refractUV+vec2(0.0,u_texel.y)*blurRadius).rgb
                 +texture(u_sceneTex,refractUV-vec2(0.0,u_texel.y)*blurRadius).rgb;
        blur*=0.25;
        refractedScene=mix(refractedScene,blur,
            smoothstep(0.055,0.24,opticalPath)*0.42);
    }

    /* Derive spectral absorption from the caller-owned material identity.
     * Channels absent from body colour are absorbed first; the strongest
     * channel remains transmissive. No water hue is baked into this shader. */
    float materialPeak=max(max(u_materialBody.r,u_materialBody.g),
                           max(u_materialBody.b,0.001));
    vec3 materialTransmission=clamp(u_materialBody/materialPeak,
                                    vec3(0.035),vec3(1.0));
    vec3 absorption=-log(materialTransmission)*3.20+vec3(0.22);
    vec3 transmittance=exp(-absorption*opticalPath);

    float hemi=clamp(worldNormal.y*0.5+0.5,0.0,1.0);
    vec3 ambient=mix(u_groundAmbient,u_skyAmbient,hemi);
    vec3 L=normalize(u_sunDirectionView);
    float ndl=max(dot(N,L),0.0);

    vec3 waterScatterColor=u_materialBody;
    vec3 illumination=ambient+u_sunColor*(0.12+0.30*ndl);
    vec3 transmitted=refractedScene*transmittance;
    /* Material colour already describes the liquid's spectral identity.
     * Component-wise multiplication by coloured illumination and extinction
     * shifted MAT_WATER toward muddy green. Light controls intensity here;
     * only dielectric reflection/specular retains the real light hue. */
    float illuminationEnergy=dot(illumination,vec3(0.2126,0.7152,0.0722));
    float scatterAmount=1.0-dot(transmittance,vec3(0.2126,0.7152,0.0722));
    vec3 inScatter=waterScatterColor*scatterAmount
                  *(1.12+illuminationEnergy*1.55);

    float fresnel=FresnelSchlick(ndv);
    float volumeWeight=smoothstep(0.010,0.105,kernelThickness);
    /* Screen-space droplets can be only one or two pixels wide.  Preserve the
     * dielectric response but attenuate unresolved grazing energy so a whole
     * splat does not collapse into a white ring. */
    float visibleFresnel=fresnel*mix(0.28,0.72,volumeWeight);
    vec3 reflection=mix(u_groundAmbient,u_skyAmbient,
                        clamp(worldNormal.y*0.65+0.35,0.0,1.0))
                   *visibleFresnel*0.72;

    /* All highlights now come from engine lights.  Stable micro-roughness
     * breaks up the lobe without inventing an unrelated screen-space sparkle. */
    float grain=fract(sin(dot(floor(gl_FragCoord.xy*0.5),
                                   vec2(12.9898,78.233)))*43758.5453);
    float roughness=mix(0.085,0.115,grain);
    vec3 sunHalf=normalize(V+L);
    float broadSunLobe=pow(max(dot(N,sunHalf),0.0),34.0)*0.065;
    vec3 specular=u_sunColor
                 *(WaterSpecularBRDF(N,V,L,roughness)*0.68+broadSunLobe)*ndl;
    specular*=mix(vec3(1.0),u_materialGlow,0.10);

    for(int i=0;i<FLUID_POINT_LIGHTS;i++) {
        if(i>=u_pointLightCount) break;
        vec3 toLight=u_pointLightPosRadius[i].xyz-positionView;
        float distanceToLight=length(toLight);
        float attenuation=clamp(1.0-distanceToLight/
            max(u_pointLightPosRadius[i].w,0.001),0.0,1.0);
        attenuation*=attenuation;
        if(attenuation<=0.0) continue;
        vec3 pointL=toLight/max(distanceToLight,0.001);
        float pointNdl=max(dot(N,pointL),0.0);
        vec3 pointHalf=normalize(V+pointL);
        float broadPointLobe=pow(max(dot(N,pointHalf),0.0),24.0)*0.12;
        specular+=u_pointLightColor[i].rgb
                 *(WaterSpecularBRDF(N,V,pointL,roughness)
                   +broadPointLobe)
                 *pointNdl*attenuation*1.55;
    }

    vec3 water=transmitted+inScatter+reflection+specular;
    float absorptionOpacity=1.0-dot(transmittance,vec3(0.333333));
    float opacity=clamp(0.25+absorptionOpacity*0.96
                       +visibleFresnel*0.22,0.25,0.94);
    finalColor=vec4(water,opacity);
}
