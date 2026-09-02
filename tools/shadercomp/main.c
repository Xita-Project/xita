/*
 * xv_shadercomp — on-device Cg -> GXP compiler for the XboxVita pipeline
 *
 * psp2cgc (Sony's offline Cg compiler) is proprietary and not part of vitasdk.
 * The very same compiler, however, ships on every PS Vita as the runtime module
 * libshacccg.suprx (SceShaccCg).  This homebrew drives it DIRECTLY through the
 * SceShaccCg API (psp2/shacccg.h) — no vitaShaRK, no SceShaccCgExt code patches.
 *
 *     <card>:data/xboxvita/shaders/<name>.cg  --->  <name>.gxp (same directory)
 *                                                    compile.log
 *
 * Shader kind is taken from the file name: "*.frag.cg" / "*_frag.cg" / "*_fp.cg"
 * compile as fragment programs, everything else (Stage 3 output) as vertex programs.
 *
 * Screen: BLUE while working, GREEN when everything compiled, RED on any failure
 * (details in compile.log).  The app exits by itself a few seconds later.
 *
 * PC side (root Makefile):  make shaders-usb  /  make shaders-pull-usb  /  make vita-eject
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/display.h>
#include <psp2/shacccg.h>

/* SceShaccCg allocates through the allocator we hand it (malloc), so the app
 * heap must be large; vitasdk's default is far too small for the compiler. */
int _newlib_heap_size_user = 192 * 1024 * 1024;

/* VitaShell's USB mode exposes the official memory card: ux0: on a stock setup,
 * uma0: when an SD2VITA/PSVSD has taken over ux0:.  Every directory that exists
 * is processed; outputs land next to the sources. */
static const char *const SHADER_DIRS[] = {
    "ux0:data/xboxvita/shaders",
    "uma0:data/xboxvita/shaders",
    "ur0:data/xboxvita/shaders",
};

/* ur0:data/ is where ShaRKBR33D / ShaRKF00D save the module; the others are
 * copies dropped over USB (make shaders-usb). */
static const char *const SHACCCG_PATHS[] = {
    "ur0:data/libshacccg.suprx",
    "ux0:data/libshacccg.suprx",
    "uma0:data/libshacccg.suprx",
    "ux0:data/xboxvita/libshacccg.suprx",
    "uma0:data/xboxvita/libshacccg.suprx",
};

#define COUNT(a)       (sizeof(a) / sizeof((a)[0]))
#define MAX_SRC_SIZE   (1024 * 1024)
#define SCREEN_W       960
#define SCREEN_H       544
#define SCREEN_STRIDE  1024

/* ------------------------------------------------------------------------- */
/*  Logging: console (if a debug shell is attached) + compile.log             */
/* ------------------------------------------------------------------------- */

static SceUID g_log = -1;

static void xlog(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n >= (int)sizeof(buf))
        n = sizeof(buf) - 1;
    sceClibPrintf("%s", buf);
    if (g_log >= 0)
        sceIoWrite(g_log, buf, n);
}

/* ------------------------------------------------------------------------- */
/*  Minimal status display: solid colour framebuffer                          */
/* ------------------------------------------------------------------------- */

#define COLOUR_WORKING 0xFF804000u   /* blue-ish  */
#define COLOUR_OK      0xFF20A020u   /* green     */
#define COLOUR_FAIL    0xFF2020C0u   /* red       */

static void show_colour(uint32_t abgr)
{
    static SceUID uid = -1;
    static uint32_t *fb = NULL;
    if (!fb) {
        /* CDRAM blocks must be a multiple of 256 KB. */
        uint32_t size = (4 * SCREEN_STRIDE * SCREEN_H + 0x3FFFF) & ~0x3FFFFu;
        uid = sceKernelAllocMemBlock("fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, NULL);
        if (uid < 0) {
            xlog("framebuffer alloc failed: 0x%08X\n", uid);
            return;
        }
        sceKernelGetMemBlockBase(uid, (void **)&fb);
    }
    for (uint32_t i = 0; i < SCREEN_STRIDE * SCREEN_H; ++i)
        fb[i] = abgr;
    SceDisplayFrameBuf dfb;
    memset(&dfb, 0, sizeof(dfb));
    dfb.size        = sizeof(dfb);
    dfb.base        = fb;
    dfb.pitch       = SCREEN_STRIDE;
    dfb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    dfb.width       = SCREEN_W;
    dfb.height      = SCREEN_H;
    sceDisplaySetFrameBuf(&dfb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
}

/* ------------------------------------------------------------------------- */
/*  File helpers                                                              */
/* ------------------------------------------------------------------------- */

static char *read_file(const char *path, uint32_t *out_size)
{
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size <= 0 || size > MAX_SRC_SIZE) {
        sceIoClose(fd);
        return NULL;
    }
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        sceIoClose(fd);
        return NULL;
    }
    int got = sceIoRead(fd, buf, (SceSize)size);
    sceIoClose(fd);
    if (got != (int)size) {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    if (out_size)
        *out_size = (uint32_t)size;
    return buf;
}

static int write_file(const char *path, const void *data, uint32_t size)
{
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0)
        return fd;
    int put = sceIoWrite(fd, data, size);
    sceIoClose(fd);
    return put == (int)size ? 0 : -1;
}

