#ifndef WUXING_MESH_DEFORM_GLSL
#define WUXING_MESH_DEFORM_GLSL

#include "core/uv/shaders/uv_deform.glsl"

// ============================================================
// WUXING — core/deform's FIRST GLSL mirror (05/08/2026)
//
// core/deform/README.md explains why this did not exist until now: no
// vertex shader in this engine samples a texture, so vertex texture fetch
// was unproven on the rlvk backend, and a mirror written with no consumer
// would be a shader nobody could run or verify.
//
// SCOPE, DELIBERATELY NARROWER THAN THE CPU MODULE. This file mirrors only
// the two MeshDeformKind branches that need no texture at all:
//   MESH_DEFORM_SINE            — periodic along the material coordinate
//   the PROCEDURAL LATTICE path — hash noise, no image (mesh_deform.c's
//                                 `else` branch taken when a field has no
//                                 noisePixels, which every consumer routed
//                                 through this file will have)
// The IMAGE-source branch of MESH_DEFORM_NOISE_CHANNEL (an actual sampler
// read in the vertex stage) is NOT mirrored here — that is the one piece
// core/deform/README.md's caution is actually about, and nothing in this
// engine has asked for it yet. Port it, with its own runtime verification,
// when a real consumer needs it — do not add it speculatively.
//
// MESH_DEFORM_CURL is likewise not mirrored: it is a declared enum member
// with no evaluation branch on the CPU side either (mesh_deform.c has never
// implemented it) — there is nothing here to port yet.
//
// Packing (must match MeshDeform_PackGPU in core/deform/mesh_deform.c):
//   u_meshDeform[i*3 + 0] = (kind, direction, channel, amplitude)
//   u_meshDeform[i*3 + 1] = (tiling.x, tiling.y, speed, phase)
//   u_meshDeform[i*3 + 2] = (env, envStart, envEnd, 0)
//   u_meshDeformMeta      = (layerCount, field amplitude, field timeScale, 0)
//
// A landmine paid for twice building this feature (see ENGINE_LANDMINES.md,
// "Writing an include directive INSIDE A COMMENT still includes the file"):
// core/shader_preprocessor.c resolves the include keyword with a bare text
// search that cannot tell a comment from real code. Never spell that eight-
// character keyword followed by a quoted path anywhere in this file, not
// even to describe what NOT to pull in.
// ============================================================

#define MESH_DEFORM_MAX_LAYERS 4
// Must match MeshDeformKind's order in core/deform/mesh_deform.h — append
// only, never renumber (the same contract UV_DEFORM_* already follows).
#define MESH_DEFORM_NOISE_CHANNEL 0
#define MESH_DEFORM_SINE 1
#define MESH_DEFORM_CURL 2

// Must match MeshDeformDirection's order in core/deform/mesh_deform.h.
#define MESH_DEFORM_DIR_NORMAL_SCALE 0
#define MESH_DEFORM_DIR_NORMAL_OFFSET 1
#define MESH_DEFORM_DIR_AXIS 2
#define MESH_DEFORM_DIR_TANGENT 3

uniform vec4 u_meshDeform[MESH_DEFORM_MAX_LAYERS * 3];
uniform vec4 u_meshDeformMeta;

// ------------------------------------------------------------------
// PROCEDURAL LATTICE — mirrors MD_Hash/MD_Smooth/MeshDeform_SampleLattice
// in core/deform/mesh_deform.c EXACTLY, integer constant for integer
// constant. periodU/periodV wrap the (x,y) lattice cell so it tiles; the
// THIRD axis (w, normally time*speed) does not wrap — same as the CPU
// version, which never folds it either (see that file's own long-session
// caveat about unbounded noise domains; this mirror inherits it rather
// than fixing it, to stay a faithful reproduction of the reference).
// ------------------------------------------------------------------
float MeshDeform_Hash(int x, int y, int z)
{
    uint h = uint(x * 374761393 + y * 668265263 + z * 2147483647);
    h = (h ^ (h >> 13u)) * 1274126177u;
    return float((h ^ (h >> 16u)) & 0xFFFFFFu) / float(0xFFFFFF);
}

