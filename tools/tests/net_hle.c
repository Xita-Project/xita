/* Host-side behavioral tests using the installed SDK's actual declarations.
 * No radio simulation: mocks check addresses, buffering, retries and guest ABI.
 * Run tools/tests/net_hle.sh. */
#include <assert.h>
#include "../../recomp/kernel/xk_net.c"
static uint8_t ram[65536]; static uint32_t pt[16];
uint8_t *g_xram = ram; uint32_t *g_xpt = pt;
static uint64_t ticks; static int calls, pending, connects, ptp_pending, deleted;
static SceNetEtherAddr remote = {{2, 3, 4, 5, 6, 7}}, sent_mac;
static int accept_pending;
static int send_rc, send_count, rx_calls; static uint16_t sent_port;
uint64_t xk_os_monotonic_us(void) { return ticks; }
void xk_yield(void) { ticks += 1000; }
void xk_os_log(const char *fmt, ...) { (void)fmt; }
int sceKernelPowerTick(SceKernelPowerTickType type) { (void)type; return 0; }
int sceNetAdhocctlGetPeerList(int *len, void *buf) { (void)buf; calls++; *len = 0; return 0; }
int sceNetAdhocPdpCreate(const SceNetEtherAddr *m, SceUShort16 p, unsigned int b, int f)
{ (void)m; (void)p; (void)b; (void)f; calls++; return 10; }
int sceNetAdhocPdpDelete(int id, int flag) { (void)id; (void)flag; deleted++; return 0; }
int sceNetAdhocPtpClose(int id, int flag) { (void)id; (void)flag; deleted++; return 0; }
int sceNetAdhocPdpSend(int id, const SceNetEtherAddr *m, SceUShort16 p, const void *d, int n, unsigned int t, int f)
{ (void)id; (void)d; (void)n; assert(t == 0 && f == AH_NB); calls++; send_count++; sent_mac=*m; sent_port=p; return send_rc; }
int sceNetAdhocPdpRecv(int id, SceNetEtherAddr *m, SceUShort16 *p, void *d, int *n, unsigned int t, int f)
{
    (void)id; assert(t == 0 && f == AH_NB); calls++; rx_calls++;
    if (!pending) return SCE_ERROR_NET_ADHOC_WOULD_BLOCK;
    pending--; assert(*n >= 4); *m=remote; *p=2302; *n=4; memcpy(d,"halo",4); return 0;
}
int sceNetAdhocPtpListen(const SceNetEtherAddr *m, SceUShort16 p, unsigned int b, unsigned int r, int c, int l, int f)
{ (void)m; (void)p; (void)b; (void)r; (void)c; (void)l; (void)f; calls++; return 11; }
int sceNetAdhocPtpAccept(int id, SceNetEtherAddr *m, SceUShort16 *p, unsigned int t, int f)
{ (void)id; assert(!t && f==AH_NB); calls++; if (accept_pending) { accept_pending=0; *m=remote; *p=4000; return 13; } return SCE_ERROR_NET_ADHOC_WOULD_BLOCK; }
int sceNetAdhocPtpOpen(const SceNetEtherAddr *m, SceUShort16 p, const SceNetEtherAddr *d, SceUShort16 q, unsigned int b, unsigned int r, int c, int f)
{ (void)m; (void)p; (void)b; (void)r; (void)c; (void)f; assert(!memcmp(d,&remote,6) && q==2302); calls++; return 12; }
int sceNetAdhocPtpConnect(int id, unsigned int t, int f)
{ (void)id; assert(!t && f==AH_NB); return connects++ ? 0 : (int)SCE_ERROR_NET_ADHOC_WOULD_BLOCK; }
int sceNetAdhocPtpRecv(int id, void *d, int *n, unsigned int t, int f)
{ (void)id; assert(!t && f==AH_NB); if (!ptp_pending) return SCE_ERROR_NET_ADHOC_WOULD_BLOCK; ptp_pending=0; assert(*n>=3); memcpy(d,"ptp",3); *n=3; return 0; }
int sceNetAdhocPtpSend(int id, const void *d, int *n, unsigned int t, int f)
{ (void)id; (void)d; assert(!t && f==AH_NB); if (*n>2) *n=2; return 0; }
int sceNetAdhocPollSocket(SceNetAdhocPollSd *s, int n, unsigned int t, int f)
{ assert(n==1 && !t && f==AH_NB); s->revents=SCE_NET_ADHOC_EV_SEND; return 1; }
static uint32_t invoke(void (*fn)(xctx *), int n, const uint32_t *args)
{
    xctx c={0}; c.r[4]=0x100; X_M32(0x100)=0x1234;
    for (int i=0;i<n;i++) X_M32(0x104+i*4)=args[i];
    fn(&c); assert(c.r[4]==0x104+4*(unsigned)n); return c.r[0];
}
#define CALL(fn, ...) invoke(fn, sizeof((uint32_t[]){__VA_ARGS__})/4, (uint32_t[]){__VA_ARGS__})
int main(void)
{
    for (int i=0;i<16;i++) pt[i]=i*4096;
    g_verbose=0;
    /* Default: arbitrary destination still routes in memory, with no SDK calls. */
    uint32_t a=CALL(xv_hle_ws_socket,2,2,17), b=CALL(xv_hle_ws_socket,2,2,17);
    put_addr(0x200,htons16(2302),0);
    assert(!CALL(xv_hle_ws_bind,b,0x200,16));
    memcpy(X_G(0x300),"test",4); put_addr(0x200,htons16(2302),0x12345678);
    assert(CALL(xv_hle_ws_recvfrom,a,0x300,4,0,0x200,16)==4);
    assert(CALL(xv_hle_ws_sendto,b,0x400,4,0,0x220,0x240)==4);
    assert(!memcmp(X_G(0x400),"test",4) && X_M32(0x224)==LOCAL_IP && !calls);
    ns_free(sk(a)); ns_free(sk(b)); assert(!deleted);
    /* Wireless fake IP and collision rejection. */
    g_adhoc=1; g_mac=(SceNetEtherAddr){{2,0,0,0,1,2}}; g_ip=ah_ip(&g_mac);
    assert(g_ip==0x0201fea9 && ah_learn(&remote)==0x0706fea9);
    SceNetEtherAddr collision=remote; ((uint8_t *)&collision)[1]++;
    assert(!ah_learn(&collision));
    a=CALL(xv_hle_ws_socket,2,2,17); put_addr(0x200,htons16(2302),0);
    assert(!CALL(xv_hle_ws_bind,a,0x200,16));
    put_addr(0x200,htons16(2302),0xffffffff);
    assert(CALL(xv_hle_ws_recvfrom,a,0x300,4,0,0x200,16)==4);
    static const uint8_t bc[6]={255,255,255,255,255,255};
    assert(!memcmp(&sent_mac,bc,6) && sent_port==2302);
    sk(a)->dq_n=0; pending=1;
    /* select preserves a datagram, repeated MSG_PEEK preserves it again. */
    X_M32(0x500)=1; X_M32(0x504)=a; X_M32(0x600)=X_M32(0x604)=0;
    assert(CALL(xv_hle_ws_select,0,0x500,0,0,0x600)==1);
    assert(sk(a)->dq_n==1);
    assert(CALL(xv_hle_ws_sendto,a,0x400,4,2,0x220,0x240)==4 && sk(a)->dq_n==1);
    assert(CALL(xv_hle_ws_sendto,a,0x400,4,0,0x220,0x240)==4 && !sk(a)->dq_n);
    assert(!memcmp(X_G(0x400),"halo",4) && X_M32(0x224)==0x0706fea9 && X_M16(0x222)==htons16(2302));
    X_M32(0x250)=1; CALL(xv_hle_ws_ioctlsocket,a,FIONBIO,0x250);
    uint64_t before=ticks;
    assert(CALL(xv_hle_ws_sendto,a,0x400,4,0,0,0)==0xffffffff && g_err==WSAEWOULDBLOCK && ticks==before);
    /* Guest send timeout bounds retries, and unknown IP never sends. */
    sk(a)->nonblock=0; sk(a)->adhoc.send_ms=2; send_rc=SCE_ERROR_NET_ADHOC_WOULD_BLOCK;
    assert(ah_udp_send(sk(a),"a",1,htons16(2302),0x0706fea9)==WSAEWOULDBLOCK && ticks-before==2000);
    int old=send_count; assert(ah_udp_send(sk(a),"a",1,1,0x0909fea9)==WSAEADDRNOTAVAIL && send_count==old); send_rc=0;
    /* PTP asynchronous connect, partial send, queued receive, EOF. */
    b=CALL(xv_hle_ws_socket,2,1,6); sk(b)->nonblock=1;
    put_addr(0x200,htons16(2302),0x0706fea9);
    assert(CALL(xv_hle_ws_connect,b,0x200,16)==0xffffffff && g_err==WSAEWOULDBLOCK);
    assert(writable(sk(b)) && sk(b)->connected);
    assert(CALL(xv_hle_ws_send,b,0x300,4,0)==2);
    ptp_pending=1; assert(readable(sk(b)));
    assert(CALL(xv_hle_ws_recv,b,0x400,3,2)==3 && sk(b)->rx_len==3);
    assert(CALL(xv_hle_ws_recv,b,0x400,3,0)==3 && !memcmp(X_G(0x400),"ptp",3));
    sk(b)->peer_closed=1; assert(CALL(xv_hle_ws_recv,b,0x400,3,0)==0);
    CALL(xv_hle_ws_closesocket,a); CALL(xv_hle_ws_closesocket,b); assert(deleted==2);
    /* Loopback TCP must still identify the client as 127.0.0.1 in ad-hoc mode. */
    a=CALL(xv_hle_ws_socket,2,1,6); b=CALL(xv_hle_ws_socket,2,1,6);
    put_addr(0x200,htons16(5000),LOCAL_IP);
    assert(!CALL(xv_hle_ws_bind,a,0x200,16)); assert(!CALL(xv_hle_ws_listen,a,2));
    assert(!CALL(xv_hle_ws_connect,b,0x200,16));
    uint32_t client=CALL(xv_hle_ws_accept,a,0x220,0x240);
    assert(X_M32(0x224)==LOCAL_IP);
    assert(CALL(xv_hle_ws_send,b,0x300,4,0)==4);
    assert(CALL(xv_hle_ws_recv,client,0x400,4,0)==4);
    CALL(xv_hle_ws_closesocket,client); CALL(xv_hle_ws_closesocket,b); CALL(xv_hle_ws_closesocket,a);
    /* Wireless listener readiness saves the accepted handle for accept(). */
    a=CALL(xv_hle_ws_socket,2,1,6); put_addr(0x200,htons16(5000),0);
    assert(!CALL(xv_hle_ws_bind,a,0x200,16)); assert(!CALL(xv_hle_ws_listen,a,2));
    accept_pending=1; assert(readable(sk(a)) && sk(a)->acc_n==1);
    client=CALL(xv_hle_ws_accept,a,0x220,0x240);
    assert(X_M32(0x224)==0x0706fea9 && X_M16(0x222)==htons16(4000));
    CALL(xv_hle_ws_closesocket,client); CALL(xv_hle_ws_closesocket,a);
    /* Halo 3925 puts its camera pointer immediately after its 12-byte XNADDR.
     * Guard that exact boundary before checking any fields or return flags. */
    memset(X_G(0x700), 0xa5, 40);
    X_M32(0x70c) = 0x8006ba98;
    CALL(xv_hle_xn_XNetGetTitleXnAddr,0x700);
    assert(X_M32(0x70c) == 0x8006ba98);
    for (unsigned i=16;i<40;i++) assert(X_M8(0x700+i) == 0xa5);
    /* XNADDR round-trip and explicit local-server exception. */
    assert(CALL(xv_hle_xn_XNetGetTitleXnAddr,0x700)==2);
    assert(X_M16(0x700)==12 && X_M32(0x708)==g_ip && !memcmp(X_G(0x702),&g_mac,6));
    assert(!CALL(xv_hle_xn_XNetXnAddrToInAddr,0x700,0,0x750) && X_M32(0x750)==g_ip);
    memcpy(X_G(0x702),&remote,6); X_M32(0x708)=0;
    assert(!CALL(xv_hle_xn_XNetXnAddrToInAddr,0x700,0,0x750) && X_M32(0x750)==0x0706fea9);
    X_M32(0x708)=LOCAL_IP;
    assert(!CALL(xv_hle_xn_XNetXnAddrToInAddr,0x700,0,0x750) && X_M32(0x750)==LOCAL_IP);
    /* Both transports, every field split across noncontiguous guest pages.
     * Check bytes independently of the C struct, including surrounding memory. */
    pt[1]=0x5000; pt[3]=0x9000;
    for (g_adhoc=0;g_adhoc<=1;g_adhoc++) {
        for (uint32_t addr=0xff5;addr<=0x1001;addr++) {
            for (unsigned i=0;i<48;i++) X_M8(addr-4+i)=0xa5;
            assert(CALL(xv_hle_xn_XNetGetTitleXnAddr,addr)==2);
            uint8_t expected[12]={12,0,0,0x50,0xf2,0x12,0x34,0x56,10,0,0,1};
            uint32_t ip=g_adhoc ? g_ip : TITLE_IP;
            if (g_adhoc) { memcpy(expected+2,&g_mac,6); memcpy(expected+8,&ip,4); }
            for (unsigned i=0;i<12;i++) assert(X_M8(addr+i)==expected[i]);
            for (unsigned i=0;i<4;i++) assert(X_M8(addr-4+i)==0xa5);
            for (unsigned i=12;i<44;i++) assert(X_M8(addr+i)==0xa5);
            X_M8(0x2ffe)=X_M8(0x3003)=0x5a;
            assert(!CALL(xv_hle_xn_XNetXnAddrToInAddr,addr,0,0x2fff));
            for (unsigned i=0;i<4;i++) assert(X_M8(0x2fff+i)==((ip>>(i*8))&255));
            assert(X_M8(0x2ffe)==0x5a && X_M8(0x3003)==0x5a);
        }
    }
    puts("net HLE: loopback, PDP/PTP, select/peek, 3925 address bounds/page splits, timeouts and close PASS");
}
