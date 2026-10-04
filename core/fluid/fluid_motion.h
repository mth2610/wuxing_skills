#ifndef CORE_FLUID_COMPAT_FLUID_MOTION_H
#define CORE_FLUID_COMPAT_FLUID_MOTION_H

/* Legacy source compatibility. New code includes core/liquid/liquid_motion.h. */
#include "core/liquid/liquid_motion.h"

#define FLUID_MOTION_LAVA LIQUID_MOTION_LAVA
#define FLUID_MOTION_LIQUID_METAL LIQUID_MOTION_LIQUID_METAL
#define FLUID_MOTION_MUD LIQUID_MOTION_MUD
#define FLUID_MOTION_POISON LIQUID_MOTION_POISON
#define FLUID_MOTION_WATER LIQUID_MOTION_WATER
#define FluidMotionDesc LiquidMotionDesc
#define FluidMotionProfile LiquidMotionProfile
#define FluidMotion_Get LiquidMotion_Get

#endif
