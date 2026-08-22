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

int SdlAudio::queued_ms() const {
#ifdef __EMSCRIPTEN__
    if (stream_) {
        const int queued = SDL_GetAudioStreamQueued(stream_);
        if (queued > 0) {
            return queued / static_cast<int>(sizeof(float)) * 1000 /
                   static_cast<int>(AudioEngine::kSampleRate);
        }
    }
#endif
    return 0;
}

void SdlAudio::set_target_ms(int ms) noexcept {
    target_ms_ = ms < 10 ? 10 : (ms > 500 ? 500 : ms);
}

void SdlAudio::pump() {
#ifdef __EMSCRIPTEN__
    if (!stream_) {
        return;
    }
    // Hold target_ms_ of audio queued: long enough to ride out browser timer jitter (a
    // yielded tick can overshoot by several milliseconds), short enough that SFX stay in
    // step with what is on screen. pump() runs once per game tick, so the queue must
    // outlast one tick with margin -- 14.3 ms on HARD in a level, 28.5 ms at the half rate.
    const int target_bytes = static_cast<int>(AudioEngine::kSampleRate) * target_ms_ / 1000 *
                             static_cast<int>(sizeof(float));
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
