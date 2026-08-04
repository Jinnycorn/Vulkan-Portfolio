#include "AudioEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <xaudio2.h>
#endif

namespace hlab {

struct AudioEngine::Impl
{
#ifdef _WIN32
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* masteringVoice = nullptr;
    IXAudio2SourceVoice* sourceVoice = nullptr;
    std::vector<std::int16_t> samples;
    bool uninitializeCom = false;
#endif
};

namespace {
#ifdef _WIN32
constexpr std::uint32_t kSampleRate = 48000;
constexpr std::uint16_t kChannels = 2;
constexpr double kLoopSeconds = 16.0;
constexpr double kTau = std::numbers::pi_v<double> * 2.0;

double midiFrequency(int note)
{
    return 440.0 * std::pow(2.0, (double(note) - 69.0) / 12.0);
}

double softSine(double phase)
{
    const double fundamental = std::sin(phase);
    return fundamental + std::sin(phase * 2.0) * 0.16 +
           std::sin(phase * 3.0) * 0.05;
}

std::vector<std::int16_t> buildAmbientLoop()
{
    constexpr std::array<std::array<int, 4>, 4> chords{{
        {{45, 52, 57, 60}},
        {{41, 48, 53, 57}},
        {{48, 55, 60, 64}},
        {{43, 50, 55, 59}},
    }};
    constexpr std::array<int, 8> melody{{69, 72, 76, 72, 67, 71, 74, 71}};

    const std::size_t frameCount =
        static_cast<std::size_t>(double(kSampleRate) * kLoopSeconds);
    std::vector<std::int16_t> output(frameCount * kChannels);

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const double t = double(frame) / double(kSampleRate);
        const double loopEnvelope =
            std::pow(std::sin(std::numbers::pi_v<double> *
                              (double(frame) + 0.5) / double(frameCount)),
                     0.35);
        const int chordIndex = std::min(3, int(t / 4.0));
        const double chordTime = std::fmod(t, 4.0);
        const double chordEnvelope =
            std::min(1.0, chordTime / 0.65) *
            std::min(1.0, (4.0 - chordTime) / 0.80);

        double left = 0.0;
        double right = 0.0;
        for (std::size_t voice = 0; voice < chords[chordIndex].size(); ++voice) {
            const double frequency = midiFrequency(chords[chordIndex][voice]);
            const double drift = std::sin(kTau * (0.045 + voice * 0.009) * t) * 0.0025;
            const double phase = kTau * frequency * (1.0 + drift) * t;
            const double level = (voice == 0 ? 0.10 : 0.058) * chordEnvelope;
            left += softSine(phase + double(voice) * 0.21) * level;
            right += softSine(phase + double(voice) * 0.21 + 0.08) * level;
        }

        const int melodyStep = std::min(7, int(t / 2.0));
        const double melodyTime = std::fmod(t, 2.0);
        const double melodyEnvelope =
            std::exp(-melodyTime * 2.8) * std::min(1.0, melodyTime * 24.0);
        const double melodyFrequency = midiFrequency(melody[melodyStep]);
        const double melodyPhase = kTau * melodyFrequency * t;
        const double bell =
            (std::sin(melodyPhase) + std::sin(melodyPhase * 2.01) * 0.24) *
            melodyEnvelope * 0.038;
        left += bell * 0.82;
        right += bell;

        const double air = std::sin(kTau * 0.075 * t) * 0.009;
        left = std::tanh((left + air) * loopEnvelope);
        right = std::tanh((right - air) * loopEnvelope);

        output[frame * 2] =
            static_cast<std::int16_t>(std::clamp(left, -1.0, 1.0) * 32767.0);
        output[frame * 2 + 1] =
            static_cast<std::int16_t>(std::clamp(right, -1.0, 1.0) * 32767.0);
    }

    return output;
}
#endif
} // namespace

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>())
{
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

bool AudioEngine::initialize()
{
    if (available_) {
        return true;
    }

#ifdef _WIN32
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    impl_->uninitializeCom = SUCCEEDED(comResult);

    if (FAILED(XAudio2Create(&impl_->engine, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        shutdown();
        return false;
    }

    if (FAILED(impl_->engine->CreateMasteringVoice(&impl_->masteringVoice))) {
        shutdown();
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = kChannels;
    format.nSamplesPerSec = kSampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    if (FAILED(impl_->engine->CreateSourceVoice(&impl_->sourceVoice, &format))) {
        shutdown();
        return false;
    }

    impl_->samples = buildAmbientLoop();

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes =
        static_cast<UINT32>(impl_->samples.size() * sizeof(std::int16_t));
    buffer.pAudioData =
        reinterpret_cast<const BYTE*>(impl_->samples.data());
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = XAUDIO2_LOOP_INFINITE;

    if (FAILED(impl_->sourceVoice->SubmitSourceBuffer(&buffer))) {
        shutdown();
        return false;
    }

    available_ = true;
    setVolume(volume_);
    return true;
#else
    return false;
#endif
}

void AudioEngine::shutdown()
{
#ifdef _WIN32
    if (impl_) {
        if (impl_->sourceVoice) {
            impl_->sourceVoice->Stop();
            impl_->sourceVoice->FlushSourceBuffers();
            impl_->sourceVoice->DestroyVoice();
            impl_->sourceVoice = nullptr;
        }
        if (impl_->masteringVoice) {
            impl_->masteringVoice->DestroyVoice();
            impl_->masteringVoice = nullptr;
        }
        if (impl_->engine) {
            impl_->engine->Release();
            impl_->engine = nullptr;
        }
        impl_->samples.clear();
        if (impl_->uninitializeCom) {
            CoUninitialize();
            impl_->uninitializeCom = false;
        }
    }
#endif
    available_ = false;
    playing_ = false;
}

void AudioEngine::play()
{
#ifdef _WIN32
    if (available_ && !playing_ && SUCCEEDED(impl_->sourceVoice->Start())) {
        playing_ = true;
    }
#endif
}

void AudioEngine::pause()
{
#ifdef _WIN32
    if (available_ && playing_) {
        impl_->sourceVoice->Stop();
        playing_ = false;
    }
#endif
}

void AudioEngine::setVolume(float volume)
{
    volume_ = std::clamp(volume, 0.0f, 1.0f);
#ifdef _WIN32
    if (available_) {
        impl_->sourceVoice->SetVolume(muted_ ? 0.0f : volume_);
    }
#endif
}

void AudioEngine::setMuted(bool muted)
{
    muted_ = muted;
    setVolume(volume_);
}

bool AudioEngine::isAvailable() const
{
    return available_;
}

bool AudioEngine::isPlaying() const
{
    return playing_;
}

bool AudioEngine::isMuted() const
{
    return muted_;
}

float AudioEngine::volume() const
{
    return volume_;
}

} // namespace hlab
