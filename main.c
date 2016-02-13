#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <locale.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>
#include "utf8.h"
#include "shell.h"

int debug;

Atom wm_protocols;
Atom wm_delete_window;

typedef struct Term Term;
struct Term {
    // X stuff
    Display *display;
    XIM im;
    XIC ic;

    // Cairo stuff
    cairo_surface_t *surface;
    cairo_t *cr;
    cairo_pattern_t *fg;
    cairo_pattern_t *bg;
    PangoLayout *layout;
    int border;
    bool dirty;

    // shell
    Shell shell;
    bool exiting;

    int cursor_pos; // cursor position in bytes
    int cursor_type; // cursor shape
    double charwidth;
    double charheight;
    int inputx; // where the input is on the screen
    int inputy;
    int scroll; // scrollback y position in pixels
    int height; // height of window

    // edit buffer
    char *edit;
    int editlen;
    int editcap;

    // scrollback buffer
    char *hist;
    int histlen;
    int histcap;
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

    return surface;
}

void draw_text(cairo_t *cr, PangoLayout *layout, cairo_pattern_t *fg, const char* text, size_t len) {
    cairo_set_source(cr, fg);
    pango_layout_set_text(layout, text, len);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_cairo_show_layout(cr, layout);
}

void draw_cursor(Term *t, int x, int y) {
    PangoRectangle rect;
    pango_layout_index_to_pos(t->layout, t->cursor_pos, &rect);
    pango_extents_to_pixels(&rect, NULL);
    rect.x += x;
    rect.y += y - t->scroll;
    cairo_set_source(t->cr, t->fg);
    switch (t->cursor_type) {
    default:
    case 0:
        // solid box
        if (rect.width == 0) {
            rect.width = t->charwidth;
        }
        cairo_rectangle(t->cr, rect.x, rect.y, rect.width, rect.height);
        cairo_clip(t->cr);
        cairo_paint(t->cr);

        // redraw glyph
        cairo_set_source(t->cr, t->bg);
        cairo_rectangle(t->cr, rect.x, rect.y, rect.width, rect.height);
        cairo_clip(t->cr);
        cairo_move_to(t->cr, x, y - t->scroll);
        // assume we just rendered the text
        pango_cairo_show_layout(t->cr, t->layout);
        break;
    case 1:
        // box outline
        cairo_rectangle(t->cr, rect.x+0.5, rect.y+0.5, rect.width-1, rect.height-1);
        cairo_set_line_width(t->cr, 1);
        cairo_stroke(t->cr);
        break;
    case 2:
        // vertical line
        cairo_rectangle(t->cr, rect.x - 1, rect.y, 1, rect.height);
        cairo_rectangle(t->cr, rect.x - 2, rect.y, 3, 3);
        cairo_rectangle(t->cr, rect.x - 2, rect.y + rect.height - 3, 3, 3);
        cairo_fill(t->cr);
        break;
    }
}

void term_redraw(Term *t) {
    PangoRectangle rect;
    cairo_push_group(t->cr);

    // Draw background
    cairo_set_source(t->cr, t->bg);
    cairo_paint(t->cr);

    // Draw scrollback
    cairo_move_to(t->cr, t->border, t->border - t->scroll);
    draw_text(t->cr, t->layout, t->fg, t->hist, t->histlen);

    // Draw input (below scrollback)
    pango_layout_get_pixel_extents(t->layout, NULL, &rect);
    t->inputx = t->border;
    t->inputy = rect.y + rect.height;
    cairo_move_to(t->cr, t->inputx, t->inputy - t->scroll);
    draw_text(t->cr, t->layout, t->fg, t->edit, t->editlen);

    // Draw cursor
    draw_cursor(t, t->border, rect.y+rect.height);

    cairo_pop_group_to_source(t->cr);
    cairo_paint(t->cr);
    cairo_surface_flush(t->surface);
    XFlush(t->display);
    t->dirty = false;
}

