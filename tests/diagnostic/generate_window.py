#!/usr/bin/env python3
"""Generate a dependency-free 32 KiB ROM-only DMG scrolling BG + Window diagnostic.

Run this file to regenerate background.gb. No assembler or external assets.
Visible image: moving shade blocks behind a fixed bordered W-pattern panel.
"""
from pathlib import Path


def build_rom():
    rom = bytearray(32768)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])
    rom[0x104:0x134] = bytes.fromhex(
        'CE ED 66 66 CC 0D 00 0B 03 73 00 83 00 0C 00 0D '
        '00 08 11 1F 88 89 00 0E DC CC 6E E6 DD DD D9 99 '
        'BB BB 67 63 6E 0E EC CC DD DC 99 9F BB B9 33 3E')
    rom[0x134:0x144] = b'WINDOW DEMO'.ljust(16, b'\0')
    rom[0x148] = 0  # 32 KiB, ROM-only, no cartridge RAM.
    rom[0x14A] = 1  # Non-Japanese.
    # VBlank ISR: preserve AF, scroll the BG one pixel, return with IME enabled.
    # Window has independent coordinates and must remain fixed across frames.
    rom[0x40:0x4A] = bytes([0xF5,0xF0,0x43,0x3C,0xE0,0x43,0xF1,0xD9,0x00,0x00])
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
    copy('tiles', 0x8000, 7 * 16, 'copy_tiles')
    copy('map', 0x9800, 1024, 'copy_map')
    copy('window_map', 0x9C00, 1024, 'copy_window_map')
    emit(0x3E, 0xE4, 0xE0, 0x47)  # Identity BGP mapping.
    emit(0xAF, 0xEA, 0xFF, 0xFF, 0xE0, 0x0F)  # IE=IF=0.
    emit(0x3E, 80, 0xE0, 0x4A, 0x3E, 31, 0xE0, 0x4B)  # WY=80, WX=24+7.
    emit(0x3E, 0xF1, 0xE0, 0x40)  # BG map 9800, Window map 9C00, both on.
    emit(0x3E, 1, 0xEA, 0xFF, 0xFF, 0xFB, 0x00)  # Enable VBlank IRQ; EI; NOP.
    label('idle')
    emit(0x76)  # HALT; hardware continues drawing.
    jr(0x18, 'idle')

    label('tiles')
    glyph = [0x81,0x81,0x81,0x99,0xA5,0xC3,0x81,0x00]
    for tile in range(7):
        for y in range(8):
            if tile < 4:
                emit(255 if tile & 1 else 0, 255 if tile & 2 else 0)
            elif tile == 4:
                emit(0, 0)  # White panel fill (Window color zero is opaque).
            elif tile == 5:
                emit(255, 255)  # Black panel border.
            else:
                emit(glyph[y], glyph[y])  # Black W on white.
    label('map')
    for y in range(32):
        for x in range(32):
            emit((x + y) % 4)
    label('window_map')
    for y in range(32):
        for x in range(32):
            border = y in (0,7) or x in (0,16)
            emit(5 if border else 6 if (x + y) % 2 else 4)
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
    target = Path(__file__).with_name('window.gb')
    target.write_bytes(build_rom())
    print(f'Wrote {target} ({target.stat().st_size} bytes)')
