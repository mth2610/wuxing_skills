// net/net_eos.c — Epic Online Services backend (compiled with -DWUXING_EOS=ON,
// otherwise net_eos_stub.c). Internet play over Epic's free NAT-punch/relay:
//
//   host:   ./wuxing --host-online          → prints a 5-char join code
//   client: ./wuxing --join-online <code>
//
// Flow: Device ID auth (anonymous, no Epic account) → host creates a public
// lobby carrying the code as a searchable attribute → client finds it by
// code, reads the owner's ProductUserId, and talks P2P. Packets ride the
// transport core (net_transport_internal.h): this file only moves bytes —
// EOS's 1170-byte MTU is smaller than a full agent snapshot, so logical
// packets are split with a 3-byte [seq|idx|count] header and reassembled.
//
// Keys live in eos_keys.cfg (gitignored — EOS_SETUP.md), read from the
// working directory. Setup calls (login, lobby ops) block the main thread
// with a bounded pump — they happen at CLI startup, not mid-match.
#include "net/net_transport_internal.h"
#include "raylib.h" // TraceLog only

#include "eos_sdk.h"
#include "eos_init.h"
#include "eos_logging.h"
#include "eos_connect.h"
#include "eos_lobby.h"
#include "eos_p2p.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h> // CFRunLoopRunInMode — see PumpUntilDone
#endif

#define EOS_KEYS_FILE      "eos_keys.cfg"
#define EOS_BUCKET_ID      "wuxing:duel:v1"
#define EOS_JOINCODE_KEY   "JOINCODE"
#define EOS_JOINCODE_LEN   5
#define EOS_SOCKET_NAME    "WUXING"
#define EOS_SETUP_TIMEOUT  15.0 // seconds per blocking setup step

// --- Keys (never logged, never committed) -----------------------------------
static char s_productId[128], s_sandboxId[128], s_deploymentId[128];
static char s_clientId[128], s_clientSecret[128];

// --- SDK state ---------------------------------------------------------------
static EOS_HPlatform      s_platform = NULL;
static EOS_HConnect       s_connect  = NULL;
static EOS_HLobby         s_lobby    = NULL;
static EOS_HP2P           s_p2p      = NULL;
static EOS_ProductUserId  s_localPuid = NULL;
static EOS_ProductUserId  s_hostPuid  = NULL; // CLIENT: the lobby owner we talk to
static char               s_lobbyId[128] = { 0 };
static bool               s_isHost = false;
static EOS_NotificationId s_notifyRequest     = EOS_INVALID_NOTIFICATIONID;
static EOS_NotificationId s_notifyClosed      = EOS_INVALID_NOTIFICATIONID;
static EOS_NotificationId s_notifyEstablished = EOS_INVALID_NOTIFICATIONID;

static const EOS_P2P_SocketId s_socketId = {
    .ApiVersion = EOS_P2P_SOCKETID_API_LATEST,
    .SocketName = EOS_SOCKET_NAME,
};

// Async-op scratch: setup steps run one at a time, blocking on s_opStatus
// (0 = pending, 1 = ok, -1 = failed).
static volatile int          s_opStatus = 0;
static EOS_ContinuanceToken  s_continuance = NULL;
static EOS_ProductUserId     s_opPuid = NULL;
static char                  s_opLobbyId[128];

// --- Small helpers -----------------------------------------------------------

static double NowSeconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// Pump the SDK until the pending op resolves (bounded). Setup-time only.
static bool PumpUntilDone(const char *what) {
    double start = NowSeconds();
    while (s_opStatus == 0 && NowSeconds() - start < EOS_SETUP_TIMEOUT) {
        EOS_Platform_Tick(s_platform);
#ifdef __APPLE__
        // The SDK's macOS HTTP stack delivers on the main CFRunLoop — a
        // plain sleep starves it and every async op times out.
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
#else
        struct timespec nap = { 0, 10 * 1000 * 1000 }; // 10ms
        nanosleep(&nap, NULL);
#endif
    }
    if (s_opStatus != 1) {
        TraceLog(LOG_WARNING, "[EOS] %s %s", what,
                 s_opStatus == 0 ? "timed out" : "failed");
        return false;
    }
    return true;
}

