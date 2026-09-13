/* Halo 2 startup's cache formatter requires both raw and namespace access.
 * This deliberately supports only formatting an EMPTY diagnostic volume.
 * Its raw metadata lives in a real backing file. At dismount we validate the
 * guest-created FATX header, empty FAT and empty root before exposing the
 * already-empty directory. Raw access to a populated/mounted volume is rejected:
 * a general FATX driver that keeps raw and namespace writes coherent is future work.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cache_volume.h"

static char directories[3][128], backing[3][128];
static int mounted[3];
static uint32_t scratch;

static int empty_directory(const char *path)
{
    xk_dir *d = xk_os_opendir(path);
    if (!d) return 0;
    char name[512]; int is_dir; uint64_t size; int empty = 1;
    while (xk_os_readdir(d, name, sizeof name, &is_dir, &size))
        if (strcmp(name, ".") && strcmp(name, "..")) { empty = 0; break; }
    xk_os_closedir(d);
    return empty;
}

int h2_cache_mounts(void)
{
    /* Register before xk_init: its generic fallback mounts follow these. */
    for (unsigned i = 0; i < 3; ++i) {
        char device[64];
        snprintf(directories[i], sizeof directories[i], H2_SAVE_ROOT "/cache%u", i + 3);
        snprintf(backing[i], sizeof backing[i], H2_SAVE_ROOT "/cache%u.raw", i + 3);
        xk_os_mkdir(directories[i]);
        if (xk_os_stat(directories[i], NULL, NULL, NULL)) return -1;
        int missing = 0;
        xk_file *f = xk_os_open(backing[i], 1, 1, 0, &missing);
        if (!f) return -1;
        int64_t size = xk_os_size(f);
        if (size == 0) {
            /* Sparse, actual byte storage; metadata is produced by the guest. */
            const unsigned char zero = 0;
            if (xk_os_write(f, H2_CACHE_CAPACITY - 1, &zero, 1) != 1) { xk_os_close(f); return -1; }
        } else if (size != H2_CACHE_CAPACITY) { xk_os_close(f); return -1; }
        xk_os_close(f);
        snprintf(device, sizeof device, "\\device\\harddisk0\\partition%u", i + 3);
        xk_path_mount(device, directories[i]);
        snprintf(device, sizeof device, "\\device\\h2raw%u", i + 3);
        xk_path_mount(device, backing[i]);
    }
    return 0;
}

static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int h2_cache_validate_empty(xk_file *file)
{
    unsigned char header[4096];
    if (xk_os_size(file) != H2_CACHE_CAPACITY || xk_os_read(file, 0, header, sizeof header) != sizeof header)
        return 0;
    uint32_t sectors = le32(header + 8);
    if (le32(header) != 0x58544146 || !sectors || sectors > 128 || (sectors & (sectors - 1)) || le32(header + 12) != 1)
        return 0;
    uint32_t cluster = sectors * 512;
    uint32_t entries = H2_CACHE_CAPACITY / cluster + 1;
    uint32_t width = entries < 0xFFF0 ? 2 : 4;
    uint32_t fat_size = (entries * width + 4095) & ~4095u;
    if (fat_size + sizeof header + cluster >= H2_CACHE_CAPACITY) return 0;
    unsigned char block[4096];
    for (uint32_t offset = 0; offset < fat_size; offset += sizeof block) {
        if (xk_os_read(file, sizeof header + offset, block, sizeof block) != sizeof block) return 0;
        for (unsigned i = 0; i < sizeof block; ++i) {
            unsigned char expected = 0;
            if (offset + i < width * 2) expected = (offset + i == 0) ? 0xF8 : 0xFF;
            if (block[i] != expected) return 0;
        }
    }
    for (uint32_t offset = 0; offset < cluster; offset += sizeof block) {
        uint32_t size = cluster - offset < sizeof block ? cluster - offset : sizeof block;
        if (xk_os_read(file, sizeof header + fat_size + offset, block, size) != size) return 0;
        for (unsigned i = 0; i < size; ++i) if (block[i] != 0xFF) return 0;
    }
    return 1;
}

static int raw_index(xk_obj *o)
{
    if (!o || o->type != XO_FILE || !o->u.file.path) return -1;
    for (unsigned i = 0; i < 3; ++i) if (!strcmp(o->u.file.path, backing[i])) return i;
    return -1;
}
static int namespace_index(xk_obj *o)
{
    if (!o || (o->type != XO_FILE && o->type != XO_DIRECTORY) || !o->u.file.path) return -1;
    for (unsigned i = 0; i < 3; ++i) {
        size_t n = strlen(directories[i]);
        if (n && !strncmp(o->u.file.path, directories[i], n) && (o->u.file.path[n] == 0 || o->u.file.path[n] == '/')) return i;
    }
    return -1;
}
static void finish(xctx *c, uint32_t iosb, uint32_t status, uint32_t count, unsigned args)
{
    if (iosb) { IOSB_STATUS(iosb) = status; IOSB_INFO(iosb) = count; }
    c->r[0] = status; c->r[4] += 4 + args * 4;
}

