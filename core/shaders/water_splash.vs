#version 330
#include "core/shaders/common/vs_header.glsl"

// ============================================================
// Water Splash Material (Vertex Shader) - Custom GLB Mesh
// ============================================================

uniform float u_customParam1; // Splash progress (0.0 -> 1.0)
uniform float u_customParam2; // Random phase offset (0.0 -> 10.0)

void main() {
    
    vec3 animatedPos = vertexPosition;
    
    // --- CYLINDER SQUEEZE MODIFIER ---
    // At t=0, force all outer vertices (the rim) to fold inwards into a straight vertical wall.
    // It fully unfolds into the crown by t=0.45.
    float squeezeFactor = 1.0 - smoothstep(0.0, 0.45, u_customParam1);
    
    float origR = length(vertexPosition.xz);
    float cylinderRadius = 0.2; // Tighter cylinder for a more pronounced initial pillar
    
    // If the vertex is outside the cylinder, pull it in! If inside, leave it alone.
    float targetR = mix(origR, min(origR, cylinderRadius), squeezeFactor);
    
    float radialMultiplier = targetR / (origR + 0.0001); // Prevent division by zero
    
    animatedPos.x *= radialMultiplier;
    animatedPos.z *= radialMultiplier;
    
    // --- RADIAL WAVE HEIGHT MODIFIER ---
    // Instead of scaling the whole mesh Y at once, we ripple the height from center to edge!
    float r = length(vertexPosition.xz);
    
    // Normalize radius (assuming mesh radius is roughly 2.5 units in local space)
    float normR = clamp(r / 2.5, 0.0, 1.0);
    
    // Center starts animating at 0.0. Edges are delayed by up to 0.35.
    float startTime = normR * 0.35;
    float endTime = 1.0; 
    
    // Calculate the local progress for this specific vertex (0.0 to 1.0)
    float localProgress = clamp((u_customParam1 - startTime) / (endTime - startTime), 0.0, 1.0);
    
    // Apply parabolic height (0 -> 1 -> 0) based on local progress
    float localHeight = 4.0 * localProgress * (1.0 - localProgress);
    
    // AMPLITUDE SUPPRESSION: The center should NOT rise much (forms a crater).
    // The rim (outer edges) should splash up the most!
    // smoothstep(0.1, 0.9, normR) curves from 0 at center to 1 at the rim.
    float amplitude = smoothstep(0.1, 0.9, normR);
    
    // Center barely rises (15%), rim shoots up 40% higher than original (1.4)!
    amplitude = mix(0.15, 1.4, amplitude);
    
    localHeight *= amplitude;
    
    animatedPos.y *= localHeight;
    
    // 1. Calculate WORLD SPACE position to fix huge Blender scale issues
    vec4 worldPos = matModel * vec4(animatedPos, 1.0);
    
    // 2. Ensure we have a valid normal to push along
    vec3 validNormal = vertexNormal;
    if (length(validNormal) < 0.1) {
        // Procedural normal pointing outward from center (XZ)
        validNormal = normalize(vec3(vertexPosition.x, 0.2, vertexPosition.z));
    }
    
    // 3. Gentle Fluid Wobble (Using World Space for smooth frequency + Random Phase)
    float wobble = sin(worldPos.y * 3.0 + worldPos.x * 2.0 - (u_time + u_customParam2) * 5.0);
    
    // Smooth height factor to GLUE the base to the ground! (Base wobbles 0%, Top wobbles 100%)
    float heightFactor = clamp(worldPos.y / 2.0, 0.0, 1.0);
    
    // Very gentle displacement (so it doesn't inflate and tear the splash pieces)
    float displacementAmt = wobble * 0.1 * heightFactor; 
    vec3 displacedPos = animatedPos + validNormal * displacementAmt;
    
    // Standard output pipeline
    VS_FinalOutput(displacedPos);
}
