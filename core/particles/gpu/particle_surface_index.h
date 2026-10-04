#ifndef WUXING_PARTICLE_SURFACE_INDEX_H
#define WUXING_PARTICLE_SURFACE_INDEX_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define GPU_SURFACE_MAX_STREAMS 32

typedef struct GpuSurfaceRoute {
    int emitterId;
    float materialId;
} GpuSurfaceRoute;

/* std430 mirror: a uint followed by a float, stride 8 bytes. */
typedef struct GpuSurfaceIndex {
    uint32_t particleIndex;
    float materialId;
} GpuSurfaceIndex;

typedef struct GpuSurfaceRouting {
    float emitterId, renderMode;
} GpuSurfaceRouting;

static inline GpuSurfaceRouting GpuSurfaceRouting_Read(const void *slot,
                                                      size_t emitterOffset,
                                                      size_t modeOffset)
{
    GpuSurfaceRouting routing;
    memcpy(&routing.emitterId, (const unsigned char *)slot + emitterOffset, sizeof(float));
    memcpy(&routing.renderMode, (const unsigned char *)slot + modeOffset, sizeof(float));
    return routing;
}

/* Routing metadata is written at spawn and remains valid until ring overwrite.
 * Deliberately do not inspect CPU active/life: GPU arrival may differ from the
 * event shadow. GPU vertex/fragment stages retain authoritative life rejection.
 * Duplicate routes select the first material, preserving a single splat/slot.
 * Returns -1 for invalid input or insufficient output capacity; never truncates. */
static inline int GpuSurfaceIndex_Build(const void *particles, int particleCount,
                                       size_t stride, size_t emitterOffset,
                                       size_t modeOffset,
                                       const GpuSurfaceRoute *routes, int routeCount,
                                       GpuSurfaceIndex *output, int capacity)
{
    if (!particles || !routes || !output || particleCount < 0 || capacity < 0 ||
        routeCount < 0 || routeCount > GPU_SURFACE_MAX_STREAMS ||
        emitterOffset + sizeof(float) > stride || modeOffset + sizeof(float) > stride)
        return -1;
    int count = 0;
    for (int i = 0; i < particleCount; ++i) {
        const unsigned char *slot = (const unsigned char *)particles + (size_t)i * stride;
        GpuSurfaceRouting routing = GpuSurfaceRouting_Read(slot, emitterOffset, modeOffset);
        if (routing.renderMode != 3.0f) continue;
        for (int r = 0; r < routeCount; ++r) {
            if (routing.emitterId != (float)routes[r].emitterId) continue;
            if (count >= capacity) return -1;
            output[count++] = (GpuSurfaceIndex){(uint32_t)i, routes[r].materialId};
            break;
        }
    }
    return count;
}

#endif
