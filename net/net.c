// net/net.c — Compact quantized wire formats + interest-culled delta snapshots.
#include "net/net.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define NET_MAGIC 'W'
#define NET_KIND_INTENT 0x01
#define NET_KIND_SNAP 0x02
#define NET_KIND_ROSTER 0x03
#define NET_KIND_DELTA 0x04

// Delta per-agent field bitmask
#define DM_POS_DELTA 0x01u // int8 dx, dy, dz (3B) — covers +/-3.17m XZ, +/-1.98m Y
#define DM_POS_FULL 0x02u  // uint16 px, int16 py, uint16 pz (6B)
#define DM_YAW 0x04u       // uint8 yaw (1B)
#define DM_FLAGS 0x08u     // uint8 flags (1B)
#define DM_HP 0x10u        // uint16 hp (2B)
#define DM_MP 0x20u        // uint16 mp (2B)
#define DM_STATIC 0x40u    // uint8 meta, uint16 mhp (+ uint16 mmp if meta&0x80)
#define DM_NEW 0x80u       // agent absent in baseline (client zeroes slot first)

static int WriteI16(unsigned char *buf, int off, int16_t v)
{
    memcpy(buf + off, &v, 2);
    return off + 2;
}
static int ReadI16(const unsigned char *buf, int off, int16_t *v)
{
    memcpy(v, buf + off, 2);
    return off + 2;
}
static int WriteU16(unsigned char *buf, int off, uint16_t v)
{
    memcpy(buf + off, &v, 2);
    return off + 2;
}
static int ReadU16(const unsigned char *buf, int off, uint16_t *v)
{
    memcpy(v, buf + off, 2);
    return off + 2;
}

static uint16_t QuantizeCoordXZ(float v)
{
    if (!isfinite(v))
        return 0;
    // 40.0f steps/meter = 2.5cm precision across [-256.0m, +1382.375m]
    float q = roundf((v + 256.0f) * 40.0f);
    if (q > 65535.0f)
        q = 65535.0f;
    if (q < 0.0f)
        q = 0.0f;
    return (uint16_t)q;
}

static float DequantizeCoordXZ(uint16_t q)
{
    return ((float)q * 0.025f) - 256.0f;
}

static int16_t QuantizeCoordY(float v)
{
    if (!isfinite(v))
        return 0;
    // 64.0f steps/meter = 1.56cm vertical precision across [-512m, +511.98m]
    float q = roundf(v * 64.0f);
    if (q > 32767.0f)
        q = 32767.0f;
    if (q < -32768.0f)
        q = -32768.0f;
    return (int16_t)q;
}

static float DequantizeCoordY(int16_t q)
{
    return (float)q * (1.0f / 64.0f);
}

static uint16_t QuantizeStat(float v)
{
    if (!isfinite(v) || v <= 0.0f)
        return 0;
    if (v >= 65535.0f)
        return 65535;
    uint16_t q = (uint16_t)roundf(v);
    // Never round a living agent's fractional HP/MP (0 < v < 0.5) down to 0.
    return (q == 0) ? 1 : q;
}

static int8_t QuantizeDirAxis(float v)
{
    if (!isfinite(v))
        return 0;
    if (v > 1.0f)
        v = 1.0f;
    if (v < -1.0f)
        v = -1.0f;
    return (int8_t)roundf(v * 127.0f);
}

static float DequantizeDirAxis(int8_t q)
{
    return (float)q * (1.0f / 127.0f);
}

static uint8_t QuantizeYaw(float yawRad)
{
    if (!isfinite(yawRad))
        return 0;
    yawRad = fmodf(yawRad, 2.0f * PI);
    if (yawRad < 0.0f)
        yawRad += 2.0f * PI;
    return (uint8_t)(((int)roundf(yawRad * (256.0f / (2.0f * PI)))) & 0xFF);
}

static float DequantizeYaw(uint8_t q)
{
    return (float)q * ((2.0f * PI) / 256.0f);
}

void Net_PackVec3Quantized(Vector3 v, unsigned char out[6])
{
    int off = 0;
    off = WriteU16(out, off, QuantizeCoordXZ(v.x));
    off = WriteI16(out, off, QuantizeCoordY(v.y));
    (void)WriteU16(out, off, QuantizeCoordXZ(v.z));
}

