# 图片精灵性能测试

[文档导航](README.md) · [森林游戏示例](SPRITES.md) · [线程与帧流程](ARCHITECTURE.md) · [Vulkan 后端](VULKAN.md)

`renderer_sprite_benchmark` 用森林风格的场景测试精灵数量、图集图片种类数，并在同一次运行中对比 OpenGL/Vulkan、单线程/独立渲染线程四种组合。代码位于 [main.cpp](../examples/sprite_benchmark/main.cpp)，使用公开 SDK 接口，不修改渲染核心，也不依赖动画和交互模块。

## 构建和运行

在源码根目录执行。双后端构建需要 Vulkan 开发头文件，运行需要相应驱动和桌面环境：

```powershell
cmake --preset windows -DRENDERER_BUILD_EXAMPLES=ON -DRENDERER_BUILD_VULKAN=ON
cmake --build build/windows --config Release --target renderer_sprite_benchmark --parallel 6
.\build\windows\Release\renderer_sprite_benchmark.exe --backend all --threads all --counts 1000,5000,10000,20000 --atlas-images 25,100,400 --warmup 60 --frames 240 --csv build/windows/benchmarks/sprites.csv
```

每个精灵数量、图片种类数组合依次运行四种后端/线程组合；默认窗口可见，Esc 取消当前测试，已完成的 CSV 行保留。编译时只有一个后端，`--backend all` 就只运行该后端；显式请求未编译的后端会报错。默认关闭垂直同步和图形校验，开启 FXAA、角色移动与序列帧。使用 Release 获取性能数据；Debug 和 `--validation` 适合功能检查。

固定两个数量、三种图集规模，并重复三轮的自动对比（共 72 组）：

```powershell
.\build\windows\Release\renderer_sprite_benchmark.exe --hidden --counts 1000,10000 --atlas-images 25,100,400 --warmup 30 --frames 120 --repeats 3 --csv build/windows/benchmarks/compare.csv
```

重复轮次会轮换四种组合的执行顺序。隐藏窗口仍调用真实呈现 API，不是无图形环境运行；桌面合成器、窗口可见性及驱动策略仍可能影响结果，比较时保持一致。

## 场景和数量口径

图集和元数据与交互森林示例共用 [forest_assets.hpp](../examples/common/forest_assets.hpp)：原创树木、石头、花草、四方向角色、帽子和地面软阴影均由程序生成，不需要外部游戏素材。

- `--counts` 指 **SpriteRenderer 总数**，身体、帽子、地面软阴影各计一个，程序保证创建数与指定数一致。
- 地面另有一个网格实例，不计入 `sprites`，但计入引擎实例、三角形和绘制统计。
- 角色使用 SortingGroup，身体换帧，帽子和软阴影随父节点移动；使用固定 `1/60` 动画步长，速度不随实测 FPS 改变。
- 相机固定，适配整个场景。输出单独记录最终快照通过裁剪的 `visible_sprites`，容量不能只看创建数量；通过裁剪仍不代表每个精灵的像素最终可见，重叠部分会被其他物体覆盖。
- 默认固定 28×28 世界区域和精灵尺寸。数量越大，密度和像素重叠也越大，同时考验 CPU 排序/上传和 GPU 填充；它不只是绘制调用数量测试。
- 所有精灵共用一个纹理图集，改变图片种类数不会主动拆分材质和纹理绑定，具有实例化合批优势；真实项目的多图集、多材质和不同状态可能产生更多绘制调用。
- 沿用森林示例的图片软阴影，关闭方向光阴影图。测试不包含碰撞、导航、拾取、音频或完整玩法更新。

可调 `--scene-size` 和 `--sprite-scale` 控制密度与图片屏幕面积。四种组合必须使用相同参数和种子；改变面积、尺寸或分辨率后的数据应分开比较。

## 图集图片种类测试