void term_resize(Term *t, int width, int height) {
    cairo_xlib_surface_set_size(t->surface, width, height);
    pango_layout_set_width(t->layout, (width - 2*t->border)*PANGO_SCALE);
    t->height = height;
    t->dirty = true;
}

void term_scroll(Term *t, int dir) {
    t->scroll += (t->height - 10) * dir;
    if (t->scroll < 0) {
        t->scroll = 0;
    }
    t->dirty = true;
}

void term_movecursor(Term *t, int n) {
    if (n > 0) {
        n = utf8decode(t->edit+t->cursor_pos, t->editlen-t->cursor_pos, NULL);
        //printf("%.*s\n", n, t->text+t->cursor_pos);
    } else if (n < 0) {
        n = -utf8decodelast(t->edit, t->cursor_pos, NULL);
        //printf("%.*s\n", -n, t->edit+t->cursor_pos+n);
    } else {
        return;
    }
    if (t->cursor_pos + n >= 0)
    if (t->cursor_pos + n <= t->editlen) {
        t->cursor_pos += n;
    }
    t->dirty = true;
}

void term_appendhist(Term *t, char *buf, size_t len) {
    if (t->histcap - t->histlen < len) {
        void *v;
        int newcap;
        newcap = t->histcap * 2;
        if (newcap - t->histlen < len) {
            newcap = t->histlen + len;
        }
        if (newcap < t->histcap) {
            printf("overflow\n");
            exit(1);
        }
        v = realloc(t->hist, newcap);
        if (v == NULL) {
            perror("realloc");
            exit(1);
        }
        t->hist = v;
        t->histcap = newcap;
    }
    memmove(t->hist+t->histlen, buf, len);
    t->histlen += len;
    t->dirty = true;
}

void term_inserttext(Term *t, char *buf, size_t len) {
    int i = t->cursor_pos;
    if (t->editcap - t->editlen < len) {
        return;
    }
    memmove(t->edit+i+len, t->edit+i, t->editlen-i);
    memmove(t->edit+i, buf, len);
    t->editlen += len;
    t->cursor_pos += len;
    t->dirty = true;
}

void term_backspace(Term *t) {
    int i = t->cursor_pos;
    int len;
    int32_t r;
    len = utf8decodelast(t->edit, i, &r);
    if (len == 0) {
        return;
    }
    if (i < t->editlen) {
        memmove(t->edit+i-len, t->edit+i, t->editlen-i);
    }
    t->editlen -= len;
    t->cursor_pos -= len;
    t->dirty = true;
}

void term_set_font(Term *t, const char *name) {
    PangoFontDescription *desc;
    PangoFontMetrics *metrics;
    int width, height;
    desc = pango_font_description_from_string(name);
    metrics = pango_context_get_metrics(
        pango_layout_get_context(t->layout), desc, NULL);
    pango_layout_set_font_description(t->layout, desc);
    width = pango_font_metrics_get_approximate_char_width(metrics);
    height = pango_font_metrics_get_ascent(metrics) +
                    pango_font_metrics_get_descent(metrics);
    pango_font_metrics_unref(metrics);
    pango_font_description_free(desc);
    t->charwidth = pango_units_to_double(width);
    t->charheight = pango_units_to_double(height);
    t->dirty = true;
}

