#!/usr/bin/env python3
"""Build the Windows exe icon (bumpy.ico) from the game's own startup splash.

BUMPRESE.VEC is the original's pre-menu splash: Bumpy's face peeking over a
ledge, gripping it with both hands, under the game logo -- the game's actual
mascot shot, not just the wordmark. Source is a screen-format dump of it:

    OpenBumpy.exe --render-screen BUMPRESE.VEC splash.bmp

That dump is the native 320x200 frame. This letterboxes it onto a 320x320
square with plain black bars top and bottom -- full width kept so neither
hand is cropped -- then downsamples with nearest-neighbour (the source is
native-resolution dithered pixel art, not a smooth vector logo, so NEAREST
keeps it crisp rather than blurring the dither into mud) into a
multi-resolution .ico.

Usage:
    OpenBumpy.exe --render-screen BUMPRESE.VEC splash.bmp
    python tools/re/build_splash_icon.py splash.bmp src/app/bumpy.ico [preview.png]
"""

import io
import struct
import sys

from PIL import Image

ICON_SIZES = [16, 32, 48, 64, 128, 256]


def letterbox_square(source):
    side = source.width
    canvas = Image.new("RGB", (side, side), (0, 0, 0))
    top = (side - source.height) // 2
    canvas.paste(source, (0, top))
    return canvas


def build_ico(master, sizes):
    images = []
    for size in sizes:
        layer = master.convert("RGBA").resize((size, size), Image.NEAREST)
        buffer = io.BytesIO()
        layer.save(buffer, format="PNG")
        images.append(buffer.getvalue())

    header = struct.pack("<HHH", 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries = bytearray()
    for size, png in zip(sizes, images):
        entries += struct.pack(
            "<BBBBHHII",
            size & 0xFF,
            size & 0xFF,
            0,
            0,
            1,
            32,
            len(png),
            offset,
        )
        offset += len(png)
    return header + bytes(entries) + b"".join(images)


def main(argv):
    if len(argv) not in (3, 4):
        print(__doc__)
        return 2
    source = Image.open(argv[1]).convert("RGB")
    square = letterbox_square(source)
    with open(argv[2], "wb") as handle:
        handle.write(build_ico(square, ICON_SIZES))
    print(f"wrote {argv[2]} ({square.width}x{square.height} master -> sizes {ICON_SIZES})")
    if len(argv) == 4:
        square.save(argv[3])
        print(f"wrote preview {argv[3]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
