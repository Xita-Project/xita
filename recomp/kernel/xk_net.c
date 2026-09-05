/* xk_net.c - loopback Winsock/XNet for Halo's transport layer.
 *
 * Halo's split-screen and system-link games run a network server and a network client inside the same
 * process, joined through sockets (the network layer at 0x121350..0x122FD0: TCP endpoints with
 * listen/accept/connect/select, UDP endpoints with sendto/recvfrom).  The XBE links Microsoft's XNet
 * stack statically (a C++ object at [0x262A78], reached through thin thunks at 0x1AF3E0..0x1B158C); that
 * stack drives the NIC directly, so instead every thunk the game calls is HLE'd here (see --hle-addr in
 * the regen command) onto an in-memory stack where every address is local: a connect() finds the
 * listener on that port, and datagrams are queued on sockets bound to the destination port.
 * With XV_NET_ADHOC=1, remote addresses use PDP/PTP (xk_net_adhoc.inc), while
 * 127.0.0.1 remains in memory. Otherwise this original loopback path is used,
 * and select() readiness comes from the queues.  Blocking calls yield to the other guest threads.
 *
 * Winsock layout reminders: sockaddr_in { u16 family; u16 port (net order); u32 addr; u8 zero[8] };
 * fd_set { u32 count; u32 fd[64] }; timeval { i32 sec; i32 usec }.  Ports and addresses are kept in
 * the raw byte order the game stores them in - they are only ever compared with each other. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xk.h"
#include "xk_os.h"
#include "xv_x86rt.h"
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/pspnet_adhoc.h>
#include <psp2/netcheck_dialog.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/processmgr.h>

#define NS_MAX      64
#define NS_HANDLE   0x2000u                 /* handle = NS_HANDLE + index; the game only tests for -1 */
#define TCP_RING    (64 * 1024)
#define DG_MAX      64
#define DG_BYTES    1536
#define ACC_MAX     16

#define WSAEINTR         10004
#define WSAEFAULT        10014
#define WSAEINVAL        10022
#define WSAEMFILE        10024
#define WSAEWOULDBLOCK   10035
#define WSAEMSGSIZE      10040
#define WSAENOTSOCK      10038
#define WSAEADDRINUSE    10048
#define WSAEADDRNOTAVAIL 10049
#define WSAENETDOWN      10050
#define WSAECONNRESET    10054
#define WSAENOTCONN      10057
#define WSAECONNREFUSED  10061

#define FIONBIO   0x8004667Eu
#define FIONREAD  0x4004667Fu

typedef struct { uint16_t len, sport; uint32_t sip; uint8_t d[DG_BYTES]; } dgram;
typedef struct {
    struct { int id, connecting, error; uint32_t recv_ms, send_ms; } adhoc;
    int used, type, nonblock, listening, connected, peer_closed, bound;
    int peer;                                   /* index of the connected TCP peer, -1 if none */
    uint16_t lport, pport; uint32_t lip, pip;   /* local / peer sockaddr_in fields, raw order */
    uint8_t *rx; uint32_t rx_head, rx_len;      /* TCP receive ring */
    dgram *dq; int dq_head, dq_n;               /* UDP receive queue */
    int acc[ACC_MAX], acc_n;                    /* listener: accepted-but-not-taken connections */
} nsock;

static nsock g_s[NS_MAX];
static int g_adhoc; /* set at startup, before guest threads */
static SceNetEtherAddr g_mac;
static uint32_t g_ip;
static int g_err;                               /* WSAGetLastError() value */
static uint16_t g_eph = 0xC000;                 /* next ephemeral port (host order) */
static int g_verbose = -1;
static const uint32_t LOCAL_IP = 0x0100007Fu;   /* 127.0.0.1 in memory order: the local server (0x9C823) only accepts peers from loopback */
static const uint32_t TITLE_IP = 0x0100000Au;   /* 10.0.0.1: what XNetGetTitleXnAddr reports as the console address */

