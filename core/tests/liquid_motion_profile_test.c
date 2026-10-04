#include "core/liquid/liquid_motion.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); bad++; } } while (0)
int main(void)
{
    int bad=0;
    LiquidMotionDesc water=LiquidMotion_Get(LIQUID_MOTION_WATER);
    LiquidMotionDesc poison=LiquidMotion_Get(LIQUID_MOTION_POISON);
    LiquidMotionDesc mud=LiquidMotion_Get(LIQUID_MOTION_MUD);
    LiquidMotionDesc lava=LiquidMotion_Get(LIQUID_MOTION_LAVA);
    LiquidMotionDesc metal=LiquidMotion_Get(LIQUID_MOTION_LIQUID_METAL);
    CHECK(water.splashVelocity>poison.splashVelocity && poison.splashVelocity>lava.splashVelocity);
    CHECK(lava.splashVelocity>mud.splashVelocity);
    CHECK(mud.settleViscosity>lava.settleViscosity && lava.settleViscosity>water.settleViscosity);
    CHECK(metal.gatherStrength>mud.gatherStrength && mud.gatherStrength>water.gatherStrength);
    CHECK(metal.tangentRetention>water.tangentRetention && water.tangentRetention>mud.tangentRetention);
    CHECK(water.turbulence>poison.turbulence && poison.turbulence>lava.turbulence);
    LiquidMotionDesc all[5]={water,poison,mud,lava,metal};
    for (int i=0;i<5;++i) {
        CHECK(all[i].impactViscosity>0 && all[i].settleViscosity>0);
        CHECK(all[i].restitution>=0 && all[i].restitution<=1);
        CHECK(all[i].tangentRetention>=0 && all[i].tangentRetention<=1);
        CHECK(all[i].impactDuration>0 && all[i].impactDuration<all[i].lifetime);
    }
    printf("fluid motion profiles: %s\n",bad?"FAIL":"PASS");
    return bad!=0;
}
