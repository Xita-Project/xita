#!/usr/bin/env python3
"""Cross-compile production packet/HLE and generated islands for Cortex-A9.

Requires VitaSDK, iced-x86, unicorn and pyelftools. This is instruction-level
correctness and instruction counting, not Vita timing. The worker identity
predicate is a linked test flag; its real OS/thread-check cost is NOT measured.
"""
from pathlib import Path
import argparse
import json
import random
import struct
import subprocess
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
from test_material_packet import ROOT, generate

RAM, PT, STACK, CTX, END = 0x20000000, 0x21000000, 0x22000000, 0x23000000, 0x24000000
SIZE = 2 << 20


def build(out, enabled=True):
    out.mkdir(parents=True, exist_ok=True)
    generate(out)
    (out / 'arm_runtime.c').write_text('''#include "kernel/xd3d.c"
#include <stddef.h>
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt; xk_thread *xk_cur;
int xv_trace_enabled,xv_trace_funcs,xv_watch_n; const unsigned xv_guest_trace_enabled=0;
volatile uint32_t xv_cur_fn; const char xv_object_job_marker=0;
unsigned arm_worker; int xv_object_is_worker_thread(void){return arm_worker;}
const unsigned material_layout[]={sizeof(xctx),sizeof(xd3d_state_t),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,fiber)};
''')
    (out / 'arm_islands.c').write_text('''#include "xv_x86rt.h"
#include "kernel/xk_material_packet.h"
#include "kernel/xk_object_jobs.h"
extern volatile uint32_t xv_cur_fn;
void xv_hle_D3DDevice_SetRenderState_Simple(xctx*);
void xv_hle_D3DDevice_SetTextureState_Deferred(xctx*);
void xv_hle_D3DDevice_SetRenderState_CullMode(xctx*);
#define XV_HLE_CALL(address,fn) do { if(xv_is_object_job(c)) xv_object_job_hle(c,address,fn); else { uint32_t old=xv_cur_fn;xv_cur_fn=0x80000000u|(address);fn(c);xv_cur_fn=old; } } while(0)
#include "material_packet_reference.inc"
void material_arm_reference(xctx *c,unsigned i) {
 switch(i){case 0:material_reference_0(c);break;case 1:material_reference_1(c);break;case 2:material_reference_2(c);break;case 3:material_reference_3(c);break;case 4:material_reference_4(c);break;}
}
void material_arm_hook(xctx *c,unsigned i) {
 switch(i){case 0:material_hooked_0(c);break;case 1:material_hooked_1(c);break;case 2:material_hooked_2(c);break;case 3:material_hooked_3(c);break;case 4:material_hooked_4(c);break;}
}
''')
    (out / 'arm_shims.c').write_text('''#include <stddef.h>
#include <stdint.h>
#include "xv_x86rt.h"
char *getenv(const char *name){(void)name;return NULL;}
int atoi(const char *s){(void)s;return 0;}
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;while(n--)*a++=*b++;return d;}
void *memset(void *d,int v,size_t n){unsigned char *a=d;while(n--)*a++=(unsigned char)v;return d;}
int memcmp(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
int snprintf(char *s,size_t n,const char *f,...){(void)s;(void)n;(void)f;__builtin_trap();}
void xk_os_log(const char *s,...){(void)s;__builtin_trap();}
void xv_object_job_hle(xctx *c,unsigned a,xv_fn_t fn){(void)c;(void)a;(void)fn;__builtin_trap();}
''')
    flags = ['-std=gnu11', '-O2', '-fno-strict-aliasing', '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon',
             '-mfloat-abi=hard', '-ffunction-sections', '-fdata-sections', '-ffreestanding', '-fno-builtin',
             '-DXV_EXPERIMENTAL_OBJECT_JOBS', '-I' + str(ROOT / 'recomp'), '-I' + str(out)]
    if enabled:
        flags.append('-DXV_MATERIAL_PACKET')
    objects = []
    for name in ('runtime', 'islands', 'shims'):
        obj = out / ('arm_' + name + '.o')
        subprocess.run(['arm-vita-eabi-gcc', *flags, '-c', str(out / ('arm_' + name + '.c')), '-o', str(obj)], check=True)
        objects.append(str(obj))
    elf = out / 'material.elf'
    retained = ['material_layout', 'material_arm_reference', 'material_arm_hook', 'xv_material_packet_override', 'xv_material_packet_available']
    subprocess.run(['arm-vita-eabi-gcc', '-nostdlib', '-Wl,--gc-sections', '-Wl,-Ttext=0x100000',
                    '-Wl,-e,material_arm_hook', *['-Wl,-u,' + n for n in retained], *objects, '-lgcc', '-o', str(elf)], check=True)
    return elf


