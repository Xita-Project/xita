#ifndef XV_DASH_H
#define XV_DASH_H

#include <stdint.h>

/* Pixels are opaque ABGR8888 words; pitch is in pixels, not bytes.
 * The UI uses a 960x544 canvas. Larger surfaces are allowed (top-left origin).
 * present may replace pixels with another caller-owned buffer of the same size.
 */
typedef struct {
    uint32_t *pixels;
    int width, height, pitch;
} xv_dash_framebuffer;

enum {
    XV_DASH_UP = 1u << 0, XV_DASH_DOWN = 1u << 1,
    XV_DASH_LEFT = 1u << 2, XV_DASH_RIGHT = 1u << 3,
    XV_DASH_CROSS = 1u << 4, XV_DASH_CIRCLE = 1u << 5
};
typedef struct { int x, y; } xv_dash_touch;
typedef struct {
    uint32_t buttons;
    int lx, ly, rx, ry; /* signed -128..127; zero is neutral */
    unsigned front_count, rear_count;
    xv_dash_touch front[8], rear[8]; /* canvas pixel coordinates */
} xv_dash_input;

typedef enum {
    XV_DASH_CAMPAIGN, XV_DASH_MULTIPLAYER, XV_DASH_SETTINGS
} xv_dash_mode;
typedef struct {
    char game_id[32]; /* "haloce" or "halo2"; empty on cancel/error */
    xv_dash_mode mode;
    char map[256]; /* map stem or save filename; empty for settings */
    int is_save; /* distinguishes a save from a campaign map */
} xv_dash_result;
typedef struct {
    xv_dash_framebuffer framebuffer;
    const char *data_root; /* NULL means ux0:data/xita; override for host tests */
    void *userdata;
    /* poll: 0 = sampled input, positive = cancel, negative = error. */
    int (*poll)(void *userdata, xv_dash_input *input);
    /* present: display completed frame, pace at 60 Hz, supply next buffer.
     * Return zero on success, negative on error. No buffer is freed by module. */
    int (*present)(void *userdata, xv_dash_framebuffer *framebuffer);
    /* Embedded runtime: Launch Game plus supported pre-launch settings.
     * Selection returns an empty map and opens the game's normal menu. */
    int simple_launcher;
    void (*update_status)(char *text, unsigned size);
    int (*update_action)(int action);
    int release_updates; /* actions: check, download, install, rollback */
    void (*update_detail)(char *text, unsigned size);
    int (*update_busy)(void);
    /* Optional external profile availability. Positive = launchable; otherwise
     * explain the missing installation in text. Does not launch or modify data. */
    int (*game_status)(const char *game_id, char *text, unsigned size);
} xv_dash_config;

/* 0 = selected, 1 = caller cancelled, -1 = invalid config / allocation / I/O
 * callback error. Circle navigates back; poll can cancel the whole dashboard.
 * All internal allocations are released before returning. */
int xv_dash_run(const xv_dash_config *cfg, xv_dash_result *out);

/* Nonblocking graphics panel. The recording thread owns the model; copy the
 * view into a retired frame slot before publishing it to the render thread. */
typedef struct xv_dash_graphics xv_dash_graphics;
typedef struct {
    int active, selected, first, count;
    char names[5][32], values[5][32];
    char help[96], status[96];
    int live;
} xv_dash_graphics_view;
xv_dash_graphics *xv_dash_graphics_create(const char *data_root);
void xv_dash_graphics_destroy(xv_dash_graphics *panel);
/* Edge buttons only. Returns the saved setting's key, or NULL. */
const char *xv_dash_graphics_input(xv_dash_graphics *panel, uint32_t edge, int *value);
void xv_dash_graphics_snapshot(const xv_dash_graphics *panel, xv_dash_graphics_view *view);
int xv_dash_graphics_live(const char *key);
void xv_dash_graphics_status(xv_dash_graphics *panel, const char *message);
#endif
