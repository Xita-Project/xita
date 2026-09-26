"""Exercise the production report with one callee reached through two parents."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
s = (root / 'recomp/kernel/xk_scene_thread.c').read_text()
a = s.index('static void phase_report(unsigned frames)')
b = s.index('\n#if defined(XV_SCENE_THREAD)', a)
fixture = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#define PHASE_MAX 512
static struct { uint32_t parent, addr; uint64_t us; unsigned n; } phase_tab[2][PHASE_MAX];
static unsigned phase_used[2], phase_overflow[2];
static int phases=1;
static const char *phase_tag[]={"[tick-phases]","[scene-phases]"};
static char logs[16384];
static void log_line(const char *fmt, ...) {
 va_list ap;va_start(ap,fmt);size_t n=strlen(logs);
 vsnprintf(logs+n,sizeof(logs)-n,fmt,ap);va_end(ap);
}
#define XK_LOG log_line
'''
fixture += s[a:b]
fixture += r'''
int main(void) {
 phase_used[0]=3;
 phase_tab[0][0]=(typeof(phase_tab[0][0])){0x10,0xAA,2000,1};
 phase_tab[0][1]=(typeof(phase_tab[0][0])){0x20,0xAA,3000,1};
 phase_tab[0][2]=(typeof(phase_tab[0][0])){0xAA,0xBB,3000,2};
 phase_report(1);
 assert(strstr(logs,"AA incl 5.00 self 2.00 (1 children): BB 3.00"));
 assert(strstr(logs,"10>AA 2.00 (1)"));
 assert(strstr(logs,"20>AA 3.00 (1)"));
 for(unsigned i=0;i<3;i++)assert(!phase_tab[0][i].us&&!phase_tab[0][i].n);
 puts("PASS: multi-parent totals, raw edge labels and report reset");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d); (p/'test.c').write_text(fixture)
 subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
