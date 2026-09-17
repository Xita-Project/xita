/* Complete production uploader entry cost, including cache lookup/admission,
 * controls, comparison, snapshot and metadata. The CPU->GPU worker is outside
 * this producer call; mocked firmware copies are counted separately. */
#include "../../runtime/xv_vertex_upload.c"
static const void *source_arg;
static unsigned vertices_arg,packed_arg;
char *getenv(const char *s) {(void)s;return NULL;}
void xv_logf(const char *f,...) {(void)f;}
void xv_gpu_flush(const void *p,uint32_t n) {(void)p;(void)n;}
SceUID sceKernelAllocMemBlock(const char *n,SceKernelMemBlockType t,SceSize s,SceKernelAllocMemBlockOpt *o)
{(void)n;(void)t;(void)s;(void)o;return -1;}
int sceKernelGetMemBlockBase(SceUID id,void **p) {(void)id;(void)p;return -1;}
int sceKernelFreeMemBlock(SceUID id) {(void)id;return 0;}
int sceGxmMapMemory(void *p,SceSize n,SceGxmMemoryAttribFlags f) {(void)p;(void)n;(void)f;return -1;}
int xv_upload_worker_submit(void *d,const void *s,unsigned n,uint32_t *t)
{(void)d;(void)s;(void)n;*t=1;return 1;}
void xv_upload_worker_wait(uint32_t t) {(void)t;}
void xv_upload_worker_shutdown(void) {}
/* Hooked at the function entry by the instruction runner. Their time is not
 * represented by the Thumb return instruction. */
void *sceClibMemcpy(void *d,const void *s,unsigned n) {(void)s;(void)n;return d;}
void *sceClibMemset(void *d,int c,unsigned n) {(void)c;(void)n;return d;}
void test_setup(const void *s,void *cpu,void *gpu,unsigned config)
{
    unsigned n=config>>8,packed=(config>>4)&1,cached=config&1,bytes=n*(packed?16:32);
    memset(&pools[0],0,sizeof pools[0]);
    pools[0].cpu=cpu;pools[0].gpu=gpu;pools[0].valid_bytes=bytes;
    pools[0].started=pools[0].asynchronous=1;
    resident_override=compare_override=blocks_override=1;
    copy_override=snapshot_worker_override=0;
    if(cached) {
        unsigned hash=((uintptr_t)s>>4)&(UPLOAD_BUCKETS-1);
        pools[0].bucket[hash]=1;pools[0].count=1;pools[0].used=bytes;
        pools[0].entries[0]=(upload_entry){.source=s,.bytes=bytes};
#if XV_PACKED_VERTEX_LAYOUT
        pools[0].entries[0].layout=packed;
#endif
    }
    source_arg=s;vertices_arg=n;packed_arg=packed;
}
const void *test_call(void)
{
#if XV_PACKED_VERTEX_LAYOUT
    if(packed_arg)return xv_vertex_upload_packed(0,source_arg,vertices_arg);
#endif
    return xv_vertex_upload(0,source_arg,vertices_arg*32);
}
