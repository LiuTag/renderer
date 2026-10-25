/** @file @brief 应用层音频资源缓存、世界事件映射、流式音乐和离线 WAV 输出。 */
#ifndef RENDERER_EXAMPLES_GAME_AUDIO_SYSTEM_HPP_INCLUDED
#define RENDERER_EXAMPLES_GAME_AUDIO_SYSTEM_HPP_INCLUDED
#include <filesystem>
#include <memory>
#include <renderer/audio/audio.hpp>
namespace game_demo {
/** @brief 实际混音及事件计数，用于演示状态和自动验证。 */
struct AudioStats {
    uint64_t footsteps = 0, impacts = 0, interactions = 0, musicChanges = 0, ended = 0;
    uint64_t mixedFrames = 0, ambientStarted = 0, ambientStopped = 0;
    double energy = 0;
    bool offline = false;
};
/** @brief 应用持有的音频服务，控制调用集中在游戏线程，底层独立混音。 */
class AudioSystem {
  public:
    /** @brief 生成并缓存音频资源，创建设备或离线混音器。
     * @param directory 应用生成资源目录。@param offline 是否不打开音频设备。
     * @param capture 非空时将离线实际混音流写入 WAV。
     * @param firePosition 应用定义的火堆世界位置，声音与粒子/灯光共用该位置。
     * @return 无。
     */
    AudioSystem(const std::filesystem::path &directory, bool offline,
                const std::filesystem::path &capture, renderer::Vec3 firePosition);
    /** @brief 停止实例和设备，释放资源。@return 无。 */
    ~AudioSystem();
    /** @brief 更新监听器、天气声和音乐，离线模式推进采样时间。
     * @param seconds 当前现实秒数。@param listener 玩家位置。@param night 是否夜间。
     * @param rain 降雨强度。@param snow 降雪强度。@return 无。
     */
    void update(double seconds, renderer::Vec3 listener, bool night, float rain, float snow);
    /** @brief 播放动画脚步事件。@param position 世界位置。@return 无。 */
    void footstep(renderer::Vec3 position);
    /** @brief 播放碰撞/受击。@param position 世界位置。@return 无。 */
    void impact(renderer::Vec3 position);
    /** @brief 播放选中或交互提示。@param position 世界位置。@return 无。 */
    void interact(renderer::Vec3 position);
    /** @brief 随区块加载开始环境声。@param id 区块身份。@param position 声源位置。@return 无。 */
    void addAmbient(uint64_t id, renderer::Vec3 position);
    /** @brief 随区块卸载停止声音。@param id 区块身份。@return 无。 */
    void removeAmbient(uint64_t id);
    /** @brief 切换音乐分组静音，环境/音效继续播放。@return 无。 */
    void toggleMusic();
    /** @brief 明确设置音乐启用状态，供 UI 与快捷键同步。@param enabled 是否启用。@return 无。 */
    void setMusicEnabled(bool enabled);
    /** @brief 查询音乐是否启用。@return 未静音时为 true。 */
    bool musicEnabled() const;
    /** @brief 设置主混音音量。@param volume [0,1] 增益。@return 无。 */
    void setVolume(float volume);
    /** @brief 切换当前日/夜配乐，演示独立音乐控制。@return 无。 */
    void switchMusic();
    /** @brief 完成 WAV 文件并停止全部播放。@return 无；写入失败抛出异常。 */
    void finish();
    /** @brief 读取实际播放和混音计数。@return 当前统计。 */
    AudioStats statistics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace game_demo
#endif // RENDERER_EXAMPLES_GAME_AUDIO_SYSTEM_HPP_INCLUDED
