#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

#define GRID_CELLS   9 
#define CELL_PX      16
#define CORNER_R     6      /* corner radius: 0 = square, ~8 = rounded */

#define WIN_SIZE  (GRID_CELLS * CELL_PX)
#define HALF      (GRID_CELLS / 2)
#define GAP       15

#define SW_W  120
#define SW_H   52

static void place_win(int mx, int my, int sw, int sh,
                      int win_w, int win_h,
                      bool right_side, bool bottom_side,
                      int *out_x, int *out_y)
{
    *out_x = right_side  ? mx + GAP : mx - GAP - win_w;
    *out_y = bottom_side ? my + GAP : my - GAP - win_h;
    if (*out_x < 0)          *out_x = 0;
    if (*out_y < 0)          *out_y = 0;
    if (*out_x + win_w > sw) *out_x = sw - win_w;
    if (*out_y + win_h > sh) *out_y = sh - win_h;
}

static void draw_preview(Display *dpy, Window rootw, Window prev,
                          GC gc, int mx, int my, int screen)
{
    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);
    int sx = mx - HALF, sy = my - HALF;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx + GRID_CELLS > sw) sx = sw - GRID_CELLS;
    if (sy + GRID_CELLS > sh) sy = sh - GRID_CELLS;
    XImage *snap = XGetImage(dpy, rootw, sx, sy,
                              GRID_CELLS, GRID_CELLS, AllPlanes, ZPixmap);
    if (!snap) return;
    XClearWindow(dpy, prev);
    for (int row = 0; row < GRID_CELLS; row++) {
        for (int col = 0; col < GRID_CELLS; col++) {
            unsigned long px = XGetPixel(snap, col, row);
            int cx = col * CELL_PX, cy = row * CELL_PX;
            bool top   = (row == 0), bot  = (row == GRID_CELLS - 1);
            bool left  = (col == 0), right = (col == GRID_CELLS - 1);
            int r = (CORNER_R > 0 && (top||bot) && (left||right)) ? CORNER_R : 0;
            XSetForeground(dpy, gc, px);
            if (r > 0) {
                int ax = left ? cx : cx + CELL_PX - 2*r;
                int ay = top  ? cy : cy + CELL_PX - 2*r;
                int start = 0;
                if (top  && left)  start = 90*64;
                if (top  && right) start = 0;
                if (bot  && left)  start = 180*64;
                if (bot  && right) start = 270*64;
                XFillRectangle(dpy, prev, gc, cx, cy, CELL_PX, CELL_PX);
                XSetForeground(dpy, gc, BlackPixel(dpy, screen));
                XFillRectangle(dpy, prev, gc, ax, ay, 2*r, 2*r);
                XSetForeground(dpy, gc, px);
                XFillArc(dpy, prev, gc, ax, ay, 2*r, 2*r, start, 90*64);
            } else {
                XFillRectangle(dpy, prev, gc, cx, cy, CELL_PX, CELL_PX);
            }
            XSetForeground(dpy, gc, BlackPixel(dpy, screen));
            if (col < GRID_CELLS - 1)
                XDrawLine(dpy, prev, gc,
                          cx+CELL_PX-1, cy, cx+CELL_PX-1, cy+CELL_PX-1);
            if (row < GRID_CELLS - 1)
                XDrawLine(dpy, prev, gc,
                          cx, cy+CELL_PX-1, cx+CELL_PX-1, cy+CELL_PX-1);
        }
    }
    int cx = HALF * CELL_PX, cy = HALF * CELL_PX;
    XSetForeground(dpy, gc, WhitePixel(dpy, screen));
    XDrawRectangle(dpy, prev, gc, cx, cy, CELL_PX-1, CELL_PX-1);
    XDestroyImage(snap);
}

static void draw_swatch(Display *dpy, Window swin, GC gc,
                         int screen, XFontStruct *font,
                         int r, int g, int b, unsigned long raw)
{
    /* background */
    XSetForeground(dpy, gc, 0x1a1a1a);
    XFillRectangle(dpy, swin, gc, 0, 0, SW_W, SW_H);

    /* colour block */
    XSetForeground(dpy, gc, raw);
    XFillRectangle(dpy, swin, gc, 0, 0, SW_W / 3, SW_H);

    /* thin separator */
    XSetForeground(dpy, gc, 0x444444);
    XDrawLine(dpy, swin, gc, SW_W/3, 0, SW_W/3, SW_H);

    /* text */
    char hex[12], rgb_str[24];
    snprintf(hex,     sizeof(hex),     "#%02x%02x%02x", r, g, b);
    snprintf(rgb_str, sizeof(rgb_str), "%d %d %d",      r, g, b);
    XSetForeground(dpy, gc, WhitePixel(dpy, screen));
    if (font) {
        XDrawString(dpy, swin, gc, SW_W/3 + 7, 20, hex,     strlen(hex));
        XDrawString(dpy, swin, gc, SW_W/3 + 7, 38, rgb_str, strlen(rgb_str));
    }
}

static unsigned long sample_pixel(Display *dpy, Window rootw,
                                   int mx, int my,
                                   int *r, int *g, int *b)
{
    int sw2 = DisplayWidth(dpy, DefaultScreen(dpy));
    int sh2 = DisplayHeight(dpy, DefaultScreen(dpy));
    if (mx < 0) mx = 0;
    if (mx >= sw2) mx = sw2 - 1;
    if (my < 0) my = 0;
    if (my >= sh2) my = sh2 - 1;
    XImage *img = XGetImage(dpy, rootw, mx, my, 1, 1, AllPlanes, ZPixmap);
    if (!img) { *r = *g = *b = 0; return 0; }
    unsigned long px = XGetPixel(img, 0, 0);
    *r = (px & img->red_mask)   >> 16;
    *g = (px & img->green_mask) >>  8;
    *b = (px & img->blue_mask);
    XDestroyImage(img);
    return px;
}

