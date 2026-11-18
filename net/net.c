// net/net.c — Quantized wire formats + interest-culled open-world snapshots.
#include "net/net.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define NET_MAGIC        'W'
#define NET_KIND_INTENT  0x01
#define NET_KIND_SNAP    0x02
#define NET_KIND_ROSTER  0x03

static int WriteF32(unsigned char *buf, int off, float v)      { memcpy(buf + off, &v, 4); return off + 4; }
static int ReadF32(const unsigned char *buf, int off, float *v){ memcpy(v, buf + off, 4); return off + 4; }

static int WriteI16(unsigned char *buf, int off, int16_t v)      { memcpy(buf + off, &v, 2); return off + 2; }
static int ReadI16(const unsigned char *buf, int off, int16_t *v){ memcpy(v, buf + off, 2); return off + 2; }
static int WriteU16(unsigned char *buf, int off, uint16_t v)     { memcpy(buf + off, &v, 2); return off + 2; }
static int ReadU16(const unsigned char *buf, int off, uint16_t *v){ memcpy(v, buf + off, 2); return off + 2; }

static int16_t QuantizeCoord(float v) {
    float q = roundf(v * 64.0f);
    if (q >  32767.0f) q =  32767.0f;
    if (q < -32768.0f) q = -32768.0f;
    return (int16_t)q;
}

static uint16_t QuantizeStat(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 65535.0f) return 65535;
    return (uint16_t)roundf(v);
}

// --- PlayerIntent (client -> host, 26B) ---
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
    out->jump          = (buf[3] & 1) != 0;
    out->dash          = (buf[3] & 2) != 0;
    out->meditate      = (buf[3] & 4) != 0;
    out->castSkillSlot = (int)(signed char)buf[4];
    out->basicAttack   = (int)buf[5];
    int off = 6;
    off = ReadF32(buf, off, &out->moveDir.x);
    off = ReadF32(buf, off, &out->moveDir.y);
    off = ReadF32(buf, off, &out->aimPoint.x);
    off = ReadF32(buf, off, &out->aimPoint.y);
    (void)ReadF32(buf, off, &out->aimPoint.z);
    return true;
}

// --- Quantized Agent Snapshot (18 bytes/agent: 45% smaller than 33B) ---
// Layout per agent:
//   agentId(1) + meta(1) + flags(1) + yaw(1) + posXYZ(6) + hp/maxHp/mp/maxMp(8) = 18B
#define SNAP_AGENT_BYTES  18
#define SNAP_HEADER_BYTES 4

int Net_PackAgentSnapshotFor(Vector3 focusPos, float interestRadius,
                             unsigned char *buf, int maxBytes) {
    if (buf == NULL || maxBytes < SNAP_HEADER_BYTES) return 0;
    int off = SNAP_HEADER_BYTES;
    int count = 0;
    float rSq = (interestRadius > 0.0f) ? (interestRadius * interestRadius) : -1.0f;

    for (int i = 0; i < MAX_AGENTS && count < 255; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (a == NULL) continue;

        if (rSq > 0.0f && a->archetype == ARCH_MINION) {
            float dx = a->position.x - focusPos.x;
            float dz = a->position.z - focusPos.z;
            if (dx * dx + dz * dz > rSq) continue;
        }

        if (off + SNAP_AGENT_BYTES > maxBytes) return 0;

        unsigned char meta = (unsigned char)(((a->team & 0x3) << 0) |
                                             ((a->archetype & 0x3) << 2) |
                                             ((a->currentElement & 0x7) << 4));
        unsigned char flags = (unsigned char)((a->taijiActive ? 1 : 0) |
                                              (a->isMeditating ? 2 : 0) |
                                              (a->isStealthed ? 4 : 0) |
                                              (a->duelFlag ? 8 : 0));
        float yawRad = atan2f(a->velocity.x, a->velocity.z);
        if (yawRad < 0.0f) yawRad += 2.0f * PI;
        unsigned char yawByte = (unsigned char)((yawRad / (2.0f * PI)) * 255.0f);

        buf[off++] = (unsigned char)i;
        buf[off++] = meta;
        buf[off++] = flags;
        buf[off++] = yawByte;
        off = WriteI16(buf, off, QuantizeCoord(a->position.x));
        off = WriteI16(buf, off, QuantizeCoord(a->position.y));
        off = WriteI16(buf, off, QuantizeCoord(a->position.z));
        off = WriteU16(buf, off, QuantizeStat(a->health));
        off = WriteU16(buf, off, QuantizeStat(a->maxHealth));
        off = WriteU16(buf, off, QuantizeStat(a->mana));
        off = WriteU16(buf, off, QuantizeStat(a->maxMana));
        count++;
    }
    buf[0] = NET_MAGIC;
    buf[1] = NET_KIND_SNAP;
    buf[2] = NET_PROTOCOL_VERSION;
    buf[3] = (unsigned char)count;
    return off;
}