static int file_exists(const char *path)
{
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    sceIoClose(fd);
    return 1;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}

/* ------------------------------------------------------------------------- */
/*  SceShaccCg, driven directly                                               */
/* ------------------------------------------------------------------------- */

static SceUID                  g_shacccg_mod = -1;
static SceShaccCgSourceFile    g_source;                 /* the one file we ever "open" */
static SceShaccCgCallbackList  g_callbacks;

/* The compiler asks for its main source through this callback (and for any
 * #include, which we do not support: everything is returned as the main file). */
static SceShaccCgSourceFile *open_file_cb(const char *fileName,
                                          const SceShaccCgSourceLocation *includedFrom,
                                          const SceShaccCgCompileOptions *compileOptions,
                                          const char **errorString)
{
    (void)includedFrom; (void)compileOptions; (void)errorString;
    (void)fileName;
    return &g_source;
}

static int cg_init(void)
{
    for (unsigned i = 0; i < COUNT(SHACCCG_PATHS); ++i) {
        if (!file_exists(SHACCCG_PATHS[i]))
            continue;
        g_shacccg_mod = sceKernelLoadStartModule(SHACCCG_PATHS[i], 0, NULL, 0, NULL, NULL);
        xlog("  sceKernelLoadStartModule(%s) -> 0x%08X\n", SHACCCG_PATHS[i], g_shacccg_mod);
        if (g_shacccg_mod >= 0)
            break;
    }
    if (g_shacccg_mod < 0)
        return -1;

    sceShaccCgSetDefaultAllocator(malloc, free);
    sceShaccCgInitializeCallbackList(&g_callbacks, SCE_SHACCCG_TRIVIAL);
    g_callbacks.openFile = open_file_cb;

    const char *ver = sceShaccCgGetVersionString();
    xlog("  SceShaccCg version: %s\n", ver ? ver : "(null)");
    return 0;
}

/*
 * Compile one program.  Returns 0 and fills *out on success; on failure returns
 * -1 after logging every diagnostic.  `use_fx` mirrors psp2cgc's -cgfx switch.
 */
static int cg_compile(const char *name, const char *src, uint32_t len, SceShaccCgTargetProfile profile,
                      int use_fx, const SceShaccCgCompileOutput **out)
{
    SceShaccCgCompileOptions opt;
    memset(&opt, 0, sizeof(opt));
    sceShaccCgInitializeCompileOptions(&opt);
    opt.mainSourceFile      = name;
    opt.targetProfile       = profile;
    opt.entryFunctionName   = "main";
    opt.locale              = SCE_SHACCCG_ENGLISH;
    opt.useFx               = use_fx;
    opt.noStdlib            = 0;
    opt.optimizationLevel   = 3;          /* psp2cgc -O3 */
    opt.useFastmath         = 0;          /* position math stays faithful to NV2A */
    opt.useFastprecision    = 0;
    opt.useFastint          = 0;
    opt.warningLevel        = 3;
    opt.performanceWarnings = 1;
    opt.pedantic            = 0;

    g_source.fileName = name;
    g_source.text     = src;
    g_source.size     = len;

    const SceShaccCgCompileOutput *o = sceShaccCgCompileProgram(&opt, &g_callbacks, 0);
    if (!o) {
        xlog("    sceShaccCgCompileProgram returned NULL\n");
        return -1;
    }
    static const char *lvl[] = { "info", "warning", "error" };
    for (int i = 0; i < o->diagnosticCount; ++i) {
        const SceShaccCgDiagnosticMessage *d = &o->diagnostics[i];
        int line = d->location ? (int)d->location->lineNumber : -1;
        xlog("    [%s %u] line %d: %s\n", lvl[d->level < 3 ? d->level : 2], (unsigned)d->code,
             line, d->message ? d->message : "");
    }
    if (!o->programData || !o->programSize) {
        xlog("    FAILED (no program data, %d diagnostic(s))\n", o->diagnosticCount);
        *out = o;                 /* caller decides whether to destroy it */
        return -1;
    }
    *out = o;
    return 0;
}

