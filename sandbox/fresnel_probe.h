#ifndef SANDBOX_FRESNEL_PROBE_H
#define SANDBOX_FRESNEL_PROBE_H

#include "raylib.h"

/* Quy ước fresnel của dự án có đúng không — đo bằng số, không bằng mắt.
 * Xem đầu core/shaders/probe_fresnel.fs. */
void FresnelProbe_Arm(void);
void FresnelProbe_Draw3D(Camera3D cam); /* trong pass 3D */
void FresnelProbe_Readback(void);       /* trong pass 2D, sau Draw3D */

#endif /* SANDBOX_FRESNEL_PROBE_H */
