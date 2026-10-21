# Audio Module API

Public interface of the Audio module (`#include "audio/audio_system.h"`).

## Sound Lifecycle
- `void Audio_Init(void)` — Initializes the audio device and master volume.
- `void Audio_Shutdown(void)` — Stops music and closes the audio device.
- `void Audio_Update(float dt)` — Streams active music frames.

## Playback & Spatial Audio
- `void Audio_PlaySFX(SfxId id)` — Plays a non-spatial one-shot sound with slight pitch variation.
- `void Audio_PlaySFXAt(SfxId id, Vector3 worldPos)` — Plays a 3D positioned sound with distance falloff and stereo panning.
- `void Audio_PlayMusic(MusicId id)` — Starts streaming looped background music.
- `void Audio_StopMusic(void)` — Stops and unloads the current music stream.

## Configuration & Helpers
- `void Audio_SetMasterVolume(float volume01)` — Sets global master volume in $[0.0, 1.0]$.
- `void Audio_SetListener(Vector3 pos)` — Sets the virtual listener/camera position in 3D world space.
- `SfxId Audio_CastSfxForElement(int element)` — Resolves element index (0=Water, 1=Wood, 2=Fire, 3=Earth, 4=Metal) to its corresponding cast SFX ID.
