#version 330
#ifdef GL_ES
precision highp float;
#endif
in vec3 vertexPosition;
uniform mat4 mvp;
out vec3 skyRay;
void main()
{
    skyRay = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
    gl_Position.z = gl_Position.w * 0.999998;
}
