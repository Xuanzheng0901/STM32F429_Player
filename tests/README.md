# F429 主机验证

Windows 使用 MinGW GCC。先把编译器 bin 目录加入当前进程 PATH；以下示例按本机安装路径：

    $env:PATH = 'F:/DevTools/mingw64/bin;' + $env:PATH

各测试目录分别配置、编译和运行：

    cmake -S tests/fatfs_port -B build/fatfs-tests -G Ninja -DCMAKE_C_COMPILER=F:/DevTools/mingw64/bin/gcc.exe
    cmake --build build/fatfs-tests -j 8
    ctest --test-dir build/fatfs-tests --output-on-failure

把源目录和构建目录分别替换为 tests/audio_player / build/audio-tests、tests/music_ui / build/ui-tests、tests/player_control / build/control-tests、tests/display_port / build/display-port-tests、tests/audio_io / build/io-tests 可运行其余套件。音频套件还需生成夹具（Python 3、ffmpeg 在 PATH）：

    python tests/audio_player/make_wav.py build/audio-tests/wav-fixtures
    python tests/audio_player/make_mp3.py build/audio-tests/mp3-fixtures
    python tests/audio_player/make_flac.py build/audio-tests/flac-fixtures

| 套件 | 数量 | 覆盖 |
| --- | ---: | --- |
| fatfs_port | 7 | 真实 R0.16、UTF-8/中文/超长路径、标签、索引、读写故障；无卡启动自动插卡、播放或暂停拔卡、快速重插、重复初始化前反初始化/复位、停止确认先于卸载；49 首跨窗口浏览 100 次，禁止任何 f_open 时仍能读取缓存标签和列表 |
| audio_player | 28 | 真实 FAT32 图片与逐样本 PCM 校验；WAV/MP3/FLAC、16/24/32 位、暂停/切歌、DMA 错误/超时/迟到、DAC 锁定、FLAC 缓存和 I2S 时钟；20 秒 96k/24b 文件在 15 秒后注入 20 ms 读卡延迟、80 ms 迟到仍报错 |
| music_ui | 2 | 真实 LVGL、列表焦点/滚动/边界、曲目信息所有权、状态更新、无卡/扫描/重新载入、通知队列和 USB 界面；实体编码器/确认短按缓存、上方五键映射、侧边音量键消抖/不连发，中键短按/800 ms 长按互斥 |
| player_control | 3 | Windows Fiber 保留任务循环；浏览合并、选曲、暂停、循环、EOF、完成回调、列表环绕/单曲循环/洗牌多轮不重复、过期完成事件和回调选曲优先、停止超时禁止卸载/挂载、迟到停止确认和多轮插拔、错误恢复、USB 独占/退出/期间插拔卡、耳机事件及无 UI 模式 |
| display_port | 5 | 全刷/快刷/局刷、编码器回绕、日志 |
| audio_io | 6 | UAC1 96k/32b 描述符及 776 B 端点、95/96/97 帧包、32 位低位及 I2S 半字顺序、满幅插值、缓存欠载/溢出/恢复、400 秒模拟漂移、转换中重启 USB 流、DMA/DAC 故障、接口 alt 切换与静音请求、耳机消抖和拔出中断静音 |
| heap_budget | 1 | 40 KiB RTOS 堆启动峰值、swdraw 线程、Idle 回收前挂载与反复重挂载 |
| battery | 1 | ADC 平均/二分之一分压还原、OCV 节点及单调性、30 秒周期、采样失败后恢复；UI 套件另验证电压/百分比/填充条 |

UI 测试按新布局检查左侧高亮边缘、浏览时右侧播放器像素不变及控件边界。64 位主机使用 32 KiB LVGL 堆，固件保持 24 KiB。需要检查实际绘制结果时，运行 `build/ui-tests/music_ui.exe build/ui-tests/layout.pbm`，可导出 360×184 的黑白预览。

字体扩充后，UI 测试还检查新增人名用字、繁体、扩展 A、增补平面汉字和全角字符，
确保由真实 LVGL 字库解析且不是占位框。运行
`build/ui-tests/music_ui.exe build/ui-tests/font-expanded.pbm expanded`
可导出新增字形的 UI 预览。

USB 类测试同时验证双声道 Input Terminal、Master/左/右 Feature Unit 控制项以及
AC/配置描述符总长度，避免只改流格式而遗漏控制拓扑；此检查已复现并覆盖原单声道残留。

当前共 53 项。音频夹具沿用 F407 工程生成的 FAT32 镜像及独立 FFmpeg PCM 参考；生成程序已迁入本工程，测试全部链接当前 F429 的应用及 FatFs 代码。主机 HAL/RTOS 为模拟接口，不是板上音频、真实热插拔时序、电气或吞吐证据。

堆预算测试使用生产 heap_4 分配器，运行命令和边界见 [heap_budget](heap_budget/README.md)。

音量控制回归验证继承启动衰减值、每次寄存器变化 8、64～192 上下界与反向操作顺序，
以及无 UI、USB 模式/拔卡时可调节；I2C 模拟写入必须发生在音频任务且不持有临界区。
192 处继续减音量静音，加音量取消静音并增大一档，中键静音保持衰减值，SD/USB 切换保留静音。
音量显示回调必须发生在曲库任务，启动时不提示、同值按键仍刷新提示。UI 回归检查
无文字竖条、静音空条、2 秒超时、连续按键重置计时和隐藏后恢复底图。运行
`build/ui-tests/music_ui.exe build/ui-tests/volume.pbm volume` 可导出右侧音量条预览。

显示协议回归还验证延后全刷请求：请求时不访问硬件，相同画面也全刷一次，随后恢复局刷。

电池套件使用 `tests/battery / build/battery-tests`，不需要音频夹具。

`Optimized` 固件预设的主机优化回归可单独运行，保留默认测试目录：

```powershell
cmake -S tests/audio_player -B build/audio-o3-tests -G Ninja -DCMAKE_C_COMPILER=F:/DevTools/mingw64/bin/gcc.exe -DAUDIO_TEST_OPTIMIZATION=-O3
python tests/audio_player/make_wav.py build/audio-o3-tests/wav-fixtures
python tests/audio_player/make_mp3.py build/audio-o3-tests/mp3-fixtures
python tests/audio_player/make_flac.py build/audio-o3-tests/flac-fixtures
cmake --build build/audio-o3-tests -j 8
ctest --test-dir build/audio-o3-tests --output-on-failure
cmake -S tests/audio_io -B build/io-o3-tests -G Ninja -DCMAKE_C_COMPILER=F:/DevTools/mingw64/bin/gcc.exe -DCMAKE_C_FLAGS=-O3
cmake --build build/io-o3-tests -j 8
ctest --test-dir build/io-o3-tests --output-on-failure
```

以上 28+6 项已在主机 `-O3` 下通过，包含独立 PCM 参考比对和 32 位 USB 输出。
主机测试将所测音频库整体设为 `-O3`；固件仅五个热点文件使用 `-O3`，
实际覆盖范围应以 `build/Optimized/compile_commands.json` 为准。
