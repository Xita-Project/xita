#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "release_platform.h"
#include "../../runtime/xv_release.h"
#include "../../runtime/xv_sha256.h"
#include "../../runtime/xv_release_manifest.h"
static int (*job)(unsigned,void *);
static size_t (*writer)(void *,size_t,size_t,void *);
static int fail_http,corrupt,requested=-1,staging,finished,network;
static unsigned received;
static char metadata_text[1024],url[256],sha[65];static unsigned char body[4096];
int sceSysmoduleLoadModule(int a){(void)a;return 0;} int sceSysmoduleUnloadModule(int a){(void)a;return 0;}
int sceNetInit(SceNetInitParam *p){assert(p->memory);assert(!network);network=1;return 0;}
int sceNetTerm(void){assert(network);network=0;return 0;}
int sceNetCtlInit(void){return 0;}int sceNetCtlTerm(void){return 0;}int sceKernelPowerTick(int a){(void)a;return 0;}
int sceKernelCreateThread(const char *n,int (*fn)(unsigned,void *),int p,unsigned z,int a,int c,void *o){(void)n;(void)p;(void)z;(void)a;(void)c;(void)o;job=fn;return 1;}
int sceKernelStartThread(int a,unsigned b,void *c){(void)a;(void)b;(void)c;return 0;}
int sceKernelWaitThreadEnd(int a,void *b,void *c){(void)a;(void)b;(void)c;assert(!xv_release_busy());return 0;}
int sceKernelDeleteThread(int a){(void)a;return 0;}
CURLcode curl_global_init(long f){(void)f;return CURLE_OK;}void curl_global_cleanup(void){}
CURL *curl_easy_init(void){return (CURL *)1;}void curl_easy_cleanup(CURL *p){(void)p;}
/* curl headers provide type-check macros: define callable symbols explicitly. */
#undef curl_easy_setopt
#undef curl_easy_getinfo
CURLcode curl_easy_setopt(CURL *c,CURLoption o,...){
 (void)c;va_list a;va_start(a,o);
 switch(o){
 case CURLOPT_URL:snprintf(url,sizeof url,"%s",va_arg(a,const char *));break;
 case CURLOPT_WRITEFUNCTION:writer=va_arg(a,size_t (*)(void *,size_t,size_t,void *));break;
 case CURLOPT_SSL_VERIFYPEER:assert(va_arg(a,long)==1);break;
 case CURLOPT_SSL_VERIFYHOST:assert(va_arg(a,long)==2);break;
 case CURLOPT_PROTOCOLS_STR:case CURLOPT_REDIR_PROTOCOLS_STR:assert(!strcmp(va_arg(a,const char *),"https"));break;
 case CURLOPT_CAINFO:assert(!strcmp(va_arg(a,const char *),"app0:release-ca.pem"));break;
 default:break;
 }va_end(a);return CURLE_OK;
}
CURLcode curl_easy_getinfo(CURL *c,CURLINFO i,...){(void)c;(void)i;va_list a;va_start(a,i);*va_arg(a,long *)=fail_http?404:200;va_end(a);return CURLE_OK;}
CURLcode curl_easy_perform(CURL *c){
 (void)c;if(fail_http)return CURLE_HTTP_RETURNED_ERROR;
 if(strstr(url,"latest/download/xita-update.txt"))return writer(metadata_text,1,strlen(metadata_text),NULL)==strlen(metadata_text)?CURLE_OK:CURLE_WRITE_ERROR;
 assert(!strcmp(url,"https://github.com/Xita-Project/xita/releases/download/v-test/xita-runtime.self"));
 if(corrupt)body[80]^=1;
 size_t n=writer(body,1,sizeof body,NULL);
 if(corrupt)body[80]^=1;
 return n==sizeof body?CURLE_OK:CURLE_WRITE_ERROR;
}
int xv_update_begin(unsigned n,const char *s,const char *c){assert(n==sizeof body && strlen(c)==64);strcpy(sha,s);received=0;staging=1;finished=0;return 0;}
static xv_sha256 hash;
int xv_update_chunk(unsigned o,const void *p,unsigned n){assert(staging && o==received);if(!o)xv_sha256_init(&hash);xv_sha256_add(&hash,p,n);received+=n;return 0;}
int xv_update_finish(void){char actual[65];xv_sha256_end(&hash,actual);if(strcmp(actual,sha))return -1;finished=1;return 0;}
void xv_update_close(void){staging=0;}
int xv_update_request(int rollback){if(!rollback)assert(finished);requested=rollback;return 0;}
static void work(void){assert(xv_release_busy());assert(xv_release_action(0)<0);job(0,NULL);assert(!network&&!xv_release_busy());}
int main(void){
 memset(body,1,sizeof body);memcpy(body,"SCE\0",4);char digest[65];xv_sha256 h;xv_sha256_init(&h);xv_sha256_add(&h,body,sizeof body);xv_sha256_end(&h,digest);
 char contract[65];memset(contract,'a',64);contract[64]=0;
 FILE *f=fopen("app0:update-contract.txt","w");assert(f);fprintf(f,"%s\n",contract);fclose(f);
 snprintf(metadata_text,sizeof metadata_text,"XITA-RELEASE-1\nv-test\n0.3.0\n4096\n%s\n%s\ntester\nFixes and improvements.\n",digest,contract);
 xv_release_manifest m;assert(!xv_release_parse(metadata_text,strlen(metadata_text),&m));
 char bad[1100];snprintf(bad,sizeof bad,"%sextra\n",metadata_text);assert(xv_release_parse(bad,strlen(bad),&m));
 assert(!xv_release_token("../evil"));assert(!xv_release_token("https://evil"));assert(!xv_release_token("a/b"));
 assert(xv_release_parse(metadata_text,strlen(metadata_text)-1,&m));
 char status[128];assert(xv_release_action(1)<0&&xv_release_action(2)<0);
 assert(!xv_release_action(0));work();xv_release_status(status,sizeof status);assert(strstr(status,"Available"));assert(requested==-1);
 corrupt=1;assert(!xv_release_action(1));work();assert(!staging&&!finished);assert(xv_release_action(2)<0);
 corrupt=0;assert(!xv_release_action(0));work();assert(!xv_release_action(1));work();assert(finished && requested==-1);
 assert(!xv_release_action(2)&&requested==0);assert(!xv_release_action(3)&&requested==1);
 fail_http=1;assert(!xv_release_action(0));work();xv_release_status(status,sizeof status);assert(strstr(status,"No public update"));assert(xv_release_action(1)<0);
 fail_http=0;strcpy(metadata_text,"invalid\n");assert(!xv_release_action(0));work();xv_release_status(status,sizeof status);assert(strstr(status,"Invalid release"));
 puts("PASS: release flow, HTTPS policy, corruption rejection, user confirmation, rollback and unavailable releases");return 0;
}
