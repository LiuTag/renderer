# 无窗口相机控制示例

[相机控制说明](../../docs/CAMERA.md) · [示例总览](../README.md)

renderer_camera_demo 只依赖 camera/core，演示 Orbit、Follow、Free 切换和带父节点的 Camera 写回。它不创建 Window，不需要 GPU、声卡或输入设备。

```powershell
cmake --build build/windows --config Debug --target renderer_camera_demo --parallel 6
.\build\windows\Debug\renderer_camera_demo.exe
```

安装 SDK 后可独立构建：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/camera -B build/camera-sdk -DCMAKE_PREFIX_PATH=C:/SDK/renderer
cmake --build build/camera-sdk --config Release --parallel 6
ctest --test-dir build/camera-sdk -C Release --output-on-failure
```

支持 RENDERER_BUILD_EXAMPLES/RENDERER_BUILD_TESTS 分别控制程序和检查。完整交互见 [game 综合示例](../game/README.md)，窗口事件由应用映射为设备无关输入。
