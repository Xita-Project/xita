/* xk_xapi.c - kernel init, thunk table rewrite, Hal/Phy/Xe/Interlocked exports, and the XAPI/XNet HLE
 * functions that are not simply lifted (launch info, input devices, dashboard, debug output). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xk.h"

void xk_threads_init(uint32_t tls_dir);

/* generated tables (xv_fn_table.c) */
typedef struct { uint32_t slot, ordinal; } xv_kimport_t;
extern const xv_kimport_t xv_kernel_imports[];
extern const unsigned xv_kernel_imports_count;
extern const char *const xv_kernel_names[367];
extern xv_fn_t xv_kernel_dispatch(unsigned ordinal);

static const char *g_game_dir = "haloce", *g_save_dir = "save";

/* ---- kernel data exports live in guest memory so `mov eax,[thunk]; mov eax,[eax]` works ---- */
static uint32_t data_export_var(unsigned ordinal)
{
    switch (ordinal) {
    case 156: return xk_var_KeTickCount;
    case 322: return xk_var_XboxHardwareInfo;
    case 164: return xk_var_LaunchDataPage;
    case 324: return xk_var_XboxKrnlVersion;
    case 40:  return xk_var_HalDiskCachePartitionCount;
    default:  return 0;
    }
}

/* XV_LEVEL=<code>: level select without a save.  The campaign's first entry is the scenario name string
 * "levels\a10\a10" in three level tables inside the image; overwriting it with e.g. "levels\b30\b30"
 * (same length: all campaign codes are three characters) makes "The Pillar of Autumn" start that level
 * through the game's own cache lookup, so the cache validation that defeated a file-level redirect passes. */
static void xk_level_select(void)
{
    const char *lvl = getenv("XV_LEVEL");
    if (!lvl || !*lvl || !strcmp(lvl, "a10")) return;
    static const uint32_t sites[] = { 0x002078A8u, 0x002091ECu, 0x0020F938u };   /* Halo 3925 level tables (16-byte entries) */
    /* 3-letter code -> levels\xxx\xxx; anything else is written as given (<= 15 chars), e.g. XV_LEVEL=x\bloodgulch:
     * the game takes the map file name from the last path component */
    char to[16];
    if (strlen(lvl) == 3) snprintf(to, sizeof to, "levels\\%s\\%s", lvl, lvl); else { if (strlen(lvl) > 15) { XK_LOG("level select: '%s' too long (max 15)\n", lvl); return; } snprintf(to, sizeof to, "%s", lvl); }
    int n = 0;
    for (unsigned i = 0; i < 3; ++i) if (memcmp(X_G(sites[i]), "levels\\a10\\a10", 14) == 0) { memset(X_G(sites[i]), 0, 16); memcpy(X_G(sites[i]), to, strlen(to)); n++; }
    XK_LOG("level select: %s patched into %d table(s) - campaign mission 1 now loads it\n", to, n);
}

void xk_thunks_init(void)
{
    xk_level_select();
    xk_var_KeTickCount = xk_kalloc(4);
    xk_var_XboxHardwareInfo = xk_kalloc(16);        /* { Flags, GpuRevision, McpRevision, reserved } */
    X_M32(xk_var_XboxHardwareInfo) = 0x00000002u;   /* XBOX_HW_FLAG_INTERNAL_USB_HUB? keep retail bits: 0 */
    X_M32(xk_var_XboxHardwareInfo) = 0;
    X_M8(xk_var_XboxHardwareInfo + 4) = 0xA2; X_M8(xk_var_XboxHardwareInfo + 5) = 0xD3;
    xk_var_LaunchDataPage = xk_kalloc(4); X_M32(xk_var_LaunchDataPage) = 0;   /* PLAUNCH_DATA_PAGE = NULL: cold boot */
    xk_var_XboxKrnlVersion = xk_kalloc(8); X_M16(xk_var_XboxKrnlVersion) = 1; X_M16(xk_var_XboxKrnlVersion + 2) = 0; X_M16(xk_var_XboxKrnlVersion + 4) = 3944; X_M16(xk_var_XboxKrnlVersion + 6) = 1;
    xk_var_HalDiskCachePartitionCount = xk_kalloc(4); X_M32(xk_var_HalDiskCachePartitionCount) = 3;
    unsigned data = 0, funcs = 0;
    for (unsigned i = 0; i < xv_kernel_imports_count; ++i) {
        uint32_t slot = xv_kernel_imports[i].slot; unsigned ord = xv_kernel_imports[i].ordinal;
        uint32_t var = data_export_var(ord);
        if (var) { X_M32(slot) = var; data++; }
        else { X_M32(slot) = 0xFE000000u | ord; funcs++; }      /* magic target -> xv_call dispatches to xk_<name> */
    }
    XK_LOG("thunks: %u function imports, %u data imports\n", funcs, data);
}