static void update_both(Display *dpy, Window rootw,
                         Window prev, Window swin,
                         GC gc_prev, GC gc_sw,
                         int screen, XFontStruct *font,
                         int mx, int my)
{
    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);

    /* preview: prefer right+bottom of cursor */
    bool prev_right  = (mx + GAP + WIN_SIZE <= sw);
    bool prev_bottom = (my + GAP + WIN_SIZE <= sh);

    /* swatch: opposite horizontal, same vertical */
    bool sw_right  = !prev_right;
    bool sw_bottom = prev_bottom;

    int px, py, swx, swy;
    place_win(mx, my, sw, sh, WIN_SIZE, WIN_SIZE,
              prev_right, prev_bottom, &px, &py);
    place_win(mx, my, sw, sh, SW_W,    SW_H,
              sw_right,   sw_bottom,   &swx, &swy);

    XMoveWindow(dpy, prev, px,  py);
    XMoveWindow(dpy, swin, swx, swy);

    draw_preview(dpy, rootw, prev, gc_prev, mx, my, screen);

    int r, g, b;
    unsigned long raw = sample_pixel(dpy, rootw, mx, my, &r, &g, &b);
    draw_swatch(dpy, swin, gc_sw, screen, font, r, g, b, raw);

    XFlush(dpy);
}

int main(int argc, char **argv) {
    int c;
    bool hex = true, rgb = false, coordinate = false, view = false;
    while ((c = getopt(argc, argv, "hxrvcat:")) != -1) {
        switch (c) {
            case 'h':
                printf("Usage: px [-h] [-xrvca] [-t seconds]\n\n"
                       "Mouse left-click to pick up the color\n\n"
                       "Options:\n"
                       "  -h        display this message and exit\n"
                       "  -x        print in hexadecimal only (default)\n"
                       "  -r        print in rgb\n"
                       "  -v        show color swatch in terminal\n"
                       "  -c        print coordinate\n"
                       "  -a        run px with the -xvc options\n"
                       "  -t <n>    sleep for n seconds\n\n"
                       "Tweak: GRID_CELLS, CELL_PX, CORNER_R, SW_W, SW_H at top of source\n");
                return 0;
            case 'x': hex = true;  rgb = false; break;
            case 'r': rgb = true;  hex = false; break;
            case 'v': view = true; break;
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

    XSetWindowAttributes swa = {
        .override_redirect = True,
        .background_pixel  = BlackPixel(display, screen),
        .border_pixel      = 0x444444,
    };
    unsigned long swa_mask = CWOverrideRedirect | CWBackPixel | CWBorderPixel;

    Window prev = XCreateWindow(display, rootw, 0, 0, WIN_SIZE, WIN_SIZE, 1,
                                 CopyFromParent, InputOutput, CopyFromParent,
                                 swa_mask, &swa);
    XMapWindow(display, prev);
    XSelectInput(display, prev, 0);

    Window swin = XCreateWindow(display, rootw, 0, 0, SW_W, SW_H, 1,
                                 CopyFromParent, InputOutput, CopyFromParent,
                                 swa_mask, &swa);
    XMapWindow(display, swin);
    XSelectInput(display, swin, 0);

    GC gc_prev = XCreateGC(display, prev, 0, NULL);
    GC gc_sw   = XCreateGC(display, swin, 0, NULL);

    XFontStruct *font = XLoadQueryFont(display, "fixed");
    if (font) XSetFont(display, gc_sw, font->fid);

    XEvent event;
    XGrabPointer(display, rootw, False,
                 ButtonPressMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime);
    XGrabKeyboard(display, rootw, False,
                  GrabModeAsync, GrabModeAsync, CurrentTime);

    {
        Window root_ret, child_ret;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        XQueryPointer(display, rootw, &root_ret, &child_ret,
                      &root_x, &root_y, &win_x, &win_y, &mask);
        update_both(display, rootw, prev, swin,
                    gc_prev, gc_sw, screen, font, root_x, root_y);
    }

    int last_x = -1, last_y = -1;

    while (1) {
        XNextEvent(display, &event);

        if (event.type == MotionNotify) {
            int mx = event.xmotion.x;
            int my = event.xmotion.y;
            { int _sw = DisplayWidth(display,screen), _sh = DisplayHeight(display,screen);
              if (mx < 0) mx = 0;
              if (mx >= _sw) mx = _sw - 1;
              if (my < 0) my = 0;
              if (my >= _sh) my = _sh - 1; }
            if (mx == last_x && my == last_y) continue;
            last_x = mx; last_y = my;
            update_both(display, rootw, prev, swin,
                        gc_prev, gc_sw, screen, font, mx, my);
        }

        if (event.type == KeyPress) {
            break;
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
    XDestroyWindow(display, swin);
    XFreeGC(display, gc_prev);
    XFreeGC(display, gc_sw);
    if (font) XFreeFont(display, font);
    XUngrabKeyboard(display, CurrentTime);
    XUngrabPointer(display, CurrentTime);
    XFreeCursor(display, cursor);
    XCloseDisplay(display);
    return 0;
}