void xevent(Term *t, XEvent *xev) {
    KeySym sym;
    char buf[32];
    int n;
    int index;
    int trailing;

    switch (xev->type) {
    case ButtonPress:
        pango_layout_xy_to_index(t->layout,
            (xev->xbutton.x - t->inputx)*PANGO_SCALE,
            (xev->xbutton.y - t->inputy)*PANGO_SCALE,
            &index, &trailing);
        //printf("%d,%d = %d (%d)\n", xev->xbutton.x, xev->xbutton.y, index, trailing);
        t->cursor_pos = index;
        t->dirty = true;
        break;

    case KeyPress:
        n = Xutf8LookupString(t->ic, &xev->xkey, buf, sizeof buf, &sym, NULL);
        switch(sym) {
        case XK_Escape:
            t->exiting = true;
            break;
        case XK_Return:
            if (shell_running(&t->shell)) {
                shell_write(&t->shell, t->edit, t->editlen);
                shell_write(&t->shell, "\n", 1);
            } else {
                term_appendhist(t, "% ", 2);
                term_appendhist(t, t->edit, t->editlen);
                term_appendhist(t, "\n", 1);
                if (t->editlen < t->editcap) {
                    t->edit[t->editlen] = '\0';
                    shell_run(&t->shell, t->edit);
                }
            }
            t->editlen = 0;
            t->cursor_pos = 0;
            break;
        case XK_Left:
            term_movecursor(t, -1);
            break;
        case XK_Right:
            term_movecursor(t, 1);
            break;
        case XK_Home:
            t->cursor_pos = 0;
            t->dirty = true;
            break;
        case XK_End:
            t->cursor_pos = t->editlen;
            t->dirty = true;
            break;
        case XK_Page_Up:
            term_scroll(t, -1);
            break;
        case XK_Page_Down:
            term_scroll(t, +1);
            break;
        case XK_F1:
            t->cursor_type = (t->cursor_type + 3 - 1) % 3;
            t->dirty = true;
            break;
        case XK_F2:
            t->cursor_type = (t->cursor_type + 1) % 3;
            t->dirty = true;
            break;
        case XK_F3:
            term_set_font(t, "Sans 10");
            break;
        case XK_F4:
            term_set_font(t, "Dina 10");
            break;
        case XK_BackSpace:
            term_backspace(t);
            break;
        default:
            if (n == 1 && buf[0] < 0x20) {
                if (buf[0] == 4 && !shell_running(&t->shell)) {
                    t->exiting = true;
                    break;
                }
                printf("keysym %ld, state=%d\n", sym, xev->xkey.state);
                shell_write(&t->shell, t->edit, t->editlen);
                shell_write(&t->shell, buf, 1);
                t->editlen = 0;
                t->cursor_pos = 0;
                break;
            }
            //printf("key %ld, n=%d, buf=%.*s\n", sym, n, n, buf);
            term_inserttext(t, buf, n);
            break;
        }
        break;

    case KeyRelease:
        break;

    case ClientMessage:
        if (xev->xclient.message_type == wm_protocols && xev->xclient.data.l[0] == wm_delete_window) {
            //fprintf(stderr, "got close event\n");
            t->exiting = true;
        }
        break;

    case ConfigureNotify:
        if (debug) {
            fprintf(stderr, "got configure event\n");
        }
        term_resize(t, xev->xconfigure.width, xev->xconfigure.height);
        break;

    case Expose:
        //fprintf(stderr, "got exposure event\n");
        term_redraw(t);
        break;

    default:
        fprintf(stderr, "Ignoring event %d\n", xev->type);
    }
}

