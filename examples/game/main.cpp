/** @file @brief 综合游戏示例的命令行入口，Windows 保留 Unicode 文件路径。 */
#include "application.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
int run(int argc, char **argv) {
    try {
        game_demo::Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&] {
                if (++i >= argc)
                    throw std::invalid_argument("Missing value for " + arg);
                return std::string(argv[i]);
            };
            auto path = [&] {
                const auto s = value();
                return std::filesystem::path(std::u8string(s.begin(), s.end()));
            };
            auto number = [&](unsigned maximum) {
                auto text = value();
                size_t end = 0;
                auto n = std::stoul(text, &end);
                if (end != text.size() || n < 1 || n > maximum)
                    throw std::invalid_argument("Invalid number for " + arg);
                return unsigned(n);
            };
            if (arg == "--backend")
                options.backend = value();
            else if (arg == "--frames")
                options.frames = number(1000000);
            else if (arg == "--width")
                options.width = number(8192);
            else if (arg == "--height")
                options.height = number(8192);
            else if (arg == "--cache")
                options.cache = path();
            else if (arg == "--capture")
                options.capture = path();
            else if (arg == "--audio-capture")
                options.audioCapture = path();
            else if (arg == "--model")
                options.model = path();
            else if (arg == "--hidden")
                options.hidden = true;
            else if (arg == "--offscreen")
                options.offscreen = true;
            else if (arg == "--threaded")
                options.threaded = true;
            else if (arg == "--single-thread")
                options.threaded = false;
            else if (arg == "--offline-audio")
                options.offlineAudio = true;
            else if (arg == "--self-test")
                options.selfTest = true;
            else if (arg == "--fullscreen")
                options.fullscreen = true;
            else if (arg == "--exclusive")
                options.fullscreen = options.exclusive = true;
            else if (arg == "--oit")
                options.oit = true;
            else if (arg == "--no-fxaa")
                options.fxaa = false;
            else if (arg == "--help") {
                std::cout
                    << "renderer_game_demo [--backend auto|opengl|vulkan] [--frames N] "
                       "[--hidden|--offscreen] [--threaded|--single-thread] [--offline-audio] "
                       "[--width N] [--height N] [--cache directory] [--capture file.png] "
                       "[--audio-capture file.wav] [--model file] [--fullscreen|--exclusive] "
                       "[--oit] "
                       "[--no-fxaa] [--self-test]\n"
                       "Controls: WASD move, Q/Z orbit, wheel zoom, left select, right move, E "
                       "interact, "
                       "Space hit, N day/night, R weather, O OIT, F FXAA, H shadows, "
                       "C contact shadows, G grade, M mute music, B music, P pause, T edit, K "
                       "save, "
                       "F11 fullscreen, F12 PNG, Esc quit.\n";
                return 0;
            } else
                throw std::invalid_argument("Unknown option: " + arg);
        }
        if (options.backend != "auto" && options.backend != "opengl" && options.backend != "vulkan")
            throw std::invalid_argument("Invalid backend");
        if ((options.hidden || options.offscreen) && !options.frames)
            throw std::invalid_argument("Hidden/offscreen mode requires --frames");
        if (options.selfTest && options.frames < 240)
            throw std::invalid_argument("Self-test requires at least 240 frames");
        if (options.offscreen && options.fullscreen)
            throw std::invalid_argument("Offscreen mode has no fullscreen window");
        if (options.cache.empty())
            throw std::invalid_argument("Cache directory cannot be empty");
        options.offlineAudio = options.offlineAudio || options.hidden || options.offscreen ||
                               !options.audioCapture.empty();
        game_demo::Application app(options);
        app.run();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> arguments;
    arguments.reserve(size_t(argc));
    for (int i = 0; i < argc; ++i) {
        auto value = std::filesystem::path(argv[i]).u8string();
        arguments.emplace_back(value.begin(), value.end());
    }
    std::vector<char *> pointers;
    for (auto &s : arguments)
        pointers.push_back(s.data());
    return run(argc, pointers.data());
}
#else
int main(int argc, char **argv) {
    return run(argc, argv);
}
#endif
