#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

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
                       "  -t <n>    sleep for n seconds\n\n");
                return 0;
            case 'x':
                hex = true;
                rgb = false;
                break;
            case 'r':
                rgb = true;
                hex = false;
                break;
            case 'v':
                view = true;
                break;
            case 'c':
                coordinate = true;
                break;
            case 'a':
                hex = true, view = true, coordinate = true;
                break;
            case 't':
                sleep(atoi(optarg));
                break;
        }
    }

    Display *display = XOpenDisplay(0);
    if(!display) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    Window rootw  = DefaultRootWindow(display);
    Cursor cursor = XCreateFontCursor(display, XC_crosshair);
    XEvent event;
    XImage *image;

    XGrabPointer(display, rootw, False, ButtonPressMask, GrabModeAsync,
                 GrabModeAsync, None, cursor, CurrentTime);

    while(1) {
        XNextEvent(display, &event);
        if(event.type == ButtonPress && event.xbutton.button == 1) {
            int x = event.xbutton.x;
            int y = event.xbutton.y;
            image = XGetImage(display, rootw, x, y, 1, 1, AllPlanes, ZPixmap);

            int color = XGetPixel(image, 0, 0);
            int r = (color & image->red_mask) >> 16;
            int g = (color & image->green_mask) >> 8;
            int b = (color & image->blue_mask);

            if(rgb)
                printf("%d, %d, %d", r, g, b);
            if(hex)
                printf("#%06x", color);
            if(view)
                printf("  \033[48;2;%d;%d;%dm      \033[0m", r, g, b);
            if(coordinate)
                printf("  (%dx%d)", x, y);

            putchar('\n');
            break;
        }
    }

    XDestroyImage(image);
    XUngrabPointer(display, CurrentTime);
    XFreeCursor(display, cursor);
    XCloseDisplay(display);
    return 0;
}