class Machine:
    def __init__(self, elf):
        self.uc = u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
        u.reg_write(UC_ARM_REG_C1_C0_2, 15 << 20)
        u.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
        with elf.open('rb') as f:
            e = ELFFile(f)
            self.symbols = {s.name: s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols() if s.name}
            for segment in e.iter_segments():
                if segment['p_type'] != 'PT_LOAD':
                    continue
                base = segment['p_vaddr']; start = base & ~4095
                u.mem_map(start, ((base + segment['p_memsz'] + 4095) & ~4095) - start)
                u.mem_write(base, segment.data())
        for base, size in ((RAM, SIZE), (PT, 4 << 20), (STACK, 65536), (CTX, 4096), (END, 4096)):
            u.mem_map(base, size)
        for name, value in (('g_xram', RAM), ('g_xpt', PT), ('g_img_base', RAM)):
            u.mem_write(self.symbols[name], struct.pack('<I', value))
        self.layout = struct.unpack('<10I', u.mem_read(self.symbols['material_layout'], 40))
        self.instructions = 0
        def count(uc, address, size, unused):
            self.instructions += 1
        u.hook_add(UC_HOOK_CODE, count)

    def call(self, name, a, b=0):
        self.instructions = 0
        u = self.uc
        u.reg_write(UC_ARM_REG_R0, a & 0xffffffff); u.reg_write(UC_ARM_REG_R1, b)
        u.reg_write(UC_ARM_REG_SP, STACK + 65024); u.reg_write(UC_ARM_REG_LR, END | 1)
        u.emu_start(self.symbols[name] | 1, END, count=100000)
        assert u.reg_read(UC_ARM_REG_PC) == END
        return self.instructions

    def result(self):
        return (bytes(self.uc.mem_read(CTX, self.layout[0])), bytes(self.uc.mem_read(RAM, SIZE)),
                bytes(self.uc.mem_read(self.symbols['xd3d_state'], self.layout[1])))


def run(out):
    elf = build(out)
    baseline, candidate = Machine(elf), Machine(elf)
    candidate.call('xv_material_packet_override', 1)
    rng = random.Random(0x70110)
    rows = []
    for case in range(128):
        pages = [i * 4096 for i in range(SIZE // 4096)]
        sp = 0x504A0 + (case & 3)
        if case & 4: pages[0x50] = 0x18F000
        if case & 8: sp = 0x501A8 + (case & 3)
        if case & 16: pages[0x60] = pages[0x50]
        if case % 17 == 0: sp = 0x51004 + (case & 3)
        memory = bytes([case & 255]) * SIZE
        context = bytearray(rng.getrandbits(8) for _ in range(candidate.layout[0]))
        for i in range(8): struct.pack_into('<I', context, i * 4, rng.getrandbits(32))
        struct.pack_into('<I', context, 12, case if case % 3 else 0)
        struct.pack_into('<I', context, 16, sp)
        struct.pack_into('<I', context, 20, sp - 0x2c if case & 32 else 0x60000 + case)
        struct.pack_into('<I', context, candidate.layout[3], case & 7)
        struct.pack_into('<I', context, candidate.layout[7], 3)
        struct.pack_into('<I', context, candidate.layout[8], 32)
        struct.pack_into('<I', context, candidate.layout[9], 0)
        state = bytes([(case + 11) & 255]) * candidate.layout[1]
        for island in range(5):
            results = []
            for machine, function in ((baseline, 'material_arm_reference'), (candidate, 'material_arm_hook')):
                machine.uc.mem_write(RAM, memory); machine.uc.mem_write(PT, struct.pack('<' + 'I' * len(pages), *pages))
                machine.uc.mem_write(CTX, bytes(context)); machine.uc.mem_write(machine.symbols['xd3d_state'], state)
                n = machine.call(function, CTX, island)
                results.append((machine.result(), n))
            assert results[0][0] == results[1][0], (case, island)
            rows.append({'case': case, 'island': island, 'reference_instructions': results[0][1], 'packet_instructions': results[1][1]})
    # Disabled hook equals original, and expose its additional guard/counter cost.
    candidate.call('xv_material_packet_override', 0)
    disabled = []
    for island in range(5):
        results = []
        for machine, function in ((baseline, 'material_arm_reference'), (candidate, 'material_arm_hook')):
            machine.uc.mem_write(RAM, memory); machine.uc.mem_write(CTX, bytes(context))
            machine.uc.mem_write(machine.symbols['xd3d_state'], state)
            n = machine.call(function, CTX, island); results.append((machine.result(), n))
        assert results[0][0] == results[1][0]
        disabled.append({'island': island, 'reference_instructions': results[0][1], 'off_hook_instructions': results[1][1]})
    # Default builds contain no packet calls. Verify the compiled fallback and
    # its executed instruction count with the same Cortex-A9 compiler/settings.
    default_elf = build(out / 'compiled-out', enabled=False)
    baseline, candidate = Machine(default_elf), Machine(default_elf)
    candidate.call('xv_material_packet_override', 1)
    candidate.call('xv_material_packet_available', 0)
    assert candidate.uc.reg_read(UC_ARM_REG_R0) == 0
    compiled_out = []
    for island in range(5):
        results = []
        for machine, function in ((baseline, 'material_arm_reference'), (candidate, 'material_arm_hook')):
            machine.uc.mem_write(RAM, memory); machine.uc.mem_write(PT, struct.pack('<' + 'I' * len(pages), *pages))
            machine.uc.mem_write(CTX, bytes(context)); machine.uc.mem_write(machine.symbols['xd3d_state'], state)
            n = machine.call(function, CTX, island); results.append((machine.result(), n))
        assert results[0] == results[1], (island, 'compiled-out fallback/count differs')
        compiled_out.append({'island': island, 'reference_instructions': results[0][1], 'compiled_out_hook_instructions': results[1][1]})
    report = {'cases': len(rows), 'comparison': 'production VitaSDK Cortex-A9 code; complete context/guest/D3D state',
              'limits': 'Instructions are not cycles/FPS. Worker predicate is a test flag: real OS identity-check cost omitted. First-use initialization is included only in the first rows.',
              'mean_by_island': [{'island': i, 'reference': sum(x['reference_instructions'] for x in rows if x['island'] == i) / 128,
                                  'packet': sum(x['packet_instructions'] for x in rows if x['island'] == i) / 128} for i in range(5)],
              'disabled': disabled, 'compiled_out': compiled_out, 'rows': rows}
    (out / 'arm-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'rows'}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args(); args.output_dir.mkdir(parents=True, exist_ok=True)
    run(args.output_dir.resolve())
