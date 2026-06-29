#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;
uniform float u_time;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

float getJiggleWobble(vec3 pos) {
    float swell = sin(pos.y * 0.3 + u_time * 15.0);
    float bump  = cos(pos.x * 0.3 + u_time * 10.0) * sin(pos.z * 0.3 - u_time * 12.0);
    return (swell * 0.6 + bump * 0.4) * 3.0;
}

void main() {
    float wobble = getJiggleWobble(vertexPosition);
    vec3 displacedPos = vertexPosition + vertexNormal * wobble;

    fragPosition = vec3(matModel * vec4(displacedPos, 1.0));
    fragNormal   = normalize(vec3(matModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;
    gl_Position  = mvp * vec4(displacedPos, 1.0);
}
