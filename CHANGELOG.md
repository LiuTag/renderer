# 版本变更记录

[项目概览](README.md) · [实现清单](IMPLEMENTATION.md) · [文档导航](docs/README.md)

## 0.2.0 当前源码整理（2026-10-24）

本次汇总初始本地提交之后的更新。CMake 项目和 SDK 包版本为 0.2.0；公共值类型和接口有增加，使用者应重新构建应用并使用同一份 SDK 的头文件与库。

### 图片世界与渲染表现

- 增加共享不可变 SpriteBatch，可发布无场景节点的精灵实例，支持资源保留、整批/逐实例裁剪、排序、合批及实例身份拾取。
- 增加 tilemap、collision、particles、environment、presentation 五个独立扩展，分别负责分块地图、轻量碰撞、图片粒子、昼夜天气和精灵表现。
- 核心增加精灵颜色覆盖、Alpha 内描边、线性调色和屏幕空间接触阴影；OpenGL/Vulkan 复用同一管线与 Shader。
- 增加模块化 world 示例，组合地面、装饰、角色移动、火烟雨雪、昼夜、遮挡淡出与受击闪白。

### 流式世界与物理

- tilemap 增加 TileStreamer：后台纯数据读取、发布/卸载预算、距离优先与滞回、协作取消、失败重试、编辑保存和区块生命周期事件。
- 增加带校验和 Unicode 路径支持的区块文件数据源；应用可通过 payload 管理物件及玩法数据。
- 增加独立 physics2d 模块，封装 Box2D 3.2.0：刚体和常用形状、复合碰撞、固定步、内部工作线程、受力、接触/传感器/命中事件、查询、距离关节及 Scene 同步。
- world 示例按流式事件挂接物件与碰撞体；启用 physics2d 时采用 Box2D，关闭后可回退到 collision。

### 离屏渲染与图片输出

- 增加 Renderer 和 RHI Device 的 OpenGL/Vulkan 离屏工厂；高层场景与渲染设置保持一致。
- Vulkan 离屏不初始化 GLFW，不创建窗口、surface 或 swapchain；Windows OpenGL 使用 WGL pbuffer，有短暂引导窗口且驱动可能保留内部隐藏 HWND。
- 增加 renderToImage、resizeOffscreen 和 Image::savePng；支持渲染线程、精确像素尺寸、Unicode 路径及直接 PNG 输出。
- 增加仅依赖核心与后端的森林离屏示例；PNG 编码实现静态编入核心，SDK 使用者无需额外 PNG 库。

### 工程、SDK 与文档

- 扩展、示例、测试分别通过 CMake 开关构建，VS 中按 core/modules/examples/tests 分组。
- SDK 按启用组件安装头文件、静态库、传递依赖、许可证和可独立构建的示例。
- 统一文档中的模块清单、完整帧流程、动态物体更新、依赖管理、示例数量和当前验证结果；历史测试数量不再作为当前版本统计。
- 完整双后端配置包含 48 项 CTest 检查、15 个示例目标；安装后的聚合示例包含 16 项运行检查。实测平台、复现命令和限制见[验证说明](docs/VALIDATION.md)。
