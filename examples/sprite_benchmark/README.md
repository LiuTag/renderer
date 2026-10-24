# 森林精灵性能测试示例

比较同一森林场景的精灵数量、图集图片种类数、OpenGL/Vulkan 和单线程/独立渲染线程。仅依赖 core 和已启用的图形后端，不依赖动画/交互扩展。

从源码根目录构建：

```powershell
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=ON -DRENDERER_BUILD_VULKAN=ON
cmake --build build/windows --config Release --target renderer_sprite_benchmark --parallel 6
.\build\windows\Release\renderer_sprite_benchmark.exe --backend all --threads all --counts 1000,5000,10000 --atlas-images 25,100,400 --warmup 60 --frames 240 --csv build/windows/benchmarks/sprites.csv
```

默认关闭垂直同步，开启 FXAA 和角色动画；Esc 取消，`--hidden` 自动隐藏运行，`--capture <目录>` 在计时结束后截图。参数可通过 `--help` 查看。

SDK 也安装此目录及相邻 `common/forest_assets.hpp`，可将本目录作为独立 CMake 工程，通过 `CMAKE_PREFIX_PATH` 找到 SDK。只有一种后端的包自动只测试该后端。

场景口径、计时区间、线程重叠、CSV 字段和结果限制见[详细说明](../../docs/SPRITE_BENCHMARK.md)。
