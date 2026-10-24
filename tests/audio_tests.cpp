/** @file @brief 无需声卡的真实 miniaudio 混音、资源与实例生命周期测试。 */
#include "renderer/audio/audio.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace renderer;
namespace {
void require(bool value, const char *message = "Audio assertion failed") {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void throws(F action) {
    bool caught = false;
    try {
        action();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected an exception");
}
AudioEngine offline(uint32_t maxVoices = 8) {
    return AudioEngine({48000, maxVoices, true});
}
AudioClip constant(size_t frames = 48000, uint32_t rate = 48000) {
    return AudioClip::fromPcm(std::vector<float>(frames, .2f), 1, rate);
}
std::vector<float> mix(AudioEngine &engine, size_t frames = 1024) {
    std::vector<float> samples(frames * 2);
    engine.render(samples);
    for (float sample : samples)
        require(std::isfinite(sample), "Mixer output must be finite");
    return samples;
}
double energy(const std::vector<float> &samples, size_t channel = 0) {
    double sum = 0;
    for (size_t i = channel; i < samples.size(); i += 2)
        sum += std::abs(samples[i]);
    return sum / (samples.size() / 2);
}
// 临时生成 PCM WAV，不引入素材下载或第三方测试库；使用中文路径验证 Windows 宽字符 IO。
struct WaveFile {
    std::filesystem::path path;
    explicit WaveFile(uint32_t frames = 4800) {
        static std::atomic<unsigned> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               (std::wstring(L"renderer-音频-") + std::to_wstring(stamp) + L"-" +
                std::to_wstring(sequence++) + L".wav");
        std::ofstream stream(path, std::ios::binary);
        auto word = [&](uint32_t value, int bytes) {
            for (int i = 0; i < bytes; ++i)
                stream.put(static_cast<char>((value >> (8 * i)) & 255));
        };
        stream.write("RIFF", 4);
        word(36 + frames * 2, 4);
        stream.write("WAVEfmt ", 8);
        word(16, 4);
        word(1, 2);
        word(1, 2);
        word(48000, 4);
        word(96000, 4);
        word(2, 2);
        word(16, 2);
        stream.write("data", 4);
        word(frames * 2, 4);
        for (uint32_t i = 0; i < frames; ++i)
            word(6553, 2);
        require(bool(stream), "Cannot write temporary WAV");
    }
    ~WaveFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
} // namespace

int main() {
    const std::vector<std::pair<const char *, std::function<void()>>> tests = {
        {"PCM metadata, validation and sharing",
         [] {
             auto clip = constant(24000, 24000);
             require(clip.channels() == 1 && clip.sampleRate() == 24000);
             require(clip.frameCount() == 24000 && clip.duration() == 1);
             auto copy = clip;
             auto moved = std::move(clip);
             require(clip.frameCount() == 0 && moved.duration() == copy.duration());
             throws([] { AudioClip::fromPcm({}, 1, 48000); });
             throws([] { AudioClip::fromPcm(std::vector<float>{1, 2, 3}, 2, 48000); });
             throws([] { AudioClip::fromPcm(std::vector<float>{1}, 0, 48000); });
             throws([] { AudioClip::fromPcm(std::vector<float>{1}, 1, 1); });
             throws([] { AudioClip::fromPcm(std::vector<float>{NAN}, 1, 48000); });
             auto engine = offline();
             throws([&] { engine.play(clip); });
             AudioPlayDesc description;
             description.pitch = 0;
             throws([&] { engine.play(copy, description); });
             description = {};
             description.maxDistance = description.minDistance;
             throws([&] { engine.play(copy, description); });
             throws([] { AudioEngine invalid({48000, 0, true}); });
         }},
        {"overlap, bus gain, mute and restore",
         [] {
             auto engine = offline();
             const auto clip = constant();
             AudioPlayDesc description;
             description.loop = true;
             engine.play(clip, description);
             mix(engine);
             const auto one = energy(mix(engine));
             require(one > .05);
             engine.play(clip, description);
             require(engine.activeVoiceCount() == 2);
             mix(engine);
             require(std::abs(energy(mix(engine)) / one - 2) < .01);
             engine.setBusVolume(AudioBus::Sfx, .5f);
             engine.setBusVolume(AudioBus::Master, .5f);
             mix(engine);
             require(std::abs(energy(mix(engine)) / one - .5) < .01);
             engine.setBusMuted(AudioBus::Sfx, true);
             require(engine.busMuted(AudioBus::Sfx) && engine.busVolume(AudioBus::Sfx) == .5f);
             mix(engine);
             require(energy(mix(engine)) < 1e-6);
             engine.setBusMuted(AudioBus::Sfx, false);
             mix(engine);
             require(std::abs(energy(mix(engine)) / one - .5) < .01);
             engine.setBusMuted(AudioBus::Master, true);
             require(energy(mix(engine)) < 1e-6);
         }},
        {"bus isolation, pan and pitch",
         [] {
             auto engine = offline();
             AudioPlayDesc description;
             description.bus = AudioBus::Music;
             description.pan = -1;
             description.loop = true;
             const auto voice = engine.play(constant(), description);
             engine.setBusMuted(AudioBus::Sfx, true);
             mix(engine);
             auto output = mix(engine);
             require(energy(output, 0) > .05 && energy(output, 1) < 1e-6);
             engine.setVolume(voice, .25f);
             engine.setPitch(voice, 2);
             mix(engine);
             require(energy(mix(engine)) < .06);
             engine.stopAll();
             description = {};
             description.bus = AudioBus::UI;
             description.pitch = 2;
             auto fast = engine.play(constant(4800), description);
             mix(engine, 3000);
             require(!engine.valid(fast), "Pitch must also change playback speed");
         }},
        {"pause preserves cursor and natural completion",
         [] {
             auto engine = offline();
             std::vector<float> pcm(2048, .1f);
             std::fill(pcm.begin() + 1024, pcm.end(), .3f);
             const auto voice = engine.play(AudioClip::fromPcm(pcm, 1, 48000));
             mix(engine, 1024);
             engine.setPaused(voice, true);
             require(engine.state(voice) == AudioVoiceState::Paused,
                     "Pause state was not retained");
             auto pausedOutput = mix(engine);
             // 混音图可能保留一帧已产生的输出；控制生效后不能继续产生采样。
             require(std::all_of(pausedOutput.begin() + 16, pausedOutput.end(),
                                 [](float sample) { return std::abs(sample) < 1e-6f; }) &&
                         engine.valid(voice),
                     "Paused voice must stay silent and valid");
             engine.setPaused(voice, false);
             const auto resumed = mix(engine, 512);
             require(energy(resumed) > .2, "Resume must continue without rewinding");
             mix(engine, 2048);
             require(!engine.valid(voice), "Completed voice was not reclaimed");
             const auto events = engine.takeEvents();
             require(events.size() == 1 && events[0].voice == voice &&
                         events[0].reason == AudioEndReason::Finished,
                     "Expected one natural completion event");
             require(engine.takeEvents().empty());
         }},
        {"fade envelopes and scheduled stop",
         [] {
             auto engine = offline();
             AudioPlayDesc description;
             description.loop = true;
             description.fadeInSeconds = .02;
             const auto voice = engine.play(constant(), description);
             const auto first = energy(mix(engine, 128));
             mix(engine, 1024);
             const auto full = energy(mix(engine, 128));
             require(first < full * .2 && full > .1);
             engine.fadeTo(voice, .25f, .01);
             mix(engine, 1024);
             require(std::abs(energy(mix(engine, 128)) / full - .25) < .02);
             engine.stop(voice, .01);
             require(engine.state(voice) == AudioVoiceState::Stopping);
             throws([&] { engine.setPaused(voice, true); });
             mix(engine, 1024);
             require(!engine.valid(voice));
             require(energy(mix(engine)) < 1e-6);
             auto events = engine.takeEvents();
             require(events.size() == 1 && events[0].reason == AudioEndReason::Stopped);
         }},
        {"world position, listener orientation and distance attenuation",
         [] {
             auto engine = offline();
             AudioPlayDesc description;
             description.loop = true;
             description.spatial = true;
             description.position = {3, 0, -2};
             description.maxDistance = 10;
             const auto voice = engine.play(constant(), description);
             mix(engine);
             const auto right = mix(engine);
             require(energy(right, 1) > energy(right, 0) * 2);
             AudioListenerDesc listener;
             listener.forward = {0, 0, 1};
             engine.setListener(listener);
             mix(engine);
             const auto left = mix(engine);
             require(energy(left, 0) > energy(left, 1) * 2);
             engine.setPosition(voice, {0, 0, 20});
             mix(engine, 4096);
             require(energy(mix(engine)) < 1e-6);
             listener.position = {0, 0, 20};
             engine.setListener(listener);
             mix(engine, 4096);
             require(energy(mix(engine)) > .05);
             listener.up = listener.forward;
             throws([&] { engine.setListener(listener); });
         }},
        {"priority, per-clip limit and stale handles",
         [] {
             auto engine = offline(2);
             auto other = offline();
             const auto clip = constant();
             AudioPlayDesc description;
             description.loop = true;
             description.priority = 10;
             const auto first = engine.play(clip, description);
             const auto second = engine.play(clip, description);
             require(!other.valid(first));
             throws([&] { other.stop(first); });
             description.priority = 0;
             require(!engine.play(clip, description));
             description.priority = 10;
             const auto replacement = engine.play(clip, description);
             require(!engine.valid(first) && engine.valid(second));
             require(first.index == replacement.index &&
                     first.generation != replacement.generation);
             throws([&] { engine.stop(first); });
             auto events = engine.takeEvents();
             require(events.size() == 1 && events[0].reason == AudioEndReason::Stolen);
             engine.stopAll();
             engine.takeEvents();
             description.maxInstancesPerClip = 1;
             const auto limited = engine.play(clip, description);
             engine.play(clip, description);
             require(!engine.valid(limited) && engine.activeVoiceCount() == 1);
             engine.stopAll();
             const auto shortVoice = engine.play(constant(64));
             mix(engine);
             require(!engine.valid(shortVoice),
                     "Voice must retain a temporary clip until completion");
         }},
        {"resampling, stereo, move and thread ownership",
         [] {
             auto engine = offline();
             auto voice = engine.play(constant(2400, 24000));
             mix(engine, 3000);
             require(engine.valid(voice), "Resampling must preserve duration");
             mix(engine, 3000);
             require(!engine.valid(voice));
             std::vector<float> pcm(2048);
             for (size_t i = 0; i < pcm.size(); i += 2) {
                 pcm[i] = .2f;
                 pcm[i + 1] = .05f;
             }
             voice = engine.play(AudioClip::fromPcm(pcm, 2, 48000));
             const auto output = mix(engine, 512);
             require(std::abs(energy(output, 0) / energy(output, 1) - 4) < .01);
             AudioEngine moved(std::move(engine));
             require(moved.valid(voice));
             throws([&] { engine.update(); });
             bool rejected = false;
             std::thread wrong([&] {
                 try {
                     moved.update();
                 } catch (const std::logic_error &) {
                     rejected = true;
                 }
             });
             wrong.join();
             require(rejected);
             std::vector<float> invalid(3);
             throws([&] { moved.render(invalid); });
             throws([&] { moved.setBusVolume(static_cast<AudioBus>(99), 1); });
         }},
        {"stream page refill, looping and music crossfade",
         [] {
             WaveFile file(48000 * 3);
             auto engine = offline();
             const auto clip = AudioClip::fromFile(file.path, AudioLoadMode::Stream);
             AudioPlayDesc music;
             music.bus = AudioBus::Music;
             music.loop = true;
             const auto first = engine.play(clip, music);
             // 跨越两个初始流式页及文件末尾；让后台任务有机会补页，无需真实声卡。
             for (int i = 0; i < 130; ++i) {
                 std::this_thread::sleep_for(std::chrono::milliseconds(1));
                 require(energy(mix(engine, 1600)) > .1, "Streaming refill or loop lost audio");
             }
             require(engine.valid(first) && engine.takeEvents().empty());
             engine.stop(first, .02);
             music.fadeInSeconds = .02;
             const auto second = engine.play(clip, music);
             mix(engine, 4096);
             require(!engine.valid(first) && engine.valid(second));
             const auto events = engine.takeEvents();
             require(events.size() == 1 && events[0].voice == first &&
                         events[0].reason == AudioEndReason::Stopped,
                     "Crossfade must stop only the previous music voice");
         }},
        {"WAV preload, streaming, Unicode and missing stream source",
         [] {
             WaveFile file;
             const auto preload = AudioClip::fromFile(file.path);
             const auto stream = AudioClip::fromFile(file.path, AudioLoadMode::Stream);
             require(stream.mode() == AudioLoadMode::Stream && stream.frameCount() == 4800,
                     "Streaming metadata differs");
             require(std::abs(stream.duration() - .1) < 1e-6, "Streaming duration differs");
             auto engine = offline();
             auto voice = engine.play(preload);
             const auto expected = mix(engine, 1024);
             engine.stop(voice);
             voice = engine.play(stream);
             const auto actual = mix(engine, 1024);
             require(energy(actual) > .1, "Stream must produce real audio samples");
             for (size_t i = 16; i < actual.size(); ++i)
                 require(std::abs(actual[i] - expected[i]) < 1e-6,
                         "Stream and preload PCM output differ after pipeline priming");
             mix(engine, 6000);
             require(!engine.valid(voice), "Completed streaming voice was not reclaimed");
             require(std::filesystem::remove(file.path),
                     "Stream file remained open after completion");
             auto full = offline(1);
             AudioPlayDesc description;
             description.loop = true;
             const auto surviving = full.play(preload, description);
             throws([&] { full.play(stream); });
             require(full.valid(surviving), "Failed stream initialization must not steal a voice");
             require(full.takeEvents().empty(), "Failed stream initialization emitted an event");
             require(energy(mix(full)) > .1, "Preloaded audio must survive file removal");
             throws([&] { AudioClip::fromFile(file.path); });
         }},
    };
    for (const auto &[name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << tests.size() << " audio tests passed\n";
}