static SceShaccCgTargetProfile classify(const char *name)
{
    if (ends_with(name, ".frag.cg") || ends_with(name, "_frag.cg") || ends_with(name, "_fp.cg"))
        return SCE_SHACCCG_PROFILE_FP;
    return SCE_SHACCCG_PROFILE_VP;
}

static int compile_file(const char *dir, const char *name)
{
    char src_path[512], gxp_path[512];
    snprintf(src_path, sizeof(src_path), "%s/%s", dir, name);
    snprintf(gxp_path, sizeof(gxp_path), "%s/%.*s.gxp", dir, (int)(strlen(name) - 3), name);

    SceShaccCgTargetProfile profile = classify(name);
    xlog("== %s  (%s)\n", src_path, profile == SCE_SHACCCG_PROFILE_VP ? "vertex" : "fragment");

    uint32_t len = 0;
    char *src = read_file(src_path, &len);
    if (!src) {
        xlog("    cannot read source\n");
        return -1;
    }
    const SceShaccCgCompileOutput *o = NULL;
    int rc = cg_compile(name, src, len, profile, 0, &o);
    free(src);
    if (rc != 0)
        return -1;                       /* leave the failed output alone */

    rc = write_file(gxp_path, o->programData, o->programSize);
    xlog("    %s -> %s (%u B)\n", rc == 0 ? "ok" : "compiled but could not write", gxp_path, o->programSize);
    sceShaccCgDestroyCompileOutput(o);
    return rc;
}

/* ------------------------------------------------------------------------- */
/*  Self-test ladder: names the construct the compiler rejects, if any         */
/* ------------------------------------------------------------------------- */

static const struct { const char *name; int use_fx; const char *src; } SELFTESTS[] = {
    { "T1 minimal flat-parameter vertex shader (useFx=0)", 0,
      "void main(float3 aPosition : POSITION, float4 out vPosition : POSITION)\n"
      "{ vPosition = float4(aPosition, 1.0); }\n" },

    { "T1fx same shader with useFx=1 (cgfx mode, what vitaShaRK uses)", 1,
      "void main(float3 aPosition : POSITION, float4 out vPosition : POSITION)\n"
      "{ vPosition = float4(aPosition, 1.0); }\n" },

    { "T2 struct in/out + uniform float4 array + swizzles (Stage 3 shape)", 0,
      "struct AppIn  { float3 position : POSITION; float4 texcoord0 : TEXCOORD0; };\n"
      "struct VertOut { float4 position : POSITION; float4 color0 : COLOR0; float4 texcoord0 : TEXCOORD0; };\n"
      "VertOut main(AppIn IN, uniform float4 c[9])\n"
      "{\n"
      "    VertOut OUT;\n"
      "    float4 v0 = float4(IN.position, 1.0);\n"
      "    float4 v9 = IN.texcoord0.zyxw;\n"
      "    float4 oPos;\n"
      "    oPos.x = dot(v0, c[0]); oPos.y = dot(v0, c[1]); oPos.z = dot(v0, c[2]); oPos.w = dot(v0, c[3]);\n"
      "    OUT.position = oPos; OUT.color0 = c[8]; OUT.texcoord0 = v9;\n"
      "    return OUT;\n"
      "}\n" },

    { "T3 = T2 with saturate(x.rgb) and .a swizzle", 0,
      "struct AppIn  { float3 position : POSITION; };\n"
      "struct VertOut { float4 position : POSITION; float4 color0 : COLOR0; };\n"
      "VertOut main(AppIn IN, uniform float4 c[9])\n"
      "{\n"
      "    VertOut OUT;\n"
      "    float4 v0 = float4(IN.position, 1.0);\n"
      "    OUT.position = float4(dot(v0, c[0]), dot(v0, c[1]), dot(v0, c[2]), dot(v0, c[3]));\n"
      "    OUT.color0 = float4(saturate(c[7].rgb) * c[8].rgb, c[8].a);\n"
      "    return OUT;\n"
      "}\n" },
};

