/* Pump-owned, count-only observer of sealed command lists and existing scene
 * ends. Include after cmdlist_t. No guest/GPU reads, allocations or publication.
 * Counts describe recorded potential work, including draws later skipped. */
#pragma once
#ifdef XV_VISIBILITY_PLACEMENT
#include <limits.h>
extern uint64_t xk_os_monotonic_us(void) __attribute__((weak));
enum { VP_BOUNDS=1, VP_TAG=2, VP_GENERATION=4, VP_TARGET=8, VP_UI=16,
       VP_KIND=32, VP_OPEN=64, VP_RESULT=128 };
enum { VP_ERR_TARGET=1, VP_ERR_BEGIN=2, VP_ERR_END=4 };
typedef struct {
    uint32_t frame, active, replayed, rtt, unsupported, errors;
    uint32_t slots, issued, unissued, reused, writers, writer_slots, tagged_clears;
    uint32_t first_writer, last_writer, draws, cmds, ui, closed, last_end;
    uint32_t cut, cut_cmd, cut_ui, after_draws, after_ui;
    uint64_t indices, after_indices;
} xv_visibility_placement_packet;
typedef struct {
    uint64_t packets, no_query, no_writer, boundary, last_final, all_final;
    uint64_t unsupported, errors, not_replayed, no_buffer, abandoned, orphan;
    uint64_t reasons[8], error_reasons[3];
    uint64_t slots, issued, unissued, reused, writers, writer_slots, tagged_clears;
    uint64_t draws, indices, cmds, ui, closed, rtt;
    uint64_t writer_packets, last_command, last_command_high, after_draws, after_indices, after_ui;
    uint64_t cut_scene, cut_command, cut_ui, tail_draws, tail_indices, tail_ui, tail_scenes;
    uint64_t tail_draws_high, tail_indices_high, tail_ui_high, tail_scenes_high, empty_tail;
    uint64_t scan_cmds, scan_ui, scan_slots, scan_us, previous_report_us;
} xv_visibility_placement_totals;
static xv_visibility_placement_packet g_visibility_placement[XV_NUM_LISTS];
static xv_visibility_placement_totals g_visibility_placement_totals;
_Static_assert(XV_VISIBILITY_IDS==512 && XV_VISIBILITY_PER_FRAME==512,
               "placement bitsets require the audited 512-slot limit");
_Static_assert(sizeof(xv_visibility_placement_packet)<=128, "bounded observer state");
static uint64_t vp_now(void) { return xk_os_monotonic_us ? xk_os_monotonic_us() : 0; }
static void vp_begin(const cmdlist_t *l, uint32_t frame)
{
    uint64_t start=vp_now();
    xv_visibility_placement_packet *p=&g_visibility_placement[frame % XV_NUM_LISTS];
    xv_visibility_placement_totals *t=&g_visibility_placement_totals;
    uint32_t seen[16]={0}, written[16]={0};
    if(p->active)t->abandoned++;
    memset(p,0,sizeof *p); p->frame=frame;p->active=1;
    p->first_writer=p->last_writer=UINT32_MAX;
    if(l->ncmds>XV_MAX_CMDS || l->nui>sizeof l->ui/sizeof l->ui[0] ||
       l->nvisibility>XV_VISIBILITY_PER_FRAME) {
        p->unsupported|=VP_BOUNDS; goto done; /* never follow an invalid length */
    }
    p->cmds=l->ncmds;p->ui=l->nui;p->slots=l->nvisibility;
    if(l->active_visibility)p->unsupported|=VP_OPEN;
    for(unsigned i=0;i<l->nvisibility;i++) {
        t->scan_slots++;
        if(!l->visibility[i].serial) {p->unissued++;continue;}
        p->issued++;
        unsigned s=l->visibility[i].result_slot;
        if(s>=XV_VISIBILITY_IDS) {p->unsupported|=VP_RESULT;continue;}
        uint32_t bit=1u<<(s%32);
        if(seen[s/32]&bit)p->reused++;
        seen[s/32]|=bit;
    }
    for(unsigned i=0;i<l->ncmds;i++) {
        const cmd_t *c=&l->cmds[i];t->scan_cmds++;
        if(c->pass>XV_RT_SLOTS)p->unsupported|=VP_TARGET;
        if(c->kind==1) {p->tagged_clears+=!!c->visibility;continue;}
        if(c->kind!=0) {p->unsupported|=VP_KIND;continue;}
        p->draws++;p->indices+=c->index_count;
        if(!c->visibility) {
            if(p->writers) {p->after_draws++;p->after_indices+=c->index_count;}
            continue;
        }
        p->after_draws=0;p->after_indices=0;
        p->writers++;p->last_writer=i;
        if(p->first_writer==UINT32_MAX)p->first_writer=i;
        unsigned slot=c->visibility-1u;
        if(slot>=l->nvisibility) {p->unsupported|=VP_TAG;continue;}
        uint32_t bit=1u<<(slot%32);
        if(!(written[slot/32]&bit))p->writer_slots++;
        written[slot/32]|=bit;
        if(!l->visibility[slot].serial)p->unsupported|=VP_GENERATION;
    }
    for(unsigned i=0;i<l->nui;i++) {
        t->scan_ui++;
        p->after_ui+=p->writers && l->ui[i].before>p->last_writer;
        if(l->ui[i].before>l->ncmds || (i && l->ui[i].before<l->ui[i-1].before))
            p->unsupported|=VP_UI;
        if(l->ui[i].target>XV_RT_SLOTS)p->unsupported|=VP_TARGET;
    }
done:
    t->scan_us+=vp_now()-start;
}
/* Called only after an existing RTT EndScene. cmd/ui are exclusive replay
 * cursors; a writer at cmd belongs to a later scene and forbids this cut. */