static int verbose(void) { if (g_verbose < 0) { const char *e = getenv("XV_NET_LOG"); g_verbose = e ? atoi(e) : 0; } return g_verbose; }
#define NLOG(...) do { if (verbose()) XK_LOG("[net] " __VA_ARGS__); } while (0)
static uint16_t htons16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static int fail(xctx *c, int err) { g_err = err; c->r[0] = 0xFFFFFFFFu; if (!g_adhoc && (err != WSAEWOULDBLOCK || verbose() > 1)) NLOG("  -> error %d from %08X\n", err, X_M32(c->r[4])); return -1; }
static nsock *sk(uint32_t h) { uint32_t i = h - NS_HANDLE; return i < NS_MAX && g_s[i].used ? &g_s[i] : NULL; }
static int idx(nsock *s) { return (int)(s - g_s); }

static nsock *ns_alloc(int type)
{
    for (int i = 0; i < NS_MAX; ++i) if (!g_s[i].used) {
        nsock *s = &g_s[i]; memset(s, 0, sizeof *s); s->used = 1; s->type = type; s->peer = -1; s->adhoc.id = -1;
        if (type == 1) s->rx = malloc(TCP_RING); else s->dq = malloc(sizeof(dgram) * DG_MAX);
        if (!s->rx && !s->dq) { s->used = 0; return NULL; }
        return s;
    }
    return NULL;
}
static void ns_free(nsock *s)
{
    if (s->adhoc.id >= 0) {
        if (s->type == 2) sceNetAdhocPdpDelete(s->adhoc.id, 0);
        else sceNetAdhocPtpClose(s->adhoc.id, 0);
    }
    if (s->peer >= 0 && g_s[s->peer].used) { g_s[s->peer].peer_closed = 1; g_s[s->peer].peer = -1; }
    for (int i = 0; i < s->acc_n; ++i) ns_free(&g_s[s->acc[i]]);
    free(s->rx); free(s->dq); memset(s, 0, sizeof *s);
}
static nsock *find_bound(int type, uint16_t port, nsock *not)
{
    for (int i = 0; i < NS_MAX; ++i) if (g_s[i].used && g_s[i].type == type && g_s[i].bound && g_s[i].lport == port && &g_s[i] != not) return &g_s[i];
    return NULL;
}
static void assign_port(nsock *s) { do { s->lport = htons16(g_eph++); if (g_eph < 0xC000) g_eph = 0xC000; } while (find_bound(s->type, s->lport, s)); s->bound = 1; if (!s->lip) s->lip = g_adhoc ? g_ip : LOCAL_IP; }
#include "xk_net_adhoc.inc"

static int readable(nsock *s) { if (g_adhoc) { ah_poll(s); if (s->adhoc.error) return 1; } return s->listening ? s->acc_n > 0 : s->type == 1 ? (s->rx_len > 0 || s->peer_closed) : s->dq_n > 0; }
static int writable(nsock *s) { if (g_adhoc) { ah_poll(s); if (s->adhoc.error) return 0; } return g_adhoc && s->adhoc.id >= 0 ? ah_writable(s) : s->type == 2 || s->connected; }
static void put_addr(uint32_t a, uint16_t port, uint32_t ip) { memset(X_G(a), 0, 16); X_M16(a) = 2; X_M16(a + 2) = port; X_M32(a + 4) = ip; }

