/* Pump-only diagnostic. No readiness result from this API controls rendering,
 * query publication, waits or retirement. Ordinary builds omit every call. */
#pragma once
#ifdef XV_SCENE_CENSUS
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include "xv_frame_slots.h"
#define XV_SC_SCENES 12u
#define XV_SC_WORD_BASE (2u * XV_FRAME_TICKETS)
#define XV_SC_WORD_END (XV_SC_WORD_BASE + XV_FRAME_TICKETS * XV_SC_SCENES)
#ifndef XV_SCENE_CENSUS_NOTIFICATION_WORDS
#define XV_SCENE_CENSUS_NOTIFICATION_WORDS 0u
#endif
enum { SC_OK, SC_NO_WORDS, SC_UNTRACKED, SC_BOUNDS, SC_TARGET, SC_UI, SC_KIND, SC_OVER_CAP, SC_MISMATCH, SC_REASONS };
enum { SC_ADDED, SC_QUERY, SC_SCALED, SC_FINAL, SC_SOURCES };
typedef struct {
    uint32_t target,width,height,command,ui,draws,clears,uis;
    uint64_t indices;
} xv_sc_shape;
typedef struct {
    uint32_t command,ps_key,vs_hash,program,patcher,entry,route,reads,valid;
} xv_sc_sample;
void xv_sc_init(volatile unsigned *words,unsigned capacity);
void xv_sc_begin(uint32_t ticket,uint32_t mesh,uint64_t begin,unsigned pending,int tracked);
void xv_sc_decline(unsigned reason);
void xv_sc_plan(const xv_sc_shape *shape,unsigned sampled_command);
void xv_sc_planned(void);
uint32_t xv_sc_ticket(void);
void xv_sc_open(unsigned target,unsigned load,unsigned store);
const SceGxmNotification *xv_sc_end(unsigned target,unsigned command,unsigned ui,const SceGxmNotification *original,unsigned source);
void xv_sc_ended(int error);
int xv_sc_sampled(unsigned command);
void xv_sc_sample_draw(const xv_sc_sample *sample);
void xv_sc_submitted(int failed);
void xv_sc_observe(uint32_t completed,uint32_t submitted);
void xv_sc_fold(uint32_t ticket,int failed);
void xv_sc_report(void);
void xv_d3d_scene_census_plan(uint32_t frame,unsigned width,unsigned height,int scaled);