void xk_init(uint32_t image_base, uint32_t image_size, uint32_t tls_dir, const char *game_dir, const char *save_dir)
{
    if (game_dir) g_game_dir = game_dir;
    if (save_dir) g_save_dir = save_dir;
    xk_threads_init(tls_dir);            /* memory was set up by xk_mem_setup() before the image was loaded */
    xk_thunks_init();
    char udata[512], tdata[512], cache[512];
    snprintf(udata, sizeof udata, "%s/udata", g_save_dir);
    snprintf(tdata, sizeof tdata, "%s/tdata", g_save_dir);
    snprintf(cache, sizeof cache, "%s/cache", g_save_dir);
    xk_os_mkdir(g_save_dir); xk_os_mkdir(udata); xk_os_mkdir(tdata); xk_os_mkdir(cache);
    xk_path_mount("\\device\\cdrom0", g_game_dir);
    xk_path_mount("\\device\\harddisk0\\partition1", udata);          /* E: (udata/tdata live here) */
    xk_path_mount("\\device\\harddisk0\\partition2", g_game_dir);     /* C: (dashboard) -> not needed */
    xk_path_mount("\\device\\harddisk0\\partition3", cache);          /* X: Y: Z: cache partitions */
    xk_path_mount("\\device\\harddisk0\\partition4", cache);
    xk_path_mount("\\device\\harddisk0\\partition5", cache);
    xk_path_add_link("D:", "\\Device\\CdRom0");
    xk_path_add_link("E:", "\\Device\\Harddisk0\\Partition1");
    xk_path_add_link("T:", "\\Device\\Harddisk0\\Partition1");
    xk_path_add_link("U:", "\\Device\\Harddisk0\\Partition1");
    xk_path_add_link("Z:", "\\Device\\Harddisk0\\Partition3");
    xk_path_add_link("CdRom0:", "\\Device\\CdRom0");
    XK_LOG("kernel up: image %08X+%X, game dir %s, save dir %s\n", image_base, image_size, g_game_dir, g_save_dir);
}

/* ---- dispatch hook used by xv_call for 0xFE0000nn targets ---------------------------------- */
int xk_dispatch_magic(xctx *c, uint32_t target)
{
    if ((target & 0xFFFF0000u) != 0xFE000000u) return 0;
    unsigned ord = target & 0xFFFF;
    xv_fn_t fn = xv_kernel_dispatch(ord);
    if (!fn) { XK_LOG("kernel export ordinal %u (%s) not implemented\n", ord, ord < 367 && xv_kernel_names[ord] ? xv_kernel_names[ord] : "?"); c->r[0] = STATUS_NOT_IMPLEMENTED; c->r[4] += 4; return 1; }
    fn(c); return 1;
}

