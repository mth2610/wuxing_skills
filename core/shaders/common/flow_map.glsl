#ifndef WUXING_FLOW_MAP_GLSL
#define WUXING_FLOW_MAP_GLSL

// Valve-style two-phase flow sampling. This is the reusable MATERIAL operation:
// each surface keeps its own lighting/alpha after this function returns.
vec4 FlowMap_SampleTwoPhase(sampler2D bodyTex, sampler2D flowTex, vec2 uv,
                            float time, float speed, float strength, float tiling)
{
    vec2 tiledUV = uv * tiling;
    vec2 flow = texture(flowTex, uv).rg * 2.0 - 1.0;
    float phase0 = fract(time * speed);
    float phase1 = fract(time * speed + 0.5);
    float blend = abs(phase0 * 2.0 - 1.0);
    vec4 body0 = texture(bodyTex, tiledUV + flow * (phase0 - 0.5) * strength);
    vec4 body1 = texture(bodyTex, tiledUV + flow * (phase1 - 0.5) * strength);
    return mix(body0, body1, blend);
}

#endif
