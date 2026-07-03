#ifndef TUNING_H
#define TUNING_H

#include <stdbool.h>

#define TUNING_MAX_VALUES 128
#define TUNING_MAX_KEY_LEN 64

// Data-driven tuning (CORE_ISSUES.md Item 9). A skill/core module registers
// a float it wants tunable without recompiling; this module parses a plain
// "key = value" text config once at startup, then re-parses and re-applies
// only when the file's mtime actually changes (Tuning_Update(), call once
// per frame from main.c). No filesystem-watch dependency — a stat() poll is
// enough for a single dev machine.
//
// A key missing from the config file is left alone (keeps its current
// value, does not reset to defaultValue) — hot-reload never clobbers a
// value the file simply doesn't mention (e.g. mid-edit, a temporarily
// deleted line). defaultValue is only ever applied once, at registration.
//
// Caller owns *value and must read it fresh each time it's needed (each
// Cast/frame) rather than baking a copy into other state once — a
// hot-reload updates the registered float in place, but has no way to
// retroactively update anything already derived from its old value.

// Call once at startup with the config file's path (e.g. "tuning.cfg",
// relative to the working directory like every other asset path in this
// project). Safe to call again to point at a different file.
void Tuning_Init(const char *configPath);

// Register a tunable float. `value` is set to `defaultValue` immediately,
// then overwritten if `key` is already present in the config file. Returns
// false (no-op) past TUNING_MAX_VALUES.
bool Tuning_RegisterFloat(const char *key, float *value, float defaultValue);

// Poll the config file's mtime; re-parses and re-applies to all registered
// values only if it changed since the last check (or since Init/last
// Reload). Cheap to call unconditionally once per frame.
void Tuning_Update(void);

// Force an immediate reload regardless of mtime (e.g. a debug hotkey).
void Tuning_Reload(void);

// One-shot load from an arbitrary path (not the registered hot-reload file,
// no per-frame polling). For each keys[i] found in the file, outValues[i] is
// overwritten; keys not found are left untouched (caller should pre-fill
// outValues with defaults before calling). Returns false if the file doesn't
// exist — outValues are left entirely untouched in that case.
bool Tuning_LoadFloatsFromPath(const char *path, const char *const *keys,
                                float *outValues, int count);

// Writes `path` fresh (overwriting any existing file) as a "key = value"
// list, one per line, with the same comment format as tuning.cfg. Returns
// false if the write failed.
bool Tuning_SaveFloats(const char *path, const char *const *keys,
                        const float *values, int count);

#endif // TUNING_H
