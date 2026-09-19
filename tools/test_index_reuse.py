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
for name in ('xv_d3d_BeginFrame', 'xv_d3d_Swap'):
    body=source[source.index('void '+name+'(void)'):].split('\n}\n',1)[0]
    assert body.count('index_reuse_begin_frame();')==1, name+' must invalidate GPU pointers'
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
    index_reuse_begin_frame();
    memset(indices+slot*XV_FRAME_INDICES,0xa5,XV_FRAME_INDICES*2u);
    verify_saved();
}
int main(int argc,char **argv)
{
    (void)argv;fail_allocation=argc>1;
    const char *setting=getenv("XV_INDEX_METADATA");
    int metadata=setting ? atoi(setting)==1 : XV_INDEX_METADATA_DEFAULT;
    int on=(getenv("XV_INDEX_REUSE") && atoi(getenv("XV_INDEX_REUSE"))!=0)||metadata;
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
    /* CPU metadata can cross a frame boundary, GPU pointers cannot. Retain
     * older slot snapshots, exhaust the new ring, then retry with fresh space. */
    begin_frame(61);enabled=1;xv_index_cache_reset(g_index_cache);
    const void *prior=capture(inputs[0],256);
    begin_frame(62);
    unsigned uploads=index_metadata_reuploads,full=index_metadata_full;
    g_index_used[62%3]=XV_FRAME_INDICES;p=inputs[0];n=0xdeadbeef;
    assert(!retain_indices(&p,256,&n));
    assert(p==inputs[0] && n==0xdeadbeef && !g_draw_vertex_refs_valid);
    assert(index_metadata_reuploads==uploads);
    assert(index_metadata_full==full+(unsigned)(metadata&&!fail_allocation));
    verify_saved();g_index_used[62%3]=0;
    const void *fresh=capture(inputs[0],256);assert(fresh!=prior);
    assert(index_metadata_reuploads==uploads+(unsigned)(metadata&&!fail_allocation));
    assert((capture(inputs[0],256)==fresh)==(on&&!fail_allocation));
    inputs[0][128]^=0x80u;
    assert(capture(inputs[0],256)!=fresh);verify_saved();
    /* Changed coverage policy must rebuild. The next same-policy frame can
     * reuse bounds alone, without publishing a stale reference bitmap. */
    begin_frame(63);enabled=0;uploads=index_metadata_reuploads;
    capture(inputs[0],256);assert(index_metadata_reuploads==uploads);
    begin_frame(64);capture(inputs[0],256);
    assert(index_metadata_reuploads==uploads+(unsigned)(metadata&&!fail_allocation));
    /* Each benchmark transition discards CPU identities, but never modifies
     * already published index bytes. Restoring -1 respects the startup value. */
    begin_frame(65);enabled=1;
    for(int policy=0;policy<=1;policy++) {
        xv_d3d_index_reuse_override(policy);
        assert(!index_metadata_enabled());
        const void *a=capture(inputs[0],256),*b=capture(inputs[0],256);
        assert((a==b)==(policy&&!fail_allocation));
        xv_d3d_index_reuse_override(0);
        const void *c=capture(inputs[0],256);assert(c!=a && c!=b);
        xv_d3d_index_reuse_override(1);
        const void *d=capture(inputs[0],256);assert(d!=c && d!=a);
        assert((capture(inputs[0],256)==d)==!fail_allocation);
        xv_d3d_index_reuse_override(-1);
        const void *e=capture(inputs[0],256);assert(e!=d);
        assert((capture(inputs[0],256)==e)==(on&&!fail_allocation));
        verify_saved();
    }
    assert(allocation_calls==1);
    unsigned hits=index_reuse_hits;index_reuse_report(60);
    assert(!index_reuse_hits&&!index_reuse_misses&&!index_reuse_ineligible);
    assert(!index_metadata_reuploads&&!index_metadata_full&&!index_metadata_bytes);
    index_reuse_shutdown();assert(!g_index_cache);
    printf("PASS: retained indices/coverage, rewrites, collisions, odd sources, full ring, policy changes, 60 delayed-slot generations; hits=%u allocation-failure=%u\n",hits,fail_allocation);
}
'''
with tempfile.TemporaryDirectory(prefix='xita-index-reuse-') as directory:
    temp=Path(directory);src=temp/'retainer.c';src.write_text(fixture+allocation+retain+checks)
    sdk=Path(os.environ.get('VITASDK',str(Path.home()/'vitasdk')))
    flags=['cc','-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
        '-fno-strict-aliasing','-fsanitize=address,undefined','-I'+str(root),'-I'+str(root/'runtime'),
        '-idirafter',str(sdk/'arm-vita-eabi/include')]
    for default in ('0','1'):
        subprocess.run([*flags,'-DXV_INDEX_METADATA_DEFAULT='+default,str(src),'-o',str(temp/'test')],check=True)
        for policy,metadata,args in [('0',None,[]),('1',None,[]),('0','0',[]),('1','0',[]),('0','1',[]),('1','1',[]),
                                 ('0','2',[]),('1','0',['allocation-failure']),
                                 ('0','1',['allocation-failure'])]:
            env=dict(os.environ,XV_INDEX_REUSE=policy)
            if metadata is None:env.pop('XV_INDEX_METADATA',None)
            else:env['XV_INDEX_METADATA']=metadata
            subprocess.run([str(temp/'test'),*args],env=env,check=True)
