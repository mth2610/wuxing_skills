#include "core/particles/particle_plane.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); bad++; } } while (0)
int main(void)
{
    int bad = 0;
    Vector3 p = {2,-10,3}, v = {4,-100,7};
    CHECK(ParticlePlane_Resolve((Vector3){0}, (Vector3){0,2,0}, .1f, .25f, 1, &p, &v));
    CHECK(fabsf(p.y-.1f)<1e-5f && v.x==4 && v.z==7 && v.y==25);
    p=(Vector3){-1,2,3}; v=(Vector3){-6,4,2};
    CHECK(ParticlePlane_Resolve((Vector3){0}, (Vector3){1,0,0}, .2f, 0, 1, &p, &v));
    CHECK(fabsf(p.x-.2f)<1e-5f && v.x==0 && v.y==4 && v.z==2);
    p=(Vector3){0,-1,-1}; v=(Vector3){0,-2,-2};
    CHECK(ParticlePlane_Resolve((Vector3){0}, (Vector3){0,1,1}, .1f, 0, 1, &p, &v));
    CHECK(fabsf((p.y+p.z)/sqrtf(2)-.1f)<1e-5f);
    CHECK(fabsf(v.y)+fabsf(v.z)<1e-5f);
    p=(Vector3){0,2,0}; v=(Vector3){0,-1,0};
    CHECK(!ParticlePlane_Resolve((Vector3){0}, (Vector3){0,1,0}, .1f, 0, 1, &p, &v));
    CHECK(p.y==2 && v.y==-1);
    p=(Vector3){0,-1,0}; v=(Vector3){2,3,4};
    CHECK(ParticlePlane_Resolve((Vector3){0}, (Vector3){0,1,0}, .1f, 0, 0, &p, &v));
    CHECK(v.x==2 && v.y==3 && v.z==4); /* Separating velocity must survive. */
    CHECK(!ParticlePlane_Resolve((Vector3){0}, (Vector3){0}, .1f, 0, 1, &p, &v));
    printf("fluid receiver: %s\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}
