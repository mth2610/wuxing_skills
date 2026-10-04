#include <stdlib.h>
#include <string.h>

// Liquid Impact bench fixture. This deliberately calls the public event API
// directly so the tester isolates droplet collision/residue from the generic
// impact package's light, distortion and smoke beats.
void VFX_ComposeLiquidImpact(Vector3 pos)
{
    const VFX_ElementMaterial *water = VFX_Material(VC_MAT_WATER);
    LiquidImpactEvent event = {
        .hitPoint = pos,
        .hitNormal = (Vector3){0.0f, 1.0f, 0.0f},
        .impulseDirection = Vector3Normalize((Vector3){0.35f, 1.0f, 0.20f}),
        .initialVelocity = (Vector3){1.4f, -6.5f, 0.8f},
        .force01 = 1.0f,
        .scale = 1.15f,
        .bodyColor = water->body,
        .glowColor = water->glow,
        .softColor = water->soft
    };
    const char *backendOverride=getenv("WUXING_LIQUID_IMPACT_BACKEND");
    if (backendOverride && strcmp(backendOverride,"pbd")==0)
        event.backend=LIQUID_IMPACT_BACKEND_PBD;
    int count=1;
    const char *countOverride=getenv("WUXING_LIQUID_IMPACT_COUNT");
    if (countOverride) {
        int value=atoi(countOverride);
        if (value==1 || value==2 || value==4) count=value;
    }
    for (int i=0;i<count;++i) {
        event.hitPoint=Vector3Add(pos,(Vector3){
            ((float)i-0.5f*(float)(count-1))*1.65f,0.0f,0.0f});
        LiquidImpact_SpawnWater(&event);
    }
}
