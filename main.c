#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

Atom wm_protocols;
Atom wm_delete_window;

typedef struct Term Term;
struct Term {
    Display *display;
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoLayout *layout;

    char *text;
    int textlen;
    int textcap;

    int cursor_pos;
    int cursor_type;
    int border;

    char *scrollback;
};

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
    XStoreName(display, win, "magicalterm");

    // Show the window and create a cairo surface
    XMapWindow(display, win);

    // Create surface
    surface = cairo_xlib_surface_create(display, win, visual, x, y);
    cairo_xlib_surface_set_size(surface, x, y);

    return surface;
}

void draw_text(cairo_t *cr, PangoLayout *layout, const char* text, size_t len) {
    cairo_set_source_rgb(cr, 0, 0, 1);
    pango_layout_set_text(layout, text, len);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_cairo_update_layout(cr, layout);
    pango_cairo_show_layout(cr, layout);
}

void draw_cursor(Term *t) {
    PangoRectangle rect;
    pango_layout_index_to_pos(t->layout, t->cursor_pos, &rect);
    double d = PANGO_SCALE;
    rect.x += t->border*d;
    rect.y += t->border*d;
    cairo_set_source_rgb(t->cr, 0, 0, 0);
    switch (t->cursor_type) {
    default:
        cairo_rectangle(t->cr, rect.x/d, rect.y/d, rect.width/d, rect.height/d);
        break;
    case 1:
        cairo_rectangle(t->cr, rect.x/d+.5, rect.y/d+.5, rect.width/d-1, rect.height/d-1);
        cairo_set_line_width(t->cr, 1);
        cairo_stroke(t->cr);
        break;
    case 2:
        cairo_rectangle(t->cr, rect.x/d - 1, rect.y/d, 1, rect.height/d);
        cairo_rectangle(t->cr, rect.x/d - 2, rect.y/d - 2, 3, 3);
        cairo_rectangle(t->cr, rect.x/d - 2, (rect.y + rect.height)/d - 2, 3, 3);
        break;
    }
    cairo_clip(t->cr);
    cairo_paint(t->cr);
}

void term_redraw(Term *t) {
    cairo_t *cr = t->cr;
    cairo_push_group(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_move_to(cr, t->border, t->border);
    draw_text(cr, t->layout, t->text, t->textlen);
    draw_cursor(t);
    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
    cairo_surface_flush(t->surface);
}

void term_movecursor(Term *t, int n) {
    if (t->cursor_pos + n >= 0)
    if (t->cursor_pos + n <= t->textlen) {
        t->cursor_pos += n;
    }
}

int event_loop(Term *t) {
    XEvent e;
    KeySym sym;
    char buf[32];
    int n;
    int index;
    int trailing;
    for (;;) {
        XNextEvent(t->display, &e);
        switch (e.type) {
        case ButtonPress:
            pango_layout_xy_to_index(t->layout,
                (e.xbutton.x-t->border)*PANGO_SCALE,
                (e.xbutton.y-t->border)*PANGO_SCALE,
                &index, &trailing);
            printf("%d,%d = %d (%d)\n", e.xbutton.x, e.xbutton.y, index, trailing);
            t->cursor_pos = index;
            term_redraw(t);
            break;

        case KeyPress:
            sym = XLookupKeysym(&e.xkey, 0);
            switch(sym) {
            case XK_Escape:
                goto cleanup;
            case XK_Left:
                term_movecursor(t, -1);
                term_redraw(t);
                break;
            case XK_Right:
                term_movecursor(t, 1);
                term_redraw(t);
                break;
            case XK_Home:
                t->cursor_pos = 0;
                term_redraw(t);
                break;
            case XK_End:
                t->cursor_pos = t->textlen;
                term_redraw(t);
                break;
            case XK_BackSpace:
                if (t->cursor_pos == t->textlen) {
                    t->textlen--;
                }
                term_movecursor(t, -1);
                term_redraw(t);
                break;
            default:
                // TODO Xmb
                n = XLookupString(&e.xkey, buf, sizeof buf, &sym, NULL);
                if (n == 1 && 0x20 <= buf[0] && buf[0] <= 0x7e) {
                    if (t->cursor_pos < t->textcap) {
                        if (t->cursor_pos == t->textlen) {
                            t->textlen++;
                        }
                        t->text[t->cursor_pos] = buf[0];
                        t->cursor_pos += n;
                    }
                    term_redraw(t);
                    break;
                }
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
            cairo_xlib_surface_set_size(t->surface,
                e.xconfigure.width, e.xconfigure.height);
            pango_layout_set_width(t->layout,
                (e.xconfigure.width - 2*t->border)*PANGO_SCALE);
            break;

        case Expose:
            //fprintf(stderr, "got exposure event\n");
            term_redraw(t);
            break;

        default:
            fprintf(stderr, "Ignoring event %d\n", e.type);
        }
    }

cleanup:
    return 0;
}

int main() {
    PangoFontDescription *desc;
    Term t;

    t.display = XOpenDisplay(NULL);
    if (t.display == NULL) {
        exit(1);
    }

    wm_protocols = XInternAtom(t.display, "WM_PROTOCOLS", 0);
    wm_delete_window = XInternAtom(t.display, "WM_DELETE_WINDOW", 0);

    t.surface = cairo_create_x11_surface(t.display, 300, 100);
    if (t.surface == NULL) {
        exit(1);
    }
    t.cr = cairo_create(t.surface);
    if (t.cr == NULL) {
        exit(1);
    }
    t.layout = pango_cairo_create_layout(t.cr);
    if (t.layout == NULL) {
        exit(1);
    }

    desc = pango_font_description_from_string("Sans 10");
    pango_layout_set_font_description(t.layout, desc);
    pango_font_description_free(desc);

    char text[256] = "Hello, world! abdfgylqptjkb";
    t.text = text;
    t.textlen = strlen(t.text);
    t.textcap = sizeof text;
    t.cursor_pos = 0;
    t.cursor_type = 1;
    t.border = 2;

    event_loop(&t);

    g_object_unref(t.layout);
    cairo_destroy(t.cr);
    cairo_surface_destroy(t.surface);
    XCloseDisplay(t.display);
    return 0;
}
