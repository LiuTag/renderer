# 压缩纹理、数组与文字质量对照

[示例索引](../README.md) · [纹理接口](../../docs/TEXTURES.md) · [文字缓存/SDF](../../docs/TEXT.md)

本目录包含两个目标，都受 RENDERER_BUILD_EXAMPLES 控制，并在 VS 的 examples 文件夹下。

- renderer_texture_pack：只依赖 core 的 CPU DDS 打包工具，无需窗口、GPU、UI 或 text。
- renderer_texture_text_demo：需要 text 及至少一个后端，展示独立 RGBA8、RGBA8 数组、BC3 数组+mip，以及位图/SDF 缩放对照。SDF 使用外描边和屏幕导数抗锯齿。

## 源码构建和运行

```powershell
cmake --build build/windows --config Release --target renderer_texture_pack renderer_texture_text_demo
build/windows/Release/renderer_texture_pack.exe --format bc3 --output build/trees.dds tree1.png tree2.png tree3.png
build/windows/Release/renderer_texture_text_demo.exe --backend opengl
build/windows/Release/renderer_texture_text_demo.exe --backend vulkan --offscreen --frames 60 --output build/texture-text.png --cache build/texture-cache
```

Q/E 调整正交相机缩放，F 切换 FXAA，Escape 关闭。--threaded / --single-thread 控制渲染线程，--hidden 隐藏窗口，--font 指定补充字体。Windows 可找到 msyh.ttc 时加载为回退，应用自行提供有分发权限的字体。程序显示英文对照标签，不需要中文系统字体即可运行。

离屏会实际写入 PNG 和 sprites.dds，不调用 UI，也不需要音频设备。--self-test 在 60 帧内缩放相机、重建文字缓存，检查 BC/数组统计、实例复用和图形校验错误。左侧 BITMAP 与右侧 SDF 使用相同逻辑字号，可缩放观察差异。

打包器的输入可以是普通图片或可读取的 DDS（取第 0 层/级），多张输入必须尺寸/颜色空间相同。格式包括 rgba8/bc1/bc3/bc4/bc5，默认 bc3 并生成完整 mip；--no-mips 关闭 mip，--linear 声明线性数据。BC1 要求不透明，BC4/5 要配合 --linear。RGBA8 的 mip 也按 sRGB/Alpha 正确过滤。DDS 自带的颜色空间优先，需确保其元数据正确。BC2/7 由外部工具编码后导入。

## 独立 SDK 工程

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/texture_text -B build/texture-sdk `
  -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build build/texture-sdk --config Release
ctest --test-dir build/texture-sdk -C Release --output-on-failure
```

仅 core 的 SDK 仍可构建打包器；缺少 text/后端时自动跳过可视示例。启用 RENDERER_BUILD_TESTS 且对应 GPU 开关打开时注册 CPU 打包检查及四项窗口/离屏检查；全部位于 tests 分组。已安装 SDK 无需 Vulkan SDK 或第三方实现头文件。
