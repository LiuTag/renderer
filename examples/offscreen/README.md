# 无应用窗口的森林渲染示例

[示例总览](../README.md) · [离屏接口与平台约束](../../docs/OFFSCREEN.md)

`renderer_offscreen_demo` 使用共用森林图集，直接把角色、树石、花草及地面阴影渲染到纹理并保存 PNG。仅依赖 core 与一个图形后端，程序不创建应用 Window，不运行窗口事件循环。

## 从源码构建

```powershell
cmake --preset windows -DRENDERER_BUILD_VULKAN=ON -DRENDERER_BUILD_EXAMPLES=ON
cmake --build build/windows --config Release --target renderer_offscreen_demo --parallel 6
.\build\windows\Release\renderer_offscreen_demo.exe --backend vulkan --threaded --output build/windows/forest.png
```

参数支持 `--backend opengl|vulkan`、`--output`、`--width`、`--height`、`--threaded`、`--oit`、`--no-fxaa`、`--self-test`。默认尺寸 1280×720、开启 FXAA；存在 Vulkan 后端时默认选 Vulkan。图片尺寸按像素指定，与显示器和 DPI 无关。

## 使用已安装的 SDK

保留相邻的 `examples/common` 目录后，独立配置本目录：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/offscreen -B build/offscreen-sdk `
  -DCMAKE_PREFIX_PATH=C:/SDK/renderer -DRENDERER_GL_TESTS=ON -DRENDERER_VULKAN_TESTS=ON
cmake --build build/offscreen-sdk --config Release --parallel 6
ctest --test-dir build/offscreen-sdk -C Release --output-on-failure
```

示例编译由 `RENDERER_BUILD_EXAMPLES` 控制，运行检查由 `RENDERER_BUILD_TESTS` 及对应 GPU 测试开关控制；未构建的后端不会注册检查。

Vulkan 路径完全不创建窗口、表面或交换链。OpenGL 离屏目前只实现 Windows WGL pbuffer，初始化短暂使用隐藏引导窗口，驱动还可能创建内部隐藏 HWND；严格零窗口需求使用 Vulkan。输出为已与背景合成的画面，高层管线目前不支持透明背景导出。
