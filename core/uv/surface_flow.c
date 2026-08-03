#include "core/uv/surface_flow.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The CPU half of core/uv/shaders/surface_flow.glsl — transliterations, so a
// headless test can assert the maths without a GPU. Change one, change both;
// core/tests/uv_deform_test.c guards the mirror.

static float Fract(float x) { return x - floorf(x); }

void SurfaceFlow_Clear(SurfaceFlow *sf) {
  if (!sf) return;
  sf->layerCount = 0;
  sf->stretchUV = false;
  sf->envAxis = 1;
  sf->twoPhase = false;
  sf->twoPhaseSpeed = 1.0f;
  sf->twoPhaseStrength = 0.05f;
}

bool SurfaceFlow_AddLayer(SurfaceFlow *sf, SurfaceFlowLayer layer) {
  if (!sf || sf->layerCount >= SURFACE_FLOW_MAX_LAYERS) return false;

  // Range-check against the COUNT sentinel, never the last member by name, and
  // announce every clamp: a silent one produces a plausible result.
  if ((int)layer.blend < 0 || (int)layer.blend >= SURFACE_FLOW_BLEND_COUNT) {
    TraceLog(LOG_WARNING,
             "SurfaceFlow: blend op %d out of range [0,%d) — clamped to "
             "SURFACE_FLOW_MUL",
             (int)layer.blend, (int)SURFACE_FLOW_BLEND_COUNT);
    layer.blend = SURFACE_FLOW_MUL;
  }
  if ((int)layer.env < 0 || (int)layer.env >= UV_ENV_KIND_COUNT) {
    TraceLog(LOG_WARNING,
             "SurfaceFlow: envelope kind %d out of range [0,%d) — clamped to "
             "UV_ENV_NONE",
             (int)layer.env, (int)UV_ENV_KIND_COUNT);
    layer.env = UV_ENV_NONE;
  }

  sf->layers[sf->layerCount++] = layer;
  return true;
}

float SurfaceFlow_Pan(float t, float speed) { return Fract(t * speed); }

float SurfaceFlow_AlongV(float stretched, float base, float scale, float pan,
                         bool stretch) {
  return stretch ? stretched : Fract(base * scale) - pan;
}

float SurfaceFlow_AcrossU(float across, float centre, float halfWidth) {
  return 0.5f + (across - centre) / (2.0f * halfWidth);
}

Vector2 SurfaceFlow_LayerUV(const SurfaceFlow *sf, int layer, Vector2 uv,
                            Vector2 mat, float t) {
  Vector2 out = uv;
  if (!sf || layer < 0 || layer >= sf->layerCount) return out;
  const SurfaceFlowLayer *L = &sf->layers[layer];

  float panU = SurfaceFlow_Pan(t, L->pan.x);
  float panV = SurfaceFlow_Pan(t, L->pan.y);

  if (sf->stretchUV) {
    out.x = uv.x - panU;
    out.y = uv.y - panV;
    return out;
  }
  // Does NOT fold — see the note on SurfaceFlow_LayerUV in
  // core/uv/shaders/surface_flow.glsl. `mat` is contracted to arrive bounded.
  out.x = mat.x * L->tiling.x - panU;
  out.y = mat.y * L->tiling.y - panV;
  return out;
}

void SurfaceFlow_PackGPU(const SurfaceFlow *sf, float t, float *outFloats,
                         float *outMeta) {
  if (!outFloats || !outMeta) return;
  memset(outFloats, 0, sizeof(float) * SURFACE_FLOW_PACK_FLOATS);
  outMeta[0] = 0.0f;
  outMeta[1] = 0.0f;
  outMeta[2] = 0.0f;
  outMeta[3] = 0.0f;
  if (!sf) return;

  int packed = 0;
  for (int i = 0; i < sf->layerCount && packed < SURFACE_FLOW_MAX_LAYERS; i++) {
    const SurfaceFlowLayer *L = &sf->layers[i];
    if (fabsf(L->tiling.x) < 1e-6f && fabsf(L->tiling.y) < 1e-6f) continue;

    float *p = outFloats + packed * SURFACE_FLOW_FLOATS_PER_LAYER;
    p[0] = L->tiling.x;
    p[1] = L->tiling.y;
    // Fold the pan HERE, on the C side, where the modulus is chosen
    // deliberately. t * speed grows without bound and float32 loses its low
    // bits long before an effect is retired; the sheet wraps REPEAT, so the
    // fold changes nothing visible.
    p[2] = SurfaceFlow_Pan(t, L->pan.x);
    p[3] = SurfaceFlow_Pan(t, L->pan.y);

    p[4] = (float)L->blend;
    p[5] = (float)L->env;
    p[6] = L->envStart;
    p[7] = L->envEnd;
    packed++;
  }

  outMeta[0] = (float)packed;
  outMeta[1] = sf->stretchUV ? 1.0f : 0.0f;
  outMeta[2] = (float)sf->envAxis;
}

SurfaceFlowLocs SurfaceFlow_CacheLocations(Shader shader) {
  SurfaceFlowLocs locs;
  locs.layerLoc = GetShaderLocation(shader, "u_flowLayer");
  locs.metaLoc = GetShaderLocation(shader, "u_flowMeta");
  locs.twoPhaseLoc = GetShaderLocation(shader, "u_flowTwoPhase");
  return locs;
}

void SurfaceFlow_Apply(const SurfaceFlow *sf, Shader shader,
                       const SurfaceFlowLocs *locs, float t) {
  if (!sf || !locs) return;

  float packed[SURFACE_FLOW_PACK_FLOATS];
  float meta[4];
  SurfaceFlow_PackGPU(sf, t, packed, meta);

  // vec4 arrays only — see the note in UVDeform_Apply.
  if (locs->layerLoc != -1)
    SetShaderValueV(shader, locs->layerLoc, packed, SHADER_UNIFORM_VEC4,
                    SURFACE_FLOW_MAX_LAYERS * 2);
  if (locs->metaLoc != -1)
    SetShaderValue(shader, locs->metaLoc, meta, SHADER_UNIFORM_VEC4);
  if (locs->twoPhaseLoc != -1) {
    float tp[4] = {sf->twoPhaseSpeed, sf->twoPhaseStrength,
                   sf->twoPhase ? 1.0f : 0.0f, 0.0f};
    SetShaderValue(shader, locs->twoPhaseLoc, tp, SHADER_UNIFORM_VEC4);
  }
}
