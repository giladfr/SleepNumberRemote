"""Tile sim/out/*.png into one labelled image (sim/out/contact_sheet.png)."""
import glob, os, sys
from PIL import Image, ImageDraw, ImageFont

files = sorted(f for f in glob.glob("out/*.png") if "contact" not in f)
cols, scale, pad, label_h = 6, 0.5, 16, 26
w, h = int(480 * scale), int(640 * scale)
rows = (len(files) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (w + pad) + pad, rows * (h + pad + label_h) + pad), (32, 36, 44))
draw = ImageDraw.Draw(sheet)
try:
    font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 15)
except OSError:
    font = ImageFont.load_default()
for i, f in enumerate(files):
    x = pad + (i % cols) * (w + pad)
    y = pad + (i // cols) * (h + pad + label_h)
    draw.text((x, y), os.path.basename(f)[:-4], fill=(200, 205, 215), font=font)
    sheet.paste(Image.open(f).resize((w, h), Image.LANCZOS), (x, y + label_h))
sheet.save("out/contact_sheet.png")
print(f"{len(files)} screens -> out/contact_sheet.png {sheet.size}")
