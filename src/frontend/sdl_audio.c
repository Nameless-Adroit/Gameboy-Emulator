#include "sdl_audio.h"
#include <stdio.h>
#include <string.h>

bool sdl_audio_init(SDLAudio *audio)
{
    memset(audio, 0, sizeof(*audio));
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) goto fail;
    /* Track our subsystem reference separately from device ownership so
     * a later open failure releases only resources acquired by this module. */
    audio->initialized = true;
    SDL_AudioSpec wanted;
    SDL_zero(wanted);
    wanted.freq = (int)APU_SAMPLE_RATE;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 1024; /* Device chunk: about 21 ms at 48 kHz. */
    /* No callback thread touches the APU. SDL converts this fixed format to
     * the physical device format internally when the backend requires it. */
    audio->device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
    if (!audio->device) goto fail;
    /* New SDL devices start paused. Queue mode supplies silence whenever
     * playback catches up with the producer; there is no APU callback thread. */
    SDL_PauseAudioDevice(audio->device, 0);
    return true;
fail:
    /* Failure is recoverable for the runner: release partial resources,
     * report the device issue, and let pump discard future PCM silently. */
    fprintf(stderr, "Audio unavailable: %s (continuing silently)\n", SDL_GetError());
    sdl_audio_destroy(audio);
    return false;
}

void sdl_audio_clear(SDLAudio *audio)
{
    /* Useful for STOP and host stalls: this queue is copied presentation
     * data, so clearing it cannot alter emulated channel timing or registers. */
    if (audio->device) SDL_ClearQueuedAudio(audio->device);
}

bool sdl_audio_pump(SDLAudio *audio, APU *apu)
{
    int16_t samples[1024 * 2]; /* 1024 frames, two signed-16 samples each. */
    size_t frames;
    /* Bound latency after a host stall. Audio consumption never paces or
     * advances CPU/PPU time. Missing frames become silence in SDL. */
    /* SDL reports bytes: 48000/5 frames is 200 ms, times four bytes per
     * stereo frame. Clear before adding the next batch to bound stale audio. */
    if (audio->device && SDL_GetQueuedAudioSize(audio->device) > APU_SAMPLE_RATE / 5u * 4u)
        sdl_audio_clear(audio);
    /* Drain even when muted/unavailable, keeping core memory bounded.
     * SDL_QueueAudio copies the stack buffer, so it may be reused immediately.
     * The byte count is frames * two channels * sizeof(int16_t) = frames * 4. */
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
    /* Release device before subsystem; the video/event frontend still owns
     * its other SDL subsystems until its own shutdown. Zeroing makes repeated
     * cleanup harmless and leaves pump usable as a silent consumer. */
    if (audio->device) SDL_CloseAudioDevice(audio->device);
    if (audio->initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    memset(audio, 0, sizeof(*audio));
}
