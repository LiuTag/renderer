# 第三方依赖与许可来源

本文说明项目使用的固定版本与获取方式。许可证原文及上游版权声明保持不变，位于 `third_party/licenses` 和相应源码文件中。

## 获取与升级约定

| 方式 | 依赖 | 考量 |
| --- | --- | --- |
| CMake FetchContent | GLFW、glslang、SPIRV-Cross、Box2D | 复用完整上游 CMake 工程，减少主仓库源码体积；远程压缩包按固定版本和 SHA256 校验 |
| 源码随仓库保存 | TinyGLTF/json/stb、miniaudio、Volk | 文件数量少、集成直接，保留上游原文，正常构建无需单独下载这些文件 |
| 生成结果随仓库保存 | GLAD | 固定 OpenGL 4.5 加载代码，正常构建无需 Python、生成器或联网再生成 |

FetchContent 默认在构建目录 `_deps` 保存源码和构建结果。离线构建可通过 `FETCHCONTENT_SOURCE_DIR_<依赖名>` 使用预先准备的精确版本源码；此方式直接使用指定目录，不重新下载或自动校验本地内容。`.cache` 是当前工作区可选的本地源码缓存，不纳入 Git。获取方式不同，但版本升级都应人工更新版本/哈希或生成结果，并重新验证构建、测试与 SDK。

安装包提供公共引擎头文件和完整静态链接依赖。glslang、SPIRV-Cross、GLFW、GLAD、Box2D 等单独随包安装，miniaudio、Volk 和模型/图片编解码实现编入对应引擎库；CMake 目标自动传递依赖。第三方实现头文件不进入公共 API，也不要求 SDK 使用者自行安装。系统图形/音频运行时和驱动仍由运行环境提供。

## GLFW 3.4

