#include "xv_version.h"
/* Private-LAN development control. The network thread never touches Xbox RAM,
 * GXM, settings or saves. It publishes expiring pad input and requests a copy
 * from the existing completed-frame callback. No listener exists by default. */
#include "xv_remote.h"
#include "xv_benchmark.h"
#include "xv_log.h"
#include "xv_update_halo2.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "xv_remote_platform.h"

#define ROOT "ux0:data/xita/"
#define LOAD(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
#define STORE(p,v) __atomic_store_n(p,v,__ATOMIC_RELEASE)
enum { WIDTH=960, HEIGHT=544, REQUEST_MAX=2048, LOG_CHUNK=65536 };
static unsigned running, enabled, frame_count, capture, pad_seq, pad_buttons, pad_axes, pad_deadline;
static uint32_t *screen;
static char key[33];
static unsigned captured_frame;
static unsigned draw_trace_pending;
static unsigned page_census_pending;
static int listener=-1;
static int upload_in_progress;
static int upload_game = -1;
typedef struct {
    int (*begin)(unsigned,const char *,const char *);
    int (*chunk)(unsigned,const void *,unsigned);
    int (*finish)(void);
    int (*request)(int);
    void (*json)(char *,size_t);
} update_ops;
static const update_ops updates[] = {
    {xv_update_begin,xv_update_chunk,xv_update_finish,xv_update_request,xv_update_json},
    {xv_halo2_update_begin,xv_halo2_update_chunk,xv_halo2_update_finish,xv_halo2_update_request,xv_halo2_update_json}
};
static uint64_t awake_until;
static unsigned handoff;
void xv_update_progress(unsigned stage)
{
    if(!xv_updates_requested() || stage<=XV_UPDATE_IDLE || stage>=XV_UPDATE_HANDOFF_COUNT)return;
    unsigned previous=LOAD(&handoff);
    while(previous<stage && !__atomic_compare_exchange_n(&handoff,&previous,stage,0,
          __ATOMIC_RELEASE,__ATOMIC_RELAXED)) {}
}
#ifdef __vita__
static SceUID worker=-1;
static void *net_pool;
static int net_module,net_initialized,ctl_initialized;
#else
static pthread_t worker;
static int thread_started;
#endif

void xv_remote_pad(uint32_t *buttons,uint8_t *lx,uint8_t *ly,uint8_t *rx,uint8_t *ry)
{
    if(!LOAD(&enabled))return;
    /* 0x10000 is SCE_CTRL_INTERCEPTED in the application API: system focus
     * metadata, not a held gameplay button. It can remain set after LoadExec.
     * Keep it intact while injecting only Xita's paired, leased game input.
     * Every reported physical button or stick motion still takes priority. */
    const uint32_t intercepted=0x00010000u;
    if((*buttons & ~intercepted) || *lx<112 || *lx>144 || *ly<112 || *ly>144 ||
       *rx<112 || *rx>144 || *ry<112 || *ry>144)return;
    unsigned seq=LOAD(&pad_seq);if(seq&1)return;
    unsigned axes=LOAD(&pad_axes),buttons_remote=LOAD(&pad_buttons),until=LOAD(&pad_deadline);
    if(seq!=LOAD(&pad_seq) || !until || (int32_t)(until-(uint32_t)remote_now())<=0)return;
    *buttons=(*buttons & intercepted)|buttons_remote;*lx=axes;*ly=axes>>8;*rx=axes>>16;*ry=axes>>24;
}

void xv_remote_frame(const void *pixels,unsigned width,unsigned height,unsigned pitch)
{
    if(!LOAD(&enabled))return;
    unsigned frame=__atomic_add_fetch(&frame_count,1,__ATOMIC_RELAXED);
    unsigned expected=1;
    if(!__atomic_compare_exchange_n(&capture,&expected,2,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))return;
    if(xv_benchmark_status() || !pixels || width!=WIDTH || height!=HEIGHT || pitch<WIDTH) {
        STORE(&capture,4);return;
    }
    for(unsigned y=0;y<HEIGHT;y++)memcpy(screen+y*WIDTH,(const uint32_t *)pixels+y*pitch,WIDTH*4);
    captured_frame=frame;
    STORE(&capture,3); /* Own this immutable copy until the HTTP response ends. */
}

