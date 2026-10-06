/* Legacy acceleration/damping and packed enum values remain independent of
 * the new typed Newton laws. Executes the production CPU evaluator/packer. */
#include "core/force_field.c"
#include "core/particles/particle_dynamics.h"
#include <stdio.h>

int main(void)
{
    const ForceType types[] = {FORCE_GRAVITY_DIR, FORCE_GRAVITY_POINT,
        FORCE_VORTEX, FORCE_WIND, FORCE_NOISE_PERLIN, FORCE_NOISE_CURL,
        FORCE_DRAG, FORCE_VISCOSITY, FORCE_RADIAL_AXIS, FORCE_VORTEX_AXIS,
        FORCE_VECTOR_TEXTURE, FORCE_RECEIVER_PLANE};
    for (int i=0;i<12;++i) {
        ForceField field={.layerCount=1,.layers={{.type=types[i],.strength=2}}};
        ForceFieldGPU packed;
        ForceField_PackGPU(&field,(Vector3){0},(Vector3){0,1,0},&packed);
        if ((int)types[i]!=i || packed.layerCount!=1 ||
            packed.layers[0].params0.w!=i || packed.layers[0].params0.x!=2)
            return 1;
    }
    ForceField gravity={.layerCount=1,.layers={{.type=FORCE_GRAVITY_DIR,
        .direction={0,-1,0},.strength=9.81f}}};
    Vector3 acceleration=ForceField_Evaluate(&gravity,(Vector3){0},
        (Vector3){0},0,(Vector3){0},(Vector3){0});
    Vector3 light=ParticleDynamics_ApplyAccelerationAndForce((Vector3){0},
        acceleration,(Vector3){0},100,0.1f);
    Vector3 heavy=ParticleDynamics_ApplyAccelerationAndForce((Vector3){0},
        acceleration,(Vector3){0},1,0.1f);
    if (fabsf(light.y+0.981f)>1e-6f || light.y!=heavy.y) return 1;
    ForceField damping={.layerCount=1,.layers={{.type=FORCE_VISCOSITY,.strength=2}}};
    float whole=ForceField_GetViscosityDamping(&damping,0.5f);
    float half=ForceField_GetViscosityDamping(&damping,0.25f);
    if (fabsf(whole-expf(-1))>1e-6f || fabsf(whole-half*half)>1e-6f) return 1;
    puts("PASS: legacy acceleration units, exponential damping, and all packed numeric IDs");
    return 0;
}
