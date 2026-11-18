// net/net_eos_stub.c — placeholder online backend, compiled when the EOS
// SDK isn't wired (default). The real implementation (net/net_eos.c) lands
// behind -DWUXING_EOS=ON once the SDK + keys exist — see EOS_SETUP.md.
#include "net/net_transport.h"
#include "raylib.h" // TraceLog
#include <stddef.h>

bool Net_OnlineAvailable(void) {
    return false;
}

bool Net_StartHostOnline(char *outJoinCode, int maxCodeLen) {
    (void)outJoinCode; (void)maxCodeLen;
    TraceLog(LOG_WARNING, "[NET] online backend not built — see EOS_SETUP.md (LAN: --host/--join)");
    return false;
}

bool Net_JoinOnline(const char *joinCode) {
    (void)joinCode;
    TraceLog(LOG_WARNING, "[NET] online backend not built — see EOS_SETUP.md (LAN: --host/--join)");
    return false;
}

bool Net_JoinOrHostExploreRealm(int realmId, char *outJoinCode, int maxCodeLen) {
    (void)realmId; (void)outJoinCode; (void)maxCodeLen;
    return false;
}

bool Net_VoiceIsAvailable(void) { return false; }
void Net_VoiceSetMicEnabled(bool enabled) { (void)enabled; }
bool Net_VoiceIsMicEnabled(void) { return false; }
void Net_VoiceSetMutedAll(bool muted) { (void)muted; }
bool Net_VoiceIsMutedAll(void) { return false; }
bool Net_VoiceIsAnyoneSpeaking(void) { return false; }
int  Net_VoiceGetSpeakingCount(void) { return 0; }
