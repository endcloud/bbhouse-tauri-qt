#!/usr/bin/env python3
"""Generate padded macOS PNG and ICNS from the preserved original (requires Pillow and iconutil)."""
from pathlib import Path
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ICONS = ROOT / "app/resources/icons"
CANVAS = 1024
ARTWORK = 824  # Approx. 80% optical size, with 100 transparent pixels on each side.


def main():
    source = Image.open(ICONS / "source/bbhouse-icon-1024-mac.png").convert("RGBA")
    if source.size != (CANVAS, CANVAS):
        raise ValueError("Expected a 1024x1024 macOS source icon")
    icon = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    artwork = source.resize((ARTWORK, ARTWORK), Image.Resampling.LANCZOS)
    inset = (CANVAS - ARTWORK) // 2
    icon.alpha_composite(artwork, (inset, inset))
    scratch = ROOT / "build"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="macos-icon-", dir=scratch) as temp:
        temp = Path(temp)
        iconset = temp / "bbhouse.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                suffix = "@2x" if scale == 2 else ""
                pixels = size * scale
                icon.resize((pixels, pixels), Image.Resampling.LANCZOS).save(
                    iconset / f"icon_{size}x{size}{suffix}.png")
        output = temp / "icon.icns"
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(output)], check=True)
        icon.save(ICONS / "bbhouse-icon-1024-mac.png")
        (ICONS / "icon.icns").write_bytes(output.read_bytes())


if __name__ == "__main__":
    main()
