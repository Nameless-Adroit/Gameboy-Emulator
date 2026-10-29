#include "sdl_audio.h"
#include <stdio.h>
#include <string.h>

bool sdl_audio_init(SDLAudio *audio)
{
    memset(audio, 0, sizeof(*audio));
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) goto fail;
    audio->initialized = true;
    SDL_AudioSpec wanted;
    SDL_zero(wanted);
    wanted.freq = (int)APU_SAMPLE_RATE;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 1024;
    /* No callback thread touches the APU. SDL converts this fixed format to
     * the physical device format internally when the backend requires it. */
    audio->device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
    if (!audio->device) goto fail;
    SDL_PauseAudioDevice(audio->device, 0);
    return true;
fail:
    fprintf(stderr, "Audio unavailable: %s (continuing silently)\n", SDL_GetError());
    sdl_audio_destroy(audio);
    return false;
}

void sdl_audio_clear(SDLAudio *audio)
{
    if (audio->device) SDL_ClearQueuedAudio(audio->device);
}

bool sdl_audio_pump(SDLAudio *audio, APU *apu)
{
    int16_t samples[1024 * 2];
    size_t frames;
    /* Bound latency after a host stall. Audio consumption never paces or
     * advances CPU/PPU time. Missing frames become silence in SDL. */
    if (audio->device && SDL_GetQueuedAudioSize(audio->device) > APU_SAMPLE_RATE / 5u * 4u)
        sdl_audio_clear(audio);
    while ((frames = apu_read_samples(apu, samples, 1024)) != 0) {
        if (audio->device && SDL_QueueAudio(audio->device, samples, (Uint32)(frames * 4u)) != 0) {
            fprintf(stderr, "Queue audio: %s\n", SDL_GetError());
            return false;
        }
    }
    return true;
}

void sdl_audio_destroy(SDLAudio *audio)
{
    if (audio->device) SDL_CloseAudioDevice(audio->device);
    if (audio->initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    memset(audio, 0, sizeof(*audio));
}