/* ---- Hal / Phy / Xe / Av ---------------------------------------------------------------------- */
void xk_HalGetInterruptVector(xctx *c) { if (X_ARG(1)) X_M8(X_ARG(1)) = 5; c->r[0] = 0x30 + X_ARG(0); X_RET(2); }
void xk_HalRegisterShutdownNotification(xctx *c) { X_RET(2); }
void xk_HalReturnToFirmware(xctx *c) { XK_LOG("HalReturnToFirmware(%u) - game requested exit\n", X_ARG(0)); exit(0); }
void xk_HalInitiateShutdown(xctx *c) { XK_LOG("HalInitiateShutdown\n"); exit(0); }
void xk_HalReadSMBusValue(xctx *c) { if (X_ARG(3)) X_M32(X_ARG(3)) = 0; c->r[0] = STATUS_SUCCESS; X_RET(4); }
void xk_HalWriteSMBusValue(xctx *c) { c->r[0] = STATUS_SUCCESS; X_RET(4); }
void xk_HalReadSMCTrayState(xctx *c) { if (X_ARG(0)) X_M32(X_ARG(0)) = 0x40; if (X_ARG(1)) X_M32(X_ARG(1)) = 0; c->r[0] = STATUS_SUCCESS; X_RET(2); }   /* tray closed, media detected */
void xk_HalIsResetOrShutdownPending(xctx *c) { c->r[0] = 0; X_RET(0); }
void xk_HalEnableSecureTrayEject(xctx *c) { X_RET(0); }
void xk_HalWriteSMCScratchRegister(xctx *c) { X_RET(1); }
void xk_HalReadWritePCISpace(xctx *c) { X_RET(6); }
void xk_HalDisableSystemInterrupt(xctx *c) { X_RET(1); }
void xk_HalEnableSystemInterrupt(xctx *c) { X_RET(2); }
void xk_PhyGetLinkState(xctx *c) { c->r[0] = 0; X_RET(1); }
void xk_PhyInitialize(xctx *c) { c->r[0] = STATUS_SUCCESS; X_RET(2); }
void xk_XeLoadSection(xctx *c) { X_M16(X_ARG(0) + 0x10) += 1; c->r[0] = STATUS_SUCCESS; X_RET(1); }       /* everything is resident; bump SectionReferenceCount */
void xk_XeUnloadSection(xctx *c) { if (X_M16(X_ARG(0) + 0x10)) X_M16(X_ARG(0) + 0x10) -= 1; c->r[0] = STATUS_SUCCESS; X_RET(1); }
void xk_AvSetDisplayMode(xctx *c) { c->r[0] = STATUS_SUCCESS; X_RET(6); }
void xk_AvGetSavedDataAddress(xctx *c) { c->r[0] = 0; X_RET(0); }
void xk_AvSetSavedDataAddress(xctx *c) { X_RET(1); }
void xk_AvSendTVEncoderOption(xctx *c) { if (X_ARG(3)) X_M32(X_ARG(3)) = 0; X_RET(4); }
void xk_ExQueryNonVolatileSetting(xctx *c)
{
    uint32_t index = X_ARG(0), ptype = X_ARG(1), val = X_ARG(2), len = X_ARG(3), plen = X_ARG(4);
    uint32_t v = 0, n = 4;
    switch (index) {
    case 0x101: v = 0x00000400; break;              /* XC_LANGUAGE: english=1 */
    case 0x102: v = 0x00400000; break;              /* XC_VIDEO_FLAGS: widescreen/480p bits */
    case 0x103: v = 0x00000000; break;              /* XC_AUDIO_FLAGS: stereo */
    case 0x109: v = 0x00000000; break;              /* XC_TIMEZONE_BIAS */
    case 0x0F7: v = 0x00000000; break;              /* XC_FACTORY_AV_REGION */
    case 0x0F9: v = 0x00000001; break;              /* XC_FACTORY_GAME_REGION: NA */
    case 0x104: v = 0x00000001; break;              /* XC_PARENTAL_CONTROL_GAMES: allow all */
    default: v = 0; break;
    }
    if (index == 0x101) v = 1;
    if (ptype) X_M32(ptype) = 4;
    if (val && len >= 4) X_M32(val) = v;
    if (plen) X_M32(plen) = n;
    c->r[0] = STATUS_SUCCESS; X_RET(5);
}
void xk_ExSaveNonVolatileSetting(xctx *c) { c->r[0] = STATUS_SUCCESS; X_RET(4); }
void xk_DbgBreakPoint(xctx *c) { XK_LOG("DbgBreakPoint\n"); X_RET(0); }
void xk_KeInitializeApc(xctx *c) { X_RET(8); }
void xk_KeInsertQueueApc(xctx *c) { c->r[0] = 0; X_RET(4); }

