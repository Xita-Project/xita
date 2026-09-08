#include "xv_dash.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "font.h"
#include "license_text.h"

#define GREEN 0xff60ee8cu
#define DIM   0xff46975bu
#define GREY  0xff727972u
#define WHITE 0xffc0ffd0u
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
typedef struct { const char *id, *name; } map_info;
static const map_info campaign[] = {
    {"a10", "The Pillar of Autumn"}, {"a30", "Halo"},
    {"a50", "The Truth and Reconciliation"}, {"b30", "The Silent Cartographer"},
    {"b40", "Assault on the Control Room"}, {"c10", "343 Guilty Spark"},
    {"c20", "The Library"}, {"c40", "Two Betrayals"},
    {"d20", "Keyes"}, {"d40", "The Maw"}
};
static const map_info multiplayer[] = {
    {"beavercreek", "Battle Creek"}, {"sidewinder", "Sidewinder"},
    {"damnation", "Damnation"}, {"ratrace", "Rat Race"},
    {"prisoner", "Prisoner"}, {"hangemhigh", "Hang 'Em High"},
    {"chillout", "Chill Out"}, {"carousel", "Derelict"},
    {"boardingaction", "Boarding Action"}, {"bloodgulch", "Blood Gulch"},
    {"wizard", "Wizard"}, {"putput", "Chiron TL-34"}, {"longest", "Longest"}
};
enum { FPS, BC_MIPS, PROF, VBLANK, TEX_DETAIL, VOLUME, SENSITIVITY, DEADZONE,
       INVERT_Y, LOOK_CURVE, TOUCH, CPU, TEX_FILTER, MIP_SMOOTH, RESOLUTION,
       MATERIAL, GLOW, PARTICLES, DECAL_TIME, DECAL_LIMIT, FRAME_CAP, CPU_CLOCK, EXTENDED_BC, TRIPLE_BUFFER, SETTINGS_COUNT };
static const char *const keys[] = {"XV_FPS", "XV_BC_MIPS", "XV_PROF", "XV_VBLANK_HZ",
    "XV_TEX_MAXDIM", "XV_VOLUME", "XV_LOOK_SENS", "XV_DEADZONE", "XV_INVERT_Y",
    "XV_LOOK_CURVE", "XV_TOUCH", "XV_CPU", "XV_TEX_FILTER", "XV_MIP_SMOOTH", "XV_RENDER_HEIGHT",
    "XV_MATERIAL_QUALITY", "XV_GLOW_QUALITY", "XV_PARTICLE_QUALITY", "XV_DECAL_SECONDS",
    "XV_DECAL_LIMIT", "XV_FRAME_CAP", "XV_CPU_MHZ", "XV_EXTENDED_BC", "XV_TRIPLE_BUFFER"};
static const int defaults[] = {0, 0, 0, 60, 256, 50, 100, 0, 0, 0, 1, 1, 0, 1, 544,
    2, 2, 2, 0, 0, 0, 444, 0, 0};
static const int rates[] = {60, 120, 250, 500, 1000};
static const int texture_caps[] = {64, 128, 256, 512};
static const int resolutions[] = {360, 400, 480, 544};
static const int frame_caps[] = {0, 20, 25, 30};
static const int decal_times[] = {0, 5, 15, 30, 60};
static const int decal_limits[] = {0, 32, 64, 128};
static const char *const texture_labels[] = {"VERY LOW", "LOW", "STANDARD", "HIGH"};
typedef struct { int x1, y1, x2, y2; } segment;
typedef struct {
    xv_dash_framebuffer fb;
    const char *root;
    int image, ui, camp[10], multi[13], nc, nm;
    char **saves;
    int ns;
    int page, depth, row, game, mode, values[SETTINGS_COUNT], simple, scroll;
    unsigned tick, held;
    uint32_t previous;
    int touching;
    char status[96];
    segment mesh[1800];
    int mesh_count;
} dash;

static int path(char *dst, size_t size, const dash *s, const char *leaf)
{
    int n = snprintf(dst, size, "%s/%s", s->root, leaf);
    return n >= 0 && (size_t)n < size;
}
static int exists(const dash *s, const char *leaf)
{
    char p[1024]; struct stat st;
    return path(p, sizeof(p), s, leaf) && stat(p, &st) == 0 && S_ISREG(st.st_mode);
}
static int namecmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}
static int discover(dash *s)
{
    char p[1024], leaf[512];
    s->image = exists(s, "halo_image.bin");
    s->ui = exists(s, "haloce/maps/ui.map");
    if (s->simple) return 0;
    for (int i = 0; i < COUNT(campaign); i++) {
        snprintf(leaf, sizeof(leaf), "haloce/maps/%s.map", campaign[i].id);
        if (exists(s, leaf)) s->camp[s->nc++] = i;
    }
    for (int i = 0; i < COUNT(multiplayer); i++) {
        snprintf(leaf, sizeof(leaf), "haloce/maps/%s.map", multiplayer[i].id);
        if (exists(s, leaf)) s->multi[s->nm++] = i;
    }
    if (!path(p, sizeof(p), s, "save")) return -1;
    DIR *dir = opendir(p);
    if (!dir) return 0;
    struct dirent *e;
    while ((e = readdir(dir))) {
        size_t len = strlen(e->d_name);
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        /* launch.cfg is line based; do not offer unrepresentable names. */
        if (len > 255 || strpbrk(e->d_name, "\r\n")) continue;
        snprintf(leaf, sizeof(leaf), "save/%s", e->d_name);
        if (!exists(s, leaf)) continue;
        char **next = realloc(s->saves, ((size_t)s->ns + 1) * sizeof(*next));
        if (!next) { closedir(dir); return -1; }
        s->saves = next;
        s->saves[s->ns] = malloc(len + 1);
        if (!s->saves[s->ns]) { closedir(dir); return -1; }
        memcpy(s->saves[s->ns++], e->d_name, len + 1);
    }
    closedir(dir);
    if (s->ns) qsort(s->saves, (size_t)s->ns, sizeof(*s->saves), namecmp);
    return 0;
}

