/* Private Vita backend for xk_net.c. All calls run on the guest scheduler thread,
 * except startup/shutdown before/after that thread. SDK I/O is always nonblocking.
 * Receive polling uses the existing queues, so select and MSG_PEEK are lossless. */
#define AH_NB SCE_NET_ADHOC_F_NONBLOCK
#define AH_PEERS 32
static struct { uint32_t ip; SceNetEtherAddr mac; } ah_peer[AH_PEERS];
static unsigned char ah_pool[1024 * 1024] __attribute__((aligned(64)));
static int ah_net_module, ah_net, ah_ctl, ah_module, ah_transport, ah_control;
static uint64_t ah_next_peers;

static uint32_t ah_ip(const SceNetEtherAddr *mac)
{
    const uint8_t *b = (const uint8_t *)mac;
    return 0x0000fea9u | (uint32_t)b[4] << 16 | (uint32_t)b[5] << 24;
}
static uint32_t ah_learn(const SceNetEtherAddr *mac)
{
    const uint8_t *b = (const uint8_t *)mac;
    static const uint8_t zero[6];
    if ((b[0] & 1) || !memcmp(b, zero, 6)) return 0;
    uint32_t ip = ah_ip(mac);
    if (!memcmp(mac, &g_mac, 6)) return g_ip;
    /* A /16 collision cannot be represented by this scheme. Reject ambiguity
     * rather than silently sending another console's packets to the wrong MAC. */
    if (ip == g_ip) return 0;
    int empty = -1;
    for (int i = 0; i < AH_PEERS; i++) {
        if (ah_peer[i].ip == ip) return memcmp(mac, &ah_peer[i].mac, 6) ? 0 : ip;
        if (!ah_peer[i].ip && empty < 0) empty = i;
    }
    if (empty < 0) return 0;
    ah_peer[empty].ip = ip; ah_peer[empty].mac = *mac;
    NLOG("peer %02x:%02x:%02x:%02x:%02x:%02x -> 169.254.%u.%u\n",
         b[0], b[1], b[2], b[3], b[4], b[5], b[4], b[5]);
    return ip;
}
static void ah_peers(void)
{
    uint64_t now = xk_os_monotonic_us();
    if (now < ah_next_peers) return;
    ah_next_peers = now + 500000;
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
    SceNetAdhocctlPeerInfo peers[AH_PEERS];
    int len = sizeof peers;
    if (sceNetAdhocctlGetPeerList(&len, peers) < 0) return;
    if (len > (int)sizeof peers) len = sizeof peers;
    /* SDK writes a linked list inside the caller's buffer. Bound traversal and
     * check every pointer before dereferencing it. */
    SceNetAdhocctlPeerInfo *p = len >= (int)sizeof(*p) ? peers : NULL;
    for (int n = 0; p && n < AH_PEERS; n++, p = p->next) {
        uintptr_t off = (uintptr_t)p - (uintptr_t)peers;
        if (off > (unsigned)len - sizeof(*p) || off % _Alignof(SceNetAdhocctlPeerInfo)) break;
        ah_learn(&p->macAddr);
    }
}
static int ah_broadcast(uint32_t ip)
{
    /* Fake addresses use /16. Also recognize the old title's /24 subnet. */
    return ip == 0xffffffffu || ip == 0xfffffea9u || ip == 0xff00000au;
}
static int ah_address(uint32_t ip, SceNetEtherAddr *mac)
{
    if (ah_broadcast(ip)) { memset(mac, 255, 6); return 0; }
    ah_peers();
    for (int i = 0; i < AH_PEERS; i++) if (ah_peer[i].ip == ip && ip) { *mac = ah_peer[i].mac; return 0; }
    return WSAEADDRNOTAVAIL;
}
static int ah_error(int rc)
{
    if (rc >= 0) return 0;
    switch ((uint32_t)rc) {
    case SCE_ERROR_NET_ADHOC_WOULD_BLOCK:
    case SCE_ERROR_NET_ADHOC_TIMEOUT: return WSAEWOULDBLOCK;
    case SCE_ERROR_NET_ADHOC_PORT_IN_USE: return WSAEADDRINUSE;
    case SCE_ERROR_NET_ADHOC_INVALID_ADDR: return WSAEADDRNOTAVAIL;
    case SCE_ERROR_NET_ADHOC_NOT_CONNECTED: return WSAENOTCONN;
    case SCE_ERROR_NET_ADHOC_DISCONNECTED: return WSAECONNRESET;
    case SCE_ERROR_NET_ADHOC_CONNECTION_REFUSED: return WSAECONNREFUSED;
    case SCE_ERROR_NET_ADHOC_INVALID_DATALEN:
    case SCE_ERROR_NET_ADHOC_NOT_ENOUGH_SPACE: return WSAEMSGSIZE;
    case SCE_ERROR_NET_ADHOC_INVALID_SOCKET_ID: return WSAENOTSOCK;
    case SCE_ERROR_NET_ADHOC_INVALID_ARG: return WSAEINVAL;
    default: return WSAENETDOWN;
    }
}
static int ah_expired(nsock *s, uint64_t start, int send)
{
    uint32_t ms = send ? s->adhoc.send_ms : s->adhoc.recv_ms;
    /* No guest timeout: retain a finite cooperative wait (5s). */
    return s->nonblock || xk_os_monotonic_us() - start >= (ms ? (uint64_t)ms * 1000 : 5000000);
}
static int ah_pdp(nsock *s)
{
    if (s->adhoc.id >= 0) return 0;
    if (!s->bound) assign_port(s);
    int id = sceNetAdhocPdpCreate(&g_mac, htons16(s->lport), 32768, 0);
    if (id < 0) return ah_error(id);
    s->adhoc.id = id;
    NLOG("PDP socket %04X id %d port %u\n", NS_HANDLE + idx(s), id, htons16(s->lport));
    return 0;
}
static int ah_listen(nsock *s, int backlog)
{
    if (s->type != 1 || s->adhoc.id >= 0) return WSAEINVAL;
    if (!s->bound) assign_port(s);
    if (backlog < 1) backlog = 1; if (backlog > ACC_MAX) backlog = ACC_MAX;
    int id = sceNetAdhocPtpListen(&g_mac, htons16(s->lport), 16384, 200000, 20, backlog, 0);
    if (id < 0) return ah_error(id);
    s->adhoc.id = id;
    NLOG("PTP listener %04X id %d port %u\n", NS_HANDLE + idx(s), id, htons16(s->lport));
    return 0;
}
static void ah_poll(nsock *s)
{
    ah_peers();
    if (s->adhoc.id < 0 || s->adhoc.error) return;
    int rc;
    if (s->adhoc.connecting) {
        rc = sceNetAdhocPtpConnect(s->adhoc.id, 0, AH_NB);
        if (rc >= 0) { s->adhoc.connecting = 0; s->connected = 1; }
        else if (ah_error(rc) != WSAEWOULDBLOCK) { s->adhoc.connecting = 0; s->adhoc.error = ah_error(rc); }
        return;
    }
    if (s->listening) {
        if (s->acc_n >= ACC_MAX) return;
        SceNetEtherAddr mac; SceUShort16 port;
        rc = sceNetAdhocPtpAccept(s->adhoc.id, &mac, &port, 0, AH_NB);
        if (rc >= 0) {
            uint32_t ip = ah_learn(&mac);
            nsock *p = ip ? ns_alloc(1) : NULL;
            if (!p) { sceNetAdhocPtpClose(rc, 0); return; }
            p->adhoc.id = rc; p->connected = p->bound = 1;
            p->lip = s->lip; p->lport = s->lport; p->pip = ip; p->pport = htons16(port);
            s->acc[s->acc_n++] = idx(p);
            NLOG("PTP accepted %04X id %d peer %08X\n", NS_HANDLE + idx(p), rc, ip);
        }
    } else if (s->type == 2) {
        if (s->dq_n == DG_MAX) return;
        dgram *d = &s->dq[(s->dq_head + s->dq_n) % DG_MAX];
        SceNetEtherAddr mac; SceUShort16 port; int len = DG_BYTES;
        rc = sceNetAdhocPdpRecv(s->adhoc.id, &mac, &port, d->d, &len, 0, AH_NB);
        if (rc >= 0 && len >= 0 && len <= DG_BYTES) {
            uint32_t ip = ah_learn(&mac);
            if (ip && ip != g_ip) { d->len = len; d->sport = htons16(port); d->sip = ip; s->dq_n++; }
        }
    } else {
        if (!s->connected || s->peer_closed || s->rx_len == TCP_RING) return;
        unsigned tail = (s->rx_head + s->rx_len) % TCP_RING;
        int len = TCP_RING - tail;
        if ((unsigned)len > TCP_RING - s->rx_len) len = TCP_RING - s->rx_len;
        rc = sceNetAdhocPtpRecv(s->adhoc.id, s->rx + tail, &len, 0, AH_NB);
        if (rc >= 0) { if (!len) s->peer_closed = 1; else s->rx_len += len; }
    }
    if (rc < 0 && ah_error(rc) != WSAEWOULDBLOCK) s->adhoc.error = ah_error(rc);
}
static int ah_writable(nsock *s)
{
    if (s->listening || (s->type == 1 && !s->connected)) return 0;
    SceNetAdhocPollSd sd = { .id = s->adhoc.id, .events = SCE_NET_ADHOC_EV_SEND };
    int rc = sceNetAdhocPollSocket(&sd, 1, 0, AH_NB);
    return rc >= 0 && (sd.revents & SCE_NET_ADHOC_EV_SEND);
}
static int ah_connect(nsock *s, uint16_t port, uint32_t ip)
{
    if (s->adhoc.id < 0) {
        SceNetEtherAddr mac; int e = ah_address(ip, &mac); if (e) return e;
        if (!s->bound) assign_port(s);
        int id = sceNetAdhocPtpOpen(&g_mac, htons16(s->lport), &mac, htons16(port), 16384, 200000, 20, 0);
        if (id < 0) return ah_error(id);
        s->adhoc.id = id; s->adhoc.connecting = 1; s->pip = ip; s->pport = port;
        NLOG("PTP socket %04X id %d peer %08X port %u\n", NS_HANDLE + idx(s), id, ip, htons16(port));
    }
    uint64_t start = xk_os_monotonic_us();
    do {
        ah_poll(s);
        if (s->adhoc.error) return s->adhoc.error;
        if (s->connected) return 0;
        if (ah_expired(s, start, 1)) return WSAEWOULDBLOCK;
        xk_yield();
    } while (1);
}
static int ah_send(nsock *s, const void *data, int *len)
{
    if (*len < 0) return WSAEINVAL;
    if (!s->connected) return WSAENOTCONN;
    uint64_t start = xk_os_monotonic_us(); int requested = *len;
    do {
        *len = requested;
        int e = ah_error(sceNetAdhocPtpSend(s->adhoc.id, data, len, 0, AH_NB));
        if (e != WSAEWOULDBLOCK || ah_expired(s, start, 1)) return e;
        xk_yield();
    } while (1);
}
static int ah_udp_send(nsock *s, const void *data, uint32_t len, uint16_t port, uint32_t ip)
{
    if (ip == LOCAL_IP || ip == g_ip) return 0;
    SceNetEtherAddr mac; int e = ah_address(ip, &mac); if (e) return e;
    if ((e = ah_pdp(s))) return e;
    uint64_t start = xk_os_monotonic_us();
    do {
        e = ah_error(sceNetAdhocPdpSend(s->adhoc.id, &mac, htons16(port), data, len, 0, AH_NB));
        if (e != WSAEWOULDBLOCK || ah_expired(s, start, 1)) return e;
        xk_yield();
    } while (1);
}
static void put_addr(uint32_t a, uint16_t port, uint32_t ip);
static void ah_recv_udp(xctx *c, nsock *s, uint32_t n, uint32_t from, uint32_t fromlen)
{
    uint64_t start = xk_os_monotonic_us();
    do {
        ah_poll(s);
        if (s->dq_n) break;
        if (s->adhoc.error) { fail(c, s->adhoc.error); return; }
        if (ah_expired(s, start, 0)) { fail(c, WSAEWOULDBLOCK); return; }
        xk_yield();
    } while (1);
    dgram *d = &s->dq[s->dq_head]; unsigned r = d->len < n ? d->len : n;
    memcpy(X_G(X_ARG(1)), d->d, r);
    if (from) { put_addr(from, d->sport, d->sip); if (fromlen) X_M32(fromlen) = 16; }
    int trunc = d->len > n;
    if (!(X_ARG(3) & 2)) { s->dq_head = (s->dq_head + 1) % DG_MAX; s->dq_n--; }
    if (trunc) fail(c, WSAEMSGSIZE); else c->r[0] = r;
}

