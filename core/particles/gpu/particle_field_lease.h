#ifndef WUXING_PARTICLE_FIELD_LEASE_H
#define WUXING_PARTICLE_FIELD_LEASE_H
#include <stddef.h>
#include <stdbool.h>

typedef struct GpuParticleFieldLease {
    const void *field;
    float remaining;
} GpuParticleFieldLease;

/* Stable slots: never compact while particle buffers contain slot indices.
 * A shared field keeps the maximum remaining lifetime of all spawned users.
 * Arrival fields are leased at spawn, before they become active on the GPU.
 * Zero lifetime means lookup only (used by the path packer). */
static inline int GpuParticleFieldLease_Register(GpuParticleFieldLease *leases,
                                                int capacity, const void *field,
                                                float lifetime)
{
    if (!leases || !field || capacity <= 0) return -1;
    int freeSlot = -1;
    for (int i = 0; i < capacity; ++i) {
        if (leases[i].field == field) {
            if (lifetime > leases[i].remaining) leases[i].remaining = lifetime;
            return i;
        }
        if (!leases[i].field && freeSlot < 0) freeSlot = i;
    }
    if (freeSlot < 0 || !(lifetime > 0.0f)) return -1;
    leases[freeSlot] = (GpuParticleFieldLease){field, lifetime};
    return freeSlot;
}

/* Uses the same float subtraction and dt as CPU/GPU life. Compute decrements
 * life and rejects expired particles before consulting force slots, so clearing
 * an expired lease before packing cannot remove a still-live user's force. */
static inline void GpuParticleFieldLease_Advance(GpuParticleFieldLease *leases,
                                                int capacity, float dt)
{
    if (!leases || capacity <= 0 || !(dt > 0.0f)) return;
    for (int i = 0; i < capacity; ++i) {
        if (!leases[i].field) continue;
        leases[i].remaining -= dt;
        if (leases[i].remaining <= 0.0f)
            leases[i] = (GpuParticleFieldLease){NULL, 0.0f};
    }
}

/* Returns the Tight Upper Bound (1 + highest active slot index), or 0 if empty.
 * Allows callers to shrink the active SSBO upload span when trailing leases expire. */
static inline int GpuParticleFieldLease_ActiveSpan(const GpuParticleFieldLease *leases,
                                                  int capacity)
{
    if (!leases || capacity <= 0) return 0;
    for (int i = capacity - 1; i >= 0; --i) {
        if (leases[i].field && leases[i].remaining > 0.0f)
            return i + 1;
    }
    return 0;
}

/* Pointer identity only; never dereference caller-owned storage. */
static inline bool GpuParticleFieldLease_InUse(const GpuParticleFieldLease *leases,
                                              int capacity, const void *field)
{
    if (!leases || !field || capacity <= 0) return false;
    for (int i = 0; i < capacity; ++i)
        if (leases[i].field == field && leases[i].remaining > 0.0f) return true;
    return false;
}

#endif
