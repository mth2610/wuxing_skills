#ifndef WUXING_PARTICLE_FIELD_REFERENCE_H
#define WUXING_PARTICLE_FIELD_REFERENCE_H
#include <stdbool.h>

/* Borrowed pointer identity only; storage may be reclaimed after no active
 * particle holds either its primary field or its future arrival field. */
static inline bool ParticleFieldReference_Matches(bool active, const void *field,
                                                 const void *primary,
                                                 const void *arrival)
{
    return active && field && (field == primary || field == arrival);
}
#endif
