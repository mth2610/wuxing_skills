#ifndef UV_FX_H
#define UV_FX_H

#include "core/uv/surface_flow.h"
#include "core/uv/uv_deform.h"

// -----------------------------------------------------------------------------
// UV FX — bind a deform field and a surface flow in one call.
//
// The two halves are always used together (warp the coordinate, then sample
// it), so a consumer should not have to know they are two modules. This is the
// whole seam:
//
//   // once, after loading the shader
//   UVFxLocs locs = UVFx_CacheLocations(shader);
//
//   // per frame
//   BeginShaderMode(shader);
//   UVFx_Apply(&field, &flow, shader, &locs, (float)GetTime());
//   <draw mesh>
//   EndShaderMode();
//
// Header-only: it owns no state, and both halves already guard every uniform
// write on a -1 location, so binding a field to a shader that declares only
// one of the two blocks is legal and silent by design.
//
// GLSL side: #include "core/uv/shaders/uv_field.glsl".
// -----------------------------------------------------------------------------

typedef struct {
  UVDeformLocs deform;
  SurfaceFlowLocs flow;
} UVFxLocs;

static inline UVFxLocs UVFx_CacheLocations(Shader shader) {
  UVFxLocs locs;
  locs.deform = UVDeform_CacheLocations(shader);
  locs.flow = SurfaceFlow_CacheLocations(shader);
  return locs;
}

// MUST be called after BeginShaderMode(shader) for that same shader: under
// rlvk the shader argument only supplies the location, while the destination
// is whichever shader is currently bound.
//
// Either pointer may be NULL — a surface that warps but does not sample, or
// samples but does not warp, is a normal case, not an error.
static inline void UVFx_Apply(const UVDeformField *field, const SurfaceFlow *flow,
                              Shader shader, const UVFxLocs *locs, float t) {
  if (!locs) return;
  if (field) UVDeform_Apply(field, shader, &locs->deform);
  if (flow) SurfaceFlow_Apply(flow, shader, &locs->flow, t);
}

// A field and the flow that samples it describe the SAME asset, so their
// stretchUV must agree: one half tiling a sheet the other half is stretching
// produces a rope with no silhouette on one axis and a correct shape on the
// other. Call this after building both.
static inline void UVFx_SyncStretch(UVDeformField *field, SurfaceFlow *flow,
                                    bool stretchUV) {
  if (field) field->stretchUV = stretchUV;
  if (flow) flow->stretchUV = stretchUV;
}

#endif // UV_FX_H