static bool LoadKeys(void) {
    FILE *f = fopen(EOS_KEYS_FILE, "r");
    if (f == NULL) {
        TraceLog(LOG_WARNING, "[EOS] missing %s — see EOS_SETUP.md", EOS_KEYS_FILE);
        return false;
    }
    char line[512];
    while (fgets(line, sizeof(line), f) != NULL) {
        char key[64], val[256];
        if (sscanf(line, " %63[a-z_] = %255s", key, val) != 2) continue;
        if      (strcmp(key, "product_id")    == 0) strncpy(s_productId,    val, sizeof(s_productId) - 1);
        else if (strcmp(key, "sandbox_id")    == 0) strncpy(s_sandboxId,    val, sizeof(s_sandboxId) - 1);
        else if (strcmp(key, "deployment_id") == 0) strncpy(s_deploymentId, val, sizeof(s_deploymentId) - 1);
        else if (strcmp(key, "client_id")     == 0) strncpy(s_clientId,     val, sizeof(s_clientId) - 1);
        else if (strcmp(key, "client_secret") == 0) strncpy(s_clientSecret, val, sizeof(s_clientSecret) - 1);
    }
    fclose(f);
    if (!s_productId[0] || !s_sandboxId[0] || !s_deploymentId[0] ||
        !s_clientId[0] || !s_clientSecret[0]) {
        TraceLog(LOG_WARNING, "[EOS] %s incomplete — need product/sandbox/deployment/client id + secret",
                 EOS_KEYS_FILE);
        return false;
    }
    return true;
}

static void EOS_CALL OnEosLog(const EOS_LogMessage *msg) {
    TraceLog(msg->Level <= EOS_LOG_Warning ? LOG_WARNING : LOG_INFO,
             "[EOS/%s] %s", msg->Category, msg->Message);
}

// --- Platform + Device ID login (shared by host and client) ------------------

static void EOS_CALL OnCreateDeviceId(const EOS_Connect_CreateDeviceIdCallbackInfo *data) {
    // DuplicateNotAllowed = this machine already has a device id — fine.
    s_opStatus = (data->ResultCode == EOS_Success ||
                  data->ResultCode == EOS_DuplicateNotAllowed) ? 1 : -1;
    if (s_opStatus < 0)
        TraceLog(LOG_WARNING, "[EOS] CreateDeviceId: %s", EOS_EResult_ToString(data->ResultCode));
}

static void EOS_CALL OnDeleteDeviceId(const EOS_Connect_DeleteDeviceIdCallbackInfo *data) {
    s_opStatus = (data->ResultCode == EOS_Success ||
                  data->ResultCode == EOS_NotFound) ? 1 : -1;
}

static void EOS_CALL OnCreateUser(const EOS_Connect_CreateUserCallbackInfo *data) {
    if (data->ResultCode == EOS_Success) { s_opPuid = data->LocalUserId; s_opStatus = 1; }
    else {
        TraceLog(LOG_WARNING, "[EOS] CreateUser: %s", EOS_EResult_ToString(data->ResultCode));
        s_opStatus = -1;
    }
}

static void EOS_CALL OnConnectLogin(const EOS_Connect_LoginCallbackInfo *data) {
    if (data->ResultCode == EOS_Success) {
        s_opPuid = data->LocalUserId;
        s_opStatus = 1;
    } else if (data->ResultCode == EOS_InvalidUser && data->ContinuanceToken != NULL) {
        // Device id exists but no product user yet — CreateUser continues it.
        s_continuance = data->ContinuanceToken;
        s_opStatus = 1;
    } else {
        TraceLog(LOG_WARNING, "[EOS] Connect login: %s", EOS_EResult_ToString(data->ResultCode));
        s_opStatus = -1;
    }
}

