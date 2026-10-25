/** @file @brief 综合森林应用的运行参数和生命周期入口；示例只调用 SDK 公共接口。 */
#ifndef RENDERER_EXAMPLES_GAME_APPLICATION_HPP_INCLUDED
#define RENDERER_EXAMPLES_GAME_APPLICATION_HPP_INCLUDED
#include <filesystem>
#include <memory>
#include <string>
namespace game_demo {
/** @brief 窗口/离屏、后端、音频和自动演示配置。 */
struct Options {
    std::string backend = "auto";
    unsigned width = 1280, height = 720, frames = 0;
    bool hidden = false, offscreen = false, threaded = true, offlineAudio = false, selfTest = false;
    bool fullscreen = false, exclusive = false, oit = false, fxaa = true;
    std::filesystem::path cache = "build/game-demo", capture, audioCapture, model;
};
/** @brief 持有场景和十二个扩展，组织输入、模拟、音画事件与渲染，不向核心增加玩法代码。 */
class Application {
  public:
    /** @brief 初始化资源、世界和设备。@param options 运行配置。@return 无；失败抛出异常。 */
    explicit Application(const Options &options);
    /** @brief 按模块生命周期释放设备和后台任务。@return 无。 */
    ~Application();
    /** @brief 运行交互或固定步自动演示。@return 无；自检或设备失败抛出异常。 */
    void run();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace game_demo
#endif // RENDERER_EXAMPLES_GAME_APPLICATION_HPP_INCLUDED
