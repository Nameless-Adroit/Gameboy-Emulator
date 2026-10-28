# Background diagnostic ROM

`background.gb` is a generated 32 KiB ROM-only DMG cartridge (the loader's
smallest supported cartridge size). Its program and graphics use about 1.2 KiB;
the remainder is padding. It uses five tiles and no external assets or assembler.
The header includes the DMG logo, header checksum and global checksum.

The program disables interrupts, waits for VBlank if the LCD was initially on,
disables LCD, copies tiles to 8000 and a map to 9800, sets BGP=E4 and SCX=SCY=0,
enables LCD/BG, then HALTs. The expected picture has a black border, four shade
bars, a checkerboard and repeated X tiles. No sprites or window are used.
It has been executed by this emulator; real DMG hardware has not been tested.

Regenerate the committed ROM (Python 3 standard library only):

```sh
python3 tests/diagnostic/generate_background.py
```

Build, execute, verify every pixel and save the actual framebuffer as a standard
160 × 144 grayscale PGM image, without SDL:

```sh
cmake --build build
./build/bg_diagnostic_tests tests/diagnostic/background.gb build/background.pgm
```

`BG_Diagnostic_ROM` in CTest performs the same CPU execution and pixel comparison
without saving a file. The harness stops at the first frame and fails if the CPU
locks/stops, never reaches VBlank, or any pixel differs from the pattern.

`background.png` below was exported from the harness-produced PGM and enlarged
4× with nearest-neighbor sampling, not drawn independently from an expected
image. To regenerate that optional PNG preview, Pillow is required only for the
conversion; it is not a build/runtime/test dependency:

```sh
python3 -c "from PIL import Image; Image.open('build/background.pgm').resize((640,576), Image.Resampling.NEAREST).save('tests/diagnostic/background.png')"
```

![Actual CPU-rendered frame, enlarged 4×](background.png)