int event_loop(Term *t) {
    XEvent xev;
    struct timeval tv;
    struct itimerspec its = {
        .it_interval = {0, 1e9/30}, // 30 fps
        .it_value = {0, 1e9/30},
    };
    struct timeval now, then;
    char buf[256];
    fd_set rfd;
    int nevents;
    int timerfd;
    int xfd;
    int maxfd;
    int err;

    xfd = ConnectionNumber(t->display);

    timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (timerfd < 0) {
        perror("timerfd");
        return -1;
    }
    err = timerfd_settime(timerfd, 0, &its, NULL);
    if (err < 0) {
        perror("timerfd");
        return -1;
    }

    maxfd = t->shell.fd;
    if (xfd > maxfd) {
        maxfd = xfd;
    }
    if (timerfd > maxfd) {
        maxfd = timerfd;
    }
    if (selfpipe.r > maxfd) {
        maxfd = selfpipe.r;
    }

    t->dirty = true;
    t->exiting = false;
    while (!t->exiting) {
        FD_ZERO(&rfd);
        FD_SET(t->shell.fd, &rfd);
        FD_SET(selfpipe.r, &rfd);
        FD_SET(xfd, &rfd);
        FD_SET(timerfd, &rfd);

        // timeout = 1 second
        // because i feel like we shouldn't block forever
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        errno = 0;
        err = select(maxfd+1, &rfd, NULL, NULL, &tv);
        if (err < 0) {
            switch(errno) {
            case EINTR:
                continue;
            default:
                perror("select");
                return -1;
            }
        }

        if (err == 0) {
            if (debug) {
                printf("timeout\n");
            }
            term_redraw(t);
            continue;
        }

        nevents = err;
        if (debug) {
            gettimeofday(&then, NULL);
        }

        if (FD_ISSET(selfpipe.r, &rfd)) {
            err = read(selfpipe.r, buf, 1);
            if (err < 0) {
                perror("read selfpipe");
                continue;
            }
            shell_reap(&t->shell);
        }

        if (FD_ISSET(t->shell.fd, &rfd)) {
            if (debug) {
                printf("shell\n");
            }
            err = shell_read(&t->shell, buf, sizeof buf);
            if (err < 0) {
                perror("read shell");
                continue;
            }
            //printf("read %d bytes: %s\n", err, buf);
            if (err > 0) {
                term_appendhist(t, buf, err);
            }
        }

        if (FD_ISSET(xfd, &rfd)) {
            if (debug) {
                printf("xevent\n");
            }
            while (XPending(t->display)) {
                XNextEvent(t->display, &xev);
                if (XFilterEvent(&xev, None)) {
                    continue;
                }
                xevent(t, &xev);
            }
        }

        if (FD_ISSET(timerfd, &rfd)) {
            read(timerfd, buf, sizeof buf);
            if (t->dirty) {
                if (debug) {
                    printf("timer redraw\n");
                }
                term_redraw(t);
            }
        }

        if (debug) {
            gettimeofday(&now, NULL);
            int diff = (now.tv_sec - then.tv_sec)*1000000 + (now.tv_usec - then.tv_usec);
            printf("handled %d events in %d µ\n", nevents, diff);
        }
    }

    return 0;
}

int main() {
    Term t;
    int err;

    setlocale(LC_ALL, "");
    t.display = XOpenDisplay(NULL);
    if (t.display == NULL) {
        exit(1);
    }

    t.im = XOpenIM(t.display, NULL, NULL, NULL);
    if (t.im == NULL) {
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

    // Create input context
    t.ic = XCreateIC(t.im,
        XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
        XNClientWindow, cairo_xlib_surface_get_drawable(t.surface),
        XNFocusWindow, cairo_xlib_surface_get_drawable(t.surface),
        NULL);
    if (t.ic == NULL) {
        fprintf(stderr, "couldn't create input context\n");
        exit(1);
    }
    XSetICFocus(t.ic);

    term_set_font(&t, "Sans 10");

    //char text[256] = "Hello, world! Pokémon. ポケモン. ポケットモンスター";
    char text[256] = "";
    t.edit = text;
    t.editlen = strlen(t.edit);
    t.editcap = sizeof text;
    t.cursor_pos = 0;
    t.cursor_type = 0;
    t.border = 2;
    t.inputx = t.border;
    t.inputy = t.border;
    t.scroll = 0;

    t.hist = NULL;
    t.histlen = 0;
    t.histcap = 0;

    t.fg = cairo_pattern_create_rgb(0, 0, 0);
    t.bg = cairo_pattern_create_rgb(1, 1, 0xd5/255.0);

    err = shell_init(&t.shell);
    if (err < 0) {
        exit(1);
    }

    event_loop(&t);

    shell_exit(&t.shell);
    cairo_pattern_destroy(t.fg);
    cairo_pattern_destroy(t.bg);
    g_object_unref(t.layout);
    cairo_destroy(t.cr);
    cairo_surface_destroy(t.surface);
    XDestroyIC(t.ic);
    XCloseIM(t.im);
    XCloseDisplay(t.display);
    return 0;
}
