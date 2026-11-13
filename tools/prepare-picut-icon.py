"""Remove only the connected neutral background of supplied icon artwork.

Development asset utility; neither Python nor Pillow is shipped with picut.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    original = Image.open(args.source).convert("RGBA")
    rgb = original.convert("RGB")
    # A dark outline separates the subject from the baked checkerboard. Flood
    # from outside so eye highlights and the pale belly remain opaque.
    neutral = bytes(
        255 if min(pixel) >= 80 and max(pixel) - min(pixel) <= 24 else 0
        for pixel in rgb.get_flattened_data()
    )
    mask = Image.new("L", (rgb.width + 2, rgb.height + 2), 255)
    mask.paste(Image.frombytes("L", rgb.size, neutral), (1, 1))
    ImageDraw.floodfill(mask, (0, 0), 128)
    alpha = mask.crop((1, 1, rgb.width + 1, rgb.height + 1)).point(
        lambda value: 0 if value == 128 else 255
    )
    original.putalpha(ImageChops.multiply(alpha, original.getchannel("A")))
    subject = original.crop(original.getbbox())
    subject.thumbnail((940, 940), Image.Resampling.LANCZOS)
    icon = Image.new("RGBA", (1024, 1024))
    icon.alpha_composite(subject, ((1024 - subject.width) // 2,
                                   (1024 - subject.height) // 2))
    icon.save(args.output)


if __name__ == "__main__":
    main()
