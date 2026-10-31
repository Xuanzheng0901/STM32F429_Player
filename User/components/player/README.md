# Player 前端交互

player.c 接收 UI 的浏览、选曲、暂停和循环命令，管理曲库任务及卡插拔流程，通过 player_callbacks_t 发布窗口、播放状态、介质状态和弹窗事件。支持 player_init(NULL) 无界面运行。

- music_library.c：递归扫描、隐藏路径索引和 15 首窗口。
- music_tags.c：ID3、WAV INFO 和 FLAC Vorbis Comment 标签。
- music.h、player_events.h：模块共享的数据和事件定义，不包含任务或硬件操作。
- music 任务：优先级 1、普通 SRAM 静态 16 KiB 栈；每 20 ms 检查介质，执行挂载、扫描及 UI 回调。

播放控制统一调用 audio/playback.h。前端不持有音频命令队列、DMA 缓冲、解码器或正在播放的文件；音频任务不获取 LVGL 锁。拔卡时先调用 playback_stop 等待文件关闭，再卸载卷；USB 模式的停止确认只针对 SD 文件，不中断 USB 输出。

启动和重新插卡只选第一首，USB 退出后从当前曲库窗口选一首，均保持暂停。挂载失败提示错误，下次插卡重试。连接、耳机等音频事件由 playback_notice 读取，在曲库任务中转发给 UI；短暂连接/断开不会因状态合并漏掉 SD 选曲恢复。

文件解码见 [decoder](../decoder/README.md)，音频任务与硬件输出见 [audio](../audio/README.md)。构建和测试见 [应用说明](../README.md) 与 [主机验证](../../../tests/README.md)。
