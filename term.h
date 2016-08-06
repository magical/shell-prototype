// Terminal:
//   talks to X
//   talks to the shell

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
