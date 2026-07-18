#include "visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "core/particle_system.h"
#include "core/decal_system.h"
#include "core/vfx_light.h"
#include "core/trail_system.h"
#include "core/camera_fx.h"
#include "core/camera_context.h"
#include "environment/environment_system.h"
#include "core/ribbon_strip.h"
#include "core/path_spline.h"
#include "core/screen_distort.h"
#include "core/time_fx.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "core/geometry/mesh_cache.h"
#include "core/material/material_system.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "core/vfx_proc_ray.h"
#include "core/emitter_system.h"
#include "core/color_gradient.h"
#include "core/utils_math.h"
#include "core/mesh_adjacency.h"
#include "rlgl.h"
#include "raymath.h"
#include <stdlib.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#ifndef PI
#define PI 3.1415926535f
#endif

// Include modular visual composer implementations (grouped by element/functionality)
#include "common/common.inl"

#include "metal/metal.inl"
#include "wood/wood.inl"
#include "water/water.inl"
#include "fire/fire.inl"
#include "earth/earth.inl"
#include "plasma/plasma.inl"
#include "taiji/taiji.inl"

#include "vc_archetype.inl"

void VFX_Compose_Update(float dt) { VC_Archetype_Update(dt); }
void VFX_Compose_Draw3D(Camera3D cam) { VC_Archetype_Draw3D(cam); }