static bool EnsureLoggedIn(void) {
    if (s_localPuid != NULL) return true;
    if (!LoadKeys()) return false;

    if (s_platform == NULL) {
        EOS_InitializeOptions init = { 0 };
        init.ApiVersion     = EOS_INITIALIZE_API_LATEST;
        init.ProductName    = "wuxing_skills";
        init.ProductVersion = "0.1";
        EOS_EResult r = EOS_Initialize(&init);
        if (r != EOS_Success && r != EOS_AlreadyConfigured) {
            TraceLog(LOG_WARNING, "[EOS] EOS_Initialize: %s", EOS_EResult_ToString(r));
            return false;
        }
        EOS_Logging_SetCallback(OnEosLog);
        // WUXING_EOS_VERBOSE=1 opens the SDK's own diagnostics when auth or
        // lobby calls fail silently (default keeps the console readable).
        EOS_Logging_SetLogLevel(EOS_LC_ALL_CATEGORIES,
                                getenv("WUXING_EOS_VERBOSE") != NULL
                                    ? EOS_LOG_Verbose : EOS_LOG_Warning);

        EOS_Platform_Options opt = { 0 };
        opt.ApiVersion   = EOS_PLATFORM_OPTIONS_API_LATEST;
        opt.ProductId    = s_productId;
        opt.SandboxId    = s_sandboxId;
        opt.DeploymentId = s_deploymentId;
        opt.ClientCredentials.ClientId     = s_clientId;
        opt.ClientCredentials.ClientSecret = s_clientSecret;
        opt.bIsServer = EOS_FALSE;
        opt.Flags     = EOS_PF_DISABLE_OVERLAY | EOS_PF_DISABLE_SOCIAL_OVERLAY;
        s_platform = EOS_Platform_Create(&opt);
        if (s_platform == NULL) {
            TraceLog(LOG_WARNING, "[EOS] EOS_Platform_Create failed — check eos_keys.cfg values");
            return false;
        }
        s_connect = EOS_Platform_GetConnectInterface(s_platform);
        s_lobby   = EOS_Platform_GetLobbyInterface(s_platform);
        s_p2p     = EOS_Platform_GetP2PInterface(s_platform);

        // Relay control (must be set BEFORE any connection opens). NAT punch
        // fails on many home routers (symmetric/strict NAT), and when it
        // does the two players just never see each other. Epic's relay is
        // free and always reachable, so DEFAULT to forcing it — internet
        // play "just works" at the cost of a few ms. WUXING_EOS_DIRECT=1
        // asks for direct-when-possible (AllowRelays) for LAN/low-latency.
        EOS_P2P_SetRelayControlOptions rc = { 0 };
        rc.ApiVersion   = EOS_P2P_SETRELAYCONTROL_API_LATEST;
        rc.RelayControl = (getenv("WUXING_EOS_DIRECT") != NULL)
                              ? EOS_RC_AllowRelays : EOS_RC_ForceRelays;
        EOS_EResult rcr = EOS_P2P_SetRelayControl(s_p2p, &rc);
        TraceLog(LOG_INFO, "[EOS] relay control = %s (%s)",
                 rc.RelayControl == EOS_RC_ForceRelays ? "FORCE" : "ALLOW",
                 EOS_EResult_ToString(rcr));
    }

    // Dev-only: WUXING_EOS_FRESH_DEVICE=1 discards this machine's device id
    // so the next login mints a NEW anonymous user. Needed to test host+join
    // on ONE machine — lobby search hides lobbies the searcher is already in,
    // and both instances share the keychain device id otherwise.
    if (getenv("WUXING_EOS_FRESH_DEVICE") != NULL) {
        EOS_Connect_DeleteDeviceIdOptions del = { 0 };
        del.ApiVersion = EOS_CONNECT_DELETEDEVICEID_API_LATEST;
        s_opStatus = 0;
        EOS_Connect_DeleteDeviceId(s_connect, &del, NULL, OnDeleteDeviceId);
        if (!PumpUntilDone("DeleteDeviceId")) return false;
    }

    // 1) Make sure this machine has a device id credential.
    EOS_Connect_CreateDeviceIdOptions dev = { 0 };
    dev.ApiVersion  = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
    dev.DeviceModel = "PC";
    s_opStatus = 0;
    EOS_Connect_CreateDeviceId(s_connect, &dev, NULL, OnCreateDeviceId);
    if (!PumpUntilDone("CreateDeviceId")) return false;

    // 2) Log the device id in as an anonymous product user.
    EOS_Connect_Credentials cred = { 0 };
    cred.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    cred.Token      = NULL;
    cred.Type       = EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_UserLoginInfo info = { 0 };
    info.ApiVersion  = EOS_CONNECT_USERLOGININFO_API_LATEST;
    info.DisplayName = "WuxingPlayer";
    EOS_Connect_LoginOptions login = { 0 };
    login.ApiVersion    = EOS_CONNECT_LOGIN_API_LATEST;
    login.Credentials   = &cred;
    login.UserLoginInfo = &info;
    s_opStatus = 0; s_opPuid = NULL; s_continuance = NULL;
    EOS_Connect_Login(s_connect, &login, NULL, OnConnectLogin);
    if (!PumpUntilDone("Connect login")) return false;

    if (s_opPuid == NULL && s_continuance != NULL) { // first run on this machine
        EOS_Connect_CreateUserOptions cu = { 0 };
        cu.ApiVersion       = EOS_CONNECT_CREATEUSER_API_LATEST;
        cu.ContinuanceToken = s_continuance;
        s_opStatus = 0;
        EOS_Connect_CreateUser(s_connect, &cu, NULL, OnCreateUser);
        if (!PumpUntilDone("CreateUser")) return false;
    }
    if (s_opPuid == NULL) return false;
    s_localPuid = s_opPuid;
    TraceLog(LOG_INFO, "[EOS] logged in (Device ID)");
    return true;
}

// --- P2P plumbing: fragmentation over the 1170-byte MTU ----------------------

#define FRAG_HEADER  3                                     // [seq][idx][count]
#define FRAG_PAYLOAD (EOS_P2P_MAX_PACKET_SIZE - FRAG_HEADER)
#define MAX_LOGICAL  (16 * 1024)                           // matches transport buf

