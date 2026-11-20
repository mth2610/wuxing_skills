// net/net_prediction.h
// Client-side movement prediction (0ms input feel) + 60 FPS smooth snapshot
// interpolation for remote heroes, monsters, and bosses.
#ifndef NET_PREDICTION_H
#define NET_PREDICTION_H

#include "control/control.h"
#include "entities/entities.h"
#include "net/net.h"

void NetPred_Reset(void);

// Feeds a newly unpacked 20Hz snapshot from the host into the interpolation buffer.
void NetPred_OnSnapshot(const NetAgentState *states, int count, int localHeroId);

// Triggers immediate client-side jump/dash edges on the local hero so Khinh Công
// and dodge launch cleanly at 60 FPS without waiting for a host snapshot.
void NetPred_OnLocalIntent(int localHeroId, const PlayerIntent *in);

// Applies immediate client-side movement preview on the local hero so WASD
// feels instantaneous even over high-latency P2P relays.
void NetPred_ApplyLocalPreview(int localHeroId, const PlayerIntent *in, float dt);

// Smoothly interpolates all mirrored agents toward their latest host targets at 60 FPS.
void NetPred_Tick(float dt, int localHeroId);

// Returns true while the client's local predicted Khinh Công jump is airborne.
bool NetPred_IsLocalAirborne(float *outVelY);

// Returns true while the client's local predicted dash burst (i-frame) is active.
bool NetPred_IsLocalDashing(void);

// Consumes 1 (first jump) or 2 (2nd air-jump somersault) for client jump animation.
int NetPred_ConsumeLocalJumpFired(void);

#endif // NET_PREDICTION_H
