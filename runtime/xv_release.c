#include "xv_release.h"
#include <stdio.h>
#if XV_DEVELOPER_BUILD
int xv_release_action(int a) {(void)a;return -1;}
int xv_release_busy(void) {return 0;}
void xv_release_status(char *p,unsigned n) {snprintf(p,n,"Developer updater");}
void xv_release_detail(char *p,unsigned n) {if(n)*p=0;}
#else
#include "xv_release_manifest.h"
#include "xv_update.h"
#include "xv_version.h"
#include <curl/curl.h>
#ifdef XV_RELEASE_HOST_TEST
#include "../tools/tests/release_platform.h"
#else
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/rng.h>
#include <mbedtls/platform_time.h>
#endif
#ifndef XV_RELEASE_HOST_TEST
mbedtls_ms_time_t mbedtls_ms_time(void) {return (mbedtls_ms_time_t)(sceKernelGetProcessTimeWide()/1000);}
/* Vita supplies cryptographic entropy; never fall back to clocks or rand(). */
int mbedtls_hardware_poll(void *unused,unsigned char *out,size_t length,size_t *olen)
{
    (void)unused;*olen=0;
    while(*olen<length) {
        size_t n=length-*olen;if(n>64)n=64;
        if(sceKernelGetRandomNumber(out+*olen,(SceSize)n)<0)return -1;
        *olen+=n;
    }
    return 0;
}
#endif
#define RELEASES "https://github.com/Xita-Project/xita/releases/"
#define LOAD(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
#define STORE(p,v) __atomic_store_n(p,v,__ATOMIC_RELEASE)
enum {IDLE,CHECKING,READY,DOWNLOADING,STAGED,FAILED,CURRENT,INCOMPATIBLE};
static unsigned state, downloaded;
static SceUID worker=-1;
static xv_release_manifest release;
static char error_text[96];
static unsigned operation;
static char manifest_data[1024];static size_t manifest_size;
static size_t metadata(void *p,size_t a,size_t b,void *unused)
{
    (void)unused;if(a && b>sizeof manifest_data/a)return 0;
    size_t n=a*b;if(n>sizeof manifest_data-1-manifest_size)return 0;
    memcpy(manifest_data+manifest_size,p,n);manifest_size+=n;return n;
}
static size_t payload(void *p,size_t a,size_t b,void *unused)
{
    (void)unused;if(a && b>XV_UPDATE_CHUNK/a)return 0;
    unsigned n=(unsigned)(a*b),offset=LOAD(&downloaded);
    if(n>release.size-offset||xv_update_chunk(offset,p,n))return 0;
    STORE(&downloaded,offset+n);sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);return n;
}
static int fetch(const char *url,int binary)
{
    CURL *c=curl_easy_init();if(!c)return -1;
    /* Never consult user proxy/CA overrides, send credentials, or permit HTTP/file redirects. */
    CURLcode rc=CURLE_OK;
#define OPT(k,v) do { if(rc==CURLE_OK)rc=curl_easy_setopt(c,k,v); } while(0)
    OPT(CURLOPT_URL,url);OPT(CURLOPT_PROTOCOLS_STR,"https");OPT(CURLOPT_REDIR_PROTOCOLS_STR,"https");
    OPT(CURLOPT_FOLLOWLOCATION,1L);OPT(CURLOPT_MAXREDIRS,5L);
    OPT(CURLOPT_SSL_VERIFYPEER,1L);OPT(CURLOPT_SSL_VERIFYHOST,2L);
    OPT(CURLOPT_CAINFO,"app0:release-ca.pem");OPT(CURLOPT_PROXY,"");
    OPT(CURLOPT_CONNECTTIMEOUT,20L);OPT(CURLOPT_TIMEOUT,binary?900L:60L);
    OPT(CURLOPT_LOW_SPEED_LIMIT,1024L);OPT(CURLOPT_LOW_SPEED_TIME,30L);
    OPT(CURLOPT_NOSIGNAL,1L);OPT(CURLOPT_FAILONERROR,1L);
    OPT(CURLOPT_USERAGENT,"Xita-release-updater/1");
    OPT(CURLOPT_WRITEFUNCTION,binary?payload:metadata);
    if(rc==CURLE_OK)rc=curl_easy_perform(c);
    long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);curl_easy_cleanup(c);
    if(rc||status!=200) {
        snprintf(error_text,sizeof error_text,status==404 ? "No public update available for this release." : "Download failed. Check Wi-Fi and system date.");return -1;
    }
    return 0;
