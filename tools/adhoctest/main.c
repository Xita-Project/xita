#include "screen.h"
#include <stdint.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/adhoc_matching.h>
#include <psp2/pspnet_adhoc.h>
#include <psp2/netcheck_dialog.h>

#define PDP_PORT 31000
#define MATCH_PORT 31001
#define PTP_PORT 31002
#define NB SCE_NET_ADHOC_F_NONBLOCK
#define SECOND UINT64_C(1000000)
#define MAGIC UINT32_C(0x41544958)
enum { BEACON=1, PING, ECHO };
/* Explicit wire offsets, little endian; no struct padding or foreign clocks. */
typedef struct { uint8_t bytes[64]; } Packet;
_Static_assert(sizeof(Packet) == 64, "wire packet size");
static uint8_t net_pool[1024*1024] __attribute__((aligned(64)));
static uint8_t matching_pool[128*1024] __attribute__((aligned(64)));
static uint8_t stream_tx[4096], stream_rx[4096];
static uint64_t sent_at[100];
static uint8_t received[100], sent_ok[100];
static SceNetEtherAddr local, peer;
static int quitting, have_peer, pdp = -1;
static uint64_t peer_time, session;
static uint32_t match_events, match_type, match_ip;
static uint64_t now(void) { return sceKernelGetProcessTimeWide(); }
static void put32(uint8_t *p, uint32_t n)
{ for (int i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8)); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put64(uint8_t *p, uint64_t n)
{ put32(p, (uint32_t)n); put32(p+4, (uint32_t)(n>>32)); }
static uint64_t get64(const uint8_t *p)
{ return get32(p)|((uint64_t)get32(p+4)<<32); }
static void mac_log(const char *label, const SceNetEtherAddr *mac)
{
    char s[32] = {0};
    CALL(sceNetEtherNtostr(mac, s, sizeof(s)));
    log_line("%s %s", label, s);
}
static int retryable(int rc)
{
    return (uint32_t)rc == SCE_ERROR_NET_ADHOC_WOULD_BLOCK ||
           (uint32_t)rc == SCE_ERROR_NET_ADHOC_TIMEOUT;
}
static void matching_callback(int id, int type, SceNetInAddr *addr, SceSize len, void *opt)
{
    (void)id; (void)len; (void)opt;
    /* Callback thread never touches the file or framebuffer. Diagnostic snapshot
       only; event names are not exposed by this SDK, so no invented event IDs. */
    __atomic_store_n(&match_type, (uint32_t)type, __ATOMIC_RELAXED);
    __atomic_store_n(&match_ip, addr ? addr->s_addr : 0, __ATOMIC_RELAXED);
    __atomic_fetch_add(&match_events, 1, __ATOMIC_RELEASE);
}
static int pump(int dialog)
{
    static uint64_t next_power, next_draw;
    static uint32_t last_events;
    SceCtrlData pad = {0};
    int rc = sceCtrlPeekBufferPositive(0, &pad, 1);
    if (rc < 0) { result("sceCtrlPeekBufferPositive", rc); quitting=1; }
    if (pad.buttons & SCE_CTRL_CROSS) quitting=1;
    uint64_t t=now();
    if (t >= next_power) {
        CALL(sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT));
        next_power=t+SECOND;
    }
    uint32_t events=__atomic_load_n(&match_events, __ATOMIC_ACQUIRE);
    if (events != last_events) {
        log_line("Matching callbacks=%u latest type=%u addr=0x%08X", (unsigned)events,
            (unsigned)__atomic_load_n(&match_type, __ATOMIC_RELAXED),
            (unsigned)__atomic_load_n(&match_ip, __ATOMIC_RELAXED));
        last_events=events;
    }
    if (t >= next_draw) {
        if (screen_draw(dialog)<0) quitting=1;
        next_draw=t+100000;
    }
    sceKernelDelayThread(10000);
    return !quitting;
}
static Packet packet(unsigned kind, unsigned seq)
{
    Packet p = {{0}};
    put32(p.bytes, MAGIC); put32(p.bytes+4, kind); put32(p.bytes+8, seq);
    put64(p.bytes+16, now()); put64(p.bytes+24, session);
    for (unsigned i=32;i<64;i++) p.bytes[i]=(uint8_t)(i ^ seq);
    return p;
}
static int send_packet(const SceNetEtherAddr *to, const Packet *p)
{
    int rc=CALL(sceNetAdhocPdpSend(pdp, to, PDP_PORT, p, sizeof(*p), 0, NB));
    log_line("PDP TX kind=%u seq=%u", (unsigned)get32(p->bytes+4), (unsigned)get32(p->bytes+8));
    return rc;
}
static unsigned replies, successful;
static uint64_t rtt_min=UINT64_MAX, rtt_max, rtt_sum;
static int stats_done;
static void service_pdp(void)
{
    /* Bounded draining prevents a busy peer from starving Cross/power ticks. */
    for (int i=0;i<8;i++) {
        Packet p;
        SceNetEtherAddr source;
        SceUShort16 port=0;
        int len=sizeof(p);
        int rc=CALL(sceNetAdhocPdpRecv(pdp, &source, &port, &p, &len, 0, NB));
        uint64_t arrival=now();
        if (rc < 0) {
            if (!retryable(rc)) log_line("PDP receive error; continuing until deadline");
            break;
        }
        if (len != sizeof(p) || port != PDP_PORT || get32(p.bytes) != MAGIC ||
            !memcmp(&source, &local, sizeof(local))) continue;
        unsigned kind=get32(p.bytes+4), seq=get32(p.bytes+8);
        if (kind < BEACON || kind > ECHO) continue;
        if (!have_peer && kind != ECHO) {
            peer=source; have_peer=1; peer_time=arrival;
            mac_log("Peer MAC", &peer);
        }
        if (!have_peer || memcmp(&source, &peer, sizeof(peer))) continue;
        if (kind == PING) { put32(p.bytes+4, ECHO); send_packet(&source, &p); }
        if (kind == ECHO && !stats_done && seq < 100 && sent_ok[seq] &&
            !received[seq] && get64(p.bytes+24) == session &&
            get64(p.bytes+16) == sent_at[seq]) {
            int valid=1;
            for (unsigned b=32;b<64;b++) if(p.bytes[b] != (uint8_t)(b^seq)) valid=0;
            if (!valid) { log_line("Corrupt PDP echo seq=%u", seq); continue; }
            uint64_t rtt=arrival-sent_at[seq];
            received[seq]=1; replies++; rtt_sum+=rtt;
            if (rtt<rtt_min) rtt_min=rtt;
            if (rtt>rtt_max) rtt_max=rtt;
            log_line("PDP echo seq=%u RTT=%llu us", seq, (unsigned long long)rtt);
        }
    }
}
static int connect_group(void)
{
    SceNetAdhocctlGroupName group = { .data="XITA" };
    SceNetCheckDialogParam param;
    sceNetCheckDialogParamInit(&param);
    param.mode=SCE_NETCHECK_DIALOG_MODE_PSP_ADHOC_CONN;
    param.groupName=&group;
    param.timeoutUs=60*1000000;
    if (CALL(sceNetCheckDialogInit(&param)) < 0) return -1;
    uint64_t deadline=now()+65*SECOND;
    int status;
    do {
        status=CALL(sceNetCheckDialogGetStatus());
        if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) break;
        if (!pump(1) || now()>=deadline) {
            CALL(sceNetCheckDialogAbort());
            CALL(sceNetCheckDialogTerm());
            return -1;
        }
    } while (status == SCE_COMMON_DIALOG_STATUS_RUNNING);
    SceNetCheckDialogResult res = {0};
    int rc=CALL(sceNetCheckDialogGetResult(&res));
    result("NetCheck result.result", res.result);
    CALL(sceNetCheckDialogTerm());
    return rc < 0 || res.result != SCE_COMMON_DIALOG_RESULT_OK ? -1 : 0;
}
static void ptp_test(void)
{
    screen_status(3, "PTP pending");
    int server=memcmp(&local, &peer, sizeof(local)) < 0;
    int listener=-1, socket=-1, connected=0;
    uint64_t start=now(), deadline=start+30*SECOND;
    log_line("PTP role: %s (lower MAC listens)", server ? "listener" : "connector");
    if (server) {
        listener=CALL(sceNetAdhocPtpListen(&local, PTP_PORT, 16384, 200000, 20, 1, 0));
        if (listener < 0) { screen_status(3, "PTP listen FAILED"); goto done; }
    } else {
        socket=CALL(sceNetAdhocPtpOpen(&local, PTP_PORT+1, &peer, PTP_PORT, 16384, 200000, 20, 0));
        if (socket < 0) { screen_status(3, "PTP open FAILED"); goto done; }
    }
    while (pump(0) && now()<deadline) {
        service_pdp();
        int rc;
        if (server) {
            SceNetEtherAddr addr;
            SceUShort16 port;
            rc=CALL(sceNetAdhocPtpAccept(listener, &addr, &port, 0, NB));
            if (rc >= 0) {
                if (memcmp(&addr, &peer, sizeof(peer))) {
                    CALL(sceNetAdhocPtpClose(rc, 0));
                    continue;
                }
                socket=rc; connected=1; break;
            }
        } else {
            /* Let a peer with a slightly later PDP deadline enter listen first. */
            if (now()-start < 2*SECOND) continue;
            rc=CALL(sceNetAdhocPtpConnect(socket, 0, NB));
            if (rc >= 0) { connected=1; break; }
        }
        if (!retryable(rc)) break;
    }
    if (!connected) { screen_status(3, "PTP connection FAILED or timed out"); goto done; }
    for (unsigned i=0;i<sizeof(stream_tx);i++) stream_tx[i]=(uint8_t)(i*37+11);
    memset(stream_rx, 0, sizeof(stream_rx));
    size_t tx=0, rx=0;
    uint64_t echo_start=now();
    deadline=echo_start+15*SECOND;
    while (pump(0) && now()<deadline) {
        service_pdp();
        int rc, len;
        if ((!server || rx == sizeof(stream_rx)) && tx<sizeof(stream_tx)) {
            len=sizeof(stream_tx)-tx;
            const uint8_t *data=server ? stream_rx : stream_tx;
            rc=CALL(sceNetAdhocPtpSend(socket, data+tx, &len, 0, NB));
            if (rc >= 0 && len>0 && (size_t)len<=sizeof(stream_tx)-tx) tx+=(size_t)len;
            else if (rc >= 0 || !retryable(rc)) break;
        }
        if (rx<sizeof(stream_rx)) {
            len=sizeof(stream_rx)-rx;
            rc=CALL(sceNetAdhocPtpRecv(socket, stream_rx+rx, &len, 0, NB));
            if (rc >= 0 && len>0 && (size_t)len<=sizeof(stream_rx)-rx) rx+=(size_t)len;
            else if (rc >= 0 || !retryable(rc)) break;
        }
        if (tx==sizeof(stream_tx) && rx==sizeof(stream_rx)) break;
    }
    /* Flush the server's echo before closing, including partial flush retries. */
    int flushed=0;
    while (!quitting && tx==sizeof(stream_tx) && now()<deadline) {
        int rc=CALL(sceNetAdhocPtpFlush(socket, 0, NB));
        if (rc >= 0) { flushed=1; break; }
        if (!retryable(rc)) break;
        service_pdp(); pump(0);
    }
    screen_status(3, "PTP 4KB %s: TX=%u RX=%u flush=%d elapsed=%llu us",
        tx==4096 && rx==4096 && flushed && !memcmp(stream_tx, stream_rx, 4096) ? "PASS" : "FAIL",
        (unsigned)tx, (unsigned)rx, flushed, (unsigned long long)(now()-echo_start));
    if (server) log_line("Listener result verifies request and flush; peer verifies echo.");
done:
    if (socket >= 0) CALL(sceNetAdhocPtpClose(socket, 0));
    if (listener >= 0) CALL(sceNetAdhocPtpClose(listener, 0));
}
int main(void)
{
    int net_module=0, adhoc_module=0, matching_module=0;
    int net=0, ctl=0, adhoc=0, actl=0, match=0, matching=-1, started=0, joined=0;
    if (screen_init()<0) {
        screen_status(0,"Graphics setup failed. Network test did not start.");
        screen_status(1,"See adhoc.log for the failing call. Cross exits.");
        goto failed;
    }
    if (screen_draw(0)<0) goto cleanup;
    log_line("New Xita AdHoc Test run. Cross exits. No router required.");
    session=now();
    if (CALL(sceSysmoduleLoadModule(SCE_SYSMODULE_NET))<0) goto failed;
    net_module=1;
    SceNetInitParam np = { .memory=net_pool, .size=sizeof(net_pool), .flags=0 };
    if (CALL(sceNetInit(&np))<0) goto failed;
    net=1;
    if (CALL(sceNetCtlInit())<0) goto failed;
    ctl=1;
    if (CALL(sceSysmoduleLoadModule(SCE_SYSMODULE_PSPNET_ADHOC))<0) goto failed;
    adhoc_module=1;
    if (CALL(sceNetAdhocInit())<0) goto failed;
    adhoc=1;
    SceNetAdhocctlAdhocId aid = { .type=SCE_NET_ADHOCCTL_ADHOCTYPE_PRODUCT_ID, .data={'X','I','T','A','A','D','H','0','1'} };
    if (CALL(sceNetAdhocctlInit(&aid))<0) goto failed;
    actl=1;
    if (connect_group()<0) goto failed;
    joined=1;
    if (CALL(sceNetAdhocctlGetEtherAddr(&local))<0) goto failed;
    mac_log("Local MAC", &local);
    SceNetAdhocctlParameter parameter = {0};
    if (CALL(sceNetAdhocctlGetParameter(&parameter))<0) goto failed;
    log_line("Connected group=%.8s channel=%d", parameter.groupName.data, parameter.channel);
    if (memcmp(parameter.groupName.data, "XITA\0\0\0\0", 8)) {
        log_line("Unexpected group; stopping"); goto failed;
    }
    /* Native matching uses IP addresses, unlike the MAC-based PSP transport.
       Start it as a diagnostic, and use beacons for the PDP address exchange. */
    if (CALL(sceSysmoduleLoadModule(SCE_SYSMODULE_NET_ADHOC_MATCHING))>=0) {
        matching_module=1;
        if (CALL(sceNetAdhocMatchingInit(sizeof(matching_pool), matching_pool))>=0) {
            match=1;
            matching=CALL(sceNetAdhocMatchingCreate(SCE_NET_ADHOC_MATCHING_MODE_P2P,
                2, MATCH_PORT, 8192, 500000, 2000000, 3, 500000, matching_callback));
            if (matching>=0) {
                static char hello[]="XITA";
                if (CALL(sceNetAdhocMatchingStart(matching, 0x10000100, 16384, 0,
                    sizeof(hello), hello))>=0) started=1;
            }
        }
    }
    log_line("Matching started=%d; PDP MAC beacons every 500ms", started);
    pdp=CALL(sceNetAdhocPdpCreate(&local, PDP_PORT, 32768, 0));
    if (pdp<0) goto failed;
    SceNetEtherAddr broadcast;
    memset(&broadcast, 0xff, sizeof(broadcast));
    uint64_t deadline=now()+90*SECOND, next_beacon=0, next_ping=0, finish=0;
    unsigned attempts=0;
    while (pump(0)) {
        uint64_t t=now();
        if (t>=next_beacon) {
            Packet beacon=packet(BEACON, 0); send_packet(&broadcast, &beacon);
            next_beacon=t+500000;
        }
        service_pdp();
        if (!have_peer) {
            if (t>=deadline) { log_line("Discovery timed out after 90 seconds"); goto failed; }
            continue;
        }
        /* One second of beacons lets both sides learn the MAC before probes. */
        if (t>=peer_time+SECOND && attempts<100 && t>=next_ping) {
            Packet p=packet(PING, attempts);
            sent_at[attempts]=get64(p.bytes+16);
            if (send_packet(&peer, &p)>=0) { sent_ok[attempts]=1; successful++; }
            attempts++; next_ping=t+100000;
            if (attempts==100) finish=now()+2*SECOND;
        }
        if (attempts==100 && now()>=finish) break;
    }
    stats_done=1;
    screen_status(0, "PDP attempts=%u sent=%u replies=%u send-errors=%u",
        attempts, successful, replies, attempts-successful);
    screen_status(1, "PDP loss=%u/%u (successful sends without echo); total missed=%u/%u",
        successful-replies, successful, attempts-replies, attempts);
    if (replies) screen_status(2, "RTT min/avg/max = %llu/%llu/%llu us",
        (unsigned long long)rtt_min, (unsigned long long)(rtt_sum/replies), (unsigned long long)rtt_max);
    else screen_status(2, "RTT unavailable: no valid echoes");
    if (!quitting) ptp_test();
    log_line("Tests finished. Echo service remains active. Cross exits.");
    while (pump(0)) service_pdp();
    goto cleanup;
failed:
    log_line("Test stopped: inspect call results above. Cross exits; relaunch to retry.");
    while (pump(0)) {}
cleanup:
    if (pdp>=0) CALL(sceNetAdhocPdpDelete(pdp, 0));
    if (started) CALL(sceNetAdhocMatchingStop(matching));
    if (matching>=0) CALL(sceNetAdhocMatchingDelete(matching));
    if (match) CALL(sceNetAdhocMatchingTerm());
    if (matching_module) CALL(sceSysmoduleUnloadModule(SCE_SYSMODULE_NET_ADHOC_MATCHING));
    if (joined) CALL(sceNetCtlAdhocDisconnect());
    if (actl) CALL(sceNetAdhocctlTerm());
    if (adhoc) CALL(sceNetAdhocTerm());
    if (adhoc_module) CALL(sceSysmoduleUnloadModule(SCE_SYSMODULE_PSPNET_ADHOC));
    if (ctl) { sceNetCtlTerm(); log_line("sceNetCtlTerm = void"); }
    if (net) CALL(sceNetTerm());
    if (net_module) CALL(sceSysmoduleUnloadModule(SCE_SYSMODULE_NET));
    screen_close();
    sceKernelExitProcess(0);
    return 0;
}