/* Read arbitrarily long lines, so unknown settings and comments round-trip. */
static int read_line(FILE *f, char **line, size_t *cap)
{
    size_t n = 0; int ch;
    while ((ch = fgetc(f)) != EOF) {
        if (n + 2 > *cap) {
            size_t size = *cap ? *cap * 2 : 256;
            char *p = realloc(*line, size);
            if (!p) return -1;
            *line = p; *cap = size;
        }
        (*line)[n++] = (char)ch;
        if (ch == '\n') break;
    }
    if (ferror(f)) return -1;
    if (!n) return 0;
    (*line)[n] = 0;
    return 1;
}
static int setting_key(const char *line, const char **value)
{
    while (isspace((unsigned char)*line)) line++;
    for (int i = 0; i < COUNT(keys); i++) {
        size_t n = strlen(keys[i]);
        if (strncmp(line, keys[i], n)) continue;
        const char *p = line + n;
        while (*p == ' ' || *p == '\t') p++;
        if (*p++ != '=') continue;
        while (*p == ' ' || *p == '\t') p++;
        *value = p;
        return i;
    }
    return -1;
}
/* edit_key >= 0 updates only the setting the user changed. The legacy standalone
 * mode uses -1 to preserve its four-setting save contract. */
static int settings(dash *s, int write_back, int edit_key)
{
    char p[1024], tmp[1024];
    if (!path(p, sizeof(p), s, "xita.cfg") ||
        !path(tmp, sizeof(tmp), s, "xita.cfg.tmp")) return -1;
    FILE *in = fopen(p, "rb"), *out = NULL;
    if (!in && errno != ENOENT) return -1;
    if (write_back && !(out = fopen(tmp, "wb"))) { if (in) fclose(in); return -1; }
    char *line = NULL; size_t cap = 0;
    int seen[SETTINGS_COUNT] = {0}, rc = 0, last_newline = 1, got;
    while (in && (got = read_line(in, &line, &cap)) != 0) {
        if (got < 0) { rc = -1; break; }
        const char *value = NULL;
        int k = setting_key(line, &value);
        if (k >= 0) {
            seen[k] = 1;
            if (!write_back) {
                int v = atoi(value);
                if (k < 3 || k == INVERT_Y || k == TOUCH || k == CPU || k == TRIPLE_BUFFER) s->values[k] = !!v;
                else if (k != VBLANK) s->values[k] = v;
                else for (int j = 0; j < COUNT(rates); j++)
                    if (v == rates[j]) s->values[k] = v;
            }
        }
        if (out) {
            if (k < 0 || (edit_key >= 0 ? k != edit_key : k >= 4)) { if (fputs(line, out) == EOF) rc = -1; }
            else {
                const char *end = value;
                while (*end && !isspace((unsigned char)*end) && *end != '#' && *end != ';') end++;
                if (fwrite(line, 1, (size_t)(value - line), out) != (size_t)(value - line) ||
                    fprintf(out, "%d%s", s->values[k], end) < 0) rc = -1;
            }
        }
        last_newline = line[strlen(line) - 1] == '\n';
    }
    if (in && fclose(in)) rc = -1;
    free(line);
    if (out) {
        if (!last_newline && fputc('\n', out) == EOF) rc = -1;
        for (int i = 0; i < SETTINGS_COUNT; i++)
            if ((edit_key >= 0 ? i == edit_key : i < 4) && !seen[i] && fprintf(out, "%s=%d\n", keys[i], s->values[i]) < 0) rc = -1;
        if (fclose(out)) rc = -1;
        if (!rc && rename(tmp, p)) rc = -1;
        if (rc) remove(tmp);
    }
    return rc;
}

