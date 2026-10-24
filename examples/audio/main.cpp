/** @file @brief 演示程序生成音效、音乐流、交叉淡变与离线混音，只使用 SDK 公共接口。 */
#include "renderer/audio/audio.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

using namespace renderer;
namespace {
AudioClip tone(double frequency) {
    std::vector<float> pcm(48000);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = .08f * std::sin(float(6.283185307179586 * frequency * double(i) / 48000));
    return AudioClip::fromPcm(pcm, 1, 48000);
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
    try {
        bool offline = false;
        std::filesystem::path musicFile;
        for (int i = 1; i < argc; ++i) {
            const std::filesystem::path option = argv[i];
            if (option == "--offline")
                offline = true;
            else if (option == "--music" && i + 1 < argc)
                musicFile = argv[++i];
            else
                throw std::invalid_argument(
                    "Usage: renderer_audio_demo [--offline] [--music file]");
        }
        AudioEngine engine({48000, 16, offline});
        engine.setBusVolume(AudioBus::Master, .5f);
        engine.setBusVolume(AudioBus::Music, .5f);
        const auto music =
            musicFile.empty() ? tone(220) : AudioClip::fromFile(musicFile, AudioLoadMode::Stream);
        const auto nextMusic = tone(330);
        const auto effect = tone(880);
        AudioPlayDesc description;
        description.bus = AudioBus::Music;
        description.loop = true;
        description.fadeInSeconds = .15;
        const auto first = engine.play(music, description);
        std::vector<float> output(480 * 2);
        double energy = 0;
        for (int step = 0; step < 200; ++step) {
            if (step == 50) {
                AudioPlayDesc sfx;
                sfx.spatial = true;
                sfx.position = {2, 0, -1};
                engine.play(effect, sfx);
            }
            if (step == 100) {
                engine.stop(first, .25);
                description.fadeInSeconds = .25;
                engine.play(nextMusic, description);
            }
            if (offline) {
                engine.render(output);
                for (float sample : output)
                    energy += std::abs(sample);
                // 流式文件需要给后台解码任务运行机会；常规设备回调天然按采样时钟推进。
                if (!musicFile.empty())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                engine.update();
            }
        }
        if (offline && energy < 1)
            throw std::runtime_error("Offline mixer produced no audio");
        const auto events = engine.takeEvents();
        engine.stopAll();
        std::cout << "Audio example: " << (offline ? "offline" : "system device")
                  << ", rate=" << engine.sampleRate() << ", end events=" << events.size() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