static void vp_end(uint32_t frame,unsigned cmd,unsigned ui,int result)
{
    xv_visibility_placement_packet *p=&g_visibility_placement[frame % XV_NUM_LISTS];
    if(!p->active || p->frame!=frame)return;
    if(result<0) {p->errors|=VP_ERR_END;return;}
    p->closed++;p->last_end=cmd;
    if(cmd>p->cmds || ui>p->ui) {p->unsupported|=VP_BOUNDS;return;}
    if(p->writers && cmd>p->last_writer && !p->cut) {
        p->cut=p->closed;p->cut_cmd=cmd;p->cut_ui=ui;
    }
}
static void vp_error(uint32_t frame,unsigned error)
{
    xv_visibility_placement_packet *p=&g_visibility_placement[frame % XV_NUM_LISTS];
    if(p->active && p->frame==frame)p->errors|=error;
}
static void vp_replayed(uint32_t frame,unsigned rtt)
{
    xv_visibility_placement_packet *p=&g_visibility_placement[frame % XV_NUM_LISTS];
    if(p->active && p->frame==frame) {p->replayed=1;p->rtt=rtt;}
}
static void vp_report(void)
{
    xv_visibility_placement_totals *t=&g_visibility_placement_totals;
    if(t->packets<60)return;
    uint64_t start=vp_now();
#define VP_U(x) ((unsigned long long)t->x)
    XV_LOG("[visibility-placement] %llu packets: no-query %llu slots-no-writer %llu boundary %llu last-writer-final %llu unsupported %llu replay-error %llu not-replayed %llu; all-writers-final %llu no-buffer %llu abandoned %llu orphan %llu\n",
        VP_U(packets),VP_U(no_query),VP_U(no_writer),VP_U(boundary),VP_U(last_final),VP_U(unsupported),VP_U(errors),VP_U(not_replayed),VP_U(all_final),VP_U(no_buffer),VP_U(abandoned),VP_U(orphan));
    XV_LOG("[visibility-placement-work] slots/issued/unissued/reused-result %llu/%llu/%llu/%llu potential-writers/unique-slots/tagged-clears %llu/%llu/%llu; recorded cmds/draws/indices/ui %llu/%llu/%llu/%llu successful-RTT-ends %llu RTT-packets %llu\n",
        VP_U(slots),VP_U(issued),VP_U(unissued),VP_U(reused),VP_U(writers),VP_U(writer_slots),VP_U(tagged_clears),VP_U(cmds),VP_U(draws),VP_U(indices),VP_U(ui),VP_U(closed),VP_U(rtt));
    XV_LOG("[visibility-placement-last] writer-packets %llu last-writer-cmd-plus1 sum/max %llu/%llu; after-last-writer recorded draws/indices/ui %llu/%llu/%llu; includes unsupported metadata, check declines\n",
        VP_U(writer_packets),VP_U(last_command),VP_U(last_command_high),VP_U(after_draws),VP_U(after_indices),VP_U(after_ui));
    XV_LOG("[visibility-placement-tail] boundary-packets %llu first-end scene/cmd/ui sums %llu/%llu/%llu; remaining draws/indices/ui/scenes sums %llu/%llu/%llu/%llu max %llu/%llu/%llu/%llu empty-draw-ui-tail %llu; overlay/settings excluded, counts are not GPU time\n",
        VP_U(boundary),VP_U(cut_scene),VP_U(cut_command),VP_U(cut_ui),VP_U(tail_draws),VP_U(tail_indices),VP_U(tail_ui),VP_U(tail_scenes),VP_U(tail_draws_high),VP_U(tail_indices_high),VP_U(tail_ui_high),VP_U(tail_scenes_high),VP_U(empty_tail));
    XV_LOG("[visibility-placement-declines] bounds/tag/unissued-writer/target/ui/kind/open-query/result-slot %llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu; RTT-errors target/begin/end %llu/%llu/%llu; final main-scene status unobserved\n",
        VP_U(reasons[0]),VP_U(reasons[1]),VP_U(reasons[2]),VP_U(reasons[3]),VP_U(reasons[4]),VP_U(reasons[5]),VP_U(reasons[6]),VP_U(reasons[7]),VP_U(error_reasons[0]),VP_U(error_reasons[1]),VP_U(error_reasons[2]));
    XV_LOG("[visibility-placement-cost] scanned cmds/ui/slots %llu/%llu/%llu scan-aggregate-us %llu previous-report-us %llu; fixed-storage %u bytes; structural observation only, no early publication\n",
        VP_U(scan_cmds),VP_U(scan_ui),VP_U(scan_slots),VP_U(scan_us),VP_U(previous_report_us),(unsigned)(sizeof g_visibility_placement+sizeof *t));
#undef VP_U
    memset(t,0,sizeof *t);t->previous_report_us=vp_now()-start;
}
/* After result publication/notification, outside replay loops. The source
 * list is still frame-owned. Main's final scene result is deliberately unknown. */
