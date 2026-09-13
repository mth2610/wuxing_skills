#include "core/fluid/fluid_motion.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); bad++; } } while (0)
int main(void)
{
    int bad=0;
    FluidMotionDesc water=FluidMotion_Get(FLUID_MOTION_WATER);
    FluidMotionDesc poison=FluidMotion_Get(FLUID_MOTION_POISON);
    FluidMotionDesc mud=FluidMotion_Get(FLUID_MOTION_MUD);
    FluidMotionDesc lava=FluidMotion_Get(FLUID_MOTION_LAVA);
    FluidMotionDesc metal=FluidMotion_Get(FLUID_MOTION_LIQUID_METAL);
    CHECK(water.splashVelocity>poison.splashVelocity && poison.splashVelocity>lava.splashVelocity);
    CHECK(lava.splashVelocity>mud.splashVelocity);
    CHECK(mud.settleViscosity>lava.settleViscosity && lava.settleViscosity>water.settleViscosity);
    CHECK(metal.gatherStrength>mud.gatherStrength && mud.gatherStrength>water.gatherStrength);
    CHECK(metal.tangentRetention>water.tangentRetention && water.tangentRetention>mud.tangentRetention);
    CHECK(water.turbulence>poison.turbulence && poison.turbulence>lava.turbulence);
    FluidMotionDesc all[5]={water,poison,mud,lava,metal};
    for (int i=0;i<5;++i) {
        CHECK(all[i].impactViscosity>0 && all[i].settleViscosity>0);
        CHECK(all[i].restitution>=0 && all[i].restitution<=1);
        CHECK(all[i].tangentRetention>=0 && all[i].tangentRetention<=1);
        CHECK(all[i].impactDuration>0 && all[i].impactDuration<all[i].lifetime);
    }
    printf("fluid motion profiles: %s\n",bad?"FAIL":"PASS");
    return bad!=0;
}
