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
#include "core/geometry/crystal_mesh_generator.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "core/vfx_proc_ray.h"
#include "core/color_gradient.h"
#include "core/utils_math.h"
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
#include "vc_common.inl"
#include "vc_metal.inl"
#include "vc_wood.inl"
#include "vc_water.inl"
#include "vc_fire.inl"
#include "vc_earth.inl"
#include "vc_plasma.inl"

// Include high-level general VFX system implementations
#include "vc_projectile.inl"
#include "vc_ground.inl"
#include "vc_beam.inl"
#include "vc_path.inl"
#include "vc_summon.inl"
#include "vc_explosion.inl"
#include "vc_aura.inl"

// Phase 3 archetypes
#include "vc_shield.inl"
#include "vc_chain.inl"
#include "vc_zone.inl"
#include "vc_slash.inl"
#include "vc_charge.inl"