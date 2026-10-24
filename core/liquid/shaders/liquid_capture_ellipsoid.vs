#version 330
#ifdef GL_ES
precision highp float;
#endif

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexTexCoord;
layout(location = 2) in vec3 vertexNormal;
layout(location = 3) in vec4 vertexColor;
layout(location = 4) in vec4 vertexTangent;

uniform mat4 u_projection;
out vec2 v_ndc;
flat out vec3 v_centerView;
flat out vec3 v_radii;
flat out float v_material;

void main() {
    v_ndc = vertexTexCoord;
    v_centerView = vertexNormal;
    v_radii = vertexTangent.xyz;
    v_material = floor(vertexColor.r*255.0+0.5);
    /* vertexPosition is in view space and u_projection is raymath's OpenGL-convention
     * frustum (m5 > 0). Under rlvk, FBO passes run with scope.flipY = false while
     * standard rlFrustum paths flip Y; keep v_ndc in standard clip space for ray
     * unprojection, and match the FBO's clip orientation on gl_Position. */
    vec4 clip = u_projection * vec4(vertexPosition, 1.0);
    gl_Position = clip;
}
