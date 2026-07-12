#version 330
#include "core/shaders/common/vs_header.glsl"

// ============================================================
// Water Splash Material (Vertex Shader) - Fluid Subdivided Petal
// ============================================================

uniform float u_customParam1; // Splash progress (0.0 -> 1.0)

void main() {
    vec3 displacedPos = vertexPosition;
    float progress = u_customParam1;
    
    // Multi-phase physics
    float openPhase = smoothstep(0.1, 0.7, progress);
    float fallPhase = smoothstep(0.4, 1.0, progress);
    
    // We estimate max height to be ~1.2 for the height ratio calculation.
    // Base is at 0.0. The higher up, the stronger the deformations.
    float hRatio = clamp(vertexPosition.y / 1.2, 0.0, 1.0);
    
    if (vertexPosition.y > 0.0) {
        
        // 1. Tapering (Spike generation)
        // The width (X) and thickness (Z) taper towards 0 at the very top.
        float taper = 1.0 - (hRatio * hRatio * 0.9); // Keeps some thickness, tapers near top
        displacedPos.x *= taper;
        displacedPos.z *= taper;
        
        // 2. Outward Bending (Blooming)
        // Local +Z points outward from the splash center.
        // A quadratic curve (hRatio^2) creates a smooth, sweeping curve.
        float bendOutward = hRatio * hRatio * 1.6 * openPhase;
        displacedPos.z += bendOutward;
        
        // 3. Gravity Sagging
        // The tips get pulled down heavily as the splash dissipates.
        float sagDownward = hRatio * hRatio * 1.2 * fallPhase;
        displacedPos.y -= sagDownward;
        
        // 4. Fluid Rippling (Turbulence)
        // Add a high-frequency sine wave to simulate the liquid membrane fluttering.
        float ripple = sin(vertexPosition.y * 12.0 - u_time * 18.0) * 0.06;
        displacedPos.x += ripple * hRatio; 
        displacedPos.z += ripple * hRatio;
    }
    
    // Standard output pipeline
    VS_FinalOutput(displacedPos);
    
    // Recalculate Vertex Normal to catch lighting beautifully on the bent curves
    if (vertexPosition.y > 0.0) {
        vec3 customNormal = vertexNormal;
        // The petal bends outwards (+Z), so its surface normal tilts upwards (+Y)
        customNormal.y += hRatio * openPhase * 1.5;
        // Fluid ripple affects the normal too
        customNormal.x += cos(vertexPosition.y * 12.0 - u_time * 18.0) * 0.3 * hRatio;
        
        customNormal = normalize(customNormal);
        fragNormal = normalize(vec3(matModel * vec4(customNormal, 0.0)));
    }
}
