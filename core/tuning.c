#include "core/tuning.h"
#include "raylib.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char key[TUNING_MAX_KEY_LEN];
    float *target;
} TuningRegistration;

static TuningRegistration s_registrations[TUNING_MAX_VALUES];
static int s_registrationCount = 0;

static char s_configPath[256] = {0};
static long s_lastModTime = 0;

// Finds "key = value" (or "key=value") at the start of a line in `text` and
// parses the number after '='. Comments/unrelated lines are skipped for
// free: a match only counts if the key is immediately preceded by the start
// of the file or a newline, so "# key = value" (key not at line start)
// never matches. A key that's a prefix of a longer key (e.g. "radius" vs
// "radius2") is also rejected — only whitespace is allowed between the key
// and '='.
static bool FindKeyValue(const char *text, const char *key, float *outValue) {
    size_t keyLen = strlen(key);
    const char *p = text;
    while ((p = strstr(p, key)) != NULL) {
        bool atLineStart = (p == text) || (*(p - 1) == '\n') || (*(p - 1) == '\r');
        const char *after = p + keyLen;
        if (atLineStart) {
            while (*after == ' ' || *after == '\t') after++;
            if (*after == '=') {
                *outValue = (float)atof(after + 1);
                return true;
            }
        }
        p += keyLen;
    }
    return false;
}

static void ApplyConfigToAll(void) {
    if (s_configPath[0] == '\0')
        return;
    char *text = LoadFileText(s_configPath);
    if (text == NULL)
        return; // missing file — leave every registered value as-is
    for (int i = 0; i < s_registrationCount; i++) {
        float parsed;
        if (FindKeyValue(text, s_registrations[i].key, &parsed))
            *s_registrations[i].target = parsed;
    }
    UnloadFileText(text);
}

void Tuning_Init(const char *configPath) {
    snprintf(s_configPath, sizeof(s_configPath), "%s", configPath ? configPath : "");
    s_lastModTime = FileExists(s_configPath) ? GetFileModTime(s_configPath) : 0;
}

bool Tuning_RegisterFloat(const char *key, float *value, float defaultValue) {
    if (s_registrationCount >= TUNING_MAX_VALUES)
        return false;

    TuningRegistration *reg = &s_registrations[s_registrationCount++];
    snprintf(reg->key, sizeof(reg->key), "%s", key);
    reg->target = value;

    *value = defaultValue;
    if (s_configPath[0] != '\0') {
        char *text = LoadFileText(s_configPath);
        if (text != NULL) {
            float parsed;
            if (FindKeyValue(text, key, &parsed))
                *value = parsed;
            UnloadFileText(text);
        }
    }
    return true;
}

void Tuning_Update(void) {
    if (s_configPath[0] == '\0' || !FileExists(s_configPath))
        return;
    long modTime = GetFileModTime(s_configPath);
    if (modTime != s_lastModTime) {
        s_lastModTime = modTime;
        ApplyConfigToAll();
    }
}

void Tuning_Reload(void) {
    if (s_configPath[0] == '\0')
        return;
    s_lastModTime = FileExists(s_configPath) ? GetFileModTime(s_configPath) : 0;
    ApplyConfigToAll();
}