void xv_net_shutdown(void)
{
    if (!g_adhoc) return;
    for (int i = 0; i < NS_MAX; i++) if (g_s[i].used) ns_free(&g_s[i]);
    if (ah_control) { sceNetCtlAdhocDisconnect(); sceNetAdhocctlTerm(); ah_control = 0; }
    if (ah_transport) { sceNetAdhocTerm(); ah_transport = 0; }
    if (ah_module) { sceSysmoduleUnloadModule(SCE_SYSMODULE_PSPNET_ADHOC); ah_module = 0; }
    if (ah_ctl) { sceNetCtlTerm(); ah_ctl = 0; }
    if (ah_net) { sceNetTerm(); ah_net = 0; }
    if (ah_net_module) { sceSysmoduleUnloadModule(SCE_SYSMODULE_NET); ah_net_module = 0; }
}
/* Startup occurs before the game thread exists: the 65s connection-dialog
 * deadline is not charged to any Winsock/game timeout. draw renders via GXM. */
int xv_net_startup(void (*draw)(void))
{
    const char *e = getenv("XV_NET_ADHOC");
    g_adhoc = e && atoi(e) == 1;
    if (!g_adhoc) return 0;
    int rc;
#define AH_INIT(call, owned) do { rc = (call); XK_LOG("[net] %s -> %08X\n", #call, rc); if (rc < 0) goto failed; owned = 1; } while (0)
    AH_INIT(sceSysmoduleLoadModule(SCE_SYSMODULE_NET), ah_net_module);
    SceNetInitParam np = { .memory = ah_pool, .size = sizeof ah_pool, .flags = 0 };
    AH_INIT(sceNetInit(&np), ah_net);
    AH_INIT(sceNetCtlInit(), ah_ctl);
    AH_INIT(sceSysmoduleLoadModule(SCE_SYSMODULE_PSPNET_ADHOC), ah_module);
    AH_INIT(sceNetAdhocInit(), ah_transport);
    SceNetAdhocctlAdhocId aid = { .type = SCE_NET_ADHOCCTL_ADHOCTYPE_PRODUCT_ID, .data = {'X','I','T','A','A','D','H','0','1'} };
    AH_INIT(sceNetAdhocctlInit(&aid), ah_control);
#undef AH_INIT
    SceCommonDialogConfigParam cp;
    sceCommonDialogConfigParamInit(&cp);
    if (sceCommonDialogSetConfigParam(&cp) < 0) goto failed;
    SceNetAdhocctlGroupName group = { .data = "XITA" };
    SceNetCheckDialogParam param;
    sceNetCheckDialogParamInit(&param);
    param.mode = SCE_NETCHECK_DIALOG_MODE_PSP_ADHOC_CONN;
    param.groupName = &group; param.timeoutUs = 60000000;
    if (sceNetCheckDialogInit(&param) < 0) goto failed;
    uint64_t deadline = sceKernelGetProcessTimeWide() + 65000000;
    int status;
    while ((status = sceNetCheckDialogGetStatus()) == SCE_COMMON_DIALOG_STATUS_RUNNING && sceKernelGetProcessTimeWide() < deadline) {
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
        draw();
    }
    SceNetCheckDialogResult result = {0};
    rc = status == SCE_COMMON_DIALOG_STATUS_FINISHED ? sceNetCheckDialogGetResult(&result) : -1;
    if (rc < 0) sceNetCheckDialogAbort();
    sceNetCheckDialogTerm();
    if (rc < 0 || result.result != SCE_COMMON_DIALOG_RESULT_OK) goto failed;
    SceNetAdhocctlParameter parameter = {0};
    if (sceNetAdhocctlGetParameter(&parameter) < 0 || memcmp(parameter.groupName.data, "XITA\0\0\0\0", 8)) goto failed;
    if (sceNetAdhocctlGetEtherAddr(&g_mac) < 0) goto failed;
    g_ip = ah_ip(&g_mac);
    XK_LOG("[net] ad-hoc XITA ready, local 169.254.%u.%u\n", ((uint8_t *)&g_mac)[4], ((uint8_t *)&g_mac)[5]);
    return 0;
failed:
    XK_LOG("[net] ad-hoc initialization failed; restart to retry\n");
    xv_net_shutdown();
    return -1;
}