#undef OPT
}
static int installed_contract(void)
{
    char contract[67]={0};FILE *f=fopen("app0:update-contract.txt","rb");if(!f)return 0;
    size_t n=fread(contract,1,sizeof contract-1,f);int bad=ferror(f);fclose(f);
    return !bad&&n==65&&contract[64]=='\n'&&!memcmp(contract,release.contract,64);
}
static int run(unsigned args,void *argp)
{
    (void)args;(void)argp;void *pool=NULL;int net=0,ctl=0,module=0,curl=0;unsigned result=FAILED;
    snprintf(error_text,sizeof error_text,"Could not initialize networking.");
    /* No guest/network transport is running while the dashboard owns this job. */
    if(sceSysmoduleLoadModule(SCE_SYSMODULE_NET)<0)goto done;
    module=1;
    pool=malloc(1024*1024);if(!pool)goto done;
    SceNetInitParam np={.memory=pool,.size=1024*1024,.flags=0};
    if(sceNetInit(&np)<0)goto done;
    net=1;
    if(sceNetCtlInit()<0)goto done;
    ctl=1;
    if(curl_global_init(CURL_GLOBAL_DEFAULT))goto done;
    curl=1;
    if(operation==0) {
        manifest_size=0;
        if(fetch(RELEASES "latest/download/xita-update.txt",0))goto done;
        if(xv_release_parse(manifest_data,manifest_size,&release)) {snprintf(error_text,sizeof error_text,"Invalid release metadata. Nothing installed.");goto done;}
        result=!installed_contract()?INCOMPATIBLE:!strcmp(release.version,XV_BUILD_VERSION)?CURRENT:READY;
    } else {
        char url[256];snprintf(url,sizeof url,RELEASES "download/%s/xita-runtime.self",release.tag);
        if(xv_update_begin(release.size,release.sha,release.contract)) {snprintf(error_text,sizeof error_text,"Cannot stage this update. Check free space.");goto done;}
        STORE(&downloaded,0);
        if(fetch(url,1)||LOAD(&downloaded)!=release.size||xv_update_finish()) {
            xv_update_close();snprintf(error_text,sizeof error_text,"Transfer or verification failed. Check again to retry.");goto done;
        }
        result=STAGED;
    }
done:
    if(curl)curl_global_cleanup();
    if(ctl)sceNetCtlTerm();
    if(net)sceNetTerm();
    free(pool);
    if(module)sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    STORE(&state,result);return 0;
}
int xv_release_busy(void) {unsigned s=LOAD(&state);return s==CHECKING||s==DOWNLOADING;}
int xv_release_action(int action)
{
    if(xv_release_busy())return -1;
    if(worker>=0) {sceKernelWaitThreadEnd(worker,NULL,NULL);sceKernelDeleteThread(worker);worker=-1;}
    if(action==2)return LOAD(&state)==STAGED?xv_update_request(0):-1;
    if(action==3)return xv_update_request(1);
    if(action!=0 && !(action==1&&LOAD(&state)==READY))return -1;
    const char *adhoc=getenv("XV_NET_ADHOC");if(adhoc&&atoi(adhoc)==1) {
        snprintf(error_text,sizeof error_text,"Disable ad hoc mode and relaunch to update.");STORE(&state,FAILED);return -1;
    }
    operation=(unsigned)action;STORE(&downloaded,0);STORE(&state,action?DOWNLOADING:CHECKING);
    worker=sceKernelCreateThread("xita_release",run,0x10000100,256*1024,0,SCE_KERNEL_CPU_MASK_USER_ALL,NULL);
    if(worker<0||sceKernelStartThread(worker,0,NULL)<0) {
        if(worker>=0)sceKernelDeleteThread(worker);
        worker=-1;
        snprintf(error_text,sizeof error_text,"Could not start updater.");STORE(&state,FAILED);return -1;
    }
    return 0;
}
void xv_release_status(char *out,unsigned size)
{
    switch(LOAD(&state)) {
    case CHECKING:snprintf(out,size,"Checking GitHub Releases...");break;
    case READY:snprintf(out,size,"Available: %s",release.version);break;
    case DOWNLOADING:snprintf(out,size,"Downloading: %u%%",(unsigned)((unsigned long long)LOAD(&downloaded)*100u/release.size));break;
    case STAGED:snprintf(out,size,"Verified: %s. Select Install to restart.",release.version);break;
    case CURRENT:snprintf(out,size,"You have the current release.");break;
    case INCOMPATIBLE:snprintf(out,size,"%s needs a full VPK install in VitaShell.",release.version);break;
    case FAILED:snprintf(out,size,"%s",error_text);break;
    default:snprintf(out,size,"Check for a published Xita release.");break;
    }
}
void xv_release_detail(char *out,unsigned size)
{
    unsigned s=LOAD(&state);
    snprintf(out,size,"%s",s==READY||s==STAGED||s==CURRENT||s==INCOMPATIBLE?release.notes:"Updates preserve game files, saves and settings.");
}
#endif
