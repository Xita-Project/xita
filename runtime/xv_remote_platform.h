#pragma once
/* Small socket/thread adapter, also used by the real host-loopback tests. */
#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
typedef SceNetSockaddrIn remote_address;
#define REMOTE_THREAD(name) static int name(SceSize argc, void *argv)
#define remote_recv(s,b,n) sceNetRecv(s,b,n,0)
#define remote_send(s,b,n) sceNetSend(s,b,n,0)
#define remote_close(s) sceNetSocketClose(s)
static int remote_accept(int s)
{
    remote_address peer={0};unsigned length=sizeof peer;
    return sceNetAccept(s,(SceNetSockaddr *)&peer,&length);
}
#define remote_sleep(us) sceKernelDelayThread(us)
#define remote_now() sceKernelGetProcessTimeWide()
static int remote_nonblock(int s) { int on=1; return sceNetSetsockopt(s,SCE_NET_SOL_SOCKET,SCE_NET_SO_NBIO,&on,sizeof on); }
static int remote_listen(unsigned port)
{
    int s=sceNetSocket("xita_test",SCE_NET_AF_INET,SCE_NET_SOCK_STREAM,0);
    if(s<0)return -1;
    int reuse=1;
    if(sceNetSetsockopt(s,SCE_NET_SOL_SOCKET,SCE_NET_SO_REUSEADDR,&reuse,sizeof reuse)<0) {remote_close(s);return -1;}
    remote_address a={0};a.sin_len=sizeof a;a.sin_family=SCE_NET_AF_INET;a.sin_port=sceNetHtons(port);
    if(remote_nonblock(s)<0 || sceNetBind(s,(SceNetSockaddr *)&a,sizeof a)<0 || sceNetListen(s,2)<0) {remote_close(s);return -1;}
    return s;
}
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define REMOTE_THREAD(name) static void *name(void *argv)
#define remote_recv(s,b,n) recv(s,b,n,0)
#define remote_send(s,b,n) send(s,b,n,MSG_NOSIGNAL)
#define remote_close(s) close(s)
static int remote_accept(int s)
{
    struct sockaddr_in peer={0};socklen_t length=sizeof peer;
    return accept(s,(struct sockaddr *)&peer,&length);
}
#define remote_sleep(us) usleep(us)
static uint64_t remote_now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static int remote_nonblock(int s) {return fcntl(s,F_SETFL,O_NONBLOCK);}
static int remote_listen(unsigned port)
{
    int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return -1;
    int reuse=1;if(setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse)<0) {remote_close(s);return -1;}
    struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(remote_nonblock(s)<0 || bind(s,(struct sockaddr *)&a,sizeof a)<0 || listen(s,2)<0) {remote_close(s);return -1;}
    return s;
}
#endif
