#version 330
#include "core/shaders/common/vs_header.glsl"

// ============================================================
// Water Splash Material (Vertex Shader) - Custom GLB Mesh
// ============================================================

uniform float u_customParam1; // Splash progress (0.0 -> 1.0)

void main() {
    vec3 displacedPos = vertexPosition;
    
    // 1. Calculate WORLD SPACE position to fix huge Blender scale issues
    vec4 worldPos = matModel * vec4(vertexPosition, 1.0);
    
    // 2. Ensure we have a valid normal to push along
    vec3 validNormal = vertexNormal;
    if (length(validNormal) < 0.1) {
        validNormal = vec3(0.0, 1.0, 0.0);
    }
    
    // 3. Gentle Fluid Wobble (Using World Space for smooth frequency)
    float wobble = sin(worldPos.y * 3.0 + worldPos.x * 2.0 - u_time * 5.0);
    
    // Smooth height factor to GLUE the base to the ground! (Base wobbles 0%, Top wobbles 100%)
    float heightFactor = clamp(worldPos.y / 2.0, 0.0, 1.0);
    
    // Very gentle displacement (adjusted for height so it bends flexibly)
    float displacementAmt = wobble * 1.5 * heightFactor; 
    displacedPos += validNormal * displacementAmt;
    
    // Standard output pipeline
    VS_FinalOutput(displacedPos);
    
    // 4. Subtle Shimmer Normal
    // Perturb the normal slightly to make the surface lighting shimmer like water
    vec3 customNormal = validNormal;
    customNormal.x += cos(worldPos.y * 4.0 - u_time * 5.0) * 0.1;
    customNormal.z += sin(worldPos.x * 4.0 - u_time * 5.0) * 0.1;
    
    fragNormal = normalize(vec3(matModel * vec4(normalize(customNormal), 0.0)));
}
