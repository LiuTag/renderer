# 世界文字示例

[文档导航](../../docs/README.md) · [世界文字模块](../../docs/TEXT.md) · [综合游戏](../game/README.md)

renderer_text_demo 使用公开 SDK 绘制固定路牌、竖直 Billboard、绑定移动节点的像素字号名字牌，以及位于墙后的文字。D 可以切换遮挡，其余控件在控制台显示。字体缓存、动态文本和整个渲染流程与真实应用相同。

```powershell
cmake --preset windows -DRENDERER_BUILD_TEXT=ON -DRENDERER_BUILD_VULKAN=ON
cmake --build --preset windows --target renderer_text_demo --parallel 8
build/windows/Debug/renderer_text_demo.exe --backend vulkan --threaded
build/windows/Debug/renderer_text_demo.exe --backend opengl --offscreen --frames 36 --self-test --output build/windows/text.png
```

| 操作 | 效果 |
| --- | --- |
| Q / E，滚轮 | 围绕场景旋转、拉近/拉远，观察文字朝向和字号 |
| D | 墙后文字开启/关闭深度遮挡 |
| B | 切换上方文字的三种 Billboard 朝向 |
| P | 透视/正交投影 |
| S | 名字牌切换固定像素/世界尺寸 |
| O / F | 切换 OIT / FXAA |
| Escape | 退出 |

命令行支持 --backend opengl/vulkan、--hidden、--offscreen、--threaded/--single-thread、--frames、--self-test、--output、--font、--fallback-font。默认优先使用可用的 Vulkan；窗口运行到关闭，离屏默认一帧。自检至少运行 36 帧，会切换深度、投影、字号和后处理。图片默认为 text.png，支持 Unicode 输出路径。

--font 替换主字体，--fallback-font 增加回退字体。SDK 附带的 Lato 主要用于拉丁文字，应用的中文/其他字体需自行提供。文字模块不要求 UI 或 RmlUi。

## 安装后独立构建

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/text -B build/text-sdk -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build build/text-sdk --config Release --parallel 8
ctest --test-dir build/text-sdk -C Release --output-on-failure
```

SDK 需要 text 与至少一个后端。字体路径通过 renderer_TEXT_FONT 获取，不要求源仓库、FreeType 开发包或 Vulkan SDK。渲染需要相应 GPU 驱动；Windows OpenGL 离屏使用 WGL pbuffer，Vulkan 离屏不创建窗口/表面/交换链。
