#include "core/particles/particle_field_capabilities.h"
#include <stdio.h>
int main(void)
{
    ForceField field = {.layerCount=2, .layers={{.type=FORCE_GRAVITY_DIR},
        {.type=FORCE_VECTOR_TEXTURE}}};
    if (ParticleField_CpuSupported(&field)) return 1;
    field.layers[1].type=FORCE_VISCOSITY;
    if (!ParticleField_CpuSupported(&field) || !ParticleField_CpuSupported(NULL)) return 1;
    puts("CPU rejects entire vector-texture field; legacy procedural layers supported");
    return 0;
}
