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
void main()
{
    vec3 ray = normalize(skyRay);
    float elevation = max(ray.y, 0.0);
    float daylight = smoothstep(-0.12, 0.08, u_sunDirection.y);
    vec3 zenith = u_skyAmbient * vec3(0.18, 0.38, 0.72);
    vec3 horizon = u_hazeColor * u_skyAmbient * 0.72;
    vec3 sky = mix(horizon, zenith, smoothstep(0.0, 0.65, elevation));
    float sunCosine = clamp(dot(ray, u_sunDirection), 0.0, 1.0);
    float halo = pow(sunCosine, 32.0) * 0.12;
    float discWidth = max(fwidth(sunCosine), 0.000002);
    float disc = smoothstep(0.999989 - discWidth, 0.999989 + discWidth, sunCosine);
    sky = mix(u_skyAmbient * vec3(0.025, 0.035, 0.060), sky, daylight);
    sky += u_sunColor * (halo + disc * 4.0) * daylight;
    finalColor = vec4(sky, 1.0);
}