static unsigned char s_sendSeq[2]; // per channel

typedef struct {
    unsigned char seq, count;
    unsigned int  haveMask;    // bit per fragment (count ≤ 15 for 16KB)
    int           lastLen;     // payload bytes of the final fragment, -1 until seen
    void         *peerRef;     // whose fragments these are
    unsigned char buf[MAX_LOGICAL];
} Reassembly;
static Reassembly s_reasm[2];

// peerRef: the target ProductUserId (host role); NULL = the lobby owner
// (client role — its only link).
static bool EosSend(void *peerRef, int channel, const void *data, int len, bool reliable) {
    EOS_ProductUserId remote = (peerRef != NULL) ? (EOS_ProductUserId)peerRef : s_hostPuid;
    if (s_localPuid == NULL || remote == NULL || len <= 0 || len > MAX_LOGICAL)
        return false;
    const unsigned char *src = (const unsigned char *)data;
    int count = (len + FRAG_PAYLOAD - 1) / FRAG_PAYLOAD;
    unsigned char seq = s_sendSeq[channel & 1]++;

    EOS_P2P_SendPacketOptions opt = { 0 };
    opt.ApiVersion  = EOS_P2P_SENDPACKET_API_LATEST;
    opt.LocalUserId  = s_localPuid;
    opt.RemoteUserId = remote;
    opt.SocketId     = &s_socketId;
    opt.Channel      = (uint8_t)channel;
    // Queue while the NAT punch is still completing (client's first intents).
    opt.bAllowDelayedDelivery = EOS_TRUE;
    opt.Reliability = reliable ? EOS_PR_ReliableOrdered : EOS_PR_UnreliableUnordered;
    opt.bDisableAutoAcceptConnection = EOS_FALSE;

    for (int i = 0; i < count; i++) {
        int chunk = (i == count - 1) ? len - i * FRAG_PAYLOAD : FRAG_PAYLOAD;
        unsigned char pkt[EOS_P2P_MAX_PACKET_SIZE];
        pkt[0] = seq; pkt[1] = (unsigned char)i; pkt[2] = (unsigned char)count;
        memcpy(pkt + FRAG_HEADER, src + i * FRAG_PAYLOAD, (size_t)chunk);
        opt.Data = pkt;
        opt.DataLengthBytes = (uint32_t)(chunk + FRAG_HEADER);
        if (EOS_P2P_SendPacket(s_p2p, &opt) != EOS_Success) return false;
    }
    return true;
}

static void ReceiveFragment(void *peerRef, int channel,
                            const unsigned char *pkt, int pktLen) {
    if (pktLen <= FRAG_HEADER) return;
    unsigned char seq = pkt[0], idx = pkt[1], count = pkt[2];
    const unsigned char *payload = pkt + FRAG_HEADER;
    int payloadLen = pktLen - FRAG_HEADER;
    if (count == 0 || idx >= count || count > MAX_LOGICAL / FRAG_PAYLOAD + 1) return;

    if (count == 1) { // whole packet — the common case (intents, ctrl, roster)
        NetTransport_BackendPacket(peerRef, channel, payload, payloadLen);
        return;
    }
    // Unordered channel: fragments may arrive in any order — place by index,
    // deliver once every bit is in. A lost fragment just drops the snapshot
    // (the next one is 50ms behind). Only host→client snapshots fragment, so
    // one reassembly slot per channel is enough — but guard against peer mixups.
    Reassembly *r = &s_reasm[channel & 1];
    if (r->seq != seq || r->count != count || r->peerRef != peerRef) {
        r->seq = seq; r->count = count; r->haveMask = 0; r->lastLen = -1;
        r->peerRef = peerRef;
    }
    if (idx * FRAG_PAYLOAD + payloadLen > MAX_LOGICAL) return;
    memcpy(r->buf + idx * FRAG_PAYLOAD, payload, (size_t)payloadLen);
    r->haveMask |= 1u << idx;
    if (idx == count - 1) r->lastLen = payloadLen;
    if (r->lastLen >= 0 && r->haveMask == (1u << count) - 1u) {
        NetTransport_BackendPacket(peerRef, channel,
                                   r->buf, (count - 1) * FRAG_PAYLOAD + r->lastLen);
        r->count = 0; // consumed
    }
}

// --- P2P connection lifecycle -------------------------------------------------

