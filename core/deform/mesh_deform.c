#include "core/deform/mesh_deform.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MD_TAU
#define MD_TAU 6.2831853f
#endif

// The samplers below were lifted VERBATIM from core/geometry/pm_tube.inl, which
// is now a consumer rather than an owner. Two live effects depend on their
// exact arithmetic — the trail tube on the image path, the beam on the lattice
// path — so this is a move, not a rewrite. core/tests/mesh_deform_test.c
// asserts both still produce the same floats they did before the move.

float MeshDeform_SampleImage(const unsigned char *px, int w, int h, int channel,
                             float u, float v) {
  if (px == NULL || w <= 0 || h <= 0) return 0.5f;
  float xf = u * (float)w - 0.5f, yf = v * (float)h - 0.5f;
  int x0 = (int)floorf(xf), y0 = (int)floorf(yf);
  float fx = xf - (float)x0, fy = yf - (float)y0;
  int x1 = ((x0 + 1) % w + w) % w, y1 = ((y0 + 1) % h + h) % h;
  x0 = (x0 % w + w) % w;
  y0 = (y0 % h + h) % h;
  const int s = 4; /* R8G8B8A8 */
  float a = (float)px[(y0 * w + x0) * s + channel];
  float b = (float)px[(y0 * w + x1) * s + channel];
  float c = (float)px[(y1 * w + x0) * s + channel];
  float d = (float)px[(y1 * w + x1) * s + channel];
  float top = a + (b - a) * fx, bot = c + (d - c) * fx;
  return (top + (bot - top) * fy) / 255.0f;
}

static float MD_Hash(int x, int y, int z) {
  unsigned int h = (unsigned int)(x * 374761393 + y * 668265263 + z * 2147483647);
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) & 0xFFFFFF) / (float)0xFFFFFF;
}