/* ------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    show_colour(COLOUR_WORKING);

    /* Shader directories that exist; the log goes into the first one (creating
     * ux0:data/xboxvita/shaders if none exist, so there is always a log). */
    int present[COUNT(SHADER_DIRS)] = { 0 };
    int n_present = 0;
    for (unsigned i = 0; i < COUNT(SHADER_DIRS); ++i) {
        SceUID d = sceIoDopen(SHADER_DIRS[i]);
        if (d >= 0) {
            sceIoDclose(d);
            present[i] = 1;
            n_present++;
        }
    }
    if (!n_present) {
        sceIoMkdir("ux0:data", 0777);
        sceIoMkdir("ux0:data/xboxvita", 0777);
        sceIoMkdir(SHADER_DIRS[0], 0777);
        present[0] = 1;
    }
    char log_path[512] = "";
    for (unsigned i = 0; i < COUNT(SHADER_DIRS); ++i) {
        if (!present[i])
            continue;
        snprintf(log_path, sizeof(log_path), "%s/compile.log", SHADER_DIRS[i]);
        g_log = sceIoOpen(log_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        break;
    }

    xlog("xv_shadercomp (direct SceShaccCg) starting; log: %s\n", log_path);
    for (unsigned i = 0; i < COUNT(SHADER_DIRS); ++i)
        xlog("  shader dir %s: %s\n", SHADER_DIRS[i], present[i] ? "found" : "absent");

    {
        SceKernelFreeMemorySizeInfo fm;
        memset(&fm, 0, sizeof(fm));
        fm.size = sizeof(fm);
        sceKernelGetFreeMemorySize(&fm);
        void *probe = malloc(64 * 1024 * 1024);
        xlog("  heap %u MB, 64 MB test malloc %s; free user %u MB, cdram %u MB\n",
             (unsigned)(_newlib_heap_size_user >> 20), probe ? "OK" : "FAILED",
             (unsigned)(fm.size_user >> 20), (unsigned)(fm.size_cdram >> 20));
        free(probe);
    }

    int failed = 0, total = 0;
    if (cg_init() != 0) {
        xlog("ERROR: libshacccg.suprx not found / not loadable.\n"
             "       Put it at ur0:data/ (ShaRKBR33D/ShaRKF00D) or <card>:data/ (USB) and\n"
             "       make sure 'Enable Unsafe Homebrew' is on in HENkaku settings.\n");
        failed = 1;
        goto finish;
    }

    for (unsigned i = 0; i < COUNT(SELFTESTS); ++i) {
        const SceShaccCgCompileOutput *o = NULL;
        xlog("selftest %u: %s\n", i, SELFTESTS[i].name);
        if (cg_compile("selftest.cg", SELFTESTS[i].src, (uint32_t)strlen(SELFTESTS[i].src),
                       SCE_SHACCCG_PROFILE_VP, SELFTESTS[i].use_fx, &o) != 0) {
            xlog("    FAILED  <-- compiler rejects this\n");
            failed = 1;
            /* keep going: with the patches gone the compiler may well survive,
               and the later rungs tell us more than stopping here would */
            continue;
        }
        xlog("    ok (%u B)\n", o->programSize);
        sceShaccCgDestroyCompileOutput(o);
    }

    for (unsigned i = 0; i < COUNT(SHADER_DIRS); ++i) {
        if (!present[i])
            continue;
        SceUID dir = sceIoDopen(SHADER_DIRS[i]);
        if (dir < 0)
            continue;
        SceIoDirent ent;
        while (sceIoDread(dir, &ent) > 0) {
            if (!SCE_S_ISREG(ent.d_stat.st_mode) || !ends_with(ent.d_name, ".cg"))
                continue;
            total++;
            if (compile_file(SHADER_DIRS[i], ent.d_name) != 0)
                failed++;
        }
        sceIoDclose(dir);
    }
    if (total == 0) {
        xlog("WARNING: no .cg files found in any shader directory\n");
        failed = 1;
    }

finish:
    xlog("done: %d shader(s), %d failure(s)\n", total, failed);
    show_colour(failed ? COLOUR_FAIL : COLOUR_OK);
    sceKernelDelayThread(5 * 1000 * 1000);
    if (g_log >= 0)
        sceIoClose(g_log);
    /* No sceShaccCgReleaseCompiler / module unload on purpose: after an internal
     * error those are exactly the calls that crashed; the process ends anyway. */
    sceKernelExitProcess(0);
    return 0;
}
