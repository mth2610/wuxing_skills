#ifndef CORE_VC_EMISSION_H
#define CORE_VC_EMISSION_H
#include <stdbool.h>
#include <math.h>

/* Emission scheduling owns no field or body. Duration is seconds and rate is
 * particles/second; the caller owns its source, emitter and rendered bodies.
 * A bounded call retains overflow so a long frame does not discard particles. */
typedef struct VFX_EmissionSchedule {
    double ageSeconds, pendingCount;
    float durationSeconds, ratePerSecond;
} VFX_EmissionSchedule;

static inline int VFX_EmissionAdvance(VFX_EmissionSchedule *schedule,
                                     float dt, int maximumCount)
{
    if (!schedule || !isfinite(dt) || dt <= 0 || maximumCount <= 0 ||
        !isfinite(schedule->durationSeconds) || schedule->durationSeconds < 0 ||
        !isfinite(schedule->ratePerSecond) || schedule->ratePerSecond < 0)
        return 0;
    double remaining = fmax(0.0, schedule->durationSeconds - schedule->ageSeconds);
    double emitTime = fmin(dt, remaining);
    schedule->ageSeconds += dt;
    schedule->pendingCount += emitTime * schedule->ratePerSecond;
    int count = (int)fmin(floor(schedule->pendingCount + 1e-9), maximumCount);
    schedule->pendingCount = fmax(0.0, schedule->pendingCount - count);
    return count;
}

static inline bool VFX_EmissionComplete(const VFX_EmissionSchedule *schedule)
{
    return !schedule || (schedule->ageSeconds >= schedule->durationSeconds &&
                         schedule->pendingCount < 1.0 - 1e-9);
}
#endif