static void vp_complete(const cmdlist_t *l,uint32_t frame)
{
    uint64_t start=vp_now();
    xv_visibility_placement_packet *p=&g_visibility_placement[frame % XV_NUM_LISTS];
    xv_visibility_placement_totals *t=&g_visibility_placement_totals;
    if(!p->active || p->frame!=frame) {t->orphan++;return;}
    p->active=0;t->packets++;
#define VP_ADD(x) t->x+=p->x
    VP_ADD(slots);VP_ADD(issued);VP_ADD(unissued);VP_ADD(reused);VP_ADD(writers);
    VP_ADD(writer_slots);VP_ADD(tagged_clears);VP_ADD(draws);VP_ADD(indices);
    VP_ADD(cmds);VP_ADD(ui);VP_ADD(closed);VP_ADD(rtt);
    VP_ADD(after_draws);VP_ADD(after_indices);VP_ADD(after_ui);
#undef VP_ADD
    if(p->writers) {
        uint64_t command=p->last_writer+1u;t->writer_packets++;t->last_command+=command;
        if(command>t->last_command_high)t->last_command_high=command;
    }
    for(unsigned i=0;i<8;i++)t->reasons[i]+=!!(p->unsupported&(1u<<i));
    for(unsigned i=0;i<3;i++)t->error_reasons[i]+=!!(p->errors&(1u<<i));
    if(p->slots && !l->visibility_gpu_ready)t->no_buffer++;
    if(p->errors)t->errors++;
    else if(p->unsupported)t->unsupported++;
    else if(!p->replayed)t->not_replayed++;
    else if(!p->slots && !p->writers)t->no_query++;
    else if(!p->writers)t->no_writer++;
    else if(p->cut) {
        uint64_t draws=0,indices=0,ui=p->ui-p->cut_ui,scenes=p->closed-p->cut+1u;
        for(unsigned i=p->cut_cmd;i<p->cmds;i++) {
            t->scan_cmds++;
            if(l->cmds[i].kind==0) {draws++;indices+=l->cmds[i].index_count;}
        }
        t->boundary++;t->cut_scene+=p->cut;t->cut_command+=p->cut_cmd;t->cut_ui+=p->cut_ui;
        t->tail_draws+=draws;t->tail_indices+=indices;t->tail_ui+=ui;t->tail_scenes+=scenes;
#define VP_MAX(field,value) if((value)>t->field)t->field=(value)
        VP_MAX(tail_draws_high,draws);VP_MAX(tail_indices_high,indices);
        VP_MAX(tail_ui_high,ui);VP_MAX(tail_scenes_high,scenes);
#undef VP_MAX
        t->empty_tail+=!draws && !ui;
    } else {t->last_final++;t->all_final+=p->first_writer>=p->last_end;}
    t->scan_us+=vp_now()-start;
    vp_report();
}
#define XV_VP_BEGIN(l,f) vp_begin(l,f)
#define XV_VP_END(f,c,u,r) vp_end(f,c,u,r)
#define XV_VP_ERROR(f,e) vp_error(f,e)
#define XV_VP_REPLAYED(f,r) vp_replayed(f,r)
#define XV_VP_COMPLETE(l,f) vp_complete(l,f)
#else
#define XV_VP_BEGIN(l,f) ((void)0)
#define XV_VP_END(f,c,u,r) ((void)0)
#define XV_VP_ERROR(f,e) ((void)0)
#define XV_VP_REPLAYED(f,r) ((void)0)
#define XV_VP_COMPLETE(l,f) ((void)0)
#endif
