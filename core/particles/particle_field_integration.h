#ifndef CORE_PARTICLES_FIELD_INTEGRATION_H
#define CORE_PARTICLES_FIELD_INTEGRATION_H
#include "core/motion/motion_body.h"
/* Independent typed media require the material receiver integration even when
 * no explicit drag law is authored. An empty sample retains the legacy path. */
static inline bool ParticleField_UsesSharedIntegration(const FieldSample *sample)
{
    return sample && (sample->hasDragForce ||
        (sample->mediumIsAbsolute && sample->mediumWeight > 0));
}
#endif