/* ---- Winsock ---- */
void xv_hle_ws_socket(xctx *c)                              /* socket(af, type, protocol) */
{
    int type = (int)X_ARG(1);
    if (type != 1 && type != 2) { NLOG("socket: type %d unsupported\n", type); fail(c, WSAEINVAL); X_RET(3); }
    nsock *s = ns_alloc(type);
    if (!s) { fail(c, WSAEMFILE); X_RET(3); }
    c->r[0] = NS_HANDLE + idx(s); NLOG("socket(af %u type %d) -> %04X from %08X\n", X_ARG(0), type, c->r[0], X_M32(c->r[4])); X_RET(3);
}
void xv_hle_ws_bind(xctx *c)                                /* bind(s, addr, len) */
{
    nsock *s = sk(X_ARG(0)); uint32_t a = X_ARG(1);
    if (!s) { fail(c, WSAENOTSOCK); X_RET(3); }
    uint16_t port = X_M16(a + 2); uint32_t ip = X_M32(a + 4);
    if (port && find_bound(s->type, port, s)) { NLOG("bind %04X port %u: in use\n", X_ARG(0), htons16(port)); fail(c, WSAEADDRINUSE); X_RET(3); }
    s->lip = ip ? ip : (g_adhoc ? g_ip : LOCAL_IP); if (port) { s->lport = port; s->bound = 1; } else assign_port(s);
    if (g_adhoc && s->type == 2 && s->lip != LOCAL_IP) { int e = ah_pdp(s); if (e) { s->bound = 0; fail(c, e); X_RET(3); } }
    NLOG("bind %04X -> port %u\n", X_ARG(0), htons16(s->lport)); c->r[0] = 0; X_RET(3);
}
void xv_hle_ws_listen(xctx *c)                              /* listen(s, backlog) */
{
    nsock *s = sk(X_ARG(0)); if (!s) { fail(c, WSAENOTSOCK); X_RET(2); }
    if (g_adhoc && s->lip != LOCAL_IP) {
        int e = ah_listen(s, (int)X_ARG(1));
        if (e) { fail(c, e); X_RET(2); }
    }
    if (!s->bound) assign_port(s); s->listening = 1; NLOG("listen %04X port %u\n", X_ARG(0), htons16(s->lport)); c->r[0] = 0; X_RET(2);
}
void xv_hle_ws_connect(xctx *c)                             /* connect(s, addr, len) */
{
    nsock *s = sk(X_ARG(0)); uint32_t a = X_ARG(1);
    if (!s) { fail(c, WSAENOTSOCK); X_RET(3); }
    uint16_t port = X_M16(a + 2); uint32_t ip = X_M32(a + 4);
    if (g_adhoc && s->type == 2 && ip != LOCAL_IP && ip != g_ip) {
        int e = ah_pdp(s); if (e) { fail(c, e); X_RET(3); }
    }
    if (s->type == 2) { s->pport = port; s->pip = ip; s->connected = 1; c->r[0] = 0; X_RET(3); }   /* UDP: default destination */
    if (g_adhoc && ip != LOCAL_IP && ip != g_ip) {
        int e = ah_connect(s, port, ip);
        if (e) fail(c, e); else c->r[0] = 0; X_RET(3);
    }
    nsock *l = find_bound(1, port, s);
    if (!l || !l->listening || l->acc_n >= ACC_MAX) { NLOG("connect %04X -> port %u: refused (%s)\n", X_ARG(0), htons16(port), l ? "not listening/full" : "no socket"); fail(c, WSAECONNREFUSED); X_RET(3); }
    nsock *p = ns_alloc(1); if (!p) { fail(c, WSAEMFILE); X_RET(3); }
    if (!s->bound) assign_port(s);
    if (g_adhoc && ip == LOCAL_IP) s->lip = LOCAL_IP;
    p->bound = 1; p->lport = l->lport; p->lip = l->lip; p->pport = s->lport; p->pip = s->lip; p->connected = 1; p->peer = idx(s);
    s->pport = port; s->pip = ip; s->connected = 1; s->peer = idx(p);
    l->acc[l->acc_n++] = idx(p);
    NLOG("connect %04X -> port %u: accepted as %04X (pending on listener %04X)\n", X_ARG(0), htons16(port), NS_HANDLE + idx(p), NS_HANDLE + idx(l));
    c->r[0] = 0; X_RET(3);
}
void xv_hle_ws_accept(xctx *c)                              /* accept(s, addr, addrlen) */
{
    nsock *s = sk(X_ARG(0)); if (!s || !s->listening) { fail(c, s ? WSAEINVAL : WSAENOTSOCK); X_RET(3); }
    uint64_t start = g_adhoc ? xk_os_monotonic_us() : 0;
    int spins = 0;
    if (g_adhoc) ah_poll(s);
    while (s->acc_n == 0) {
        if (g_adhoc) { if (s->adhoc.error) { fail(c, s->adhoc.error); X_RET(3); } ah_poll(s); if (s->acc_n) break; }
        if (s->nonblock || (g_adhoc ? ah_expired(s, start, 0) : ++spins > 3000)) { fail(c, WSAEWOULDBLOCK); X_RET(3); }
        xk_yield();
    }
    nsock *p = &g_s[s->acc[0]]; memmove(s->acc, s->acc + 1, sizeof(int) * (size_t)(--s->acc_n));
    if (X_ARG(1)) { put_addr(X_ARG(1), p->pport, p->pip); if (X_ARG(2)) X_M32(X_ARG(2)) = 16; }
    c->r[0] = NS_HANDLE + idx(p); NLOG("accept %04X -> %04X from %08X\n", X_ARG(0), c->r[0], X_M32(c->r[4])); X_RET(3);
}
static int tcp_push(nsock *dst, const uint8_t *d, uint32_t n)
{
    uint32_t room = TCP_RING - dst->rx_len; if (n > room) n = room;
    for (uint32_t i = 0; i < n; ++i) dst->rx[(dst->rx_head + dst->rx_len + i) % TCP_RING] = d[i];
    dst->rx_len += n; return (int)n;
}
static int tcp_pop(nsock *s, uint8_t *d, uint32_t n)
{
    if (n > s->rx_len) n = s->rx_len;
    for (uint32_t i = 0; i < n; ++i) d[i] = s->rx[(s->rx_head + i) % TCP_RING];
    s->rx_head = (s->rx_head + n) % TCP_RING; s->rx_len -= n; return (int)n;
}
static int udp_send(nsock *s, const void *src, uint32_t n, uint16_t port, uint32_t ip)
{
    if (g_adhoc) {
        int e = ah_udp_send(s, src, n, port, ip);
        if (e) return -e;
        if (ip != LOCAL_IP && ip != g_ip && !ah_broadcast(ip)) return 1;
    }
    if (!s->bound) assign_port(s);
    int delivered = 0;
    for (int i = 0; i < NS_MAX; ++i) {                                                   /* every UDP socket on that port (broadcast-friendly) */
        nsock *d = &g_s[i];
        if (!d->used || d->type != 2 || !d->bound || d->lport != port) continue;
        if (d->dq_n >= DG_MAX) { if (!g_adhoc) NLOG("sendto: queue full on %04X, dropped\n", NS_HANDLE + i); continue; }
        dgram *g = &d->dq[(d->dq_head + d->dq_n) % DG_MAX]; g->len = (uint16_t)n; g->sport = s->lport; g->sip = g_adhoc && ip == LOCAL_IP ? LOCAL_IP : s->lip;
        memcpy(g->d, src, n); d->dq_n++; delivered++;
    }
    if (!g_adhoc) NLOG("sendto %04X %u bytes -> port %u ip %08X: %d receiver(s)\n", NS_HANDLE + idx(s), n, htons16(port), ip, delivered);
    return delivered;
}
void xv_hle_ws_send(xctx *c)                                /* send(s, buf, len, flags) */
{
    nsock *s = sk(X_ARG(0)); uint32_t n = X_ARG(2);
    if (!s) { fail(c, WSAENOTSOCK); X_RET(4); }
    if (s->type == 2) {                                                                  /* UDP: default destination from connect() */
        if (!s->connected) { fail(c, WSAENOTCONN); X_RET(4); }
        if (n > DG_BYTES) { fail(c, WSAEMSGSIZE); X_RET(4); }
        int r = udp_send(s, X_G(X_ARG(1)), n, s->pport, s->pip);
        if (r < 0) fail(c, -r); else c->r[0] = n; X_RET(4);
    }
    if (g_adhoc && s->adhoc.id >= 0 && !s->listening) {
        int len = (int)n; int e = ah_send(s, X_G(X_ARG(1)), &len);
        if (e) fail(c, e); else c->r[0] = len; X_RET(4);
    }
    if (!s->connected || s->peer < 0 || !g_s[s->peer].used) { fail(c, s->peer_closed ? WSAECONNRESET : WSAENOTCONN); X_RET(4); }
    int w = tcp_push(&g_s[s->peer], (const uint8_t *)X_G(X_ARG(1)), n);
    if (w == 0 && n) { fail(c, WSAEWOULDBLOCK); X_RET(4); }
    if (!g_adhoc) NLOG("send %04X %u -> %d\n", X_ARG(0), n, w); c->r[0] = (uint32_t)w; X_RET(4);
}
void xv_hle_ws_recv(xctx *c)                                /* recv(s, buf, len, flags) */
{
    nsock *s = sk(X_ARG(0)); uint32_t n = X_ARG(2);
    if (!s) { fail(c, WSAENOTSOCK); X_RET(4); }
    if (g_adhoc && s->type == 2) { ah_recv_udp(c, s, n, 0, 0); X_RET(4); }
    if (g_adhoc && !n) { c->r[0] = 0; X_RET(4); }
    uint64_t start = g_adhoc ? xk_os_monotonic_us() : 0;
    int spins = 0;
    if (g_adhoc) ah_poll(s);
    while (s->rx_len == 0) {
        if (g_adhoc) {
            ah_poll(s); if (s->rx_len) break;
            if (s->adhoc.error) { fail(c, s->adhoc.error); X_RET(4); }
            if (!s->peer_closed && ah_expired(s, start, 0)) { fail(c, WSAEWOULDBLOCK); X_RET(4); }
        }
        if (s->peer_closed) { c->r[0] = 0; X_RET(4); }
        if (!s->connected) { fail(c, WSAENOTCONN); X_RET(4); }
        if (s->nonblock || (!g_adhoc && ++spins > 3000)) { fail(c, WSAEWOULDBLOCK); X_RET(4); }
        xk_yield();
    }
    int peek = (X_ARG(3) & 2) != 0;                                                     /* MSG_PEEK */
    int r;
    if (peek) { r = (int)(n < s->rx_len ? n : s->rx_len); uint8_t *d = (uint8_t *)X_G(X_ARG(1)); for (int i = 0; i < r; ++i) d[i] = s->rx[(s->rx_head + (uint32_t)i) % TCP_RING]; }
    else r = tcp_pop(s, (uint8_t *)X_G(X_ARG(1)), n);
    if (!g_adhoc) NLOG("recv %04X %u -> %d\n", X_ARG(0), n, r); c->r[0] = (uint32_t)r; X_RET(4);
}
void xv_hle_ws_recvfrom(xctx *c)                              /* THUNK 0x1B158C: sendto(s, buf, len, flags, to, tolen) - bound under the name ws_recvfrom (regen spec had the pair swapped) */
{
    nsock *s = sk(X_ARG(0)); uint32_t n = X_ARG(2), to = X_ARG(4);
    if (!s) { fail(c, WSAENOTSOCK); X_RET(6); }
    if (s->type == 1) { xv_hle_ws_send(c); c->r[4] += 8; return; }                      /* TCP: plain send, drop the address args */
    uint16_t port = to ? X_M16(to + 2) : s->pport; uint32_t ip = to ? X_M32(to + 4) : s->pip;
    if (n > DG_BYTES) { fail(c, WSAEMSGSIZE); X_RET(6); }
    int r = udp_send(s, X_G(X_ARG(1)), n, port, ip);
    if (r < 0) fail(c, -r); else c->r[0] = n; X_RET(6);
}
void xv_hle_ws_sendto(xctx *c)                            /* THUNK 0x1B157D: recvfrom(s, buf, len, flags, from, fromlen) - bound under the name ws_sendto */
{
    nsock *s = sk(X_ARG(0)); uint32_t n = X_ARG(2), from = X_ARG(4);
    if (!s) { fail(c, WSAENOTSOCK); X_RET(6); }
    if (s->type == 1) { xv_hle_ws_recv(c); c->r[4] += 8; return; }
    if (g_adhoc) { ah_recv_udp(c, s, n, from, X_ARG(5)); X_RET(6); }
    int spins = 0;
    while (s->dq_n == 0) {
        if (s->nonblock || ++spins > 3000) { fail(c, WSAEWOULDBLOCK); X_RET(6); }
        xk_yield();
    }
    dgram *g = &s->dq[s->dq_head];
    uint32_t r = g->len; int trunc = 0; if (r > n) { r = n; trunc = 1; }
    memcpy(X_G(X_ARG(1)), g->d, r);
    if (from) { put_addr(from, g->sport, g->sip); if (X_ARG(5)) X_M32(X_ARG(5)) = 16; }
    s->dq_head = (s->dq_head + 1) % DG_MAX; s->dq_n--;
    NLOG("recvfrom %04X -> %u bytes from port %u%s\n", X_ARG(0), r, htons16(g->sport), trunc ? " (truncated)" : "");
    if (trunc) { fail(c, WSAEMSGSIZE); X_RET(6); }
    c->r[0] = r; X_RET(6);
}
static int set_scan(uint32_t set, int (*pred)(nsock *), int commit)
{
    if (!set) return 0;
    uint32_t n = X_M32(set); if (n > 64) n = 64; int ready = 0; uint32_t keep[64]; uint32_t kn = 0;
    for (uint32_t i = 0; i < n; ++i) { uint32_t h = X_M32(set + 4 + 4 * i); nsock *s = sk(h); if (s && pred(s)) { ready++; keep[kn++] = h; } }
    if (commit) { X_M32(set) = kn; for (uint32_t i = 0; i < kn; ++i) X_M32(set + 4 + 4 * i) = keep[i]; }
    return ready;
}
static int never(nsock *s) { return g_adhoc && s->adhoc.error; }
void xv_hle_ws_select(xctx *c)                              /* select(nfds, rd, wr, ex, timeout) */
{
    uint32_t rd = X_ARG(1), wr = X_ARG(2), ex = X_ARG(3), tv = X_ARG(4);
    int64_t wait_us = tv ? (int64_t)(int32_t)X_M32(tv) * 1000000 + (int32_t)X_M32(tv + 4) : 5000000;   /* NULL timeout: cap at 5 s */
    uint64_t t0 = xk_os_monotonic_us();
    for (;;) {
        if (g_adhoc) ah_peers();
        int n = set_scan(rd, readable, 0) + set_scan(wr, writable, 0) + set_scan(ex, never, 0);
        if (n || (int64_t)(xk_os_monotonic_us() - t0) >= wait_us) {
            int committed = set_scan(rd, readable, 1) + set_scan(wr, writable, 1) + set_scan(ex, never, 1);
            if (g_adhoc) n = committed;
            { static unsigned quiet; if (!g_adhoc && (n || wait_us == 0 || (quiet++ % 200) == 0)) NLOG("select rd %u(%04X) wr %u wait %lld us -> %d from %08X\n", rd ? (unsigned)X_M32(rd) : 0u, rd && X_M32(rd) ? (unsigned)X_M32(rd + 4) : 0u, wr ? (unsigned)X_M32(wr) : 0u, (long long)wait_us, n, X_M32(c->r[4])); }
            c->r[0] = (uint32_t)n; X_RET(5);
        }
        xk_yield();
    }
}
void xv_hle_ws_ioctlsocket(xctx *c)                         /* ioctlsocket(s, cmd, argp) */
{
    nsock *s = sk(X_ARG(0)); uint32_t cmd = X_ARG(1), arg = X_ARG(2);
    if (!s) { fail(c, WSAENOTSOCK); X_RET(3); }
    if (g_adhoc) ah_poll(s);
    if (cmd == FIONBIO) { s->nonblock = X_M32(arg) != 0; NLOG("ioctlsocket %04X FIONBIO %d\n", X_ARG(0), s->nonblock); }
    else if (cmd == FIONREAD) X_M32(arg) = s->type == 1 ? s->rx_len : s->dq_n ? s->dq[s->dq_head].len : 0;
    else NLOG("ioctlsocket %04X cmd %08X ignored\n", X_ARG(0), cmd);
    c->r[0] = 0; X_RET(3);
}
void xv_hle_ws_setsockopt(xctx *c) {
    nsock *s = sk(X_ARG(0));
    if (g_adhoc && s && X_ARG(1) == 0xffff && (X_ARG(2) == 0x1005 || X_ARG(2) == 0x1006)) {
        if (!X_ARG(3) || X_ARG(4) < 4) { fail(c, WSAEFAULT); X_RET(5); }
        if (X_ARG(2) == 0x1005) s->adhoc.send_ms = X_M32(X_ARG(3));
        else s->adhoc.recv_ms = X_M32(X_ARG(3));
    } NLOG("setsockopt %04X level %X opt %X\n", X_ARG(0), X_ARG(1), X_ARG(2)); c->r[0] = sk(X_ARG(0)) ? 0 : (uint32_t)fail(c, WSAENOTSOCK); X_RET(5); }