int Net_PackAgentSnapshot(unsigned char *buf, int maxBytes) {
    return Net_PackAgentSnapshotFor((Vector3){ 0 }, 0.0f, buf, maxBytes);
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
        s->agentId = buf[off++];
        unsigned char meta = buf[off++];
        s->team      = (meta >> 0) & 0x3;
        s->archetype = (meta >> 2) & 0x3;
        s->element   = (meta >> 4) & 0x7;
        s->flags     = buf[off++];
        s->yaw       = ((float)buf[off++] / 255.0f) * (2.0f * PI);

        int16_t px, py, pz;
        uint16_t hp, mhp, mp, mmp;
        off = ReadI16(buf, off, &px);
        off = ReadI16(buf, off, &py);
        off = ReadI16(buf, off, &pz);
        off = ReadU16(buf, off, &hp);
        off = ReadU16(buf, off, &mhp);
        off = ReadU16(buf, off, &mp);
        off = ReadU16(buf, off, &mmp);

        s->position  = (Vector3){ (float)px / 64.0f, (float)py / 64.0f, (float)pz / 64.0f };
        s->health    = (float)hp;
        s->maxHealth = (float)mhp;
        s->mana      = (float)mp;
        s->maxMana   = (float)mmp;
    }
    return n;
}

// --- Room roster (host -> clients, reliable) ---
#define ROSTER_HEADER_BYTES 4
#define ROSTER_ENTRY_BYTES  4

int Net_PackRoster(const NetRosterEntry *entries, int count,
                   unsigned char *buf, int maxBytes) {
    if (entries == NULL || buf == NULL || count < 0 || count > NET_MAX_PLAYERS) return 0;
    if (maxBytes < ROSTER_HEADER_BYTES + count * ROSTER_ENTRY_BYTES) return 0;
    int off = 0;
    buf[off++] = NET_MAGIC;
    buf[off++] = NET_KIND_ROSTER;
    buf[off++] = NET_PROTOCOL_VERSION;
    buf[off++] = (unsigned char)count;
    for (int i = 0; i < count; i++) {
        buf[off++] = entries[i].slot;
        buf[off++] = entries[i].team;
        buf[off++] = entries[i].agentId;
        buf[off++] = entries[i].flags;
    }
    return off;
}

int Net_UnpackRoster(NetRosterEntry *out, int maxEntries,
                     const unsigned char *buf, int len) {
    if (out == NULL || buf == NULL || len < ROSTER_HEADER_BYTES) return -1;
    if (buf[0] != NET_MAGIC || buf[1] != NET_KIND_ROSTER || buf[2] != NET_PROTOCOL_VERSION) return -1;
    int count = buf[3];
    if (count > NET_MAX_PLAYERS || len < ROSTER_HEADER_BYTES + count * ROSTER_ENTRY_BYTES) return -1;
    int n = (count < maxEntries) ? count : maxEntries;
    int off = ROSTER_HEADER_BYTES;
    for (int i = 0; i < n; i++) {
        out[i].slot    = buf[off++];
        out[i].team    = buf[off++];
        out[i].agentId = buf[off++];
        out[i].flags   = buf[off++];
    }
    return n;
}
