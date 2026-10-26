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
    vec4 clipPos = mvp * vec4(vertexPosition, 1.0);
    clipPos.z = clipPos.w * 0.999998;
    gl_Position = clipPos;
}
