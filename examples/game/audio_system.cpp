/** @file @brief 以独立 AudioEngine 组织程序音效、流式音乐、场景声源和可检查的 WAV 混音。 */
#include "audio_system.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
namespace game_demo {
using namespace renderer;
namespace {
constexpr unsigned rate = 48000;
constexpr double pi = 3.14159265358979323846;
// WAV 按小端写入，使用 filesystem::path 保留 Windows Unicode 路径。
void u16(std::ostream &out, uint16_t value) {
    const char bytes[] = {char(value & 255), char(value >> 8)};
    out.write(bytes, 2);
}
void u32(std::ostream &out, uint32_t value) {
    u16(out, uint16_t(value));
    u16(out, uint16_t(value >> 16));
}
class WaveFile {
    std::ofstream file_;
    uint64_t samples_ = 0;
    unsigned channels_;

  public:
    WaveFile(const std::filesystem::path &path, unsigned channels) : channels_(channels) {
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        file_.open(path, std::ios::binary | std::ios::trunc);
        if (!file_)
            throw std::runtime_error("Cannot open WAV output");
        file_.write("RIFF", 4);
        u32(file_, 36);
        file_.write("WAVEfmt ", 8);
        u32(file_, 16);
        u16(file_, 1);
        u16(file_, uint16_t(channels));
        u32(file_, rate);
        u32(file_, rate * channels * 2);
        u16(file_, uint16_t(channels * 2));
        u16(file_, 16);
        file_.write("data", 4);
        u32(file_, 0);
    }
    void append(std::span<const float> pcm) {
        if (samples_ + pcm.size() > (uint64_t(UINT32_MAX) - 36) / 2)
            throw std::length_error("WAV exceeds RIFF limit");
        for (float sample : pcm)
            u16(file_, uint16_t(int16_t(std::clamp(sample, -1.f, 1.f) * 32767)));
        samples_ += pcm.size();
        if (!file_)
            throw std::runtime_error("Cannot write WAV samples");
    }
    void finish() {
        if (!file_.is_open())
            return;
        if (samples_ % channels_)
            throw std::logic_error("Incomplete WAV frame");
        const auto bytes = uint32_t(samples_ * 2);
        file_.seekp(4);
        u32(file_, 36 + bytes);
        file_.seekp(40);
        u32(file_, bytes);
        file_.close();
        if (!file_)
            throw std::runtime_error("Cannot finalize WAV output");
    }
};
std::vector<float> music(bool night) {
    std::vector<float> pcm(rate * 4);
    const std::array<double, 8> notes =
        night ? std::array<double, 8>{220, 261.63, 329.63, 293.66, 220, 196, 164.81, 196}
              : std::array<double, 8>{261.63, 329.63, 392, 440, 392, 329.63, 293.66, 329.63};
    for (size_t i = 0; i < pcm.size(); ++i) {
        const double t = double(i) / rate, beat = std::fmod(t, .5), f = notes[size_t(t * 2) % 8];
        const double envelope = std::min(beat / .025, 1.0) * std::exp(-beat * 5);
        pcm[i] =
            float(.12 * envelope * (std::sin(2 * pi * f * t) + .25 * std::sin(4 * pi * f * t)));
    }
    return pcm;
}
AudioClip noise(double duration, double frequency, unsigned seed, bool loop = false) {
    std::vector<float> pcm(size_t(duration * rate));
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> random(-1, 1);
    float smooth = 0;
    for (size_t i = 0; i < pcm.size(); ++i) {
        const double t = double(i) / rate;
        smooth = .92f * smooth + .08f * random(rng);
        const double envelope = loop ? 1.0 : std::min(t / .005, 1.0) * std::exp(-t * 16);
        pcm[i] = float(envelope * (.2 * smooth + .06 * std::sin(2 * pi * frequency * t)));
    }
    return AudioClip::fromPcm(pcm, 1, rate);
}
} // namespace
struct AudioSystem::Impl {
    // 音频资源先构造、后析构；播放实例和设备在资源之前停止。
    AudioClip day, night, step, hit, click, fire, wind, rain, bird;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<WaveFile> output;
    std::map<uint64_t, VoiceHandle> ambient;
    VoiceHandle musicVoice, fireVoice, windVoice, rainVoice;
    AudioStats stats;
    double fractionalFrames = 0;
    bool isNight = false, manualNight = false, finished = false;
    Impl(const std::filesystem::path &directory, bool offline, const std::filesystem::path &capture,
         Vec3 firePosition)
        : day(makeMusic(directory, false)), night(makeMusic(directory, true)),
          step(noise(.14, 120, 1)), hit(noise(.25, 72, 2)), click(noise(.12, 880, 3)),
          fire(noise(1, 31, 4, true)), wind(noise(1, 19, 5, true)), rain(noise(1, 47, 6, true)),
          bird(noise(.5, 1100, 7, true)) {
        stats.offline = offline || !capture.empty();
        try {
            engine = std::make_unique<AudioEngine>(AudioEngineDesc{rate, 128, stats.offline});
        } catch (const std::exception &e) {
            if (stats.offline)
                throw;
            std::cerr << "Audio device unavailable, using offline mixer: " << e.what() << '\n';
            stats.offline = true;
            engine = std::make_unique<AudioEngine>(AudioEngineDesc{rate, 128, true});
        }
        if (!capture.empty())
            output = std::make_unique<WaveFile>(capture, 2);
        engine->setBusVolume(AudioBus::Master, .45f);
        engine->setBusVolume(AudioBus::Music, .4f);
        engine->setBusVolume(AudioBus::Ambient, .25f);
        engine->setBusVolume(AudioBus::UI, .45f);
        fireVoice = engine->play(fire, {.bus = AudioBus::Ambient,
                                        .volume = .65f,
                                        .loop = true,
                                        .spatial = true,
                                        .position = firePosition + Vec3{0, .4f, 0},
                                        .minDistance = 1,
                                        .maxDistance = 16,
                                        .priority = 20});
        windVoice = engine->play(wind, {.bus = AudioBus::Ambient, .volume = 0, .loop = true});
        rainVoice = engine->play(rain, {.bus = AudioBus::Ambient, .volume = 0, .loop = true});
        changeMusic(false);
    }
    static AudioClip makeMusic(const std::filesystem::path &directory, bool dark) {
        const auto path = directory / (dark ? "generated-night.wav" : "generated-day.wav");
        if (!std::filesystem::exists(path)) {
            WaveFile wave(path, 1);
            wave.append(music(dark));
            wave.finish();
        }
        return AudioClip::fromFile(path, AudioLoadMode::Stream);
    }
    void changeMusic(bool dark) {
        if (engine->valid(musicVoice))
            engine->stop(musicVoice, .25);
        musicVoice = engine->play(dark ? night : day, {.bus = AudioBus::Music,
                                                       .volume = .55f,
                                                       .loop = true,
                                                       .fadeInSeconds = .25,
                                                       .priority = 100});
        ++stats.musicChanges;
    }
    void effect(const AudioClip &clip, Vec3 position) {
        engine->play(clip, {.volume = .6f,
                            .spatial = true,
                            .position = position,
                            .minDistance = 1,
                            .maxDistance = 18,
                            .priority = 50,
                            .maxInstancesPerClip = 4});
    }
};
AudioSystem::AudioSystem(const std::filesystem::path &directory, bool offline,
                         const std::filesystem::path &capture, Vec3 firePosition)
    : impl_(std::make_unique<Impl>(directory, offline, capture, firePosition)) {}
AudioSystem::~AudioSystem() = default;
void AudioSystem::update(double seconds, Vec3 listener, bool night, float rain, float snow) {
    auto &p = *impl_;
    p.engine->setListener({.position = listener});
    if (p.isNight != night) {
        p.isNight = night;
        p.changeMusic(night != p.manualNight);
    }
    p.engine->setVolume(p.rainVoice, rain * .8f);
    p.engine->setVolume(p.windVoice, .08f + snow * .4f);
    if (p.stats.offline) {
        p.fractionalFrames += seconds * rate;
        auto frames = uint64_t(p.fractionalFrames);
        p.fractionalFrames -= double(frames);
        std::array<float, 1024> pcm{};
        while (frames) {
            const auto count = std::min<uint64_t>(frames, pcm.size() / 2);
            const auto samples = std::span{pcm}.first(size_t(count) * 2);
            p.engine->render(samples);
            for (float sample : samples)
                p.stats.energy += std::abs(sample);
            if (p.output)
                p.output->append(samples);
            p.stats.mixedFrames += count;
            frames -= count;
        }
    }
    p.engine->update();
    p.stats.ended += p.engine->takeEvents().size();
}
void AudioSystem::footstep(Vec3 p) {
    impl_->effect(impl_->step, p);
    ++impl_->stats.footsteps;
}
void AudioSystem::impact(Vec3 p) {
    impl_->effect(impl_->hit, p);
    ++impl_->stats.impacts;
}
void AudioSystem::interact(Vec3 p) {
    impl_->effect(impl_->click, p);
    ++impl_->stats.interactions;
}
void AudioSystem::addAmbient(uint64_t id, Vec3 p) {
    auto &s = *impl_;
    removeAmbient(id);
    s.ambient[id] = s.engine->play(s.bird, {.bus = AudioBus::Ambient,
                                            .volume = .08f,
                                            .loop = true,
                                            .spatial = true,
                                            .position = p,
                                            .minDistance = 1,
                                            .maxDistance = 10,
                                            .fadeInSeconds = .2,
                                            .priority = -10});
    ++s.stats.ambientStarted;
}
void AudioSystem::removeAmbient(uint64_t id) {
    auto &s = *impl_;
    if (auto item = s.ambient.find(id); item != s.ambient.end()) {
        if (s.engine->valid(item->second))
            s.engine->stop(item->second, .1);
        s.ambient.erase(item);
        ++s.stats.ambientStopped;
    }
}
void AudioSystem::toggleMusic() {
    auto &p = *impl_;
    p.engine->setBusMuted(AudioBus::Music, !p.engine->busMuted(AudioBus::Music));
}
void AudioSystem::switchMusic() {
    auto &p = *impl_;
    p.manualNight = !p.manualNight;
    p.changeMusic(p.isNight != p.manualNight);
}
void AudioSystem::setVolume(float volume) {
    impl_->engine->setBusVolume(AudioBus::Master, volume);
}
void AudioSystem::setMusicEnabled(bool enabled) {
    impl_->engine->setBusMuted(AudioBus::Music, !enabled);
}
bool AudioSystem::musicEnabled() const {
    return !impl_->engine->busMuted(AudioBus::Music);
}
void AudioSystem::finish() {
    auto &p = *impl_;
    if (p.finished)
        return;
    p.engine->stopAll();
    p.stats.ended += p.engine->takeEvents().size();
    if (p.output)
        p.output->finish();
    p.finished = true;
}
AudioStats AudioSystem::statistics() const {
    return impl_->stats;
}
} // namespace game_demo
