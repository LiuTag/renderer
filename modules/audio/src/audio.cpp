/** @file @brief 实现资源解码、miniaudio 混音、实例生命周期及应用线程事件队列。 */
#include "renderer/audio/audio.hpp"
#include "miniaudio.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace renderer {
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::invalid_argument(message);
}
void check(ma_result result, const char *operation) {
    if (result != MA_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": " + ma_result_description(result));
}
void checkRate(uint32_t rate) {
    require(rate >= 8000 && rate <= 192000, "Audio sample rate must be in [8000, 192000]");
}
void checkGain(float gain) {
    require(std::isfinite(gain) && gain >= 0, "Audio gain must be finite and non-negative");
}
void checkPitch(float pitch) {
    require(std::isfinite(pitch) && pitch >= .125f && pitch <= 8,
            "Audio pitch must be in [0.125, 8]");
}
size_t busIndex(AudioBus bus) {
    const auto index = static_cast<unsigned>(bus);
    require(index < 5, "Invalid audio bus");
    return index;
}
uint64_t fadeFrames(double seconds, uint32_t rate) {
    require(std::isfinite(seconds) && seconds >= 0,
            "Audio fade time must be finite and non-negative");
    const long double frames = static_cast<long double>(seconds) * rate;
    require(frames < static_cast<long double>(std::numeric_limits<uint64_t>::max() / 2),
            "Audio fade time is too large");
    return static_cast<uint64_t>(frames);
}
struct Decoder {
    ma_decoder value{};
    bool initialized = false;
    ~Decoder() {
        if (initialized)
            ma_decoder_uninit(&value);
    }
    void open(const std::filesystem::path &path) {
        auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
#ifdef _WIN32
        check(ma_decoder_init_file_w(path.c_str(), &config, &value), "Open audio file");
#else
        check(ma_decoder_init_file(path.c_str(), &config, &value), "Open audio file");
#endif
        initialized = true;
    }
};
std::atomic<uint64_t> nextAudioOwner{1};
} // namespace