static float MD_Smooth(float t) {
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float MeshDeform_SampleLattice(float u, float v, float w, int periodU, int periodV) {
  if (periodU < 1) periodU = 1;
  if (periodV < 1) periodV = 1;
  float xf = u * (float)periodU, yf = v * (float)periodV;
  int x0 = ((int)floorf(xf) % periodU + periodU) % periodU;
  int y0 = ((int)floorf(yf) % periodV + periodV) % periodV;
  int x1 = (x0 + 1) % periodU, y1 = (y0 + 1) % periodV;
  int z0 = (int)floorf(w), z1 = z0 + 1;
  float fx = MD_Smooth(xf - floorf(xf)), fy = MD_Smooth(yf - floorf(yf));
  float fz = MD_Smooth(w - floorf(w));
  float a = MD_Hash(x0, y0, z0) + (MD_Hash(x1, y0, z0) - MD_Hash(x0, y0, z0)) * fx;
  float b = MD_Hash(x0, y1, z0) + (MD_Hash(x1, y1, z0) - MD_Hash(x0, y1, z0)) * fx;
  float c0 = a + (b - a) * fy;
  a = MD_Hash(x0, y0, z1) + (MD_Hash(x1, y0, z1) - MD_Hash(x0, y0, z1)) * fx;
  b = MD_Hash(x0, y1, z1) + (MD_Hash(x1, y1, z1) - MD_Hash(x0, y1, z1)) * fx;
  float c1 = a + (b - a) * fy;
  return c0 + (c1 - c0) * fz;
}

// -----------------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------------

void MeshDeform_Clear(MeshDeformField *f) {
  if (!f) return;
  memset(f, 0, sizeof(*f));
  f->amplitude = 1.0f;
  f->timeScale = 1.0f;
  f->latticeAround = 8;
  f->latticeAlong = 4;
}

bool MeshDeform_AddLayer(MeshDeformField *f, MeshDeformLayer layer) {
  if (!f || f->layerCount >= MESH_DEFORM_MAX_LAYERS) return false;

  // Range-check against the COUNT sentinel, never the last member by name, and
  // announce every clamp: a silent one yields a plausible wrong result.
  if ((int)layer.kind < 0 || (int)layer.kind >= MESH_DEFORM_KIND_COUNT) {
    TraceLog(LOG_WARNING, "MeshDeform: kind %d out of range [0,%d) — clamped",
             (int)layer.kind, (int)MESH_DEFORM_KIND_COUNT);
    layer.kind = MESH_DEFORM_NOISE_CHANNEL;
  }
  if ((int)layer.direction < 0 || (int)layer.direction >= MESH_DEFORM_DIR_COUNT) {
    TraceLog(LOG_WARNING, "MeshDeform: direction %d out of range [0,%d) — clamped",
             (int)layer.direction, (int)MESH_DEFORM_DIR_COUNT);
    layer.direction = MESH_DEFORM_DIR_NORMAL_SCALE;
  }
  if ((int)layer.env < 0 || (int)layer.env >= UV_ENV_KIND_COUNT) {
    TraceLog(LOG_WARNING, "MeshDeform: envelope %d out of range [0,%d) — clamped",
             (int)layer.env, (int)UV_ENV_KIND_COUNT);
    layer.env = UV_ENV_NONE;
  }
  if (layer.channel < 0 || layer.channel > 3) {
    TraceLog(LOG_WARNING, "MeshDeform: channel %d outside RGBA — clamped to 0",
             layer.channel);
    layer.channel = 0;
  }

  f->layers[f->layerCount++] = layer;
  return true;
}

MeshDeformField MeshDeform_CreatePreset(MeshDeformPreset preset) {
  MeshDeformField f;
  MeshDeform_Clear(&f);

  switch (preset) {
  case MESH_DEFORM_PRESET_TUBE_CHURN:
    // The trail tube's two octaves, reproduced exactly: one coarse layer for the
    // body swelling and collapsing, one fine layer for surface ripple. A single
    // octave gives a wavy ellipse — motion without detail. The two drift at
    // different rates so bulk and ripple never phase-lock into one beat.
    f.timeScale = 1.6f;
    MeshDeform_AddLayer(&f, (MeshDeformLayer){.kind = MESH_DEFORM_NOISE_CHANNEL,
                                              .direction = MESH_DEFORM_DIR_NORMAL_SCALE,
                                              .channel = 0,
                                              .tiling = {1.0f, 1.0f},
                                              .amplitude = 2.0f,
                                              .speed = 0.05f,
                                              .latticeMul = 1.0f,
                                              .env = UV_ENV_NONE});
    MeshDeform_AddLayer(&f, (MeshDeformLayer){.kind = MESH_DEFORM_NOISE_CHANNEL,
                                              .direction = MESH_DEFORM_DIR_NORMAL_SCALE,
                                              .channel = 1,
                                              .tiling = {2.0f, 1.6f},
                                              .amplitude = 0.9f,
                                              .speed = 0.09f,
                                              .latticeMul = 1.0f,
                                              .env = UV_ENV_NONE});
    break;

  case MESH_DEFORM_PRESET_BEAM_RIPPLE:
    // Same two octaves on the PROCEDURAL lattice: no asset, and the period
    // around the section is the section's own segment count so the field wraps
    // by construction.
    f.timeScale = 1.6f;
    MeshDeform_AddLayer(&f, (MeshDeformLayer){.kind = MESH_DEFORM_NOISE_CHANNEL,
                                              .direction = MESH_DEFORM_DIR_NORMAL_SCALE,
                                              .tiling = {1.0f, 1.0f},
                                              .amplitude = 2.0f,
                                              .speed = 1.0f,
                                              .latticeMul = 1.0f,
                                              .env = UV_ENV_NONE});
    MeshDeform_AddLayer(&f, (MeshDeformLayer){.kind = MESH_DEFORM_NOISE_CHANNEL,
                                              .direction = MESH_DEFORM_DIR_NORMAL_SCALE,
                                              .tiling = {1.0f, 1.6f},
                                              .amplitude = 0.9f,
                                              .speed = 1.7f,
                                              .timeOffset = 11.0f,
                                              .latticeMul = 3.0f,
                                              .env = UV_ENV_NONE});
    break;

  case MESH_DEFORM_PRESET_SMOKE_COLUMN:
    // Welded at the source and opening with height. HEAD_WELD is exactly zero
    // at v = 0, so the column leaves its emitter as one coherent piece and only
    // comes apart above it — without that anchor the base fidgets and the whole
    // thing reads as a floating object rather than something being emitted.
    f.timeScale = 0.9f;
    MeshDeform_AddLayer(&f, (MeshDeformLayer){.kind = MESH_DEFORM_NOISE_CHANNEL,
                                              .direction = MESH_DEFORM_DIR_NORMAL_SCALE,
                                              .channel = 0,
                                              .tiling = {1.0f, 0.8f},
                                              .amplitude = 2.0f,
                                              .speed = 0.06f,
                                              .latticeMul = 1.0f,
                                              .env = UV_ENV_HEAD_WELD,
                                              .envStart = 0.0f,
                                              .envEnd = 0.18f});
    MeshDeform_AddLayer(&f, (MeshDeformLayer){.kind = MESH_DEFORM_NOISE_CHANNEL,
                                              .direction = MESH_DEFORM_DIR_NORMAL_OFFSET,
                                              .channel = 1,
                                              .tiling = {2.0f, 1.7f},
                                              .amplitude = 0.55f,
                                              .speed = 0.11f,
                                              .latticeMul = 3.0f,
                                              .env = UV_ENV_HEAD_WELD,
                                              .envStart = 0.0f,
                                              .envEnd = 0.30f});
    break;

  default:
    TraceLog(LOG_WARNING, "MeshDeform: unknown preset %d — empty field", (int)preset);
    break;
  }
  return f;
}

// -----------------------------------------------------------------------------
// Evaluation
// -----------------------------------------------------------------------------

float MeshDeform_EvaluateLayer(const MeshDeformField *f, const MeshDeformLayer *L,
                               Vector2 surf, Vector2 mat, float time) {
  if (!f || !L) return 0.0f;

  float wt = time * f->timeScale;
  float raw;

  if (L->kind == MESH_DEFORM_SINE) {
    // Folded before scaling to radians: sin(2pi*(n+x)) == sin(2pi*x), so
    // dropping whole turns cannot change the result and the argument stops
    // outgrowing float32's ability to resolve a fraction of a cycle.
    raw = 0.5f + 0.5f * UVDeform_SinePhase(mat.y * L->tiling.y + wt * L->speed,
                                           L->phase, 1.0f);
  } else if (f->noisePixels != NULL) {
    // IMAGE source. tiling.x scales the around-axis here because a bilinear
    // fetch of a seamless sheet stays seamless under any integer scale.
    raw = MeshDeform_SampleImage(f->noisePixels, f->noiseImgW, f->noiseImgH,
                                 L->channel, mat.x * L->tiling.x,
                                 mat.y * L->tiling.y + wt * L->speed);
  } else {
    // PROCEDURAL source. The around-period is the section's own segment count
    // and tiling.x is deliberately NOT applied: a period that does not divide
    // the section leaves a seam running the whole length of the body.
    int periodV = (int)((float)f->latticeAlong *
                        (L->latticeMul > 0.0f ? L->latticeMul : 1.0f));
    raw = MeshDeform_SampleLattice(mat.x, mat.y * L->tiling.y,
                                   wt * L->speed + L->timeOffset,
                                   f->latticeAround, periodV);
  }

  // Remap [0,1] -> [-1,1] scaled by this layer's weight. Without the bias the
  // surface can only push outward and the body inflates instead of churning.
  float w = (raw - 0.5f) * L->amplitude;
  return w * UVDeform_Envelope(L->env, surf.y, L->envStart, L->envEnd);
}

MeshDeformResult MeshDeform_Evaluate(const MeshDeformField *f, Vector2 surf,
                                     Vector2 mat, float time, Vector3 normal,
                                     Vector3 axis, Vector3 tangent) {
  MeshDeformResult out;
  out.radiusScale = 1.0f;
  out.radiusDelta = 0.0f;
  out.offset = (Vector3){0.0f, 0.0f, 0.0f};
  if (!f || f->layerCount <= 0) return out;

  // NORMAL_SCALE layers accumulate into ONE sum that the field amplitude scales
  // exactly once at the end. Folding the amplitude into each layer instead
  // would be algebraically equal and a different float — which is the whole
  // reason a caller can migrate onto this module without its output moving.
  float scaleSum = 0.0f;

  for (int i = 0; i < f->layerCount; i++) {
    const MeshDeformLayer *L = &f->layers[i];
    float w = MeshDeform_EvaluateLayer(f, L, surf, mat, time);

    switch (L->direction) {
    case MESH_DEFORM_DIR_NORMAL_SCALE:
      scaleSum += w;
      break;
    case MESH_DEFORM_DIR_NORMAL_OFFSET: {
      float s = w * f->amplitude;
      out.offset.x += normal.x * s;
      out.offset.y += normal.y * s;
      out.offset.z += normal.z * s;
      break;
    }
    case MESH_DEFORM_DIR_AXIS: {
      float s = w * f->amplitude;
      out.offset.x += axis.x * s;
      out.offset.y += axis.y * s;
      out.offset.z += axis.z * s;
      break;
    }
    case MESH_DEFORM_DIR_TANGENT: {
      float s = w * f->amplitude;
      out.offset.x += tangent.x * s;
      out.offset.y += tangent.y * s;
      out.offset.z += tangent.z * s;
      break;
    }
    default:
      break;
    }
  }

  out.radiusDelta = f->amplitude * scaleSum;
  out.radiusScale = 1.0f + out.radiusDelta;
  return out;
}

// -----------------------------------------------------------------------------
// GPU
// -----------------------------------------------------------------------------

void MeshDeform_PackGPU(const MeshDeformField *f, float *outFloats, float *outMeta) {
  if (!outFloats || !outMeta) return;
  memset(outFloats, 0, sizeof(float) * MESH_DEFORM_PACK_FLOATS);
  outMeta[0] = outMeta[1] = outMeta[2] = outMeta[3] = 0.0f;
  if (!f) return;

  int packed = 0;
  for (int i = 0; i < f->layerCount && packed < MESH_DEFORM_MAX_LAYERS; i++) {
    const MeshDeformLayer *L = &f->layers[i];
    if (fabsf(L->amplitude) < 1e-6f) continue;

    float *p = outFloats + packed * MESH_DEFORM_FLOATS_PER_LAYER;
    p[0] = (float)L->kind;
    p[1] = (float)L->direction;
    p[2] = (float)L->channel;
    p[3] = L->amplitude;

    p[4] = L->tiling.x;
    p[5] = L->tiling.y;
    p[6] = L->speed;
    p[7] = L->phase;

    p[8] = (float)L->env;
    p[9] = L->envStart;
    p[10] = L->envEnd;
    p[11] = 0.0f;
    packed++;
  }

  outMeta[0] = (float)packed;
  outMeta[1] = f->amplitude;
  outMeta[2] = f->timeScale;
}

MeshDeformLocs MeshDeform_CacheLocations(Shader shader) {
  MeshDeformLocs locs;
  locs.fieldLoc = GetShaderLocation(shader, "u_meshDeform");
  locs.metaLoc = GetShaderLocation(shader, "u_meshDeformMeta");
  return locs;
}

void MeshDeform_Apply(const MeshDeformField *f, Shader shader,
                      const MeshDeformLocs *locs) {
  if (!f || !locs) return;

  float packed[MESH_DEFORM_PACK_FLOATS];
  float meta[4];
  MeshDeform_PackGPU(f, packed, meta);

  // vec4 arrays only — a float[] or vec3[] uniform array is padded to 16 bytes
  // per element under std140, so a tightly packed upload lands element 0 and
  // misaligns everything after it.
  if (locs->fieldLoc != -1)
    SetShaderValueV(shader, locs->fieldLoc, packed, SHADER_UNIFORM_VEC4,
                    MESH_DEFORM_MAX_LAYERS * 3);
  if (locs->metaLoc != -1)
    SetShaderValue(shader, locs->metaLoc, meta, SHADER_UNIFORM_VEC4);
}
