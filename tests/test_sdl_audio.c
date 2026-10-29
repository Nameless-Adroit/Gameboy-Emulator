#include "sdl_audio.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main(void)
{
    SDLAudio audio; APU apu;
    apu_init(&apu);
    CHECK(sdl_audio_init(&audio));
    SDL_PauseAudioDevice(audio.device, 1); /* Deterministic queue inspection. */
    apu_write(&apu, 0xFF26, 0x80);
    apu_write(&apu, 0xFF24, 0x77); apu_write(&apu, 0xFF25, 0x22);
    apu_write(&apu, 0xFF16, 0x80); apu_write(&apu, 0xFF17, 0xF0);
    apu_write(&apu, 0xFF18, 0); apu_write(&apu, 0xFF19, 0x87);
    apu_tick(&apu, 100000, 0);
    size_t frames = apu.buffered; unsigned phase = apu.sample_phase;
    CHECK(sdl_audio_pump(&audio, &apu));
    CHECK(apu.buffered == 0 && apu.sample_phase == phase);
    CHECK(SDL_GetQueuedAudioSize(audio.device) == frames * 4u);
    CHECK(sdl_audio_pump(&audio, &apu));
    CHECK(SDL_GetQueuedAudioSize(audio.device) == frames * 4u);
    /* Queue growth is bounded even when the host does not consume it. */
    for (unsigned i = 0; i < 12; ++i) {
        apu_tick(&apu, 100000, 0); CHECK(sdl_audio_pump(&audio, &apu));
    }
    CHECK(SDL_GetQueuedAudioSize(audio.device) < APU_SAMPLE_RATE * 4u / 3u);
    sdl_audio_clear(&audio); CHECK(SDL_GetQueuedAudioSize(audio.device) == 0);
    sdl_audio_destroy(&audio); sdl_audio_destroy(&audio);
    apu_tick(&apu, 1000, 0); CHECK(sdl_audio_pump(&audio, &apu));
    CHECK(apu.buffered == 0); /* Silent fallback also drains the core buffer. */
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "gb_intentionally_unavailable", 1) == 0);
    CHECK(!sdl_audio_init(&audio));
    CHECK(!audio.device && !audio.initialized);
    apu_tick(&apu, 1000, 0); CHECK(sdl_audio_pump(&audio, &apu));
    CHECK(apu.buffered == 0);
    sdl_audio_destroy(&audio);
    puts("SDL audio tests passed"); return 0;
}
