# 文件模块主机验证

内存模拟 16 MiB SD 卡，使用生产 FatFs、sdmmc、曲库和标签代码，HAL 与 FreeRTOS 由 `mock_platform.c` 提供。

```powershell
cmake -S tests/fatfs_port -B build/fatfs-port-tests -G Ninja -DCMAKE_C_COMPILER=F:/DevTools/mingw64/bin/gcc.exe
cmake --build build/fatfs-port-tests -j 8
ctest --test-dir build/fatfs-port-tests --output-on-failure
```

| 测试 | 实际编译范围与覆盖 |
| --- | --- |
| `diskio` | 单独链接 diskio 和 sdmmc；初始化、DMA 地址边界、直接/中转读写、8/511 扇区分批、tick 回绕、完成事件、错误/超时/中止、移除锁存及恢复、ioctl；只通过公开接口验证 |
| `tags` | 链接 FatFs 和 metadata，不编译 sdcard/library；ID3v1/v2 优先级、v2.2/v2.3/v2.4、unsync、WAV INFO、Vorbis、Unicode/CP936、非法文本、256 个块以后的字段、容器子片段越界、逐次 DMA 故障注入 |
| `library` | 链接曲库及下层，不编译 sdcard/player；验证独立扫描、文件名回退、窗口和错误清空，不创建任务 |
| `filesystem` | 完整文件链；格式化、中文 UTF-8 长路径、跨扇区读写、隐藏索引、重复重建、20 首曲库首尾窗口、损坏/截断索引、目录深度及父路径恢复、空曲库、挂载失败和拔卡后重新挂载 |
| `removal` | FatFs、sdmmc、sdcard 和真实 player；模拟音频接口，验证无卡启动后插卡自动挂载、播放/暂停中移除、快速重插、音频归还先于卸载、前端清空和新控制抑制 |

所有主机目标使用 `-Wall -Wextra -Werror`，断言始终开启。模拟 SRAM 地址范围只覆盖测试 `mock_sram`；中转区通过对齐、分批、数据往返及链接地址共同验证，不依赖生产文件的静态变量。

当前 7/7 通过（含 removal、removal_paused、removal_quick），Debug/Release 和文件系统完整链接通过。实体卡座、电气、SDIO/DMA 中断及实际音频停止需要上板验证。
