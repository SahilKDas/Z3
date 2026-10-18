"""Bake English glyphs from the selected Minecraft snapshot's bitmap provider."""
from pathlib import Path
import json
from PIL import Image
root = Path(__file__).resolve().parents[1]
providers=json.loads((root / 'assets/minecraft/font/include/default.json').read_text(encoding='utf-8'))['providers']
provider=next(p for p in providers if p.get('file')=='minecraft:font/ascii.png')
source=Image.open(root / 'assets/minecraft/textures/font/ascii.png').convert('RGBA')
chars={c:(x,y) for y,row in enumerate(provider['chars']) for x,c in enumerate(row)}
atlas=Image.new('RGBA',(512,512))
advances=[]
for code in range(32,127):
    c=chr(code); x,y=chars.get(c,chars['?'])
    glyph=source.crop((x*8,y*8,x*8+8,y*8+8))
    slot=code-32
    atlas.paste(glyph,((slot%16)*8,64+(slot//16)*8))
    bbox=glyph.getchannel('A').getbbox()
    advances.append((4 if c==' ' else (bbox[2]+1 if bbox else 1))/8)
atlas.paste(Image.open(root / 'assets/minecraft/textures/gui/container/inventory.png').convert('RGBA'),(256,128))
atlas.save(root / 'fonts/english.png')
(root/'src/font_metrics.hpp').write_text('#pragma once\n#include <array>\n// Minecraft bitmap provider; English ASCII only.\ninline constexpr std::array<float,95> fontAdvance={'+', '.join(f'{n:.6f}f' for n in advances)+'};\n')
