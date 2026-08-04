#ifndef SANDBOX_COLOUR_PROBE_H
#define SANDBOX_COLOUR_PROBE_H

/* Đo xem một fragment màu ĐÃ BIẾT có tới framebuffer nguyên vẹn không.
 * Xem đầu colour_probe.c: các debug view ghi ra xám mà hiện lên xanh dương,
 * và cho tới khi biết vì sao thì mọi số đọc từ chúng đều vô nghĩa. */
void ColourProbe_Arm(void);      /* phím O trong VFX tester */
void ColourProbe_Draw2D(void);   /* vẽ trong pass 2D */
void ColourProbe_Readback(void); /* đọc pixel + in số, gọi SAU Draw2D */

#endif /* SANDBOX_COLOUR_PROBE_H */