/* fastcall Interlocked*: ecx = destination (guest address), edx = value */
void xk_InterlockedIncrement(xctx *c) { c->r[0] = ++X_M32(c->r[1]); X_RET(0); }
void xk_InterlockedDecrement(xctx *c) { c->r[0] = --X_M32(c->r[1]); X_RET(0); }
void xk_InterlockedExchange(xctx *c) { uint32_t o = X_M32(c->r[1]); X_M32(c->r[1]) = c->r[2]; c->r[0] = o; X_RET(0); }
void xk_InterlockedExchangeAdd(xctx *c) { uint32_t o = X_M32(c->r[1]); X_M32(c->r[1]) = o + c->r[2]; c->r[0] = o; X_RET(0); }
void xk_InterlockedCompareExchange(xctx *c) { uint32_t o = X_M32(c->r[1]); uint32_t comperand = X_ARG(0); if (o == comperand) X_M32(c->r[1]) = c->r[2]; c->r[0] = o; X_RET(1); }
void xk_InterlockedPushEntrySList(xctx *c) { uint32_t head = c->r[1], e = c->r[2]; uint32_t first = X_M32(head); X_M32(e) = first; X_M32(head) = e; X_M16(head + 4)++; c->r[0] = first; X_RET(0); }
void xk_InterlockedPopEntrySList(xctx *c) { uint32_t head = c->r[1]; uint32_t first = X_M32(head); if (first) { X_M32(head) = X_M32(first); X_M16(head + 4)--; } c->r[0] = first; X_RET(0); }
void xk_InterlockedFlushSList(xctx *c) { uint32_t head = c->r[1]; c->r[0] = X_M32(head); X_M32(head) = 0; X_M16(head + 4) = 0; X_RET(0); }
void xk_ExfInterlockedInsertHeadList(xctx *c) { uint32_t h = c->r[1], e = c->r[2]; uint32_t f = X_M32(h); X_M32(e) = f; X_M32(e + 4) = h; X_M32(f + 4) = e; X_M32(h) = e; c->r[0] = f == h ? 0 : f; X_RET(0); }
void xk_ExfInterlockedInsertTailList(xctx *c) { uint32_t h = c->r[1], e = c->r[2]; uint32_t b = X_M32(h + 4); X_M32(e) = h; X_M32(e + 4) = b; X_M32(b) = e; X_M32(h + 4) = e; c->r[0] = b == h ? 0 : b; X_RET(0); }
void xk_ExfInterlockedRemoveHeadList(xctx *c) { uint32_t h = c->r[1]; uint32_t f = X_M32(h); if (f == h) { c->r[0] = 0; X_RET(0); } uint32_t n = X_M32(f); X_M32(h) = n; X_M32(n + 4) = h; c->r[0] = f; X_RET(0); }
void xk_ExInterlockedAddLargeStatistic(xctx *c) { X_M64(c->r[1]) += c->r[2]; X_RET(0); }
void xk_ExInterlockedCompareExchange64(xctx *c) { uint64_t o = X_M64(c->r[1]); uint64_t ex = X_M64(c->r[2]); uint64_t cmp = X_M64(X_ARG(0)); if (o == cmp) X_M64(c->r[1]) = ex; c->r[0] = (uint32_t)o; c->r[2] = (uint32_t)(o >> 32); X_RET(1); }
void xk_KiUnlockDispatcherDatabase(xctx *c) { X_RET(0); }

/* ---- XAPI HLE ----------------------------------------------------------------------------------- */
/* DWORD XGetLaunchInfo(PDWORD pdwLaunchDataType, PLAUNCH_DATA pLaunchData) */
void xv_hle_XGetLaunchInfo(xctx *c) { c->r[0] = 1168; X_RET(2); }                       /* ERROR_NOT_FOUND: cold boot */
/* BOOL XMountUtilityDrive(BOOL fFormatClean): Z: is pre-linked to the cache partition at kernel init; the
 * default stub returned FALSE, which XapiInitProcess treats as fatal (XLaunchNewImageA to the dashboard). */
