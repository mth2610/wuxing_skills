#include "core/uv/uv_deform.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UV_TAU
#define UV_TAU 6.2831853f
#endif

// The CPU half of core/uv/shaders/uv_deform.glsl. Every function below is a
// transliteration of the GLSL one of the same name — same operations in the
// same order, so a headless test can assert the maths without a GPU.
// core/tests/uv_deform_test.c guards the mirror against rot by asserting the
// load-bearing expressions still exist in the .glsl.

static float Fract(float x) { return x - floorf(x); }

static float SmoothStep(float e0, float e1, float x) {
  float span = e1 - e0;
  if (fabsf(span) < 1e-9f) return (x < e0) ? 0.0f : 1.0f;
  float t = (x - e0) / span;
  if (t < 0.0f) t = 0.0f;
  else if (t > 1.0f) t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

// -----------------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------------

void UVDeform_Clear(UVDeformField *f) {
  if (!f) return;
  f->layerCount = 0;
  f->stretchUV = false;
}

bool UVDeform_AddLayer(UVDeformField *f, UVDeformLayer layer) {
  if (!f || f->layerCount >= UV_DEFORM_MAX_LAYERS) return false;

  // Range-check against the COUNT sentinel, never the last kind by name: a
  // check written against a named member silently starts clamping the day
  // someone appends a kind. And announce the clamp — a silent one produces a
  // plausible-looking result, which is the worst failure mode available.
  if ((int)layer.kind < 0 || (int)layer.kind >= UV_DEFORM_KIND_COUNT) {
    TraceLog(LOG_WARNING,
             "UVDeform: layer kind %d out of range [0,%d) — clamped to "
             "UV_DEFORM_SINE",
             (int)layer.kind, (int)UV_DEFORM_KIND_COUNT);
    layer.kind = UV_DEFORM_SINE;
  }
  if ((int)layer.env < 0 || (int)layer.env >= UV_ENV_KIND_COUNT) {
    TraceLog(LOG_WARNING,
             "UVDeform: envelope kind %d out of range [0,%d) — clamped to "
             "UV_ENV_NONE",
             (int)layer.env, (int)UV_ENV_KIND_COUNT);
    layer.env = UV_ENV_NONE;
  }

  f->layers[f->layerCount++] = layer;
  return true;
}

void UVDeform_SetPhase(UVDeformField *f, float basePhase) {
  if (!f) return;
  for (int i = 0; i < f->layerCount; i++) {
    if (f->layers[i].kind == UV_DEFORM_SINE)
      f->layers[i].phase = basePhase * f->layers[i].param;
  }
}

UVDeformField UVDeform_CreatePreset(UVDeformPreset preset) {
  UVDeformField f;
  UVDeform_Clear(&f);

  switch (preset) {
  case UV_DEFORM_PRESET_SIN_WAVE_TRAIL: {
    // Reproduces trail_deform.fs mode 2's three wave fields exactly, with the
    // VFX_STRAND_ENERGY defaults from core/composition/common/vc_strand_trail.inl.
    // The detune in both frequency and amplitude is the whole point: three
    // fields that crest together are one thick band, not three crossing ones.
    // `param` carries each layer's phase multiplier for UVDeform_SetPhase.
    const float amp = 0.40f, freq = 0.55f, travel = 0.85f;
    const float spread = 0.65f, envHead = 0.10f;
    for (int i = 0; i < 3; i++) {
      const float fMul[3] = {1.0f, 1.0f + 0.73f * spread, 1.0f - 0.39f * spread};
      const float sMul[3] = {1.0f, 1.41f, 0.67f};
      const float aMul[3] = {1.0f, 1.0f - 0.28f * spread, 1.0f + 0.25f * spread};
      const float pMul[3] = {1.0f, 2.3f, 4.1f};
      UVDeform_AddLayer(&f, (UVDeformLayer){.kind = UV_DEFORM_SINE,
                                            .driveAxis = 0, // metres of laid path
                                            .outAxis = 0,   // across the width
                                            .amplitude = amp * aMul[i],
                                            .frequency = freq * fMul[i],
                                            .speed = travel * sMul[i],
                                            .phase = 0.0f,
                                            .param = pMul[i],
                                            .env = UV_ENV_HEAD_WELD,
                                            .envAxis = 1, // along the surface
                                            .envStart = 0.0f,
                                            .envEnd = envHead});
    }
    break;
  }

  case UV_DEFORM_PRESET_SCROLL_V:
    UVDeform_AddLayer(&f, (UVDeformLayer){.kind = UV_DEFORM_SINE,
                                          .driveAxis = 1,
                                          .outAxis = 1,
                                          .amplitude = 0.08f,
                                          .frequency = 1.0f,
                                          .speed = 0.35f,
                                          .param = 1.0f,
                                          .env = UV_ENV_NONE});
    break;

  case UV_DEFORM_PRESET_VORTEX:
    UVDeform_AddLayer(&f, (UVDeformLayer){.kind = UV_DEFORM_SWIRL,
                                          .amplitude = 1.20f,
                                          .frequency = 2.60f, // radial falloff
                                          .phase = 0.5f,      // centre X
                                          .param = 0.5f,      // centre Y
                                          .env = UV_ENV_NONE});
    UVDeform_AddLayer(&f, (UVDeformLayer){.kind = UV_DEFORM_SINE,
                                          .driveAxis = 0,
                                          .outAxis = 1,
                                          .amplitude = 0.05f,
                                          .frequency = 3.0f,
                                          .speed = 0.7f,
                                          .param = 1.0f,
                                          .env = UV_ENV_BELL,
                                          .envAxis = 1,
                                          .envStart = 0.0f,
                                          .envEnd = 1.0f});
    break;

  default:
    TraceLog(LOG_WARNING, "UVDeform: unknown preset %d — returning empty field",
             (int)preset);
    break;
  }

  return f;
}

// -----------------------------------------------------------------------------
// CPU mirror of the GLSL
// -----------------------------------------------------------------------------

float UVDeform_Envelope(UVEnvelopeKind kind, float c, float start, float end) {
  if (kind == UV_ENV_NONE) return 1.0f;
  if (kind == UV_ENV_SMOOTHSTEP) return SmoothStep(start, end, c);
  if (kind == UV_ENV_HEAD_WELD) return SmoothStep(start, end, c) * c;

  float span = end - start;
  float k = (c - start) / (fabsf(span) < 0.000001f ? 0.000001f : span);
  if (k < 0.0f) k = 0.0f;
  else if (k > 1.0f) k = 1.0f;
  if (kind == UV_ENV_BELL) return 0.5f - 0.5f * cosf(UV_TAU * k);
  return k; // UV_ENV_RAMP
}

float UVDeform_SinePhase(float turns, float phase, float amp) {
  return sinf(Fract(turns) * UV_TAU + phase) * amp;
}

float UVDeform_Sine(float drive, float t, float freq, float speed, float phase,
                    float amp) {
  return UVDeform_SinePhase(drive * freq + t * speed, phase, amp);
}

float UVDeform_FoldAngle(float radians) { return Fract(radians / UV_TAU) * UV_TAU; }

float UVDeform_NoiseFlow(float sA, float sB, float amp) {
  return ((sA - 0.5f) + (sB - 0.5f)) * amp;
}

Vector2 UVDeform_EvaluateLayer(const UVDeformLayer *L, Vector2 uv, Vector2 mat,
                               float t, Vector2 noisePair) {
  Vector2 out = {0.0f, 0.0f};
  if (!L) return out;

  float envC = (L->envAxis == 0) ? mat.x : mat.y;
  float amp = L->amplitude * UVDeform_Envelope(L->env, envC, L->envStart, L->envEnd);

  if (L->kind == UV_DEFORM_SWIRL) {
    float cx = L->phase, cy = L->param;
    float dx = uv.x - cx, dy = uv.y - cy;
    float r = sqrtf(dx * dx + dy * dy);
    float a = amp * ((L->frequency > 0.0f) ? expf(-r * L->frequency) : 1.0f);
    float s = sinf(a), co = cosf(a);
    out.x = cx + (dx * co - dy * s) - uv.x;
    out.y = cy + (dx * s + dy * co) - uv.y;
    return out;
  }

  // FLOWMAP needs a texture and exists only on the GPU path, the same way
  // FORCE_VECTOR_TEXTURE is a deliberate CPU no-op in core/force_field.c.
  if (L->kind == UV_DEFORM_FLOWMAP) return out;

  float drive = (L->driveAxis == 0) ? mat.x : mat.y;
  float w = (L->kind == UV_DEFORM_NOISE_FLOW)
                ? UVDeform_NoiseFlow(noisePair.x, noisePair.y, amp)
                : UVDeform_Sine(drive, t, L->frequency, L->speed, L->phase, amp);

  if (L->outAxis == 0) out.x = w;
  else out.y = w;
  return out;
}

Vector2 UVDeform_Evaluate(const UVDeformField *f, Vector2 uv, Vector2 mat,
                          float t, Vector2 noisePair) {
  if (!f) return uv;
  Vector2 out = uv;
  for (int i = 0; i < f->layerCount; i++) {
    Vector2 d = UVDeform_EvaluateLayer(&f->layers[i], uv, mat, t, noisePair);
    out.x += d.x;
    out.y += d.y;
  }
  return out;
}

// -----------------------------------------------------------------------------
// GPU
// -----------------------------------------------------------------------------

void UVDeform_PackGPU(const UVDeformField *f, float *outFloats, float *outMeta) {
  if (!outFloats || !outMeta) return;
  memset(outFloats, 0, sizeof(float) * UV_DEFORM_PACK_FLOATS);
  outMeta[0] = 0.0f;
  outMeta[1] = 0.0f;
  outMeta[2] = 0.0f;
  outMeta[3] = 0.0f;
  if (!f) return;

  int packed = 0;
  for (int i = 0; i < f->layerCount && packed < UV_DEFORM_MAX_LAYERS; i++) {
    const UVDeformLayer *L = &f->layers[i];
    // Compact: a zero-amplitude layer contributes nothing but still costs the
    // shader a loop iteration and three texture-free evaluations. GPU indices
    // are therefore NOT CPU indices — same contract as ForceField_PackGPU.
    if (fabsf(L->amplitude) < 1e-6f) continue;

    float *p = outFloats + packed * UV_DEFORM_FLOATS_PER_LAYER;
    p[0] = (float)L->kind;
    p[1] = (float)L->driveAxis;
    p[2] = (float)L->outAxis;
    p[3] = L->amplitude;

    p[4] = L->frequency;
    p[5] = L->speed;
    p[6] = L->phase;
    p[7] = L->param;

    p[8] = (float)L->env;
    p[9] = (float)L->envAxis;
    p[10] = L->envStart;
    p[11] = L->envEnd;
    packed++;
  }

  outMeta[0] = (float)packed;
  outMeta[1] = f->stretchUV ? 1.0f : 0.0f;
}

UVDeformLocs UVDeform_CacheLocations(Shader shader) {
  UVDeformLocs locs;
  locs.fieldLoc = GetShaderLocation(shader, "u_uvField");
  locs.metaLoc = GetShaderLocation(shader, "u_uvMeta");
  return locs;
}

void UVDeform_Apply(const UVDeformField *f, Shader shader,
                    const UVDeformLocs *locs) {
  if (!f || !locs) return;

  float packed[UV_DEFORM_PACK_FLOATS];
  float meta[4];
  UVDeform_PackGPU(f, packed, meta);

  // vec4 arrays only — a float[] or vec3[] uniform array is padded to 16 bytes
  // per element under std140, so a tightly packed upload lands element 0 and
  // misaligns everything after it.
  if (locs->fieldLoc != -1)
    SetShaderValueV(shader, locs->fieldLoc, packed, SHADER_UNIFORM_VEC4,
                    UV_DEFORM_MAX_LAYERS * 3);
  if (locs->metaLoc != -1)
    SetShaderValue(shader, locs->metaLoc, meta, SHADER_UNIFORM_VEC4);
}
