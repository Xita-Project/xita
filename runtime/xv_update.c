/* Fixed-path, two-slot executable updates. The stable launcher is never replaced
 * by this protocol. A failed/torn inactive slot or record leaves the active slot.
 * Hashes detect transfer/storage corruption; the remote pairing authenticates
 * the operator. This is not a public download/signature service. */
#include "xv_update.h"
#include "xv_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif
#define DATA "ux0:data/xita/update/"
#define APP "app0:/"
#define WRITE_APP "ux0:app/XITA00001/"
#define GET(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
#define SET(p,v) __atomic_store_n(p,v,__ATOMIC_RELEASE)
enum { CONFIRMED=1,PENDING=2,ATTEMPTED=3,RETIRED=4 };
typedef struct { unsigned generation,size,state; char sha[65],contract[65]; } record;
static FILE *transfer;
static record incoming;
static unsigned state,received,total,requested,boot_slot;
static char boot_sha[65];
static const char *const labels[]={"No update staged","Receiving update","Update verified - ready to install","Update failed - working build preserved","Restarting to apply update"};
static int hex64(const char *p)
{
    if(strlen(p)!=64)return 0;
    for(unsigned i=0;i<64;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;
    return 1;
}
static int sync_close(FILE *f)
{
    int bad=fflush(f);
#ifndef __vita__
    if(fsync(fileno(f)))bad=1;
#endif
    if(fclose(f))bad=1;
#ifdef __vita__
    if(sceIoSync("ux0:",0)<0)bad=1;
#endif
    return bad?-1:0;
}
static int write_text(const char *path,const char *text)
{
    char tmp[192];if(snprintf(tmp,sizeof tmp,"%s.tmp",path)>=(int)sizeof tmp)return -1;
    FILE *f=fopen(tmp,"wb");if(!f)return -1;
    int bad=fwrite(text,1,strlen(text),f)!=strlen(text);
    if(sync_close(f))bad=1;
    if(bad||rename(tmp,path))return -1;
#ifdef __vita__
    if(sceIoSync("ux0:",0)<0)return -1;
#endif
    return 0;
}
static int contract(char out[65])
{
    FILE *f=fopen(APP "update-contract.txt","rb");if(!f)return -1;
    char b[67]={0};size_t n=fread(b,1,sizeof b-1,f);int bad=ferror(f);fclose(f);
    if(n==65&&b[64]=='\n')b[64]=0;
    if(bad||!hex64(b))return -1;
    memcpy(out,b,65);return 0;
}
static int record_write(const char *path,const record *r)
{
    char prefix[200],sum[65],line[280];
    int n=snprintf(prefix,sizeof prefix,"XITA1 %u %u %u %s %s",r->generation,r->size,r->state,r->sha,r->contract);
    if(n<0||n>=(int)sizeof prefix)return -1;
    xv_sha256 h;xv_sha256_init(&h);xv_sha256_add(&h,prefix,n);xv_sha256_end(&h,sum);
    snprintf(line,sizeof line,"%s %s\n",prefix,sum);return write_text(path,line);
}
static int record_read(const char *path,record *r)
{
    FILE *f=fopen(path,"rb");if(!f)return -1;
    char line[280]={0},sum[65],actual[65],installed[65];size_t n=fread(line,1,sizeof line-1,f);int bad=ferror(f);fclose(f);
    int end=0;
    if(bad||sscanf(line,"XITA1 %u %u %u %64s %64s %64s%n",&r->generation,&r->size,&r->state,r->sha,r->contract,sum,&end)!=6 ||
        end<1||(size_t)end+1!=n||line[end]!='\n'||r->generation>1000000000||r->size<4096||r->size>XV_UPDATE_LIMIT||
        r->state<CONFIRMED||r->state>RETIRED||!hex64(r->sha)||!hex64(r->contract)||!hex64(sum)||contract(installed)||strcmp(installed,r->contract))return -1;
    char *last=strrchr(line,' ');if(!last)return -1;
    xv_sha256 h;xv_sha256_init(&h);xv_sha256_add(&h,line,last-line);xv_sha256_end(&h,actual);
    return strcmp(actual,sum)?-1:0;
}
static int verify(const char *path,const record *r)
{
    FILE *f=fopen(path,"rb");if(!f)return -1;
    unsigned char b[8192];unsigned count=0;size_t n;int bad=0;
    xv_sha256 h;xv_sha256_init(&h);
    while((n=fread(b,1,sizeof b,f))) {
        if(!count&&(n<4||memcmp(b,"SCE\0",4)))bad=1;
        if(n>r->size-count) {bad=1;break;}
        count+=(unsigned)n;xv_sha256_add(&h,b,n);
    }
    if(ferror(f)||count!=r->size)bad=1;
    fclose(f);char sum[65];xv_sha256_end(&h,sum);
    return bad||strcmp(sum,r->sha)?-1:0;
}
static void slot_path(char out[128],unsigned slot,int writable)
{snprintf(out,128,"%sgame-%c.self",writable?WRITE_APP:APP,'a'+slot);}
static void meta_path(char out[128],unsigned slot)
{snprintf(out,128,DATA "slot-%u.meta",slot);}
static int read_slot(unsigned slot,record *r,int check_file)
{
    char path[128];meta_path(path,slot);
    if(record_read(path,r)) {
        if(slot||record_read(APP "boot-game.txt",r))return -1;
    }
    slot_path(path,slot,0);return check_file?verify(path,r):0;
}
void xv_update_init(void)
{
    char installed[65];SET(&boot_slot,0);SET(&state,0);SET(&requested,0);SET(&received,0);SET(&total,0);
    if(contract(installed))return; /* Older/foreign VPK has no update contract. */
    mkdir("ux0:data",0777);mkdir("ux0:data/xita",0777);mkdir(DATA,0777);
    if(!record_read(DATA "incoming.meta",&incoming)) {SET(&received,incoming.size);SET(&total,incoming.size);SET(&state,2);}
}
int xv_update_begin(unsigned size,const char *sha,const char *expected)
{
    char installed[65];if(GET(&requested)||size<4096||size>XV_UPDATE_LIMIT||!hex64(sha)||!hex64(expected)||contract(installed)||strcmp(expected,installed))return -1;
    xv_update_close();remove(DATA "incoming.meta");
    transfer=fopen(DATA "incoming.self","wb");if(!transfer) {SET(&state,3);return -1;}
    incoming=(record){.size=size,.state=PENDING};memcpy(incoming.sha,sha,65);memcpy(incoming.contract,expected,65);
    SET(&received,0);SET(&total,size);SET(&state,1);return 0;
}
int xv_update_chunk(unsigned offset,const void *data,unsigned size)
{
    if(!transfer||GET(&state)!=1||offset!=GET(&received)||!size||size>XV_UPDATE_CHUNK||size>incoming.size-offset)return -1;
    if(fwrite(data,1,size,transfer)!=size) {xv_update_close();SET(&state,3);return -1;}
    SET(&received,offset+size);return 0;
}
int xv_update_finish(void)
{
    if(!transfer||GET(&received)!=incoming.size)return -1;
    FILE *f=transfer;transfer=NULL;
    if(sync_close(f)||verify(DATA "incoming.self",&incoming)||record_write(DATA "incoming.meta",&incoming)) {SET(&state,3);return -1;}
    SET(&state,2);return 0;
}
void xv_update_close(void)
{if(transfer) {fclose(transfer);transfer=NULL;}if(GET(&state)==1)SET(&state,3);}
int xv_update_request(int rollback)
{
    char installed[65];if(GET(&requested)||contract(installed)||(rollback?GET(&state)==1:GET(&state)!=2))return -1;
    if(write_text(DATA "action",rollback?"rollback\n":"apply\n"))return -1;
    SET(&state,4);SET(&requested,1);return 0;
}
unsigned xv_update_requested(void) {return GET(&requested);}
void xv_update_status(char *out,size_t size)
{unsigned n=GET(&state);snprintf(out,size,"%s",labels[n<5?n:3]);}
void xv_update_json(char *out,size_t size)
{
    char installed[65]={0};contract(installed);
    unsigned boot=GET(&boot_slot);
    snprintf(out,size,"{\"state\":%u,\"received\":%u,\"size\":%u,\"contract\":\"%s\",\"requested\":%u,\"boot_slot\":%d,\"boot_sha256\":\"%s\"}",GET(&state),GET(&received),GET(&total),installed,GET(&requested),(int)boot-1,boot?boot_sha:"");
}
static int copy_candidate(unsigned slot,const record *r)
{
    char path[128],tmp[140];slot_path(path,slot,1);snprintf(tmp,sizeof tmp,"%s.next",path);
    FILE *in=fopen(DATA "incoming.self","rb");if(!in)return -1;
    FILE *out=fopen(tmp,"wb");if(!out) {fclose(in);return -1;}
    unsigned char b[8192];size_t n;unsigned count=0;int bad=0;
    while((n=fread(b,1,sizeof b,in))) {
        if(n>r->size-count||fwrite(b,1,n,out)!=n) {bad=1;break;}count+=(unsigned)n;
    }
    if(ferror(in)||count!=r->size)bad=1;
    fclose(in);if(sync_close(out))bad=1;
    if(bad||verify(tmp,r)||rename(tmp,path))return -1;
#ifdef __vita__
    if(sceIoSync("ux0:",0)<0)return -1;
#endif
    return 0;
}
int xv_update_boot(void)
{
    mkdir("ux0:data",0777);mkdir("ux0:data/xita",0777);mkdir(DATA,0777);
    record slots[2]={{0}},candidate;int valid[2];unsigned generation=0;int active=-1;
    for(unsigned i=0;i<2;i++) {
        valid[i]=!read_slot(i,&slots[i],1);
        if(valid[i]) {if(slots[i].generation>generation)generation=slots[i].generation;
            if(slots[i].state==CONFIRMED&&(active<0||slots[i].generation>slots[active].generation))active=(int)i;}
    }
    if(active<0)return -1;
    FILE *f=fopen(DATA "action","rb");char action[16]={0};
    if(f) {fread(action,1,sizeof action-1,f);fclose(f);}
    if(!strcmp(action,"apply\n")&&generation<1000000000&&!record_read(DATA "incoming.meta",&candidate)&&!verify(DATA "incoming.self",&candidate)) {
        unsigned target=1u-(unsigned)active;
        candidate.generation=generation+1;candidate.state=PENDING;
        if(!copy_candidate(target,&candidate)) {
            char meta[128];meta_path(meta,target);
            if(!record_write(meta,&candidate)) {slots[target]=candidate;valid[target]=1;remove(DATA "incoming.meta");remove(DATA "incoming.self");}
        }
    } else if(!strcmp(action,"rollback\n")) {
        unsigned other=1u-(unsigned)active;
        if(valid[other]&&slots[other].state==CONFIRMED) {
            record retired=slots[active];retired.state=RETIRED;char meta[128];meta_path(meta,(unsigned)active);
            if(!record_write(meta,&retired)) {slots[active]=retired;active=(int)other;}
        }
    }
    remove(DATA "action");
    for(unsigned i=0;i<2;i++)if(valid[i]&&slots[i].state==PENDING&&slots[i].generation>slots[active].generation) {
        slots[i].state=ATTEMPTED;char meta[128];meta_path(meta,i);
        if(!record_write(meta,&slots[i]))return (int)i;
    }
    return active;
}
int xv_update_confirm(unsigned slot)
{
    if(slot>1)return -1;
    record r;if(read_slot(slot,&r,0))return -1;
    if(r.state!=CONFIRMED) {
        if(r.state!=ATTEMPTED)return -1;
        r.state=CONFIRMED;char meta[128];meta_path(meta,slot);
        if(record_write(meta,&r))return -1;
    }
    /* Published once after a rendered dashboard. The string remains immutable
     * while the network thread reads it; the release store publishes its bytes. */
    if(!GET(&boot_slot)) {memcpy(boot_sha,r.sha,65);SET(&boot_slot,slot+1);}
    return 0;
}
