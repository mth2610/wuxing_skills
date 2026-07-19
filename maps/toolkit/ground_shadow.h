#ifndef GROUND_SHADOW_H
#define GROUND_SHADOW_H

// Real Shading P6 — opt-in shadow receiver for raw immediate-mode ground
// draws (rlBegin(RL_TRIANGLES)/rlVertex3f/rlColor4ub, e.g. default_arena.c's
// floor plate + zone discs, verdant_path.c's zone discs). Those draws never
// go through a Model/Material, so BeginShaderMode is the only way to get
// them onto a shader that can sample the shadow map.
//
// Usage — wrap the existing draw block, no other changes needed:
//   GroundShadow_Begin();
//   rlBegin(RL_TRIANGLES); ... rlEnd();
//   GroundShadow_End();
//
// No-op visually when EnvShadow is disabled (shadow factor forced to 1.0 —
// same vertex-color gradient as before this existed).

void GroundShadow_UpdateFrame(void); // once per frame (main.c, alongside SurfaceMaterial_UpdateFrame)
void GroundShadow_Begin(void);
void GroundShadow_End(void);

#endif // GROUND_SHADOW_H
