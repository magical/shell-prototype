#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

Atom wm_protocols;
Atom wm_delete_window;

cairo_surface_t *cairo_create_x11_surface(Display *display, int x, int y) {
    int screen;
    Visual *visual;
    Window win;
    cairo_surface_t *surface;

    screen = DefaultScreen(display);
    visual = DefaultVisual(display, screen);
    win = XCreateSimpleWindow(display, DefaultRootWindow(display),
        0, 0, x, y, 0, 0, 0);

    // Set some attributes to avoid flicker when resizing.
    XSetWindowAttributes attr;
    int mask;
    attr.background_pixmap = None;
    attr.win_gravity = NorthWestGravity;
    attr.backing_store = Always;
    mask = CWBackPixmap | CWWinGravity | CWBackingStore;
    XChangeWindowAttributes(display, win, mask, &attr);

    // Ask for events.
    mask = ButtonPressMask | KeyPressMask | ExposureMask | StructureNotifyMask;
    XSelectInput(display, win, mask);
    XSetWMProtocols(display, win, &wm_delete_window, 1);

    // Set window title
    XStoreName(display, win, "pango cairo");

    // Show the window and create a cairo surface
    XMapWindow(display, win);

    // Create surface
    surface = cairo_xlib_surface_create(display, win, visual, x, y);
    cairo_xlib_surface_set_size(surface, x, y);

    return surface;
}

int cursor_pos = 1;

void draw_text(cairo_t *cr, PangoLayout *layout, const char* text) {
    cairo_move_to(cr, 10, 20);
    cairo_set_source_rgb(cr, 0, 0, 1);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_cairo_update_layout(cr, layout);
    pango_cairo_show_layout(cr, layout);

    // Cursor
    PangoRectangle rect;
    pango_layout_index_to_pos(layout, cursor_pos, &rect);
    rect.x += 10*PANGO_SCALE;
    rect.y += 20*PANGO_SCALE;
    double d = PANGO_SCALE;
    cairo_rectangle(cr, rect.x/d - 1, rect.y/d, 2, rect.height/d);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
}

void redraw(cairo_t *cr, PangoLayout *layout) {
    cairo_push_group(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    draw_text(cr, layout, "Hello, world! abdfgylqptjkb");
    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
}

int event_loop(Display *display, cairo_surface_t *surface) {
    XEvent e;
    cairo_t *cr = cairo_create(surface);
    PangoLayout *layout = pango_cairo_create_layout(cr);

    PangoFontDescription *desc = pango_font_description_from_string("Sans 10");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    KeySym sym;
    int index;
    int trailing;
    for (;;) {
        XNextEvent(display, &e);
        switch (e.type) {
        case ButtonPress:
            pango_layout_xy_to_index(layout,
                (e.xbutton.x-10)*PANGO_SCALE,
                (e.xbutton.y-20)*PANGO_SCALE,
                &index, &trailing);
            printf("%d,%d = %d (%d)\n", e.xbutton.x, e.xbutton.y, index, trailing);
            cursor_pos = index;
            goto case_expose;

        case KeyPress:
            sym = XLookupKeysym(&e.xkey, 0);
            switch(sym) {
            case XK_Left:
                cursor_pos -= 1;
                goto case_expose;
            case XK_Right:
                cursor_pos += 1;
                goto case_expose;
            case XK_q:
                goto cleanup;
            default:
                printf("unknown key %ld\n", sym);
            }
            break;

        case KeyRelease:
            break;

        case ClientMessage:
            if (e.xclient.message_type == wm_protocols && e.xclient.data.l[0] == wm_delete_window) {
                //fprintf(stderr, "got close event\n");
                goto cleanup;
            }
            break;

        case ConfigureNotify:
            //fprintf(stderr, "got configure event\n");
            cairo_xlib_surface_set_size(surface,
                e.xconfigure.width, e.xconfigure.height);
            pango_layout_set_width(layout,
                (e.xconfigure.width - 20)*PANGO_SCALE);
            break;

        case Expose:
        case_expose:
            //fprintf(stderr, "got exposure event\n");
            redraw(cr, layout);
            cairo_surface_flush(surface);
            break;

        default:
            fprintf(stderr, "Ignoring event %d\n", e.type);
        }
    }

cleanup:
    g_object_unref(layout);
    cairo_destroy(cr);
    return 0;
}

int main() {
    Display *display;
    cairo_surface_t *surface;

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        exit(1);
    }

    wm_protocols = XInternAtom(display, "WM_PROTOCOLS", 0);
    wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", 0);

    surface = cairo_create_x11_surface(display, 300, 100);

    event_loop(display, surface);

    cairo_surface_destroy(surface);
    XCloseDisplay(display);
    return 0;
}
