#include "screen.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/common_dialog.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

/* Original 5x7 glyphs in 8x8 cells; lowercase uses uppercase glyphs. */
static const uint8_t glyph[][7] = {
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {15,16,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
static uint32_t *fb;
static SceUID fd = -1;
enum { FB_BYTES=0x240000, FB_COUNT=2 };
static struct {
    SceUID mem;
    uint32_t *data;
    SceGxmSyncObject *sync;
    SceGxmColorSurface surface;
    int mapped;
} buffers[FB_COUNT] = {{.mem=-1},{.mem=-1}};
static int gxm, ready, count, io_failed;
static unsigned front, back=1;
static int display_error;
static char lines[56][118];
static char status[4][118];
void screen_status(int slot, const char *fmt, ...)
{
    if (slot < 0 || slot >= 4) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status[slot], sizeof(status[slot]), fmt, ap);
    va_end(ap);
    log_line("%s", status[slot]);
}
void log_line(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    size_t n = strlen(buf);
    if (fd >= 0) {
        size_t done = 0;
        while (done < n) {
            int rc = sceIoWrite(fd, buf + done, n - done);
            if (rc <= 0) { io_failed = 1; break; }
            done += (size_t)rc;
        }
        if (sceIoWrite(fd, "\n", 1) != 1) io_failed = 1;
    }
    for (size_t off = 0; off < n || off == 0; off += 117) {
        if (count == 56) { memmove(lines, lines + 1, 55 * sizeof(lines[0])); count--; }
        snprintf(lines[count++], 118, "%.117s", buf + off);
    }
}
int result(const char *name, int rc)
{
    log_line("%s = %d (0x%08X)", name, rc, (unsigned)rc);
    return rc;
}
static void text(int x, int y, const char *s, uint32_t color)
{
    for (; *s && x < 952; ++s, x += 8) {
        unsigned char c = (unsigned char)toupper((unsigned char)*s);
        uint8_t rows[7] = {0};
        if (c >= '0' && c <= '9') memcpy(rows, glyph[c - '0'], 7);
        else if (c >= 'A' && c <= 'Z') memcpy(rows, glyph[c - 'A' + 10], 7);
        else switch (c) {
        case ' ': break;
        case '.': rows[6]=4; break;
        case ':': rows[2]=4; rows[5]=4; break;
        case ',': rows[5]=4; rows[6]=8; break;
        case '-': rows[3]=14; break;
        case '_': rows[6]=31; break;
        case '=': rows[2]=31; rows[4]=31; break;
        case '/': for (int r=0;r<7;r++) rows[r]=1 << (r*4/6); break;
        case '(': rows[0]=2; rows[6]=2; for(int r=1;r<6;r++) rows[r]=4; break;
        case ')': rows[0]=8; rows[6]=8; for(int r=1;r<6;r++) rows[r]=4; break;
        case '%': rows[0]=17; rows[1]=2; rows[2]=4; rows[3]=8; rows[4]=17; break;
        default: rows[0]=14; rows[1]=17; rows[2]=2; rows[3]=4; rows[5]=4; break;
        }
        for (int r=0;r<7;r++) for(int b=0;b<5;b++)
            if (rows[r] & (1 << (4-b))) fb[(y+r)*1024+x+b] = color;
    }
}
/* GXM retires dialog rendering before this callback. Wait for the display switch
 * before the recording thread can reuse the previous front buffer. Logging and
 * CPU painting stay on the main thread. */
static void display_callback(const void *data)
{
    SceDisplayFrameBuf frame = { .size=sizeof(frame), .base=*(void *const *)data,
        .pitch=1024, .pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, .width=960, .height=544 };
    int rc=sceDisplaySetFrameBuf(&frame,SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (rc>=0) rc=sceDisplayWaitVblankStart();
    if (rc<0) __atomic_store_n(&display_error,rc,__ATOMIC_RELEASE);
}
static int allocate_framebuffer(unsigned slot)
{
    buffers[slot].mem=CALL(sceKernelAllocMemBlock("adhoc_fb",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,FB_BYTES,NULL));
    if (buffers[slot].mem<0) return -1;
    return CALL(sceKernelGetMemBlockBase(buffers[slot].mem,(void **)&buffers[slot].data));
}
int screen_init(void)
{
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir("ux0:data/xita", 0777);
    fd = sceIoOpen("ux0:data/xita/adhoc.log", SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND, 0777);
    result("sceIoOpen adhoc.log", fd);
    log_line("Xita AdHoc Test UI revision 20260908b");
    /* Keep a CPU-only error screen available even if GXM initialization fails. */
    if (allocate_framebuffer(0)<0) return -1;
    fb=buffers[0].data;
    SceGxmInitializeParams p = {
        .flags=SCE_GXM_INITIALIZE_FLAG_DEFAULT, .displayQueueMaxPendingCount=1,
        .displayQueueCallback=display_callback, .displayQueueCallbackDataSize=sizeof(void *),
        .parameterBufferSize=SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE
    };
    log_line("GXM parameter buffer %u bytes; display queue depth %u",
        (unsigned)p.parameterBufferSize,p.displayQueueMaxPendingCount);
    if (CALL(sceGxmInitialize(&p)) < 0) return -1;
    gxm = 1;
    if (allocate_framebuffer(1)<0) return -1;
    for (unsigned i=0;i<FB_COUNT;i++) {
        if (CALL(sceGxmMapMemory(buffers[i].data,FB_BYTES,SCE_GXM_MEMORY_ATTRIB_RW))<0) return -1;
        buffers[i].mapped=1;
        if (CALL(sceGxmColorSurfaceInit(&buffers[i].surface,SCE_GXM_COLOR_FORMAT_A8B8G8R8,
            SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,
            SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,960,544,1024,buffers[i].data))<0) return -1;
        if (CALL(sceGxmSyncObjectCreate(&buffers[i].sync))<0) return -1;
    }
    SceCommonDialogConfigParam cp;
    sceCommonDialogConfigParamInit(&cp);
    if (CALL(sceCommonDialogSetConfigParam(&cp))<0) return -1;
    ready=1;
    return 0;
}
static int display_result(const char *name, int rc)
{
    if (rc<0) {
        result(name,rc);
        __atomic_store_n(&display_error,rc,__ATOMIC_RELEASE);
    }
    return rc;
}
int screen_draw(int dialog)
{
    if (!fb) return 0;
    if (__atomic_load_n(&display_error,__ATOMIC_ACQUIRE)<0) return -1;
    if (ready) {
        /* Diagnostic UI runs at 10 Hz. Retire the previous swap before CPU
         * painting; no changes to Halo's rendering queues or frame policy. */
        int rc=sceGxmDisplayQueueFinish();
        if (display_result("sceGxmDisplayQueueFinish",rc)<0) return -1;
        rc=__atomic_load_n(&display_error,__ATOMIC_ACQUIRE);
        if (display_result("display callback",rc)<0) return -1;
        fb=buffers[back].data;
    }
    memset(fb, 0, 1024*544*4);
    text(8, 8, "Xita AdHoc Test - Cross exits - ux0:data/xita/adhoc.log", 0xff40ff80);
    if (fd < 0 || io_failed) text(8, 20, "LOG FILE WRITE FAILED", 0xff4040ff);
    for (int i=0;i<4;i++) text(8, 36+i*10, status[i], 0xff40ff80);
    for (int i=0;i<count;i++) text(8, 84+i*8, lines[i], 0xffffffff);
    __asm__ volatile("dsb sy" ::: "memory");
    if (dialog && ready) {
        SceCommonDialogUpdateParam up = {0};
        up.renderTarget.colorSurfaceData = fb;
        up.renderTarget.surfaceType = SCE_GXM_COLOR_SURFACE_LINEAR;
        up.renderTarget.colorFormat = SCE_GXM_COLOR_FORMAT_A8B8G8R8;
        up.renderTarget.width=960; up.renderTarget.height=544; up.renderTarget.strideInPixels=1024;
        up.displaySyncObject = buffers[back].sync;
        if (display_result("sceCommonDialogUpdate",sceCommonDialogUpdate(&up))<0) return -1;
    }
    if (ready) {
        if (display_result("sceGxmPadHeartbeat",sceGxmPadHeartbeat(&buffers[back].surface,buffers[back].sync))<0) return -1;
        if (display_result("sceGxmDisplayQueueAddEntry",
            sceGxmDisplayQueueAddEntry(buffers[front].sync,buffers[back].sync,&fb))<0) return -1;
        front=back;back=(back+1)%FB_COUNT;
        return 0;
    }
    SceDisplayFrameBuf frame = { .size=sizeof(frame), .base=fb, .pitch=1024,
        .pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, .width=960, .height=544 };
    int rc = sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (display_result("sceDisplaySetFrameBuf",rc)<0) return -1;
    return display_result("sceDisplayWaitVblankStart",sceDisplayWaitVblankStart());
}
void screen_close(void)
{
    if (gxm && __atomic_load_n(&display_error,__ATOMIC_ACQUIRE)>=0)
        display_result("sceGxmDisplayQueueFinish",sceGxmDisplayQueueFinish());
    if (__atomic_load_n(&display_error,__ATOMIC_ACQUIRE)<0) {
        goto process_cleanup;
    }
    if (display_result("sceDisplaySetFrameBuf",sceDisplaySetFrameBuf(NULL, SCE_DISPLAY_SETBUF_NEXTFRAME))<0)
        goto process_cleanup;
    if (display_result("sceDisplayWaitVblankStart",sceDisplayWaitVblankStart())<0)
        goto process_cleanup;
    /* Verify detachment too: some implementations accept NULL without actually
     * releasing the displayed buffer. Keep its storage until process exit. */
    SceDisplayFrameBuf displayed={.size=sizeof(displayed)};
    if (CALL(sceDisplayGetFrameBuf(&displayed,SCE_DISPLAY_SETBUF_IMMEDIATE))<0)
        goto process_cleanup;
    for (unsigned i=0;i<FB_COUNT;i++) {
        if (buffers[i].data && displayed.base==buffers[i].data)
            goto process_cleanup;
    }
    for (unsigned i=0;i<FB_COUNT;i++) {
        if (buffers[i].sync) CALL(sceGxmSyncObjectDestroy(buffers[i].sync));
        if (buffers[i].mapped) CALL(sceGxmUnmapMemory(buffers[i].data));
    }
    if (gxm) CALL(sceGxmTerminate());
    for (unsigned i=0;i<FB_COUNT;i++) if (buffers[i].mem>=0) CALL(sceKernelFreeMemBlock(buffers[i].mem));
    if (fd >= 0) sceIoClose(fd);
    return;
process_cleanup:
    /* A failed submission has no reliable retirement fence. Do not free
     * storage the driver might still reference; process exit reclaims it. */
    log_line("Display retirement unconfirmed; process exit will release graphics resources.");
    if (fd>=0) sceIoClose(fd);
}
