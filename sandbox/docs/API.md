# Sandbox API

## VFX tester

Public declarations: [`../vfx_test.h`](../vfx_test.h). HUD implementation is generated from [`../../scripts/templates/vfx_test_ui.c.in`](../../scripts/templates/vfx_test_ui.c.in) by `scripts/sync_vfx_test.py`.

| Entry point | Contract |
|---|---|
| `VFXTest_IsPointerOverUI()` | Read-only shared-layout hit test. Call before scene camera or pointer input. Returns true over visible controls, open modal panels, or a captured UI drag; hidden UI returns false. |
| `VFXTest_UpdateAndHandleInput(...)` | Updates fixture input. Returns true when Back requests the menu. Consumes UI presses/releases before scene casting. |
| `VFXTest_DrawHUD()` | Draws the compact fixture toolbar, optional inspector, fixture browser, and Help. |
| `VFXTest_SetRenderTarget(index, spawnPos)` | Selects a NEW FX fixture and immediately triggers one-shot effects for deterministic capture. Resolve fixture indices by name with `scripts/vfx_fixture_index.py`. |
| `VFXTest_ShouldHideDebugOverlays()` | Controls optional diagnostic overlays; F3 toggles them. |
| `VFXTest_SetPerformanceTelemetry(fps, frameTimeMs, peakFrameMs)` | Supplies the three performance cards, displayed independently of F3. |
| `VFXTest_SetCameraTelemetry(tiltDegrees, zoomRatio, distanceMeters)` | Supplies actual camera tilt, zoom ratio, and camera-to-target distance. The caller derives zoom ratio from baseline distance divided by current distance. |
| `VFXTest_ConsumeCameraTiltStep()` | Returns a queued `-1`, `0`, or `+1` button step, then clears it. The camera owner applies angular step size and pitch bounds. |

Every visible parameter row has previous/decrease and next/increase buttons. Buttons select and edit their own row. Integer and float values clamp to their declared limits; enum and boolean values cycle. Tab/CapsLock selects a parameter and scrolls it into view; `/` edits forward and Shift `/` edits backward.

The inspector scrolls long lists and sizes short lists to their row count. Wheel input over UI belongs to the tester; consumers must consult `VFXTest_IsPointerOverUI()` before applying camera zoom. Button activation occurs on release over the armed rectangle, including Android's below-84-pixel gesture inset. Drag capture lasts until release, preventing a UI gesture from becoming a scene cast.

Fixtures opens the browser; Help exposes demo shortcuts and compact force-test buttons. `<`/`>` preserves authored fixture preset controls. `V` toggles the fixture animation clock; it does not pause particle, gas, or liquid simulation. `U` hides the tester controls.

`SandboxCamera_OrbitPositionAtPitch(playerPosition, yaw, zoomDistance, pitchDegrees)` in [`../sandbox_core.h`](../sandbox_core.h) changes orbit elevation while preserving the legacy camera-to-target radius at each zoom setting. `SandboxCamera_OrbitPosition(...)` retains the default 18-degree pitch for existing callers. Camera tilt buttons participate in the same UI pointer capture as parameter controls.

## Patch log

| Date | Section | Source | Tier |
|---|---|---|---|
| 2026-10-05 | VFX tester | vfx_test.h; scripts/templates/vfx_test_ui.c.in; generated vfx_test.c | Ground-truth |
