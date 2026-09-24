/* host_reports.c - the Vita UI overlay calls the kernel's 60-frame report functions; the host harness has no
 * overlay, so a thread does it every 60 presented frames. Weak references: absent features are skipped. */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
unsigned xd3d_frame(void);
#define R(name) void name(unsigned) __attribute__((weak));
R(xv_owner_phase_report) R(xv_object_jobs_report) R(xv_quat_shared_report) R(xv_quat_cache_report)
R(xv_model_palette_report) R(xv_model_hierarchy_report) R(xv_object_basis_report) R(xv_object_collect_report)
R(xv_object_scan_report) R(xv_object_hierarchy_report) R(xv_object_hierarchy_assist_report) R(xv_visibility_pass_report)
R(xv_subcluster_report) R(xv_portal_polygon_report) R(xv_clip_region_report) R(xv_query_reuse_report)
R(xv_query_world_run_report) R(xv_native_math_report) R(xv_flare_report)
#define CALL(name) do { if (name) name(delta); } while (0)
void xk_wait_stats_request(void) __attribute__((weak));
void xv_host_sample_dump(unsigned) __attribute__((weak)); void xv_host_sample_start(void) __attribute__((weak));   /* [wait] dump (blocking sites of every guest thread) at the next yield */
#include <stdint.h>
extern uint8_t *g_img_base; extern unsigned xv_n_kicks, xv_n_fires;
/* XV_HOST_CLOCK_TRACE=1: every second, Halo's vblank clock (64-bit count at 0x1F8C80) against the frame-end
 * wait target (0x2E3660), plus the kernel's fire/kick counters - shows whether the pacing loop can ever exit. */
static void clock_trace(void)
{
    static int on = -1; if (on < 0) { const char *e = getenv("XV_HOST_CLOCK_TRACE"); on = e ? atoi(e) : 0; }
    if (!on || !g_img_base) return;
    uint64_t cnt, tgt; memcpy(&cnt, g_img_base + 0x1F8C80u, 8); memcpy(&tgt, g_img_base + 0x2E3660u, 8);
    fprintf(stderr, "[host] clock: vblank %llu target %llu frame %u fires %u kicks %u\n",
            (unsigned long long)cnt, (unsigned long long)tgt, xd3d_frame(), xv_n_fires, xv_n_kicks);
}
/* The 60-frame reports run on the OWNER at the device present (xd3d_r_present, host copy in xd3d.c), where the tick's
 * object jobs are drained and, in overlap mode 2, the scene has been joined: several report reads are owner-only
 * invariants (owner_drained, xv_object_math_report_check) and aborted the Pi soaks when a thread ran them. */
/* XV_STATE_HASH=<file> (host oracle for codegen equivalence, e.g. xita_recomp.py --x87-regs): at every device
 * present, one line "frame tick arrays objects": FNV-1a over every Halo data array in the physical game-state
 * region (header + all elements; signature 'd@t@' at +0x28, max u16 @0x20, element size u16 @0x22, data @0x34)
 * and over the physics block (+0x5C..0xB8: position, velocities, orientation, bounds) of every live object body. Every 60 frames a line
 * "arrays frame name:hash ..." names the array that moved first. XV_STATE_HASH_SKIP=name,name excludes arrays
 * whose contents follow host timing (audio). Owner thread, at present: tick and scene are quiescent. */
