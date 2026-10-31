# 播放模式图标

三个图标使用 21×21 黑白像素，适配现有墨水屏按钮。`repeat_one` 为单曲循环，
`repeat` 为列表循环，`shuffle` 为随机播放。UI 切换模式时直接更换图像。

安装 Pillow 后，在仓库根目录执行：

```powershell
python design/playback-icons/render_icons.py
```

脚本更新 `User/components/UI/music_player_assets.c` 中的模式图标数据，并输出
三个原尺寸 PNG 和 `preview.png`（上方放大 4 倍，下方原尺寸）。预览标注字体
使用 Windows 微软雅黑；固件图标不依赖字体。
