/** @file @brief 昼夜时钟、天气过渡与通用灯光/调色参数，独立于玩法规则。 */
#ifndef RENDERER_MODULES_ENVIRONMENT_ENVIRONMENT_HPP_INCLUDED
#define RENDERER_MODULES_ENVIRONMENT_ENVIRONMENT_HPP_INCLUDED
#include <renderer/renderer.hpp>
namespace renderer {
/** @brief 可由应用切换的降水表现状态。 */
enum class Weather { Clear, Rain, Snow };
/** @brief 昼夜及天气参数；dayLength 是现实秒数，hour 是 [0,24) 的游戏时刻。 */
struct EnvironmentConfig {
    double dayLength = 120, hour = 12;
    float dayAmbient = .3f, nightAmbient = .12f, sunIntensity = 3, weatherTransition = 2;
    Vec3 dayColor{1, 1, 1}, nightColor{.35f, .45f, .7f}, wind{.5f, 0, .2f};
};
/** @brief 一次环境采样；降水强度为 [0,1]，应用可映射为粒子发射速率。 */
struct EnvironmentState {
    double hour = 12;
    Vec3 lightDirection{}, lightColor{1, 1, 1}, colorMultiply{1, 1, 1}, wind{};
    float lightIntensity = 0, ambient = .3f, precipitation = 0;
    Weather weather = Weather::Clear;
};
/** @brief PImpl 环境控制器，只输出表现参数，不控制生存、AI 或天气伤害。 */
class EnvironmentController {
  public:
    /** @brief 创建时钟。@param config 有限、正时长和非负颜色参数。@return 无。 */
    explicit EnvironmentController(const EnvironmentConfig &config = {});
    /** @brief 释放控制器。@return 无。 */
    ~EnvironmentController();
    /** @brief 转移状态。@param other 源对象。@return 无。 */
    EnvironmentController(EnvironmentController &&other) noexcept;
    /** @brief 转移赋值。@param other 源对象。@return 当前对象。 */
    EnvironmentController &operator=(EnvironmentController &&other) noexcept;
    /** @brief 推进时钟与天气过渡。@param seconds 非负有限现实秒数。@return 无。 */
    void advance(double seconds);
    /** @brief 设置时刻，不改变天气进度。@param hour 有限小时数，自动绕回一天。@return 无。 */
    void setHour(double hour);
    /** @brief 设置天气目标，按配置过渡强度。@param weather 目标天气。@return 无。 */
    void setWeather(Weather weather);
    /** @brief 查询表现状态。@return 当前时刻、灯光、颜色、降水及风。 */
    EnvironmentState state() const;
    /** @brief 将环境应用到方向光节点和视图。
     * @param scene 所属场景。@param sun 有效方向光节点。
     * @param view 接收环境光和调色参数的视图。@return 无。
     */
    void apply(Scene &scene, NodeHandle sun, RenderView &view) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer
#endif