- [源码版本](https://github.com/glfw/glfw/tree/3.4)。
- 由 CMake 下载，压缩包 SHA256 固定在 `CMakeLists.txt` 中；任一图形后端启用时需要此依赖，两种后端都关闭时不构建。
- 许可为 zlib/libpng，上游源码包含 LICENSE.md。
- 未修改的许可声明另存于 `third_party/licenses/GLFW.txt`。

## GLAD 2.0.8 生成加载器

- [生成器版本](https://github.com/Dav1dde/glad/tree/v2.0.8)。
- 随仓库提供 `third_party/glad/include/glad/gl.h`、`third_party/glad/include/KHR/khrplatform.h`、`third_party/glad/src/gl.c`。
- 目标为 OpenGL 4.5 Core，不包含扩展和内置平台加载器；窗口模式由 GLFW 提供函数地址，Windows 离屏模式通过 WGL/opengl32 获取。
- 使用生成器自带注册表数据和可复现模式，不含生成时间戳，也不在线更新注册表。
- 生成文件保留 SPDX 与 Khronos 声明。生成器/注册表许可说明见 `third_party/licenses/GLAD.txt`，Apache 2.0 原文见 `third_party/licenses/Apache-2.0.txt`。

仅重新生成加载器时需要 Python。在独立 Python 环境安装 `glad2==2.0.8` 后，从仓库根目录执行：

```sh
python -m glad --api gl:core=4.5 --extensions '' --out-path third_party/glad --reproducible c
```

## Volk vulkan-sdk-1.4.350.0

- [源码版本](https://github.com/zeux/volk/tree/vulkan-sdk-1.4.350.0)，未修改的 `volk.h`、`volk.c` 和 `LICENSE.md` 保存在 `third_party/volk`。
- 用于动态加载 Vulkan loader、instance 和 device 函数；仅在 `RENDERER_BUILD_VULKAN=ON` 时编译。
- `volk.h` SHA256：`479bab725a424d54715fe9e54b1a69b344a9c36fb617c9aa2cc4289814d8acdf`。
- `volk.c` SHA256：`698571dde0c43bc296ab5997ffebf5d8176a9d9e586894590069dbae5ace3c68`。
- `LICENSE.md` SHA256：`04a0693a84f19e53d281ca98bbb0c86ca77251ab13769c6168e6684feb9a1436`。
- MIT 许可原文另存于 `third_party/licenses/volk.txt`。实现对象合入 `renderer_vulkan` 静态库；SDK 不安装 Volk/Vulkan 开发头文件，也不要求应用链接 Vulkan import library。
- 源码构建通过 `find_package(Vulkan 1.3 REQUIRED)` 取得开发头文件，未将 Vulkan SDK 的绝对路径或库传递到安装包。

## glslang 15.4.0

- [源码版本](https://github.com/KhronosGroup/glslang/tree/15.4.0)。
- 由 CMake 下载并校验固定 SHA256，用于 GLSL 到 SPIR-V 编译，不包含 Vulkan 运行时。
- 许可声明见 `third_party/licenses/glslang.txt` 及上游源码。

## SPIRV-Cross vulkan-sdk-1.4.321.0

- [源码版本](https://github.com/KhronosGroup/SPIRV-Cross/tree/vulkan-sdk-1.4.321.0)。
- 由 CMake 下载并校验固定 SHA256，构建 C++ 核心和 GLSL 转译库。
- 许可按源码标注为 Apache-2.0 或 MIT；Apache 原文另存于 `third_party/licenses/SPIRV-Cross.txt`。

## TinyGLTF 2.9.6、nlohmann/json、stb_image 与 stb_image_write

- [源码版本](https://github.com/syoyo/tinygltf/tree/v2.9.6)。
- 该版本附带的 `tiny_gltf.h`、`json.hpp`、`stb_image.h`、`stb_image_write.h` 原样保存在 `third_party/tinygltf`。
- 上游压缩包 SHA256：`b96cbeec30038dacc9c6ee5e0610f0719808ee5527ea006c5061a4f991537984`。
- TinyGLTF 为 MIT，原文见 `third_party/licenses/TinyGLTF.txt`。
- nlohmann/json 为 MIT，版权和许可声明保留在 `json.hpp` 中，并原样复制到 `third_party/licenses/nlohmann-json.txt`。
- stb_image 为 public domain 或 MIT，两种声明均保留在 `stb_image.h` 末尾，并原样复制到 `third_party/licenses/stb_image.txt`。
- stb_image_write 1.16 用于 PNG 输出；同样提供 MIT/public domain 双许可，原文保存为 `third_party/licenses/stb_image_write.txt`。头文件 SHA256 为 `bdbaa2c548ac9bde2913b6b46c4d315a04d2624c81ee1553390eb8fb2de2835d`。
- 写入实现和静态符号仅存在于 core 的 `src/image.cpp`，SDK 不安装实现头文件，也不需要额外 PNG/zlib 链接库。

## miniaudio 0.11.25

- [源码版本](https://github.com/mackron/miniaudio/tree/0.11.25)，未修改的 `miniaudio.h` 与 LICENSE 保存在 `third_party/miniaudio`。
- `miniaudio.h` SHA256：`ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89`。
- 仅在 `RENDERER_BUILD_AUDIO=ON` 时编译，包含 WAV/MP3/FLAC 解码；没有额外引入 Ogg/Opus 解码依赖。
- 采用上游提供的 MIT No Attribution 许可选项，完整双许可原文保留在 `third_party/licenses/miniaudio.txt`。
- 实现对象直接编入 `renderer_audio` 静态库；SDK 不安装第三方开发头文件，使用者链接 `renderer::audio` 即可。

SDK 会在 `share/renderer/third_party/licenses` 安装上述许可声明，原始第三方源码文件保持不变。

离线源码覆盖方式见[快速开始](docs/GETTING_STARTED.md)。以上版本说明以本仓库的固定依赖为准，不表示各项目的最新发行版本。

## Box2D 3.2.0

- [固定源码版本](https://github.com/erincatto/box2d/tree/v3.2.0)，CMake 按需下载。
- 上游压缩包 SHA256：`ed94013e8d16b19876757920bfa6d7663358aef8ba9407cc18146da6b714029e`。
- 仅在 RENDERER_BUILD_PHYSICS2D=ON 时构建；C17 静态实现不包含上游示例/测试/外部 profiler。
- MIT 许可原文保存在 third_party/licenses/box2d.txt；上游源码不修改。
- SDK 自动传递私有静态库，不安装上游开发头文件、pkg-config 或独立 box2d CMake 配置。
- 离线覆盖：`-DFETCHCONTENT_SOURCE_DIR_BOX2D=C:/deps/box2d-3.2.0`。
