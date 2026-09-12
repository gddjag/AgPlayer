"""Generate side-by-side comparison images from source and implementation shots.

Usage:
    python tools/make_comparison.py \
        --source docs/qa/screenshots/main-source.jpg \
        --impl docs/qa/screenshots/main-implementation-v2.png \
        --output docs/qa/screenshots/main-comparison-v2.png
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("Pillow is required: pip install Pillow\n")
    sys.exit(1)


def resize_to_height(img: Image.Image, target_height: int) -> Image.Image:
    if img.height == target_height:
        return img
    aspect = img.width / img.height
    target_width = max(1, int(round(target_height * aspect)))
    return img.resize((target_width, target_height), Image.LANCZOS)


def build_comparison(source_path: Path, impl_path: Path, output_path: Path,
                     gap: int = 24, bg: tuple = (32, 32, 32)) -> None:
    source = Image.open(source_path).convert("RGB")
    impl = Image.open(impl_path).convert("RGB")

    target_height = min(source.height, impl.height)
    source_r = resize_to_height(source, target_height)
    impl_r = resize_to_height(impl, target_height)

    total_width = source_r.width + gap + impl_r.width
    canvas = Image.new("RGB", (total_width, target_height), bg)
    canvas.paste(source_r, (0, 0))
    canvas.paste(impl_r, (source_r.width + gap, 0))

    canvas.save(output_path, "PNG")
    print(f"wrote {output_path} ({canvas.width}x{canvas.height})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--impl", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    build_comparison(args.source, args.impl, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
