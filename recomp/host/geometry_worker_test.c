/* Actual workers against pthread-backed Vita semaphores, with failure injection. */
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "../../runtime/xv_geometry_worker.c"
static sem_t sems[16];
static unsigned next_sem, live_sems, step, fail_step, next_thread;
static struct { pthread_t thread; SceKernelThreadEntry entry; void *arg; int running; } ts[2];
static unsigned starts[2];
void xv_logf(const char *f, ...) {(void)f;}
void xv_cpu_log_thread(const char *s) {assert(!strcmp(s,"geometry-sort-0")||!strcmp(s,"geometry-sort-1"));}
SceUID sceKernelCreateSema(const char *s,SceUInt a,int initial,int max,SceKernelSemaOptParam *o)
{(void)s;(void)a;(void)o;assert(!initial&&max==1);if(++step==fail_step)return -1;unsigned id=++next_sem;assert(id<16);assert(!sem_init(&sems[id],0,0));live_sems++;return id;}
int sceKernelDeleteSema(SceUID id){assert(!sem_destroy(&sems[id]));live_sems--;return 0;}
int sceKernelSignalSema(SceUID id,int n){assert(n==1);return sem_post(&sems[id]);}
int sceKernelWaitSema(SceUID id,int n,SceUInt *t){(void)t;assert(n==1);return sem_wait(&sems[id]);}
SceUID sceKernelCreateThread(const char *s,SceKernelThreadEntry e,int p,SceSize z,SceUInt a,int mask,const SceKernelThreadOptParam *o)
{(void)s;(void)z;(void)a;(void)o;assert(p==65);assert(mask==SCE_KERNEL_CPU_MASK_USER_0||mask==SCE_KERNEL_CPU_MASK_USER_1);if(++step==fail_step)return -1;unsigned id=next_thread++;assert(id<2);ts[id].entry=e;return 100+id;}
static void *run(void *v){unsigned i=(uintptr_t)v;ts[i].entry(sizeof(void*),&ts[i].arg);return NULL;}
int sceKernelStartThread(SceUID id,SceSize n,void *p){if(++step==fail_step)return -1;unsigned i=id-100;assert(n==sizeof(void*));ts[i].arg=*(void**)p;assert(!pthread_create(&ts[i].thread,NULL,run,(void*)(uintptr_t)i));ts[i].running=1;starts[((sort_worker*)ts[i].arg)->core]++;return 0;}
int sceKernelWaitThreadEnd(SceUID id,int *s,SceUInt *t){(void)s;(void)t;unsigned i=id-100;assert(!pthread_join(ts[i].thread,NULL));ts[i].running=0;return 0;}
int sceKernelDeleteThread(SceUID id){assert(!ts[id-100].running);return 0;}
int sceKernelGetThreadCurrentPriority(void){return 64;}
int sceKernelDelayThread(SceUInt u){usleep(u);return 0;}
SceUInt64 sceKernelGetProcessTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static void reset(void){xv_geometry_worker_shutdown();assert(!live_sems);next_sem=next_thread=step=fail_step=0;}
static int compare(const void *a,const void *b){int32_t x=*(const int32_t*)a,y=*(const int32_t*)b;return(x>y)-(x<y);}
int main(void){
 int32_t *a=malloc(32768*4+64),*b=malloc(32768*4);assert(a&&b);
 unsigned sizes[]={0,1,7,511,512,513,2047,2048,2049,4096,32767,32768},cases=0;
 for(unsigned mode=0;mode<3;mode++){
  char option[2]={(char)('0'+mode),0};setenv("XV_GEOMETRY_WORKER",option,1);
  for(unsigned pass=0;pass<8;pass++)for(unsigned k=0;k<sizeof sizes/sizeof *sizes;k++){
   unsigned n=sizes[k];for(unsigned i=0;i<n;i++)a[i]=b[i]=(int32_t)((i*2654435761u)^(pass*19733u));
   memset((char*)a+n*4,0xA5,64);qsort(b,n,4,compare);xv_geometry_sort_parallel(a,n);
   assert(!memcmp(a,b,n*4));for(unsigned i=0;i<64;i++)assert(((unsigned char*)a)[n*4+i]==0xA5);
   memset(a,0xCC,n*4);cases++; /* caller may immediately reuse every source byte */
  }
  reset();
 }
 assert(starts[0]&&starts[1]);unsetenv("XV_GEOMETRY_WORKER");
 for(unsigned fail=1;fail<=8;fail++){
  fail_step=fail;for(unsigned i=0;i<32768;i++)a[i]=b[i]=(int32_t)(32768-i);
  qsort(b,32768,4,compare);xv_geometry_sort_parallel(a,32768);assert(!memcmp(a,b,32768*4));reset();
 }
 free(a);free(b);printf("geometry workers: %u serial/two/three-way cases, bounds, reuse, eight startup failures and shutdown passed\n",cases);return 0;
}
