#version 330

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;

// Output vertex attributes (to fragment shader)
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPosition;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    
    // Calculate fragment position in world space
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    
    // Calculate normal in world space
    fragNormal = normalize(vec3(matModel * vec4(vertexNormal, 0.0)));
    
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