static void EOS_CALL OnConnectionRequest(const EOS_P2P_OnIncomingConnectionRequestInfo *data) {
    if (strcmp(data->SocketId->SocketName, EOS_SOCKET_NAME) != 0) return;
    // Client only talks to the lobby owner; the host accepts anyone — the
    // transport core caps the room at NET_MAX_PLAYERS and ignores overflow.
    if (!s_isHost && data->RemoteUserId != s_hostPuid) return;
    EOS_P2P_AcceptConnectionOptions acc = { 0 };
    acc.ApiVersion   = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    acc.LocalUserId  = s_localPuid;
    acc.RemoteUserId = data->RemoteUserId;
    acc.SocketId     = &s_socketId;
    EOS_EResult ar = EOS_P2P_AcceptConnection(s_p2p, &acc);
    TraceLog(LOG_INFO, "[EOS] P2P connection request (host=%d) accept=%s",
             s_isHost ? 1 : 0, EOS_EResult_ToString(ar));
    if (ar != EOS_Success) return;
    if (s_isHost) // spawns their hero + HELLO + roster (dedupe in the core)
        NetTransport_BackendConnected((void *)data->RemoteUserId);
}

static const char *ClosedReasonStr(EOS_EConnectionClosedReason r) {
    switch (r) {
        case EOS_CCR_ClosedByLocalUser:  return "closed-by-local";
        case EOS_CCR_ClosedByPeer:       return "closed-by-peer";
        case EOS_CCR_TimedOut:           return "TIMED-OUT";
        case EOS_CCR_TooManyConnections: return "too-many-conns";
        case EOS_CCR_InvalidMessage:     return "invalid-message";
        default:                         return "unknown";
    }
}

static void EOS_CALL OnConnectionClosed(const EOS_P2P_OnRemoteConnectionClosedInfo *data) {
    // TIMED-OUT here = the connection never actually established (NAT/relay
    // problem) — the single most useful line when two players can't see
    // each other.
    TraceLog(LOG_WARNING, "[EOS] P2P connection CLOSED reason=%s (host=%d)",
             ClosedReasonStr(data->Reason), s_isHost ? 1 : 0);
    if (s_isHost) {
        NetTransport_BackendDisconnected((void *)data->RemoteUserId);
    } else if (data->RemoteUserId == s_hostPuid) {
        NetTransport_BackendDisconnected(NULL);
    }
}

static void EOS_CALL OnConnectionEstablished(const EOS_P2P_OnPeerConnectionEstablishedInfo *data) {
    // The proof the P2P path actually opened, and HOW (direct vs relay).
    const char *net =
        data->NetworkType == EOS_NCT_DirectConnection  ? "DIRECT" :
        data->NetworkType == EOS_NCT_RelayedConnection ? "RELAY"  : "none";
    TraceLog(LOG_INFO, "[EOS] P2P ESTABLISHED via %s (host=%d, %s)",
             net, s_isHost ? 1 : 0,
             data->ConnectionType == EOS_CET_Reconnection ? "reconnect" : "new");
}

static void EOS_CALL OnNATType(const EOS_P2P_OnQueryNATTypeCompleteInfo *data) {
    const char *t =
        data->NATType == EOS_NAT_Open     ? "OPEN (best)"        :
        data->NATType == EOS_NAT_Moderate ? "MODERATE"           :
        data->NATType == EOS_NAT_Strict   ? "STRICT (needs relay)" : "unknown";
    TraceLog(LOG_INFO, "[EOS] this machine's NAT type = %s (%s)", t,
             EOS_EResult_ToString(data->ResultCode));
}

static void RegisterP2PNotifies(void) {
    EOS_P2P_AddNotifyPeerConnectionRequestOptions req = { 0 };
    req.ApiVersion  = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
    req.LocalUserId = s_localPuid;
    req.SocketId    = &s_socketId;
    s_notifyRequest = EOS_P2P_AddNotifyPeerConnectionRequest(s_p2p, &req, NULL, OnConnectionRequest);

    EOS_P2P_AddNotifyPeerConnectionClosedOptions cls = { 0 };
    cls.ApiVersion  = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
    cls.LocalUserId = s_localPuid;
    cls.SocketId    = &s_socketId;
    s_notifyClosed = EOS_P2P_AddNotifyPeerConnectionClosed(s_p2p, &cls, NULL, OnConnectionClosed);

    EOS_P2P_AddNotifyPeerConnectionEstablishedOptions est = { 0 };
    est.ApiVersion  = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;
    est.LocalUserId = s_localPuid;
    est.SocketId    = &s_socketId;
    s_notifyEstablished = EOS_P2P_AddNotifyPeerConnectionEstablished(s_p2p, &est, NULL, OnConnectionEstablished);

    // Fire-and-forget NAT probe — the result lands a few Ticks later.
    EOS_P2P_QueryNATTypeOptions nat = { 0 };
    nat.ApiVersion = EOS_P2P_QUERYNATTYPE_API_LATEST;
    EOS_P2P_QueryNATType(s_p2p, &nat, NULL, OnNATType);
}

