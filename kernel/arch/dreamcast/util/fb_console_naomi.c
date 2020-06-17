/* KallistiOS ##version##

   util/fb_console_naomi.c
   Copyright (C) 2009, 2020 Lawrence Sebald

*/

#include <string.h>
#include <errno.h>
#include <kos/dbgio.h>
#include <kos/string.h>
#include <dc/fb_console.h>
#include <dc/video.h>
#include <dc/minifont.h>

/* This is a modified version of fb_console to use a built-in font, rather
   than trying to use the romfont. I haven't yet figured out if the NAOMI has
   an accessible romfont or where it is, so this is the substitute. */

static uint16 *fb;
static int fb_w, fb_h;
static int cur_x, cur_y;
static int min_x, min_y, max_x, max_y;

#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 16

#define FONT_CHARS_PER_LINE 32
#define FONT_LINES 3
#define FONT_WIDTH 256
#define FONT_HEIGHT 48

static int fb_detected() {
    return 1;
}

static int fb_init() {
    /* Assume we're using 640x480x16bpp */
    fb = NULL;
    fb_w = 640;
    fb_h = 480;
    min_x = 32;
    min_y = 32;
    max_x = 608;
    max_y = 448;
    cur_x = 32;
    cur_y = 32;

    return 0;
}

static int fb_shutdown() {
    return 0;
}

static int fb_set_irq_usage(int mode) {
    (void)mode;
    return 0;
}

static int fb_read() {
    errno = EAGAIN;
    return -1;
}

static int fb_write(int c) {
    uint16 *t = fb;

    if(!t)
        t = vram_s;

    if(c != '\n') {
        minifont_draw(t + cur_y * fb_w + cur_x, fb_w, c);
        cur_x += FONT_CHAR_WIDTH;
    }

    /* If we have a newline or we've gone past the end of the line, advance down
       one line. */
    if(c == '\n' || cur_x + FONT_CHAR_WIDTH > max_x) {
        cur_y += FONT_CHAR_HEIGHT;
        cur_x = min_x;

        /* If going down a line put us over the edge of the screen, move
           everything up a line, fixing the problem. */
        if(cur_y + FONT_CHAR_HEIGHT > max_y) {
            memcpy2(t + min_y * fb_w, t + (min_y + FONT_CHAR_HEIGHT) * fb_w,
                    (cur_y - min_y - FONT_CHAR_HEIGHT) * fb_w * 2);
            cur_y -= FONT_CHAR_HEIGHT;
            memset2(t + cur_y * fb_w, 0, FONT_CHAR_HEIGHT * fb_w * 2);
        }
    }

    return 1;
}

static int fb_flush() {
    return 0;
}

static int fb_write_buffer(const uint8 *data, int len, int xlat) {
    int rv = len;

    (void)xlat;

    while(len--) {
        fb_write((int)(*data++));
    }

    return rv;
}

static int fb_read_buffer(uint8 * data, int len) {
    (void)data;
    (void)len;
    errno = EAGAIN;
    return -1;
}

dbgio_handler_t dbgio_fb = {
    "fb",
    fb_detected,
    fb_init,
    fb_shutdown,
    fb_set_irq_usage,
    fb_read,
    fb_write,
    fb_flush,
    fb_write_buffer,
    fb_read_buffer
};

void dbgio_fb_set_target(uint16 *t, int w, int h, int borderx, int bordery) {
    /* Set up all the new parameters. */
    fb = t;

    fb_w = w;
    fb_h = h;
    min_x = borderx;
    min_y = bordery;
    max_x = fb_w - borderx;
    max_y = fb_w - bordery;
    cur_x = min_x;
    cur_y = min_y;
}
