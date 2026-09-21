#!/usr/bin/env python3
"""Exercise production clock policy with counted firmware/module stand-ins."""
from pathlib import Path
import subprocess, tempfile
from test_draw_state_batch import function
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'runtime/main.c').read_text()
body='\n'.join(function(s,n) for n in ('xv_external_cpu_clock','xv_configure_boot_cpu_clock','xv_configure_cpu_clock'))
# Boot must read settings before requesting a clock, not only before game launch.
main=s[s.index('int main(int argc, char *argv[])'):]
assert main.index('xv_load_settings();') < main.index('xv_configure_boot_cpu_clock();')
source=r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
typedef int SceUID;
typedef struct { unsigned size; int pid; unsigned argc; void *args; int flags; } tai_module_args_t;
static int cpu=444, requested=500, writes, loads, binds;
static int scePowerGetArmClockFrequency(void) { return cpu; }
static int scePowerSetArmClockFrequency(int mhz) { ++writes; if(mhz>444)return -1; cpu=mhz; return 0; }
static int scePowerGetBusClockFrequency(void) { return 222; }
static int scePowerGetGpuClockFrequency(void) { return 222; }
static int scePowerGetGpuXbarClockFrequency(void) { return 166; }
static int sceKernelGetProcessId(void) { return 42; }
static void sceKernelDelayThread(int us) { (void)us; }
static int taiLoadStartKernelModuleForUser(const char *p, tai_module_args_t *a) { (void)p;(void)a;++loads;return -5; }
static int xv_quality_int(const char *k,int d,int lo,int hi) { (void)k;(void)d;(void)lo;(void)hi;return requested; }
int xita_clock_bind(int mhz) { (void)mhz;++binds;return 0; }
int xita_clock_get_arm(void) { return cpu; }
static void log_stub(const char *fmt,...) { (void)fmt; }
#define XV_LOG(...) log_stub(__VA_ARGS__)
'''+body+r'''
int main(void) {
    const int clocks[]={333,444,500};
    for(unsigned i=0;i<3;i++) {
        cpu=clocks[i];writes=loads=binds=0;
        setenv("XV_CPU_EXTERNAL","1",1);
        xv_configure_boot_cpu_clock();xv_configure_cpu_clock();
        assert(cpu==clocks[i] && writes==0 && loads==0 && binds==0);
    }
    unsetenv("XV_CPU_EXTERNAL");cpu=333;writes=loads=binds=0;
    xv_configure_boot_cpu_clock();assert(cpu==444 && writes==1);
    cpu=500;writes=loads=binds=0;xv_configure_boot_cpu_clock();xv_configure_cpu_clock();
    assert(cpu==500 && writes==0 && loads==0 && binds==0);
    setenv("XV_CPU_EXTERNAL","0",1);cpu=444;writes=loads=binds=0;
    xv_configure_cpu_clock();assert(writes==2 && loads==1 && binds==0 && cpu==444);
    puts("clock policy: external mode performs no CPU writes, module loads or binds; defaults preserved");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-clock-policy-') as d:
    p=Path(d);(p/'test.c').write_text(source)
    subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
