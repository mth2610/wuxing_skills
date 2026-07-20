# net — Landmines

> Distilled, reusable lessons for the **net** module. Format: Symptom → Cause → Rule (`DOC_ARCHITECTURE.md` §6).
> Cross-cutting engine traps live in root `ENGINE_LANDMINES.md`. Backlog/log is in `PROGRESS.md`.

### EOS SDK dylib blocked by Gatekeeper on macOS
- **Symptom:** the app fails to load/link the EOS SDK on macOS (quarantined dylib).
- **Cause:** the downloaded `libEOSSDK-Mac-Shipping.dylib` carries the `com.apple.quarantine` xattr and is unsigned.
- **Rule:** one-time fix per checkout — `xattr -d com.apple.quarantine <dylib>` then ad-hoc `codesign --force --sign - <dylib>` (see `docs/EOS_SETUP.md`).

### EOS setup calls hang on macOS
- **Symptom:** blocking EOS setup/auth calls stall or never complete on macOS.
- **Cause:** the EOS SDK's HTTP stack delivers callbacks on the main `CFRunLoop`; a plain `sleep()` wait starves it.
- **Rule:** blocking setup waits must pump `CFRunLoopRunInMode`, not sleep.
