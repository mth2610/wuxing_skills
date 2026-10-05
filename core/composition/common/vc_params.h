#ifndef VC_PARAMS_H
#define VC_PARAMS_H

#include <stdbool.h>
#include <stdio.h>

// ============================================================================
// UNIVERSAL VFX PARAMETER SYSTEM
// General, zero-allocation parameter definition, inspection, and manipulation
// framework for Atomic and Composite VFX across all elements.
// ============================================================================

typedef enum {
    VFX_PARAM_BOOL = 0,
    VFX_PARAM_ENUM,
    VFX_PARAM_INT,
    VFX_PARAM_FLOAT
} VFX_ParamType;

typedef struct {
    const char *name;              // Parameter label, e.g. "Variant", "Style", "Shape", "Active"
    const char *group;             // Component group, e.g. "Vine", "Leaves", "Flower"
    VFX_ParamType type;
    void *valPtr;                  // Direct pointer to the variable in memory
    int minInt;
    int maxInt;                    // Value range for ENUM and INT
    float minFloat;
    float maxFloat;                // Value range for FLOAT
    float stepFloat;               // Increment step for FLOAT
    const char * const *enumNames; // Optional array of display names for enum values
    int enumCount;
} VFX_ParamDef;

// Cycle parameter value forward (triggered by KEY_SLASH '/')
static inline void VFX_Param_CycleNext(const VFX_ParamDef *p)
{
    if (!p || !p->valPtr) return;
    switch (p->type)
    {
        case VFX_PARAM_BOOL:
            *(bool*)p->valPtr = !(*(bool*)p->valPtr);
            break;
        case VFX_PARAM_ENUM:
        case VFX_PARAM_INT: {
            int v = *(int*)p->valPtr + 1;
            if (v > p->maxInt) v = p->minInt;
            *(int*)p->valPtr = v;
            break;
        }
        case VFX_PARAM_FLOAT: {
            float v = *(float*)p->valPtr + p->stepFloat;
            if (v > p->maxFloat + 0.0001f) v = p->minFloat;
            *(float*)p->valPtr = v;
            break;
        }
    }
}

// Cycle parameter value backward (triggered by KEY_COMMA ',')
static inline void VFX_Param_CyclePrev(const VFX_ParamDef *p)
{
    if (!p || !p->valPtr) return;
    switch (p->type)
    {
        case VFX_PARAM_BOOL:
            *(bool*)p->valPtr = !(*(bool*)p->valPtr);
            break;
        case VFX_PARAM_ENUM:
        case VFX_PARAM_INT: {
            int v = *(int*)p->valPtr - 1;
            if (v < p->minInt) v = p->maxInt;
            *(int*)p->valPtr = v;
            break;
        }
        case VFX_PARAM_FLOAT: {
            float v = *(float*)p->valPtr - p->stepFloat;
            if (v < p->minFloat - 0.0001f) v = p->maxFloat;
            *(float*)p->valPtr = v;
            break;
        }
    }
}

// Format the current parameter value into a string for HUD and debug overlays
static inline void VFX_Param_FormatValue(const VFX_ParamDef *p, char *buf, int maxLen)
{
    if (!p || !p->valPtr || !buf || maxLen <= 0) return;
    switch (p->type)
    {
        case VFX_PARAM_BOOL: {
            bool b = *(bool*)p->valPtr;
            snprintf(buf, maxLen, "%s", b ? "ENABLED" : "DISABLED");
            break;
        }
        case VFX_PARAM_ENUM: {
            int e = *(int*)p->valPtr;
            if (p->enumNames && e >= 0 && e < p->enumCount && p->enumNames[e]) {
                snprintf(buf, maxLen, "%s", p->enumNames[e]);
            } else {
                snprintf(buf, maxLen, "%d", e);
            }
            break;
        }
        case VFX_PARAM_INT:
            snprintf(buf, maxLen, "%d", *(int*)p->valPtr);
            break;
        case VFX_PARAM_FLOAT:
            snprintf(buf, maxLen, "%.2f", *(float*)p->valPtr);
            break;
    }
}

#endif // VC_PARAMS_H
