#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;
uniform float u_timeVS;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

void main()
{
    vec3 p = vertexPosition;

    float blob1 =
        sin(
            p.x * 1.2 +
            p.y * 0.9 +
            u_timeVS * 2.5);

    float blob2 =
        sin(
            p.z * 1.4 -
            p.x * 1.1 -
            u_timeVS * 3.5);

    float blob3 =
        sin(
            p.y * 1.8 +
            p.z * 0.7 +
            u_timeVS * 2.0);

    blob1 = max(0.0, blob1);
    blob2 = max(0.0, blob2);
    blob3 = max(0.0, blob3);

    float macroBlob =
          blob1 * 0.55
        + blob2 * 0.30
        + blob3 * 0.15;

    float ripple =
        sin(
            p.x * 8.0 +
            p.z * 7.0 +
            u_timeVS * 10.0)
        * 0.15;

    float equator =
        1.0 -
        abs(vertexNormal.y);

    equator =
        pow(equator, 0.6);

    float displacement =
        (
            macroBlob * 1.8 +
            ripple
        )
        * equator;

    float pulse =
        sin(u_timeVS * 5.0) * 0.20;

    vec3 displacedPos =
        vertexPosition +
        vertexNormal *
        (displacement + pulse);

    fragPosition =
        vec3(
            matModel *
            vec4(displacedPos, 1.0));

    fragNormal =
        normalize(
            mat3(matModel) *
            vertexNormal);

    fragTexCoord =
        vertexTexCoord;

    gl_Position =
        mvp *
        vec4(displacedPos, 1.0);
}