int xv_remote_take_page_census(void)
{
    if(!LOAD(&enabled))return 0;
    return __atomic_exchange_n(&page_census_pending,0,__ATOMIC_ACQ_REL)&&!xv_benchmark_status()&&!xv_benchmark_remote_busy()&&!xv_updates_requested();
}
int xv_remote_take_draw_trace(void)
{
    if(!LOAD(&enabled))return 0;
    unsigned requested=__atomic_exchange_n(&draw_trace_pending,0,__ATOMIC_ACQ_REL);
    return requested && !xv_benchmark_status() && !xv_benchmark_remote_busy() && !xv_updates_requested();
}

static void keep_awake(void)
{
#ifdef __vita__
    static uint64_t next;
    uint64_t now=remote_now();
    /* Unattended gameplay/update leases also keep the display active. An
     * auto-suspend-only tick permits screen-off controller interception. */
    if(now<awake_until && now>=next) {sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);next=now+1000000;}
#endif
}
static int send_all(int s,const void *data,size_t length,uint64_t deadline)
{
    const char *p=data;
    while(length && LOAD(&running) && remote_now()<deadline) {
        int n=remote_send(s,p,length);
        if(n>0) {p+=n;length-=n;} else {remote_sleep(10000);keep_awake();}
    }
    return length?-1:0;
}
static int header(int s,int code,const char *type,size_t length,const char *extra)
{
    /* All responses are bounded below 2 MiB. Avoid %zu: the Vita3K libc
     * formatter does not implement it and can misread the following argument. */
    char h[512];int n=snprintf(h,sizeof h,"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n%s\r\n",
        code,code==200?"OK":code==204?"No Content":"Error",type,(unsigned)length,extra?extra:"");
    return n>0 && (size_t)n<sizeof h ? send_all(s,h,n,remote_now()+2000000) : -1;
}
static void reply(int s,int code,const char *body)
{
    if(!header(s,code,"text/plain",strlen(body),NULL))send_all(s,body,strlen(body),remote_now()+2000000);
}
static int uint_value(const char *s,unsigned maximum,unsigned *value)
{
    if(!*s)return 0;
    unsigned v=0;
    for(;*s;s++) {
        if(*s<'0'||*s>'9')return 0;
        unsigned digit=(unsigned)(*s-'0');
        if(digit>maximum || v>(maximum-digit)/10)return 0;
        v=v*10+digit;
    }
    if(v>maximum)return 0;
    *value=v;return 1;
}
/* Fixed field order, numeric values only: no paths, shell, arbitrary writes or
 * ambiguous duplicate query keys. Reject incomplete/extra values. */
