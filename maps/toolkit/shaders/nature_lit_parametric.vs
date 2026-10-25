#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"
#include "maps/toolkit/shaders/grass_blade_state.glsl"

in vec3 vertexPosition;

uniform mat4 mvp;
uniform vec3 u_worldOffset;
uniform mat4 u_lightVP;
uniform mat4 u_staticLightVP;
uniform float u_time;
uniform float u_windStrength;
uniform vec3 u_viewPos;
uniform sampler2D u_interactionMap;
uniform vec2 u_interactionCenter;
uniform float u_interactionWorldSize;
uniform float u_interactionMaxBend;
uniform int u_interactionEnabled;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor; // .rgb = blade color, .a = dynamic wind sheen factor
out float fragHeight;
out vec2 fragTexCoord;
out vec4 v_lightSpace;
out vec4 v_staticLightSpace;

#define NATURE_VISIBLE_TUFT_LOD
#include "maps/toolkit/shaders/nature_parametric.glsl"

void main()
{
    if (!NatureTuftUsesCurrentLod()) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    vec3 bladePosition, bladeNormal;
    vec4 bladeColor;
    vec2 bladeWindUV, bladeSurfaceUV;
    NatureEvaluateBlade(bladePosition, bladeNormal, bladeColor, bladeWindUV, bladeSurfaceUV);

    // Early clip if perimeter fade collapsed the blade completely
    if (bladeColor.a <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    vec3 local = bladePosition;
    vec3 world = local + u_worldOffset;
    float h = bladeWindUV.y;
    vec3 bentNormal = bladeNormal;
    float windSheen = 0.0;

    // Skip all wind & state deformation evaluations for stationary root vertices (h == 0)
    if (h > 0.0001) {
        vec3 rootLocal = s_cachedClumpTexel.xyz;
        vec3 rootWorld = rootLocal + u_worldOffset;
        vec3 spineCenter = local - s_cachedSide * (s_cachedHalfWidth * vertexPosition.x);

        float windCompliance = 1.0;
        vec2 grassState = GrassEvaluateLocalState(rootWorld);
        if (dot(grassState, grassState) > 1e-5) {
            GrassApplyVertexMorphology(spineCenter, rootLocal, h, grassState, windCompliance);
        }

        float bladeSpanY = max(spineCenter.y - rootLocal.y, 0.0);
        float bladeHeightEst = max(bladeSpanY / max(h, 0.25), 0.22);
        float rootMask = h * (0.42 + 0.58 * h);
        float maxAllowedBend = min(
            max(u_interactionMaxBend * u_natureWindResponse.w * windCompliance, 0.001),
            bladeHeightEst * 0.58);

        vec2 windBend = NatureVegetationWindBend(
            rootWorld, bladeWindUV, u_time, u_windStrength * windCompliance, maxAllowedBend);
        windBend += NatureDominantWindImpact(rootWorld) * (u_natureWindResponse.w * windCompliance);

        if (u_interactionEnabled != 0 && u_interactionWorldSize > 0.0001) {
            vec2 uvOffset = (rootWorld.xz - u_interactionCenter) / u_interactionWorldSize;
            float maxAxis = max(abs(uvOffset.x), abs(uvOffset.y));
            // Only sample interaction texture when root is inside the active 18m region
            if (maxAxis < 0.5) {
                vec2 interactionUV = uvOffset + 0.5;
                vec3 interactionSample = textureLod(u_interactionMap, interactionUV, 0.0).rgb;
                vec2 pushDirection = interactionSample.rg * 2.0 - 1.0;
                float edgeFeather = clamp((0.50 - maxAxis) * 12.5, 0.0, 1.0);
                edgeFeather *= edgeFeather * (3.0 - 2.0 * edgeFeather);
                windBend += pushDirection * (interactionSample.b * edgeFeather * maxAllowedBend);
            }
        }

        // Smoothly cap total horizontal wind + interaction bend to maxAllowedBend
        float rawBendLen = length(windBend);
        if (rawBendLen > maxAllowedBend) {
            windBend *= maxAllowedBend / rawBendLen;
        }

        vec2 dispXZ = windBend * rootMask;
        float dispSq = dot(dispXZ, dispXZ);
        float dispLen = sqrt(dispSq);

        // Strictly monotonic cantilever deflection (guaranteed dy/dh > 0 so blade tips NEVER hook downward!)
        float bendRatio = clamp(dispLen / bladeHeightEst, 0.0, 0.60);
        spineCenter.xz += dispXZ;
        spineCenter.y   = rootLocal.y + bladeSpanY * (1.0 - 0.22 * bendRatio * h);

        // Re-orthogonalize ribbon width vector perpendicular to the bent 3D spine tangent
        // and 3D camera ray so wind never shears a blade along its own width into a 1px wire!
        float dMaskDh = 0.45 + 1.10 * h;
        vec3 bentTangent = normalize(s_cachedTangent + vec3(windBend.x * dMaskDh, -dispLen * 0.40, windBend.y * dMaskDh));
        vec3 bentSide = s_cachedSide - bentTangent * dot(s_cachedSide, bentTangent);
        float bsLenSq = dot(bentSide, bentSide);
        bentSide = (bsLenSq > 1e-5) ? (bentSide * inversesqrt(bsLenSq)) : s_cachedSide;

        vec3 toCamVec = rootWorld - u_tuftLodCamera;
        vec3 viewSide3D = cross(bentTangent, toCamVec);
        float vsLenSq = dot(viewSide3D, viewSide3D);
        if (vsLenSq > 1e-5) {
            viewSide3D *= inversesqrt(vsLenSq);
            if (dot(bentSide, viewSide3D) < 0.0) viewSide3D = -viewSide3D;
            bentSide = normalize(mix(bentSide, viewSide3D, 0.45));
        }

        local = spineCenter + bentSide * (s_cachedHalfWidth * vertexPosition.x);
        world = local + u_worldOffset;

        // As the blade bends over, its upper surface tilts skyward (+Y) and along the bend slope,
        // creating natural rolling specular wind waves instead of dark inverted patches.
        bentNormal.xz += windBend * (0.95 * h);
        bentNormal.y  += dispLen * 0.45;

        // Window windSheen to the broad mid-upper arch (tapers to 0 at tip h > 0.85 so tips never flicker)
        float archSheenMask = h * clamp((0.90 - h) * 4.5, 0.0, 1.0);
        windSheen = clamp(dispLen / maxAllowedBend, 0.0, 1.0) * archSheenMask;
    }

    fragPosition = world;
    fragNormal = normalize(bentNormal);
    fragColor = vec4(bladeColor.rgb, windSheen);
    fragHeight = h;
    fragTexCoord = bladeSurfaceUV;
    v_lightSpace = u_lightVP * vec4(world, 1.0);
    v_staticLightSpace = u_staticLightVP * vec4(world, 1.0);
    gl_Position = mvp * vec4(local, 1.0);
}