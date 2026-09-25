#!/usr/bin/env python3
"""Exercise production save selection without touching a Vita or real saves."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
s = (root / 'runtime/main.c').read_text()
a = s.index('    static char test_save[96];')
b = s.index('    xv_boot_recomp(game_dir, save_dir);', a) + len('    xv_boot_recomp(game_dir, save_dir);')
code = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/xv_test_save.h"
typedef int SceUID;
static int g_recomp_finished, exists, boots, mkdirs, closed;
static char boot_path[128];
#define XV_LOG(...) ((void)0)
static int sceIoDopen(const char *p) { assert(strncmp(p,"ux0:data/xita/test-saves/",25)==0); return exists ? 7 : -1; }
static void sceIoDclose(int d) { assert(d==7); closed++; }
static void sceIoMkdir(const char *p,int mode) { (void)p; (void)mode; mkdirs++; }
static void xv_boot_recomp(const char *g,const char *p) { (void)g; boots++; strcpy(boot_path,p); }
static int launch(void) { const char *game_dir="game";
''' + s[a:b] + r'''
return 0;
}
static void reset(void) { boots=mkdirs=closed=g_recomp_finished=0; boot_path[0]=0; }
int main(void) {
 char p[96];
 assert(xv_test_save_path(NULL,p,sizeof p)==0);
 assert(xv_test_save_path("",p,sizeof p)==0);
 const char *bad[]={"../save","a/b","a\\b","a:b","a b",".","..","abcdefghijklmnopqrstuvwxyz1234567","\xff"};
 for(unsigned i=0;i<sizeof bad/sizeof *bad;i++) {
  reset();setenv("XV_TEST_SAVE",bad[i],1);exists=1;
  assert(launch()==-1 && !boots && !mkdirs && g_recomp_finished);
 }
 assert(xv_test_save_path("a",p,2)==-1);
 reset();unsetenv("XV_TEST_SAVE");assert(!launch());
 assert(boots==1 && !strcmp(boot_path,"ux0:data/xita/save"));
 reset();setenv("XV_TEST_SAVE","a30-verify_210",1);exists=0;
 assert(launch()==-1 && !boots && !mkdirs && g_recomp_finished);
 reset();exists=1;assert(!launch());
 assert(boots==1 && closed==1 && !strcmp(boot_path,"ux0:data/xita/test-saves/a30-verify_210"));
 puts("save selection: pass; invalid/missing test roots never boot into player saves");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-save-test-') as d:
    p = Path(d); (p/'test.c').write_text(code)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
