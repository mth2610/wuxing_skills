# Audio Module Landmines

Debugging lessons distilled from the audio engine.

### Missing Audio File Retry Thrashing
- **Symptom:** Framerate stutter when sound events trigger before audio assets exist.
- **Cause:** Repeatedly trying `LoadSound` from disk every frame when the asset is missing.
- **Rule:** Cache a `s_sfxMissing[id]` flag on the first failed load attempt; warn once and never retry disk access for that asset again.

### Hard-coded Volume Machine-Gun Effect
- **Symptom:** Rapid multi-hit combat audio sounds robotic and abrasive.
- **Cause:** Identical pitch on consecutive frames.
- **Rule:** Apply a tiny random pitch jitter (`1.0f + GetRandomValue(-6, 6) * 0.01f`) on every sound play.
