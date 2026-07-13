// core/audio_system.c — see audio_system.h. SFX go through
// ResourceManager's Sound cache (lazy-loaded on first play); the music bed
// is a single streamed track owned here. Missing files degrade to silence.
#include "core/audio_system.h"
#include "core/resource_manager.h"
#include <math.h>
#include <stddef.h>

// Spatial falloff: full volume at the listener, silent past this (meters).
#define AUDIO_SFX_MAX_DIST 26.0f

// One path per SfxId (index-aligned with the enum). Drop the WAVs here and
// they play — no code change. Kept .wav for low-latency one-shots.
static const char *kSfxPath[SFX_COUNT] = {
    [SFX_UI_CLICK]    = "assets/audio/sfx/ui_click.wav",
    [SFX_CAST_WATER]  = "assets/audio/sfx/cast_water.wav",
    [SFX_CAST_WOOD]   = "assets/audio/sfx/cast_wood.wav",
    [SFX_CAST_FIRE]   = "assets/audio/sfx/cast_fire.wav",
    [SFX_CAST_EARTH]  = "assets/audio/sfx/cast_earth.wav",
    [SFX_CAST_METAL]  = "assets/audio/sfx/cast_metal.wav",
    [SFX_CAST_TAIJI]  = "assets/audio/sfx/cast_taiji.wav",
    [SFX_MELEE_HIT]   = "assets/audio/sfx/melee_hit.wav",
    [SFX_SKILL_HIT]   = "assets/audio/sfx/skill_hit.wav",
    [SFX_CLASH]       = "assets/audio/sfx/clash.wav",
    [SFX_EXPLOSION]   = "assets/audio/sfx/explosion.wav",
    [SFX_TAIJI_ENTER] = "assets/audio/sfx/taiji_enter.wav",
    [SFX_RINGOUT]     = "assets/audio/sfx/ringout.wav",
    [SFX_VICTORY]     = "assets/audio/sfx/victory.wav",
    [SFX_DEFEAT]      = "assets/audio/sfx/defeat.wav",
};

static const char *kMusicPath[MUS_COUNT] = {
    [MUS_NONE]        = NULL,
    [MUS_ARENA_NIGHT] = "assets/audio/music/arena_night.ogg",
};

static bool     s_ready = false;      // audio device open
static float    s_master = 0.8f;
static Vector3  s_listener = { 0 };
static MusicId  s_musicId = MUS_NONE;
static Music    s_music = { 0 };
static bool     s_musicLoaded = false;

// Per-SfxId "we already tried and it's missing" flag, so a missing file
// warns once (via the resource manager) and then costs nothing.
static bool  s_sfxMissing[SFX_COUNT] = { false };
static Sound s_sfx[SFX_COUNT];        // cached handles (also live in RM cache)
static bool  s_sfxLoaded[SFX_COUNT] = { false };

void Audio_Init(void) {
    InitAudioDevice();
    s_ready = IsAudioDeviceReady();
    if (!s_ready) {
        TraceLog(LOG_WARNING, "[AUDIO] device failed to open — running silent");
        return;
    }
    SetMasterVolume(s_master);
    for (int i = 0; i < SFX_COUNT; i++) { s_sfxLoaded[i] = false; s_sfxMissing[i] = false; }
    TraceLog(LOG_INFO, "[AUDIO] ready (drop assets under assets/audio/ to hear them)");
}

void Audio_Shutdown(void) {
    if (!s_ready) return;
    Audio_StopMusic();
    // SFX Sounds are owned by ResourceManager's cache — it unloads them.
    CloseAudioDevice();
    s_ready = false;
}

void Audio_Update(float dt) {
    (void)dt;
    if (!s_ready) return;
    if (s_musicLoaded) UpdateMusicStream(s_music);
}

void Audio_SetMasterVolume(float volume01) {
    s_master = (volume01 < 0.0f) ? 0.0f : (volume01 > 1.0f) ? 1.0f : volume01;
    if (s_ready) SetMasterVolume(s_master);
}

void Audio_SetListener(Vector3 pos) { s_listener = pos; }

SfxId Audio_CastSfxForElement(int element) {
    switch (element) {
        case 0: return SFX_CAST_WATER;
        case 1: return SFX_CAST_WOOD;
        case 2: return SFX_CAST_FIRE;
        case 3: return SFX_CAST_EARTH;
        case 4: return SFX_CAST_METAL;
        default: return SFX_CAST_WATER;
    }
}

// Fetch (lazy-load) the Sound for id, or NULL-equivalent if the asset is
// missing. Returns a pointer to a valid, playable Sound or NULL.
static Sound *ResolveSfx(SfxId id) {
    if (id < 0 || id >= SFX_COUNT || s_sfxMissing[id]) return NULL;
    if (!s_sfxLoaded[id]) {
        s_sfx[id] = ResourceManager_LoadSound(kSfxPath[id]);
        s_sfxLoaded[id] = true;
        if (!IsSoundValid(s_sfx[id])) {
            s_sfxMissing[id] = true; // file absent — stop retrying
            return NULL;
        }
    }
    return IsSoundValid(s_sfx[id]) ? &s_sfx[id] : NULL;
}

// Tiny pitch jitter keeps repeated hits/casts from sounding robotic.
static void PlayWithVariation(Sound *s, float volume) {
    SetSoundVolume(*s, volume);
    SetSoundPitch(*s, 1.0f + ((float)GetRandomValue(-6, 6) * 0.01f));
    PlaySound(*s);
}

void Audio_PlaySFX(SfxId id) {
    if (!s_ready) return;
    Sound *s = ResolveSfx(id);
    if (s != NULL) PlayWithVariation(s, 1.0f);
}

void Audio_PlaySFXAt(SfxId id, Vector3 worldPos) {
    if (!s_ready) return;
    Sound *s = ResolveSfx(id);
    if (s == NULL) return;
    float dx = worldPos.x - s_listener.x;
    float dz = worldPos.z - s_listener.z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist >= AUDIO_SFX_MAX_DIST) return;
    float vol = 1.0f - dist / AUDIO_SFX_MAX_DIST; // linear falloff
    // Simple stereo pan from the left/right offset (0 = left, 1 = right).
    float pan = 0.5f - (dx / AUDIO_SFX_MAX_DIST) * 0.5f;
    if (pan < 0.0f) pan = 0.0f; else if (pan > 1.0f) pan = 1.0f;
    SetSoundPan(*s, pan);
    PlayWithVariation(s, vol);
}

void Audio_PlayMusic(MusicId id) {
    if (!s_ready) return;
    if (id == s_musicId && s_musicLoaded) return;
    Audio_StopMusic();
    if (id <= MUS_NONE || id >= MUS_COUNT || kMusicPath[id] == NULL) return;
    s_music = LoadMusicStream(kMusicPath[id]);
    if (!IsMusicValid(s_music)) { // asset missing — stay silent
        TraceLog(LOG_INFO, "[AUDIO] music %s not present yet", kMusicPath[id]);
        return;
    }
    s_music.looping = true;
    s_musicLoaded = true;
    s_musicId = id;
    PlayMusicStream(s_music);
}

void Audio_StopMusic(void) {
    if (s_musicLoaded) {
        StopMusicStream(s_music);
        UnloadMusicStream(s_music);
        s_musicLoaded = false;
    }
    s_musicId = MUS_NONE;
}
