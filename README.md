# Lo-Fi Player 100

WAV / MP3 / Opus music player for the CASIO fx-CG50 / Graph 90+E and fx-CG100 / Graph Math+ (OS 2.00).

**English summary.** Lo-Fi Player 100 is a gint add-in that plays WAV, MP3 and Ogg Opus files through the
calculator's 2.5 mm serial port: the TX pin is driven as a 1-bit DAC (512-bit PWM frames at 28.8 kHz), so a plain
2.5 mm → 3.5 mm adapter cable is enough to listen on headphones, with no extra circuit. One `LoFi100.g3a` runs on
both models (fx-CG50: install directly; fx-CG100: needs an add-in loader such as MPM). MP3 is decoded with libmad,
Opus with fixed-point libopus 1.3.1, both in real time; files stay in flash and are read through a map of their
NOR blocks. Build with the fxSDK (`fxsdk build-cg`, or `bash build.sh` with Docker); run the host tests with
`bash tests/run-all.sh`. Licensed under the GNU GPL version 2. The documentation below and the development log
in [CHANGELOG.md](CHANGELOG.md) / [NOISE.md](NOISE.md) are in Chinese.

---

面向 CASIO fx-CG50 / Graph 90+E 与 fx-CG100 / Graph Math+ **OS 2.00** 的 WAV / MP3 / Opus 播放器，通过 2.5 mm 串口的
TX 引脚输出音频。不需要任何外接电路：2.5 mm 转 3.5 mm 转接线直接接耳机。同一个 `LoFi100.g3a` 两种机型通用。
原名 CGplayer100（1.0.2 及以前）；串口编码思路源自 [ComputerNerd/CGplayer](https://github.com/ComputerNerd/CGplayer)（2014）。

当前版本 **1.1.11**。各版本的改动、实机测量与排查过程见 [CHANGELOG.md](CHANGELOG.md)，噪声来源汇总见 [NOISE.md](NOISE.md)。

## 安装

- **fx-CG50 / Graph 90+E：** 把 `LoFi100.g3a` 复制到存储根目录，从主菜单打开。
- **fx-CG100 / Graph Math+（OS 2.00）：** 需要已有 MPM 等 `.g3a` 加载环境；把 `LoFi100.g3a` 复制到存储，从加载器启动。
  串口经 OS 2.00 的固定入口打开，启动时核对代码签名，其他 OS 版本未适配。
- 不支持 fx-CG10 / 20（Prizm）与 fx-CG Manager。

音频文件放在存储里任意文件夹，启动后在文件列表中选择。

## 操作

下表是 fx-CG100 的键名；fx-CG50 上 OK → EXE、BACK → EXIT、TOOLS → OPTN、HOME → MENU，PAGE UP / DOWN → 左 / 右。

| 界面 | 按键 | 操作 |
|---|---|---|
| 文件选择 | 上 / 下、PAGE UP / DOWN | 选择、翻页（从播放页返回时光标停在原文件） |
| 文件选择 | OK / EXE | 进入文件夹或加载文件 |
| 文件选择 | BACK | 返回上级；根目录时退出插件 |
| 文件选择 | TOOLS | 刷新列表 |
| 播放器 | OK / EXE | 播放；WAV 从上次停下的位置继续，播完后从头开始 |
| 播放器（就绪、暂停或播放中） | 上 / 下 | 音量 1–24：1–16 衰减，17–24 增益 +1.5 dB/档（都经前瞻限幅器，切换平滑） |
| 播放器（就绪、暂停或播放中） | + | 切换等待负载：decode（默认）/ random / MAC，见下文 |
| 播放器 | 左 / 右 | WAV 起始位置前后移 5 秒（MP3 / Opus 只在播放中快进快退） |
| 播放器 | BACK / TOOLS | 返回文件选择 |
| 播放中 | OK / EXE | 暂停 / 继续 |
| 播放中 | 左 / 右 | 快退 / 快进 5 秒 |
| 播放中 | BACK / TOOLS / AC / HOME | 停止并返回文件选择 |
| 文件选择、播放器（fx-CG100） | SHIFT + AC（OFF） | 退出插件回到 MPM（关机请回到 MPM 或系统后再按）；播放中同 AC，先停止 |

播放期间不刷新屏幕，暂停 / 停止后显示真实进度。按住任意键时 gint 的键盘扫描会恢复运行，噪声会短暂变大，
松开约 35 ms 后恢复。fx-CG50 上用 EXIT 退出后可以直接再次打开（gint 的重启机制）；用 MENU 离开时插件保持运行。

## 支持的文件与转换

- **WAV：** RIFF PCM，单声道，无符号 8-bit 或有符号 16-bit，4 000–48 000 Hz，允许非 44 字节文件头和未知块；
  拒绝截断和不支持的格式。
- **MP3：** libmad 定点解码，单声道或立体声（混合为单声道）。
- **Opus：** libopus 1.3.1 定点解码子集，Ogg 封装，mapping family 0（单声道、立体声），以 24 kHz 解码。

### Opus（推荐，体积最小）

```sh
ffmpeg -i input.flac -ac 1 -af "volume=-3dB" -ar 48000 -c:a libopus -b:a 32k output.opus
```

单声道 32 kbps 约 0.25 MB/分钟，是 256 kbps 立体声 MP3 的约 1/8。解码器自带 4 dB 余量，超出满幅的峰由限幅器
压下而不是削波；`-3dB` 仍建议保留，限幅器动作越少越干净（Opus 头里的输出增益在解码器饱和之后才生效，代替不了它）。

### WAV

```sh
ffmpeg -n -i input.flac -ac 1 -c:a pcm_s16le output.wav
# 或指定采样率（44100 / 48000 / 28800 均可）
ffmpeg -n -i input.flac -ac 1 -ar 44100 -c:a pcm_s16le output.wav
```

8-bit 文件按 16-bit 路径以 256 倍放大处理，但自带约 −48 dBFS 的抖动本底。音量偏低的音源可在转换时加限幅 / 响度
归一（如 `loudnorm`），信噪比随电平直接改善。`tools/` 里有生成测试音的脚本（如 `make-tone16.py`：1 kHz、−6 dBFS、
16-bit、28 800 Hz、10 秒）。

## 工作原理

串口 TX 是一个 1-bit DAC：SCIF 同步模式、`CA=1, CKS=0, SCBRR=0`，位率 Pφ/4。耳机是低通滤波器，只对电平的短时
平均值有反应。

**时钟。** 启动时套用 Ptune 4 F5 配置（CPU 235.93 MHz、总线 117.96 MHz、Pφ 29.4912 MHz），由 gint 的 CPG/BSC 保存
恢复机制在切入 OS 或退出时恢复。每次播放前把 Pφ 翻倍到 58.9824 MHz（`src/overclock.c`），SCIF 位率随之变为
14.7456 Mbit/s，播放结束后切回；`play_session` 核对 Pφ/4 = 28 800 × 512，不符则拒绝播放。OS 的扩频时钟
（SSCGCR.SSEN，约 9.26 kHz 调制）会带来蜂鸣和 1/64 的音高偏低，播放期间关闭。

**编码（`src/sdm.h`）。** 每秒 28 800 帧，每帧 512 位，帧内一个居中的高脉冲，脉宽 n 就是样本值。端口驱动器的
实测特性决定了这种形式：短于约 1.6 µs 的脉冲几乎不传出，每个脉冲前沿有一段随前面低电平长短变化的损失；长脉冲、
少翻转的 PWM 受影响最小。

- 工作点 n = 48..464（中点 256）：脉冲和间隙都不短于 3.3 µs。
- 16-bit 样本量化为 n：±0.5 LSB 的三角抖动加半阶误差反馈（噪声传递函数 1 − 0.5 z⁻¹）。
- 脉冲居中，质心不随 n 移动（左 / 右对齐等于最大 ±7 µs 的延时调制）。
- 每帧在环形缓冲上直接生成（两条 32 字节缓存行，`movca.l` + `ocbp`），不查表。

**流水线（`src/mp3-live.c`）。** 解码器或 WAV 读取 → 流式调制器（`src/sdm-stream.c`：非 28 800 Hz 的源经 Kaiser 窗
sinc 多相滤波器重采样；音量、+4 dB 解码余量补偿与前瞻限幅器）→ 128 KiB 环形缓冲 → DMAC 通道 5（SCIF 发送请求，
32 KiB 一块，DEI5 中断"先装后算"续装；不可用时退回 TMU 中断供数）→ SCIF。调制器每次只写一帧，生产节奏与帧同步。

**等待负载。** TX 的高电平就是 I/O 电源，电源随 CPU / 总线负载起伏，所以生产者等环空间时做什么会影响噪声。
默认 decode：用第二个解码器反复解最后一包（MP3 为最后一帧的合成），让负载不随解码节奏起伏，去掉 50 Hz（Opus）/
38 Hz（MP3）的嗡嗡声，代价是宽带嘶声约 +2 dB、CPU 满载。random 按随机强度做缓存内乘加，把电源峰抹开；MAC 是
固定的缓存内乘加，静音时嘶声最少。不同机型电源网络的响应不同，同一负载的噪声形状也不同（fx-CG50 较平，
fx-CG100 在 5–6 kHz 有一个鼓包），见 CHANGELOG 1.1.11。

**播放代码在 RAM 里运行。** 加载项链接在虚拟地址 0x00300000：fx-CG100 上 MPM 把整个映像拷进 SDRAM，fx-CG50 上
OS 则直接从 NOR 逐页映射，每次缺页都在屏蔽中断的情况下调用 OS 映射例程，DMA 续装会被推迟，线路断流。所以
解码器、调制器、送数、读文件的代码与查表，以及它们用到的 libgcc / fxlibc 运行库函数，都在构建时改放到 `.data`
（gint 启动时拷进用户 RAM，`main()` 开头写回给取指；详见 `CMakeLists.txt`），播放期间不执行 ROM 里的代码，
两种机型的行为一致。

**按键。** 播放期间暂停 gint 的键盘定时器（它的扫描会造成咔哒声），直接读 KEYSC：每 64 帧（2.2 ms）按 gint 的方式
顺序读全部 6 个数据字；任何键按下都让键盘驱动恢复运行，全部松开 35.6 ms 后再暂停。暂停时键盘驱动运行，事件队列和
直接读取都接受按键。

## 加载与内存

WAV 优先按文件大小申请 RAM；失败时用 BFile 逐块读取并在 32 MiB NOR 中查找每个 512 字节块的物理位置，关闭文件后
直接从 Flash 读取（支持碎片文件；找不到完全匹配的块则拒绝播放，BACK 可取消）。5 MiB 文件的索引约 40 KiB。MP3 和
Opus 始终使用索引路径，播放期间不调用 OS 文件接口；没有整首 PCM 缓存。播放时堆上约需 130 KiB 的发送缓冲加解码器
状态（Opus 约 90 KiB，含 24 KiB 伪栈与第二个填充解码器）。

串口：fx-CG100 OS 2.00 的入口 Open `0x8030A358`、Close `0x8030A3F6`、IsOpen `0x8030A89E`，启动时核对三处 16 字节代码
签名；fx-CG50 用 syscall `0x1BB7` / `0x1BB8` / `0x1BC6`。

## 构建与测试

需要 [fxSDK](https://git.planet-casio.com/Lephenixnoir/fxsdk)（sh-elf-gcc 14.1）与 [gint](https://git.planet-casio.com/Lephenixnoir/gint)
（本版本用提交 `2bc6f7e` 构建）。

```sh
fxsdk build-cg              # 本机已装 fxSDK 时
bash build.sh               # 或在含 fxSDK 的 Docker 镜像里构建（FXSDK_IMAGE 指定镜像）
```

输出为 `dist/LoFi100.g3a`（不纳入版本库，发布时作为附件）。

```sh
bash tests/run-all.sh             # 全部主机测试：C 单元测试、语法检查、流水线、Opus 跳转、调制器 SNR、MP3 集成
bash tests/run-all.sh --docker    # 另跑需要 fxSDK 镜像的两项：按键逻辑（GNU ld）与 SH4 模拟器上的帧生成
bash tests/make-fixtures.sh       # 重新生成合成的 Opus 测试文件（需要带 libopus 的 ffmpeg）
```

主机测试需要 C 编译器、Python 3（numpy、scipy）与 ffmpeg。主机测试覆盖调制器、重采样、解码、流水线帧数和按键
逻辑；音质、实时性和稳定性只能靠实机录音判断。

## 已知限制

- SH7305 的 Pφ 上限没有公开资料（同系 SH7724 规格为 33 MHz）；58.98 MHz 下长时间播放尚无问题报告，但没有系统测过。
- 插件空闲、串口关闭时（文件列表、播放前后），线路会拾取屏幕驱动的约 7.3 kHz 干扰，播放中没有。
- 量化与电源噪声是现在的本底，A 计权约 −51…−58 dB（相对线路载波，视机型与是否有音乐）；两种机型的噪声形状不同。
- 转接线右声道接的是计算器 RX 引脚，无声或微弱串扰；输出为单声道。
- 输出平均电平为轨中点（约 1.65 V 直流经端口串联电阻加在音圈上），高阻耳机更稳妥。没有模拟音量，只有数字音量。

## 目录

| 路径 | 内容 |
|---|---|
| `src/` | 播放器源码（`main.c` 界面与文件，`mp3-live.c` 流水线，`sdm*.{c,h}` 调制器，`tx-*` 环形缓冲与 DMA，`nor-source.c` 读文件，`mp3-decode.c` / `opus-file.c` 解码层） |
| `vendor/` | libmad（GPL-2.0-or-later）与 libopus 1.3.1（BSD-3-Clause），来源与补丁见各自的 `UPSTREAM.md` |
| `tests/` | 主机测试与 gint 桩头文件，合成测试素材在 `tests/fixtures/` |
| `tools/` | 生成测试音、转换 WAV 的脚本 |
| `assets/icons/` | 菜单图标（PNG 与可编辑 SVG） |

## 许可证与第三方

本项目以 GNU GPL 第 2 版发布，见 [COPYING](COPYING)。构建产物链接或包含：

- libmad（GPL-2.0-or-later），`vendor/libmad`，改动见 `vendor/*.patch`；
- libopus 1.3.1 定点解码子集（BSD-3-Clause），`vendor/opus`；
- [gint](https://git.planet-casio.com/Lephenixnoir/gint)（作者声明为自由软件）与 fxlibc（CC0）；fxlibc 的 `mem*` 与 libgcc
  的除法 / clz 目标文件在构建时复制进 RAM（libgcc 以 GCC 运行库例外条款发布）；
- 图标改编自 Hi-Res Audio 标志（Wikimedia 标注 PD-textlogo），见 `assets/icons/README.md`。

## 参考

- 串口编码上游：[ComputerNerd/CGplayer](https://github.com/ComputerNerd/CGplayer)，版本 `55678463c2ca675c4afc56b14bf609f73ec4428d`
  （来源树未包含 LICENSE）。本仓库不包含其源文件，沿用的是思路：串口作 1-bit 输出、在 NOR 中逐块查找文件的物理位置。
- fx-CG100 串口入口来源：[NESizm/Prizoop 移植作者](https://www.planet-casio.com/Fr/forums/topic19383-1-nesizm-and-prizoop-now-on-fx-cg100graph-math.html)。
- KEYSC 寄存器资料：[Casiopeia: Unraveling the secrets of the KEYSC](https://www.casiopeia.net/forum/viewtopic.php?f=25&p=20596&t=7329)。
- fx-CG50 回到主菜单的机制：gint 的 `gint_osmenu()` / `gint_setrestart()`（Dr-Carlos 的逆向）。
- 0.2.1 的界面曾参考 [Cemetech 的 Audio Player 改进版](https://www.cemetech.net/forum/viewtopic.php?t=20477&start=0)，后简化为文字界面。