`--atlas-images 25,100,400` 扫描单张图集中的图块数，允许 25～1024。每个内容块固定 96×96 像素，外留 2 像素边距，按 100×100 格子排布；25、100、400 块分别为 500×500、1000×1000、2000×2000 图集。非平方数量也支持，末行空格为透明。

原始 25 块包含四方向动画帧、树石、软阴影、帽子、标记和花草。扩大种类数时，为同类图片生成不同配色和独立 UV 图块，保持尺寸、轮廓、Alpha、布局种子和几何负载一致。这里的“图片种类”是不同图块，包含动画帧和配色变体，不表示相同数量的物体类别。

CSV 同时记录 `atlas_images`（创建的图块数）与 `visible_image_kinds`（最后显示快照中，通过裁剪的精灵实际引用的不同图块数）。图片翻转仍计同一种。由于标记图块未放入森林、角色当前只显示一个方向/帧，以及实例分配范围，实际引用数可以少于图集总块数；程序不会将未使用的图块伪装成渲染过的种类。增大精灵数量通常能覆盖更多变体。

这会改变图集尺寸、纹理采样局部性和资源规模，图集/精灵资源建立仍排除在计时区间外。它用于观察稳定渲染的成本，不是图片加载速度或多张图集切换测试。

## 多线程比较怎样执行

每轮先提交第 N 帧的快照，然后由主线程更新并提取第 N+1 帧，最后等待第 N 帧 CPU 提交并呈现：

```text
单线程：渲染 N → 更新/提取 N+1 → 呈现 N
独立渲染线程：
    主线程：提交 N → 更新/提取 N+1 → 等待结果 → 请求呈现
    渲染线程：准备/提交 N ─────────────────→ 呈现 N
```

这让主线程的场景更新与渲染线程的帧准备/提交有机会重叠。渲染线程仍只有一个，不会自动并行裁剪多个区块或用多个 Vulkan 命令缓冲录制线程。线程切换、队列和缓存竞争有成本，因此开启线程不保证更快；CPU 工作较少或 GPU 已成为瓶颈时尤其如此。

## 测量区间与结果

创建窗口、设备、Shader、场景和图集不计入采样；预热负责首次资源上传和缓存建立。预热后先等待 GPU 空闲，再测指定帧数，最后等待尾帧 GPU 工作完成。截图、CSV 写入、结果打印及可见精灵查询均在采样区间外。采样区间包括事件收集、下一帧更新/提取、当前帧提交/等待和呈现；没有每帧 `waitIdle()`。

| 指标 | 含义 |
| --- | --- |
| `atlas_images` / `visible_image_kinds` | 图集总图块数 / 最后显示快照实际引用的不同图块数 |
| `atlas_width/height` / `atlas_rgba8_bytes` | 图集尺寸 / CPU RGBA8 像素字节数；不代表驱动实际显存分配量 |
| `fps` | 采样帧数 / 整段墙钟时间，包含最后 GPU 排空；对比后端和线程的主要吞吐指标 |
| `frame_mean/p50/p95/p99/max_ms` | 每轮主线程墙钟耗时，nearest-rank 分位数；不包含最后单独排空时间 |
| `update_mean_ms` / `extract_mean_ms` | 下一帧角色更新 / 世界变换及快照提取 |
| `prepare_mean_ms` | 引擎准备当前视图：Billboard、裁剪、排序、合批等 |
| `submit_mean_ms` | 渲染后端的帧槽等待、上传、录制及提交，可能包含 GPU 背压 |
| `wait_mean_ms` | 主线程等待本次提交 future；单线程提交已同步执行，通常很小 |
| `present_mean_ms` | 高层 present 的墙钟耗时，可能包含工作线程路由、交换链获取及驱动等待 |
| `gpu_graph_mean/p95_ms` | 已完成旧帧的 RenderGraph Pass 时间之和，不等同于完整显示延迟 |
| `gpu_samples` | 获得有效 GPU 计时的采样帧数；没有可用结果时 GPU 时间列留空 |
| `gpu_pass_*_mean_ms` | 各 Pass 的延迟 GPU 计时，未启用的 Pass 留空 |
| `scene_draws_mean` / `total_draws_mean` | 场景绘制次数 / 包含后处理等的总绘制次数 |
| `triangles_mean` / `uploaded_bytes_mean` | 场景三角形与本帧上传字节 |
| `elapsed_ms` / `gpu_drain_ms` | 完整采样墙钟时间 / 最后排空的时间 |
| `target_met` | FPS 达到目标，且 P95 不超过 `1000 / target_fps` 毫秒 |