static int pad_query(char *query,unsigned values[6])
{
    static const char *names[]={"buttons=","lx=","ly=","rx=","ry=","ms="};
    static const unsigned maximum[]={0xF3F9,255,255,255,255,2000};
    for(unsigned i=0;i<6;i++) {
        size_t n=strlen(names[i]);if(strncmp(query,names[i],n))return 0;query+=n;
        char *end=strchr(query,'&');if((i<5)!=!!end)return 0;if(end)*end=0;
        if(!uint_value(query,maximum[i],values+i))return 0;
        if(end)query=end+1;
    }
    /* Only ordinary Vita buttons, never PS/power or reserved bits. */
    return !(values[0]&~0xF3F9u);
}
static int authorized_request(char *request,char **method,char **target,unsigned *body_size)
{
    char *line=strstr(request,"\r\n");if(!line)return 0;*line=0;
    char *space=strchr(request,' ');if(!space)return 0;*space=0;*method=request;*target=space+1;
    space=strchr(*target,' ');if(!space || strcmp(space," HTTP/1.1"))return 0;*space=0;
    int auth=0,length=0;
    for(char *p=line+2;*p;) {
        char *end=strstr(p,"\r\n");if(!end)return 0;*end=0;
        char *colon=strchr(p,':');if(!colon)return 0;*colon=0;
        char *value=colon+1;while(*value==' ')value++;
        if(!strcasecmp(p,"Authorization")) {
            if(auth++ || strncmp(value,"Bearer ",7) || strlen(value+7)!=32)return 0;
            unsigned diff=0;for(unsigned i=0;i<32;i++)diff|=(unsigned char)value[i+7]^(unsigned char)key[i];
            if(diff)return 0;
        } else if(!strcasecmp(p,"Content-Length")) {if(length++ || !uint_value(value,XV_UPDATE_CHUNK,body_size))return 0;}
        else if(!strcasecmp(p,"Transfer-Encoding"))return 0;
        p=end+2;
    }
    return auth==1 && (!*body_size || (!strcmp(*method,"POST") && (!strncmp(*target,"/update/chunk?offset=",21) || !strncmp(*target,"/update/halo2/chunk?offset=",27))));
}
static void serve(int s)
{
    char request[REQUEST_MAX];size_t used=0;uint64_t deadline=remote_now()+2000000;
    while(used<sizeof request-1 && LOAD(&running) && remote_now()<deadline) {
        int n=remote_recv(s,request+used,sizeof request-1-used);
        if(!n)return;
        if(n<0) {remote_sleep(10000);continue;}
        used+=n;request[used]=0;
        char *end=strstr(request,"\r\n\r\n");
        if(!end) {if(memchr(request,0,used)) {reply(s,400,"Invalid request\n");return;}continue;}
        size_t head=(size_t)(end+4-request),initial_body=used-head;
        if(memchr(request,0,head)) {reply(s,400,"Invalid request\n");return;}
        /* Leave the final header's CRLF but remove the empty terminator. */
        end[2]=0;
        char *method=NULL,*target=NULL;unsigned body_size=0;
        if(!authorized_request(request,&method,&target,&body_size)) {reply(s,403,"Authentication or framing rejected\n");return;}
        if(initial_body>body_size) {reply(s,400,"Unexpected request body\n");return;}
        unsigned game=0;
        char normalized[REQUEST_MAX];
        if(!strncmp(target,"/update/halo2",13) && (!target[13] || target[13]=='/')) {
            game=1; snprintf(normalized,sizeof normalized,"/update%s",target+13);target=normalized;
        }
        const update_ops *update=&updates[game];
        if(!strncmp(target,"/update",7)) {
            if(strcmp(method,"GET") && ((upload_in_progress && upload_game!=(int)game) || xv_updates_requested())) {
                reply(s,409,"Another update operation is active\n");return;
            }
            if((strcmp(method,"GET")||strcmp(target,"/update"))&&(xv_benchmark_status()||xv_benchmark_remote_busy())) {reply(s,409,"Updates disabled during benchmark\n");return;}
            if(!strcmp(method,"GET")&&!strcmp(target,"/update")) {
                char body[1280];update->json(body,sizeof body);
                size_t n=strlen(body);
                if(n && body[n-1]=='}')snprintf(body+n-1,sizeof body-n+1,",\"handoff\":%u}",
                    xv_updates_requested()?LOAD(&handoff):XV_UPDATE_IDLE);
                /* Read-only queue telemetry remains available throughout the
                 * logger drain. Existing handoff enum values are unchanged. */
                extern void xv_log_get_status(xv_log_status *) __attribute__((weak));
                if(xv_log_get_status) {
                    xv_log_status log; xv_log_get_status(&log); n=strlen(body);
                    if(n && body[n-1]=='}')snprintf(body+n-1,sizeof body-n+1,
                        ",\"log\":{\"state\":%u,\"enabled\":%u,\"transition\":%u,\"error\":%d,\"startup_error\":%d,\"open\":%u,\"queued\":%u,\"accepted\":%llu,\"written\":%llu,\"synced\":%llu,\"failed\":%llu,\"offset\":%u,\"report\":%llu,\"frame\":%u,\"chunk\":%u,\"bytes\":[%llu,%llu,%llu]}}",
                        log.state,log.enabled,log.transition,log.error,log.startup_error,log.open_report,log.queued,(unsigned long long)log.accepted,(unsigned long long)log.written,
                        (unsigned long long)log.synced,(unsigned long long)log.failed_sequence,log.failed_file_offset,
                        (unsigned long long)log.pending_report,log.pending_frame,log.pending_chunk,
                        (unsigned long long)log.accepted_bytes,(unsigned long long)log.written_bytes,(unsigned long long)log.synced_bytes);
                }
                if(!header(s,200,"application/json",strlen(body),NULL))send_all(s,body,strlen(body),remote_now()+2000000);
            } else if(!strcmp(method,"POST")&&!strncmp(target,"/update/begin?size=",19)) {
                char *sha=strstr(target+19,"&sha256="),*abi=sha?strstr(sha+8,"&contract="):NULL;unsigned size;
                if(!sha||!abi) {reply(s,400,"Invalid update manifest\n");return;}
                *sha=0;*abi=0;
                if(!uint_value(target+19,game?XV_HALO2_UPDATE_LIMIT:XV_UPDATE_LIMIT,&size)||update->begin(size,sha+8,abi+10))reply(s,409,"Incompatible package or update unavailable\n");
                else {upload_in_progress=1;upload_game=(int)game;reply(s,204,"");}
            } else if(!strcmp(method,"POST")&&!strncmp(target,"/update/chunk?offset=",21)) {
                unsigned offset;if(!body_size||!uint_value(target+21,game?XV_HALO2_UPDATE_LIMIT:XV_UPDATE_LIMIT,&offset)) {reply(s,400,"Invalid update chunk\n");return;}
                char *chunk=malloc(body_size);if(!chunk) {reply(s,503,"No upload buffer\n");return;}
                memcpy(chunk,request+head,initial_body);size_t have=initial_body;deadline=remote_now()+15000000;
                while(have<body_size&&LOAD(&running)&&remote_now()<deadline) {
                    int got=remote_recv(s,chunk+have,body_size-have);
                    if(!got)break;
                    if(got<0) {remote_sleep(10000);keep_awake();continue;}
                    have+=(unsigned)got;
                }
                int bad=have!=body_size||update->chunk(offset,chunk,body_size);free(chunk);
                reply(s,bad?409:204,bad?"Incomplete chunk or wrong offset\n":"");
            } else if(!strcmp(method,"POST")&&!strcmp(target,"/update/finish")) {
                int bad=update->finish();if(!bad)upload_in_progress=0;reply(s,bad?409:204,bad?"Update verification failed\n":"");
            } else if(!strcmp(method,"POST")&&(!strcmp(target,"/update/apply")||!strcmp(target,"/update/rollback"))) {
                int bad=update->request(!strcmp(target,"/update/rollback"));
                if(!bad)xv_update_progress(XV_UPDATE_REQUESTED);
                reply(s,bad?409:204,bad?"Update not ready\n":"");
            } else reply(s,404,"Unknown update operation\n");
        } else if(!strcmp(method,"POST")&&!strncmp(target,"/env?",5)) {
            /* Set process environment variables before the game starts (diagnostic knobs read at
             * configure time); K=V pairs joined by '&', no decoding. */
            char vars[512];strncpy(vars,target+5,sizeof vars-1);vars[sizeof vars-1]=0;
            unsigned n=0;
            for(char *tok=strtok(vars,"&");tok;tok=strtok(NULL,"&")) {
                char *eq=strchr(tok,'=');if(!eq||eq==tok)continue;*eq=0;
                setenv(tok,eq+1,1);n++;xv_logf("[remote] env %s=%s\n",tok,eq+1);
            }
            reply(s,n?204:400,n?"":"No K=V pairs\n");
        } else if(!strcmp(method,"POST")&&!strcmp(target,"/trace/pages")) {
            /* One-shot per-frame dirty-page census (owner hashes the guest arena at
             * two consecutive Presents and logs changed pages). Diagnostic only. */
            if(upload_in_progress||xv_updates_requested()||xv_benchmark_status()||xv_benchmark_remote_busy()) {
                reply(s,409,"Page census unavailable during update or benchmark\n");return;
            }
            unsigned expected=0;
            if(!__atomic_compare_exchange_n(&page_census_pending,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))
                reply(s,409,"Page census already pending\n");
            else reply(s,204,"");
        } else if(!strcmp(method,"POST")&&!strcmp(target,"/trace/draw")) {
            if(upload_in_progress||xv_updates_requested()||xv_benchmark_status()||xv_benchmark_remote_busy()) {
                reply(s,409,"Draw trace unavailable during update or benchmark\n");return;
            }
            unsigned expected=0;
            if(!__atomic_compare_exchange_n(&draw_trace_pending,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))
                reply(s,409,"Draw trace already pending\n");
            else reply(s,204,""); /* Queued, not proof that a frame was recorded. */
        } else if(!strcmp(method,"POST")&&!strncmp(target,"/benchmark?kind=",16)) {
            static const char *const kinds[]={"object-basis","model-palette","vertex-worker","vertex-references","native-bounds","vertex-copy","draw-scan","flare","resolution","early-visibility","point-math","texture-state","matrix-neon","object-scan","hle-dispatch","flare-query-overlap","guest-affinity","snapshot-worker","guest-phases","prep-bundle","object-jobs","vertex-prepare","depth-prepare","object-math","object-lock","object-wait","object-point","model-hierarchy","object-quat","blend-replace","index-reuse","object-pose","material-packet","polygon-edge","log-writer","clip-region"};
            unsigned kind=0;
            for(unsigned i=0;i<sizeof kinds/sizeof *kinds;i++)if(!strcmp(target+16,kinds[i]))kind=i+1;
            if(!strcmp(target+16,"diagnostic-shot"))kind=XV_BENCH_DIAGNOSTIC_POLL;
            if(!strcmp(target+16,"diagnostic-hist"))kind=XV_BENCH_DIAGNOSTIC_HIST;
            if(!strcmp(target+16,"depth-store"))kind=XV_BENCH_DEPTH_STORE;
            if(!strcmp(target+16,"vertex-blocks"))kind=XV_BENCH_VERTEX_BLOCKS;
            if(!strcmp(target+16,"object-holds"))kind=XV_BENCH_OBJECT_HOLDS;
            if(!strcmp(target+16,"object-collect"))kind=XV_BENCH_OBJECT_COLLECT;
            if(!strcmp(target+16,"query-boundary"))kind=XV_BENCH_QUERY_BOUNDARY;
            if(!strcmp(target+16,"light-census"))kind=XV_BENCH_LIGHT_CENSUS;
            if(!kind)reply(s,400,"Unknown benchmark kind\n");
            else if(upload_in_progress||xv_updates_requested()||LOAD(&draw_trace_pending)||xv_benchmark_remote_request(kind))reply(s,409,"Benchmark unavailable: enter first-person gameplay and finish any active operation\n");
            else reply(s,204,"");
        } else if(!strcmp(method,"GET")&&!strcmp(target,"/status")) {
            { extern void xv_render_view_watchdog(void) __attribute__((weak)); if(xv_render_view_watchdog)xv_render_view_watchdog(); }   /* un-freeze a scene stuck on the render view */
            uint64_t now=remote_now();
            char body[320];snprintf(body,sizeof body,"{\"protocol\":1,\"build\":\"%s\",\"version\":\"%s\",\"revision\":\"%s\",\"frames\":%u,\"benchmark\":%u,\"awake_seconds\":%llu}\n",
                XV_BUILD_LABEL,XV_BUILD_VERSION,XV_BUILD_REVISION,LOAD(&frame_count),xv_benchmark_status(),
                (unsigned long long)(awake_until>now?(awake_until-now)/1000000:0));
            if(!header(s,200,"application/json",strlen(body),NULL))send_all(s,body,strlen(body),remote_now()+2000000);
        } else if(!strcmp(method,"POST")&&!strncmp(target,"/pad?",5)) {
            unsigned v[6];if(!pad_query(target+5,v)) {reply(s,400,"Invalid pad command\n");return;}
            __atomic_add_fetch(&pad_seq,1,__ATOMIC_ACQ_REL);
            STORE(&pad_buttons,v[0]);STORE(&pad_axes,v[1]|v[2]<<8|v[3]<<16|v[4]<<24);
            STORE(&pad_deadline,v[5]?(uint32_t)(remote_now()+v[5]*1000u):0);
            __atomic_add_fetch(&pad_seq,1,__ATOMIC_RELEASE);
            reply(s,204,"");
        } else if(!strcmp(method,"POST")&&!strncmp(target,"/lease?seconds=",15)) {
            unsigned seconds;if(!uint_value(target+15,3600,&seconds)) {reply(s,400,"Lease limit is 3600 seconds\n");return;}
            awake_until=remote_now()+(uint64_t)seconds*1000000;keep_awake();reply(s,204,"");
        } else if(!strcmp(method,"GET")&&!strcmp(target,"/screen")) {
            if(xv_benchmark_status()||xv_benchmark_remote_busy()) {reply(s,409,"Capture disabled during benchmark\n");return;}
            unsigned expected=0;
            if(!__atomic_compare_exchange_n(&capture,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) {reply(s,409,"Capture pending\n");return;}
            deadline=remote_now()+3000000;
            while(LOAD(&running)&&remote_now()<deadline&&LOAD(&capture)<3)remote_sleep(10000);
            unsigned state=LOAD(&capture);
            if(state==3) {
                char ppm[40],extra[80];int n=snprintf(ppm,sizeof ppm,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
                snprintf(extra,sizeof extra,"X-Xita-Frame: %u\r\n",captured_frame);
                deadline=remote_now()+15000000;
                if(!header(s,200,"image/x-portable-pixmap",n+WIDTH*HEIGHT*3,extra) && !send_all(s,ppm,n,deadline)) {
                    unsigned char row[WIDTH*3];
                    for(unsigned y=0;y<HEIGHT;y++) {
                        for(unsigned x=0;x<WIDTH;x++) {uint32_t p=screen[y*WIDTH+x];row[x*3]=p;row[x*3+1]=p>>8;row[x*3+2]=p>>16;}
                        if(send_all(s,row,sizeof row,deadline))break;
                    }
                }
                STORE(&capture,0);
            } else if(state==4) {STORE(&capture,0);reply(s,409,"Frame unavailable during benchmark\n");}
            else {
                expected=1;__atomic_compare_exchange_n(&capture,&expected,0,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED);
                /* If already copying, wait for that finite memcpy before releasing ownership. */
                while(LOAD(&capture)==2)remote_sleep(1000);
                STORE(&capture,0);reply(s,504,"No completed display frame\n");
            }
        } else if(!strcmp(method,"GET")&&(!strncmp(target,"/log?offset=",12)||!strncmp(target,"/launcher-log?offset=",21)||!strncmp(target,"/halo2-log?offset=",18)||!strncmp(target,"/log/",5))) {
            if(xv_benchmark_status()||xv_benchmark_remote_busy()) {reply(s,409,"Bulk log reads disabled during benchmark\n");return;}
            int launcher=!strncmp(target,"/launcher-log?offset=",21);
            int halo2=!strncmp(target,"/halo2-log?offset=",18);
            unsigned run=0;
            const char *digits=target+(halo2?18:launcher?21:12);
            if(!strncmp(target,"/log/",5)) {
                if(target[5]<'1'||target[5]>'3'||strncmp(target+6,"?offset=",8)) {
                    reply(s,400,"Invalid previous log\n");return;
                }
                run=(unsigned)(target[5]-'0');digits=target+14;
            }
            unsigned offset;if(!uint_value(digits,0x7fffffffu,&offset)) {reply(s,400,"Invalid log offset\n");return;}
            /* Match xv_log.c's fixed rotation slots. No client-supplied path
             * enters fopen, and reading evidence never rotates or removes it. */
            static const char *const logs[]={ROOT "xita.log",ROOT "xita.1.log",ROOT "xita.2.log",ROOT "xita.3.log"};
            FILE *f=fopen(halo2?"ux0:data/xita-halo2/boot.log":launcher?ROOT "update/launcher.log":logs[run],"rb");if(!f) {reply(s,404,"Log unavailable\n");return;}
            if(fseek(f,0,SEEK_END)) {fclose(f);reply(s,500,"Log seek failed\n");return;}
            long size=ftell(f);
            if(size<0 || offset>(unsigned long)size || fseek(f,offset,SEEK_SET)) {fclose(f);reply(s,416,"Log offset unavailable\n");return;}
            char *chunk=malloc(LOG_CHUNK);if(!chunk) {fclose(f);reply(s,503,"No log buffer\n");return;}
            size_t amount=(unsigned long)size-offset;if(amount>LOG_CHUNK)amount=LOG_CHUNK;
            size_t got=fread(chunk,1,amount,f);int failed=ferror(f);fclose(f);
            if(failed)reply(s,500,"Log read failed\n");
            else {char extra[80];snprintf(extra,sizeof extra,"X-Log-Size: %ld\r\nX-Log-Run: %u\r\n",size,run);
                if(!header(s,200,"text/plain",got,extra))send_all(s,chunk,got,remote_now()+5000000);}
            free(chunk);
        } else reply(s,404,"Unknown operation\n");
        return;
    }
    reply(s,408,"Request incomplete or too large\n");
}

REMOTE_THREAD(server)
{
#ifdef __vita__
    (void)argc;
#endif
    (void)argv;
    while(LOAD(&running)) {
        keep_awake();int s=remote_accept(listener);
        if(s<0) {remote_sleep(100000);continue;}
        if(remote_nonblock(s)>=0)serve(s);
        remote_close(s);
    }
    return 0;
}
int xv_remote_ready(void) {return LOAD(&enabled)!=0;}
void xv_remote_stop(void)
{
    STORE(&enabled,0);STORE(&running,0);STORE(&pad_deadline,0);
#ifdef __vita__
    if(worker>=0) {sceKernelWaitThreadEnd(worker,NULL,NULL);sceKernelDeleteThread(worker);worker=-1;}
#else
    if(thread_started) {pthread_join(worker,NULL);thread_started=0;}
#endif
    if(listener>=0) {remote_close(listener);listener=-1;}
    /* A callback that claimed the request owns screen until it publishes. */
    while(LOAD(&capture)==2)remote_sleep(1000);
    xv_update_close();xv_halo2_update_close();
    upload_in_progress=0;
    free(screen);screen=NULL;STORE(&capture,0);STORE(&draw_trace_pending,0);memset(key,0,sizeof key);
#ifdef __vita__
    if(ctl_initialized) {sceNetCtlTerm();ctl_initialized=0;}
    if(net_initialized) {sceNetTerm();net_initialized=0;}
    if(net_module) {sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);net_module=0;}
    free(net_pool);net_pool=NULL;
#endif
}
void xv_remote_start(void)
{
    const char *e=getenv("XV_REMOTE_TEST");if(!e||strcmp(e,"1"))return;
    e=getenv("XV_NET_ADHOC");if(e&&atoi(e)==1) {xv_logf("[remote] disabled: ad hoc owns networking\n");return;}
    unsigned port=8080;e=getenv("XV_REMOTE_PORT");
    if(e&&(!uint_value(e,65535,&port)||port<1024)) {xv_logf("[remote] invalid port\n");return;}
    FILE *f=fopen(ROOT "remote.key","rb");if(!f) {xv_logf("[remote] missing remote.key; service disabled\n");return;}
    char buf[36];size_t n=fread(buf,1,sizeof buf,f);int bad=ferror(f);fclose(f);
    while(n&&(buf[n-1]=='\n'||buf[n-1]=='\r'))n--;
    if(bad||n!=32) {xv_logf("[remote] invalid key length; service disabled\n");return;}
    for(unsigned i=0;i<32;i++)if(!isxdigit((unsigned char)buf[i])) {xv_logf("[remote] invalid key; service disabled\n");return;}
    memcpy(key,buf,32);key[32]=0;
    screen=malloc(WIDTH*HEIGHT*4);if(!screen)goto failed;
#ifdef __vita__
    if(sceSysmoduleLoadModule(SCE_SYSMODULE_NET)<0)goto failed;
    net_module=1;
    net_pool=malloc(1024*1024);if(!net_pool)goto failed;
    SceNetInitParam np={.memory=net_pool,.size=1024*1024,.flags=0};
    if(sceNetInit(&np)<0)goto failed;
    net_initialized=1;
    if(sceNetCtlInit()<0)goto failed;
    ctl_initialized=1;
#endif
    listener=remote_listen(port);if(listener<0)goto failed;
    STORE(&running,1);STORE(&enabled,1);
#ifdef __vita__
    worker=sceKernelCreateThread("xv_remote_test",server,0x10000110,64*1024,0,SCE_KERNEL_CPU_MASK_USER_0,NULL);
    if(worker<0)goto failed;
    if(sceKernelStartThread(worker,0,NULL)<0) {sceKernelDeleteThread(worker);worker=-1;goto failed;}
    SceNetCtlInfo info={0};
    if(sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS,&info)>=0)
        xv_logf("[remote] test service %s:%u; authenticated controls/captures/logs and staged executable updates\n",info.ip_address,port);
    else xv_logf("[remote] test service port %u; waiting for configured Wi-Fi\n",port);
#else
    if(pthread_create(&worker,NULL,server,NULL))goto failed;
    thread_started=1;
#endif
    return;
failed:
    xv_logf("[remote] startup failed; game continues without remote testing\n");xv_remote_stop();
}
