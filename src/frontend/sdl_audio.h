#ifndef SDL_AUDIO_H
#define SDL_AUDIO_H
#include <SDL.h>
#include <stdbool.h>
#include "apu.h"

/* Host queue owns copied PCM only, never core state or the emulation clock. */
typedef struct { SDL_AudioDeviceID device; bool initialized; } SDLAudio;
bool sdl_audio_init(SDLAudio *audio);
bool sdl_audio_pump(SDLAudio *audio, APU *apu);
void sdl_audio_clear(SDLAudio *audio);
void sdl_audio_destroy(SDLAudio *audio);
#endif