void xv_hle_XMountUtilityDrive(xctx *c) { xk_path_add_link("Z:", "\\Device\\Harddisk0\\Partition3"); XK_LOG("XMountUtilityDrive(%u) -> Z:\n", X_ARG(0)); c->r[0] = 1; X_RET(1); }
void xv_hle_XUnmountUtilityDrive(xctx *c) { c->r[0] = 1; X_RET(0); }
void xv_hle_XInitDevices(xctx *c) { XK_LOG("XInitDevices(%u prealloc types)\n", X_ARG(0)); X_RET(2); }
static int g_pad_reported;
/* DWORD XGetDeviceChanges(PXPP_DEVICE_TYPE, PDWORD pdwInsertions, PDWORD pdwRemovals) */
void xv_hle_XGetDeviceChanges(xctx *c)
{
    uint32_t type = X_ARG(0); int is_gamepad = type && X_M32(type + 4) == 0xFFFFFFFFu || 1;
    uint32_t ins = 0;
    static int pad2 = -1; if (pad2 < 0) { const char *e = getenv("XV_PAD2"); pad2 = e ? atoi(e) : 0; }
    if (!g_pad_reported) { ins = pad2 ? 3 : 1; g_pad_reported = 1; XK_LOG("[pad] XGetDeviceChanges: insertions %u\n", ins); }                    /* gamepad in port 1 (+ a virtual one in port 2: XV_PAD2=1) */
    (void)is_gamepad;
    X_M32(X_ARG(1)) = ins; X_M32(X_ARG(2)) = 0;
    c->r[0] = ins != 0; X_RET(3);
}
/* HANDLE XInputOpen(PXPP_DEVICE_TYPE, DWORD dwPort, DWORD dwSlot, PXINPUT_POLLING_PARAMETERS) */
void xv_hle_XInputOpen(xctx *c) { static int pad2 = -1; if (pad2 < 0) { const char *e = getenv("XV_PAD2"); pad2 = e ? atoi(e) : 0; }
    c->r[0] = X_ARG(1) == 0 ? 0x00777701u : (X_ARG(1) == 1 && pad2) ? 0x00777702u : 0; XK_LOG("[pad] XInputOpen port %u -> %08X\n", X_ARG(1), c->r[0]); X_RET(4); }
void xv_hle_XInputClose(xctx *c) { X_RET(1); }
/* DWORD XInputGetState(HANDLE, PXINPUT_STATE { DWORD dwPacketNumber; XINPUT_GAMEPAD { WORD wButtons; BYTE bAnalogButtons[8]; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } }) */
/* Force the multiplayer match to start (XV_FORCE_START=1): the lobby is gated on a 2nd player, but the
 * client session object [2E362C] is fully configured at the ENLISTED PLAYERS screen (map + variant
 * chosen).  0xA1240(session) is the real game-start executor - it broadcasts the map load and runs the
 * engine start (0xD1540/0xFA620) - normally reached only via the countdown/host handshake.  We call it
 * directly with the session pointer when the player holds L+R+Triangle (pad.force_start), bypassing the "waiting for another
 * player" gate so a single player can drop into the map.  Fires once per lobby entry. */
void f_000A1240(xctx *c);
static void xv_force_mp_start(xctx *c)
{
    uint32_t sess = X_M32(0x2E362Cu);
    if (!sess) { XK_LOG("[force-start] no client session at [2E362C]\n"); return; }
    uint16_t state = X_M16(sess + 0xCA6u);
    XK_LOG("[force-start] session %08X state %u -> calling A1240\n", sess, state);
    uint32_t saved = c->r[4];
    X_PUSH32(sess);              /* stdcall arg */
    X_PUSH32(0u);               /* dummy return address (A1240 does ret 4) */
    f_000A1240(c);
    c->r[4] = saved;            /* belt-and-suspenders: restore esp */
    XK_LOG("[force-start] A1240 returned, state now %u\n", X_M16(sess + 0xCA6u));
}
void xv_hle_XInputGetState(xctx *c)
{
    static uint32_t packet; xk_os_pad p; xk_os_pad_poll(&p);
    uint32_t st = X_ARG(1);
    { static int fs = -1; if (fs < 0) { const char *e = getenv("XV_FORCE_START"); fs = e ? atoi(e) : 0; }
      if (fs && X_ARG(0) == 0x00777701u && p.force_start) {                               /* L+R+Triangle chord or "force" script token */
          static int fired; if (!fired) { fired = 1; xv_force_mp_start(c); } } }
    if (X_ARG(0) == 0x00777702u) {                   /* virtual player 2: idle except what the pad layer injects (script p2* tokens / chord) */
        { static unsigned n; if (p.p2_buttons || p.p2_analog[0] || (n++ % 240) == 0) XK_LOG("[pad] P2 poll #%u buttons %04X A %u from %08X\n", n, p.p2_buttons, p.p2_analog[0], X_M32(c->r[4])); }
        X_M32(st) = ++packet; X_M16(st + 4) = p.p2_buttons; memcpy(X_G(st + 6), p.p2_analog, 8);
        X_M16(st + 14) = 0; X_M16(st + 16) = 0; X_M16(st + 18) = 0; X_M16(st + 20) = 0;
        c->r[0] = 0; X_RET(2);
    }
    X_M32(st) = ++packet; X_M16(st + 4) = p.buttons; memcpy(X_G(st + 6), p.analog, 8);
    X_M16(st + 14) = (uint16_t)p.lx; X_M16(st + 16) = (uint16_t)p.ly; X_M16(st + 18) = (uint16_t)p.rx; X_M16(st + 20) = (uint16_t)p.ry;
    c->r[0] = X_ARG(0) == 0x00777701u ? 0 : 1167;   /* ERROR_DEVICE_NOT_CONNECTED */
    X_RET(2);
}
void xv_hle_XInputSetState(xctx *c) { c->r[0] = 0; X_RET(2); }
void xv_hle_XapiBootToDash(xctx *c) { XK_LOG("XapiBootToDash(%u, %u, %u) - game exited to dashboard\n", X_ARG(0), X_ARG(1), X_ARG(2)); exit(0); }
void xv_hle_XLaunchNewImageA(xctx *c) { XK_LOG("XLaunchNewImageA(\"%s\") - not supported, exiting\n", xk_gstr(X_ARG(0))); exit(0); }
void xv_hle_OutputDebugStringA(xctx *c) { const char *s = xk_gstr(X_ARG(0)); XK_LOG("debug: %s%s", s, s[0] && s[strlen(s) - 1] == '\n' ? "" : "\n"); X_RET(1); }
void xv_hle_XCalculateSignatureBegin(xctx *c) { c->r[0] = 0xFFFFFFFFu; X_RET(1); }       /* INVALID_HANDLE_VALUE */

