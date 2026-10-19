#!/usr/bin/env python3
"""Generate the application's verified font inventory using installed licensed fonts."""
import ctypes as C
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
latin = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'
cjk = '/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf'
ft = C.CDLL('libfreetype.so.6')
ft.FT_Get_Char_Index.argtypes = [C.c_void_p, C.c_ulong]
ft.FT_Get_Char_Index.restype = C.c_uint
lib, face = C.c_void_p(), C.c_void_p()
assert ft.FT_Init_FreeType(C.byref(lib)) == 0
assert ft.FT_New_Face(lib, cjk.encode(), 0, C.byref(face)) == 0
chars = set()
for a in range(0xa1, 0xf8):
    for b in range(0xa1, 0xff):
        try: char = bytes([a, b]).decode('gb2312')
        except UnicodeDecodeError: continue
        if ft.FT_Get_Char_Index(face, ord(char)): chars.add(char)
# Add punctuation used by the interface, with the same source-glyph check.
for char in '·°':
    if ft.FT_Get_Char_Index(face, ord(char)): chars.add(char)
ft.FT_Done_Face(face); ft.FT_Done_FreeType(lib)
chars = ''.join(sorted(chars))
(root/'assets/fonts/passport-symbols.txt').write_text(''.join(chr(i) for i in range(32,127))+'·°'+chars)
subprocess.run(['npx','--yes','--package','lv_font_conv@1.5.3','lv_font_conv',
    '--font',latin,'--range','0x20-0x7e,0xb7,0xb0','--font',cjk,'--symbols',chars,
    '--size','16','--bpp','2','--format','lvgl','--no-compress',
    '--lv-font-name','passport_font_16','--lv-include','lvgl.h',
    '--output',str(root/'assets/fonts/passport_font_16.c')],check=True)
print('Generated ASCII plus',len(chars),'source-verified CJK/punctuation glyphs')

# Keep generated comments portable and avoid trailing whitespace.
output = root/'assets/fonts/passport_font_16.c'
text = output.read_text().replace(str(output), 'assets/fonts/passport_font_16.c')
output.write_text('\n'.join(line.rstrip() for line in text.splitlines()).rstrip()+'\n')
