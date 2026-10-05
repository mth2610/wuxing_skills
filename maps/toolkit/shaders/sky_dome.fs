#version 330
#ifdef GL_ES
precision highp float;
#endif
in vec3 skyRay;
uniform vec3 u_sunDirection;
uniform vec3 u_skyAmbient;
uniform vec3 u_hazeColor;
uniform vec3 u_sunColor;
out vec4 finalColor;
#include "maps/toolkit/shaders/map_sky.glsl"
void main()
{
    finalColor = vec4(MapSkyRadiance(skyRay, u_sunDirection, u_skyAmbient, u_hazeColor, u_sunColor), 1.0);
}
