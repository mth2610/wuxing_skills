#ifndef CORE_FLUID_COMPAT_FLUID_PBD_H
#define CORE_FLUID_COMPAT_FLUID_PBD_H

/* Legacy source compatibility. New code includes core/liquid/liquid_pbd.h. */
#include "core/liquid/liquid_pbd.h"

#define FLUID_PBD_MAX_PARTICLES LIQUID_PBD_MAX_PARTICLES
#define FluidPBDRenderParticle LiquidPBDRenderParticle
#define FluidPBD_GetRenderParticles LiquidPBD_GetRenderParticles
#define FluidPBD_Init LiquidPBD_Init
#define FluidPBD_SpawnImpact LiquidPBD_SpawnImpact
#define FluidPBD_Update LiquidPBD_Update

#endif
