// audio/audio_system.c — see audio/audio_system.h. SFX go through
// ResourceManager's Sound cache (lazy-loaded on first play); the music bed
// is a single streamed track owned here. Missing files degrade to silence.
#include "audio/audio_system.h"
#include "core/resource_manager.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AUDIO_MAX_MUSIC_TRACKS 64

static char    *s_musicTracks[AUDIO_MAX_MUSIC_TRACKS] = { 0 };
static int      s_musicTrackCount = 0;
static int      s_lastTrackIdx = -1;

// Spatial falloff: full volume at the listener, silent past this (meters).
#define AUDIO_SFX_MAX_DIST 26.0f
// Voice polyphony per SfxId via raylib LoadSoundAlias (0 extra wave memory).
#define AUDIO_SFX_ALIASES 4
#define AUDIO_SFX_MAX_VARIANTS 6

// One path per SfxId (index-aligned with the enum). Drop the WAVs here and
// they play — no code change. Kept .wav for low-latency one-shots.
static const char *kSfxPath[SFX_COUNT] = {
    [SFX_UI_CLICK]      = "assets/audio/sfx/ui_click.wav",
    [SFX_CAST_WATER]    = "assets/audio/sfx/cast_water.wav",
    [SFX_CAST_WOOD]     = "assets/audio/sfx/cast_wood.wav",
    [SFX_CAST_FIRE]     = "assets/audio/sfx/cast_fire.wav",
    [SFX_CAST_EARTH]    = "assets/audio/sfx/cast_earth.wav",
    [SFX_CAST_METAL]    = "assets/audio/sfx/cast_metal.wav",
    [SFX_CAST_TAIJI]    = "assets/audio/sfx/cast_taiji.wav",
    [SFX_MELEE_HIT]     = "assets/audio/sfx/melee_hit.wav",
    [SFX_SKILL_HIT]     = "assets/audio/sfx/skill_hit.wav",
    [SFX_CLASH]         = "assets/audio/sfx/clash.wav",
    [SFX_EXPLOSION]     = "assets/audio/sfx/explosion.wav",
    [SFX_TAIJI_ENTER]   = "assets/audio/sfx/taiji_enter.wav",
    [SFX_RINGOUT]       = "assets/audio/sfx/ringout.wav",
    [SFX_VICTORY]       = "assets/audio/sfx/victory.wav",
    [SFX_DEFEAT]        = "assets/audio/sfx/defeat.wav",
    [SFX_MARTIAL_SWING] = "assets/audio/sfx/martial_swing.wav",
    [SFX_AI_ALERT]      = "assets/audio/sfx/ai_alert.wav",
    [SFX_AI_CAST]       = "assets/audio/sfx/ai_cast.wav",
    [SFX_AI_THINK]      = "assets/audio/sfx/ai_think.wav",
};

