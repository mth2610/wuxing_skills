// net/net_transport_internal.h — seam between the transport core
// (net_transport.c: host sim / snapshots / intents / roster) and a pluggable
// packet backend (net_eos.c). The ENet path stays built into net_transport.c;
// a backend REPLACES it for the session (online play). net/-internal only —
// no other module includes this.
//
// Multi-peer (protocol v3): every callback carries an opaque peerRef
// identifying the remote player — the backend's stable per-peer handle
// (EOS: the ProductUserId). The core maps peerRef → its HostPeer slot.
// CLIENT role has exactly one link (the host): pass peerRef = NULL.
#ifndef NET_TRANSPORT_INTERNAL_H
#define NET_TRANSPORT_INTERNAL_H

#include "net/net_transport.h"

// Deliver a whole logical packet (already de-fragmented) to the core.
// Channel is NET_CH_STREAM / NET_CH_CONTROL semantics from net_transport.c.
void NetTransport_BackendPacket(void *peerRef, int channel,
                                const unsigned char *data, int len);

// Peer lifecycle — same effects as the ENet CONNECT/DISCONNECT events
// (host spawns/kills that peer's hero + rebroadcasts the roster, client
// resets its mirror state). A host with no free slot ignores the connect.
void NetTransport_BackendConnected(void *peerRef);
void NetTransport_BackendDisconnected(void *peerRef);

// Install a backend and set the role. send() targets one peer (peerRef,
// NULL = the host link on CLIENT role) — the core loops for broadcasts;
// packets up to a full agent snapshot (the backend fragments if its MTU is
// smaller). tick() pumps the backend once per frame (before the core's send
// phase); stop() tears the session down (Net_Stop calls it, then clears the
// hooks).
typedef bool (*NetBackendSendFn)(void *peerRef, int channel,
                                 const void *data, int len, bool reliable);
typedef void (*NetBackendTickFn)(float dt);
typedef void (*NetBackendStopFn)(void);
void NetTransport_SetBackend(NetMode mode, NetBackendSendFn send,
                             NetBackendTickFn tick, NetBackendStopFn stop);

#endif // NET_TRANSPORT_INTERNAL_H