Vector3 Net_UnpackVec3Quantized(const unsigned char in[6])
{
    uint16_t x, z;
    int16_t y;
    int off = 0;
    off = ReadU16(in, off, &x);
    off = ReadI16(in, off, &y);
    (void)ReadU16(in, off, &z);
    return (Vector3){DequantizeCoordXZ(x), DequantizeCoordY(y), DequantizeCoordXZ(z)};
}

// --- Sequence helpers --------------------------------------------------------

uint16_t Net_NextSeq(uint16_t seq)
{
    seq++;
    return (seq == 0) ? 1 : seq;
}

static bool SeqNewer(uint16_t a, uint16_t b)
{
    if (b == 0)
        return a != 0;
    return (a != b) && ((uint16_t)(a - b) < 32768u);
}

void Net_SnapHistoryReset(NetSnapHistory *hist)
{
    if (hist != NULL)
        memset(hist, 0, sizeof(*hist));
}

static const NetSnapFrame *FindSnapFrame(const NetSnapHistory *hist, uint16_t seq)
{
    if (hist == NULL || seq == 0)
        return NULL;
    for (int i = 0; i < NET_SNAP_HIST_SIZE; i++)
    {
        if (hist->frames[i].seq == seq)
            return &hist->frames[i];
    }
    return NULL;
}

// --- PlayerIntent (client -> host, variable 6..22B) --------------------------
// Header (6B): [magic][kind][ver][flags][ackSnapSeq 2B]
//   flags: bit0=jump, bit1=dash, bit2=meditate, bit3=hasClientPose,
//          bit4=clientInAir, bit5=clientDashing, bit6=hasMove, bit7=hasAction
// Optional:
//   hasMove:       int8 moveX, int8 moveY (2B)
//   hasAction:     uint8 actionPacked + quantized aimXYZ (1B + 6B = 7B)
//   hasClientPose: quantized posXYZ + uint8 yaw (6B + 1B = 7B)
#define INTENT_HEADER_BYTES 6
#define INTENT_MAX_BYTES 22

int Net_PackIntentEx(const PlayerIntent *in, uint16_t ackSnapSeq,
                     unsigned char *buf, int maxBytes)
{
    if (in == NULL || buf == NULL || maxBytes < INTENT_MAX_BYTES)
        return 0;

    bool hasMove = (isfinite(in->moveDir.x) && fabsf(in->moveDir.x) > 0.001f) ||
                   (isfinite(in->moveDir.y) && fabsf(in->moveDir.y) > 0.001f);
    bool hasAction = (in->castSkillSlot >= 0 || in->basicAttack > 0);

    int off = 0;
    buf[off++] = NET_MAGIC;
    buf[off++] = NET_KIND_INTENT;
    buf[off++] = NET_PROTOCOL_VERSION;
    buf[off++] = (unsigned char)((in->jump ? 1 : 0) |
                                 (in->dash ? 2 : 0) |
                                 (in->meditate ? 4 : 0) |
                                 (in->hasClientPose ? 8 : 0) |
                                 (in->clientInAir ? 16 : 0) |
                                 (in->clientDashing ? 32 : 0) |
                                 (hasMove ? 64 : 0) |
                                 (hasAction ? 128 : 0));
    off = WriteU16(buf, off, ackSnapSeq);

    if (hasMove)
    {
        buf[off++] = (unsigned char)QuantizeDirAxis(in->moveDir.x);
        buf[off++] = (unsigned char)QuantizeDirAxis(in->moveDir.y);
    }
    if (hasAction)
    {
        unsigned int slotCode = (in->castSkillSlot >= 0 && in->castSkillSlot < 15)
                                    ? (unsigned int)(in->castSkillSlot + 1)
                                    : 0u;
        unsigned int atkCode = (in->basicAttack > 0 && in->basicAttack < 16)
                                   ? (unsigned int)in->basicAttack
                                   : 0u;
        buf[off++] = (unsigned char)((slotCode & 0x0Fu) | ((atkCode & 0x0Fu) << 4));
        Net_PackVec3Quantized(in->aimPoint, buf + off);
        off += 6;
    }
    if (in->hasClientPose)
    {
        Net_PackVec3Quantized(in->clientPos, buf + off);
        off += 6;
        buf[off++] = QuantizeYaw(in->clientYaw);
    }
    return off;
}