// --- Backend tick/stop (installed into the transport core) -------------------

static void EosTick(float dt) {
    (void)dt;
    EOS_Platform_Tick(s_platform);

    EOS_P2P_ReceivePacketOptions opt = { 0 };
    opt.ApiVersion       = EOS_P2P_RECEIVEPACKET_API_LATEST;
    opt.LocalUserId      = s_localPuid;
    opt.MaxDataSizeBytes = EOS_P2P_MAX_PACKET_SIZE;
    opt.RequestedChannel = NULL;

    for (int budget = 0; budget < 128; budget++) {
        EOS_ProductUserId peer = NULL;
        EOS_P2P_SocketId  sock;
        uint8_t  channel = 0;
        uint8_t  data[EOS_P2P_MAX_PACKET_SIZE];
        uint32_t bytes = 0;
        if (EOS_P2P_ReceivePacket(s_p2p, &opt, &peer, &sock, &channel,
                                  data, &bytes) != EOS_Success) break;
        if (strcmp(sock.SocketName, EOS_SOCKET_NAME) != 0) continue;
        if (!s_isHost && peer != s_hostPuid) continue; // client: host only
        // Host: pass the sender through — the core drops strangers itself.
        ReceiveFragment(s_isHost ? (void *)peer : NULL, (int)channel,
                        data, (int)bytes);
    }
}

static void EOS_CALL OnLeaveLobby(const EOS_Lobby_LeaveLobbyCallbackInfo *data) {
    (void)data; // teardown is fire-and-forget
}

static void EosStop(void) {
    if (s_p2p != NULL && s_localPuid != NULL) {
        // Close every connection on our socket in one call (multi-peer).
        EOS_P2P_CloseConnectionsOptions cls = { 0 };
        cls.ApiVersion  = EOS_P2P_CLOSECONNECTIONS_API_LATEST;
        cls.LocalUserId = s_localPuid;
        cls.SocketId    = &s_socketId;
        EOS_P2P_CloseConnections(s_p2p, &cls);
    }
    if (s_notifyRequest != EOS_INVALID_NOTIFICATIONID) {
        EOS_P2P_RemoveNotifyPeerConnectionRequest(s_p2p, s_notifyRequest);
        s_notifyRequest = EOS_INVALID_NOTIFICATIONID;
    }
    if (s_notifyClosed != EOS_INVALID_NOTIFICATIONID) {
        EOS_P2P_RemoveNotifyPeerConnectionClosed(s_p2p, s_notifyClosed);
        s_notifyClosed = EOS_INVALID_NOTIFICATIONID;
    }
    if (s_notifyEstablished != EOS_INVALID_NOTIFICATIONID) {
        EOS_P2P_RemoveNotifyPeerConnectionEstablished(s_p2p, s_notifyEstablished);
        s_notifyEstablished = EOS_INVALID_NOTIFICATIONID;
    }
    if (s_lobby != NULL && s_lobbyId[0] != '\0') {
        EOS_Lobby_LeaveLobbyOptions lv = { 0 };
        lv.ApiVersion  = EOS_LOBBY_LEAVELOBBY_API_LATEST;
        lv.LocalUserId = s_localPuid;
        lv.LobbyId     = s_lobbyId;
        EOS_Lobby_LeaveLobby(s_lobby, &lv, NULL, OnLeaveLobby);
        for (int i = 0; i < 10; i++) EOS_Platform_Tick(s_platform); // let it flush
        s_lobbyId[0] = '\0';
    }
    s_hostPuid = NULL;
    memset(s_reasm, 0, sizeof(s_reasm));
    // Platform + login stay up for the process — re-hosting skips auth.
}

// --- Lobby: create with join code / find by join code ------------------------

static void EOS_CALL OnCreateLobby(const EOS_Lobby_CreateLobbyCallbackInfo *data) {
    if (data->ResultCode == EOS_Success) {
        strncpy(s_opLobbyId, data->LobbyId, sizeof(s_opLobbyId) - 1);
        s_opStatus = 1;
    } else {
        TraceLog(LOG_WARNING, "[EOS] CreateLobby: %s", EOS_EResult_ToString(data->ResultCode));
        s_opStatus = -1;
    }
}

static void EOS_CALL OnGenericResult(const EOS_Lobby_UpdateLobbyCallbackInfo *data) {
    s_opStatus = (data->ResultCode == EOS_Success) ? 1 : -1;
    if (s_opStatus < 0)
        TraceLog(LOG_WARNING, "[EOS] UpdateLobby: %s", EOS_EResult_ToString(data->ResultCode));
}

