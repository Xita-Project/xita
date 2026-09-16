/* Real UI publication with a delayed consumer. No resets may touch a slot
 * still referenced by an earlier packet; late UI writes precede publication. */
#include <assert.h>
#include <stdio.h>
void xv_native_math_report(unsigned n) {}
void xd3d_prepare_report(unsigned n) {}
void xv_texture_worker_report(void) {}
void xv_geometry_worker_report(void) {}
void xv_draw_profile_report(unsigned n) {}
void xv_vertex_upload_report(unsigned n) {}
#include "../../runtime/xv_ui_gxm.c"
static unsigned submitted, consumed, sealed, begun, queued_ui, published_ui;
static struct { unsigned slot, verts, batches; } packets[128];
static int dirty_ui;
static unsigned reports_started,reports_ended;
static int report_active;
int xv_log_report_begin(void)
{ assert(!report_active);report_active=1;reports_started++;return 1; }
int xv_log_report_begin_frame(unsigned frame)
{ (void)frame;return xv_log_report_begin(); }
void xv_log_get_status(xv_log_status *out)
{ memset(out,0,sizeof *out); }
void xv_log_report_end(void)
{ assert(report_active);report_active=0;reports_ended++; }
void xv_settings_snapshot(xv_dash_graphics_view *view)
{ memset(view,0,sizeof(*view));view->active=1;view->selected=(int)submitted; }
volatile uint64_t xv_pump_us_acc;
unsigned xv_d3d_draw_acc, xv_d3d_bsp_acc, xv_n_kicks, xv_n_fires;
uint64_t xv_t_vbcb_us, xv_t_draw_us;
void xv_logf(const char *fmt, ...) {}
void xv_gpu_flush(const void *p, uint32_t n) {}
void xv_gpu_flush_ui(const void *p, uint32_t n) { dirty_ui=1; queued_ui++; }
void xv_gpu_flush_pending(void) { assert(dirty_ui); dirty_ui=0; published_ui++; }
uint64_t xk_os_monotonic_us(void) { static uint64_t t; return ++t; }
void xd3d_hist_small_check(unsigned f, unsigned d) {}
int sceGxmDisplayQueueFinish(void) { return 0; }
static void consume(void)
{
    assert(consumed<submitted);
    unsigned slot=packets[consumed].slot;
    assert(g.frame[slot].vcount==packets[consumed].verts);
    assert(g.frame[slot].bcount==packets[consumed].batches);
    assert(g.frame[slot].settings.active && g.frame[slot].settings.selected==(int)consumed);
    consumed++;
}
void xv_present_drain(void) { while(consumed!=submitted)consume(); }
uint32_t xv_d3d_EndFrame(void) { return sealed++; }
void xv_d3d_BeginFrame(void)
{
    for(unsigned i=consumed;i<submitted;i++)assert(packets[i].slot!=g.rec);
    begun++;
}
void xv_present(void)
{
    assert(!dirty_ui && queued_ui==submitted+1 && published_ui==submitted+1);
    unsigned slot=xv_ui_gxm_published_frame();
    assert(slot<XV_FRAME_SLOTS && slot!=g.rec);
    packets[submitted].slot=slot;
    packets[submitted].verts=g.frame[slot].vcount;
    packets[submitted].batches=g.frame[slot].bcount;
    submitted++;
    /* Acquire only the next slot. Other packets stay live. */
    if(submitted-consumed==XV_FRAME_SLOTS)consume();
    for(unsigned i=consumed;i<submitted;i++)assert(packets[i].slot!=g.rec);
}
int main(void)
{
    g.ready=1; g.pub=-1; g_mesh_path=1;
    for(unsigned f=0;f<120;f++) {
        g.frame[g.rec].bcount=3+f;g.frame[g.rec].vcount=4*(3+f);
        xd3d_r_present(f,1);
        assert(!g.frame[g.rec].bcount && !g.frame[g.rec].vcount);
        assert(submitted-consumed==(f ? 2u : 1u));
    }
    xv_present_drain();
    assert(consumed==120 && sealed==120 && begun==120);
    const char *batch=getenv("XV_PROFILE_BATCH");
    unsigned expected=batch && !atoi(batch) ? 0u : 2u;
    assert(!report_active && reports_started==expected && reports_ended==expected);
    puts("PASS: triple UI ownership, late write publication, slow consumer and 120 frame rotations");
}
