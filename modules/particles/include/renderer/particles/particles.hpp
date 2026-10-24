/** @file @brief 图片粒子发射、生命周期曲线和批量发布，不为粒子创建 Scene 节点。 */
#ifndef RENDERER_MODULES_PARTICLES_PARTICLES_HPP_INCLUDED
#define RENDERER_MODULES_PARTICLES_PARTICLES_HPP_INCLUDED
#include <renderer/sprite_batch.hpp>
#include <vector>
namespace renderer {
/** @brief 内置可调整的图片特效预设。 */
enum class ParticlePreset { Fire, Smoke, Rain, Snow };
/** @brief 发射配置；速度/位置使用世界单位，寿命和更新单位为秒。 */
struct ParticleEmitterDesc {
    SpriteHandle sprite;
    uint32_t capacity = 4096, seed = 42;
    float rate = 50;
    float minLifetime = .5f, maxLifetime = 1.5f; ///< 寿命秒数，0 < min <= max <= 600。
    float startSize = .3f, endSize = .05f;       ///< 原图世界尺寸的正倍数，按年龄线性插值。
    Vec3 origin{}, spread{.1f, .1f, .1f}, velocityMin{-.2f, 1, -.2f}, velocityMax{.2f, 2, .2f},
        acceleration{};
    Vec4 startColor{1, 1, 1, 1}, endColor{1, 1, 1, 0};
    BillboardMode billboard = BillboardMode::ViewPlane;
    SpriteTransparency transparency = SpriteTransparency::FollowView;
};
/** @brief 建立火焰/烟雾/雨/雪配置，应用可继续修改。@param preset 预设种类。
 * @param sprite 使用的图片。@return 完整发射描述，不加载素材。
 */
ParticleEmitterDesc particlePreset(ParticlePreset preset, SpriteHandle sprite);
/** @brief 可读的粒子运动状态；身份在当前系统内唯一。 */
struct ParticleState {
    Vec3 position, velocity;
    float age = 0, lifetime = 1, rotation = 0;
    uint64_t id = 0;
};
/** @brief 采用固定 1/60 步长的 PImpl 粒子系统，按单写线程使用。 */
class ParticleSystem {
  public:
    /** @brief 创建发射器。@param description 受限容量、速率及有限参数。@return 无。 */
    explicit ParticleSystem(const ParticleEmitterDesc &description);
    /** @brief 释放系统。@return 无。 */
    ~ParticleSystem();
    /** @brief 转移系统。@param other 源系统。@return 无。 */
    ParticleSystem(ParticleSystem &&other) noexcept;
    /** @brief 转移赋值。@param other 源系统。@return 当前对象。 */
    ParticleSystem &operator=(ParticleSystem &&other) noexcept;
    /** @brief 推进固定步模拟。@param seconds [0,4] 秒，余数保留。@return 无。 */
    void advance(double seconds);
    /** @brief 瞬间发射，超容量部分丢弃。@param count 请求数量。@return 实际生成数量。 */
    uint32_t burst(uint32_t count);
    /** @brief 开关持续发射，已有粒子继续运动。@param enabled 是否发射。@return 无。 */
    void setEmitting(bool enabled);
    /** @brief 改变后续发射位置。@param origin 有限世界坐标。@return 无。 */
    void setOrigin(Vec3 origin);
    /** @brief 改变持续发射速率。@param rate 每秒 0～100000 个。@return 无。 */
    void setRate(float rate);
    /** @brief 清除存活粒子及时间余数。@return 无。 */
    void clear();
    /** @brief 读取粒子状态。@return 只读 span，下次修改系统前有效。 */
    std::span<const ParticleState> particles() const;
    /** @brief 将粒子批量发布。@param assets 解析图片的仓库。@return 保有资源和当前状态的批次。 */
    SpriteBatch batch(const AssetStore &assets) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer
#endif
