#ifndef CORE_PARTICLES_FIELD_CAPABILITIES_H
#define CORE_PARTICLES_FIELD_CAPABILITIES_H
#include "core/force_field.h"
#include <stdbool.h>
/* CPU has no vector-texture sampler. Reject the complete field rather than
 * silently losing that layer while retaining the other physical components. */
static inline bool ParticleField_CpuSupported(const ForceField *field)
{
    if (!field) return true;
    int count = field->layerCount;
    if (count > FORCE_FIELD_MAX_LAYERS) count = FORCE_FIELD_MAX_LAYERS;
    for (int i = 0; i < count; ++i)
        if (field->layers[i].type == FORCE_VECTOR_TEXTURE) return false;
    return true;
}
#endif
