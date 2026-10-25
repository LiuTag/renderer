# 音乐与音效模块

[文档导航](README.md) · [模块划分](MODULES.md) · [SDK 安装](SDK.md) · [示例源码](../examples/audio/main.cpp)

`renderer::audio` 是独立可选静态库，使用固定版本 miniaudio 0.11.25。它只依赖 core 的公共数学/句柄类型，不依赖 OpenGL、窗口、动画或交互模块，也不访问 `src/internal`。`AudioEngine` 使用独占 PImpl，`AudioClip` 使用共享不可变 PImpl；公共头文件不包含第三方音频类型。

音画联动的完整参考是[森林营地示例](../examples/game/README.md)：应用持有独立 AudioEngine，动画脚步/物理接触映射为声音，火堆声音与粒子/灯光共用位置，区块卸载停止环境声，昼夜与雨雪切换配乐/天气声。示例也可把实际离线混音保存为 WAV；无需给渲染器或 Scene 增加音频依赖。

## 构建、安装和接入

默认开启音频模块。源码根工程构建示例及离线测试：

```powershell
cmake --preset windows -DRENDERER_BUILD_AUDIO=ON -DRENDERER_BUILD_EXAMPLES=ON -DRENDERER_BUILD_TESTS=ON
cmake --build --preset windows-release --parallel 6
ctest --test-dir build/windows -C Release -R renderer.audio --output-on-failure
.\build\windows\Release\renderer_audio_demo.exe
```

最后一条命令通过系统默认设备播放两秒低音量演示声，包含音乐淡入、空间音效和交叉切换。不需要声音输出时加 `--offline`，程序将检查混音数据而不打开声卡。播放自己的音乐文件：

```powershell
.\build\windows\Release\renderer_audio_demo.exe --music C:/assets/music.flac
```

示例中的第一首音乐在一秒后切换到程序生成的音调；它演示 API，不是完整音乐播放器。其素材由程序生成，无需下载音频。

`RENDERER_BUILD_AUDIO=OFF` 会关闭库、专用头文件安装、音频测试及示例。`RENDERER_BUILD_EXAMPLES`、`RENDERER_BUILD_TESTS` 仍分别控制示例和测试；离线音频测试不受 `RENDERER_GL_TESTS` 控制。关闭 OpenGL 仍能构建、使用和测试音频。

SDK 使用者显式请求组件：

```cmake
find_package(renderer 0.2 CONFIG REQUIRED COMPONENTS audio)
target_link_libraries(my_game PRIVATE renderer::audio)
```

包含 `<renderer/audio/audio.hpp>`。安装增加 `renderer_audio.lib`，miniaudio 的实现对象已编入该库，不需要安装第三方开发头文件或另行下载库；平台链接依赖由 CMake 传递。关闭 audio 的 SDK 会拒绝 `REQUIRED COMPONENTS audio`。安装后的 `share/renderer/examples/audio` 可独立配置构建，并注册一个 `--offline` 的 CTest。

## 资源与一次播放

`AudioClip` 表示可共享的声音数据，`VoiceHandle` 表示一次播放。一个资源可以重叠播放多次，每次拥有独立位置、音量、播放进度和循环状态。

```cpp
#include <renderer/audio/audio.hpp>
using namespace renderer;

AudioEngine audio;
const auto click = AudioClip::fromFile("assets/click.wav");
const auto music = AudioClip::fromFile("assets/music.flac", AudioLoadMode::Stream);

AudioPlayDesc background;
background.bus = AudioBus::Music;
background.loop = true;
background.fadeInSeconds = 1;
auto musicVoice = audio.play(music, background);
auto clickVoice = audio.play(click);

audio.setPaused(musicVoice, true);
audio.setPaused(musicVoice, false);
audio.stop(musicVoice, .5); // 在音频时钟上淡出，应用仍需继续存活。
```

| 加载方式 | 内存与准备 | 适合 |
| --- | --- | --- |
| `Preload` | 在调用线程完整解码为浮点 PCM；资源副本和多次播放共享采样数据 | 脚步、攻击、按钮等短音效 |
| `Stream` | 创建资源时校验文件并读取长度；每次播放准备首批数据，之后后台任务补页 | 背景音乐、较长环境声 |
| `fromPcm` | 复制交错排列的单/双声道浮点采样 | 程序生成音效、上层自定义解码 |

本版文件格式为 WAV、MP3、FLAC，声道数为 1 或 2，采样率为 8000 到 192000；不包含额外的 Ogg Vorbis/Opus 解码器。时间长度必须已知且非零。PCM 各采样必须有限，通常使用 `[-1, 1]`。播放时允许资源采样率不同于输出采样率，底层自动重采样。

加载失败抛出异常。`fromFile` 和 `fromPcm` 可在后台线程创建资源，完成后通过应用自己的同步方式交给音频控制线程。相同文件的多次 `fromFile` 不建立全局缓存；应用应缓存并复用 `AudioClip`。流式资源保存绝对文件路径，播放期间源文件必须可读，删除或更换源文件不受支持。

正在播放的实例保留资源引用，原 `AudioClip` 可以离开作用域；预加载成功后不再依赖源文件。静态资源数据可跨引擎共享，播放句柄只属于创建它的引擎。

## 混音分组与音乐切换

固定分组为 `Master`、`Music`、`Sfx`、`Ambient`、`UI`。四个子组汇入 Master，每个组独立保存音量与静音状态：

```cpp
audio.setBusVolume(AudioBus::Master, .8f);
audio.setBusVolume(AudioBus::Music, .5f);
audio.setBusMuted(AudioBus::Music, true);
audio.setBusMuted(AudioBus::Music, false); // 恢复先前 .5 的音乐音量。
```

