#!/usr/bin/env python3
"""
生成界面用的精简中文点阵字库 -> main/cn_font.h

用 NotoSansSC 字体把 CHARS 里的字符渲染成 16x16 单色点阵，
每个字符 16 行、每行 16 bit -> uint16_t rows[16]（bit15 最左、bit0 最右）。
display_driver.cc 按 UTF-8 解码后查表绘制。

字体来源：原小智工程自带的 NotoSansSC-Regular.ttf（备份目录）。
用法：python3 tools/gen_cn_font.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

FONT = os.path.join(
    os.path.dirname(__file__), "..", "..",
    "ESP32S3-Jarvis.BACKUP-20261119", "managed_components",
    "lvgl__lvgl", "tests", "src", "test_files", "fonts", "noto",
    "NotoSansSC-Regular.ttf",
)
FONT = os.path.abspath(FONT)

# 自动扫描 main/ 下所有源码里的非 ASCII 字符（含注释），保证绝不缺字
MAIN_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "main"))
CN = set()
for fn in os.listdir(MAIN_DIR):
    if fn.endswith((".cc", ".cpp", ".h", ".c")) and fn != "cn_font.h":
        with open(os.path.join(MAIN_DIR, fn), encoding="utf-8", errors="ignore") as f:
            for ch in f.read():
                if ord(ch) > 0x7F:
                    CN.add(ch)
ASCII = "".join(chr(c) for c in range(0x20, 0x7F))  # 空格 ~ ~

CHARS = sorted(CN | set(ASCII), key=ord)

SIZE = 16
SS = 4  # 超采样倍数，抗锯齿后二值化更平滑

if not os.path.isfile(FONT):
    raise SystemExit(f"字体不存在: {FONT}")


def glyph_rows(ch):
    hi = SIZE * SS
    font = ImageFont.truetype(FONT, hi, index=0)
    img = Image.new("L", (hi, hi), 0)
    d = ImageDraw.Draw(img)
    # getbbox: (x0,y0,x1,y1)，中文字形可能有负偏移，居中绘制
    try:
        box = d.textbbox((0, 0), ch, font=font)
    except Exception:
        box = (0, 0, hi, hi)
    w, h = box[2] - box[0], box[3] - box[1]
    ox = (hi - w) // 2 - box[0]
    oy = (hi - h) // 2 - box[1]
    d.text((ox, oy), ch, font=font, fill=255)
    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    px = img.load()
    rows = []
    for y in range(SIZE):
        v = 0
        for x in range(SIZE):
            if px[x, y] >= 110:            # 二值化阈值
                v |= (1 << (SIZE - 1 - x))  # bit15 = 最左列
        rows.append(v)
    return rows


out = []
out.append("/* 自动生成，勿手改。工具: tools/gen_cn_font.py */")
out.append("#pragma once")
out.append("#include <stdint.h>")
out.append("")
out.append(f"#define CN_FONT_SIZE {SIZE}")
out.append(f"#define CN_FONT_COUNT {len(CHARS)}")
out.append("")
out.append("typedef struct {")
out.append("    uint16_t unicode;   /* UTF-16/Unicode 码点 (BMP 内) */")
out.append(f"    uint16_t rows[{SIZE}];  /* 每行 16bit，bit15=最左列 */")
out.append("} cn_glyph_t;")
out.append("")
out.append("static const cn_glyph_t cn_font[CN_FONT_COUNT] = {")
for ch in CHARS:
    cp = ord(ch)
    rows = glyph_rows(ch)
    rstr = ",".join(f"0x{v:04X}" for v in rows)
    label = ch if 0x20 <= cp <= 0x7E and ch not in "\\" else f"U+{cp:04X}"
    out.append(f"    {{0x{cp:04X}, {{{rstr}}}}},  // {label}")
out.append("};")
out.append("")

dst = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "main", "cn_font.h"))
with open(dst, "w", encoding="utf-8") as f:
    f.write("\n".join(out))
print(f"已生成 {dst}")
print(f"字符数 {len(CHARS)}，文件 {os.path.getsize(dst)} 字节")
