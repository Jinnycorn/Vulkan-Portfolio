#pragma once

#include <memory>

namespace hlab {

class AudioEngine
{
  public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool initialize();
    void shutdown();

    void play();
    void pause();
    void setVolume(float volume);
    void setMuted(bool muted);

    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isMuted() const;
    [[nodiscard]] float volume() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool available_{false};
    bool playing_{false};
    bool muted_{false};
    float volume_{0.22f};
};

} // namespace hlab
