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
    gl_Position = u_projection * vec4(vertexPosition, 1.0);
}
