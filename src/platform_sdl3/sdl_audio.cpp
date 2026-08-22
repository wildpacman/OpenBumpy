#include "platform_sdl3/sdl_audio.h"

#include <stdexcept>

namespace bumpy {

void SdlAudio::callback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                        int /*total_amount*/) {
    if (additional_amount <= 0) {
        return;
    }
    auto* self = static_cast<SdlAudio*>(userdata);
    const std::size_t frames = static_cast<std::size_t>(additional_amount) / sizeof(float);
    if (frames == 0) {
        return;
    }
    if (self->scratch_.size() < frames) {
        self->scratch_.resize(frames);
    }
    self->engine_.render(self->scratch_.data(), frames);
    SDL_PutAudioStreamData(stream, self->scratch_.data(),
                           static_cast<int>(frames * sizeof(float)));
}

SdlAudio::SdlAudio(AudioEngine& engine) : engine_(engine) {
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = static_cast<int>(AudioEngine::kSampleRate);
#ifdef __EMSCRIPTEN__
    // Push mode: no callback. Asyncify routinely leaves the main stack unwound between
    // ticks, and a JS-driven audio callback re-entering engine_.render() at that moment
    // is a corruption hazard. The run loop calls pump() instead.
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
#else
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlAudio::callback, this);
#endif
    if (!stream_) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        throw std::runtime_error(SDL_GetError());
    }
}

SdlAudio::~SdlAudio() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
    }
}

void SdlAudio::pump() {
#ifdef __EMSCRIPTEN__
    if (!stream_) {
        return;
    }
    // How much already-rendered audio to keep queued. This is not a comfort setting: it
    // is a floor on how late a sound effect can be. pump() renders ahead of the device, so
    // an SFX triggered this frame is mixed *behind* everything already queued and cannot
    // play until the queue drains. Desktop has no equivalent -- SDL's callback pulls just
    // in time -- which is exactly why the web build sounded late and the desktop one did
    // not.
    //
    // 60 ms, chosen by listening rather than by arithmetic (2026-08-22, EASY, in a level).
    // At the original 100 ms the lag was obvious; at 60 ms it was gone; below 60 the queue
    // began to run dry and crackle. The floor makes sense: pump() runs once per game tick,
    // so the queue has to outlast one tick with margin, and a tick is 28.5 ms at the half
    // rate (14.3 ms on HARD). 35 ms is barely one and a quarter half-rate ticks -- one late
    // tick and it underruns. 60 ms is a bit over two, against a worst tick measured at
    // 34 ms.
    //
    // The rest of the latency is the browser's and is not ours to spend: measured at 10 ms
    // of AudioContext base latency plus 40 ms of device output latency. If crackle ever
    // does appear, the fix is not a bigger queue -- it is to call pump() again after each
    // wake inside the yield loop in sdl_app.cpp, which halves the longest unfed gap without
    // costing a millisecond of latency.
    constexpr int target_bytes =
        static_cast<int>(AudioEngine::kSampleRate) * 60 / 1000 * static_cast<int>(sizeof(float));
    const int queued = SDL_GetAudioStreamQueued(stream_);
    if (queued < 0 || queued >= target_bytes) {
        return;
    }
    const std::size_t frames = static_cast<std::size_t>(target_bytes - queued) / sizeof(float);
    if (scratch_.size() < frames) {
        scratch_.resize(frames);
    }
    engine_.render(scratch_.data(), frames);
    SDL_PutAudioStreamData(stream_, scratch_.data(), static_cast<int>(frames * sizeof(float)));
#endif
}

}  // namespace bumpy
