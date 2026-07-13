// core/audio_system.h — game audio (SFX + one music bed). Data-driven and
// asset-optional: every event maps to a fixed file path under assets/audio/;
// a missing file is a silent no-op (raylib loads an empty Sound, and
// IsSoundValid gates playback), so the whole game runs before any audio
// asset exists — drop the WAV/OGG in at the documented path and it plays.
//
// Layering: this is a core/ engine service. Gameplay modules that forbid
// VFX/audio includes (entities/, combat/) must NOT call it directly — the
// wiring lives in main.c / game/ / ui/, which poll those modules' events
// (Combat_PeekEvents, AI_PollExplosions, Control_ConsumeCastFired, …) and
// translate them to Audio_* calls. Keep gameplay logic audio-blind.
#ifndef AUDIO_SYSTEM_H
#define AUDIO_SYSTEM_H

#include "raylib.h"

// One entry per game sound event. File path table is in audio_system.c;
// names below say exactly which WAV the user drops in (assets/audio/sfx/).
typedef enum {
    SFX_UI_CLICK = 0,   // menu / lobby button
    SFX_CAST_WATER,     // element-tinted cast whooshes (Vô Hệ / loadout)
    SFX_CAST_WOOD,
    SFX_CAST_FIRE,
    SFX_CAST_EARTH,
    SFX_CAST_METAL,
    SFX_CAST_TAIJI,     // Phong/Lôi
    SFX_MELEE_HIT,      // đấm / đá / chưởng landing
    SFX_SKILL_HIT,      // a projectile / AoE hitting an agent
    SFX_CLASH,          // Đấu Pháp — two skills clashing (khắc chế)
    SFX_EXPLOSION,      // minion self-destruct
    SFX_TAIJI_ENTER,    // sting when a hero enters Cảnh Giới Thái Cực
    SFX_RINGOUT,        // an agent falls off the arena
    SFX_VICTORY,        // match won
    SFX_DEFEAT,         // match lost
    SFX_COUNT
} SfxId;

typedef enum {
    MUS_NONE = 0,
    MUS_ARENA_NIGHT,    // assets/audio/music/arena_night.ogg — looping bed
    MUS_COUNT
} MusicId;

// InitAudioDevice + reset state. Safe to call once after InitWindow. If the
// audio device fails to open, every other call becomes a no-op.
void Audio_Init(void);
void Audio_Shutdown(void);
// Per-frame: streams the active music + nothing else. Call in the main loop.
void Audio_Update(float dt);

void Audio_SetMasterVolume(float volume01);
// World position of the "ears" (usually the local player) for spatial SFX.
void Audio_SetListener(Vector3 pos);

// 2D playback (UI, stingers) — always full volume.
void Audio_PlaySFX(SfxId id);
// 3D playback — volume falls off with distance from the listener; ignored
// past AUDIO_SFX_MAX_DIST. Use for hits/casts/explosions in the arena.
void Audio_PlaySFXAt(SfxId id, Vector3 worldPos);
// Helper: element index (0=Water..4=Metal, per Agent.currentElement) →
// SFX_CAST_*; out-of-range falls back to SFX_CAST_WATER.
SfxId Audio_CastSfxForElement(int element);

// Looping music bed; starts id, stopping whatever was playing. MUS_NONE or
// a missing file just stops/does nothing.
void Audio_PlayMusic(MusicId id);
void Audio_StopMusic(void);

#endif // AUDIO_SYSTEM_H