static void pixel(dash *s, int x, int y, uint32_t c, unsigned a)
{
    if (x < 0 || y < 0 || x >= s->fb.width || y >= s->fb.height) return;
    uint32_t *p = &s->fb.pixels[y * s->fb.pitch + x];
    if (a == 255) { *p = c; return; }
    uint32_t d = *p, r = 0xff000000u;
    for (int shift = 0; shift <= 16; shift += 8)
        r |= (((((c >> shift) & 255) * a + ((d >> shift) & 255) * (255 - a)) / 255) << shift);
    *p = r;
}
static void rect(dash *s, int x, int y, int w, int h, uint32_t c, unsigned a)
{
    /* Clip once per span and hoist constant alpha work out of the pixel loop. */
    int right = x + w, bottom = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > s->fb.width) right = s->fb.width;
    if (bottom > s->fb.height) bottom = s->fb.height;
    if (x >= right || y >= bottom) return;
    if (a == 255) {
        for (int j = y; j < bottom; j++) {
            uint32_t *p = &s->fb.pixels[j * s->fb.pitch];
            for (int i = x; i < right; i++) p[i] = c;
        }
    } else {
        unsigned r = (c & 255) * a, g = ((c >> 8) & 255) * a;
        unsigned b = ((c >> 16) & 255) * a, inv = 255 - a;
        for (int j = y; j < bottom; j++) {
            uint32_t *p = &s->fb.pixels[j * s->fb.pitch];
            for (int i = x; i < right; i++) {
                uint32_t d = p[i];
                p[i] = 0xff000000u | ((r + (d & 255) * inv) / 255) |
                    (((g + ((d >> 8) & 255) * inv) / 255) << 8) |
                    (((b + ((d >> 16) & 255) * inv) / 255) << 16);
            }
        }
    }
}
static void line(dash *s, int x, int y, int x2, int y2, uint32_t c)
{
    int dx = abs(x2-x), dy = -abs(y2-y), sx = x<x2 ? 1:-1, sy = y<y2 ? 1:-1, err = dx+dy;
    for (;;) {
        pixel(s,x,y,c,255);
        if (x == x2 && y == y2) break;
        int e = 2*err;
        if (e >= dy) { err += dy; x += sx; }
        if (e <= dx) { err += dx; y += sy; }
    }
}
static void text(dash *s, int x, int y, const char *str, int scale, int spacing, uint32_t c)
{
    for (; *str; str++, x += 6*scale + spacing) {
        unsigned ch = (unsigned char)*str;
        if (ch < 32 || ch > 127) ch = '?';
        if (x + 5*scale >= s->fb.width - 20) break;
        for (int row = 0; row < 8; row++) for (int col = 0; col < 5; col++)
            if (font[ch-32][row] & (1u << (6-col))) rect(s,x+col*scale,y+row*scale,scale,scale,c,255);
    }
}
static void pill(dash *s, int x, int y, int w, int h, uint32_t c, unsigned a)
{
    int r = h/2;
    for (int j = 0; j < h; j++) {
        int dy = j-r;
        int inset = r - (int)sqrtf((float)(r*r-dy*dy));
        rect(s,x+inset,y+j,w-2*inset,1,c,a);
    }
}
static void mesh_init(dash *s)
{
    const int vx[] = {0,16,48,64,48,16}, vy[] = {28,0,0,28,56,56};
    for (int col = -1; col < 22; col++) for (int row = -2; row < 11; row++) {
        int x = col*48, y = row*56 + (col & 1)*28;
        for (int k = 0; k < 6; k++) {
            int next = (k+1)%6;
            s->mesh[s->mesh_count++] = (segment){x+vx[k],y+vy[k],x+vx[next],y+vy[next]};
        }
    }
}
static void item(dash *s, int row, const char *label, int selected, int enabled)
{
    int y = 213 + row*32;
    if (selected) {
        pill(s,371,y-8,541,30,GREEN,60);
        rect(s,372,y-2,3,18,GREEN,255);
    }
    text(s,390,y,label,2,0,enabled ? (selected ? WHITE : DIM) : GREY);
}
static int content_count(const dash *s)
{
    return s->mode == 0 ? s->nc : s->mode == 1 ? s->nm : s->ns;
}

