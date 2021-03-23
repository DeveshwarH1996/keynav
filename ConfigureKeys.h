#ifndef CONFIGURE_KEYS_H
#define CONFIGURE_KEYS_H

#define WININFO_MAXHIST (100)

void restart();

class ConfigureKeys {
    public:    
        ConfigureKeys(char **argv);
        int Initialize(int argc, char **argv);
        void RunLoop();

    struct appstate_t {
        appstate_t();
        int active;
        int dragging;
        int need_draw;
        int need_moveresize;
        enum { record_getkey, record_ing, record_off } recording;
        int playback;

        int grid_nav; /* 1 if grid nav is active */
        enum { GRID_NAV_COL, GRID_NAV_ROW } grid_nav_state;
        enum { GRID_LABEL_NONE, GRID_LABEL_AA } grid_label;
        int grid_nav_col;
        int grid_nav_row;
    };

    typedef enum { HANDLE_CONTINUE, HANDLE_STOP } handler_info_t;

    typedef struct recording {
        int keycode;
        std::vector<std::string> commands;
    } recording_t;

    std::vector<recording_t> recordings;
    recording_t active_recording;
    std::string recordings_filename;

    typedef struct wininfo {
        int x;
        int y;
        int w;
        int h;
        int grid_rows;
        int grid_cols;
        int border_thickness;
        int center_cut_size;
        int curviewport;
    } wininfo_t;

    typedef struct mouseinfo {
        int x;
        int y;
    } mouseinfo_t;

    typedef struct viewport {
        int x;
        int y;
        int w;
        int h;
        int screen_num;
        Screen *screen;
        Window root;
    } viewport_t;

    typedef struct keybinding {
        std::string commands;
        int keycode;
        int mods;
    } keybinding_t;
    typedef struct startkey {
        int keycode;
        int mods;
    } startkey_t;

    std::vector<startkey_t> startkeys;
    std::vector<keybinding_t> keybindings;

    wininfo_t wininfo;
    mouseinfo_t mouseinfo;
    std::vector<viewport_t> viewports;
    int xinerama;
    int daemonize;
    int is_daemon;

    Display *dpy;
    Window zone;
    std::vector<XRectangle> clip_rectangles;

    GC canvas_gc;
    Pixmap canvas;
    cairo_surface_t *canvas_surface;
    cairo_t *canvas_cairo;
    Pixmap shape;
    cairo_surface_t *shape_surface;
    cairo_t *shape_cairo;

    xdo_t *xdo;
    appstate_t appstate;

    int drag_button;
    char drag_modkeys[128];
    // /* history tracking */
    wininfo_t wininfo_history[WININFO_MAXHIST]; /* XXX: is 100 enough? */
    int wininfo_history_cursor;
    struct dispatch_t {
        std::string command;
        void (ConfigureKeys::*func)(char *args);
    };

    std::vector<dispatch_t> dispatch;
private: 
    char **mArgv;
    void AssignDispatch();
protected:

    void defaults();

    void cmd_cell_select(char *args);
    void cmd_click(char *args);
    void cmd_cursorzoom(char *args);
    void cmd_cut_down(char *args);
    void cmd_cut_left(char *args);
    void cmd_cut_right(char *args);
    void cmd_cut_up(char *args);
    void cmd_daemonize(char *args);
    void cmd_doubleclick(char *args);
    void cmd_drag(char *args);
    void cmd_end(char *args);
    void cmd_toggle_start(char *args);
    void cmd_grid(char *args);
    void cmd_grid_nav(char *args);
    void cmd_history_back(char *args);
    void cmd_loadconfig(char *args);
    void cmd_move_down(char *args);
    void cmd_move_left(char *args);
    void cmd_move_right(char *args);
    void cmd_move_up(char *args);
    void cmd_quit(char *args);
    void cmd_record(char *args);
    void cmd_playback(char *args);
    void cmd_restart(char *args);
    void cmd_shell(char *args);
    void cmd_start(char *args);
    void cmd_resume(char *args);
    void cmd_warp(char *args);
    void cmd_windowzoom(char *args);

    void update();
    void correct_overflow();
    void handle_keypress(XKeyEvent *e);
    void handle_commands(const std::string& Commands);
    void parse_config();
    int parse_config_line(char *line);
    void save_history_point();
    void restore_history_point(int moves_ago);
    void cell_select(int x, int y);
    handler_info_t handle_recording(XKeyEvent *e);
    handler_info_t handle_gridnav(XKeyEvent *e);

    void query_screens();
    void query_screen_xinerama();
    void query_screen_normal();
    int viewport_sort(const void *a, const void *b);
    int query_current_screen();
    int query_current_screen_xinerama();
    int query_current_screen_normal();
    void viewport_left();
    void viewport_right();
    int pointinrect(int px, int py, int rx, int ry, int rw, int rh);
    int percent_of(int num, char *args, float default_val);
    void recordings_save(const std::string& filename);
    void parse_recordings(const char *filename);
    void openpixel(Display *dpy, Window zone, mouseinfo_t *mouseinfo);
    void closepixel(Display *dpy, Window zone, mouseinfo_t *mouseinfo);
    void cmd_start_base(char *args, bool ResetCoordinates);
    int parse_keycode(char *keyseq);
    int parse_mods(char *keyseq);
    void addbinding(int keycode, int mods, char *commands);
    void parse_config_file(const char* file);
    void updatecliprects(wininfo_t *info);
    void updategrid(Window win, ConfigureKeys::wininfo_t *info, int apply_clip, int draw);
    void updategridtext(Window win, ConfigureKeys::wininfo_t *info, int apply_clip, int draw);
    void grab_keyboard();
};
#endif