bool Net_UnpackIntentEx(PlayerIntent *out, uint16_t *outAckSnapSeq,
                        const unsigned char *buf, int len)
{
    if (out == NULL || buf == NULL || len < INTENT_HEADER_BYTES)
        return false;
    if (buf[0] != NET_MAGIC || buf[1] != NET_KIND_INTENT || buf[2] != NET_PROTOCOL_VERSION)
        return false;

    memset(out, 0, sizeof(*out));
    out->castSkillSlot = -1;

    unsigned char flags = buf[3];
    out->jump = (flags & 1) != 0;
    out->dash = (flags & 2) != 0;
    out->meditate = (flags & 4) != 0;
    out->hasClientPose = (flags & 8) != 0;
    out->clientInAir = (flags & 16) != 0;
    out->clientDashing = (flags & 32) != 0;
    bool hasMove = (flags & 64) != 0;
    bool hasAction = (flags & 128) != 0;

    int expected = INTENT_HEADER_BYTES + (hasMove ? 2 : 0) +
                   (hasAction ? 7 : 0) + (out->hasClientPose ? 7 : 0);
    if (len < expected)
        return false;

    uint16_t ackSeq = 0;
    int off = ReadU16(buf, 4, &ackSeq);
    if (outAckSnapSeq != NULL)
        *outAckSnapSeq = ackSeq;

    if (hasMove)
    {
        out->moveDir.x = DequantizeDirAxis((int8_t)buf[off++]);
        out->moveDir.y = DequantizeDirAxis((int8_t)buf[off++]);
    }
    if (hasAction)
    {
        unsigned char act = buf[off++];
        unsigned int slotCode = act & 0x0Fu;
        unsigned int atkCode = (act >> 4) & 0x0Fu;
        out->castSkillSlot = (slotCode > 0) ? (int)(slotCode - 1) : -1;
        out->basicAttack = (int)atkCode;
        out->aimPoint = Net_UnpackVec3Quantized(buf + off);
        off += 6;
    }
    if (out->hasClientPose)
    {
        out->clientPos = Net_UnpackVec3Quantized(buf + off);
        off += 6;
        out->clientYaw = DequantizeYaw(buf[off++]);
    }
    return true;
}

int Net_PackIntent(const PlayerIntent *in, unsigned char *buf, int maxBytes)
{
    return Net_PackIntentEx(in, 0, buf, maxBytes);
}

bool Net_UnpackIntent(PlayerIntent *out, const unsigned char *buf, int len)
{
    return Net_UnpackIntentEx(out, NULL, buf, len);
}

// --- Agent quantization helpers ----------------------------------------------

static void QuantizeAgent(const Agent *a, NetPackedAgent *dst)
{
    uint16_t hp = QuantizeStat(a->health);
    uint16_t mhp = QuantizeStat(a->maxHealth);
    uint16_t mp = QuantizeStat(a->mana);
    uint16_t mmp = QuantizeStat(a->maxMana);
    bool hasMana = (mp > 0 || mmp > 0);

    unsigned char meta = (unsigned char)(((a->team & 0x3) << 0) |
                                         ((a->archetype & 0x3) << 2) |
                                         ((a->currentElement & 0x7) << 4) |
                                         (hasMana ? 0x80 : 0x00));
    unsigned char flags;
    if (a->archetype == ARCH_MINION)
    {
        flags = a->netMonsterMeta;
    }
    else
    {
        flags = (unsigned char)((a->taijiActive ? 1 : 0) |
                                (a->isMeditating ? 2 : 0) |
                                (a->isStealthed ? 4 : 0) |
                                (a->duelFlag ? 8 : 0) |
                                (((unsigned int)a->pkMode & 0x3u) << 4));
    }

    float yawRad = a->facingYaw;
    // Only fall back to velocity heading for minions whose facingYaw is unset;
    // heroes legitimately face 0.0 rad without velocity overriding them.
    if (a->archetype == ARCH_MINION && fabsf(yawRad) < 0.0001f &&
        (fabsf(a->velocity.x) > 0.001f || fabsf(a->velocity.z) > 0.001f))
    {
        yawRad = atan2f(a->velocity.x, a->velocity.z);
    }

    dst->px = QuantizeCoordXZ(a->position.x);
    dst->py = QuantizeCoordY(a->position.y);
    dst->pz = QuantizeCoordXZ(a->position.z);
    dst->hp = hp;
    dst->mhp = mhp;
    dst->mp = hasMana ? mp : 0;
    dst->mmp = hasMana ? mmp : 0;
    dst->meta = meta;
    dst->flags = flags;
    dst->yaw = QuantizeYaw(yawRad);
    dst->present = 1;
}