void __real_xk_NtOpenFile(xctx *c);
void __wrap_xk_NtOpenFile(xctx *c)
{
    uint32_t oa = X_ARG(2), esp = c->r[4]; char name[256];
    xk_ansi_to_c(OA_NAME(oa), name, sizeof name);
    int index = -1;
    for (unsigned i = 0; i < 3; ++i) {
        char device[64]; snprintf(device, sizeof device, "\\Device\\Harddisk0\\Partition%u", i + 3);
        if (!OA_ROOT(oa) && !strcasecmp(name, device) && !(X_ARG(5) & 1)) index = i;
    }
    if (index < 0) { __real_xk_NtOpenFile(c); return; }
    if (mounted[index] || !empty_directory(directories[index])) {
        XK_LOG("[h2/blocked] raw access to mounted/populated cache%u requires a general FATX driver\n", index + 3);
        finish(c, X_ARG(3), STATUS_NOT_IMPLEMENTED, 0, 6); return;
    }
    if (!scratch) scratch = xk_kalloc(128);
    if (!scratch) { finish(c, X_ARG(3), STATUS_NO_MEMORY, 0, 6); return; }
    char raw[64]; snprintf(raw, sizeof raw, "\\Device\\H2Raw%u", index + 3);
    for (unsigned i = 0; i <= strlen(raw); ++i) X_M8(scratch + 32 + i) = raw[i];
    OA_ROOT(scratch) = 0; OA_NAME(scratch) = scratch + 16; OA_ATTR(scratch) = OA_ATTR(oa);
    AS_LEN(scratch + 16) = strlen(raw); AS_MAX(scratch + 16) = strlen(raw) + 1; AS_BUF(scratch + 16) = scratch + 32;
    X_M32(esp + 12) = scratch;
    __real_xk_NtOpenFile(c);
    X_M32(esp + 12) = oa;
}

void __real_xk_NtDeviceIoControlFile(xctx *c);
void __wrap_xk_NtDeviceIoControlFile(xctx *c)
{
    xk_obj *o = xk_handle_get(X_ARG(0)); int index = raw_index(o);
    if (index < 0) { __real_xk_NtDeviceIoControlFile(c); return; }
    uint32_t code = X_ARG(5), out = X_ARG(8), length = X_ARG(9), status = STATUS_SUCCESS, written = 0;
    XK_LOG("[h2/volume] cache%u ioctl=%08X bytes=%u\n", index + 3, code, length);
    if (X_ARG(1) || X_ARG(2) || X_ARG(3)) status = STATUS_NOT_IMPLEMENTED;
    else if (mounted[index]) status = STATUS_INVALID_DEVICE_REQUEST;
    else if (code != 0x70000 && code != 0x74004) status = STATUS_INVALID_DEVICE_REQUEST;
    else if (!out || length < (code == 0x70000 ? 24u : 32u)) status = STATUS_BUFFER_TOO_SMALL;
    else if (code == 0x70000) { /* DISK_GEOMETRY: virtual CHS product = configured capacity. */
        X_M64(out) = H2_CACHE_CAPACITY / (32u * 64u * 512u);
        X_M32(out + 8) = 12; X_M32(out + 12) = 32; X_M32(out + 16) = 64; X_M32(out + 20) = 512;
        written = 24;
    } else { /* PARTITION_INFORMATION */
        for (unsigned i = 0; i < 32; ++i) X_M8(out + i) = 0;
        X_M64(out + 8) = H2_CACHE_CAPACITY;
        X_M32(out + 20) = index + 3;
        X_M8(out + 26) = 1; /* RecognizedPartition */
        written = 32;
    }
    finish(c, X_ARG(4), status, written, 10);
}

void __wrap_xk_NtFsControlFile(xctx *c)
{
    xk_obj *o = xk_handle_get(X_ARG(0)); int index = raw_index(o);
    uint32_t status = STATUS_INVALID_DEVICE_REQUEST;
    if (index >= 0 && !mounted[index] && X_ARG(5) == 0x90020 && !X_ARG(1) && !X_ARG(2) && !X_ARG(3)) {
        if (h2_cache_validate_empty(o->u.file.f) && empty_directory(directories[index])) {
            mounted[index] = 1;
            XK_LOG("[h2/volume] cache%u guest FATX format validated; empty namespace mounted\n", index + 3);
            status = STATUS_SUCCESS;
        } else {
            XK_LOG("[h2/blocked] cache%u format is not a supported empty FATX volume\n", index + 3);
            status = STATUS_NOT_IMPLEMENTED;
        }
    }
    finish(c, X_ARG(4), status, 0, 10);
}

