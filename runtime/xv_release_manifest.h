#pragma once
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* Bounded, canonical text. URLs are constructed locally, never read from metadata. */
typedef struct { char tag[65], version[65], sha[65], contract[65], notes[193]; unsigned size; } xv_release_manifest;
static int xv_release_token(const char *p)
{
    size_t n=strlen(p); if(!n || n>64 || p[0]=='.' || strstr(p,".."))return 0;
    for(;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-'||*p=='_'))return 0;
    return 1;
}
static int xv_release_hex(const char *p)
{
    if(strlen(p)!=64)return 0;
    for(;*p;p++)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')))return 0;
    return 1;
}
static int xv_release_parse(const char *data,size_t size,xv_release_manifest *out)
{
    char b[1024],*lines[8]; unsigned count=0;
    if(!size||size>=sizeof b||data[size-1]!='\n'||memchr(data,0,size))return -1;
    memcpy(b,data,size);b[size]=0;lines[count++]=b;
    for(size_t i=0;i<size;i++)if(b[i]=='\n') {b[i]=0;if(i+1<size) {if(count==8)return -1;lines[count++]=b+i+1;}}
    if(count!=8||strcmp(lines[0],"XITA-RELEASE-1")||!xv_release_token(lines[1])||!xv_release_token(lines[2])||
       !xv_release_hex(lines[4])||!xv_release_hex(lines[5])||strcmp(lines[6],"tester")||strlen(lines[7])>192)return -1;
    if(!*lines[3])return -1;
    unsigned n=0;for(const char *p=lines[3];*p;p++) {if(*p<'0'||*p>'9'||n>6710886)return -1;n=n*10+(*p-'0');}
    if(n<4096||n>64*1024*1024)return -1;
    for(const char *p=lines[7];*p;p++)if((unsigned char)*p<32||(unsigned char)*p>126)return -1;
    memset(out,0,sizeof *out);out->size=n;
    strcpy(out->tag,lines[1]);strcpy(out->version,lines[2]);strcpy(out->sha,lines[4]);strcpy(out->contract,lines[5]);strcpy(out->notes,lines[7]);return 0;
}
