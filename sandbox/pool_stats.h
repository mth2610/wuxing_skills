#ifndef SANDBOX_POOL_STATS_H
#define SANDBOX_POOL_STATS_H

// Item 32: Pool stats overlay — press and hold F3 to show all system pools.
// Red row = pool hit max this session (at least one drop occurred).

void PoolStats_Init(void);
void PoolStats_DrawOverlay(void);  // call in 2D overlay section (after EndMode3D)

#endif // SANDBOX_POOL_STATS_H
