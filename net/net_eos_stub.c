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
