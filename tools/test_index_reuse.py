#!/usr/bin/env python3
"""Exercise the production index retainer with source rewrites and delayed slots."""
import ast
import os
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
tree=ast.parse((root/'tools/test_vertex_references.py').read_text())
fixture=next(ast.literal_eval(n.value) for n in tree.body
             if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='fixture' for t in n.targets))
fixture=fixture.replace('#define XV_FRAME_INDICES 2048','#define XV_FRAME_INDICES 262144')
source=(root/'runtime/xv_d3d.c').read_text()
retain=source[source.index('static unsigned index_bounds('):source.index('static uint32_t geometry_hash(')]
allocation=r'''
static unsigned allocation_calls, fail_allocation;
static void *test_allocate(size_t bytes)
{ allocation_calls++;return fail_allocation?NULL:malloc(bytes); }
#define malloc test_allocate
'''
checks=r'''
#undef malloc
static uint16_t inputs[8][8193];
typedef struct {const uint16_t *gpu;unsigned count;uint16_t expected[8192];} saved_draw;
static saved_draw saved[3][80];
static unsigned saved_count[3], random_state=0x732104ab;
static unsigned next_random(void)
{random_state=random_state*1664525u+1013904223u;return random_state;}
static void verify_saved(void)
{
    for(unsigned s=0;s<3;s++)for(unsigned i=0;i<saved_count[s];i++)
        assert(!memcmp(saved[s][i].gpu,saved[s][i].expected,saved[s][i].count*2u));
}
static const void *capture(const void *source,unsigned count)
{
    unsigned slot=g_build_frame%3,n=0;const void *p=source;
    assert(saved_count[slot]<80 && count<=8192);
    saved_draw *d=&saved[slot][saved_count[slot]++];
    memcpy(d->expected,source,count*2u);d->count=count;
    assert(retain_indices(&p,count,&n));d->gpu=p;
    assert((uintptr_t)p>=(uintptr_t)(indices+slot*XV_FRAME_INDICES));
    assert((uintptr_t)p+count*2u<=(uintptr_t)(indices+(slot+1)*XV_FRAME_INDICES));
    unsigned maximum=0,groups=0;uint32_t bits[256]={0};
    for(unsigned i=0;i<count;i++) {
        uint16_t value=d->expected[i];if(value>maximum)maximum=value;
        bits[value/256u]|=1u<<((value/8u)%32u);
    }
    for(unsigned i=0;i<8192;i++)groups+=(bits[i/32u]>>(i%32u))&1u;
    assert(n==maximum+1u && g_draw_vertex_refs_valid==enabled);
    if(enabled)assert(g_draw_vertex_refs.vertices==n && g_draw_vertex_refs.groups==groups &&
                      !memcmp(bits,g_draw_vertex_refs.bits,sizeof bits));
    verify_saved();return p;
}
static void begin_frame(unsigned frame)
{
    /* Only this slot retires; the preceding two GPU generations remain live. */
    verify_saved();g_build_frame=frame;unsigned slot=frame%3;
    saved_count[slot]=0;g_index_used[slot]=0;g_index_requested[slot]=0;
    xv_index_cache_reset(g_index_cache);
    memset(indices+slot*XV_FRAME_INDICES,0xa5,XV_FRAME_INDICES*2u);
    verify_saved();
}
int main(int argc,char **argv)
{
    (void)argv;fail_allocation=argc>1;
    int on=getenv("XV_INDEX_REUSE") && atoi(getenv("XV_INDEX_REUSE"))!=0;
    enabled=1;scan=1;
    for(unsigned j=0;j<8;j++)for(unsigned i=0;i<8193;i++)inputs[j][i]=(uint16_t)next_random();
    begin_frame(0);
    const void *first=capture(inputs[0],256),*repeat=capture(inputs[0],256);
    assert((first==repeat)==(on&&!fail_allocation));
    unsigned before=g_index_used[0];
    /* An unchanged retained draw remains valid even when append space is full. */
    g_index_used[0]=XV_FRAME_INDICES;const void *p=inputs[0];unsigned n;
    assert(retain_indices(&p,256,&n)==(on&&!fail_allocation));
    inputs[0][255]^=0x0100u;p=inputs[0];assert(!retain_indices(&p,256,&n));
    g_index_used[0]=before;const void *changed=capture(inputs[0],256);assert(changed!=first);
    enabled=0;assert(capture(inputs[0],256)!=changed);enabled=1;
    capture(inputs[0],256);
    /* Find and replace a direct-map collision; previously retained GPU bytes
     * must not alias the overwritten CPU comparison mirror. */
    if(on&&!fail_allocation) {
        xv_index_cache_entry *e=xv_index_cache_select(g_index_cache,inputs[0],256);
        const void *collision=NULL;
        for(unsigned i=0;i<7000;i++)if(xv_index_cache_select(g_index_cache,inputs[1]+i,256)==e) {
            collision=inputs[1]+i;break;
        }
        assert(collision);capture(collision,256);capture(inputs[0],256);
    }
    unsigned counts[]={1,63,64,65,255,256,257,4095,4096,4097,8192};
    for(unsigned frame=1;frame<=60;frame++) {
        begin_frame(frame);
        for(unsigned draw=0;draw<48;draw++) {
            unsigned which=draw%8,count=counts[(draw/2)%11];
            enabled=(draw/12)%2;scan=(draw/6)%2;
            /* Include odd source addresses, repeated draws, same-pointer
             * rewrites, arbitrary maxima and scalar/NEON policy changes. */
            uint8_t *source=(uint8_t*)inputs[which]+(draw%2);
            if(draw%3==0)source[(next_random()%(count*2u))]^=0x80u;
            capture(source,count);
            if(draw<16 && count>=64 && count<=4096)capture(source,count);
        }
    }
    verify_saved();
    assert(scan_reference_calls>0 && scan_reference_fast==0); /* host scalar fallback */
    if(on&&!fail_allocation)assert(index_reuse_hits>0 && index_reuse_misses>0 && index_reuse_ineligible>0);
    else assert(!index_reuse_hits && !index_reuse_misses);
    assert(allocation_calls==(unsigned)on);
    unsigned hits=index_reuse_hits;index_reuse_report(60);
    assert(!index_reuse_hits&&!index_reuse_misses&&!index_reuse_ineligible);
    index_reuse_shutdown();assert(!g_index_cache);
    printf("PASS: retained indices/coverage, rewrites, collisions, odd sources, full ring, policy changes, 60 delayed-slot generations; hits=%u allocation-failure=%u\n",hits,fail_allocation);
}
'''
with tempfile.TemporaryDirectory(prefix='xita-index-reuse-') as directory:
    temp=Path(directory);src=temp/'retainer.c';src.write_text(fixture+allocation+retain+checks)
    sdk=Path(os.environ.get('VITASDK',str(Path.home()/'vitasdk')))
    subprocess.run(['cc','-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
        '-fno-strict-aliasing','-fsanitize=address,undefined','-I'+str(root),'-I'+str(root/'runtime'),
        '-idirafter',str(sdk/'arm-vita-eabi/include'),str(src),'-o',str(temp/'test')],check=True)
    for policy,args in [('0',[]),('1',[]),('1',['allocation-failure'])]:
        subprocess.run([str(temp/'test'),*args],env=dict(os.environ,XV_INDEX_REUSE=policy),check=True)
