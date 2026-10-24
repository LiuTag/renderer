# 无窗口 Box2D 示例

仅链接 renderer::physics2d，验证动态角色推挤物件、传感器/接触事件、射线查询和 Scene 高度保持，不需要 GPU、窗口或音频设备。

源码根工程默认生成 `renderer_physics2d_demo`，位于 VS examples 文件夹。SDK 安装后可独立构建：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/physics2d -B build/physics2d -DCMAKE_PREFIX_PATH=C:/SDK/renderer
cmake --build build/physics2d --config Release
ctest --test-dir build/physics2d -C Release --output-on-failure
```

构建配置应与 SDK 一致；仅安装 Debug 时使用 --config Debug。`RENDERER_BUILD_EXAMPLES` / `RENDERER_BUILD_TESTS` 可分别关闭示例与检查，关闭后清理旧测试清单。完整开发契约见[Box2D 模块文档](../../docs/PHYSICS2D.md)。图形集成见[world 示例](../world/README.md)，启用该模块时会自动使用 Box2D。