enum { LAUNCH_VISIBLE_ROWS = 5, LICENSE_PAGE = 6, LICENSE_VISIBLE_LINES = 18 };
static const char *const launch_pages[] = {"LAUNCH GAME", "GRAPHICS", "AUDIO", "CONTROLS", "DISPLAY", "PERFORMANCE", "ABOUT / LICENSE"};
static const int launch_keys[][12] = {
    {-1}, {TEX_DETAIL, TEX_FILTER, MIP_SMOOTH, RESOLUTION, MATERIAL,
           GLOW, PARTICLES, DECAL_TIME, DECAL_LIMIT, FRAME_CAP, EXTENDED_BC, TRIPLE_BUFFER}, {VOLUME, -1},
    {SENSITIVITY, DEADZONE, INVERT_Y, LOOK_CURVE, TOUCH}, {FPS, CPU, -1},
    {CPU_CLOCK, -1}, {-1}
};
static const int launch_counts[] = {0, 12, 1, 5, 2, 1, 0};
static const char *const setting_names[] = {
    "Performance overlay", "", "", "", "Texture detail", "Master volume",
    "Look sensitivity", "Stick deadzone", "Invert look", "Look response", "Touch controls", "CPU meter",
    "Texture filtering", "Mip smoothing", "Render resolution", "Material quality",
    "Glow / lens flares", "Particle detail", "Decal lifetime", "Decal limit", "Frame limit", "CPU clock", "More compressed textures", "Triple buffering"
};
static const char *const setting_help[] = {
    "Show FPS and frame timing during play.", "", "", "",
    "Lower detail saves texture memory. High uses more.",
    "50% is the default mix with room for loud sounds.",
    "Adjust how quickly the right stick turns the camera.",
    "Increase this if the sticks drift when untouched.",
    "Reverse the vertical direction of the right stick.",
    "Gentle response gives finer control near the center.",
    "Enable front touch shortcuts. Rear touch is disabled by default.",
    "Show core usage when the performance overlay is on.",
    "Point looks pixelated; linear blends texture pixels.",
    "Auto keeps existing mip blending; off may shimmer.",
    "360p: 640x360. 400p: 704x400. 480p: 848x480. Native: 960x544.",
    "Lower quality simplifies surface detail and reflections.",
    "Low keeps the central glow. Off removes lens-flare glow.",
    "Shorten cosmetic smoke and sparks. Projectiles keep their timing.",
    "Shorter-lived impact marks leave fewer decals on screen.",
    "Limit retained impact marks; level decorations are preserved.",
    "Caps presentation FPS when fast enough. Game time stays unchanged.",
    "500 MHz needs an overclock plugin; unavailable clocks use 444 MHz.",
    "Keep more textures compressed. Compare appearance before keeping it on.",
    "Experimental: prepare frames ahead. May add input delay. Default off."
};
static void render_license(dash *s)
{
    char position[40];
    int end = s->scroll + LICENSE_VISIBLE_LINES;
    if (end > COUNT(xv_license_lines)) end = COUNT(xv_license_lines);
    text(s,376,194,"XITA / GPL-3.0-ONLY",1,1,GREEN);
    snprintf(position,sizeof position,"%d-%d / %d",s->scroll+1,end,COUNT(xv_license_lines));
    text(s,902-(int)strlen(position)*6,194,position,1,0,DIM);
    for (int i=s->scroll; i<end; i++)
        text(s,376,216+(i-s->scroll)*12,xv_license_lines[i],1,0,WHITE);
    text(s,376,449,"UP / DOWN  SCROLL     LEFT / RIGHT  PAGE",1,0,GREEN);
    text(s,376,466,"Full license included. CIRCLE returns to navigation.",1,0,DIM);
}
static void setting_value(const dash *s, int k, char *value, size_t size)
{
    int v = s->values[k];
    if (k == TEX_DETAIL) {
        for (int i = 0; i < COUNT(texture_caps); i++) if (v == texture_caps[i]) {
            snprintf(value,size,"%s",texture_labels[i]); return;
        }
        snprintf(value,size,"CUSTOM %d",v);
    } else if (k == TEX_FILTER) snprintf(value,size,"%s",v == 1 ? "POINT" : v == 2 ? "LINEAR" : "GAME");
    else if (k == MIP_SMOOTH) snprintf(value,size,"%s",v ? "AUTO" : "OFF");
    else if (k == RESOLUTION) {
        if (v == 360 || v == 400 || v == 480) snprintf(value,size,"%dP",v);
        else snprintf(value,size,"NATIVE");
    }
    else if (k == MATERIAL || k == GLOW || k == PARTICLES)
        snprintf(value,size,"%s",v == 0 ? (k == GLOW ? "OFF" : "LOW") : v == 1 ? (k == GLOW ? "LOW" : "MEDIUM") : "ORIGINAL");
    else if (k == DECAL_TIME || k == DECAL_LIMIT) {
        if (!v) snprintf(value,size,"ORIGINAL");
        else snprintf(value,size,k == DECAL_TIME ? "%d SEC" : "%d",v);
    }
    else if (k == FRAME_CAP) {
        if (!v) snprintf(value,size,"OFF"); else snprintf(value,size,"%d FPS",v);
    }
    else if (k == CPU_CLOCK) snprintf(value,size,"%d MHZ",v == 500 ? 500 : 444);
    else if (k == LOOK_CURVE) snprintf(value,size,"%s",v == 2 ? "GENTLE" : "LINEAR");
    else if (k == VOLUME || k == SENSITIVITY || k == DEADZONE) snprintf(value,size,"%d%%",v);
    else snprintf(value,size,"%s",v ? "ON" : "OFF");
}
static void render_launcher(dash *s)
{
    for (int i = 0; i < COUNT(launch_pages); i++) {
        int y = 180 + i*43, active = s->page == i;
        pill(s,40,y,286,38,active ? GREEN : DIM,active ? 190 : 60);
        pill(s,43,y+3,280,32,0xff092510u,active ? 80 : 255);
        text(s,64,y+11,launch_pages[i],2,1,active ? WHITE : DIM);
    }
    rect(s,350,177,576,305,0xff040b05u,235);
    if (!s->page) {
        text(s,378,205,"HALO",5,4,WHITE);
        text(s,378,260,"COMBAT EVOLVED",2,2,GREEN);
        line(s,378,304,894,304,DIM);
        text(s,378,326,"Your settings are ready.",2,0,WHITE);
        text(s,378,366,"Launch opens Halo's main menu.",1,1,DIM);
        text(s,378,390,"Choose a campaign or multiplayer game there.",1,0,DIM);
        text(s,378,414,"In-game graphics: SELECT + CIRCLE.",1,0,GREEN);
        if (s->image && s->ui) text(s,378,439,"CROSS  LAUNCH GAME",2,1,GREEN);
        else text(s,378,439,"Game files are missing. Check your installation.",1,0,GREEN);
    } else if (s->page == LICENSE_PAGE) {
        if (s->depth) render_license(s);
        else {
            text(s,378,206,"XITA",4,2,WHITE);
            text(s,378,267,"GNU GPL VERSION 3",2,0,GREEN);
            text(s,378,306,"Original code: GPL-3.0-only.",1,0,WHITE);
            text(s,378,330,"You may share and modify the covered code.",1,0,DIM);
            text(s,378,354,"No warranty. See the full license for terms.",1,0,DIM);
            text(s,378,390,"Game code and assets have separate rights.",1,0,DIM);
            text(s,378,441,"CROSS  READ FULL LICENSE",2,0,GREEN);
        }
    } else {
        int count = launch_counts[s->page];
        if (s->row < s->scroll) s->scroll = s->row;
        if (s->row >= s->scroll + LAUNCH_VISIBLE_ROWS) s->scroll = s->row - LAUNCH_VISIBLE_ROWS + 1;
        int end = s->scroll + LAUNCH_VISIBLE_ROWS;
        if (end > count) end = count;
        text(s,376,194,launch_pages[s->page],1,2,GREEN);
        if (count > LAUNCH_VISIBLE_ROWS) {
            char range[32];
            snprintf(range,sizeof range,"%d-%d OF %d",s->scroll+1,end,count);
            text(s,892-(int)strlen(range)*6,194,range,1,0,DIM);
            rect(s,918,218,2,185,DIM,80);
            rect(s,918,218+s->scroll*185/count,2,LAUNCH_VISIBLE_ROWS*185/count,GREEN,255);
            text(s,384,412,"UP / DOWN  SCROLL FOR MORE SETTINGS",1,0,DIM);
        }
        for (int i = s->scroll; i < end; i++) {
            int k = launch_keys[s->page][i], y = 227+(i-s->scroll)*38;
            char value[32]; setting_value(s,k,value,sizeof value);
            int active = s->depth && s->row == i;
            if (active) { pill(s,366,y-9,548,33,GREEN,45); rect(s,369,y-3,3,20,GREEN,255); }
            text(s,384,y,setting_names[k],2,0,active ? WHITE : DIM);
            text(s,892-(int)strlen(value)*12,y,value,2,0,active ? WHITE : GREEN);
        }
        int k = launch_keys[s->page][s->row];
        text(s,375,441,setting_help[k],1,0,DIM);
        text(s,375,462,"Changes save automatically for your next launch.",1,0,DIM);
    }
    text(s,48,487,s->status,1,0,GREEN);
    line(s,40,503,920,503,DIM);
    text(s,48,519,s->depth && s->page == LICENSE_PAGE ? "UP / DOWN  SCROLL     LEFT / RIGHT  PAGE     CIRCLE  BACK" :
         s->depth ? "LEFT / RIGHT  CHANGE     CIRCLE  BACK     UP / DOWN  SELECT" :
         "CROSS  SELECT     UP / DOWN  NAVIGATE",1,1,DIM);
    text(s,775,519,"PS VITA",1,2,GREEN);
}
static int edit_setting(dash *s, int direction)
{
    int k = launch_keys[s->page][s->row], old = s->values[k], v = old;
    const int *choices = NULL; int count = 0;
    if (k == RESOLUTION) { choices = resolutions; count = COUNT(resolutions); }
    else if (k == FRAME_CAP) { choices = frame_caps; count = COUNT(frame_caps); }
    else if (k == DECAL_TIME) { choices = decal_times; count = COUNT(decal_times); }
    else if (k == DECAL_LIMIT) { choices = decal_limits; count = COUNT(decal_limits); }
    if (choices) {
        int selected = -1;
        for (int i = 0; i < count; ++i) if (old == choices[i]) selected = i;
        v = selected < 0 ? defaults[k] : choices[(selected + (direction < 0 ? count-1 : 1)) % count];
    }
    else if (k == TEX_DETAIL) {
        int selected = -1;
        for (int i = 0; i < COUNT(texture_caps); i++) if (old == texture_caps[i]) selected = i;
        if (selected < 0) v = 256;
        else v = texture_caps[(selected + (direction < 0 ? COUNT(texture_caps)-1 : 1)) % COUNT(texture_caps)];
    }
    else if (k == TEX_FILTER) v = (old >= 0 && old <= 2) ? (old + (direction < 0 ? 2 : 1)) % 3 : 0;
    else if (k == MATERIAL || k == GLOW || k == PARTICLES)
        v = old >= 0 && old <= 2 ? (old + (direction < 0 ? 2 : 1)) % 3 : 2;
    else if (k == CPU_CLOCK) v = old == 500 ? 444 : 500;
    else if (k == LOOK_CURVE) v = old == 2 ? 0 : 2;
    else if (k == VOLUME || k == SENSITIVITY || k == DEADZONE) {
        int step = k == VOLUME ? 10 : k == SENSITIVITY ? 25 : 5;
        int limit = k == VOLUME ? 100 : k == SENSITIVITY ? 400 : 95;
        v += direction*step;
        if (v < 0) v = 0;
        if (v > limit) v = limit;
    } else v = !old;
    if (v == old) return 0;
    s->values[k] = v;
    if (settings(s,1,k)) {
        s->values[k] = old;
        strcpy(s->status,"Could not save settings. Check storage and try again.");
    } else { strcpy(s->status,"Settings saved"); return k + 1; }
    return 0;
}

