#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

#define GRID_CELLS   9
#define CELL_PX      16
#define CORNER_R     6      /* corner radius: 0 = square, ~8 = rounded */

#define WIN_SIZE  (GRID_CELLS * CELL_PX)
#define HALF      (GRID_CELLS / 2)


static void draw_preview(Display *dpy, Window rootw, Window prev,
                          GC gc, int mx, int my, int screen)
{
    int off = HALF;
    XImage *snap = XGetImage(dpy, rootw,
                              mx - off, my - off,
                              GRID_CELLS, GRID_CELLS,
                              AllPlanes, ZPixmap);
    if (!snap) return;

    XClearWindow(dpy, prev);

    for (int row = 0; row < GRID_CELLS; row++) {
        for (int col = 0; col < GRID_CELLS; col++) {
            unsigned long px = XGetPixel(snap, col, row);
            XSetForeground(dpy, gc, px);

            int cx = col * CELL_PX;
            int cy = row * CELL_PX;

            /* clip corners on the four corner cells */
            bool top    = (row == 0);
            bool bot    = (row == GRID_CELLS - 1);
            bool left   = (col == 0);
            bool right  = (col == GRID_CELLS - 1);
            int r = (CORNER_R > 0 && (top||bot) && (left||right)) ? CORNER_R : 0;

            if (r > 0) {
                /* figure out which quadrant so arc faces inward */
                int arc_x = left  ? cx         : cx + CELL_PX - 2*r;
                int arc_y = top   ? cy         : cy + CELL_PX - 2*r;
                int start;
                if (top  && left)  start = 90*64;
                if (top  && right) start = 0;
                if (bot  && left)  start = 180*64;
                if (bot  && right) start = 270*64;

                /* fill the whole cell, then punch out the outer corner */
                XFillRectangle(dpy, prev, gc, cx, cy, CELL_PX, CELL_PX);

                /* overwrite corner with window background (black) */
                XSetForeground(dpy, gc,
                    BlackPixel(dpy, screen)); /* matches XSetWindowBackground */
                XFillRectangle(dpy, prev, gc, arc_x, arc_y, 2*r, 2*r);
                XSetForeground(dpy, gc, px);
                XFillArc(dpy, prev, gc, arc_x, arc_y, 2*r, 2*r, start, 90*64);
            } else {
                XFillRectangle(dpy, prev, gc, cx, cy, CELL_PX, CELL_PX);
            }

            /* draw 1-px dark grid line on right/bottom edges */
            XSetForeground(dpy, gc, BlackPixel(dpy, screen));
            if (col < GRID_CELLS - 1)
                XDrawLine(dpy, prev, gc,
                          cx + CELL_PX - 1, cy,
                          cx + CELL_PX - 1, cy + CELL_PX - 1);
            if (row < GRID_CELLS - 1)
                XDrawLine(dpy, prev, gc,
                          cx, cy + CELL_PX - 1,
                          cx + CELL_PX - 1, cy + CELL_PX - 1);
        }
    }

    /* highlight centre cell with a white border */
    int cx = HALF * CELL_PX;
    int cy = HALF * CELL_PX;
    XSetForeground(dpy, gc, WhitePixel(dpy, screen));
    XDrawRectangle(dpy, prev, gc, cx, cy, CELL_PX - 1, CELL_PX - 1);

    XDestroyImage(snap);
    XFlush(dpy);
}

int main(int argc, char **argv) {
    int c;
    bool hex = true, rgb = false, coordinate = false, view = false;
    while((c = getopt(argc, argv, "hxrvcat:")) != -1) {
        switch(c) {
            case 'h':
                printf("Usage: px [-h] [-xrvca] [-t seconds]\n\n"
                       "Mouse left-click to pick up the color\n\n"
                       "Options:\n"
                       "  -h        display this message and exit\n"
                       "  -x        print in hexadecimal only (default)\n"
                       "  -r        print in rgb\n"
                       "  -v        show color\n"
                       "  -c        print coordinate\n"
                       "  -a        run px with the -xvc options\n"
                       "  -t <n>    sleep for n seconds\n\n"
                       "Tweak preview: GRID_CELLS, CELL_PX, CORNER_R at top of source\n");
                return 0;
            case 'x': hex = true;  rgb = false; break;
            case 'r': rgb = true;  hex = false; break;
            case 'v': view = true;  break;
            case 'c': coordinate = true; break;
            case 'a': hex = true; view = true; coordinate = true; break;
            case 't': sleep(atoi(optarg)); break;
        }
    }

    Display *display = XOpenDisplay(0);
    if (!display) { fprintf(stderr, "Cannot open display\n"); return 1; }

    int screen   = DefaultScreen(display);
    Window rootw = DefaultRootWindow(display);
    Cursor cursor = XCreateFontCursor(display, XC_crosshair);

    /* ── preview window ── */
    XSetWindowAttributes swa = {
        .override_redirect = True,
        .background_pixel  = BlackPixel(display, screen),
        .border_pixel      = WhitePixel(display, screen),
    };
    Window prev = XCreateWindow(display, rootw,
                                 0, 0, WIN_SIZE, WIN_SIZE, 1,
                                 CopyFromParent, InputOutput, CopyFromParent,
                                 CWOverrideRedirect | CWBackPixel | CWBorderPixel,
                                 &swa);
    XMapWindow(display, prev);

    GC gc = XCreateGC(display, prev, 0, NULL);

    XEvent event;
    XGrabPointer(display, rootw, False,
                 ButtonPressMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime);

    /* draw immediately at current pointer position so the window isn't black */
    {
        Window root_ret, child_ret;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        XQueryPointer(display, rootw, &root_ret, &child_ret,
                      &root_x, &root_y, &win_x, &win_y, &mask);
        XMoveWindow(display, prev, root_x + 20, root_y + 20);
        draw_preview(display, rootw, prev, gc, root_x, root_y, screen);
    }

    int last_x = -1, last_y = -1;

    while (1) {
        XNextEvent(display, &event);

        if (event.type == MotionNotify) {
            int mx = event.xmotion.x;
            int my = event.xmotion.y;
            if (mx == last_x && my == last_y) continue;
            last_x = mx; last_y = my;

            /* place preview window offset from cursor */
            int wx = mx + 20, wy = my + 20;
            XMoveWindow(display, prev, wx, wy);
            draw_preview(display, rootw, prev, gc, mx, my, screen);
        }

        if (event.type == ButtonPress && event.xbutton.button == 1) {
            int x = event.xbutton.x;
            int y = event.xbutton.y;
            XImage *image = XGetImage(display, rootw, x, y, 1, 1, AllPlanes, ZPixmap);
            int color = XGetPixel(image, 0, 0);
            int r = (color & image->red_mask)   >> 16;
            int g = (color & image->green_mask) >>  8;
            int b = (color & image->blue_mask);

            if (rgb)        printf("%d, %d, %d", r, g, b);
            if (hex)        printf("#%06x", color);
            if (view)       printf("  \033[48;2;%d;%d;%dm      \033[0m", r, g, b);
            if (coordinate) printf("  (%dx%d)", x, y);
            putchar('\n');

            XDestroyImage(image);
            break;
        }
    }

    XDestroyWindow(display, prev);
    XFreeGC(display, gc);
    XUngrabPointer(display, CurrentTime);
    XFreeCursor(display, cursor);
    XCloseDisplay(display);
    return 0;
}
