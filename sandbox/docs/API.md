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
| `VFXTest_SetPerformanceTelemetry(fps, frameTimeMs, peakFrameMs)` | Supplies inline FPS, frame time and peak frame time in the top toolbar, displayed independently of F3. |
| `VFXTest_SetCameraTelemetry(tiltDegrees, zoomRatio, distanceMeters)` | Supplies actual camera tilt, zoom ratio, and camera-to-target distance (distance is retained as telemetry but omitted from the compact toolbar). The caller derives zoom ratio from baseline distance divided by current distance. |
| `VFXTest_ConsumeCameraTiltStep()` | Returns a queued `-1`, `0`, or `+1` button step, then clears it. The camera owner applies angular step size and pitch bounds. |

Every visible parameter row has previous/decrease and next/increase buttons. Buttons select and edit their own row. Integer and float values clamp to their declared limits; enum and boolean values cycle. Tab/CapsLock selects a parameter and scrolls it into view; `/` edits forward and Shift `/` edits backward.

The inspector scrolls long lists and sizes short lists to their row count. Wheel input over UI belongs to the tester; consumers must consult `VFXTest_IsPointerOverUI()` before applying camera zoom. Button activation occurs on release over the armed rectangle, including Android's below-84-pixel gesture inset. Drag capture lasts until release, preventing a UI gesture from becoming a scene cast.

Fixtures opens the browser; Help exposes demo shortcuts and compact force-test buttons. `<`/`>` preserves authored fixture preset controls. `V` toggles the fixture animation clock; it does not pause particle, gas, or liquid simulation. `U` hides the tester controls.

`SandboxCamera_OrbitPositionAtPitch(playerPosition, yaw, zoomDistance, pitchDegrees)` in [`../sandbox_core.h`](../sandbox_core.h) changes orbit elevation while preserving the legacy camera-to-target radius at each zoom setting. `SandboxCamera_OrbitPosition(...)` retains the default 18-degree pitch for existing callers. Camera tilt buttons participate in the same UI pointer capture as parameter controls.

In the VFX screen, `7` / keypad `7` casts a field-only forward guide from the character's facing direction. The 12-metre path bends slightly and rises from waist height. Its 3 m/s travelling pulse uses a 2.0 m reference radius tapering from 2.40 to 1.70 m, preserved receiver lanes, 1.8 m/s swirl, 0.35 m/s turbulence and a 0.22 N force budget. A 0.25 s attack and 0.8 s fade soften entry/exit; duration derives from path length and speed. Existing free/settled Wood foliage receives the guide force; rooted grass/reeds/flowers receive Newton forces through a mass/spring/damping response with spatial attraction and rebound. Ambient tracers receive the bounded local Wind approximation. The guide expires without a target field or explosion.

`./build/wuxing --render-guide --map verdant_path --origin 28,0,46 --warmup 90 --out guide.png` captures the same field-only cast with the deterministic capture clock. Other capture camera/size flags work as for `--render-vfx`. `--render-vfx 999` at matching settings produces a no-fixture control plate. `WUXING_WIND_RECEIVER_TRACE=1` reports periodic vegetation source and visible/shadow uniform uploads.

## Patch log

| Date | Section | Source | Tier |
|---|---|---|---|
| 2026-10-05 | VFX tester | vfx_test.h; scripts/templates/vfx_test_ui.c.in; generated vfx_test.c | Ground-truth |
