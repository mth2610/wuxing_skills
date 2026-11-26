#version 330

#ifdef GL_ES
precision highp float;
#endif

// GPU-skinned animated 3D VFX model vertex shader.
// Uses 3x4 affine bone rows packed in u_boneRows (up to 64 bones = 192 vec4s).
// Avoids per-frame UpdateMeshBuffer() stalls on Vulkan/rlvk (ENGINE_LANDMINES §30).

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec4 vertexBoneIds;
in vec4 vertexBoneWeights;

uniform mat4  mvp;
uniform mat4  matModel;
uniform mat4  matNormal;
uniform mat4  u_worldFromShaderSpace;
uniform float u_skinned;
uniform vec2  u_spineBounds; // x = head minX, y = tail maxX in bind pose
uniform vec4  u_boneRows[192]; // 64 bones * 3 affine rows

out vec2  fragTexCoord;
out vec4  fragColor;
out vec3  fragWorldPos;
out vec3  fragWorldNormal;
out vec3  fragLocalPos;
out float fragSpineT;

vec3 SkinPosition(int boneIdx, vec3 p) {
    int r = clamp(boneIdx, 0, 63) * 3;
    vec4 p4 = vec4(p, 1.0);
    return vec3(dot(u_boneRows[r + 0], p4),
                dot(u_boneRows[r + 1], p4),
                dot(u_boneRows[r + 2], p4));
}

vec3 SkinNormal(int boneIdx, vec3 n) {
    int r = clamp(boneIdx, 0, 63) * 3;
    return vec3(dot(u_boneRows[r + 0].xyz, n),
                dot(u_boneRows[r + 1].xyz, n),
                dot(u_boneRows[r + 2].xyz, n));
}

void main() {
    vec3 pos = vertexPosition;
    vec3 nrm = vertexNormal;

    if (u_skinned > 0.5) {
        vec4 w = vertexBoneWeights;
        float wSum = w.x + w.y + w.z + w.w;
        if (wSum > 0.0001) {
            w /= wSum;
            ivec4 ids = ivec4(vertexBoneIds + 0.5);
            vec3 sp = vec3(0.0);
            vec3 sn = vec3(0.0);
            if (w.x > 0.0) { sp += SkinPosition(ids.x, pos) * w.x; sn += SkinNormal(ids.x, nrm) * w.x; }
            if (w.y > 0.0) { sp += SkinPosition(ids.y, pos) * w.y; sn += SkinNormal(ids.y, nrm) * w.y; }
            if (w.z > 0.0) { sp += SkinPosition(ids.z, pos) * w.z; sn += SkinNormal(ids.z, nrm) * w.z; }
            if (w.w > 0.0) { sp += SkinPosition(ids.w, pos) * w.w; sn += SkinNormal(ids.w, nrm) * w.w; }
            pos = sp;
            float nLen = length(sn);
            nrm = (nLen > 0.0001) ? (sn / nLen) : vertexNormal;
        }
    }

    fragTexCoord    = vertexTexCoord;
    fragColor       = vertexColor;
    fragLocalPos    = pos;
    fragSpineT      = clamp((vertexPosition.x - u_spineBounds.x) /
                            max(u_spineBounds.y - u_spineBounds.x, 0.001), 0.0, 1.0);

    vec3 shaderPos  = vec3(matModel * vec4(pos, 1.0));
    fragWorldPos    = vec3(u_worldFromShaderSpace * vec4(shaderPos, 1.0));
    vec3 shaderNrm  = vec3(matNormal * vec4(nrm, 0.0));
    vec3 worldNrm   = mat3(u_worldFromShaderSpace) * shaderNrm;
    float wnLen     = length(worldNrm);
    fragWorldNormal = (wnLen > 0.0001) ? (worldNrm / wnLen) : vec3(0.0, 1.0, 0.0);

    gl_Position     = mvp * vec4(pos, 1.0);
}