static void DequantizeAgent(int id, const NetPackedAgent *src, NetAgentState *dst)
{
    bool hasMana = (src->meta & 0x80) != 0;
    dst->agentId = (unsigned char)id;
    dst->team = (src->meta >> 0) & 0x3;
    dst->archetype = (src->meta >> 2) & 0x3;
    dst->element = (src->meta >> 4) & 0x7;
    dst->flags = src->flags;
    dst->yaw = DequantizeYaw(src->yaw);
    dst->position = (Vector3){
        DequantizeCoordXZ(src->px),
        DequantizeCoordY(src->py),
        DequantizeCoordXZ(src->pz)};
    dst->health = (float)src->hp;
    dst->maxHealth = (float)src->mhp;
    dst->mana = hasMana ? (float)src->mp : 0.0f;
    dst->maxMana = hasMana ? (float)src->mmp : 0.0f;
}

// --- World Capture (once per host tick, shared across all peers) -------------

#define SNAP_MAX_SINGLE_PKT_AGENTS 64
#define DELTA_HEADER_BYTES 9

void Net_CaptureWorldSnapshot(NetWorldSnap *out)
{
    if (out == NULL)
        return;
    out->heroCount = 0;
    out->minionCount = 0;

    for (int i = 0; i < MAX_AGENTS; i++)
    {
        const Agent *a = Entity_GetAgent(i);
        if (a == NULL)
        {
            out->agents[i].present = 0;
            continue;
        }
        QuantizeAgent(a, &out->agents[i]);
        out->posX[i] = a->position.x;
        out->posZ[i] = a->position.z;
        if (a->archetype == ARCH_MINION)
        {
            out->minionIds[out->minionCount++] = (uint8_t)i;
        }
        else
        {
            out->heroIds[out->heroCount++] = (uint8_t)i;
        }
    }
}

static int SelectAgentsForPeer(const NetWorldSnap *world, Vector3 focusPos,
                               float interestRadius, uint8_t *selList,
                               uint8_t selected[MAX_AGENTS])
{
    memset(selected, 0, MAX_AGENTS);
    float rSq = (interestRadius > 0.0f) ? (interestRadius * interestRadius) : -1.0f;
    int cap = (rSq > 0.0f) ? SNAP_MAX_SINGLE_PKT_AGENTS : MAX_AGENTS;
    if (cap > 255)
        cap = 255;

    int selCount = 0;
    // Pass 1: always include heroes & bosses first so they are never culled.
    for (int k = 0; k < world->heroCount && selCount < cap; k++)
    {
        int id = world->heroIds[k];
        selected[id] = 1;
        selList[selCount++] = (uint8_t)id;
    }

    // Pass 2: include closest & actively attacking minions up to budget.
    if (selCount < cap && world->minionCount > 0)
    {
        if (rSq <= 0.0f)
        {
            for (int k = 0; k < world->minionCount && selCount < cap; k++)
            {
                int id = world->minionIds[k];
                selected[id] = 1;
                selList[selCount++] = (uint8_t)id;
            }
        }
        else
        {
            int slotsLeft = cap - selCount;
            if (slotsLeft > SNAP_MAX_SINGLE_PKT_AGENTS)
                slotsLeft = SNAP_MAX_SINGLE_PKT_AGENTS;
            uint8_t mId[SNAP_MAX_SINGLE_PKT_AGENTS];
            float mDist[SNAP_MAX_SINGLE_PKT_AGENTS];
            int mCount = 0;

            for (int k = 0; k < world->minionCount; k++)
            {
                int id = world->minionIds[k];
                float dx = world->posX[id] - focusPos.x;
                float dz = world->posZ[id] - focusPos.z;
                float d2 = dx * dx + dz * dz;
                if (d2 > rSq)
                    continue;
                unsigned char monState = (world->agents[id].flags >> 2) & 0x7u;
                if (monState == 3u || monState == 4u)
                    d2 *= 0.25f;

                if (mCount < slotsLeft)
                {
                    int pos = mCount++;
                    while (pos > 0 && mDist[pos - 1] > d2)
                    {
                        mId[pos] = mId[pos - 1];
                        mDist[pos] = mDist[pos - 1];
                        pos--;
                    }
                    mId[pos] = (uint8_t)id;
                    mDist[pos] = d2;
                }
                else if (d2 < mDist[slotsLeft - 1])
                {
                    int pos = slotsLeft - 1;
                    while (pos > 0 && mDist[pos - 1] > d2)
                    {
                        mId[pos] = mId[pos - 1];
                        mDist[pos] = mDist[pos - 1];
                        pos--;
                    }
                    mId[pos] = (uint8_t)id;
                    mDist[pos] = d2;
                }
            }
            for (int k = 0; k < mCount; k++)
            {
                int id = mId[k];
                selected[id] = 1;
                selList[selCount++] = (uint8_t)id;
            }
        }
    }
    return selCount;
}

