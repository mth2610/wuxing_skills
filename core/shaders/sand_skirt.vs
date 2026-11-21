#version 330
#ifdef GL_ES
precision highp float;
#endif

// Ground Sweeping Dust Skirt Vertex Shader (domed scouring sand annulus around tornado foot)

in vec3 vertexPosition; // .xz in unit annulus [0.16..1.0], .y = 0

uniform mat4  u_view;
uniform mat4  u_projection;
uniform vec3  u_basePos;
uniform float u_skirtRadius;
uniform float u_domeLift;

out vec2  v_local;
out float v_radial;
out vec3  v_worldPos;

void main()
{
    float r = length(vertexPosition.xz);
    v_radial = clamp((r - 0.16) / 0.84, 0.0, 1.0);
    v_local = vertexPosition.xz;

    float lift = pow(1.0 - v_radial, 2.2) * u_domeLift + 0.05;
    vec3 wp = u_basePos + vec3(vertexPosition.x * u_skirtRadius, lift, vertexPosition.z * u_skirtRadius);
    v_worldPos = wp;

    gl_Position = u_projection * (u_view * vec4(wp, 1.0));
}
