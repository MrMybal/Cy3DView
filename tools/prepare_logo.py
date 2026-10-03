# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
"""Create icon sizes from the supplied artwork without changing its design.

Run manually after replacing cy3dview-logo-violet.png (requires Pillow).
Generated PNG/ICO files are source assets; regular builds do not require Pillow.
"""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
assets = root / "assets"
assets.mkdir(exist_ok=True)
source = Image.open(root / "cy3dview-logo-violet.png").convert("RGBA")
sizes = (16, 24, 32, 48, 64, 128, 256)
for size in sizes:
    image = Image.new("RGBA", (size, size))
    scaled = source.copy()
    scaled.thumbnail((size, size), Image.Resampling.LANCZOS)
    image.paste(scaled, ((size - scaled.width) // 2, (size - scaled.height) // 2))
    image.save(assets / f"logo-{size}.png", optimize=True)
Image.open(assets / "logo-256.png").save(
    assets / "cy3dview.ico", sizes=[(size, size) for size in sizes]
)
print("Logo PNG sizes and Windows icon prepared")
