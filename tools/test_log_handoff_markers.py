#!/usr/bin/env python3
"""Production exit tail: sync failure cannot acknowledge or launch an update."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'runtime/main.c').read_text()
start = source.rindex('    if(xv_updates_requested()) {')
prefix = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "runtime/xv_log.h"
#include "runtime/xv_update_halo2.h"
static unsigned updating,syncs,fail_sync,launches,success,phase,exit_seen,marker,exits,warnings;
static int exit_code;
static jmp_buf handed_off;
unsigned xv_update_requested(void) { return updating==1; }
unsigned xv_halo2_update_requested(void) { return updating==2; }
static void log_event(const char *fmt,...) {
    if(strstr(fmt,"handing off to boot helper"))marker=1;
    if(strstr(fmt,"Xita runtime exiting"))exit_seen=1;
}
#define XV_LOG log_event
int sceClibPrintf(const char *fmt,...) { (void)fmt;warnings++;return 0; }
int xv_log_flush_wait(unsigned timeout) {
    assert(timeout==5000000 && (exit_seen || marker));
    return ++syncs==fail_sync ? XV_LOG_IO : XV_LOG_OK;
}
void xv_update_progress(unsigned stage) {
    assert(marker && syncs && fail_sync!=1 && stage==XV_UPDATE_LAUNCHER_HANDOFF);phase=stage;
}
int sceAppMgrLoadExec(const char *path,void *args,void *opt) {
    assert(!strcmp(path,"app0:eboot.bin") && !opt);
    if(updating==2) { char **a=args; assert(a && !strcmp(a[0],"--xita-game=halo2") && !a[1]); }
    else assert(!args);
    assert(marker && syncs==1 && fail_sync!=1 && phase==XV_UPDATE_LAUNCHER_HANDOFF);
    launches++;if(success)longjmp(handed_off,1);return -99;
}
int sceKernelDelayThread(unsigned us) { (void)us;assert(!"successful handoff must not return");return 0; }
int sceKernelExitProcess(int code) { assert(syncs);exits++;exit_code=code;return 0; }
static int run_exit_tail(void) {
'''
suffix = r'''
static void reset(unsigned update,unsigned failure) {
    updating=update;fail_sync=failure;syncs=launches=success=phase=exit_seen=marker=exits=warnings=0;exit_code=-1;
}
int main(void) {
    reset(0,0);assert(!run_exit_tail());assert(exit_seen && syncs==1 && !launches && exits==1 && exit_code==0);
    reset(0,1);assert(!run_exit_tail());assert(warnings==1 && !launches && exits==1);
    for(unsigned game=1;game<=2;game++) {
    reset(game,1);assert(run_exit_tail()==1);assert(marker && syncs==1 && !launches && !phase && exit_code==1);
    reset(game,0);assert(!run_exit_tail());assert(launches==1 && syncs==2 && exit_seen && exit_code==0);
    reset(game,2);assert(!run_exit_tail());assert(launches==1 && syncs==2 && warnings==1 && exit_seen);
    reset(game,0);success=1;if(!setjmp(handed_off)) {run_exit_tail();assert(0);}
    assert(launches==1 && syncs==1 && !exits && !exit_seen);
    }
    puts("PASS: final sync gates handoff; normal and failed-LoadExec exits flush their marker; sync failure stays explicit");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-log-handoff-') as tmp:
    out = Path(tmp)
    (out / 'test.c').write_text(prefix + '#ifdef XV_RUN_RECOMP\n' + source[start:] + suffix)
    subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-DXV_RUN_RECOMP', '-fsanitize=address,undefined', '-I' + str(ROOT),
                    str(out / 'test.c'), '-o', str(out / 'test')], check=True)
    subprocess.run([str(out / 'test')], check=True)
