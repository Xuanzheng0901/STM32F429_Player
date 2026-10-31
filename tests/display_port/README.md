# Display / Log 主机测试

使用原生 GCC 编译生产模块，HAL 和 FreeRTOS 接口由 `mocks/` 提供。

```sh
cmake -S tests/display_port -B build/display-port-tests -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build/display-port-tests -j 8
ctest --test-dir build/display-port-tests --output-on-failure
```

| 测试 | 覆盖内容 |
| --- | --- |
| `ssd1685_0` / `ssd1685_1` / `ssd1685_2` | 全刷、快刷和局刷模式；RAM 地址及分块传输、参考帧同步、相同画面跳过、输入跨度、休眠恢复 |
| `knob` | 16 位 TIM3 计数回绕、正反方向、档位余数和超过 65535 的软件档位累计 |
| `log_format` | 长文本及长 tag 截断、字符串长度及颜色复位、日志过滤、16 块内存池耗尽与复用、UART 完成中断通知及调度标志 |
