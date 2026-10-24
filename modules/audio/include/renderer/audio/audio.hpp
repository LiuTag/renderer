/**
 * @file
 * @brief 声明独立音频模块的资源、播放实例、混音分组和监听器，不暴露 miniaudio 类型。
 */
#ifndef RENDERER_MODULES_AUDIO_AUDIO_HPP_INCLUDED
#define RENDERER_MODULES_AUDIO_AUDIO_HPP_INCLUDED
#include "renderer/handle.hpp"
#include "renderer/math.hpp"
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace renderer {
/** @brief 预解码适合短音效；流式读取适合音乐，播放期间必须保留源文件。 */
enum class AudioLoadMode { Preload, Stream };
/** @brief 固定混音分组，四个子组最终汇入 Master。 */
enum class AudioBus { Master, Music, Sfx, Ambient, UI };
/** @brief 一次播放的结束原因；事件由应用线程取出，不调用音频线程回调。 */
enum class AudioEndReason { Finished, Stopped, Stolen };
/** @brief 播放实例当前状态，结束后句柄失效。 */
enum class AudioVoiceState { Playing, Paused, Stopping };
using VoiceHandle = Handle<struct AudioVoiceTag>;

/** @brief 共享不可变 PImpl 音频资源，预解码数据独立于设备，可跨引擎复用。 */
class AudioClip {
  public:
    /**
     * @brief 从 WAV、MP3 或 FLAC 文件创建资源；加载和首次解码在调用线程执行。
     * @param path 文件路径；Stream 保存绝对路径，后续播放重新打开文件。
     * @param mode Preload 解码全部数据，Stream 只校验格式和读取长度。
     * @return 可共享的资源；文件无效、格式不支持或长度未知时抛出异常。
     */
    static AudioClip fromFile(const std::filesystem::path &path,
                              AudioLoadMode mode = AudioLoadMode::Preload);
    /**
     * @brief 复制交错排列的浮点 PCM，可在后台线程准备资源。
     * @param samples 有限浮点采样；每帧按声道顺序排列，数目必须为声道数的正整数倍。
     * @param channels 1（单声道）或 2（立体声）。
     * @param sampleRate 每秒帧数，范围 8000 到 192000。
     * @return 预解码资源；参数无效时抛出异常。
     */
    static AudioClip fromPcm(std::span<const float> samples, uint32_t channels,
                             uint32_t sampleRate);
    /** @brief 释放资源引用；正在播放的实例继续保留数据。@return 无。 */
    ~AudioClip();
    /** @brief 共享资源。@param other 源资源。@return 无。 */
    AudioClip(const AudioClip &other);
    /** @brief 共享资源。@param other 源资源。@return 当前资源。 */
    AudioClip &operator=(const AudioClip &other);
    /** @brief 转移资源。@param other 源资源。@return 无。 */
    AudioClip(AudioClip &&other) noexcept;
    /** @brief 转移资源。@param other 源资源。@return 当前资源。 */
    AudioClip &operator=(AudioClip &&other) noexcept;
    /** @brief 读取加载模式。@return 资源模式；已转移对象返回 Preload。 */
    AudioLoadMode mode() const;
    /** @brief 读取声道数。@return 1 或 2；已转移对象返回 0。 */
    uint32_t channels() const;
    /** @brief 读取采样率。@return 每秒 PCM 帧数；已转移对象返回 0。 */
    uint32_t sampleRate() const;
    /** @brief 读取长度。@return PCM 帧数；已转移对象返回 0。 */
    uint64_t frameCount() const;
    /** @brief 读取原始时长。@return 秒，不包含播放音调倍率；已转移对象返回 0。 */
    double duration() const;

  private:
    struct Impl;
    explicit AudioClip(std::shared_ptr<const Impl> impl);
    std::shared_ptr<const Impl> impl_;
    friend class AudioEngine;
};

/** @brief 音频引擎创建参数；离线模式不初始化声卡，供自动测试及导出使用。 */
struct AudioEngineDesc {
    uint32_t sampleRate = 48000; ///< 输出采样率，8000 到 192000。
    uint32_t maxVoices = 64;     ///< 同时存活的实例上限，1 到 4096，包含暂停实例。
    bool offline = false;        ///< true 时必须由 render() 主动推进音频时钟。
};
/** @brief 播放配置；空间位置采用右手世界坐标，+Y 向上、-Z 向前。 */
struct AudioPlayDesc {
    AudioBus bus = AudioBus::Sfx;
    float volume = 1; ///< 非负有限线性增益；大于 1 可能引起输出削波。
    float pitch = 1;  ///< 0.125 到 8，改变音调同时改变速度。
    float pan = 0;    ///< -1 左，0 中间，1 右；空间化之后额外应用。
    bool loop = false;
    bool spatial = false; ///< 音乐及 UI 建议关闭；定位音效建议使用单声道。
    Vec3 position{};
    float minDistance = 1;            ///< 正数；此范围内不因距离衰减。
    float maxDistance = 100;          ///< 大于 minDistance；线性衰减至静音的距离。
    double fadeInSeconds = 0;         ///< 音频时钟上的淡入时长，0 表示立即播放。
    int priority = 0;                 ///< 较大优先；满额时替换最低优先级中的最早实例。
    uint32_t maxInstancesPerClip = 0; ///< 同一共享资源的并发上限；0 表示只受总上限约束。
};
/** @brief 独立于相机的监听器，可由应用跟随玩家或相机。 */
struct AudioListenerDesc {
    Vec3 position{};
    Vec3 forward{0, 0, -1}; ///< 非零有限向量，不能与 up 平行。
    Vec3 up{0, 1, 0};       ///< 非零有限向量，内部正交归一化。
};
/** @brief 播放结束事件，携带已失效的原句柄供应用识别。 */
struct AudioEvent {
    VoiceHandle voice;
    AudioEndReason reason = AudioEndReason::Finished;
};

/**
 * @brief 以 PImpl 管理设备、实例与混音；所有控制方法在创建线程调用。
 * 系统设备独立持续混音；update 只回收实例，不以渲染帧推进播放时间。
 * 移动后仍保留创建线程归属；析构会等待设备及流式任务安全退出。
 */
class AudioEngine {
  public:
    /** @brief 创建音频设备或离线引擎。@param description 创建参数。@return 无；失败抛异常。 */
    explicit AudioEngine(const AudioEngineDesc &description = {});
    /** @brief 停止并释放全部实例及设备。@return 无。 */
    ~AudioEngine();
    /** @brief 转移引擎。@param other 源引擎。@return 无。 */
    AudioEngine(AudioEngine &&other) noexcept;
    /** @brief 转移引擎。@param other 源引擎。@return 当前引擎。 */
    AudioEngine &operator=(AudioEngine &&other) noexcept;
    /** @brief 禁止复制设备。@param other 源引擎。@return 无；已删除。 */
    AudioEngine(const AudioEngine &other) = delete;
    /** @brief 禁止复制设备。@param other 源引擎。@return 无；已删除。 */
    AudioEngine &operator=(const AudioEngine &other) = delete;
    /**
     * @brief 创建并启动一次播放，自动回收已结束实例；流式播放准备首批数据可能短暂阻塞。
     * @param clip 资源，内部共享持有，不要求原对象继续存活。
     * @param description 播放配置。
     * @return 实例句柄；并发满额且优先级不足时返回空句柄，加载失败抛出异常。
     */
    VoiceHandle play(const AudioClip &clip, const AudioPlayDesc &description = {});
    /**
     * @brief 停止实例，立即停止会使句柄失效，淡出停止在 update 回收时失效。
     * @param voice 当前引擎有效句柄，旧句柄或其他引擎句柄抛出异常。
     * @param fadeOutSeconds 非负有限秒数；暂停实例直接停止。
     * @return 无。
     */
    void stop(VoiceHandle voice, double fadeOutSeconds = 0);
    /** @brief 立即停止全部实例并产生事件。@return 无。 */
    void stopAll();
    /**
     * @brief 暂停或恢复，保持播放位置；淡出停止中的实例不允许暂停。
     * @param voice 有效实例句柄。
     * @param paused true 暂停，false 恢复。
     * @return 无。
     */
    void setPaused(VoiceHandle voice, bool paused);
    /** @brief 设置实例增益，保留淡变包络。@param voice 有效实例。@param volume
     * 非负有限增益。@return 无。 */
    void setVolume(VoiceHandle voice, float volume);
    /** @brief 设置音调及速度。@param voice 有效实例。@param pitch 0.125 到 8。@return 无。 */
    void setPitch(VoiceHandle voice, float pitch);
    /** @brief 更新世界位置。@param voice 有效实例。@param position 有限世界坐标。@return 无。 */
    void setPosition(VoiceHandle voice, Vec3 position);
    /** @brief 设置播放增益包络。@param voice 有效且未淡出停止的实例。@param gain
     * 非负有限目标倍率。@param seconds 非负有限秒数。@return 无。 */
    void fadeTo(VoiceHandle voice, float gain, double seconds);
    /** @brief 设置混音组增益，静音时仍保留该值。@param bus 分组。@param volume
     * 非负有限增益。@return 无。 */
    void setBusVolume(AudioBus bus, float volume);
    /** @brief 读取混音组的设置增益。@param bus 分组。@return 线性增益，不包含静音状态。 */
    float busVolume(AudioBus bus) const;
    /** @brief 设置混音组静音。@param bus 分组。@param muted true 静音。@return 无。 */
    void setBusMuted(AudioBus bus, bool muted);
    /** @brief 查询分组静音。@param bus 分组。@return true 表示该组显式静音。 */
    bool busMuted(AudioBus bus) const;
    /** @brief 更新监听器。@param listener 位置与正交朝向描述。@return 无。 */
    void setListener(const AudioListenerDesc &listener);
    /** @brief 查询句柄是否仍存活，不主动回收结束实例。@param voice 待检查句柄。@return
     * 属于本引擎且存活为 true。 */
    bool valid(VoiceHandle voice) const;
    /** @brief 查询实例状态，完成状态在 update 后通过事件反映。@param voice 有效实例。@return
     * 当前控制状态。 */
    AudioVoiceState state(VoiceHandle voice) const;
    /** @brief 回收自然结束及淡出停止的实例，排队结束事件，建议每帧调用。@return 无。 */
    void update();
    /** @brief 先 update 再取出全部结束事件。@return 本次取出后队列清空。 */
    std::vector<AudioEvent> takeEvents();
    /** @brief 读取尚未回收的实例数量，包含暂停实例。@return 实例数。 */
    uint32_t activeVoiceCount() const;
    /** @brief 读取输出采样率。@return 每秒 PCM 帧数。 */
    uint32_t sampleRate() const;
    /**
     * @brief 仅离线模式可用，混出立体声浮点 PCM 并推进音频时钟，随后自动 update。
     * @param stereoSamples 每帧两个交错浮点值，数目必须为 2 的整数倍；空 span 不推进时间。
     * @return 无；设备模式调用会抛出异常，输出不额外限幅。
     */
    void render(std::span<float> stereoSamples);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Impl &checked() const;
};
} // namespace renderer
#endif // RENDERER_MODULES_AUDIO_AUDIO_HPP_INCLUDED