多线程模式下 CPU 阶段存在重叠，**不要把 update/extract/prepare/submit 相加当成整帧耗时**。GPU 时间来自旧帧，预热过短时还可能包含预热帧。OpenGL 的图内 Present 包含后缓冲拷贝，Vulkan 在高层 present 中另行提交交换链 blit，后者不在渲染图计时内；GPU graph 时间不能代表两个后端完整呈现路径的成本。以相同桌面环境下的整段 FPS、帧分位数和 present 耗时一起判断。

CSV 每个已完成组合一行，包含图集图片种类/尺寸、窗口隐藏状态、设备、API 版本、构建类型、线程、重复轮次、分辨率、种子、场景尺寸、透明/FXAA/动画/校验开关及物体组成。每次运行覆盖指定文件，比较不同运行时使用不同文件名。

控制台最后输出三类比值：同一后端的 `render/single`，同一线程模式的 `Vulkan/OpenGL`，以及同一后端/线程在不同图集种类数下相对最小图集档位的 FPS 比值，大于 1 表示分子组合吞吐更高；重复运行时使用各轮 FPS 的算术平均。容量摘要仅表示 **已测试数量中**，所有重复轮次都满足 FPS 和 P95 预算的最大数量；0 表示没有档位达标，不表示不能渲染。

## 常用参数

| 参数 | 默认 | 作用 |
| --- | --- | --- |
| `--backend` | all | all / opengl / vulkan |
| `--threads` | all | all / single / render |
| `--counts` | 1000,5000,10000,20000 | 逗号分隔，排序去重，单档 1～1,000,000 个精灵 |
| `--atlas-images` | 25 | 逗号分隔的图集图片种类数，单档 25～1024；与精灵数量形成笛卡尔积 |
| `--warmup` / `--frames` | 60 / 240 | 每组预热帧 / 正式采样帧 |
| `--repeats` | 1 | 重复完整矩阵，允许 1～20 轮 |
| `--width` / `--height` | 1280 / 800 | 请求窗口尺寸，CSV 记录真实帧缓冲像素；运行中缩放会终止该组 |
| `--seed` | 42 | 布局随机种子 |
| `--scene-size` / `--sprite-scale` | 28 / 1 | 世界区域边长 / 精灵缩放 |
| `--transparency` | sorted | sorted：排序 Alpha；oit：所有精灵 FollowView 进入加权 OIT；oit-ordered：视图 OIT、精灵保序，与森林示例策略一致 |
| `--no-fxaa` | 不传入 | 关闭默认 FXAA |
| `--static` | 不传入 | 关闭角色移动和换帧，仍提取快照及执行完整渲染 |
| `--hidden` | 不传入 | 隐藏窗口，仍进行真实呈现 |
| `--validation` | 不传入 | 开启可用的 OpenGL/Vulkan 校验，用于功能排错 |
| `--target-fps` | 60 | 容量摘要的 FPS 和 P95 预算 |
| `--csv` | sprite-benchmark.csv | 汇总 CSV 路径 |
| `--capture` | 不传入 | 采样后按后端/线程/数量/轮次保存 PPM 的目录 |
| `--self-test` | 不传入 | 检查每个采样帧的实例/可见数量、最后图像及图形错误，不设置性能合格线 |

