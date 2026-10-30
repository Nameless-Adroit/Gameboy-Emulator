#ifndef SDL_AUDIO_H
#define SDL_AUDIO_H
#include <SDL.h>
#include <stdbool.h>
#include "apu.h"

/* Host queue owns copied PCM only, never core state or the emulation clock. */
typedef struct { SDL_AudioDeviceID device; bool initialized; } SDLAudio;
/* Initialize a fresh instance; false means the runner may continue silently.
 * The instance owns its device and one SDL audio-subsystem reference. */
bool sdl_audio_init(SDLAudio *audio);
/* Main-thread consumer: copy queued core PCM to SDL, or discard it if no
 * device is open. False reports a queue error; never advances emulation. */
bool sdl_audio_pump(SDLAudio *audio, APU *apu);
/* Drop stale host output without resetting APU registers or sample phase. */
void sdl_audio_clear(SDLAudio *audio);
/* Close before the video frontend calls SDL_Quit(); safe to repeat on an
 * initialized or zeroed instance, including after failed initialization. */
void sdl_audio_destroy(SDLAudio *audio);
#endif
