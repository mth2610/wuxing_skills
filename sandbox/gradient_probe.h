#ifndef SANDBOX_GRADIENT_PROBE_H
#define SANDBOX_GRADIENT_PROBE_H

#include <stdbool.h>

/* Một hình chữ nhật đổ gradient, đi qua ĐÚNG đường ống mà VFX đi qua.
 * Trả lời một câu hỏi duy nhất: dải màu bị "chia mảng" là lỗi của hiệu ứng
 * (ShieldShell) hay của nền tảng? Xem đầu core/shaders/probe_gradient.fs. */
void GradientProbe_Arm(void);
bool GradientProbe_IsActive(void);

void GradientProbe_DrawScene(void);   /* TRONG PostFX_Begin/End — vào target HDR */
void GradientProbe_DrawControl(void); /* SAU PostFX_Draw — chứng (không qua post) */
void GradientProbe_Readback(void);    /* cuối pass 2D, trước EndDrawing */

#endif /* SANDBOX_GRADIENT_PROBE_H */
