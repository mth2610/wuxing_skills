// net/net.c — Module 11 core: versioned wire formats, no sockets yet (the
// ENet transport layer ships with the PvP milestone). Same-architecture
// little-endian float memcpy is fine for the peer-hosted mobile target.
#include "net/net.h"
#include <string.h>
#include <stddef.h>

// Packet magic: 'W' + kind byte + version byte.
#define NET_MAGIC        'W'
#define NET_KIND_INTENT  0x01
#define NET_KIND_SNAP    0x02

// --- helpers ---
static int WriteF32(unsigned char *buf, int off, float v)   { memcpy(buf + off, &v, 4); return off + 4; }
static int ReadF32(const unsigned char *buf, int off, float *v) { memcpy(v, buf + off, 4); return off + 4; }

// --- PlayerIntent (client → host) ---
// Layout: magic, kind, version, flags(jump/dash/meditate), castSlot(int8),
// basicAttack(u8: 0=none, else BasicAttackType+1),
// moveDir.xy, aimPoint.xyz  → 6 + 5*4 = 26 bytes.
#define INTENT_PACKET_BYTES 26

int Net_PackIntent(const PlayerIntent *in, unsigned char *buf, int maxBytes) {
    if (in == NULL || buf == NULL || maxBytes < INTENT_PACKET_BYTES) return 0;
    int off = 0;
    buf[off++] = NET_MAGIC;
    buf[off++] = NET_KIND_INTENT;
    buf[off++] = NET_PROTOCOL_VERSION;
    buf[off++] = (unsigned char)((in->jump ? 1 : 0) | (in->dash ? 2 : 0) | (in->meditate ? 4 : 0));
    buf[off++] = (unsigned char)(signed char)in->castSkillSlot;
    buf[off++] = (unsigned char)in->basicAttack;
    off = WriteF32(buf, off, in->moveDir.x);
    off = WriteF32(buf, off, in->moveDir.y);
    off = WriteF32(buf, off, in->aimPoint.x);
    off = WriteF32(buf, off, in->aimPoint.y);
    off = WriteF32(buf, off, in->aimPoint.z);
    return off;
}

bool Net_UnpackIntent(PlayerIntent *out, const unsigned char *buf, int len) {
    if (out == NULL || buf == NULL || len < INTENT_PACKET_BYTES) return false;
    if (buf[0] != NET_MAGIC || buf[1] != NET_KIND_INTENT || buf[2] != NET_PROTOCOL_VERSION) return false;
    memset(out, 0, sizeof(*out));
    out->jump     = (buf[3] & 1) != 0;
    out->dash     = (buf[3] & 2) != 0;
    out->meditate = (buf[3] & 4) != 0;
    out->castSkillSlot = (int)(signed char)buf[4];
    out->basicAttack = (int)buf[5];
    int off = 6;
    off = ReadF32(buf, off, &out->moveDir.x);
    off = ReadF32(buf, off, &out->moveDir.y);
    off = ReadF32(buf, off, &out->aimPoint.x);
    off = ReadF32(buf, off, &out->aimPoint.y);
    (void)ReadF32(buf, off, &out->aimPoint.z);
    return true;
}

// --- Agent snapshot (host → clients) ---
// Header: magic, kind, version, count. Per agent: id, team, archetype,
// element, flags (5 bytes) + 7 floats (28) = 33 bytes.
#define SNAP_AGENT_BYTES 33
#define SNAP_HEADER_BYTES 4

int Net_PackAgentSnapshot(unsigned char *buf, int maxBytes) {
    if (buf == NULL || maxBytes < SNAP_HEADER_BYTES) return 0;
    int off = SNAP_HEADER_BYTES;
    int count = 0;
    for (int i = 0; i < MAX_AGENTS && count < 255; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (a == NULL) continue;
        if (off + SNAP_AGENT_BYTES > maxBytes) return 0; // undersized buffer
        buf[off++] = (unsigned char)i;
        buf[off++] = (unsigned char)a->team;
        buf[off++] = (unsigned char)a->archetype;
        buf[off++] = (unsigned char)a->currentElement;
        buf[off++] = (unsigned char)((a->taijiActive ? 1 : 0) |
                                     (a->isMeditating ? 2 : 0) |
                                     (a->isStealthed ? 4 : 0));
        off = WriteF32(buf, off, a->position.x);
        off = WriteF32(buf, off, a->position.y);
        off = WriteF32(buf, off, a->position.z);
        off = WriteF32(buf, off, a->health);
        off = WriteF32(buf, off, a->maxHealth);
        off = WriteF32(buf, off, a->mana);
        off = WriteF32(buf, off, a->maxMana);
        count++;
    }
    buf[0] = NET_MAGIC;
    buf[1] = NET_KIND_SNAP;
    buf[2] = NET_PROTOCOL_VERSION;
    buf[3] = (unsigned char)count;
    return off;
}

int Net_UnpackAgentSnapshot(NetAgentState *out, int maxAgents,
                            const unsigned char *buf, int len) {
    if (out == NULL || buf == NULL || len < SNAP_HEADER_BYTES) return -1;
    if (buf[0] != NET_MAGIC || buf[1] != NET_KIND_SNAP || buf[2] != NET_PROTOCOL_VERSION) return -1;
    int count = buf[3];
    if (len < SNAP_HEADER_BYTES + count * SNAP_AGENT_BYTES) return -1;

    int n = (count < maxAgents) ? count : maxAgents;
    int off = SNAP_HEADER_BYTES;
    for (int k = 0; k < n; k++) {
        NetAgentState *s = &out[k];
        s->agentId   = buf[off++];
        s->team      = buf[off++];
        s->archetype = buf[off++];
        s->element   = buf[off++];
        s->flags     = buf[off++];
        off = ReadF32(buf, off, &s->position.x);
        off = ReadF32(buf, off, &s->position.y);
        off = ReadF32(buf, off, &s->position.z);
        off = ReadF32(buf, off, &s->health);
        off = ReadF32(buf, off, &s->maxHealth);
        off = ReadF32(buf, off, &s->mana);
        off = ReadF32(buf, off, &s->maxMana);
    }
    return n;
}
