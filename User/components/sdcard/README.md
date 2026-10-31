# SD 卡生命周期

卷只由本模块持有，曲库任务是唯一挂载、扫描和卸载调用者。模块本身不创建任务或锁；文件间的互斥由 FatFs 提供。

sdcard_poll 每 20 ms 调用，返回 SDCARD_UNCHANGED、SDCARD_INSERTED 或 SDCARD_REMOVED。启动和重新插卡都要求三次连续有卡，拔卡沿用 sdmmc 的失效锁存。sdcard_is_removed 供音频任务立即判断旧卡是否失效，快速重插也不恢复旧句柄。

sdcard_mount_and_scan 重新建立介质会话、挂载并重建隐藏索引。失败会卸载。sdcard_load_music_window 按中心序号读取 15 首窗口。sdcard_unmount 清除曲库可用状态并卸载卷。

播放器收到拔卡事件后先禁止新控制，发出停止命令，等音频任务停止 I2S DMA、关闭当前 FIL 并确认，最后调用 sdcard_unmount。扫描期间拔卡，底层传输会退出，扫描自身关闭索引及目录；不在其他任务中强行卸载正在使用的卷。

插卡失败不反复重试；重新拔插后再试。成功后只选中第一首，用户确认播放。
