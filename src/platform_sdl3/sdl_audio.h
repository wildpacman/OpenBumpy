#pragma once

#include <SDL3/SDL.h>

#include "audio/audio_engine.h"

#include <vector>

namespace bumpy {

// RAII wrapper around an SDL3 audio stream that pulls mixed samples from an
// AudioEngine. Opens a mono float32 stream at AudioEngine::kSampleRate on the
// default playback device and drives it from a pull callback (the audio
// thread calls back into `engine.render`); closes/destroys the stream on
// destruction. `engine` must outlive this object.
class SdlAudio {
public:
    explicit SdlAudio(AudioEngine& engine);
    ~SdlAudio();

    SdlAudio(const SdlAudio&) = delete;
    SdlAudio& operator=(const SdlAudio&) = delete;

    // Web build: top up the audio queue from the run loop. A no-op on desktop, where
    // SDL's audio thread pulls through callback(). Safe (and cheap) to call every frame.
    void pump();

    // Web build only. `target_ms` is how much already-rendered audio pump() keeps queued,
    // and it is a floor on how late an SFX can be heard: a sound triggered this frame is
    // rendered *behind* everything already in the queue, so it cannot play sooner than the
    // queue takes to drain. Desktop has no equivalent -- SDL's callback pulls just in time.
    // TEMPORARY setter, for measuring how much of the observed lag is this and how much is
    // the browser's own output latency.
    [[nodiscard]] int queued_ms() const;
    [[nodiscard]] int target_ms() const noexcept { return target_ms_; }
    void set_target_ms(int ms) noexcept;

private:
    static void callback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                         int total_amount);

    AudioEngine& engine_;
    SDL_AudioStream* stream_{};
    // Scratch render buffer, reused across callback()/pump() invocations. Desktop drives
    // callback() from SDL's audio thread; the web build drives pump() from the run loop
    // instead. Exactly one of the two is ever active for a given build (the other compiles
    // to nothing), so only one thread ever touches scratch_ and no locking is needed.
    std::vector<float> scratch_;
    int target_ms_{100};
};

}  // namespace bumpy