void xv_hle_ws_getsockopt(xctx *c)                          /* getsockopt(s, level, opt, val, len) */
{
    uint32_t val = X_ARG(3), len = X_ARG(4);
    if (!sk(X_ARG(0))) { fail(c, WSAENOTSOCK); X_RET(5); }
    uint32_t opt = X_ARG(2);
    if (g_adhoc && val && len && X_M32(len) >= 4) {
        nsock *s = sk(X_ARG(0)); ah_poll(s);
        if (opt == 0x1005 || opt == 0x1006 || opt == 0x1007) {
            X_M32(val) = opt == 0x1005 ? s->adhoc.send_ms : opt == 0x1006 ? s->adhoc.recv_ms : (uint32_t)s->adhoc.error;
            if (opt == 0x1007) s->adhoc.error = 0;
            X_M32(len) = 4; c->r[0] = 0; X_RET(5);
        }
    }
    if (val && len && X_M32(len) >= 4) { X_M32(val) = (opt == 0x1001 || opt == 0x1002) ? 0x4000 : 0; X_M32(len) = 4; }   /* SO_SNDBUF / SO_RCVBUF: 16 KB (the game's floor); SO_ERROR etc.: 0 */
    NLOG("getsockopt %04X level %X opt %X -> %u\n", X_ARG(0), X_ARG(1), opt, val ? (unsigned)X_M32(val) : 0u);
    c->r[0] = 0; X_RET(5);
}
void xv_hle_ws_getpeername(xctx *c)   /* THUNK 0x1B03F1 (bound as ws_getpeername in the regen spec): getsockname body */
{
    nsock *s = sk(X_ARG(0)); if (!s) { fail(c, WSAENOTSOCK); X_RET(3); }
    if (!s->bound) assign_port(s);
    put_addr(X_ARG(1), s->lport, s->lip); if (X_ARG(2)) X_M32(X_ARG(2)) = 16; NLOG("getsockname %04X -> port %u from %08X\n", X_ARG(0), htons16(s->lport), X_M32(c->r[4])); c->r[0] = 0; X_RET(3);
}
void xv_hle_ws_getsockname(xctx *c)   /* THUNK 0x1B03FC (bound as ws_getsockname): getpeername body - 0x121C80 tries this first, then 0x1B03F1 */
{
    nsock *s = sk(X_ARG(0)); if (!s) { fail(c, WSAENOTSOCK); X_RET(3); }
    if (!s->connected) { fail(c, WSAENOTCONN); X_RET(3); }
    put_addr(X_ARG(1), s->pport, s->pip); if (X_ARG(2)) X_M32(X_ARG(2)) = 16; NLOG("getpeername %04X -> port %u from %08X\n", X_ARG(0), htons16(s->pport), X_M32(c->r[4])); c->r[0] = 0; X_RET(3);
}
void xv_hle_ws_closesocket(xctx *c)
{
    nsock *s = sk(X_ARG(0)); if (!s) { fail(c, WSAENOTSOCK); X_RET(1); }
    NLOG("closesocket %04X from %08X\n", X_ARG(0), X_M32(c->r[4])); ns_free(s); c->r[0] = 0; X_RET(1);
}
void xv_hle_ws_WSAGetLastError(xctx *c) { c->r[0] = (uint32_t)g_err; X_RET(0); }