struct xv_dash_graphics { dash state; };
int xv_dash_graphics_live(const char *key)
{
    return key && (!strcmp(key,"XV_RENDER_HEIGHT") || !strcmp(key,"XV_TEX_FILTER") ||
        !strcmp(key,"XV_MIP_SMOOTH") || !strcmp(key,"XV_FRAME_CAP") || !strcmp(key,"XV_TRIPLE_BUFFER"));
}
xv_dash_graphics *xv_dash_graphics_create(const char *root)
{
    xv_dash_graphics *panel = calloc(1,sizeof(*panel));
    if (!panel) return NULL;
    dash *s = &panel->state;
    s->root = root ? root : "ux0:data/xita";
    s->simple = s->page = s->depth = 1;
    memcpy(s->values,defaults,sizeof defaults);
    for (int i=0;i<SETTINGS_COUNT;i++) {
        const char *value=getenv(keys[i]); if(value)s->values[i]=atoi(value);
    }
    if (settings(s,0,-1)) strcpy(s->status,"Could not read settings. Check storage.");
    return panel;
}
void xv_dash_graphics_destroy(xv_dash_graphics *panel) { free(panel); }
void xv_dash_graphics_status(xv_dash_graphics *panel, const char *message)
{ if(panel)snprintf(panel->state.status,sizeof panel->state.status,"%s",message); }
const char *xv_dash_graphics_input(xv_dash_graphics *panel, uint32_t edge, int *value)
{
    if (!panel || !value) return NULL;
    dash *s=&panel->state;
    if (edge & (XV_DASH_UP|XV_DASH_DOWN)) {
        s->row=(s->row+((edge&XV_DASH_UP)?launch_counts[1]-1:1))%launch_counts[1];
        s->status[0]=0;
    } else if (edge & (XV_DASH_LEFT|XV_DASH_RIGHT|XV_DASH_CROSS)) {
        int changed=edit_setting(s,(edge&XV_DASH_LEFT)?-1:1);
        if(changed) {
            int k=changed-1; *value=s->values[k];
            strcpy(s->status,xv_dash_graphics_live(keys[k]) ? "Saved. Applying at the next frame." : "Saved for next launch. Relaunch Xita to apply.");
            return keys[k];
        }
    }
    return NULL;
}
void xv_dash_graphics_snapshot(const xv_dash_graphics *panel, xv_dash_graphics_view *view)
{
    memset(view,0,sizeof(*view));
    if(!panel)return;
    const dash *s=&panel->state;
    view->active=1;view->selected=s->row;view->count=launch_counts[1];
    view->first=s->row/5*5;
    for(int i=0;i<5 && view->first+i<view->count;i++) {
        int k=launch_keys[1][view->first+i];
        snprintf(view->names[i],sizeof view->names[i],"%s",setting_names[k]);
        setting_value(s,k,view->values[i],sizeof view->values[i]);
    }
    int k=launch_keys[1][s->row];view->live=xv_dash_graphics_live(keys[k]);
    snprintf(view->help,sizeof view->help,"%s",setting_help[k]);
    snprintf(view->status,sizeof view->status,"%s",s->status);
}
static int launcher_input(dash *s, uint32_t edge, xv_dash_result *out)
{
    if (edge & XV_DASH_CIRCLE) { s->depth = 0; s->row = 0; s->scroll = 0; s->status[0] = 0; }
    else if (s->depth && s->page == LICENSE_PAGE) {
        int delta = edge & XV_DASH_UP ? -1 : edge & XV_DASH_DOWN ? 1 :
                    edge & XV_DASH_LEFT ? -LICENSE_VISIBLE_LINES :
                    edge & XV_DASH_RIGHT ? LICENSE_VISIBLE_LINES : 0;
        int limit = COUNT(xv_license_lines) - LICENSE_VISIBLE_LINES;
        if (limit < 0) limit = 0;
        s->scroll += delta;
        if (s->scroll < 0) s->scroll = 0;
        if (s->scroll > limit) s->scroll = limit;
    }
    else if (edge & (XV_DASH_UP | XV_DASH_DOWN)) {
        int count = s->depth ? launch_counts[s->page] : COUNT(launch_pages);
        int *row = s->depth ? &s->row : &s->page;
        *row = (*row + ((edge & XV_DASH_UP) ? count-1 : 1)) % count;
        if (!s->depth) { s->row = 0; s->scroll = 0; }
        s->status[0] = 0;
    } else if (s->depth && (edge & (XV_DASH_LEFT | XV_DASH_RIGHT | XV_DASH_CROSS))) {
        edit_setting(s,edge & XV_DASH_LEFT ? -1 : 1);
    } else if (edge & (XV_DASH_CROSS | XV_DASH_RIGHT)) {
        if (s->page) { s->depth = 1; s->row = 0; s->scroll = 0; }
        else if (!s->image || !s->ui) strcpy(s->status,"Game files are missing. Check your installation.");
        else { strcpy(out->game_id,"haloce"); out->mode = XV_DASH_CAMPAIGN; return 1; }
    }
    return 0;
}
static void render(dash *s)
{
    for (int y = 0; y < s->fb.height; y++)
        for (int x = 0; x < s->fb.width; x++) s->fb.pixels[y*s->fb.pitch+x] = 0xff020703u;
    int drift = (int)(s->tick % 3360)/60;
    for (int i = 0; i < s->mesh_count; i++) {
        segment *m = &s->mesh[i];
        line(s,m->x1,m->y1+drift,m->x2,m->y2+drift,0xff0c2513u);
    }
    float pulse = (sinf((float)s->tick/60.0f*2.0f)+1)*0.5f;
    for (int i = 14; i > 0; i -= 2) pill(s,350-i,29-i,260+i*2,92+i*2,GREEN,4);
    pill(s,350,29,260,92,GREEN,95+(unsigned)(pulse*25));
    pill(s,366,35,228,30,WHITE,30);
    text(s,412,53,"XITA",5,5,WHITE);
    text(s,48,83,"THE NEXT CHAPTER",1,2,DIM);
    text(s,735,83,"NATIVE / PS VITA",1,1,DIM);
    rect(s,40,148,880,7,GREEN,12);
    line(s,40,150,920,150,GREEN);
    if (s->simple) { render_launcher(s); return; }
    const char *menus[] = {"GAMES","SETTINGS","CONTROLS","ABOUT"};
    for (int i = 0; i < 4; i++) {
        int y = 207+i*63, active = s->page == i;
        if (active) for (int g = 8; g > 0; g -= 2) pill(s,45-g,y-g,274+g*2,46+g*2,GREEN,8+(unsigned)(pulse*3));
        pill(s,45,y,274,46,active ? GREEN : DIM,active ? 160:80);
        pill(s,47,y+2,270,42,0xff092510u,active ? 80:255);
        text(s,76,y+15,menus[i],2,3,active ? WHITE:DIM);
        if (active && !s->depth) text(s,290,y+16,"+",2,0,WHITE);
    }
    rect(s,355,177,570,305,0xff040b05u,215);
    text(s,375,185,menus[s->page],1,2,GREEN);
    if (!s->page) {
        if (s->depth <= 1) {
            item(s,0,"Halo: Combat Evolved",s->depth && s->row == 0,s->image && s->ui);
            item(s,2,"Halo 2 - Planned",s->depth && s->row == 1,0);
            char buf[96];
            snprintf(buf,sizeof(buf),"%d campaign / %d multiplayer / %d saves",s->nc,s->nm,s->ns);
            text(s,390,243,buf,1,0,DIM);
            if (!s->image) text(s,375,345,"halo_image.bin missing - build from your own disc",1,0,GREEN);
            if (!s->ui) text(s,375,365,"ui.map missing - copy it from your own disc",1,0,GREEN);
            if (s->image && s->ui) text(s,375,345,"Installed - select to browse content",1,0,DIM);
        } else if (s->depth == 2) {
            const char *modes[] = {"Campaign", "Multiplayer", "Saves", "Game settings"};
            for (int i = 0; i < 4; i++) item(s,i,modes[i],s->row == i,1);
        } else {
            int count = content_count(s), start = s->row / 7 * 7;
            for (int i = start; i < count && i < start+7; i++) {
                const char *label = s->mode == 0 ? campaign[s->camp[i]].name :
                    s->mode == 1 ? multiplayer[s->multi[i]].name : s->saves[i];
                char clipped[43];
                snprintf(clipped,sizeof(clipped),"%.42s",label);
                item(s,i-start,clipped,s->row == i,1);
            }
            char buf[96];
            if (count) snprintf(buf,sizeof(buf),"%d / %d    %s",s->row+1,count,s->mode == 2 ? "SAVE FILES":"INSTALLED MAPS");
            else snprintf(buf,sizeof(buf),"No %s found",s->mode == 2 ? "save files":"maps - copy from your own disc");
            text(s,375,458,buf,1,1,DIM);
        }
    } else if (s->page == 1) {
        for (int i = 0; i < 4; i++) {
            char buf[64];
            if (i == 3) snprintf(buf,sizeof(buf),"%s: %d",keys[i],s->values[i]);
            else snprintf(buf,sizeof(buf),"%s: %s",keys[i],s->values[i] ? "ON":"OFF");
            item(s,i,buf,s->depth && s->row == i,1);
        }
        text(s,375,385,"Cross changes value and saves xita.cfg",1,0,DIM);
        text(s,375,405,"Unknown keys and comments are preserved",1,0,DIM);
    } else if (s->page == 2) {
        const char *rows[] = {
            "VITA                 XBOX          HALO",
            "Left / right stick   Sticks        Move / look",
            "Cross / Circle       A / B         Jump / melee",
            "Square               X             Action / reload",
            "Triangle             Y             Switch weapon",
            "L / R                Triggers      Grenade / fire",
            "Start / Select       Start / Back  Pause",
            "D-pad down / up      Stick clicks  Crouch / zoom",
            "D-pad right / left   White / Black Light / grenade",
            "Select + Start (hold)              Frame-time overlay"
        };
        for (int i = 0; i < COUNT(rows); i++) text(s,375,217+i*24,rows[i],1,0,i ? DIM:GREEN);
    } else {
        const char *rows[] = {
            "XITA / GPL-3.0-ONLY", "",
            "Copyright (C) 2026 Xita contributors.",
            "You may modify and redistribute covered code",
            "under GNU GPL version 3. No warranty.",
            "The full terms are included in app0:LICENSE.",
            "Third-party notices: app0:THIRD_PARTY.md.", "",
            "Game code and assets are not covered.", "",
            "https://github.com/BirchWoodGod/xita"
        };
        for (int i = 0; i < COUNT(rows); i++) text(s,375,215+i*23,rows[i],1,0,i ? DIM:GREEN);
    }
    text(s,48,477,s->status,1,0,GREEN);
    line(s,40,503,920,503,DIM);
    text(s,48,519,"CROSS  CONFIRM     CIRCLE  BACK     D-PAD / LEFT STICK  MOVE",1,1,DIM);
    text(s,755,519,"XITA / DASHBOARD",1,1,GREEN);
}