extern uint8_t *g_xram; extern uint32_t *g_xpt;
static uint64_t sh_fnv(uint64_t h, const uint8_t *p, size_t n) { for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; } return h; }
static uint64_t sh_guest(uint64_t h, uint32_t va, uint32_t n)
{
    while (n) { uint32_t chunk = 0x1000u - (va & 0xFFFu); if (chunk > n) chunk = n;
        uint32_t off = g_xpt[va >> 12]; if (off >= (64u << 20)) { h ^= 0xDEADu; h *= 1099511628211ull; }
        else h = sh_fnv(h, g_xram + off + (va & 0xFFFu), chunk);
        va += chunk; n -= chunk; }
    return h;
}
static void state_hash(unsigned now)
{
    static FILE *out; static int on = -1; static struct { uint32_t hdr, data, bytes; char name[33]; } arr[256]; static unsigned narr, scanned_at;
    static char skip[256];
    if (on < 0) { const char *e = getenv("XV_STATE_HASH"); on = e != NULL; if (e) out = fopen(e, "w"); const char *s = getenv("XV_STATE_HASH_SKIP"); snprintf(skip, sizeof skip, ",%s,", s ? s : ""); }
    if (!on || !out || !g_xram || !g_xpt) return;
    int valid = narr > 0 && now - scanned_at < 300;
    for (unsigned i = 0; valid && i < narr; ++i) { uint32_t sig; memcpy(&sig, g_xram + arr[i].hdr + 0x28, 4); if (sig != 0x64407440u) valid = 0; }
    if (!valid) {
        narr = 0; scanned_at = now;
        for (uint32_t off = 0x28u; off + 0x10u <= (64u << 20) && narr < 256; off += 4u) {
            uint32_t sig; memcpy(&sig, g_xram + off, 4); if (sig != 0x64407440u) continue;
            const uint8_t *hp = g_xram + off - 0x28u; uint16_t max, esz; uint32_t data; memcpy(&max, hp + 0x20, 2); memcpy(&esz, hp + 0x22, 2); memcpy(&data, hp + 0x34, 4);
            char nm[33]; memcpy(nm, hp, 32); nm[32] = 0; for (int i = 0; i < 32 && nm[i]; ++i) if (nm[i] < 32 || nm[i] > 126) nm[i] = '?';
            char key[40]; snprintf(key, sizeof key, ",%s,", nm); if (strstr(skip, key)) continue;
            arr[narr].hdr = off - 0x28u; arr[narr].data = data; arr[narr].bytes = (uint32_t)max * esz; memcpy(arr[narr].name, nm, 33); narr++;
        }
    }
    uint64_t ha = 1469598103934665603ull, ho = ha; uint64_t per[256];
    for (unsigned i = 0; i < narr; ++i) {
        uint64_t h = sh_fnv(1469598103934665603ull, g_xram + arr[i].hdr, 0x38);
        memcpy(&arr[i].data, g_xram + arr[i].hdr + 0x34, 4);
        h = sh_guest(h, arr[i].data, arr[i].bytes); per[i] = h; ha ^= h; ha *= 1099511628211ull;
        if (!strcmp(arr[i].name, "object")) {
            uint16_t max, esz; memcpy(&max, g_xram + arr[i].hdr + 0x20, 2); memcpy(&esz, g_xram + arr[i].hdr + 0x22, 2);
            for (unsigned k = 0; k < max && esz >= 12; ++k) {
                uint8_t d[12]; uint32_t va = arr[i].data + k * esz; uint32_t off = g_xpt[va >> 12]; if (off >= (64u << 20)) continue;
                memcpy(d, g_xram + off + (va & 0xFFFu), 12); uint16_t salt; uint32_t body; memcpy(&salt, d, 2); memcpy(&body, d + 8, 4);
                if (salt && body) ho = sh_guest(ho, body + 0x5Cu, 0x5Cu);   /* position, velocities, forward/up, angular velocity, bounding center/radius */
            }
        }
    }
    uint32_t gg; memcpy(&gg, g_img_base + 0x2F8CA0u, 4); uint32_t tick = 0;
    if (gg) { uint32_t off = g_xpt[(gg + 0xCu) >> 12]; if (off < (64u << 20)) memcpy(&tick, g_xram + off + ((gg + 0xCu) & 0xFFFu), 4); }
    fprintf(out, "%u %08X %016llx %016llx\n", now, tick, (unsigned long long)ha, (unsigned long long)ho);
    if (now % 60 == 0) { fprintf(out, "arrays %u", now); for (unsigned i = 0; i < narr; ++i) fprintf(out, " %s:%08x", arr[i].name, (unsigned)per[i]); fprintf(out, "\n"); fflush(out); }
    /* XV_STATE_DUMP=<name>,<from frame>: raw bytes of that array every 60 frames from then on (<hash file>.<frame>.bin) */
    static char dname[40]; static unsigned dfrom = ~0u; static int dinit;
    if (!dinit) { dinit = 1; const char *d = getenv("XV_STATE_DUMP"); if (d) { const char *comma = strrchr(d, ','); if (comma) { snprintf(dname, sizeof dname, "%.*s", (int)(comma - d), d); dfrom = (unsigned)atoi(comma + 1); } } }
    if (dname[0] && now >= dfrom && now % 60 == 0)
        for (unsigned i = 0; i < narr; ++i) if (!strcmp(arr[i].name, dname)) {
            uint16_t esz_; memcpy(&esz_, g_xram + arr[i].hdr + 0x22, 2); char path[600]; snprintf(path, sizeof path, "%s.%u.esz%u.bin", getenv("XV_STATE_HASH"), now, esz_); FILE *f = fopen(path, "wb"); if (!f) break;
            for (uint32_t o = 0; o < arr[i].bytes; o += 0x1000u) { uint32_t va = arr[i].data + o, n = arr[i].bytes - o < 0x1000u ? arr[i].bytes - o : 0x1000u;
                uint32_t off = g_xpt[va >> 12]; if (off < (64u << 20) && (va & 0xFFFu) + n <= 0x1000u) fwrite(g_xram + off + (va & 0xFFFu), 1, n, f);
                else for (uint32_t k = 0; k < n; ++k) { uint32_t v2 = va + k, o2 = g_xpt[v2 >> 12]; uint8_t b = o2 < (64u << 20) ? g_xram[o2 + (v2 & 0xFFFu)] : 0; fwrite(&b, 1, 1, f); } }
            fclose(f); break;
        }
}
/* XV_ADDR_WHOIS=<frame>:<addr>,<addr>,... (host diagnostic): once at that frame, name what holds each guest address -
 * the game data array element (name, index, offset) or the object whose body starts nearest below it (index, salt,
 * definition tag, offset). Used to identify the render-view merge conflict sites the Vita reports. */
