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

static uint16_t QuantizeCoordXZ(float v) {
    // 48.0f steps/meter = 2.08cm precision across [-128.0m, +1237.3m] (covers 4x Open World 1200m maps)
    float q = roundf((v + 128.0f) * 48.0f);
    if (q > 65535.0f) q = 65535.0f;
    if (q < 0.0f)     q = 0.0f;
    return (uint16_t)q;
}

static float DequantizeCoordXZ(uint16_t q) {
    return ((float)q / 48.0f) - 128.0f;
}

static int16_t QuantizeCoordY(float v) {
    // 64.0f steps/meter = 1.56cm vertical precision across [-512m, +511m]
    float q = roundf(v * 64.0f);
    if (q >  32767.0f) q =  32767.0f;
    if (q < -32768.0f) q = -32768.0f;
    return (int16_t)q;
}

static float DequantizeCoordY(int16_t q) {
    return (float)q / 64.0f;
}

static uint16_t QuantizeStat(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 65535.0f) return 65535;
    return (uint16_t)roundf(v);
}

// --- PlayerIntent (client -> host, 33B with authoritative client pose) ---
#define INTENT_PACKET_BYTES 33

int Net_PackIntent(const PlayerIntent *in, unsigned char *buf, int maxBytes) {
    if (in == NULL || buf == NULL || maxBytes < INTENT_PACKET_BYTES) return 0;
    int off = 0;
    buf[off++] = NET_MAGIC;
    buf[off++] = NET_KIND_INTENT;
    buf[off++] = NET_PROTOCOL_VERSION;
    buf[off++] = (unsigned char)((in->jump ? 1 : 0) |
                                 (in->dash ? 2 : 0) |
                                 (in->meditate ? 4 : 0) |
                                 (in->hasClientPose ? 8 : 0) |
                                 (in->clientInAir ? 16 : 0) |
                                 (in->clientDashing ? 32 : 0));
    buf[off++] = (unsigned char)(signed char)in->castSkillSlot;
    buf[off++] = (unsigned char)in->basicAttack;
    off = WriteF32(buf, off, in->moveDir.x);
    off = WriteF32(buf, off, in->moveDir.y);
    off = WriteF32(buf, off, in->aimPoint.x);
    off = WriteF32(buf, off, in->aimPoint.y);
    off = WriteF32(buf, off, in->aimPoint.z);
    off = WriteU16(buf, off, QuantizeCoordXZ(in->clientPos.x));
    off = WriteI16(buf, off, QuantizeCoordY(in->clientPos.y));
    off = WriteU16(buf, off, QuantizeCoordXZ(in->clientPos.z));
    float yawRad = fmodf(in->clientYaw, 2.0f * PI);
    if (yawRad < 0.0f) yawRad += 2.0f * PI;
    buf[off++] = (unsigned char)((yawRad / (2.0f * PI)) * 255.0f);
    return off;
}

bool Net_UnpackIntent(PlayerIntent *out, const unsigned char *buf, int len) {
    if (out == NULL || buf == NULL || len < INTENT_PACKET_BYTES) return false;
    if (buf[0] != NET_MAGIC || buf[1] != NET_KIND_INTENT || buf[2] != NET_PROTOCOL_VERSION) return false;
    memset(out, 0, sizeof(*out));
    out->jump          = (buf[3] & 1) != 0;
    out->dash          = (buf[3] & 2) != 0;
    out->meditate      = (buf[3] & 4) != 0;
    out->hasClientPose = (buf[3] & 8) != 0;
    out->clientInAir   = (buf[3] & 16) != 0;
    out->clientDashing = (buf[3] & 32) != 0;
    out->castSkillSlot = (int)(signed char)buf[4];
    out->basicAttack   = (int)buf[5];
    int off = 6;
    off = ReadF32(buf, off, &out->moveDir.x);
    off = ReadF32(buf, off, &out->moveDir.y);
    off = ReadF32(buf, off, &out->aimPoint.x);
    off = ReadF32(buf, off, &out->aimPoint.y);
    off = ReadF32(buf, off, &out->aimPoint.z);
    uint16_t cx, cz;
    int16_t cy;
    off = ReadU16(buf, off, &cx);
    off = ReadI16(buf, off, &cy);
    off = ReadU16(buf, off, &cz);
    out->clientPos = (Vector3){ DequantizeCoordXZ(cx), DequantizeCoordY(cy), DequantizeCoordXZ(cz) };
    out->clientYaw = ((float)buf[off++] / 255.0f) * (2.0f * PI);
    return true;
}

// --- Quantized Agent Snapshot (18 bytes/agent: 45% smaller than 33B) ---
// Layout per agent:
//   agentId(1) + meta(1) + flags(1) + yaw(1) + posXYZ(6) + hp/maxHp/mp/maxMp(8) = 18B
#define SNAP_AGENT_BYTES  18
#define SNAP_HEADER_BYTES 4

// 60 agents = 4B + 60*18B = 1084B <= 1167B EOS P2P single-packet payload limit
// (comfortably fits 4 Heroes + 2 Bosses + 54 Monsters in 1 unfragmented UDP packet).
#define SNAP_MAX_SINGLE_PKT_AGENTS 60