float MeshDeform_Smooth(float t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float MeshDeform_SampleLattice(float u, float v, float w, int periodU, int periodV)
{
    if (periodU < 1) periodU = 1;
    if (periodV < 1) periodV = 1;
    float xf = u * float(periodU);
    float yf = v * float(periodV);
    int x0 = ((int(floor(xf)) % periodU) + periodU) % periodU;
    int y0 = ((int(floor(yf)) % periodV) + periodV) % periodV;
    int x1 = (x0 + 1) % periodU;
    int y1 = (y0 + 1) % periodV;
    int z0 = int(floor(w));
    int z1 = z0 + 1;
    float fx = MeshDeform_Smooth(xf - floor(xf));
    float fy = MeshDeform_Smooth(yf - floor(yf));
    float fz = MeshDeform_Smooth(w - floor(w));

    float a = MeshDeform_Hash(x0, y0, z0) + (MeshDeform_Hash(x1, y0, z0) - MeshDeform_Hash(x0, y0, z0)) * fx;
    float b = MeshDeform_Hash(x0, y1, z0) + (MeshDeform_Hash(x1, y1, z0) - MeshDeform_Hash(x0, y1, z0)) * fx;
    float c0 = a + (b - a) * fy;
    a = MeshDeform_Hash(x0, y0, z1) + (MeshDeform_Hash(x1, y0, z1) - MeshDeform_Hash(x0, y0, z1)) * fx;
    b = MeshDeform_Hash(x0, y1, z1) + (MeshDeform_Hash(x1, y1, z1) - MeshDeform_Hash(x0, y1, z1)) * fx;
    float c1 = a + (b - a) * fy;
    return c0 + (c1 - c0) * fz;
}

// ------------------------------------------------------------------
// ONE LAYER -> its scalar weight. Mirrors MeshDeform_EvaluateLayer.
// `surf` is (across, along) — surf.y (along) is what the envelope reads,
// hardcoded, same as the CPU version (no envAxis here — mesh_deform never
// gained the extra axis-select field uv_deform has).
// `mat` is the material/drive coordinate — mat.y drives both the SINE
// phase and the lattice's v axis, mat.x drives the lattice's u axis, same
// split the CPU version uses.
// ------------------------------------------------------------------
float MeshDeform_EvaluateLayer(vec4 p0, vec4 p1, vec4 p2, vec2 surf, vec2 mat,
                               float time, float fieldTimeScale,
                               int latticeAround, int latticeAlong)
{
    int kind = int(p0.x);
    float amplitude = p0.w;
    vec2 tiling = p1.xy;
    float speed = p1.z;
    float phase = p1.w;
    int env = int(p2.x);
    float envStart = p2.y, envEnd = p2.z;

    float wt = time * fieldTimeScale;
    float raw;
    if (kind == MESH_DEFORM_SINE)
    {
        raw = 0.5 + 0.5 * UVDeform_SinePhase(mat.y * tiling.y + wt * speed, phase, 1.0);
    }
    else
    {
        // Procedural lattice only — see this file's header for why the
        // image-source branch is not mirrored here.
        raw = MeshDeform_SampleLattice(mat.x, mat.y * tiling.y, wt * speed,
                                       latticeAround, latticeAlong);
    }

    float w = (raw - 0.5) * amplitude;
    return w * UVDeform_Envelope(env, surf.y, envStart, envEnd);
}

// ------------------------------------------------------------------
// THE WHOLE FIELD -> a vertex-space offset, along caller-supplied `axis`
// (MESH_DEFORM_DIR_AXIS) and `tangent` (MESH_DEFORM_DIR_TANGENT) — the two
// directions core/deform/mesh_deform.h documents as "for a caller-supplied
// direction instead [of the mesh's own normal], for shear and for
// stretching along the sweep". Mirrors MeshDeform_Evaluate, restricted to
// those two directions: NORMAL_SCALE/NORMAL_OFFSET need a per-vertex
// surface normal a flat ribbon strip does not have in the way a swept tube
// does, so no consumer of this entry point packs a layer with either.
// `latticeAround`/`latticeAlong` are the FIELD's own base periods
// (MeshDeformField.latticeAround/latticeAlong on the C side) — passed
// separately because they are not part of the per-layer packed data.
// ------------------------------------------------------------------
vec3 MeshDeform_ApplyField(vec2 surf, vec2 mat, float time, vec3 axis,
                           vec3 tangent, int latticeAround, int latticeAlong)
{
    int n = int(u_meshDeformMeta.x);
    float fieldAmplitude = u_meshDeformMeta.y;
    float fieldTimeScale = u_meshDeformMeta.z;
    vec3 offset = vec3(0.0);
    for (int i = 0; i < MESH_DEFORM_MAX_LAYERS; i++)
    {
        if (i >= n) break;
        vec4 p0 = u_meshDeform[i * 3 + 0];
        vec4 p1 = u_meshDeform[i * 3 + 1];
        vec4 p2 = u_meshDeform[i * 3 + 2];
        float w = MeshDeform_EvaluateLayer(p0, p1, p2, surf, mat, time,
                                           fieldTimeScale, latticeAround, latticeAlong);
        int direction = int(p0.y);
        float s = w * fieldAmplitude;
        if (direction == MESH_DEFORM_DIR_AXIS)
            offset += axis * s;
        else if (direction == MESH_DEFORM_DIR_TANGENT)
            offset += tangent * s;
        // NORMAL_SCALE/NORMAL_OFFSET: not handled — see this function's
        // header comment. A layer packed with either contributes nothing
        // here rather than silently doing the wrong thing with a normal
        // this entry point was never given.
    }
    return offset;
}

#endif