static uint32_t wh_r32(uint32_t va) { uint32_t off = g_xpt[va >> 12]; uint32_t v = 0; if (off < (64u << 20)) memcpy(&v, g_xram + off + (va & 0xFFFu), 4); return v; }
static void addr_whois(unsigned now)
{
    static unsigned at = ~0u; static uint32_t want[32]; static unsigned nwant; static int init;
    if (!init) { init = 1; const char *e = getenv("XV_ADDR_WHOIS"); if (!e) return; at = (unsigned)strtoul(e, NULL, 10); const char *p = strchr(e, ':');
        while (p && nwant < 32) { want[nwant++] = (uint32_t)strtoul(p + 1, NULL, 16); p = strchr(p + 1, ','); } }
    if (now != at || !g_xram || !g_xpt) return;
    struct { uint32_t data, bytes, esz; char name[33]; } arr[256]; unsigned narr = 0; uint32_t obj_data = 0, obj_max = 0, obj_esz = 0;
    for (uint32_t off = 0x28u; off + 0x10u <= (64u << 20) && narr < 256; off += 4u) {
        uint32_t sig; memcpy(&sig, g_xram + off, 4); if (sig != 0x64407440u) continue;
        const uint8_t *hp = g_xram + off - 0x28u; uint16_t max, esz; uint32_t data; memcpy(&max, hp + 0x20, 2); memcpy(&esz, hp + 0x22, 2); memcpy(&data, hp + 0x34, 4);
        memcpy(arr[narr].name, hp, 32); arr[narr].name[32] = 0; for (int i = 0; i < 32 && arr[narr].name[i]; ++i) if (arr[narr].name[i] < 32 || arr[narr].name[i] > 126) arr[narr].name[i] = '?';
        arr[narr].data = data; arr[narr].bytes = (uint32_t)max * esz; arr[narr].esz = esz;
        if (!strcmp(arr[narr].name, "object")) { obj_data = data; obj_max = max; obj_esz = esz; }
        narr++;
    }
    /* XV_ADDR_WHOIS_ARRAYS=<word>: also list the data arrays whose name contains <word>, with the image globals that point at their header */
    { const char *aw = getenv("XV_ADDR_WHOIS_ARRAYS");
      if (aw) for (uint32_t off = 0x28u; off + 0x10u <= (64u << 20); off += 4u) {
        uint32_t sig; memcpy(&sig, g_xram + off, 4); if (sig != 0x64407440u) continue;
        char nm[33]; memcpy(nm, g_xram + off - 0x28u, 32); nm[32] = 0; if (!strstr(nm, aw)) continue;
        uint32_t hdr_va = 0; for (uint32_t va = 0x80000000u; va < 0x84000000u; va += 0x1000u) if (g_xpt[va >> 12] == ((off - 0x28u) & ~0xFFFu)) { hdr_va = va + ((off - 0x28u) & 0xFFFu); break; }
        fprintf(stderr, "[whois] array '%s' header %08X\n", nm, hdr_va);
        for (uint32_t ia = 0x10000u; hdr_va && ia < 0x400000u; ia += 4u) { uint32_t v; memcpy(&v, g_img_base + ia, 4); if (v == hdr_va) fprintf(stderr, "[whois]   image global %08X -> header\n", ia); }
      } }
    for (unsigned w = 0; w < nwant; ++w) {
        uint32_t a = want[w]; int found = 0;
        for (unsigned i = 0; i < narr && !found; ++i) if (a >= arr[i].data && a < arr[i].data + arr[i].bytes && arr[i].esz) {
            uint32_t k = (a - arr[i].data) / arr[i].esz; fprintf(stderr, "[whois] %08X: array %s element %u offset +%X (esz %u)\n", a, arr[i].name, k, (a - arr[i].data) % arr[i].esz, arr[i].esz); found = 1; }
        if (found) continue;
        uint32_t best = 0, bk = 0, bsalt = 0;
        for (uint32_t k = 0; k < obj_max && obj_esz >= 12; ++k) { uint32_t e = obj_data + k * obj_esz, hdr = wh_r32(e), body = wh_r32(e + 8u);
            if ((hdr & 0xFFFFu) && body && body <= a && body > best) { best = body; bk = k; bsalt = hdr & 0xFFFFu; } }
        if (best && a - best < 0x2000u) fprintf(stderr, "[whois] %08X: object %u (handle %04X%04X) body %08X +%X, definition tag %08X, type word %08X\n", a, bk, bsalt, bk, best, a - best, wh_r32(best), wh_r32(obj_data + bk * obj_esz + 4u));
        else fprintf(stderr, "[whois] %08X: no array element or object body within 8 KiB (nearest body %08X)\n", a, best);
    }
}
void xv_host_reports_present(unsigned now)
{
    state_hash(now);
    addr_whois(now);
    static unsigned last; unsigned delta = now - last;
    if (delta < 60) return;
    last = now;
    fprintf(stderr, "[host] report at frame %u (%u frames)\n", now, delta);
    if (xk_wait_stats_request) xk_wait_stats_request();
    if (xv_host_sample_dump) xv_host_sample_dump(now);
    CALL(xv_owner_phase_report); CALL(xv_object_jobs_report); CALL(xv_quat_shared_report); CALL(xv_quat_cache_report);
    CALL(xv_model_palette_report); CALL(xv_model_hierarchy_report); CALL(xv_object_basis_report); CALL(xv_object_collect_report);
    CALL(xv_object_scan_report); CALL(xv_object_hierarchy_report); CALL(xv_object_hierarchy_assist_report); CALL(xv_visibility_pass_report);
    CALL(xv_subcluster_report); CALL(xv_portal_polygon_report); CALL(xv_clip_region_report); CALL(xv_query_reuse_report);
    CALL(xv_query_world_run_report); CALL(xv_native_math_report); CALL(xv_flare_report);
}
static void *reporter(void *arg)
{
    (void)arg;
    for (;;) { usleep(1000000); clock_trace(); }
    return 0;
}
void xv_host_reports_start(void) { if (xv_host_sample_start) xv_host_sample_start(); pthread_t t; if (!pthread_create(&t, 0, reporter, 0)) pthread_detach(t); }
