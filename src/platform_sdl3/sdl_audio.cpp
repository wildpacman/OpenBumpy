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
    // Hold ~100 ms queued: long enough to ride out browser timer jitter (a yielded tick
    // can overshoot by several milliseconds), short enough that SFX stay in step with
    // what is on screen.
    constexpr int kTargetBytes =
        static_cast<int>(AudioEngine::kSampleRate / 10) * static_cast<int>(sizeof(float));
    const int queued = SDL_GetAudioStreamQueued(stream_);
    if (queued < 0 || queued >= kTargetBytes) {
        return;
    }
    const std::size_t frames = static_cast<std::size_t>(kTargetBytes - queued) / sizeof(float);
    if (scratch_.size() < frames) {
        scratch_.resize(frames);
    }
    engine_.render(scratch_.data(), frames);
    SDL_PutAudioStreamData(stream_, scratch_.data(), static_cast<int>(frames * sizeof(float)));
#endif
}

}  // namespace bumpy
