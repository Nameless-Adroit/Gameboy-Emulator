#!/usr/bin/env python3
"""Run independent SingleStepTests/sm83 v1 JSON files against this CPU.

Usage: python tests/run_vectors.py /path/to/sm83/v1
Requires Python 3 and GCC-compatible cc. External vectors are not bundled.
Checks registers, listed final RAM and aggregate T-cycles, NOT bus ordering.
IME/IE are deliberately excluded per the upstream README; local C tests cover
interrupt sequencing. STOP/HALT use dedicated C tests, not these vectors.
"""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = r'''
#include "cpu.h"
static CPU c;
static Memory m;
static const Reg regs[] = {REG_A,REG_F,REG_B,REG_C,REG_D,REG_E,REG_H,REG_L,REG_SP,REG_PC};
int run_case(const unsigned short *initial, const unsigned short *expected,
             const unsigned short *before, unsigned bn,
             const unsigned short *after, unsigned an, unsigned ticks) {
    memory_init(&m); reset_cpu(&c,&m);
    for (unsigned i=0;i<10;++i) set_reg(&c,regs[i],initial[i]);
    for (unsigned i=0;i<bn;i+=2) m.data[before[i]]=before[i+1];
    unsigned elapsed=cpu_step(&c,&m);
    for (unsigned i=0;i<10;++i) if(get_reg(&c,regs[i])!=expected[i]) return 1+(int)i;
    for (unsigned i=0;i<an;i+=2) if(m.data[after[i]]!=after[i+1]) return 11;
    if(elapsed!=ticks) return 12;
    return 0;
}
'''
NAMES = ['a','f','b','c','d','e','h','l','sp','pc']
def array(values):
    return (ctypes.c_ushort * len(values))(*values)
def ram(state):
    return array([n for pair in state['ram'] for n in pair])

def main():
    directory = Path(sys.argv[1])
    files = sorted(directory.glob('*.json'))
    if not files:
        raise SystemExit('No JSON vector files found')
    total = failures = excluded = 0
    with tempfile.TemporaryDirectory() as tmp:
        bridge = Path(tmp) / 'bridge.c'
        bridge.write_text(BRIDGE)
        libpath = Path(tmp) / 'cpu.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O2', '-shared', '-fPIC',
                        '-I', str(ROOT), str(ROOT/'cpu.c'), str(ROOT/'memory.c'),
                        str(bridge), '-o', str(libpath)], check=True)
        lib = ctypes.CDLL(str(libpath))
        ptr = ctypes.POINTER(ctypes.c_ushort)
        lib.run_case.argtypes = [ptr,ptr,ptr,ctypes.c_uint,ptr,ctypes.c_uint,ctypes.c_uint]
        lib.run_case.restype = ctypes.c_int
        for path in files:
            tests = json.loads(path.read_text())
            if path.stem.lower() in {'10', '76'}:
                excluded += len(tests)
                continue
            for test in tests:
                initial, final = test['initial'], test['final']
                before, after = ram(initial), ram(final)
                result = lib.run_case(array([initial[n] for n in NAMES]),
                                      array([final[n] for n in NAMES]),
                                      before, len(before), after, len(after),
                                      4 * len(test['cycles']))
                total += 1
                if result:
                    failures += 1
                    if failures <= 15:
                        field = NAMES[result-1] if result <= 10 else ('ram' if result == 11 else 'cycles')
                        print(f'FAIL {path.name}: {test["name"]} ({field})')
        print(f'{total} independent vectors; {failures} failures; '
              f'{excluded} STOP/HALT vectors excluded (dedicated C tests instead).')
    return bool(failures)
if __name__ == '__main__':
    sys.exit(main())