struct AudioClip::Impl {
    AudioLoadMode mode = AudioLoadMode::Preload;
    uint32_t channels = 0, rate = 0;
    uint64_t frames = 0;
    std::filesystem::path path;
    std::vector<float> pcm;
};
AudioClip::AudioClip(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
AudioClip::~AudioClip() = default;
AudioClip::AudioClip(const AudioClip &) = default;
AudioClip &AudioClip::operator=(const AudioClip &) = default;
AudioClip::AudioClip(AudioClip &&) noexcept = default;
AudioClip &AudioClip::operator=(AudioClip &&) noexcept = default;
AudioClip AudioClip::fromPcm(std::span<const float> samples, uint32_t channels,
                             uint32_t sampleRate) {
    checkRate(sampleRate);
    require(channels == 1 || channels == 2, "Audio supports mono or stereo clips");
    require(!samples.empty() && samples.size() % channels == 0, "Invalid interleaved PCM length");
    for (float value : samples)
        require(std::isfinite(value), "PCM samples must be finite");
    auto impl = std::make_shared<Impl>();
    impl->channels = channels;
    impl->rate = sampleRate;
    impl->frames = samples.size() / channels;
    impl->pcm.assign(samples.begin(), samples.end());
    return AudioClip(std::move(impl));
}
AudioClip AudioClip::fromFile(const std::filesystem::path &path, AudioLoadMode mode) {
    require(mode == AudioLoadMode::Preload || mode == AudioLoadMode::Stream,
            "Invalid audio load mode");
    auto impl = std::make_shared<Impl>();
    impl->path = std::filesystem::absolute(path).lexically_normal();
    impl->mode = mode;
    Decoder decoder;
    decoder.open(impl->path);
    impl->channels = decoder.value.outputChannels;
    impl->rate = decoder.value.outputSampleRate;
    checkRate(impl->rate);
    require(impl->channels == 1 || impl->channels == 2, "Audio supports mono or stereo clips");
    check(ma_decoder_get_length_in_pcm_frames(&decoder.value, &impl->frames), "Read audio length");
    require(impl->frames > 0, "Audio clip must have a known, non-zero length");
    if (mode == AudioLoadMode::Preload) {
        require(impl->frames <= impl->pcm.max_size() / impl->channels, "Audio clip is too large");
        impl->pcm.resize(static_cast<size_t>(impl->frames) * impl->channels);
        ma_uint64 read = 0;
        auto result =
            ma_decoder_read_pcm_frames(&decoder.value, impl->pcm.data(), impl->frames, &read);
        if (result != MA_SUCCESS && result != MA_AT_END)
            check(result, "Decode audio");
        require(read == impl->frames, "Audio file is truncated");
        for (float value : impl->pcm)
            require(std::isfinite(value), "Decoded audio contains non-finite samples");
    }
    return AudioClip(std::move(impl));
}
AudioLoadMode AudioClip::mode() const {
    return impl_ ? impl_->mode : AudioLoadMode::Preload;
}
uint32_t AudioClip::channels() const {
    return impl_ ? impl_->channels : 0;
}
uint32_t AudioClip::sampleRate() const {
    return impl_ ? impl_->rate : 0;
}
uint64_t AudioClip::frameCount() const {
    return impl_ ? impl_->frames : 0;
}
double AudioClip::duration() const {
    return impl_ ? static_cast<double>(impl_->frames) / impl_->rate : 0;
}

struct AudioEngine::Impl {
    // Voice 分配在稳定地址；ma_sound、数据源和 PCM 在底层节点分离前必须一直存活。
    struct Voice {
        std::shared_ptr<const AudioClip::Impl> clip;
        ma_audio_buffer buffer{};
        ma_sound sound{};
        bool bufferReady = false, soundReady = false;
        AudioVoiceState state = AudioVoiceState::Playing;
        int priority = 0;
        uint64_t order = 0;
        ~Voice() {
            if (soundReady)
                ma_sound_uninit(&sound);
            if (bufferReady)
                ma_audio_buffer_uninit(&buffer);
        }
    };
    struct Slot {
        uint64_t generation = 1;
        std::unique_ptr<Voice> voice;
    };
    ma_engine engine{};
    std::array<ma_sound_group, 4> groups{};
    size_t groupsReady = 0;
    bool engineReady = false, offline = false;
    std::array<float, 5> volumes{1, 1, 1, 1, 1};
    std::array<bool, 5> muted{};
    std::vector<Slot> slots;
    std::vector<AudioEvent> events;
    const std::thread::id thread = std::this_thread::get_id();
    const uint64_t owner = nextAudioOwner.fetch_add(1);
    uint64_t sequence = 0;

    explicit Impl(const AudioEngineDesc &description) : offline(description.offline) {
        checkRate(description.sampleRate);
        require(description.maxVoices >= 1 && description.maxVoices <= 4096,
                "Audio maxVoices must be in [1, 4096]");
        slots.resize(description.maxVoices);
        auto config = ma_engine_config_init();
        config.sampleRate = description.sampleRate;
        config.channels = 2;
        config.noDevice = offline;
        config.noAutoStart = MA_TRUE;
        try {
            check(ma_engine_init(&config, &engine), "Initialize audio engine");
            engineReady = true;
            for (auto &group : groups) {
                check(
                    ma_sound_group_init(&engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &group),
                    "Initialize audio bus");
                ++groupsReady;
            }
            if (!offline)
                check(ma_engine_start(&engine), "Start audio device");
        } catch (...) {
            shutdown();
            throw;
        }
    }
    ~Impl() {
        shutdown();
    }
    void shutdown() {
        if (engineReady) {
            ma_engine_stop(&engine);
            for (auto &slot : slots)
                slot.voice.reset();
            while (groupsReady > 0)
                ma_sound_group_uninit(&groups[--groupsReady]);
            ma_engine_uninit(&engine);
            engineReady = false;
        }
    }
    bool valid(VoiceHandle handle) const {
        return handle.owner == owner && handle.index < slots.size() &&
               slots[handle.index].generation == handle.generation && slots[handle.index].voice;
    }
    Voice &get(VoiceHandle handle) const {
        if (!valid(handle))
            throw std::invalid_argument("Stale audio voice or voice belongs to another engine");
        return *slots[handle.index].voice;
    }
    VoiceHandle handle(size_t index) const {
        return {static_cast<uint32_t>(index), slots[index].generation, owner};
    }
    void retire(size_t index, AudioEndReason reason) {
        events.push_back({handle(index), reason});
        slots[index].voice.reset();
        ++slots[index].generation;
    }
    void collect() {
        for (size_t i = 0; i < slots.size(); ++i) {
            if (!slots[i].voice)
                continue;
            const auto &voice = *slots[i].voice;
            if (voice.state == AudioVoiceState::Paused)
                continue;
            if (ma_sound_at_end(&voice.sound))
                retire(i, voice.state == AudioVoiceState::Stopping ? AudioEndReason::Stopped
                                                                   : AudioEndReason::Finished);
            else if (voice.state == AudioVoiceState::Stopping && !ma_sound_is_playing(&voice.sound))
                retire(i, AudioEndReason::Stopped);
        }
    }
    void applyBus(size_t index) {
        const float volume = muted[index] ? 0 : volumes[index];
        if (index == 0)
            ma_engine_set_volume(&engine, volume);
        else
            ma_sound_group_set_volume(&groups[index - 1], volume);
    }
};

AudioEngine::Impl &AudioEngine::checked() const {
    if (!impl_)
        throw std::logic_error("AudioEngine was moved from");
    if (impl_->thread != std::this_thread::get_id())
        throw std::logic_error("AudioEngine controls must run on the creating thread");
    return *impl_;
}
AudioEngine::AudioEngine(const AudioEngineDesc &description)
    : impl_(std::make_unique<Impl>(description)) {}
AudioEngine::~AudioEngine() = default;
AudioEngine::AudioEngine(AudioEngine &&) noexcept = default;
AudioEngine &AudioEngine::operator=(AudioEngine &&) noexcept = default;
VoiceHandle AudioEngine::play(const AudioClip &clip, const AudioPlayDesc &description) {
    auto &impl = checked();
    require(bool(clip.impl_), "AudioClip was moved from");
    const auto bus = busIndex(description.bus);
    checkGain(description.volume);
    checkPitch(description.pitch);
    require(std::isfinite(description.pan) && std::abs(description.pan) <= 1,
            "Audio pan must be in [-1, 1]");
    require(finite(description.position), "Audio position must be finite");
    require(std::isfinite(description.minDistance) && std::isfinite(description.maxDistance) &&
                description.minDistance > 0 && description.maxDistance > description.minDistance,
            "Invalid audio attenuation distances");
    const auto fade =
        fadeFrames(description.fadeInSeconds, ma_engine_get_sample_rate(&impl.engine));
    impl.collect();
    size_t freeSlot = impl.slots.size(), victim = impl.slots.size();
    uint32_t sameClipCount = 0;
    for (size_t i = 0; i < impl.slots.size(); ++i) {
        const auto &voice = impl.slots[i].voice;
        if (!voice) {
            if (freeSlot == impl.slots.size())
                freeSlot = i;
        } else if (voice->clip == clip.impl_)
            ++sameClipCount;
    }
    const bool clipLimit =
        description.maxInstancesPerClip != 0 && sameClipCount >= description.maxInstancesPerClip;
    if (freeSlot == impl.slots.size() || clipLimit) {
        for (size_t i = 0; i < impl.slots.size(); ++i) {
            const auto &voice = impl.slots[i].voice;
            if (!voice || (clipLimit && voice->clip != clip.impl_) ||
                voice->priority > description.priority)
                continue;
            if (victim == impl.slots.size() ||
                voice->priority < impl.slots[victim].voice->priority ||
                (voice->priority == impl.slots[victim].voice->priority &&
                 voice->order < impl.slots[victim].voice->order))
                victim = i;
        }
        if (victim == impl.slots.size())
            return {};
        freeSlot = victim;
    }

    // 先准备新实例，再替换旧声音；失败时不会停止仍可播放的原实例。
    auto voice = std::make_unique<Impl::Voice>();
    voice->clip = clip.impl_;
    voice->priority = description.priority;
    auto *group = bus == 0 ? nullptr : &impl.groups[bus - 1];
    const ma_uint32 flags = description.spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (clip.impl_->mode == AudioLoadMode::Stream) {
#ifdef _WIN32
        check(ma_sound_init_from_file_w(&impl.engine, clip.impl_->path.c_str(),
                                        flags | MA_SOUND_FLAG_STREAM, group, nullptr,
                                        &voice->sound),
              "Open streaming audio");
#else
        check(ma_sound_init_from_file(&impl.engine, clip.impl_->path.c_str(),
                                      flags | MA_SOUND_FLAG_STREAM, group, nullptr, &voice->sound),
              "Open streaming audio");
#endif
    } else {
        auto config =
            ma_audio_buffer_config_init(ma_format_f32, clip.impl_->channels, clip.impl_->frames,
                                        clip.impl_->pcm.data(), nullptr);
        config.sampleRate = clip.impl_->rate;
        check(ma_audio_buffer_init(&config, &voice->buffer), "Initialize PCM source");
        voice->bufferReady = true;
        check(ma_sound_init_from_data_source(&impl.engine, &voice->buffer, flags, group,
                                             &voice->sound),
              "Initialize audio voice");
    }
    voice->soundReady = true;
    ma_sound_set_volume(&voice->sound, description.volume);
    ma_sound_set_pitch(&voice->sound, description.pitch);
    ma_sound_set_pan(&voice->sound, description.pan);
    ma_sound_set_looping(&voice->sound, description.loop);
    ma_sound_set_attenuation_model(&voice->sound, ma_attenuation_model_linear);
    ma_sound_set_min_distance(&voice->sound, description.minDistance);
    ma_sound_set_max_distance(&voice->sound, description.maxDistance);
    ma_sound_set_position(&voice->sound, description.position.x, description.position.y,
                          description.position.z);
    if (fade > 0)
        ma_sound_set_fade_in_pcm_frames(&voice->sound, 0, 1, fade);
    if (victim != impl.slots.size())
        impl.retire(victim, AudioEndReason::Stolen);
    check(ma_sound_start(&voice->sound), "Start audio voice");
    voice->order = ++impl.sequence;
    impl.slots[freeSlot].voice = std::move(voice);
    return impl.handle(freeSlot);
}
void AudioEngine::stop(VoiceHandle voice, double fadeOutSeconds) {
    auto &impl = checked();
    const auto frames = fadeFrames(fadeOutSeconds, ma_engine_get_sample_rate(&impl.engine));
    auto &current = impl.get(voice);
    if (frames == 0 || current.state == AudioVoiceState::Paused)
        impl.retire(voice.index, AudioEndReason::Stopped);
    else {
        check(ma_sound_stop_with_fade_in_pcm_frames(&current.sound, frames), "Fade out audio");
        current.state = AudioVoiceState::Stopping;
    }
}
void AudioEngine::stopAll() {
    auto &impl = checked();
    for (size_t i = 0; i < impl.slots.size(); ++i)
        if (impl.slots[i].voice)
            impl.retire(i, AudioEndReason::Stopped);
}
void AudioEngine::setPaused(VoiceHandle voice, bool paused) {
    auto &current = checked().get(voice);
    require(current.state != AudioVoiceState::Stopping, "Cannot pause a fading-out voice");
    if (paused) {
        check(ma_sound_stop(&current.sound), "Pause audio");
        current.state = AudioVoiceState::Paused;
    } else if (current.state == AudioVoiceState::Paused) {
        check(ma_sound_start(&current.sound), "Resume audio");
        current.state = AudioVoiceState::Playing;
    }
}
void AudioEngine::setVolume(VoiceHandle voice, float volume) {
    auto &current = checked().get(voice);
    checkGain(volume);
    ma_sound_set_volume(&current.sound, volume);
}
void AudioEngine::setPitch(VoiceHandle voice, float pitch) {
    auto &current = checked().get(voice);
    checkPitch(pitch);
    ma_sound_set_pitch(&current.sound, pitch);
}
void AudioEngine::setPosition(VoiceHandle voice, Vec3 position) {
    auto &current = checked().get(voice);
    require(finite(position), "Audio position must be finite");
    ma_sound_set_position(&current.sound, position.x, position.y, position.z);
}
void AudioEngine::fadeTo(VoiceHandle voice, float gain, double seconds) {
    auto &impl = checked();
    auto &current = impl.get(voice);
    checkGain(gain);
    const auto frames = fadeFrames(seconds, ma_engine_get_sample_rate(&impl.engine));
    require(current.state != AudioVoiceState::Stopping, "Cannot change fade of a fading-out voice");
    ma_sound_set_fade_in_pcm_frames(&current.sound, -1, gain, frames);
}
void AudioEngine::setBusVolume(AudioBus bus, float volume) {
    auto &impl = checked();
    const auto index = busIndex(bus);
    checkGain(volume);
    impl.volumes[index] = volume;
    impl.applyBus(index);
}
float AudioEngine::busVolume(AudioBus bus) const {
    return checked().volumes[busIndex(bus)];
}
void AudioEngine::setBusMuted(AudioBus bus, bool muted) {
    auto &impl = checked();
    const auto index = busIndex(bus);
    impl.muted[index] = muted;
    impl.applyBus(index);
}
bool AudioEngine::busMuted(AudioBus bus) const {
    return checked().muted[busIndex(bus)];
}
void AudioEngine::setListener(const AudioListenerDesc &listener) {
    auto &impl = checked();
    require(finite(listener.position), "Audio listener position must be finite");
    const auto forward = normalized(listener.forward);
    const auto right = normalized(cross(forward, normalized(listener.up)));
    const auto up = normalized(cross(right, forward));
    ma_engine_listener_set_position(&impl.engine, 0, listener.position.x, listener.position.y,
                                    listener.position.z);
    ma_engine_listener_set_direction(&impl.engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&impl.engine, 0, up.x, up.y, up.z);
}
bool AudioEngine::valid(VoiceHandle voice) const {
    return checked().valid(voice);
}
AudioVoiceState AudioEngine::state(VoiceHandle voice) const {
    return checked().get(voice).state;
}
void AudioEngine::update() {
    checked().collect();
}
std::vector<AudioEvent> AudioEngine::takeEvents() {
    auto &impl = checked();
    impl.collect();
    return std::exchange(impl.events, {});
}
uint32_t AudioEngine::activeVoiceCount() const {
    const auto &impl = checked();
    return static_cast<uint32_t>(std::count_if(impl.slots.begin(), impl.slots.end(),
                                               [](const auto &slot) { return bool(slot.voice); }));
}
uint32_t AudioEngine::sampleRate() const {
    return ma_engine_get_sample_rate(&checked().engine);
}
void AudioEngine::render(std::span<float> stereoSamples) {
    auto &impl = checked();
    require(impl.offline, "render() requires offline audio mode");
    require(stereoSamples.size() % 2 == 0, "Offline output must contain complete stereo frames");
    // 小块读取与设备回调一致，避免大块离线导出时延迟回收已结束实例。
    for (size_t offset = 0; offset < stereoSamples.size();) {
        const auto frames = std::min<size_t>((stereoSamples.size() - offset) / 2, 256);
        check(
            ma_engine_read_pcm_frames(&impl.engine, stereoSamples.data() + offset, frames, nullptr),
            "Mix audio");
        offset += frames * 2;
        impl.collect();
    }
}
} // namespace renderer