对比透明策略时分别运行并保存结果，例如：

```powershell
.\build\windows\Release\renderer_sprite_benchmark.exe --counts 1000,5000 --transparency oit --csv build/windows/benchmarks/oit.csv
.\build\windows\Release\renderer_sprite_benchmark.exe --counts 1000,5000 --transparency oit-ordered --csv build/windows/benchmarks/oit-ordered.csv
```

OIT 是近似合成，FollowView 会放弃 Sprite 的严格绘制顺序。性能结果与 sorted 不代表同等画质，分别核对截图。

## SDK 与自动检查

目标受 `RENDERER_BUILD_EXAMPLES` 控制，位于 VS 的 examples 文件夹。SDK 安装整个示例和共用图集头文件，应用只需安装包中的公开目标：

```powershell
cmake -S C:/SDK/renderer/share/renderer/examples/sprite_benchmark -B build/sdk-benchmark -DCMAKE_PREFIX_PATH=C:/SDK/renderer
cmake --build build/sdk-benchmark --config Release --parallel 6
.\build\sdk-benchmark\Release\renderer_sprite_benchmark.exe --backend all --threads all --counts 1000 --csv build/sdk-benchmark/result.csv
```

独立工程自动链接安装包中存在的图形组件；使用 Vulkan 包的应用不需要 Vulkan 开发头文件。核心包没有图形后端，无法构建此示例。共用图集在相邻的 `examples/common`，复制工程时保留这个目录。

开启对应 GPU 测试开关后，CTest 注册 `renderer.benchmark.opengl` / `renderer.benchmark.vulkan`。每项串行运行两个精灵数量、两种图集规模和两种线程，启用校验并检查真实图像和计数；结果写入构建目录。只验证功能，不将特定机器的 FPS 作为测试断言：

```powershell
ctest --test-dir build/windows -C Release -R renderer.benchmark --output-on-failure
```

## 本机实测记录

2026-10-24，Windows / MSVC 19.44 Release / Intel Core i5-12600KF / NVIDIA RTX 2080 Ti，驱动 610.62。1280×800 隐藏窗口、排序 Alpha、FXAA 和动画开启、校验关闭，30 帧预热、120 帧采样、三轮轮换顺序。已运行 1,000/10,000 精灵 × 25/100/400 图块 × 四种后端/线程组合，共 72 组。下表是 10,000 精灵时三轮 FPS 的算术平均：

| 图集总图块 / 最后显示快照引用种类 | OpenGL 单线程 | OpenGL 渲染线程 | Vulkan 单线程 | Vulkan 渲染线程 |
| --- | --- | --- | --- | --- |
| 25 / 24 | 82.6 | 106.3 | 88.0 | 107.9 |
| 100 / 96 | 86.0 | 105.7 | 90.0 | 108.9 |
| 400 / 375 | 84.2 | 109.6 | 88.9 | 107.8 |

本场景在高数量档位表现出线程重叠收益；1,000 精灵时没有稳定收益。图集种类增加后 FPS 未单调下降，当前结果更偏向 CPU 帧准备和快照开销；较小差异不能排除系统负载与测量波动。所有 10,000 精灵组合在本次三轮均达到 60 FPS/P95 预算，未继续扫描更高数量，不能视作最大容量。原始文件位于本地构建目录 `build/windows/benchmarks/matrix.csv`，报告为同目录 `REPORT.md`；它们是本机运行产物，不作为其他设备的性能保证。

## 新功能后的性能记录

上面的 2026-10-24 矩阵是增加图片世界模块之前的历史结果，不能直接当作新版本的吞吐结论。核心精灵实例布局因覆盖色/描边等字段由 176 增至 224 字节；默认不启用接触阴影或描边，但上传字节数已变化。批次入口也提供了新的应用组织方式，原性能示例仍使用节点场景以保持其测试口径。评估当前版本应按相同设备与命令重新采样，开启额外效果时另做一组对比。