/* XNet / Winsock: no network */
#define NETLOG(fn, nargs) do { XK_LOG("[net] " fn "(%08X,%08X,%08X,%08X) from %08X\n", X_ARG(0), X_ARG(1), X_ARG(2), X_ARG(3), X_M32(c->r[4])); } while (0)
void xv_hle_XNetStartup(xctx *c) { NETLOG("XNetStartup", 1); c->r[0] = 0; X_RET(1); }
void xv_hle_XNetGetEthernetLinkStatus(xctx *c) { c->r[0] = 0x0B; NETLOG("XNetGetEthernetLinkStatus", 0); X_RET(0); }
void xv_hle_WSAStartup(xctx *c) { NETLOG("WSAStartup", 2); c->r[0] = 0; X_RET(2); }
void xv_hle_socket(xctx *c) { NETLOG("socket", 3); c->r[0] = 0xFFFFFFFFu; X_RET(3); }
void xv_hle_bind(xctx *c) { NETLOG("bind", 3); c->r[0] = 0xFFFFFFFFu; X_RET(3); }
void xv_hle_connect(xctx *c) { NETLOG("connect", 3); c->r[0] = 0xFFFFFFFFu; X_RET(3); }
void xv_hle_listen(xctx *c) { NETLOG("listen", 2); c->r[0] = 0xFFFFFFFFu; X_RET(2); }
void xv_hle_send(xctx *c) { NETLOG("send", 4); c->r[0] = 0xFFFFFFFFu; X_RET(4); }
void xv_hle_recv(xctx *c) { NETLOG("recv", 4); c->r[0] = 0xFFFFFFFFu; X_RET(4); }
void xv_hle_ioctlsocket(xctx *c) { NETLOG("ioctlsocket", 3); c->r[0] = 0xFFFFFFFFu; X_RET(3); }

/* ---- CRT transcendental HLE ---------------------------------------------------------------------
 * MSVC's _CIfmod (wrapper 0x180ADA -> dispatcher 0x180BC0): st0 = divisor, st1 = dividend;
 * returns st0 = fmod(dividend, divisor), net stack pop of one.  Native math replaces the CRT's
 * FXAM/FPREM dance - both more correct through our x87 model and much faster on the Vita. */
#include <math.h>
void xv_hle_crt_fmod(xctx *c)
{
    double y = X_ST(0), x = X_ST(1);
    x87_pop(c);
    X_ST(0) = fmod(x, y);
    X_RET(0);
}
