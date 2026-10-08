#ifndef CORE_EMISSION_EVENTS_H
#define CORE_EMISSION_EVENTS_H
#include "raylib.h"
#include <stdint.h>
/* Component reports facts; emitter policy selects templates and birth counts.
 * CPU delivery is synchronous. This record is not a GPU/wire ABI. */
typedef enum {
    EMISSION_EVENT_LIVE, EMISSION_EVENT_DEATH,
    EMISSION_EVENT_COLLISION, EMISSION_EVENT_ARRIVAL
} EmissionEventKind;
typedef struct {
    EmissionEventKind kind;
    Vector3 position, velocity, normal;
    Vector3 stepDisplacement; /* Legacy live sampling interval, in meters. */
    float dt;
} EmissionEvent;
#endif