// --- Ack-Based Delta Snapshot Pack/Unpack ------------------------------------

int Net_PackDeltaSnapshotFor(const NetWorldSnap *world, Vector3 focusPos,
                             float interestRadius, NetSnapHistory *peerHist,
                             uint16_t ackSeq, uint16_t nextSeq,
                             unsigned char *buf, int maxBytes)
{
    if (world == NULL || peerHist == NULL || buf == NULL ||
        maxBytes < DELTA_HEADER_BYTES || nextSeq == 0)
    {
        return 0;
    }

    uint8_t selList[MAX_AGENTS];
    uint8_t selected[MAX_AGENTS];
    int selCount = SelectAgentsForPeer(world, focusPos, interestRadius, selList, selected);

    const NetSnapFrame *base = (ackSeq != 0) ? FindSnapFrame(peerHist, ackSeq) : NULL;

    // Pick next ring slot, avoiding clobbering `base` if ackSeq is the oldest slot.
    int slotIdx = peerHist->writeIdx;
    if (base != NULL && &peerHist->frames[slotIdx] == base)
    {
        slotIdx = (slotIdx + 1) % NET_SNAP_HIST_SIZE;
    }
    peerHist->writeIdx = (slotIdx + 1) % NET_SNAP_HIST_SIZE;
    NetSnapFrame *cur = &peerHist->frames[slotIdx];

    if (base != NULL)
    {
        *cur = *base;
    }
    else
    {
        memset(cur, 0, sizeof(*cur));
    }
    cur->seq = nextSeq;

    int off = DELTA_HEADER_BYTES;
    int removedCount = 0;

    // 1. Pack removed agent IDs (present in baseline, absent from current culled set).
    if (base != NULL)
    {
        for (int k = 0; k < (int)base->count; k++)
        {
            int id = base->ids[k];
            if (!selected[id])
            {
                if (removedCount >= 255 || off + 1 > maxBytes)
                    break;
                buf[off++] = (unsigned char)id;
                cur->agents[id].present = 0;
                removedCount++;
            }
        }
    }

    // 2. Pack new or modified agents.
    int changedCount = 0;
    for (int k = 0; k < selCount; k++)
    {
        if (changedCount >= 255)
            break;
        int id = selList[k];
        const NetPackedAgent *nw = &world->agents[id];
        const NetPackedAgent *old = (base != NULL && base->agents[id].present)
                                        ? &base->agents[id]
                                        : NULL;

        unsigned int mask = 0;
        int dpx = 0, dpy = 0, dpz = 0;
        bool hasMana = (nw->meta & 0x80) != 0;

        if (old == NULL)
        {
            mask = DM_NEW | DM_POS_FULL | DM_YAW | DM_FLAGS | DM_HP | DM_STATIC;
            if (hasMana)
                mask |= DM_MP;
        }
        else
        {
            dpx = (int)nw->px - (int)old->px;
            dpy = (int)nw->py - (int)old->py;
            dpz = (int)nw->pz - (int)old->pz;
            if (dpx != 0 || dpy != 0 || dpz != 0)
            {
                if (dpx >= -127 && dpx <= 127 &&
                    dpy >= -127 && dpy <= 127 &&
                    dpz >= -127 && dpz <= 127)
                {
                    mask |= DM_POS_DELTA;
                }
                else
                {
                    mask |= DM_POS_FULL;
                }
            }
            if (nw->yaw != old->yaw)
                mask |= DM_YAW;
            if (nw->flags != old->flags)
                mask |= DM_FLAGS;
            if (nw->hp != old->hp)
                mask |= DM_HP;
            if (hasMana && (nw->mp != old->mp || !(old->meta & 0x80)))
                mask |= DM_MP;
            if (nw->meta != old->meta || nw->mhp != old->mhp ||
                (hasMana && nw->mmp != old->mmp))
            {
                mask |= DM_STATIC;
            }
            if (mask == 0)
                continue; // Unchanged: 0 bytes on the wire!
        }

        int need = 2; // agentId + mask
        if (mask & DM_POS_DELTA)
            need += 3;
        if (mask & DM_POS_FULL)
            need += 6;
        if (mask & DM_YAW)
            need += 1;
        if (mask & DM_FLAGS)
            need += 1;
        if (mask & DM_HP)
            need += 2;
        if (mask & DM_MP)
            need += 2;
        if (mask & DM_STATIC)
            need += hasMana ? 5 : 3;

        if (off + need > maxBytes)
            break; // Defer remaining changes to next tick

        buf[off++] = (unsigned char)id;
        buf[off++] = (unsigned char)mask;
        if (mask & DM_POS_DELTA)
        {
            buf[off++] = (unsigned char)(int8_t)dpx;
            buf[off++] = (unsigned char)(int8_t)dpy;
            buf[off++] = (unsigned char)(int8_t)dpz;
        }
        else if (mask & DM_POS_FULL)
        {
            off = WriteU16(buf, off, nw->px);
            off = WriteI16(buf, off, nw->py);
            off = WriteU16(buf, off, nw->pz);
        }
        if (mask & DM_YAW)
            buf[off++] = nw->yaw;
        if (mask & DM_FLAGS)
            buf[off++] = nw->flags;
        if (mask & DM_HP)
            off = WriteU16(buf, off, nw->hp);
        if (mask & DM_MP)
            off = WriteU16(buf, off, nw->mp);
        if (mask & DM_STATIC)
        {
            buf[off++] = nw->meta;
            off = WriteU16(buf, off, nw->mhp);
            if (hasMana)
                off = WriteU16(buf, off, nw->mmp);
        }

        cur->agents[id] = *nw;
        cur->agents[id].present = 1;
        changedCount++;
    }

    // Rebuild compact active ID index for this baseline frame.
    uint16_t activeCount = 0;
    for (int i = 0; i < MAX_AGENTS; i++)
    {
        if (cur->agents[i].present)
            cur->ids[activeCount++] = (uint8_t)i;
    }
    cur->count = activeCount;
    peerHist->lastSeq = nextSeq;

    buf[0] = NET_MAGIC;
    buf[1] = NET_KIND_DELTA;
    buf[2] = NET_PROTOCOL_VERSION;
    (void)WriteU16(buf, 3, nextSeq);
    (void)WriteU16(buf, 5, base ? base->seq : 0);
    buf[7] = (unsigned char)changedCount;
    buf[8] = (unsigned char)removedCount;
    return off;
}

