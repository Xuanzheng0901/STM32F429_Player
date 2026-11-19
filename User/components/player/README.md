# Player 前端交互

player.c 接收 UI 的浏览、选曲、暂停和循环命令，管理曲库任务及卡插拔流程，通过 player_callbacks_t 发布窗口、播放状态、介质状态和弹窗事件。支持 player_init(NULL) 无界面运行。

- music_library.c：递归扫描、隐藏路径/标签索引和 15 首窗口。
- music_tags.c：ID3、WAV INFO 和 FLAC Vorbis Comment 标签。
- music.h、player_events.h：模块共享的数据和事件定义，不包含任务或硬件操作。
- music 任务：优先级 1、普通 SRAM 静态 16 KiB 栈；每 20 ms 检查介质，执行挂载、扫描及 UI 回调。

播放控制统一调用 audio/playback.h。前端不持有音频命令队列、DMA 缓冲、解码器或正在播放的文件；音频任务不获取 LVGL 锁。拔卡时先调用 playback_stop 等待文件关闭，再卸载卷；USB 模式的停止确认只针对 SD 文件，不中断 USB 输出。

启动和重新插卡只选第一首，USB 退出后从当前曲库窗口选一首，均保持暂停。挂载失败提示错误，下次插卡重试。连接、耳机等音频事件由 playback_notice 读取，在曲库任务中转发给 UI；短暂连接/断开不会因状态合并漏掉 SD 选曲恢复。

文件解码见 [decoder](../decoder/README.md)，音频任务与硬件输出见 [audio](../audio/README.md)。构建和测试见 [项目简介](../../../README.md) 与 [主机验证](../../../tests/README.md)。

播放完成与模式：player_callbacks_t.finished(index) 在曲库任务中每次自然 EOF 通知一次。停止、拔卡、解码错误和 USB 不触发；回调可用 player_request 显式选曲，该请求优先于自动切歌。选曲/播放控制编号会过滤过期状态，避免旧曲 EOF 覆盖新选曲。

- PLAYER_PLAY_LIST：默认模式，按索引顺序播放，最后一首回到第一首。
- PLAYER_REPEAT_ONE：自然结束后重播当前曲目。
- PLAYER_SHUFFLE：整轮遍历全曲库且不重复，跨轮不连续播放同曲；只有一首时允许重播。
- PLAYER_PLAY_ONCE：保留原有播完停止能力；旧 player_set_repeat(false/true) 分别映射到播完停止/单曲循环。

用 player_set_mode 切换模式。UI 循环图标按列表（L）→单曲（1）→洗牌（R）循环；手动选曲、切换模式、重新插卡或退出 USB 后重建洗牌轮次，起播曲目算本轮第一首。player_shuffle.c 使用带随机密钥的可逆置换过滤越界值，仅保留固定大小状态，不分配曲库大小的数组；这是无重复遍历，不承诺所有排列等概率。

建库阶段读取标签，music.idx 每条保存完整 music_info_t（当前 1168 B，含 1024 B UTF-8 路径），每次插卡重建，不兼容旧索引也无需保留。扫描共用一个歌曲对象，不为每层目录分配整份歌曲数据；Debug 扫描栈帧 936 B/层、最多五层，重建栈帧 1752 B，曲库任务仍用原 16 KiB 栈。

建库结束后保持只读索引打开。播放中翻页只做索引定位和逐条记录读取，每条读取后释放 FatFs 卷锁，不再查目录、打开歌曲或解析标签；因此建库时间增加，播放期间的文件系统竞争减少。卸载前必须调用 music_library_close，sdcard_unmount 已接入关闭；重建及读错误同样会关闭旧句柄。持续只读索引占一个文件锁槽，音频文件另占一个，不增加任务、队列或动态缓存。

停止确认最多等待 2 秒，并核对停止命令编号；超时只提示错误，保留旧卷，不执行卸载或重新挂载，避免与仍在访问文件的音频任务并发。后续重新插卡可再次请求停止；迟到的旧确认不会被当作本次确认。
