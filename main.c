#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <X11/Xlib.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

int width = 300;

cairo_surface_t *cairo_create_x11_surface(int x, int y) {
    Display *display;
    Visual *visual;
    Window win;
    int screen;
    cairo_surface_t *surface;

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        exit(1);
    }
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

    // Set window title
    XStoreName(display, win, "pango cairo");

    XMapWindow(display, win);
    surface = cairo_xlib_surface_create(display, win, visual, x, y);
    cairo_xlib_surface_set_size(surface, x,y);
    return surface;
}

void cairo_close_x11_surface(cairo_surface_t *surface)
{
    Display *display = cairo_xlib_surface_get_display(surface);
    cairo_surface_destroy(surface);
    XCloseDisplay(display);
}

void draw_text(cairo_t *cr, const char* text) {
    PangoLayout *layout;
    PangoFontDescription *desc;

    cairo_move_to(cr, 10, 20);
    layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_width(layout, (width - 20)*PANGO_SCALE);
    desc = pango_font_description_from_string("Dina 10");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    cairo_set_source_rgb(cr, 0, 0, 1);
    pango_cairo_update_layout(cr, layout);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}


void redraw(cairo_t *cr) {
    cairo_push_group(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    draw_text(cr, "Hello, world!");
    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
}
    

int event_loop(cairo_surface_t *surface) {
    XEvent e;
    Display *display = cairo_xlib_surface_get_display(surface);
    Window win = cairo_xlib_surface_get_drawable(surface);
    Atom wm_protocols = XInternAtom(display, "WM_PROTOCOLS", 0);
    Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", 0);
    XSetWMProtocols(display, win, &wm_delete_window, 1);
    cairo_t *cr;
    cr = cairo_create(surface);
    for (;;) {
        XNextEvent(display, &e);
        switch (e.type) {
        case ClientMessage:
            if (e.xclient.message_type == wm_protocols && e.xclient.data.l[0] == wm_delete_window) {
                //fprintf(stderr, "got close event\n");
                //XDestroyWindow(display, win);
                return 0;
            }
            break;
        case ConfigureNotify:
            //fprintf(stderr, "got configure event\n");
            cairo_xlib_surface_set_size(surface,
                e.xconfigure.width, e.xconfigure.height);
            width = e.xconfigure.width;
            break;
        case Expose:
            //fprintf(stderr, "got exposure event\n");
            redraw(cr);
            cairo_surface_flush(surface);
            break;
        default:
            fprintf(stderr, "Ignoring event %d\n", e.type);
        }
        XFlush(display);
    }
    cairo_destroy(cr);
}

int main() {
    cairo_surface_t *surface;

    surface = cairo_create_x11_surface(300, 100);
    event_loop(surface);
    cairo_close_x11_surface(surface);

    return 0;
}