int Net_UnpackDeltaSnapshot(NetSnapHistory *clientHist, NetAgentState *out,
                            int maxAgents, const unsigned char *buf, int len,
                            uint16_t *outSeq)
{
    if (clientHist == NULL || out == NULL || buf == NULL || len < DELTA_HEADER_BYTES)
        return -1;
    if (buf[0] != NET_MAGIC || buf[1] != NET_KIND_DELTA || buf[2] != NET_PROTOCOL_VERSION)
        return -1;

    uint16_t snapSeq = 0, baseSeq = 0;
    (void)ReadU16(buf, 3, &snapSeq);
    (void)ReadU16(buf, 5, &baseSeq);
    int changedCount = buf[7];
    int removedCount = buf[8];

    if (snapSeq == 0)
        return -1;
    if (clientHist->lastSeq != 0 && !SeqNewer(snapSeq, clientHist->lastSeq))
    {
        return -3; // Out-of-order / duplicate older UDP packet — drop cleanly
    }

    const NetSnapFrame *base = (baseSeq != 0) ? FindSnapFrame(clientHist, baseSeq) : NULL;
    if (baseSeq != 0 && base == NULL)
    {
        // Missing baseline — reset lastSeq so next intent sends ackSnapSeq=0 (keyframe request).
        clientHist->lastSeq = 0;
        return -2;
    }

    int off = DELTA_HEADER_BYTES;
    if (off + removedCount > len)
        return -1;

    // Decode into a scratch frame first so malformed packets never corrupt clientHist.
    static NetSnapFrame s_scratch;
    if (base != NULL)
    {
        s_scratch = *base;
    }
    else
    {
        memset(&s_scratch, 0, sizeof(s_scratch));
    }
    s_scratch.seq = snapSeq;

    for (int k = 0; k < removedCount; k++)
    {
        int id = buf[off++];
        if (id < MAX_AGENTS)
            s_scratch.agents[id].present = 0;
    }

    for (int k = 0; k < changedCount; k++)
    {
        if (off + 2 > len)
            return -1;
        int id = buf[off++];
        unsigned int mask = buf[off++];
        if (id >= MAX_AGENTS)
            return -1;

        NetPackedAgent *dst = &s_scratch.agents[id];
        if (mask & DM_NEW)
        {
            memset(dst, 0, sizeof(*dst));
        }

        if (mask & DM_POS_DELTA)
        {
            if (off + 3 > len)
                return -1;
            int px = (int)dst->px + (int)(int8_t)buf[off++];
            int py = (int)dst->py + (int)(int8_t)buf[off++];
            int pz = (int)dst->pz + (int)(int8_t)buf[off++];
            if (px < 0)
                px = 0;
            else if (px > 65535)
                px = 65535;
            if (py < -32768)
                py = -32768;
            else if (py > 32767)
                py = 32767;
            if (pz < 0)
                pz = 0;
            else if (pz > 65535)
                pz = 65535;
            dst->px = (uint16_t)px;
            dst->py = (int16_t)py;
            dst->pz = (uint16_t)pz;
        }
        else if (mask & DM_POS_FULL)
        {
            if (off + 6 > len)
                return -1;
            off = ReadU16(buf, off, &dst->px);
            off = ReadI16(buf, off, &dst->py);
            off = ReadU16(buf, off, &dst->pz);
        }
        if (mask & DM_YAW)
        {
            if (off + 1 > len)
                return -1;
            dst->yaw = buf[off++];
        }
        if (mask & DM_FLAGS)
        {
            if (off + 1 > len)
                return -1;
            dst->flags = buf[off++];
        }
        if (mask & DM_HP)
        {
            if (off + 2 > len)
                return -1;
            off = ReadU16(buf, off, &dst->hp);
        }
        if (mask & DM_MP)
        {
            if (off + 2 > len)
                return -1;
            off = ReadU16(buf, off, &dst->mp);
        }
        if (mask & DM_STATIC)
        {
            if (off + 3 > len)
                return -1;
            dst->meta = buf[off++];
            off = ReadU16(buf, off, &dst->mhp);
            if (dst->meta & 0x80)
            {
                if (off + 2 > len)
                    return -1;
                off = ReadU16(buf, off, &dst->mmp);
            }
            else
            {
                dst->mp = 0;
                dst->mmp = 0;
            }
        }
        dst->present = 1;
    }

    // Commit valid frame into client history ring.
    int slotIdx = clientHist->writeIdx;
    if (base != NULL && &clientHist->frames[slotIdx] == base)
    {
        slotIdx = (slotIdx + 1) % NET_SNAP_HIST_SIZE;
    }
    clientHist->writeIdx = (slotIdx + 1) % NET_SNAP_HIST_SIZE;

    uint16_t activeCount = 0;
    int decoded = 0;
    for (int i = 0; i < MAX_AGENTS; i++)
    {
        if (!s_scratch.agents[i].present)
            continue;
        s_scratch.ids[activeCount++] = (uint8_t)i;
        if (decoded < maxAgents)
        {
            DequantizeAgent(i, &s_scratch.agents[i], &out[decoded++]);
        }
    }
    s_scratch.count = activeCount;
    clientHist->frames[slotIdx] = s_scratch;
    clientHist->lastSeq = snapSeq;
    if (outSeq != NULL)
        *outSeq = snapSeq;
    return decoded;
}

