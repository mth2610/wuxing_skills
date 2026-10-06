#include "core/composition/wood/vc_wood_botanical_profile.h"
#include <stdio.h>

static int failures;
#define CHECK(c,m) do { if (!(c)) { printf("FAIL: %s\n",m); failures++; } else printf("PASS: %s\n",m); } while (0)

static float ProjectedTriangleArea(Vector3 a,Vector3 b,Vector3 c)
{
    return .5f*fabsf((b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x));
}

int main(void)
{
    BotanicalProfile profiles[]={Botanical_ProfileOval(),Botanical_ProfileWillow(),
        Botanical_DetachedPetalProfile(0),Botanical_DetachedPetalProfile(1),
        Botanical_DetachedPetalProfile(2)};
    for(int shape=0;shape<5;shape++) {
        const BotanicalProfile *profile=&profiles[shape];
        float length=shape<2?.08f:.014f, area=0;
        int finite=1, normalized=1;
        for(int i=0;i<=BOTANICAL_BLADE_SEGMENTS;i++) {
            float t=Botanical_BladeSampleT(i);
            for(int j=0;j<=BOTANICAL_BLADE_STRIPS;j++) {
                float across=-1+2.0f*j/BOTANICAL_BLADE_STRIPS;
                Vector3 point=Botanical_BladeLocalPoint(profile,length,t,across,.15f,.1f,.018f);
                Vector3 normal=Botanical_BladeLocalNormal(profile,length,t,across,.15f,.1f,.018f);
                finite &= isfinite(point.x)&&isfinite(point.y)&&isfinite(point.z)&&
                          isfinite(normal.x)&&isfinite(normal.y)&&isfinite(normal.z);
                normalized &= fabsf(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z-1)<1e-5f;
                if(i==BOTANICAL_BLADE_SEGMENTS||j==BOTANICAL_BLADE_STRIPS) continue;
                float nextT=Botanical_BladeSampleT(i+1);
                float nextAcross=-1+2.0f*(j+1)/BOTANICAL_BLADE_STRIPS;
                Vector3 next=Botanical_BladeLocalPoint(profile,length,nextT,across,.15f,.1f,.018f);
                Vector3 diagonal=Botanical_BladeLocalPoint(profile,length,nextT,nextAcross,.15f,.1f,.018f);
                Vector3 side=Botanical_BladeLocalPoint(profile,length,t,nextAcross,.15f,.1f,.018f);
                area+=ProjectedTriangleArea(point,next,diagonal)+ProjectedTriangleArea(point,diagonal,side);
            }
        }
        CHECK(finite&&normalized,"all blade vertices and endpoint normals are finite and normalized");
        CHECK(fabsf(area-Botanical_BladePlanformArea(profile,length))<area*1e-5f,
              "rendered triangles match physical broadside area despite cup and asymmetry");
        Vector3 root=Botanical_BladeLocalPoint(profile,length,0,1,.15f,.1f,.018f);
        Vector3 tip=Botanical_BladeLocalPoint(profile,length,1,1,.15f,.1f,.018f);
        CHECK(root.x==0&&root.z==0&&fabsf(tip.x)<1e-8f&&tip.z==length,
              "soft geometry retains closed endpoints and calibrated blade length");
    }
    return failures?1:0;
}
