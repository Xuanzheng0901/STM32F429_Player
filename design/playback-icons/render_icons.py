"""绘制 21×21 黑白模式图标，导出 LVGL I1 数据和预览（依赖 Pillow）。"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ASSETS = HERE.parents[1] / "User/components/UI/music_player_assets.c"


def repeat(single=False):
    image = Image.new("1", (21, 21), 1)
    draw = ImageDraw.Draw(image)
    draw.line([(2, 9), (2, 5), (17, 5)], fill=0, width=2)
    draw.polygon([(15, 1), (19, 5), (15, 9)], fill=0)
    draw.line([(18, 11), (18, 15), (3, 15)], fill=0, width=2)
    draw.polygon([(5, 11), (1, 15), (5, 19)], fill=0)
    if single:
        for y, row in enumerate(["00110", "01110", "00110", "00110", "11111"], 8):
            for x, pixel in enumerate(row, 8):
                if pixel == "1":
                    draw.point((x, y), fill=0)
    return image


def shuffle():
    image = Image.new("1", (21, 21), 1)
    draw = ImageDraw.Draw(image)
    draw.line([(1, 4), (4, 4), (15, 16), (18, 16)], fill=0, width=2)
    # 交叉处留白，避免两条路径在低分辨率下粘连。
    draw.line([(1, 16), (4, 16), (15, 4), (18, 4)], fill=1, width=4)
    draw.line([(1, 16), (4, 16), (15, 4), (18, 4)], fill=0, width=2)
    draw.polygon([(15, 0), (19, 4), (15, 8)], fill=0)
    draw.polygon([(15, 12), (19, 16), (15, 20)], fill=0)
    return image


def lvgl_data(name, image):
    # I1 调色板：0=不透明黑，1=不透明白；每行按整字节补齐。
    data = bytes([0, 0, 0, 255, 255, 255, 255, 255]) + image.tobytes()
    rows = ["    " + ", ".join(f"0x{x:02x}" for x in data[i:i + 16]) + ","
            for i in range(0, len(data), 16)]
    return (f"static const uint8_t {name}_data[] = {{\n" + "\n".join(rows) +
            f"\n}};\nconst lv_image_dsc_t {name} = {{\n"
            "    .header.magic = LV_IMAGE_HEADER_MAGIC,\n"
            "    .header.cf = LV_COLOR_FORMAT_I1,\n"
            "    .header.w = 21, .header.h = 21,\n"
            "    .header.stride = 3,\n"
            f"    .data_size = sizeof({name}_data),\n"
            f"    .data = {name}_data,\n}};\n\n")


def main():
    icons = [("repeat_one", "单曲循环", repeat(True)),
             ("repeat", "列表循环", repeat()),
             ("shuffle", "随机播放", shuffle())]
    for name, _, image in icons:
        image.save(HERE / f"{name}.png")

    source = ASSETS.read_text(encoding="utf-8")
    start = source.index("static const uint8_t music_player_repeat")
    end = source.index("static const uint8_t music_player_previous", start)
    generated = "".join(lvgl_data("music_player_" + name, image) for name, _, image in icons)
    ASSETS.write_text(source[:start] + generated + source[end:], encoding="utf-8")

    preview = Image.new("RGB", (480, 180), "white")
    draw = ImageDraw.Draw(preview)
    font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 18)
    for i, (_, label, image) in enumerate(icons):
        center = 80 + i * 160
        preview.paste(image.resize((84, 84), Image.Resampling.NEAREST), (center - 42, 20))
        draw.text((center, 120), label, font=font, fill="black", anchor="mt")
        preview.paste(image, (center - 10, 151))
    preview.save(HERE / "preview.png")


if __name__ == "__main__":
    main()
