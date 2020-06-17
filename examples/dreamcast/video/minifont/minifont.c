/* KallistiOS ##version##

   minifont.c
   Copyright (C) 2020 Lawrence Sebald

   This is just a very quick and simple example of the minifont functionality.
   There's really not much to see here, to be perfectly honest, except for how
   simple it really is to use...
*/

#include <dc/video.h>
#include <dc/minifont.h>
#include <kos/thread.h>

int main(int argc, char **argv) {
    int x, y, o;

    /* Draw a pattern on the screen. */
    for(y = 0; y < 480; y++) {
        for(x = 0; x < 640; x++) {
            int c = (x ^ y) & 255;
            vram_s[y * 640 + x] = ((c >> 3) << 12)
                                  | ((c >> 2) << 5)
                                  | ((c >> 3) << 0);
        }
    }

    o = 20 * 640 + 20;

    /* Write the requisite line. */
    minifont_draw_str(vram_s + o, 640, "Hello, World!");

    /* Pause to see the results */
    thd_sleep(10 * 1000);

    return 0;
}