增益按实例、淡变包络、分组和 Master 相乘。允许增益大于 1，但叠加后可能削波；本版不提供自动响度管理或限制器。`pitch` 范围为 0.125 到 8，变调同时变速，尚不提供保持音调的时间拉伸。

`fadeTo(voice, gain, seconds)` 调整实例的附加包络倍率，不覆盖其基础音量。`stop(voice, seconds)` 淡出并在结束后回收；暂停实例直接停止，淡出停止中的实例不能再次暂停或改写包络。将旧音乐淡出、同时用另一实例淡入即可交叉切换：

```cpp
audio.stop(musicVoice, 1);
background.fadeInSeconds = 1;
auto nextVoice = audio.play(nextMusic, background); // nextMusic 由应用预先加载。
```

交叉切换期间会同时使用两个实例，需为音乐预留并发容量。包络按音频采样时钟推进；暂停游戏逻辑或降低渲染帧率不会自动暂停音乐。应用按需要显式暂停实例或静音分组。

## 空间定位与游戏事件

默认采用非空间播放，适合音乐和 UI。空间音效建议使用单声道资源，配置世界位置与线性衰减距离：

```cpp
AudioPlayDesc step;
step.spatial = true;
step.position = playerPosition; // 应用拥有的角色世界坐标。
step.minDistance = 1;
step.maxDistance = 20;
step.maxInstancesPerClip = 4;
auto voice = audio.play(footstep, step);

AudioListenerDesc listener;
listener.position = playerPosition;
listener.forward = {0, 0, -1};
audio.setListener(listener);
```

坐标系与渲染数学接口一致：右手系、+Y 向上、-Z 向前。距离小于 minDistance 不衰减，达到 maxDistance 后静音。`setPosition` 更新已播放声源；监听器的 forward/up 会正交归一化，零向量及平行向量被拒绝。

监听器的位置与朝向由应用决定。图片角色游戏可以用玩家位置和固定世界朝向，避免相机拉远影响听觉距离。Billboard 的转向、缩放和落地点排序不改变音频。当前为基础空间化，不包含 HRTF、几何遮挡、混响区域或自动场景声源组件。

应用读取 `SpriteAnimator::takeEvents()` 的脚步事件后调用 `audio.play`；无需让 animation 链接 audio。碰撞、受击和按钮事件也由应用映射到音效，核心不解释这些玩法事件。

## 实例上限、事件和线程

`AudioEngineDesc::maxVoices` 默认为 64，包含暂停及正在淡出的实例。每次播放还可以设置 `maxInstancesPerClip`，按同一共享资源的身份限制并发；0 表示不限。新请求满额时，在允许替换的范围内选择优先级最低、同优先级最早的实例。优先级越大越重要；若所有候选优先级都更高，`play` 返回空句柄。新流式实例准备失败时保留原声音。

每帧在创建音频引擎的线程执行：

```cpp
audio.update();
for (const auto &event : audio.takeEvents()) {
    // event.voice 是原实例句柄，此时已失效。
    // event.reason 区分 Finished、Stopped、Stolen。
}
```

`takeEvents` 自身也执行 update，然后清空事件队列。请定期调用，以回收已结束的实例并消费事件；设备仍会在两次 update 之间持续混音。事件顺序为控制操作或轮询发现的顺序，同时自然结束的实例按槽位顺序报告，不表示采样级时间顺序。加载/初始化失败直接在调用线程抛出异常，不作为结束事件。

自然结束和淡出停止在 update 后使句柄失效；立即 stop/stopAll 或被抢占会立即失效。`valid` 只检查当前存活状态，不主动轮询结束。旧句柄、其他引擎句柄不能操作新实例；转为 bool 只检查非空。结束实例不能恢复，需要重新 play。

全部引擎控制方法在创建线程调用，移动后线程归属不变；后台资源准备使用独立的 `AudioClip` 创建函数。音频设备持续在底层回调中混音，流式读取通过 miniaudio 后台任务补页。用户代码不在实时回调中执行；播放准备、实例创建和回收可能分配内存、执行文件 IO 或等待底层安全分离，应放在应用线程而非音频回调。控制变化经过混音及设备缓冲后生效，不能撤回已经产生的采样。析构停止设备、释放实例，最后结束后台任务。

## 离线混音与验证

```cpp
AudioEngine offline({.sampleRate = 48000, .maxVoices = 64, .offline = true});
auto voice = offline.play(click);
std::vector<float> pcm(480 * 2); // 480 个立体声帧，约 10ms。
offline.render(pcm);
```

离线模式不打开音频设备，只有 render 推进音频时钟，输出为交错立体声浮点采样，不额外限幅。普通设备模式禁止调用 render。输出 span 长度必须为偶数；它可用于确定性 PCM 音效测试或由应用自行编码导出。

流式资源仍由后台任务读取，离线读取速度过快可能耗尽预取页；处理长文件时应给后台任务执行机会，或使用 Preload。音频测试通过真实 PCM 输出检查增益、暂停进度、包络、重采样、声道与空间方向、衰减、长文件补页和循环，并验证句柄/线程及文件生命周期。系统输出示例另行验证默认设备可启动和退出；自动 CTest 不依赖声卡或主观听音。当前实测平台为 Windows，其他平台尚未实机验证。

miniaudio 来源、文件哈希与许可证见[第三方说明](../THIRD_PARTY.md)，底层能力参考[官方手册](https://miniaud.io/docs/manual/)。