/* ---- XNet ---- */
static void fill_random(uint32_t a, uint32_t n) { uint8_t *d = (uint8_t *)X_G(a); for (uint32_t i = 0; i < n; ++i) d[i] = (uint8_t)(rand() >> 3); }
void xv_hle_xn_XNetCreateKey(xctx *c)                       /* (XNKID *8, XNKEY *16) */
{
    fill_random(X_ARG(0), 8); fill_random(X_ARG(1), 16);
    if (X_M32(X_ARG(0)) == 0) X_M32(X_ARG(0)) = 1;
    NLOG("XNetCreateKey\n"); c->r[0] = 0; X_RET(2);
}
void xv_hle_xn_XNetRegisterKey(xctx *c) { NLOG("XNetRegisterKey\n"); c->r[0] = 0; X_RET(2); }
void xv_hle_xn_XNetUnregisterKey(xctx *c) { NLOG("XNetUnregisterKey\n"); c->r[0] = 0; X_RET(1); }
void xv_hle_xn_XNetRandom(xctx *c) { fill_random(X_ARG(0), X_ARG(1)); c->r[0] = 0; X_RET(2); }
void xv_hle_xn_XNetXnAddrToInAddr(xctx *c)                  /* (XNADDR *, XNKID *, IN_ADDR *) */
{
    if (g_adhoc) {
        SceNetEtherAddr mac; memcpy(&mac, X_G(X_ARG(0) + 10), 6);
        uint32_t ip = X_M32(X_ARG(0));
        if (ip != LOCAL_IP) ip = ah_learn(&mac);
        if (!ip) { c->r[0] = WSAEADDRNOTAVAIL; X_RET(3); }
        X_M32(X_ARG(2)) = ip; c->r[0] = 0; X_RET(3);
    }
    uint32_t ina = X_M32(X_ARG(0)); X_M32(X_ARG(2)) = ina ? ina : TITLE_IP;
    NLOG("XNetXnAddrToInAddr -> %08X\n", X_M32(X_ARG(2))); c->r[0] = 0; X_RET(3);
}
void xv_hle_xn_XNetGetTitleXnAddr(xctx *c)                  /* (XNADDR *36) -> XNET_GET_XNADDR_* flags */
{
    uint32_t a = X_ARG(0); memset(X_G(a), 0, 36);
    if (g_adhoc) { ah_peers(); X_M32(a) = g_ip; memcpy(X_G(a + 10), &g_mac, 6); c->r[0] = 0x2 | 0x4; X_RET(1); }
    X_M32(a) = TITLE_IP;                                                 /* ina */
    static const uint8_t mac[6] = { 0x00, 0x50, 0xF2, 0x12, 0x34, 0x56 }; memcpy(X_G(a + 10), mac, 6);
    NLOG("XNetGetTitleXnAddr\n"); c->r[0] = 0x2 | 0x4; X_RET(1);        /* ETHERNET | STATIC */
}

/* diagnostic accessor for the pad layer (which lacks the X_ macros) */
uint32_t xv_guest_r16(uint32_t va) { return X_M16(va); }