static int volume_layout(int index, uint32_t *cluster, uint32_t *units)
{
    int missing = 0; unsigned char header[16];
    xk_file *f = xk_os_open(backing[index], 0, 0, 0, &missing);
    if (!f) return -1;
    /* A previous process may have stopped halfway through formatting. A
     * signature alone must not expose an unformatted namespace on restart. */
    int valid = h2_cache_validate_empty(f) && xk_os_read(f, 0, header, sizeof header) == sizeof header;
    xk_os_close(f);
    if (!valid || le32(header) != 0x58544146) return -1;
    uint32_t sectors = le32(header + 8);
    if (!sectors || sectors > 128 || (sectors & (sectors - 1))) return -1;
    *cluster = sectors * 512;
    uint32_t entries = H2_CACHE_CAPACITY / *cluster + 1;
    uint32_t fat = (entries * (entries < 0xFFF0 ? 2 : 4) + 4095) & ~4095u;
    *units = (H2_CACHE_CAPACITY - 4096 - fat) / *cluster;
    return 0;
}
static int count_units(const char *path, uint32_t cluster, uint64_t *used, unsigned depth)
{
    if (depth > 32) return -1;
    xk_dir *d = xk_os_opendir(path); if (!d) return -1;
    char name[512]; int is_dir; uint64_t size; int result = 0;
    while (xk_os_readdir(d, name, sizeof name, &is_dir, &size)) {
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        *used += is_dir ? 1 : (size + cluster - 1) / cluster;
        if (is_dir) {
            char child[1024];
            int length = snprintf(child, sizeof child, "%s/%s", path, name);
            if (length < 0 || (unsigned)length >= sizeof child || count_units(child, cluster, used, depth + 1)) { result = -1; break; }
        }
    }
    xk_os_closedir(d); return result;
}
void __real_xk_NtQueryVolumeInformationFile(xctx *c);
void __wrap_xk_NtQueryVolumeInformationFile(xctx *c)
{
    xk_obj *o = xk_handle_get(X_ARG(0)); int index = namespace_index(o);
    if (index < 0 || X_ARG(4) != 3) { __real_xk_NtQueryVolumeInformationFile(c); return; }
    uint32_t cluster, units, status = STATUS_SUCCESS, written = 0, out = X_ARG(2);
    uint64_t used = 1, free_bytes = 0, total_bytes = 0;
    if (!out || X_ARG(3) < 24) status = STATUS_BUFFER_TOO_SMALL;
    else if (volume_layout(index, &cluster, &units)) status = 0xC000014Fu; /* STATUS_UNRECOGNIZED_VOLUME */
    else if (count_units(directories[index], cluster, &used, 0) || xk_os_freespace(directories[index], &free_bytes, &total_bytes)) status = STATUS_UNSUCCESSFUL;
    else {
        uint64_t available = used < units ? units - used : 0;
        if (available > free_bytes / cluster) available = free_bytes / cluster;
        X_M64(out) = units; X_M64(out + 8) = available; X_M32(out + 16) = cluster / 512; X_M32(out + 20) = 512;
        written = 24;
        XK_LOG("[h2/volume] cache%u allocation unit=%u bytes total=%u free=%llu\n", index + 3, cluster, units, (unsigned long long)available);
    }
    finish(c, X_ARG(1), status, written, 5);
}

void __real_xk_NtReadFile(xctx *c);
void __real_xk_NtWriteFile(xctx *c);
static int raw_io_rejected(xctx *c)
{
    xk_obj *o = xk_handle_get(X_ARG(0)); int index = raw_index(o);
    if (index < 0) return 0;
    uint64_t offset = X_ARG(7) ? X_M64(X_ARG(7)) : o->u.file.pos;
    if (mounted[index] || offset > H2_CACHE_CAPACITY || X_ARG(6) > H2_CACHE_CAPACITY - offset) {
        finish(c, X_ARG(4), STATUS_INVALID_DEVICE_REQUEST, 0, 8); return 1;
    }
    return 0;
}
void __wrap_xk_NtReadFile(xctx *c) { if (!raw_io_rejected(c)) __real_xk_NtReadFile(c); }
void __wrap_xk_NtWriteFile(xctx *c) { if (!raw_io_rejected(c)) __real_xk_NtWriteFile(c); }
