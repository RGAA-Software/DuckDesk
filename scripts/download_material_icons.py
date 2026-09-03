#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 Google Fonts (Material Symbols) 下载图标,处理为 Qt 兼容 SVG。

来源: https://fonts.google.com/icons
下载地址(Material Symbols 官方 CDN,与网页版"下载 SVG"同一份):
  https://fonts.gstatic.com/s/i/short-term/release/<style>/<name>/default/<size>px.svg
  style: materialsymbolsoutlined / materialsymbolsrounded / materialsymbolssharp
       (旧版 Material Icons: materialicons / materialiconsoutlined / materialiconsround / ...)

Qt 兼容处理(Qt QSvgRenderer 只支持 SVG Tiny 1.2 子集):
  - 把颜色烘焙到 <svg> 根的 fill 属性(Qt 对 CSS/currentColor 支持差)
  - 清掉 path 上的 fill,统一从根继承
  - 保留 width/height/viewBox,不带任何脚本/样式

用法:
  # 直接列图标(统一颜色):
  python scripts/download_material_icons.py --out src/px_client/modules/file_transfer/icons \
      --color "#ffffff" home arrow_upward refresh

  # 清单文件(每行: <图标名> [颜色],空行/#注释忽略):
  python scripts/download_material_icons.py --out <dir> --manifest icons.txt

  # 完整参数:
  python scripts/download_material_icons.py --out <dir> \
      --style materialsymbolsrounded --size 24 --prefix ic_ \
      --color "#666666" home refresh

产物: <out>/<prefix><name>.svg,例如 ic_home.svg
"""

import argparse
import re
import sys
import urllib.request
import xml.etree.ElementTree as ET

SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)

BASE_URL = ("https://fonts.gstatic.com/s/i/short-term/release/"
            "{style}/{name}/default/{size}px.svg")


def normalize_color(color: str) -> str:
    c = color.strip()
    if re.fullmatch(r"#[0-9a-fA-F]{6}", c):
        return c.lower()
    if re.fullmatch(r"[0-9a-fA-F]{6}", c):
        return "#" + c.lower()
    raise ValueError(f"颜色必须是 #RRGGBB: {color!r}")


def fetch_svg(name: str, style: str, size: int) -> bytes:
    url = BASE_URL.format(style=style, name=name, size=size)
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        if resp.status != 200:
            raise RuntimeError(f"HTTP {resp.status}")
        return resp.read()


def process_svg(data: bytes, color: str) -> bytes:
    root = ET.fromstring(data)
    if not root.tag.endswith("svg"):
        raise RuntimeError("不是 SVG 文档")
    # 颜色烘焙到根节点,子元素 fill 去掉以继承
    root.set("fill", color)
    for el in root.iter():
        if el is not root and "fill" in el.attrib:
            del el.attrib["fill"]
    return ET.tostring(root, encoding="utf-8", xml_declaration=False) + b"\n"


def parse_manifest(path: str):
    items = []
    with open(path, "r", encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            name = parts[0]
            color = parts[1] if len(parts) > 1 else None
            items.append((name, color, lineno))
    return items


def main() -> int:
    ap = argparse.ArgumentParser(description="下载 Google Fonts Material 图标并处理为 Qt 兼容 SVG")
    ap.add_argument("icons", nargs="*", help="图标名列表(与 --manifest 二选一或并用)")
    ap.add_argument("--manifest", help="清单文件:每行 '<图标名> [颜色]'")
    ap.add_argument("--out", required=True, help="输出目录")
    ap.add_argument("--style", default="materialsymbolsrounded",
                    help="materialsymbolsoutlined/rounded/sharp 或 materialicons 系列")
    ap.add_argument("--size", type=int, default=24, help="边长(20/24/40/48)")
    ap.add_argument("--color", default="#666666", help="默认填充色 #RRGGBB(清单可逐行覆盖)")
    ap.add_argument("--prefix", default="ic_", help="输出文件名前缀")
    args = ap.parse_args()

    import os
    os.makedirs(args.out, exist_ok=True)

    # (name, color) 列表;清单颜色优先于 --color
    jobs = []
    for n in args.icons:
        jobs.append((n, args.color))
    if args.manifest:
        for name, color, _ in parse_manifest(args.manifest):
            jobs.append((name, color or args.color))
    if not jobs:
        ap.error("没有要下载的图标(位置参数或 --manifest 至少给一个)")

    failed = 0
    for name, color in jobs:
        try:
            color = normalize_color(color)
            raw = fetch_svg(name, args.style, args.size)
            out = process_svg(raw, color)
            out_path = os.path.join(args.out, f"{args.prefix}{name}.svg")
            with open(out_path, "wb") as f:
                f.write(out)
            print(f"OK  {name} ({color}) -> {out_path}")
        except Exception as e:  # noqa: BLE001
            failed += 1
            print(f"ERR {name}: {e}", file=sys.stderr)

    if failed:
        print(f"{failed} 个图标失败", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