static void EOS_CALL OnFindDone(const EOS_LobbySearch_FindCallbackInfo *data) {
    s_opStatus = (data->ResultCode == EOS_Success) ? 1 : -1;
    if (s_opStatus < 0)
        TraceLog(LOG_WARNING, "[EOS] LobbySearch_Find: %s", EOS_EResult_ToString(data->ResultCode));
}

static void EOS_CALL OnJoinLobby(const EOS_Lobby_JoinLobbyCallbackInfo *data) {
    if (data->ResultCode == EOS_Success) {
        strncpy(s_opLobbyId, data->LobbyId, sizeof(s_opLobbyId) - 1);
        s_opStatus = 1;
    } else {
        TraceLog(LOG_WARNING, "[EOS] JoinLobby: %s", EOS_EResult_ToString(data->ResultCode));
        s_opStatus = -1;
    }
}

static void GenerateJoinCode(char *out) {
    // No 0/O/1/I/L lookalikes — the code gets read out loud between friends.
    static const char charset[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    unsigned int state = (unsigned int)time(NULL) ^ ((unsigned int)getpid() << 16);
    for (int i = 0; i < EOS_JOINCODE_LEN; i++) {
        state = state * 1664525u + 1013904223u;
        out[i] = charset[(state >> 16) % (sizeof(charset) - 1)];
    }
    out[EOS_JOINCODE_LEN] = '\0';
}

// --- Public API (net_transport.h online block) --------------------------------

bool Net_OnlineAvailable(void) {
    return true;
}

bool Net_StartHostOnline(char *outJoinCode, int maxCodeLen) {
    if (Net_GetMode() != NET_MODE_OFF) return false;
    if (outJoinCode == NULL || maxCodeLen < EOS_JOINCODE_LEN + 1) return false;
    if (!EnsureLoggedIn()) return false;

    char code[EOS_JOINCODE_LEN + 1];
    GenerateJoinCode(code);

    EOS_Lobby_CreateLobbyOptions create = { 0 };
    create.ApiVersion       = EOS_LOBBY_CREATELOBBY_API_LATEST;
    create.LocalUserId      = s_localPuid;
    create.MaxLobbyMembers  = NET_MAX_PLAYERS; // 4v4 vision — 8 kể cả host
    create.PermissionLevel  = EOS_LPL_PUBLICADVERTISED;
    create.bPresenceEnabled = EOS_FALSE;
    create.bAllowInvites    = EOS_TRUE;
    create.BucketId         = EOS_BUCKET_ID;
    s_opStatus = 0; s_opLobbyId[0] = '\0';
    EOS_Lobby_CreateLobby(s_lobby, &create, NULL, OnCreateLobby);
    if (!PumpUntilDone("CreateLobby")) return false;
    strncpy(s_lobbyId, s_opLobbyId, sizeof(s_lobbyId) - 1);

    // Attach the join code as a public searchable attribute.
    EOS_Lobby_UpdateLobbyModificationOptions modOpt = { 0 };
    modOpt.ApiVersion  = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
    modOpt.LocalUserId = s_localPuid;
    modOpt.LobbyId     = s_lobbyId;
    EOS_HLobbyModification mod = NULL;
    if (EOS_Lobby_UpdateLobbyModification(s_lobby, &modOpt, &mod) != EOS_Success)
        return false;
    EOS_Lobby_AttributeData attr = { 0 };
    attr.ApiVersion   = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    attr.Key          = EOS_JOINCODE_KEY;
    attr.Value.AsUtf8 = code;
    attr.ValueType    = EOS_AT_STRING;
    EOS_LobbyModification_AddAttributeOptions add = { 0 };
    add.ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
    add.Attribute  = &attr;
    add.Visibility = EOS_LAT_PUBLIC;
    EOS_LobbyModification_AddAttribute(mod, &add);
    EOS_Lobby_UpdateLobbyOptions upd = { 0 };
    upd.ApiVersion              = EOS_LOBBY_UPDATELOBBY_API_LATEST;
    upd.LobbyModificationHandle = mod;
    s_opStatus = 0;
    EOS_Lobby_UpdateLobby(s_lobby, &upd, NULL, OnGenericResult);
    bool ok = PumpUntilDone("UpdateLobby(join code)");
    EOS_LobbyModification_Release(mod);
    if (!ok) return false;

    s_isHost = true;
    RegisterP2PNotifies();
    NetTransport_SetBackend(NET_MODE_HOST, EosSend, EosTick, EosStop);
    strncpy(outJoinCode, code, (size_t)maxCodeLen - 1);
    outJoinCode[maxCodeLen - 1] = '\0';
    TraceLog(LOG_INFO, "[EOS] hosting online — join code: %s", code);
    return true;
}

bool Net_JoinOnline(const char *joinCode) {
    if (Net_GetMode() != NET_MODE_OFF) return false;
    if (joinCode == NULL || strlen(joinCode) < 3) return false;
    if (!EnsureLoggedIn()) return false;

    char code[32] = { 0 };
    for (int i = 0; joinCode[i] != '\0' && i < 31; i++)
        code[i] = (char)((joinCode[i] >= 'a' && joinCode[i] <= 'z')
                             ? joinCode[i] - 'a' + 'A' : joinCode[i]);

    EOS_Lobby_CreateLobbySearchOptions cso = { 0 };
    cso.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
    cso.MaxResults = 1;
    EOS_HLobbySearch search = NULL;
    if (EOS_Lobby_CreateLobbySearch(s_lobby, &cso, &search) != EOS_Success)
        return false;

    EOS_Lobby_AttributeData attr = { 0 };
    attr.ApiVersion   = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    attr.Key          = EOS_JOINCODE_KEY;
    attr.Value.AsUtf8 = code;
    attr.ValueType    = EOS_AT_STRING;
    EOS_LobbySearch_SetParameterOptions par = { 0 };
    par.ApiVersion   = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
    par.Parameter    = &attr;
    par.ComparisonOp = EOS_CO_EQUAL;
    EOS_LobbySearch_SetParameter(search, &par);

    EOS_LobbySearch_FindOptions find = { 0 };
    find.ApiVersion  = EOS_LOBBYSEARCH_FIND_API_LATEST;
    find.LocalUserId = s_localPuid;
    s_opStatus = 0;
    EOS_LobbySearch_Find(search, &find, NULL, OnFindDone);
    if (!PumpUntilDone("LobbySearch_Find")) { EOS_LobbySearch_Release(search); return false; }

    EOS_LobbySearch_GetSearchResultCountOptions cnt = { 0 };
    cnt.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
    if (EOS_LobbySearch_GetSearchResultCount(search, &cnt) == 0) {
        TraceLog(LOG_WARNING, "[EOS] no lobby found for code %s", code);
        EOS_LobbySearch_Release(search);
        return false;
    }
    EOS_LobbySearch_CopySearchResultByIndexOptions cp = { 0 };
    cp.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
    cp.LobbyIndex = 0;
    EOS_HLobbyDetails details = NULL;
    if (EOS_LobbySearch_CopySearchResultByIndex(search, &cp, &details) != EOS_Success) {
        EOS_LobbySearch_Release(search);
        return false;
    }
    EOS_LobbyDetails_GetLobbyOwnerOptions own = { 0 };
    own.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
    EOS_ProductUserId owner = EOS_LobbyDetails_GetLobbyOwner(details, &own);

    EOS_Lobby_JoinLobbyOptions join = { 0 };
    join.ApiVersion         = EOS_LOBBY_JOINLOBBY_API_LATEST;
    join.LobbyDetailsHandle = details;
    join.LocalUserId        = s_localPuid;
    join.bPresenceEnabled   = EOS_FALSE;
    s_opStatus = 0; s_opLobbyId[0] = '\0';
    EOS_Lobby_JoinLobby(s_lobby, &join, NULL, OnJoinLobby);
    bool ok = PumpUntilDone("JoinLobby");
    EOS_LobbyDetails_Release(details);
    EOS_LobbySearch_Release(search);
    if (!ok || owner == NULL) return false;

    // Self-join guard: on ONE machine both instances share the Device ID →
    // the same ProductUserId → the joiner IS the host. P2P to yourself never
    // delivers, so neither side ever sees the other (the classic "cùng máy
    // quên WUXING_EOS_FRESH_DEVICE" trap). Fail loudly instead of hanging.
    if (owner == s_localPuid) {
        TraceLog(LOG_WARNING,
                 "[EOS] ban dang tu join phong CUA CHINH MINH (cung 1 danh tinh "
                 "Epic). Cung 1 may: chay cua so khach kem 'WUXING_EOS_FRESH_DEVICE=1'. "
                 "Choi that: dung 2 may khac nhau.");
        return false;
    }
    strncpy(s_lobbyId, s_opLobbyId, sizeof(s_lobbyId) - 1);

    s_isHost = false;
    s_hostPuid = owner;
    RegisterP2PNotifies();
    NetTransport_SetBackend(NET_MODE_CLIENT, EosSend, EosTick, EosStop);
    // Optimistically up: the first intent packets trigger the NAT punch
    // (bAllowDelayedDelivery queues them); the host flips connected when the
    // connection request lands on its side. Snapshots start flowing after.
    NetTransport_BackendConnected(NULL);
    TraceLog(LOG_INFO, "[EOS] joined lobby for code %s — punching through to host", code);
    return true;
}