static int activate(dash *s, xv_dash_result *out)
{
    if (!s->depth) { s->depth = 1; s->row = 0; return 0; }
    if (s->page == 1) {
        int old = s->values[s->row];
        if (s->row < 3) s->values[s->row] ^= 1;
        else for (int i = 0; i < COUNT(rates); i++) if (old == rates[i]) {
            s->values[3] = rates[(i+1)%COUNT(rates)]; break;
        }
        if (settings(s,1,-1)) { s->values[s->row] = old; strcpy(s->status,"Could not save xita.cfg - check storage"); }
        else strcpy(s->status,"Settings saved");
    } else if (!s->page) {
        if (s->depth == 1) {
            if (s->row == 1) strcpy(s->status,"Halo 2 is planned and cannot be launched");
            else if (!s->image || !s->ui) strcpy(s->status,"Halo requires halo_image.bin and haloce/maps/ui.map");
            else { s->game = s->row; s->depth = 2; s->row = 0; s->status[0] = 0; }
        } else if (s->depth == 2) {
            s->mode = s->row;
            if (s->mode != 3) { s->depth = 3; s->row = 0; }
            else { strcpy(out->game_id,"haloce"); out->mode = XV_DASH_SETTINGS; return 1; }
        } else if (content_count(s)) {
            strcpy(out->game_id,"haloce");
            out->mode = s->mode == 1 ? XV_DASH_MULTIPLAYER : XV_DASH_CAMPAIGN;
            out->is_save = s->mode == 2;
            const char *name = s->mode == 0 ? campaign[s->camp[s->row]].id :
                s->mode == 1 ? multiplayer[s->multi[s->row]].id : s->saves[s->row];
            snprintf(out->map,sizeof(out->map),"%s",name);
            return 1;
        }
    }
    return 0;
}
static int valid_fb(const xv_dash_framebuffer *fb)
{
    return fb->pixels && fb->width >= 960 && fb->height >= 544 &&
        fb->width <= 4096 && fb->height <= 4096 && fb->pitch >= fb->width && fb->pitch <= 8192;
}
int xv_dash_run(const xv_dash_config *cfg, xv_dash_result *out)
{
    if (out) memset(out,0,sizeof(*out));
    if (!cfg || !out || !cfg->poll || !cfg->present || !valid_fb(&cfg->framebuffer)) return -1;
    dash *s = calloc(1,sizeof(*s));
    if (!s) return -1;
    int rc = -1;
    s->fb = cfg->framebuffer; s->root = cfg->data_root ? cfg->data_root : "ux0:data/xita";
    s->simple = cfg->simple_launcher;
    memcpy(s->values,defaults,sizeof defaults);
    if (s->simple) for (int i = 0; i < SETTINGS_COUNT; i++) {
        const char *e = getenv(keys[i]); if (e) s->values[i] = atoi(e);
    }
    if (discover(s)) goto done;
    if (settings(s,0,-1)) strcpy(s->status,"Could not read settings. Check storage.");
    mesh_init(s);
    for (;;) {
        xv_dash_input in = {0};
        int polled = cfg->poll(cfg->userdata,&in);
        if (polled) { rc = polled > 0 ? 1:-1; break; }
        uint32_t buttons = in.buttons;
        if (in.ly < -48) buttons |= XV_DASH_UP;
        if (in.ly > 48) buttons |= XV_DASH_DOWN;
        if (in.lx < -48) buttons |= XV_DASH_LEFT;
        if (in.lx > 48) buttons |= XV_DASH_RIGHT;
        uint32_t edge = buttons & ~s->previous;
        if (buttons == s->previous) s->held++; else s->held = 0;
        if (s->held >= 24 && s->held % 6 == 0) edge |= buttons & (XV_DASH_UP|XV_DASH_DOWN);
        s->previous = buttons;
        if (s->simple) {
            if (launcher_input(s,edge,out)) { rc = 0; break; }
            render(s);
            if (cfg->present(cfg->userdata,&s->fb) < 0 || !valid_fb(&s->fb)) break;
            s->tick++;
            continue;
        }
        if (in.front_count && !s->touching) {
            int x = in.front[0].x, y = in.front[0].y;
            if (x >= 45 && x < 319 && y >= 207 && y < 442 && (y-207)%63 < 46) {
                s->page = (y-207)/63; s->depth = 0; s->row = 0; s->status[0] = 0;
            } else if (s->depth && x >= 371 && x < 912 && y >= 205 && y < 429) {
                int row = (y-205)/32;
                int count = s->page == 1 ? 4 : s->page ? 0 :
                    s->depth == 1 ? 2 : s->depth == 2 ? 4 : content_count(s);
                if (!s->page && s->depth == 1) row = row == 2 ? 1 : row == 0 ? 0 : -1;
                if (!s->page && s->depth == 3) row += s->row/7*7;
                if (row >= 0 && row < count) s->row = row;
            }
        }
        s->touching = !!in.front_count;
        if (edge & (XV_DASH_CIRCLE|XV_DASH_LEFT)) {
            if (s->depth) {
                s->depth--; s->row = s->depth == 2 ? s->mode : s->depth == 1 ? s->game : 0;
                if (s->page) s->row = 0;
            }
            s->status[0] = 0;
        } else if (edge & (XV_DASH_UP|XV_DASH_DOWN)) {
            int count = !s->depth ? 4 : s->page == 1 ? 4 : s->page ? 0 :
                s->depth == 1 ? 2 : s->depth == 2 ? 4 : content_count(s);
            if (count) {
                int *row = s->depth ? &s->row : &s->page;
                *row = (*row + ((edge & XV_DASH_UP) ? count-1 : 1)) % count;
                s->status[0] = 0;
            }
        } else if (edge & (XV_DASH_CROSS|XV_DASH_RIGHT)) {
            if (activate(s,out)) { rc = 0; break; }
        }
        render(s);
        if (cfg->present(cfg->userdata,&s->fb) < 0 || !valid_fb(&s->fb)) break;
        s->tick++; /* exactly 1/60 s; presentation callback owns pacing */
    }
done:
    for (int i = 0; i < s->ns; i++) free(s->saves[i]);
    free(s->saves); free(s);
    return rc;
}
