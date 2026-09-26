/* Private MES(H) files are inputs, never repository fixtures. Reuse the actual
 * production FIFO/uploader plus the established pthread Vita service shim. */
#define XV_VERTEX_UPLOAD_BYTES (8u*1024u*1024u)
#define XV_VERTEX_CAPTURE_BYTES (4u*1024u*1024u)
#define main capture_fixture_main
#include "vertex_capture.c"
#undef main

typedef struct {
 unsigned char *file,*live;
 size_t bytes;
 unsigned vertices,stride,count;
 const unsigned char *indices,*expected;
 xv_vertex_refs refs;
 output out;
} replay_mesh;
static uint64_t replay_ns(void)
{
 struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
 return (uint64_t)ts.tv_sec*1000000000ull+(uint64_t)ts.tv_nsec;
}
static unsigned le32(const unsigned char *p)
{ return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24; }
static unsigned le16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static int load_mesh(const char *path,replay_mesh *m)
{
 enum { PREFIX=32+192*16+18*16 };
 FILE *f=fopen(path,"rb");if(!f)return 0;
 if(fseek(f,0,SEEK_END)){fclose(f);return 0;}
 long size=ftell(f);rewind(f);
 if(size<PREFIX || size>16*1024*1024){fclose(f);return 0;}
 m->file=malloc((size_t)size);if(!m->file){fclose(f);return 0;}
 int ok=fread(m->file,1,(size_t)size,f)==(size_t)size;fclose(f);if(!ok)return 0;
 if(le32(m->file)!=0x4853454d)return 0;
 m->vertices=le32(m->file+12);m->stride=le32(m->file+16);m->count=le32(m->file+20);
 if(!m->vertices || m->vertices>65536 || (m->stride!=16 && m->stride!=32) || !m->count || m->count>2000000)return 0;
 m->bytes=(size_t)m->vertices*m->stride;
 if(PREFIX+m->bytes+(size_t)m->count*2!=(size_t)size)return 0;
 m->expected=m->file+PREFIX;m->indices=m->expected+m->bytes;
 xv_vertex_refs_clear(&m->refs);
 for(unsigned i=0;i<m->count;i++) {
  unsigned idx=le16(m->indices+2*i);if(idx>=m->vertices)return 0;
  xv_vertex_refs_add(&m->refs,(uint16_t)idx);
 }
 m->live=malloc(m->bytes);if(!m->live)return 0;
 memcpy(m->live,m->expected,m->bytes);return 1;
}
static void release_mesh(replay_mesh *m){free(m->live);free(m->file);}
static void abort_batch(replay_mesh *meshes,unsigned n)
{
 if(cap_jobs)join(0);
 for(unsigned i=0;i<n;i++)release_mesh(&meshes[i]);
 cleanup();
}
int main(int argc,char **argv)
{
 if(argc<3){fprintf(stderr,"usage: replay full|sparse|packed mesh.bin ...\n");return 2;}
 int sparse=!strcmp(argv[1],"sparse"),packed=!strcmp(argv[1],"packed");
 if(!sparse&&!packed&&strcmp(argv[1],"full"))return 2;
 owner=pthread_self();setenv("XV_VERTEX_CAPTURE","1",1);setenv("XV_VERTEX_PERSISTENT","0",1);
 setenv("XV_VERTEX_CAPTURE_RETAIN","0",1);setenv("XV_REC_CAPTURE","2",1);
 const char *partial=getenv("XV_CAPTURE_PARTIAL_WAIT");
 if(partial && strcmp(partial,"0") && strcmp(partial,"1"))return 2;
 cap_partial_wait=partial?atoi(partial):1;
 xv_vertex_worker_override(1);xv_vertex_upload_override(1);
 unsigned files=0;uint64_t checked=0,prepare_ns=0;
 int timing=getenv("XV_REPLAY_TIMING") && !strcmp(getenv("XV_REPLAY_TIMING"),"1");
 /* Bounded batches preserve output lifetimes. Batch boundaries are fixture
  * boundaries, NOT reconstructed game frames (mesh files lack that contract). */
 for(int begin=2;begin<argc;) {
  replay_mesh meshes[64]={0};unsigned n=0;
  xv_vertex_capture_begin_slot(0);xv_vertex_upload_reset(0);
  for(;n<64&&begin<argc;n++,begin++) {
   replay_mesh *m=&meshes[n];
   if(!load_mesh(argv[begin],m)){fprintf(stderr,"invalid mesh: %s\n",argv[begin]);abort_batch(meshes,n+1);return 2;}
  }
  uint64_t start=replay_ns();
  for(unsigned j=0;j<n;j++) {
   replay_mesh *m=&meshes[j];
   unsigned pack=packed&&m->stride==32?XV_PACKED_PREFIX16:0;
   if(!capture(0,m->live,(unsigned)m->bytes,m->stride,sparse?&m->refs:NULL,pack,&m->out)) {
    fprintf(stderr,"capture rejected at batch item %u\n",j);abort_batch(meshes,n);return 3;
   }
   if(!timing)memset(m->live,0xa5,m->bytes); /* Caller can mutate after capture returns. */
  }
  join(0);
  prepare_ns+=replay_ns()-start;
  for(unsigned j=0;j<n;j++) {
   replay_mesh *m=&meshes[j];unsigned width=packed&&m->stride==32?16:m->stride;
   if(!m->out.ok||m->out.callbacks!=1){fprintf(stderr,"callback failure\n");return 3;}
   for(unsigned i=0;i<(sparse?m->count:m->vertices);i++) {
    unsigned v=sparse?le16(m->indices+2*i):i;
    if(memcmp(m->expected+(size_t)v*m->stride,m->out.result[0]+(size_t)v*width,width)) {
     fprintf(stderr,"vertex mismatch file %u vertex %u\n",files,v);return 3;
    }
    checked+=width;
   }
   files++;release_mesh(m);
  }
 }
 if(timing)fprintf(stderr,"queue_waits=%u full_drains=%u\n",cap_partial_waits,cap_drains);
 if(timing)fprintf(stderr,"preparation_ns=%llu (capture+join; excludes IO, validation, poisoning; includes first worker startup)\n",(unsigned long long)prepare_ns);
 cleanup();printf("{\"mode\":\"%s\",\"files\":%u,\"checked_bytes\":%llu,\"mismatches\":0}\n",argv[1],files,(unsigned long long)checked);
 return 0;
}
