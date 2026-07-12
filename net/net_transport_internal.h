// net/net_transport_internal.h — seam between the transport core
// (net_transport.c: host sim / snapshots / intents) and a pluggable packet
// backend (net_eos.c). The ENet path stays built into net_transport.c; a
// backend REPLACES it for the session (online play). net/-internal only —
// no other module includes this.
#ifndef NET_TRANSPORT_INTERNAL_H
#define NET_TRANSPORT_INTERNAL_H

#include "net/net_transport.h"

// Deliver a whole logical packet (already de-fragmented) to the core.
// Channel is NET_CH_STREAM / NET_CH_CONTROL semantics from net_transport.c.
void NetTransport_BackendPacket(int channel, const unsigned char *data, int len);

// Peer lifecycle — same effects as the ENet CONNECT/DISCONNECT events
// (host spawns/kills the remote hero, client resets its mirror state).
void NetTransport_BackendConnected(void);
void NetTransport_BackendDisconnected(void);

// Install a backend and set the role. send() must accept packets up to a
// full agent snapshot (the backend fragments if its MTU is smaller);
// tick() pumps the backend once per frame (before the core's send phase);
// stop() tears the session down (Net_Stop calls it, then clears the hooks).
typedef bool (*NetBackendSendFn)(int channel, const void *data, int len, bool reliable);
typedef void (*NetBackendTickFn)(float dt);
typedef void (*NetBackendStopFn)(void);
void NetTransport_SetBackend(NetMode mode, NetBackendSendFn send,
                             NetBackendTickFn tick, NetBackendStopFn stop);

#endif // NET_TRANSPORT_INTERNAL_H
