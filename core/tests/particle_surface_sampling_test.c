#include "core/particles/particle_surface_sampling.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { bad++; printf("FAIL: %s\n", #x); } } while (0)
int main(void)
{
    int bad = 0;
    CHECK(ParticleSurfaceSampling_Ordinal(0,0,1280) == -1);
    CHECK(ParticleSurfaceSampling_Ordinal(0,1,320) == 160);
    CHECK(ParticleSurfaceSampling_Ordinal(0,96,320) == 0);
    CHECK(ParticleSurfaceSampling_Ordinal(95,96,320) == 319);
    int previous = -1;
    for (int i = 0; i < 96; ++i) {
        int next = ParticleSurfaceSampling_Ordinal(i,96,320);
        CHECK(next > previous && next < 320);
        previous = next;
    }
    for (int i = 0; i < 96; ++i)
        CHECK(ParticleSurfaceSampling_Ordinal(i,96,96) == i);
    printf("surface sample spacing: %s\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}
