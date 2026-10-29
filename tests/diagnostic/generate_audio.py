#!/usr/bin/env python3
"""Generate audio.gb: existing shade-bar display plus four repeating sound stages.
No assembler or external packages. Stages: left pulse, right pulse, stereo wave,
stereo noise. Each stage lasts about 0.75 seconds at normal emulation speed.
"""
import sys
from pathlib import Path
sys.dont_write_bytecode = True
from generate_background import build_rom as background_rom


def build_rom():
    rom = background_rom()
    idle = rom.index(bytes.fromhex('76 18 FD'), 0x150, 0x1000)
    rom[idle:idle + 3] = bytes.fromhex('C3 00 20')
    code = bytearray()
    fixups = []

    def write(register, value):
        code.extend([0x3E, value, 0xE0, register])

    def stage(route, registers):
        write(0x26, 0)
        write(0x26, 0x80)
        write(0x24, 0x77)
        write(0x25, route)
        for reg, value in registers:
            write(reg, value)
        for _ in range(2):
            code.extend([0xCD, 0, 0])
            fixups.append(len(code) - 2)

    stage(0x11, [(0x11, 0x80), (0x12, 0xA0), (0x13, 0xD6), (0x14, 0x86)])
    stage(0x22, [(0x16, 0x40), (0x17, 0xA0), (0x18, 0x18), (0x19, 0x87)])
    wave = [(0x30 + i, (i * 2 << 4) | (i * 2 + 1)) for i in range(8)]
    wave += [(0x38 + i, ((15 - i * 2) << 4) | (14 - i * 2)) for i in range(8)]
    stage(0x44, wave + [(0x1A, 0x80), (0x1C, 0x20), (0x1D, 0xBE), (0x1E, 0x87)])
    stage(0x88, [(0x20, 0), (0x21, 0xA2), (0x22, 0x35), (0x23, 0xC0)])
    code.extend([0xC3, 0, 0x20])
    delay = 0x2000 + len(code)
    # LD BC,FFFF; DEC BC; LD A,B; OR C; JR NZ,-5; RET.
    code.extend(bytes.fromhex('01 FF FF 0B 78 B1 20 FB C9'))
    for offset in fixups:
        code[offset:offset + 2] = delay.to_bytes(2, 'little')
    rom[0x2000:0x2000 + len(code)] = code
    rom[0x134:0x144] = b'AUDIO DIAGNOSTIC'.ljust(16, b'\0')
    checksum = 0
    for value in rom[0x134:0x14D]:
        checksum = (checksum - value - 1) & 255
    rom[0x14D] = checksum
    rom[0x14E:0x150] = b'\0\0'
    rom[0x14E:0x150] = (sum(rom) & 65535).to_bytes(2, 'big')
    return rom


if __name__ == '__main__':
    target = Path(__file__).with_name('audio.gb')
    target.write_bytes(build_rom())
    print(f'Wrote {target} ({target.stat().st_size} bytes)')