static const char *kMusicPath[MUS_COUNT] = {
    [MUS_NONE]           = NULL,
    [MUS_ARENA_NIGHT]    = "assets/audio/music/arena_night.ogg",
    [MUS_MENU_WUXIA]     = "assets/audio/music/menu_wuxia.ogg",
    [MUS_COMBAT_INTENSE] = "assets/audio/music/combat_intense.ogg",
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
static Sound s_sfx[SFX_COUNT][AUDIO_SFX_MAX_VARIANTS];
static Sound s_sfxAliases[SFX_COUNT][AUDIO_SFX_MAX_VARIANTS][AUDIO_SFX_ALIASES - 1];
static int   s_sfxVariantCount[SFX_COUNT] = { 0 };
static int   s_sfxVoiceCursor[SFX_COUNT] = { 0 };
static bool  s_sfxLoaded[SFX_COUNT] = { false };

static void Audio_ScanMusicDir(void) {
    for (int i = 0; i < s_musicTrackCount; i++) {
        free(s_musicTracks[i]);
        s_musicTracks[i] = NULL;
    }
    s_musicTrackCount = 0;
    s_lastTrackIdx = -1;

    if (!DirectoryExists("assets/audio/music")) return;
    FilePathList files = LoadDirectoryFiles("assets/audio/music");
    for (unsigned int i = 0; i < files.count && s_musicTrackCount < AUDIO_MAX_MUSIC_TRACKS; i++) {
        const char *p = files.paths[i];
        if (!p || FileExists(p) == false) continue;
        if (IsFileExtension(p, ".ogg") || IsFileExtension(p, ".mp3") ||
            IsFileExtension(p, ".wav") || IsFileExtension(p, ".flac")) {
            size_t len = strlen(p) + 1;
            char *copy = (char *)malloc(len);
            if (copy) {
                memcpy(copy, p, len);
                s_musicTracks[s_musicTrackCount++] = copy;
            }
        }
    }
    UnloadDirectoryFiles(files);
    if (s_musicTrackCount > 0) {
        TraceLog(LOG_INFO, "[AUDIO] discovered %d music track(s) in assets/audio/music/", s_musicTrackCount);
    }
}

void Audio_Init(void) {
    InitAudioDevice();
    s_ready = IsAudioDeviceReady();
    if (!s_ready) {
        TraceLog(LOG_WARNING, "[AUDIO] device failed to open — running silent");
        return;
    }
    SetMasterVolume(s_master);
    for (int i = 0; i < SFX_COUNT; i++) {
        s_sfxLoaded[i] = false;
        s_sfxMissing[i] = false;
        s_sfxVoiceCursor[i] = 0;
    }
    srand((unsigned int)time(NULL));
    Audio_ScanMusicDir();
    TraceLog(LOG_INFO, "[AUDIO] ready (drop assets under assets/audio/ to hear them)");
}

void Audio_Shutdown(void) {
    if (!s_ready) return;
    Audio_StopMusic();
    for (int i = 0; i < s_musicTrackCount; i++) {
        free(s_musicTracks[i]);
        s_musicTracks[i] = NULL;
    }
    s_musicTrackCount = 0;
    s_lastTrackIdx = -1;
    for (int i = 0; i < SFX_COUNT; i++) {
        if (!s_sfxLoaded[i] || s_sfxMissing[i]) continue;
        for (int var = 0; var < s_sfxVariantCount[i]; var++) {
            for (int v = 0; v < AUDIO_SFX_ALIASES - 1; v++) {
                if (IsSoundValid(s_sfxAliases[i][var][v])) UnloadSoundAlias(s_sfxAliases[i][var][v]);
            }
        }
        s_sfxVariantCount[i] = 0;
        s_sfxLoaded[i] = false;
    }
    // Base SFX Sounds are owned by ResourceManager's cache — it unloads them.
    CloseAudioDevice();
    s_ready = false;
}

static void Audio_PlayRandomTrack(MusicId id) {
    if (s_musicTrackCount <= 0) return;
    int idx = 0;
    if (s_musicTrackCount > 1) {
        idx = rand() % s_musicTrackCount;
        if (idx == s_lastTrackIdx)
            idx = (idx + 1 + (rand() % (s_musicTrackCount - 1))) % s_musicTrackCount;
    }
    const char *path = s_musicTracks[idx];
    s_music = LoadMusicStream(path);
    if (!IsMusicValid(s_music)) {
        TraceLog(LOG_WARNING, "[AUDIO] failed to stream music %s", path);
        return;
    }
    s_music.looping = (s_musicTrackCount == 1);
    s_musicLoaded = true;
    s_musicId = id;
    s_lastTrackIdx = idx;
    PlayMusicStream(s_music);
    TraceLog(LOG_INFO, "[AUDIO] playing random BGM: %s", path);
}

void Audio_Update(float dt) {
    (void)dt;
    if (!s_ready) return;
    if (s_musicLoaded) {
        UpdateMusicStream(s_music);
        if (s_musicTrackCount > 1 && !IsMusicStreamPlaying(s_music)) {
            MusicId prevId = s_musicId;
            Audio_StopMusic();
            Audio_PlayRandomTrack(prevId);
        }
    }
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

// Fetch (lazy-load) a random variant + round-robin Sound voice for id, or NULL if missing.
static Sound *ResolveSfx(SfxId id) {
    if (id < 0 || id >= SFX_COUNT || s_sfxMissing[id]) return NULL;
    if (!s_sfxLoaded[id]) {
        s_sfxLoaded[id] = true;
        s_sfxVariantCount[id] = 0;
        if (kSfxPath[id] && FileExists(kSfxPath[id])) {
            Sound base = ResourceManager_LoadSound(kSfxPath[id]);
            if (IsSoundValid(base)) {
                s_sfx[id][0] = base;
                for (int v = 0; v < AUDIO_SFX_ALIASES - 1; v++) {
                    s_sfxAliases[id][0][v] = LoadSoundAlias(base);
                }
                s_sfxVariantCount[id] = 1;
            }
        }
        if (kSfxPath[id] && s_sfxVariantCount[id] > 0) {
            const char *dot = strrchr(kSfxPath[id], '.');
            size_t stemLen = dot ? (size_t)(dot - kSfxPath[id]) : strlen(kSfxPath[id]);
            if (stemLen < 220) {
                char varPath[256];
                for (int k = 1; k < AUDIO_SFX_MAX_VARIANTS; k++) {
                    snprintf(varPath, sizeof(varPath), "%.*s_%d.wav", (int)stemLen, kSfxPath[id], k);
                    if (!FileExists(varPath)) break;
                    Sound vs = ResourceManager_LoadSound(varPath);
                    if (IsSoundValid(vs)) {
                        int idx = s_sfxVariantCount[id]++;
                        s_sfx[id][idx] = vs;
                        for (int v = 0; v < AUDIO_SFX_ALIASES - 1; v++) {
                            s_sfxAliases[id][idx][v] = LoadSoundAlias(vs);
                        }
                    }
                }
            }
        }
        if (s_sfxVariantCount[id] == 0) {
            s_sfxMissing[id] = true;
            return NULL;
        }
    }
    int var = (s_sfxVariantCount[id] > 1) ? (rand() % s_sfxVariantCount[id]) : 0;
    int slot = s_sfxVoiceCursor[id];
    s_sfxVoiceCursor[id] = (slot + 1) % AUDIO_SFX_ALIASES;
    Sound *voice = (slot == 0) ? &s_sfx[id][var] : &s_sfxAliases[id][var][slot - 1];
    return IsSoundValid(*voice) ? voice : (IsSoundValid(s_sfx[id][var]) ? &s_sfx[id][var] : NULL);
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
    if (id == MUS_NONE || id >= MUS_COUNT) {
        Audio_StopMusic();
        return;
    }
    if (id == s_musicId && s_musicLoaded) return;
    Audio_StopMusic();
    if (s_musicTrackCount > 0) {
        Audio_PlayRandomTrack(id);
        return;
    }
    if (kMusicPath[id] == NULL) return;
    static bool s_musicMissing[MUS_COUNT] = { false };
    if (s_musicMissing[id]) return;
    s_music = LoadMusicStream(kMusicPath[id]);
    if (!IsMusicValid(s_music)) { // asset missing — stay silent
        s_musicMissing[id] = true;
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
