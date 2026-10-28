#!/usr/bin/env python3
"""Generate a dependency-free 32 KiB ROM-only DMG background diagnostic.

Run this file to regenerate background.gb. No assembler or external assets.
Visible image: black border, four shade bars, checkerboard and repeated X tiles.
"""
from pathlib import Path


def build_rom():
    rom = bytearray(32768)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])
    rom[0x104:0x134] = bytes.fromhex(
        'CE ED 66 66 CC 0D 00 0B 03 73 00 83 00 0C 00 0D '
        '00 08 11 1F 88 89 00 0E DC CC 6E E6 DD DD D9 99 '
        'BB BB 67 63 6E 0E EC CC DD DC 99 9F BB B9 33 3E')
    rom[0x134:0x144] = b'BG DIAGNOSTIC'.ljust(16, b'\0')
    rom[0x148] = 0  # 32 KiB, ROM-only, no cartridge RAM.
    rom[0x14A] = 1  # Non-Japanese.
    code = bytearray()
    labels, fixups = {}, []

    def emit(*values):
        code.extend(values)

    def label(name):
        labels[name] = 0x150 + len(code)

    def jr(op, target):
        emit(op, 0)
        fixups.append((len(code) - 1, target, True))

    def load_hl(target):
        emit(0x21, 0, 0)
        fixups.append((len(code) - 2, target, False))

    def copy(target, destination, length, loop):
        load_hl(target)
        emit(0x11, destination & 255, destination >> 8)  # LD DE,d16
        emit(0x01, length & 255, length >> 8)  # LD BC,d16
        label(loop)
        emit(0x2A, 0x12, 0x13, 0x0B, 0x78, 0xB1)  # Copy byte; test BC.
        jr(0x20, loop)

    emit(0xF3, 0x31, 0xFE, 0xFF)  # DI; LD SP,FFFE
    emit(0xF0, 0x40, 0xE6, 0x80)  # LCD enabled? Real boot ROM leaves it on.
    jr(0x28, 'lcd_off')
    label('wait_vblank')
    emit(0xF0, 0x44, 0xFE, 0x90)  # Wait until LY >= 144 before disabling LCD.
    jr(0x38, 'wait_vblank')
    label('lcd_off')
    emit(0xAF, 0xE0, 0x40, 0xE0, 0x42, 0xE0, 0x43)  # LCD off, SCY=SCX=0.
    copy('tiles', 0x8000, 5 * 16, 'copy_tiles')
    copy('map', 0x9800, 1024, 'copy_map')
    emit(0x3E, 0xE4, 0xE0, 0x47)  # Identity BGP mapping.
    emit(0xAF, 0xEA, 0xFF, 0xFF, 0xE0, 0x0F)  # IE=IF=0.
    emit(0x3E, 0x91, 0xE0, 0x40)  # LCD on, unsigned tiles, map 9800, BG on.
    label('idle')
    emit(0x76)  # HALT; hardware continues drawing.
    jr(0x18, 'idle')

    label('tiles')
    for tile in range(5):
        for y in range(8):
            colors = [tile] * 8 if tile < 4 else [3 if x == y or x + y == 7 else 1 for x in range(8)]
            emit(sum((c & 1) << (7 - x) for x, c in enumerate(colors)),
                 sum((c >> 1) << (7 - x) for x, c in enumerate(colors)))
    label('map')
    for y in range(32):
        for x in range(32):
            if x >= 20 or y >= 18:
                tile = 0
            elif x in (0, 19) or y in (0, 17):
                tile = 3
            elif y <= 5:
                tile = min((x - 1) // 5, 3)
            elif y <= 11:
                tile = 3 if (x + y) % 2 else 0
            else:
                tile = 4
            emit(tile)
    for offset, target, relative in fixups:
        address = labels[target]
        if relative:
            displacement = address - (0x150 + offset + 1)
            assert -128 <= displacement <= 127
            code[offset] = displacement & 255
        else:
            code[offset:offset + 2] = address.to_bytes(2, 'little')
    rom[0x150:0x150 + len(code)] = code
    checksum = 0
    for value in rom[0x134:0x14D]:
        checksum = (checksum - value - 1) & 255
    rom[0x14D] = checksum
    rom[0x14E:0x150] = (sum(rom) & 65535).to_bytes(2, 'big')
    return rom


if __name__ == '__main__':
    target = Path(__file__).with_name('background.gb')
    target.write_bytes(build_rom())
    print(f'Wrote {target} ({target.stat().st_size} bytes)')