static int PackOneAgent(int i, const Agent *a, unsigned char *buf, int off) {
    unsigned char meta = (unsigned char)(((a->team & 0x3) << 0) |
                                         ((a->archetype & 0x3) << 2) |
                                         ((a->currentElement & 0x7) << 4));
    unsigned char flags;
    if (a->archetype == ARCH_MINION) {
        // For monsters/minions, repurpose the 8-bit flags field to replicate
        // [bits 0..1 MonsterKind | bits 2..4 MonsterAIState | bits 5..7 telegraphFrac]
        // at 0 extra byte cost!
        flags = a->netMonsterMeta;
    } else {
        flags = (unsigned char)((a->taijiActive ? 1 : 0) |
                                (a->isMeditating ? 2 : 0) |
                                (a->isStealthed ? 4 : 0) |
                                (a->duelFlag ? 8 : 0) |
                                (((unsigned int)a->pkMode & 0x3u) << 4));
    }
    float yawRad = a->facingYaw;
    if (fabsf(yawRad) < 0.0001f && (fabsf(a->velocity.x) > 0.001f || fabsf(a->velocity.z) > 0.001f)) {
        yawRad = atan2f(a->velocity.x, a->velocity.z);
    }
    yawRad = fmodf(yawRad, 2.0f * PI);
    if (yawRad < 0.0f) yawRad += 2.0f * PI;
    unsigned char yawByte = (unsigned char)((yawRad / (2.0f * PI)) * 255.0f);

    buf[off++] = (unsigned char)i;
    buf[off++] = meta;
    buf[off++] = flags;
    buf[off++] = yawByte;
    off = WriteU16(buf, off, QuantizeCoordXZ(a->position.x));
    off = WriteI16(buf, off, QuantizeCoordY(a->position.y));
    off = WriteU16(buf, off, QuantizeCoordXZ(a->position.z));
    off = WriteU16(buf, off, QuantizeStat(a->health));
    off = WriteU16(buf, off, QuantizeStat(a->maxHealth));
    off = WriteU16(buf, off, QuantizeStat(a->mana));
    off = WriteU16(buf, off, QuantizeStat(a->maxMana));
    return off;
}

int Net_PackAgentSnapshotFor(Vector3 focusPos, float interestRadius,
                             unsigned char *buf, int maxBytes) {
    if (buf == NULL || maxBytes < SNAP_HEADER_BYTES) return 0;
    int off = SNAP_HEADER_BYTES;
    int count = 0;
    float rSq = (interestRadius > 0.0f) ? (interestRadius * interestRadius) : -1.0f;
    int cap = (rSq > 0.0f) ? SNAP_MAX_SINGLE_PKT_AGENTS : 255;

    // Pass 1: always pack heroes & bosses first so they are never culled.
    for (int i = 0; i < MAX_AGENTS && count < cap; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (a == NULL) continue;
        if (rSq > 0.0f && a->archetype == ARCH_MINION) continue;
        if (off + SNAP_AGENT_BYTES > maxBytes) return 0;
        off = PackOneAgent(i, a, buf, off);
        count++;
    }

    // Pass 2 (Single-packet interest culling): pack the closest & actively
    // attacking minions up to the single-UDP-packet budget (60 total agents =
    // 1084B <= 1167B EOS MTU) so snapshots never fragment or drop on Wi-Fi/4G.
    if (rSq > 0.0f && count < cap) {
        int slotsLeft = cap - count;
        int mId[SNAP_MAX_SINGLE_PKT_AGENTS];
        float mDist[SNAP_MAX_SINGLE_PKT_AGENTS];
        int mCount = 0;
        for (int i = 0; i < MAX_AGENTS; i++) {
            const Agent *a = Entity_GetAgent(i);
            if (a == NULL || a->archetype != ARCH_MINION) continue;
            float dx = a->position.x - focusPos.x;
            float dz = a->position.z - focusPos.z;
            float d2 = dx * dx + dz * dz;
            if (d2 > rSq) continue;
            // Prioritize monsters currently telegraphing or striking (aiState 3 or 4)
            unsigned char monState = (a->netMonsterMeta >> 2) & 0x7u;
            if (monState == 3u || monState == 4u) d2 *= 0.25f;
            if (mCount < slotsLeft) {
                int k = mCount++;
                while (k > 0 && mDist[k - 1] > d2) {
                    mId[k] = mId[k - 1];
                    mDist[k] = mDist[k - 1];
                    k--;
                }
                mId[k] = i;
                mDist[k] = d2;
            } else if (d2 < mDist[slotsLeft - 1]) {
                int k = slotsLeft - 1;
                while (k > 0 && mDist[k - 1] > d2) {
                    mId[k] = mId[k - 1];
                    mDist[k] = mDist[k - 1];
                    k--;
                }
                mId[k] = i;
                mDist[k] = d2;
            }
        }
        for (int k = 0; k < mCount; k++) {
            if (off + SNAP_AGENT_BYTES > maxBytes) break;
            const Agent *a = Entity_GetAgent(mId[k]);
            if (a == NULL) continue;
            off = PackOneAgent(mId[k], a, buf, off);
            count++;
        }
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

        uint16_t px, pz;
        int16_t py;
        uint16_t hp, mhp, mp, mmp;
        off = ReadU16(buf, off, &px);
        off = ReadI16(buf, off, &py);
        off = ReadU16(buf, off, &pz);
        off = ReadU16(buf, off, &hp);
        off = ReadU16(buf, off, &mhp);
        off = ReadU16(buf, off, &mp);
        off = ReadU16(buf, off, &mmp);

        s->position  = (Vector3){ DequantizeCoordXZ(px), DequantizeCoordY(py), DequantizeCoordXZ(pz) };
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
