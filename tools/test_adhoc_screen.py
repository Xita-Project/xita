#!/usr/bin/env python3
"""Exercise the real diagnostic UI against deferred display callbacks and faults.

Host-only firmware model: tests ownership and error handling, not GXM firmware
behavior or wireless connectivity. The ARM barrier becomes a host full fence.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MODEL = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int SceUID;
typedef struct { int id; } SceGxmSyncObject;
typedef struct { void *data; } SceGxmColorSurface;
typedef struct {
    unsigned size; void *base; unsigned pitch, pixelformat, width, height;
} SceDisplayFrameBuf;
typedef struct {
    unsigned flags, displayQueueMaxPendingCount;
    void (*displayQueueCallback)(const void *);
    unsigned displayQueueCallbackDataSize, parameterBufferSize;
} SceGxmInitializeParams;
typedef struct { int unused; } SceCommonDialogConfigParam;
typedef struct {
    struct { void *colorSurfaceData; int surfaceType, colorFormat;
             unsigned width, height, strideInPixels; } renderTarget;
    SceGxmSyncObject *displaySyncObject;
} SceCommonDialogUpdateParam;
enum {
    SCE_DISPLAY_PIXELFORMAT_A8B8G8R8=1, SCE_DISPLAY_SETBUF_NEXTFRAME=1, SCE_DISPLAY_SETBUF_IMMEDIATE=0,
    SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW=1, SCE_GXM_INITIALIZE_FLAG_DEFAULT=0,
    SCE_GXM_MEMORY_ATTRIB_RW=3, SCE_GXM_COLOR_FORMAT_A8B8G8R8=1,
    SCE_GXM_COLOR_SURFACE_LINEAR=0, SCE_GXM_COLOR_SURFACE_SCALE_NONE=0,
    SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT=0, SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE=0x1000000,
    SCE_O_WRONLY=1, SCE_O_CREAT=2, SCE_O_APPEND=4
};
static int init_calls, fail_init, fail_draw, hit_draw, allocated, mapped_count, sync_count;
static int freed, destroyed, unmapped, terminated, closed, queued, callbacks, draws;
static int close_fault;
static void *storage[2], *shown, *pending, *switch_to;
static uint64_t shown_hash, pending_hash;
static SceGxmSyncObject syncs[2];
static void (*callback)(const void *);
static int step(void) { return ++init_calls==fail_init ? -91 : 0; }
static int fault(int id) { if (fail_draw==id) { hit_draw=1; return -92; } return 0; }
static uint64_t hash(const void *p) {
    const uint32_t *v=p; uint64_t h=0;
    for (unsigned i=0;i<1024*544;i++) h=h*31+v[i];
    return h;
}
static void intact(void) {
    if (shown) assert(hash(shown)==shown_hash);
    if (pending) assert(hash(pending)==pending_hash);
}
static int sceIoMkdir(const char *p,int mode) { (void)p;(void)mode;return 0; }
static int sceIoOpen(const char *p,int flags,int mode) { (void)p;(void)flags;(void)mode;return 9; }
static int sceIoWrite(int fd,const void *p,size_t n) { assert(fd==9);(void)p;return (int)n; }
static int sceIoClose(int fd) { assert(fd==9);closed++;return 0; }
static int sceKernelAllocMemBlock(const char *n,int t,unsigned size,void *opts) {
    (void)n;(void)opts;assert(t==SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW);
    assert(size==0x240000); if (step()<0) return -91;
    int i=allocated++; assert(i<2);storage[i]=calloc(1,size);assert(storage[i]);return i+10;
}
static int sceKernelGetMemBlockBase(int id,void **out) {
    if (step()<0) return -91;
    assert(id>=10 && id<12); *out=storage[id-10]; return 0;
}
static int sceKernelFreeMemBlock(int id) {
    assert(!shown && !pending);assert(id>=10 && id<12);
    free(storage[id-10]); storage[id-10]=NULL;freed++;return 0;
}
static int sceGxmInitialize(const SceGxmInitializeParams *p) {
    assert(p->flags==0 && p->parameterBufferSize==0x1000000);
    assert(p->displayQueueMaxPendingCount==1 && p->displayQueueCallback);
    assert(p->displayQueueCallbackDataSize==sizeof(void *));
    if (step()<0) return -91; callback=p->displayQueueCallback;return 0;
}
static int sceGxmMapMemory(void *p,unsigned size,int attrs) {
    assert(p && size==0x240000 && attrs==3);if(step()<0)return -91;mapped_count++;return 0;
}
static int sceGxmColorSurfaceInit(SceGxmColorSurface *s,int fmt,int type,int scale,
                                 int output,unsigned w,unsigned h,unsigned stride,void *data) {
    assert(fmt==1 && type==0 && scale==0 && output==0 && w==960 && h==544 && stride==1024);
    if(step()<0)return -91;s->data=data;return 0;
}
static int sceGxmSyncObjectCreate(SceGxmSyncObject **out) {
    if(step()<0)return -91;int i=sync_count++;assert(i<2);syncs[i].id=i;*out=&syncs[i];return 0;
}
static int sceGxmSyncObjectDestroy(SceGxmSyncObject *p) {
    assert(p && !pending && !shown);destroyed++;return 0;
}
static int sceGxmUnmapMemory(void *p) { assert(p && !pending && !shown);unmapped++;return 0; }
static int sceGxmTerminate(void) { assert(!pending && !shown);terminated++;return 0; }
static void sceCommonDialogConfigParamInit(SceCommonDialogConfigParam *p) { memset(p,0,sizeof(*p)); }
static int sceCommonDialogSetConfigParam(SceCommonDialogConfigParam *p) { (void)p;return step(); }
static int sceDisplaySetFrameBuf(const SceDisplayFrameBuf *p,int when) {
    assert(when==SCE_DISPLAY_SETBUF_NEXTFRAME);if(fault(5)<0)return -92;
    if(p) { assert(p->width==960 && p->height==544 && p->pitch==1024);switch_to=p->base; }
    else if(close_fault!=7) switch_to=NULL;
    return 0;
}
static int sceDisplayGetFrameBuf(SceDisplayFrameBuf *p,int when) {
    assert(when==SCE_DISPLAY_SETBUF_IMMEDIATE && p->size==sizeof(*p));
    if(close_fault==8)return -93;p->base=shown;return 0;
}
static int sceDisplayWaitVblankStart(void) {
    if(fault(6)<0)return -92;shown=switch_to;shown_hash=shown ? hash(shown) : 0;return 0;
}
static int sceGxmDisplayQueueFinish(void) {
    intact();if(fault(1)<0)return -92;
    if(pending) { void *data=pending;pending=NULL;callback(&data);callbacks++; }
    return 0;
}
static int sceCommonDialogUpdate(const SceCommonDialogUpdateParam *p) {
    intact();assert(!pending && p->renderTarget.colorSurfaceData!=shown);
    assert(p->displaySyncObject && p->renderTarget.width==960 && p->renderTarget.height==544);
    assert(p->renderTarget.strideInPixels==1024);if(fault(2)<0)return -92;
    ((uint32_t *)p->renderTarget.colorSurfaceData)[0]=0xaabbccdd;return 0;
}
static int sceGxmPadHeartbeat(const SceGxmColorSurface *s,SceGxmSyncObject *sync) {
    intact();assert(!pending && s->data!=shown && sync);return fault(3);
}
static int sceGxmDisplayQueueAddEntry(SceGxmSyncObject *old,SceGxmSyncObject *next,const void *data) {
    intact();assert(!pending && old && next && old!=next);
    assert(*(void *const *)data==storage[next->id]);if(fault(4)<0)return -92;
    pending=*(void *const *)data;pending_hash=hash(pending);queued++;return 0;
}
'''
DRIVER = r'''
int main(int argc,char **argv) {
    assert(argc==4);fail_init=atoi(argv[1]);fail_draw=atoi(argv[2]);close_fault=atoi(argv[3]);
    int rc=screen_init();
    if(fail_init) {
        assert(rc<0 && init_calls==fail_init);
        /* Initialization never submits GPU work; its error page remains usable. */
        assert(screen_draw(0)==0);assert(queued==0);
    } else {
        assert(rc==0 && init_calls==12);
        for(unsigned i=0;i<8;i++) {
            screen_status(0,"Frame %u",i);rc=screen_draw(i&1);draws++;
            if(rc<0) break;
        }
        if(fail_draw) {
            assert(hit_draw && rc<0);
            uint64_t h0=hash(storage[0]),h1=hash(storage[1]);
            assert(screen_draw(1)<0); /* A lost retirement fence forbids repaint. */
            assert(hash(storage[0])==h0 && hash(storage[1])==h1);
        } else assert(queued==8 && callbacks==7);
    }
    if(close_fault && close_fault<7)fail_draw=close_fault;
    screen_close();assert(closed==1);
    if(fail_draw || close_fault) {
        assert(freed==0 && destroyed==0 && unmapped==0 && terminated==0);
        /* Model the OS releasing graphics ownership on process exit. */
        for(unsigned i=0;i<2;i++) free(storage[i]);
    } else {
        assert(freed==allocated && destroyed==sync_count && unmapped==mapped_count);
        assert(terminated==(callback!=NULL));
        if(!fail_init)assert(callbacks==8);
    }
    return 0;
}
'''


def main():
    source = (ROOT / "tools/adhoctest/screen.c").read_text()
    source = re.sub(r"^#include <psp2/[^>]+>\n", "", source, flags=re.M)
    barrier = '__asm__ volatile("dsb sy" ::: "memory");'
    assert source.count(barrier) == 1
    source = source.replace(barrier, "__atomic_thread_fence(__ATOMIC_SEQ_CST);")
    with tempfile.TemporaryDirectory(prefix="xita-adhoc-screen-") as tmp:
        tmp = Path(tmp)
        c_file, binary = tmp / "screen_test.c", tmp / "screen_test"
        c_file.write_text(MODEL + source + DRIVER)
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I", str(ROOT / "tools/adhoctest"), str(c_file), "-o", str(binary)], check=True)
        cases = [(0, 0, 0)] + [(n, 0, 0) for n in range(1, 13)] + [(0, n, 0) for n in range(1, 7)]
        cases += [(0, 0, n) for n in (1, 5, 6, 7, 8)]
        for init_fault, draw_fault, close_fault in cases:
            subprocess.run([str(binary), str(init_fault), str(draw_fault), str(close_fault)], check=True)
    print(f"Ad hoc screen: {len(cases)} ownership/startup/submission fault cases passed (ASan/UBSan)")


if __name__ == "__main__":
    main()