#ifdef XV_SCENE_CENSUS_IMPLEMENTATION
typedef struct {
    xv_sc_shape shape;
    SceGxmNotification fence;
    uint64_t lower,upper;
    uint32_t sample_command,policy,source,attached,ready,negative;
    xv_sc_sample sample;
} xv_sc_scene;
typedef struct {
    uint64_t begin,last_after;
    uint32_t ticket,mesh,count,ended,open,reason,planned,submitted,failed,live;
    xv_sc_scene scenes[XV_SC_SCENES];
} xv_sc_packet;
typedef struct {
    uint64_t lower,upper,gap_lower,gap_upper,bracket;
    uint32_t valid,no_negative,coalesced,sampled,sample_ticket,policy,sources[SC_SOURCES];
    xv_sc_sample sample;
} xv_sc_row;
static xv_sc_packet g_sc_packets[XV_FRAME_TICKETS],*g_sc_current;
static volatile unsigned *g_sc_words;
static struct {
    uint64_t sweeps,loads,sweep_us,max_sweep_us,max_poll_gap;
    uint32_t retired,valid,failed,missing,mixed,invalid,max_scenes,orphans;
    uint32_t reasons[SC_REASONS],pending[XV_FRAME_TICKETS],attached[SC_SOURCES],end_errors,count;
    xv_sc_shape shape[XV_SC_SCENES];
    xv_sc_row row[XV_SC_SCENES];
} g_sc_totals;
static int sc_delta(uint64_t a,uint64_t b,uint64_t *v)
{ *v=a-b;return *v<=INT64_MAX; }
void xv_sc_init(volatile unsigned *words,unsigned capacity)
{
    g_sc_words=words && capacity>=XV_SC_WORD_END?words:NULL;
    xv_logf("[scene-census] enabled words %u required %u available %u; packet/totals %u/%u bytes + notification reservation %u bytes; budget %u scenes, CPU completion bounds only\n",
        !!g_sc_words,XV_SC_WORD_END,capacity,(unsigned)sizeof g_sc_packets,(unsigned)sizeof g_sc_totals,
        (unsigned)(XV_FRAME_TICKETS*XV_SC_SCENES*sizeof(unsigned)),XV_SC_SCENES);
}
void xv_sc_begin(uint32_t ticket,uint32_t mesh,uint64_t begin,unsigned pending,int tracked)
{
    xv_sc_packet *p=&g_sc_packets[ticket&(XV_FRAME_TICKETS-1u)];
    if(p->live) {g_sc_totals.orphans++;g_sc_current=NULL;return;}
    memset(p,0,sizeof *p);p->ticket=ticket;p->mesh=mesh;p->begin=p->last_after=begin;p->live=1;
    p->reason=!tracked?SC_UNTRACKED:!g_sc_words?SC_NO_WORDS:SC_OK;g_sc_current=p;
    if(pending<XV_FRAME_TICKETS)g_sc_totals.pending[pending]++;
    else p->reason=SC_BOUNDS;
}
uint32_t xv_sc_ticket(void) {return g_sc_current?g_sc_current->ticket:0;}
void xv_sc_decline(unsigned reason)
{if(g_sc_current && !g_sc_current->reason)g_sc_current->reason=reason<SC_REASONS?reason:SC_MISMATCH;}
void xv_sc_plan(const xv_sc_shape *shape,unsigned sampled_command)
{
    xv_sc_packet *p=g_sc_current;if(!p)return;
    if(p->count<XV_SC_SCENES) {p->scenes[p->count].shape=*shape;p->scenes[p->count].sample_command=sampled_command;}
    p->count++;
}
void xv_sc_planned(void)
{
    xv_sc_packet *p=g_sc_current;if(!p)return;p->planned=1;
    if(p->count>g_sc_totals.max_scenes)g_sc_totals.max_scenes=p->count;
    if(!p->count)xv_sc_decline(SC_MISMATCH);
    if(p->count>XV_SC_SCENES)xv_sc_decline(SC_OVER_CAP);
    if(p->reason)return;
    unsigned q=p->ticket&(XV_FRAME_TICKETS-1u);
    for(unsigned i=0;i<p->count;i++) {
        p->scenes[i].lower=p->begin;
        p->scenes[i].fence=(SceGxmNotification){g_sc_words+XV_SC_WORD_BASE+q*XV_SC_SCENES+i,p->ticket};
        __atomic_store_n(p->scenes[i].fence.address,~p->ticket,__ATOMIC_RELEASE);
    }
}
void xv_sc_open(unsigned target,unsigned load,unsigned store)
{
    xv_sc_packet *p=g_sc_current;if(!p || p->reason)return;
    if(!p->planned || p->open || p->ended>=p->count || p->scenes[p->ended].shape.target!=target) {xv_sc_decline(SC_MISMATCH);return;}
    p->open=1;p->scenes[p->ended].policy=(!!load)|((!!store)<<1);
}
const SceGxmNotification *xv_sc_end(unsigned target,unsigned command,unsigned ui,const SceGxmNotification *original,unsigned source)
{
    xv_sc_packet *p=g_sc_current;if(!p || p->reason)return original;
    if(!p->open || p->ended>=p->count) {xv_sc_decline(SC_MISMATCH);return original;}
    xv_sc_scene *s=&p->scenes[p->ended];
    if(s->shape.target!=target || (command!=UINT32_MAX && (s->shape.command!=command || s->shape.ui!=ui))) {xv_sc_decline(SC_MISMATCH);return original;}
    s->source=original?source:SC_ADDED;
    if(original) {s->fence=*original;return original;} /* Pointer identity is preserved. */
    return &s->fence;
}
void xv_sc_ended(int error)
{
    xv_sc_packet *p=g_sc_current;if(!p || p->reason)return;
    if(!p->open || p->ended>=p->count) {xv_sc_decline(SC_MISMATCH);return;}
    xv_sc_scene *s=&p->scenes[p->ended++];s->attached=error>=0;p->open=0;
    if(s->attached && s->source<SC_SOURCES)g_sc_totals.attached[s->source]++;
    if(error<0)g_sc_totals.end_errors++;
    if(error<0)p->failed=1;
}
int xv_sc_sampled(unsigned command)
{xv_sc_packet *p=g_sc_current;return p && !p->reason && p->open && p->ended<p->count && p->scenes[p->ended].sample_command==command;}
void xv_sc_sample_draw(const xv_sc_sample *sample)
{if(xv_sc_sampled(sample->command))g_sc_current->scenes[g_sc_current->ended].sample=*sample;}
void xv_sc_submitted(int failed)
{
    xv_sc_packet *p=g_sc_current;if(!p)return;
    p->submitted=1;p->failed|=!!failed;
    if(!p->reason && (!p->planned || p->open || p->ended!=p->count))p->reason=SC_MISMATCH;
    g_sc_current=NULL;
}
void xv_sc_observe(uint32_t completed,uint32_t submitted)
{
    if(!g_sc_words)return;
    unsigned pending=submitted-completed;if(!pending || pending>=XV_FRAME_TICKETS)return;
    /* One bracket for a bounded sweep. A false earlier word followed by a true
     * later word can be GPU progress DURING this sweep, not an inversion. */
    unsigned ready[XV_FRAME_TICKETS][XV_SC_SCENES]={{0}};
    uint64_t before=sceKernelGetProcessTimeWide(),after,elapsed;
    for(unsigned n=1;n<=pending;n++) {
        uint32_t ticket=completed+n;unsigned q=ticket&(XV_FRAME_TICKETS-1u);xv_sc_packet *p=&g_sc_packets[q];
        if(!p->live || p->ticket!=ticket || !p->submitted || p->failed || p->reason)continue;
        for(unsigned i=0;i<p->count;i++)if(p->scenes[i].attached && !p->scenes[i].ready) {
            ready[q][i]=1u+(__atomic_load_n(p->scenes[i].fence.address,__ATOMIC_ACQUIRE)==p->scenes[i].fence.value);g_sc_totals.loads++;
        }
    }
    after=sceKernelGetProcessTimeWide();g_sc_totals.sweeps++;
    if(sc_delta(after,before,&elapsed)) {g_sc_totals.sweep_us+=elapsed;if(elapsed>g_sc_totals.max_sweep_us)g_sc_totals.max_sweep_us=elapsed;}
    for(unsigned n=1;n<=pending;n++) {
        uint32_t ticket=completed+n;unsigned q=ticket&(XV_FRAME_TICKETS-1u);xv_sc_packet *p=&g_sc_packets[q];
        if(!p->live || p->ticket!=ticket || !p->submitted || p->failed || p->reason)continue;
        if(!sc_delta(before,p->last_after,&elapsed)) {p->reason=SC_MISMATCH;continue;}
        if(elapsed>g_sc_totals.max_poll_gap)g_sc_totals.max_poll_gap=elapsed;
        if(!sc_delta(after,before,&elapsed)) {p->reason=SC_MISMATCH;continue;}
        p->last_after=after;
        for(unsigned i=0;i<p->count;i++) {
            xv_sc_scene *s=&p->scenes[i];
            if(ready[q][i]==2) {s->ready=1;s->upper=after;}
            else if(ready[q][i]==1) {s->negative=1;s->lower=before;}
        }
    }
}
void xv_sc_fold(uint32_t ticket,int failed)
{
    xv_sc_packet *p=&g_sc_packets[ticket&(XV_FRAME_TICKETS-1u)];g_sc_totals.retired++;
    if(!p->live || p->ticket!=ticket) {g_sc_totals.orphans++;return;}
    p->live=0;
    if(failed || p->failed) {g_sc_totals.failed++;return;}
    if(p->reason) {g_sc_totals.reasons[p->reason]++;return;}
    if(!p->submitted || !p->count || p->count>XV_SC_SCENES) {g_sc_totals.invalid++;return;}
    uint64_t lo[XV_SC_SCENES],hi[XV_SC_SCENES],gl[XV_SC_SCENES],gh[XV_SC_SCENES];
    for(unsigned i=0;i<p->count;i++) {
        xv_sc_scene *s=&p->scenes[i];uint64_t unused;
        if(!s->attached || !s->ready) {g_sc_totals.missing++;return;}
        if(!sc_delta(s->lower,p->begin,&lo[i]) || !sc_delta(s->upper,p->begin,&hi[i]) || !sc_delta(s->upper,s->lower,&unused)) {g_sc_totals.invalid++;return;}
        uint64_t a=i?p->scenes[i-1].lower:p->begin,b=i?p->scenes[i-1].upper:p->begin;
        if(!sc_delta(s->upper,a,&gh[i])) {g_sc_totals.invalid++;return;}
        gl[i]=sc_delta(s->lower,b,&unused)?unused:0;
    }
    if(!g_sc_totals.count) {g_sc_totals.count=p->count;for(unsigned i=0;i<p->count;i++) {g_sc_totals.shape[i]=p->scenes[i].shape;g_sc_totals.row[i].policy=p->scenes[i].policy;}}
    int same=g_sc_totals.count==p->count;
    for(unsigned i=0;same && i<p->count;i++)same=!memcmp(&g_sc_totals.shape[i],&p->scenes[i].shape,sizeof(xv_sc_shape)) && g_sc_totals.row[i].policy==p->scenes[i].policy;
    if(!same) {g_sc_totals.mixed++;return;}
    for(unsigned i=0;i<p->count;i++) {
        xv_sc_row *r=&g_sc_totals.row[i];
        if(UINT64_MAX-r->lower<lo[i] || UINT64_MAX-r->upper<hi[i] ||
           UINT64_MAX-r->gap_lower<gl[i] || UINT64_MAX-r->gap_upper<gh[i]) {g_sc_totals.invalid++;return;}
    }
    g_sc_totals.valid++;
    for(unsigned i=0;i<p->count;i++) {
        xv_sc_scene *s=&p->scenes[i];xv_sc_row *r=&g_sc_totals.row[i];
        r->valid++;r->no_negative+=!s->negative;r->coalesced+=i && !gl[i];
        r->lower+=lo[i];r->upper+=hi[i];r->gap_lower+=gl[i];r->gap_upper+=gh[i];
        if(hi[i]-lo[i]>r->bracket)r->bracket=hi[i]-lo[i];
        if(s->source<SC_SOURCES)r->sources[s->source]++;
        r->policy=s->policy;
        if(s->sample.valid) {r->sample=s->sample;r->sample_ticket=ticket;r->sampled++;}
    }
}
void xv_sc_report(void)
{
    xv_logf("[scene-census] retired %u shape-valid %u mixed %u failed %u missing %u invalid %u orphan %u max-scenes %u; declines no-words/untracked/bounds/target/ui/kind/cap/mismatch %u/%u/%u/%u/%u/%u/%u/%u; outstanding0/1/2/3 %u/%u/%u/%u; attached add/query/scaled/final %u/%u/%u/%u end-errors %u; sweeps/loads %llu/%llu sweep-us sum/max %llu/%llu max-poll-gap-us %llu; CPU-observed completion bounds, no GPU duration or reorder proof\n",
        g_sc_totals.retired,g_sc_totals.valid,g_sc_totals.mixed,g_sc_totals.failed,g_sc_totals.missing,g_sc_totals.invalid,g_sc_totals.orphans,g_sc_totals.max_scenes,
        g_sc_totals.reasons[1],g_sc_totals.reasons[2],g_sc_totals.reasons[3],g_sc_totals.reasons[4],g_sc_totals.reasons[5],g_sc_totals.reasons[6],g_sc_totals.reasons[7],g_sc_totals.reasons[8],
        g_sc_totals.pending[0],g_sc_totals.pending[1],g_sc_totals.pending[2],g_sc_totals.pending[3],
        g_sc_totals.attached[0],g_sc_totals.attached[1],g_sc_totals.attached[2],g_sc_totals.attached[3],g_sc_totals.end_errors,
        (unsigned long long)g_sc_totals.sweeps,(unsigned long long)g_sc_totals.loads,(unsigned long long)g_sc_totals.sweep_us,(unsigned long long)g_sc_totals.max_sweep_us,(unsigned long long)g_sc_totals.max_poll_gap);
    for(unsigned i=0;i<g_sc_totals.count;i++) {
        xv_sc_shape *s=&g_sc_totals.shape[i];xv_sc_row *r=&g_sc_totals.row[i];
        xv_logf("[scene-census-row] %u target %u %ux%u end %u/%u recorded draw/index/clear/ui %u/%llu/%u/%u load-store %u; n/no-negative/unresolved %u/%u/%u completion-lo-hi-sum-us %llu/%llu gap-lo-hi-sum-us %llu/%llu max-bracket-us %llu notifications add/query/scaled/final %u/%u/%u/%u; samples %u latest-ticket %u cmd %u PS %08X entry %u VS %08X prog/id %08X/%08X route %X reads %X; sampled mesh only, UI/dependencies incomplete\n",
            i,s->target,s->width,s->height,s->command,s->ui,s->draws,(unsigned long long)s->indices,s->clears,s->uis,r->policy,r->valid,r->no_negative,r->coalesced,
            (unsigned long long)r->lower,(unsigned long long)r->upper,(unsigned long long)r->gap_lower,(unsigned long long)r->gap_upper,(unsigned long long)r->bracket,
            r->sources[0],r->sources[1],r->sources[2],r->sources[3],r->sampled,r->sample_ticket,r->sample.command,r->sample.ps_key,r->sample.entry,r->sample.vs_hash,r->sample.program,r->sample.patcher,r->sample.route,r->sample.reads);
    }
    memset(&g_sc_totals,0,sizeof g_sc_totals); /* live packet brackets remain intact */
}
#endif
#endif