// --- Legacy Stateless Full Snapshot (kept for loopback/tests) ----------------

#define SNAP_AGENT_MAX_BYTES 18
#define SNAP_HEADER_BYTES 4

static int PackOnePackedAgent(int id, const NetPackedAgent *p,
                              unsigned char *buf, int off)
{
    bool hasMana = (p->meta & 0x80) != 0;
    buf[off++] = (unsigned char)id;
    buf[off++] = p->meta;
    buf[off++] = p->flags;
    buf[off++] = p->yaw;
    off = WriteU16(buf, off, p->px);
    off = WriteI16(buf, off, p->py);
    off = WriteU16(buf, off, p->pz);
    off = WriteU16(buf, off, p->hp);
    off = WriteU16(buf, off, p->mhp);
    if (hasMana)
    {
        off = WriteU16(buf, off, p->mp);
        off = WriteU16(buf, off, p->mmp);
    }
    return off;
}

int Net_PackAgentSnapshotFor(Vector3 focusPos, float interestRadius,
                             unsigned char *buf, int maxBytes)
{
    if (buf == NULL || maxBytes < SNAP_HEADER_BYTES)
        return 0;

    NetWorldSnap world;
    Net_CaptureWorldSnapshot(&world);

    uint8_t selList[MAX_AGENTS];
    uint8_t selected[MAX_AGENTS];
    int selCount = SelectAgentsForPeer(&world, focusPos, interestRadius, selList, selected);

    int off = SNAP_HEADER_BYTES;
    int count = 0;
    for (int k = 0; k < selCount && count < 255; k++)
    {
        if (off + SNAP_AGENT_MAX_BYTES > maxBytes)
            break;
        int id = selList[k];
        off = PackOnePackedAgent(id, &world.agents[id], buf, off);
        count++;
    }

    buf[0] = NET_MAGIC;
    buf[1] = NET_KIND_SNAP;
    buf[2] = NET_PROTOCOL_VERSION;
    buf[3] = (unsigned char)count;
    return off;
}

int Net_PackAgentSnapshot(unsigned char *buf, int maxBytes)
{
    return Net_PackAgentSnapshotFor((Vector3){0}, 0.0f, buf, maxBytes);
}

int Net_UnpackAgentSnapshot(NetAgentState *out, int maxAgents,
                            const unsigned char *buf, int len)
{
    if (out == NULL || buf == NULL || len < SNAP_HEADER_BYTES)
        return -1;
    if (buf[0] != NET_MAGIC || buf[1] != NET_KIND_SNAP || buf[2] != NET_PROTOCOL_VERSION)
        return -1;
    int count = buf[3];
    int off = SNAP_HEADER_BYTES;
    int decoded = 0;

    for (int k = 0; k < count; k++)
    {
        if (off + 14 > len)
            return -1;
        unsigned char agentId = buf[off++];
        unsigned char meta = buf[off++];
        bool hasMana = (meta & 0x80) != 0;
        int needExtra = hasMana ? 4 : 0;
        if (off + 12 + needExtra > len)
            return -1;

        NetPackedAgent p = {0};
        p.meta = meta;
        p.flags = buf[off++];
        p.yaw = buf[off++];
        off = ReadU16(buf, off, &p.px);
        off = ReadI16(buf, off, &p.py);
        off = ReadU16(buf, off, &p.pz);
        off = ReadU16(buf, off, &p.hp);
        off = ReadU16(buf, off, &p.mhp);
        if (hasMana)
        {
            off = ReadU16(buf, off, &p.mp);
            off = ReadU16(buf, off, &p.mmp);
        }
        p.present = 1;

        if (decoded < maxAgents)
        {
            DequantizeAgent(agentId, &p, &out[decoded++]);
        }
    }
    return decoded;
}

// --- Room roster (host -> clients, reliable) ---
#define ROSTER_HEADER_BYTES 4
#define ROSTER_ENTRY_BYTES 4

int Net_PackRoster(const NetRosterEntry *entries, int count,
                   unsigned char *buf, int maxBytes)
{
    if (entries == NULL || buf == NULL || count < 0 || count > NET_MAX_PLAYERS)
        return 0;
    if (maxBytes < ROSTER_HEADER_BYTES + count * ROSTER_ENTRY_BYTES)
        return 0;
    int off = 0;
    buf[off++] = NET_MAGIC;
    buf[off++] = NET_KIND_ROSTER;
    buf[off++] = NET_PROTOCOL_VERSION;
    buf[off++] = (unsigned char)count;
    for (int i = 0; i < count; i++)
    {
        buf[off++] = entries[i].slot;
        buf[off++] = entries[i].team;
        buf[off++] = entries[i].agentId;
        buf[off++] = entries[i].flags;
    }
    return off;
}

int Net_UnpackRoster(NetRosterEntry *out, int maxEntries,
                     const unsigned char *buf, int len)
{
    if (out == NULL || buf == NULL || len < ROSTER_HEADER_BYTES)
        return -1;
    if (buf[0] != NET_MAGIC || buf[1] != NET_KIND_ROSTER || buf[2] != NET_PROTOCOL_VERSION)
        return -1;
    int count = buf[3];
    if (count > NET_MAX_PLAYERS || len < ROSTER_HEADER_BYTES + count * ROSTER_ENTRY_BYTES)
        return -1;
    int n = (count < maxEntries) ? count : maxEntries;
    int off = ROSTER_HEADER_BYTES;
    for (int i = 0; i < n; i++)
    {
        out[i].slot = buf[off++];
        out[i].team = buf[off++];
        out[i].agentId = buf[off++];
        out[i].flags = buf[off++];
    }
    return n;
}