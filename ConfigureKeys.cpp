#include <assert.h> 
#include <iostream>
#include <vector>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>
#include <glib.h>
#include <cairo-xlib.h>

#include <algorithm>
#include <cstring>

struct iequal
{
    bool operator()(int c1, int c2) const
    {
        return std::toupper(c1) == std::toupper(c2);
    }
};
 
bool iequals(const std::string& str1, const std::string& str2)
{
    return std::equal(str1.begin(), str1.end(), str2.begin(), iequal());
}



#ifdef PROFILE_THINGS
#include <time.h>
#endif
extern "C" {
  #include <xdo.h>
}

#ifndef GLOBAL_CONFIG_FILE
#define GLOBAL_CONFIG_FILE "/etc/keynavrc"
#endif /* GLOBAL_CONFIG_FILE */

extern char **environ;

#include "ConfigureKeys.h"

#define ISACTIVE (appstate.active)
#define ISDRAGGING (appstate.dragging)

int ConfigureKeys::Initialize(int argc, char **argv) {
  
  char *pcDisplay;
  const char *prog = argv[0];

  if ((pcDisplay = getenv("DISPLAY")) == NULL) {
    fprintf(stderr, "Error: DISPLAY environment variable not set\n");
    return EXIT_FAILURE;
  }

  if ((dpy = XOpenDisplay(pcDisplay)) == NULL) {
    fprintf(stderr, "Error: Can't open display: %s\n", pcDisplay);
    return EXIT_FAILURE;
  }

  xdo = xdo_new_with_opened_display(dpy, pcDisplay, False);

  parse_config();
  query_screens();

  if (argc == 2) {
    handle_commands(argv[1]);
  } else if (argc > 2) {
    fprintf(stderr, "Usage: %s [command string]\n", prog);
    fprintf(stderr, "Did you quote your command string?\n");
    fprintf(stderr, "  Example: %s 'loadconfig mykeynavrc,daemonize'\n", prog);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}


ConfigureKeys::appstate_t::appstate_t() {
    active = 0;
    dragging = 0;
    recording = record_off;
    grid_nav = 0;
}
void ConfigureKeys::AssignDispatch() { 
    dispatch.push_back({"cut-up", &ConfigureKeys::cmd_cut_up});
    dispatch.push_back({"ut-down", &ConfigureKeys::cmd_cut_down});
    dispatch.push_back({"cut-left", &ConfigureKeys::cmd_cut_left});
    dispatch.push_back({"cut-right", &ConfigureKeys::cmd_cut_right});
    dispatch.push_back({"move-up", &ConfigureKeys::cmd_move_up});
    dispatch.push_back({"move-down", &ConfigureKeys::cmd_move_down});
    dispatch.push_back({"move-left", &ConfigureKeys::cmd_move_left});
    dispatch.push_back({"move-right", &ConfigureKeys::cmd_move_right});
    dispatch.push_back({"cursorzoom", &ConfigureKeys::cmd_cursorzoom});
    dispatch.push_back({"windowzoom", &ConfigureKeys::cmd_windowzoom});
    // Grid commands
    dispatch.push_back({"grid", &ConfigureKeys::cmd_grid});
    dispatch.push_back({"grid-nav", &ConfigureKeys::cmd_grid_nav});
    dispatch.push_back({"cell-select", &ConfigureKeys::cmd_cell_select});
    // Mouse activity
    dispatch.push_back({"warp", &ConfigureKeys::cmd_warp});
    dispatch.push_back({"click", &ConfigureKeys::cmd_click});
    dispatch.push_back({"doubleclick", &ConfigureKeys::cmd_doubleclick});
    dispatch.push_back({"drag", &ConfigureKeys::cmd_drag});
    // Other commands.
    dispatch.push_back({"loadconfig", &ConfigureKeys::cmd_loadconfig});
    dispatch.push_back({"daemonize", &ConfigureKeys::cmd_daemonize});
    dispatch.push_back({"sh", &ConfigureKeys::cmd_shell});
    dispatch.push_back({"start", &ConfigureKeys::cmd_start});
    dispatch.push_back({"resume", &ConfigureKeys::cmd_resume});
    dispatch.push_back({"end", &ConfigureKeys::cmd_end});
    dispatch.push_back({"toggle-start", &ConfigureKeys::cmd_toggle_start});
    dispatch.push_back({"history-back", &ConfigureKeys::cmd_history_back});
    dispatch.push_back({"quit", &ConfigureKeys::cmd_quit});
    dispatch.push_back({"restart", &ConfigureKeys::cmd_restart});
    dispatch.push_back({"record", &ConfigureKeys::cmd_record});
    dispatch.push_back({"playback", &ConfigureKeys::cmd_playback});
    dispatch.push_back({NULL, NULL});
}

ConfigureKeys::ConfigureKeys(char **argv) : mArgv(argv),
    appstate()
 {
    xinerama = 0;
    daemonize = 0;
    is_daemon = False;
    drag_button = 0;
    wininfo_history_cursor = 0;
}

int ConfigureKeys::parse_keycode(char *keyseq) {
  std::string keyString(keyseq);
  std::string last_tok;
  int keycode = 0;
  int keysym = 0;


  auto pos = keyString.find_last_of('+');  
  if (pos == std::string::npos) {
    last_tok = keyString;
  } 
  else {
      last_tok = keyString.substr(pos+1);
  }
  
  keysym = XStringToKeysym(last_tok.data());
  if (keysym == NoSymbol) {
    fprintf(stderr, "No keysym found for '%s' in sequence '%s'\n",
            last_tok.data(), keyseq);
    /* At this point, we'll be returning 0 for keycode */
  } else {
    /* Valid keysym */
    keycode = XKeysymToKeycode(dpy, keysym);
    if (keycode == 0) {
      fprintf(stderr, "Unable to lookup keycode for %s\n", last_tok.data());
    }
  }
  return keycode;
}

int ConfigureKeys::parse_mods(char *keyseq) {
  std::string keyString(keyseq);
  std::string last_tok;
  std::vector<std::string> mods;
  int modmask = 0;
  size_t pos = 0, nextPos = 0;

  while (nextPos = keyString.find('+',pos) != std::string::npos) {    
    mods.push_back(keyString.substr(pos+1, nextPos-pos-1));
    pos = nextPos;
  }

  int i = 0;
  /* Use all but the last token as modifiers */
  const char **symbol_map = xdo_get_symbol_map();
  for (auto& mod : mods) {
    KeySym keysym = 0;
    int j = 0;
    for (j = 0; symbol_map[j] != NULL; j+=2) {
      if (iequals(mod.data(), symbol_map[j])) {
        mod.append(symbol_map[j + 1]);
      }
    }

    keysym = XStringToKeysym(mod.data());
    //printf("%s => %d\n", mod, keysym);
    if ((keysym == XK_Shift_L) || (keysym == XK_Shift_R))
      modmask |= ShiftMask;
    if ((keysym == XK_Control_L) || (keysym == XK_Control_R))
      modmask |= ControlMask;
    if ((keysym == XK_Alt_L) || (keysym == XK_Alt_R))
      modmask |= Mod1Mask;
    if ((keysym == XK_Super_L) || (keysym == XK_Super_R)
        || (keysym == XK_Hyper_L) || (keysym == XK_Hyper_R))
      modmask |= Mod4Mask;
    if (iequals(mod, "mod1"))
      modmask |= Mod1Mask;
    // See masking of state in handle_keypress
    if (iequals(mod, "mod2"))
      printf("Error in configuration: keynav does not support mod2 modifier, but other modifiers are supported.");
    if (iequals(mod, "mod3"))
      modmask |= Mod3Mask;
    if (iequals(mod, "mod4"))
      modmask |= Mod4Mask;
    if (iequals(mod, "mod5"))
      modmask |= Mod5Mask;

    /* 'xmodmap' will output the current modN:KeySym mappings */
  }

  return modmask;
}

void ConfigureKeys::addbinding(int keycode, int mods, char *commands) {
  int i;

  // Check if we already have a binding for this, if so, override it.
  for (auto& kbt : keybindings) {
    if (kbt.keycode == keycode && kbt.mods == mods) {
      kbt.commands = commands;
      return;
    }
  }

  keybinding_t keybinding;
  keybinding.commands = commands;
  keybinding.keycode = keycode;
  keybinding.mods = mods;
  keybindings.push_back(keybinding);

  if (!std::strncmp(commands, "start", 5) || !std::strncmp(commands, "toggle-start", 12) || !std::strncmp(commands, "resume", 6) ) {
    int i = 0;
    startkey_t startkey;
    startkey.keycode = keycode;
    startkey.mods = mods;
    startkeys.push_back(startkey);
    /* Grab on all screen root windows */
    for (i = 0; i < ScreenCount(dpy); i++) {
      Window root = RootWindow(dpy, i);
      XGrabKey(dpy, keycode, mods, root, False, GrabModeAsync, GrabModeAsync);
      XGrabKey(dpy, keycode, mods | LockMask, root, False, GrabModeAsync, GrabModeAsync);
      XGrabKey(dpy, keycode, mods | Mod2Mask, root, False, GrabModeAsync, GrabModeAsync);
      XGrabKey(dpy, keycode, mods | LockMask | Mod2Mask, root, False, GrabModeAsync, GrabModeAsync);
    }
  }

  if (!strncmp(commands, "record", 6)) {
    std::string path(commands + 6);
    std::string newrecordingpath;

    auto pos = path.find_first_not_of(' ', 0);
    if (pos != std::string::npos)
        path = path.substr(pos,path.length()- pos);

    /* If args is nonempty, try to use it as the file to store recordings in */
    if (path.length() != 0 && path[0] != '\0') {
      /* Handle ~/ swapping in for actual homedir */
      if (path.compare(0, 2, "~/")) {
        newrecordingpath = getenv("HOME") + path.substr(2, path.length()- 2);
      } else {
        newrecordingpath = std::string(path);
      }

      /* Fail if we try to set the record file to another name than we set
       * previously */
      if (recordings_filename.length() != 0
          && (recordings_filename.compare(0, newrecordingpath.length(),newrecordingpath))) {
        newrecordingpath.clear();
        fprintf(stderr,
                "Recordings file already set to '%s', you tried to\n"
                "set it to '%s'. Keeping original value.\n",
                recordings_filename.data(), path.data());
      } else {
        recordings_filename = newrecordingpath;
        parse_recordings(recordings_filename.data());
      }
    }
  } /* special config handling for 'record' */
}

void ConfigureKeys::parse_config_file(const char* file) {
  FILE *fp = NULL;
#define LINEBUF_SIZE 512
  char line[LINEBUF_SIZE];
  int lineno = 0;

  if (file[0] == '~') {
    const char *homedir = getenv("HOME");

    if (homedir != NULL) {
      char *rcfile = NULL;
      asprintf(&rcfile, "%s/%s", homedir, file + 1 /* skip first char '~' */);
      parse_config_file(rcfile);
      free(rcfile);
      return;
    } else {
      fprintf(stderr,
              "No HOME set in environment. Can't expand '%s' (fatal error)\n",
              file);
      /* This is fatal. */
      exit(EXIT_FAILURE);
    }
  } /* if file[0] == '~' */

  fp = fopen(file, "r");

  /* Silently ignore file read errors */
  if (fp == NULL) {
    //fprintf(stderr, "Error trying to open file for read '%s'\n", file);
    //perror("Error");
    return;
  }
  /* fopen succeeded */
  while (fgets(line, LINEBUF_SIZE, fp) != NULL) {
    lineno++;

    /* Kill the newline */
    while (line[strlen(line) - 1] == '\n' ||
           line[strlen(line) - 1] == '\r')
    *(line + strlen(line) - 1) = '\0';

    if (parse_config_line(line) != 0) {
      fprintf(stderr, "Error with config %s:%d: %s\n", file, lineno, line);
    }
  }
  fclose(fp);
}

void ConfigureKeys::parse_config() {
  char *homedir;

  keybindings.clear();
  startkeys.clear();
  recordings.clear();

  //defaults();
  parse_config_file(GLOBAL_CONFIG_FILE);
  parse_config_file("~/.keynavrc");

  // support XDG Base Directory
  char *config_home = getenv("XDG_CONFIG_HOME");
  char *user_config_file;

  if (config_home &&
      asprintf(&user_config_file, "%s/keynav/keynavrc", config_home) != -1) {
    parse_config_file(user_config_file);
    free(user_config_file);
  } else {
    // standard default if XDG_CONFIG_HOME is not set
    parse_config_file("~/.config/keynav/keynavrc");
  }
}

void ConfigureKeys::defaults() {
  char *tmp;
  int i;
  char *default_config[] = {
    "clear",
    "ctrl+semicolon start",
    "Escape end",
    "ctrl+bracketleft end", /* for vi people who use ^[ */
    "q record ~/.keynav_macros",
    "shift+at playback",
    "a history-back",
    "h cut-left",
    "j cut-down",
    "k cut-up",
    "l cut-right",
    "shift+h move-left",
    "shift+j move-down",
    "shift+k move-up",
    "shift+l move-right",
    "space warp,click 1,end",
    "Return warp,click 1,end",
    "semicolon warp,end",
    "w warp",
    "t windowzoom",
    "c cursorzoom 300 300",
    "e end",
    "1 click 1",
    "2 click 2",
    "3 click 3",
    "ctrl+h cut-left",
    "ctrl+j cut-down",
    "ctrl+k cut-up",
    "ctrl+l cut-right",
    "y cut-left,cut-up",
    "u cut-right,cut-up",
    "b cut-left,cut-down",
    "n cut-right,cut-down",
    "shift+y move-left,move-up",
    "shift+u move-right,move-up",
    "shift+b move-left,move-down",
    "shift+n move-right,move-down",
    "ctrl+y cut-left,cut-up",
    "ctrl+u cut-right,cut-up",
    "ctrl+b cut-left,cut-down",
    "ctrl+n cut-right,cut-down",
    NULL,
  };
  for (i = 0; default_config[i]; i++) {
    tmp = strdup(default_config[i]);
    if (parse_config_line(tmp) != 0) {
      fprintf(stderr, "Error with default config line %d: %s\n", i + 1, tmp);
    }
    free(tmp);
  }
}

int ConfigureKeys::parse_config_line(char *orig_line) {
  /* syntax:
   * keysequence cmd1,cmd2,cmd3
   *
   * ex:
   * ctrl+semicolon start
   * space warp
   * semicolon warp,click
   */

  char *line = strdup(orig_line);
  char *tokctx;
  char *keyseq;
  int keycode, mods;
  int i, j;
  char *comment;

  /* Ignore everything after a '#' */
  comment = strchr(line, '#');
  if (comment != NULL)
    *comment = '\0';

  /* Ignore leading whitespace */
  while (isspace(*line))
    line++;

  /* Ignore empty lines */
  if (*line == '\n' || *line == '\0')
    return 0;

  tokctx = line;
  keyseq = strdup(strtok_r(line, " ", &tokctx));

  /* A special config option that will clear all keybindings */
  if (std::strcmp(keyseq, "clear") == 0) {
    /* TODO(sissel): Make this a cmd_clear function */
    /* Reset keybindings */
    keybindings.clear();

    /* ungrab keybindings associated with start */

    for (i = 0; i < ScreenCount(dpy); i++) {
        Window root = RootWindow(dpy, i);
        for (auto& sk : startkeys) {
            XUngrabKey(dpy, sk.keycode, sk.mods, root);
            XUngrabKey(dpy, sk.keycode, sk.mods | LockMask, root);
            XUngrabKey(dpy, sk.keycode, sk.mods | Mod2Mask, root);
            XUngrabKey(dpy, sk.keycode, sk.mods | LockMask | Mod2Mask, root);
        }
    }
      startkeys.clear();
  } else if (std::strcmp(keyseq, "daemonize") == 0) {
    handle_commands(keyseq);
  } else if (strcmp(keyseq, "loadconfig") == 0) {
    handle_commands(keyseq);
  } else {
    keycode = parse_keycode(keyseq);
    if (keycode == 0) {
      fprintf(stderr, "Problem parsing keysequence '%s'\n", keyseq);
      return 1;
    }
    mods = parse_mods(keyseq);

    /* FreeBSD sets 'tokctx' to NULL at end of string.
     * glibc sets 'tokctx' to the next character (the '\0')
     * Reported by Richard Kolkovich */
    if (tokctx == NULL || *tokctx == '\0') {
      fprintf(stderr, "Incomplete configuration line. Missing commands: '%s'\n", line);
      return 1;
    }

    addbinding(keycode, mods, tokctx /* the remainder of the line */);
  }

  free(keyseq);
  free(line);
  return 0;
}

int ConfigureKeys::percent_of(int num, char *args, float default_val) {
  static float precision = 100000.0;
  float pct = 0.0;
  int value = 0;

  /* Parse a float. If this fails, assume the default value */
  if (sscanf(args, "%f", &pct) <= 0)
    pct = default_val;

  /* > 1, then it's not a percent, it's an absolute value. */
  if (pct > 1.0)
    return (int)pct;

  value = (int)((num * (pct * precision)) / precision);
  return value;
}

void ConfigureKeys::updatecliprects(wininfo_t *info) {
  int rects = (info->grid_cols + 1) + (info->grid_rows + 1) /* grid lines */
              + (info->grid_cols * info->grid_rows); /* grid text boxes */

  clip_rectangles.resize(rects);
}

void ConfigureKeys::updategrid(Window win, ConfigureKeys::wininfo_t *info, int apply_clip, int draw) {
  double w = info->w;
  double h = info->h;
  double cell_width;
  double cell_height;
  int i;
  int rect = 0;

  if (apply_clip) {
    updatecliprects(info);
    memset(clip_rectangles.data(), 0, clip_rectangles.size() * sizeof(XRectangle));
  }

#ifdef PROFILE_THINGS
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif

  if (w <= 4 || h <= 4) {
      cairo_new_path(canvas_cairo);
      cairo_fill(canvas_cairo);
      return;
  }

  //printf("updategrid: clip:%d, draw:%d\n", apply_clip, draw);

  if (draw) {
    cairo_new_path(canvas_cairo);
    cairo_set_source_rgb(canvas_cairo, 1, 1, 1);
    cairo_rectangle(canvas_cairo, 0, 0, w, h);
    cairo_set_line_width(canvas_cairo, wininfo.border_thickness);
    cairo_fill(canvas_cairo);
  }

  cell_width = (w / info->grid_cols);
  cell_height = (h / info->grid_rows);

  int x_total_offset = 0;

  /* clip vertically */
  for (i = 0; i <= info->grid_cols; i++) {
    int x_off = 0;
    if (i > 0) {
        x_off = -info->border_thickness / 2;
    }

    if (i == info->grid_cols) {
        x_total_offset = info->w - 1;
    }

    int x_w_off = 0;
    if (i == 0 || i == info->grid_cols) {
        x_w_off = info->border_thickness / 2;
    }

    cairo_move_to(canvas_cairo, x_total_offset + 1, 0);
    cairo_line_to(canvas_cairo, x_total_offset + 1, info->h);

    clip_rectangles[rect].x = x_total_offset + x_off;
    clip_rectangles[rect].y = 0;
    clip_rectangles[rect].width = info->border_thickness - x_w_off;
    clip_rectangles[rect].height = info->h;
    rect++;

    x_total_offset += cell_width;
  }

  int y_total_offset = 0;

  /* clip horizontally */
  for (i = 0; i <= info->grid_rows; i++) {
    int y_off = 0;
    if (i > 0) {
        y_off = -info->border_thickness / 2;
    }

    if (i == info->grid_rows) {
        y_total_offset = info->h - 1;
    }

    int y_w_off = 0;
    if (i == 0 || i == info->grid_rows) {
        y_w_off = info->border_thickness / 2;
    }

    cairo_move_to(canvas_cairo, 0, y_total_offset + 1);
    cairo_line_to(canvas_cairo, info->w, y_total_offset + 1);

    clip_rectangles[rect].x = 0;
    clip_rectangles[rect].y = y_total_offset + y_off;

    clip_rectangles[rect].width = info->w;
    clip_rectangles[rect].height = info->border_thickness - y_w_off;
    rect++;

    y_total_offset += cell_height;
  }

  cairo_path_t *path = cairo_copy_path(canvas_cairo);

#ifdef PROFILE_THINGS
  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("updategrid pathbuild time: %ld.%09ld\n",
         end.tv_sec - start.tv_sec,
         end.tv_nsec - start.tv_nsec);
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif

  if (draw) {
    cairo_set_source_rgba(canvas_cairo, 0, 0, 0, 1.0);
    cairo_set_line_width(canvas_cairo, 1);
    cairo_stroke(canvas_cairo);

#ifdef PROFILE_THINGS
    XSync(dpy, False);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("updategrid draw time: %ld.%09ld\n",
           end.tv_sec - start.tv_sec,
           end.tv_nsec - start.tv_nsec);
    clock_gettime(CLOCK_MONOTONIC, &start);
#endif
  } /* if draw */

  cairo_path_destroy(path);
}

void ConfigureKeys::updategridtext(Window win, wininfo_t *info, int apply_clip, int draw) {
  double w = info->w;
  double h = info->h;
  double cell_width;
  double cell_height;
  double x_off, y_off;
  int row, col;

  int rect = (info->grid_cols + 1 + info->grid_rows + 1); /* start at end of grid lines */

  x_off = info->border_thickness / 2;
  y_off = info->border_thickness / 2;

  cairo_text_extents_t te;
#define FONTSIZE 18
  if (draw) {
    cairo_new_path(canvas_cairo);
    cairo_select_font_face(canvas_cairo, "Courier", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(canvas_cairo, FONTSIZE);
    cairo_text_extents(canvas_cairo, "AA", &te);
  }

  w -= info->border_thickness;
  h -= info->border_thickness;
  cell_width = (w / info->grid_cols);
  cell_height = (h / info->grid_rows);

  h++;
  w++;

  //printf("bearing: %f,%f\n", te.x_bearing, te.y_bearing);
  //printf("size: %f,%f\n", te.width, te.height);

  char label[3] = "AA";

  int row_selected = 0;
  for (col = 0; col < info->grid_cols; col++) {
    label[0] = 'A';
    for (row = 0; row < info->grid_rows; row++) {
      int rectwidth = te.width + 25;
      int rectheight = te.height + 8;
      int xpos = cell_width * col + x_off + (cell_width / 2);
      int ypos = cell_height * row + y_off + (cell_height / 2);

      row_selected = (appstate.grid_nav && appstate.grid_nav_row == row
                      && appstate.grid_nav_state == appstate_t::GRID_NAV_COL);
      //printf("Grid: %c%c\n", label[0], label[1]);

      /* If the current column is the one selected by grid nav, use
       * a different color */
      //printf("Grid geom: %fx%f @ %d,%d\n",
             //xpos - rectwidth / 2 + te.x_bearing / 2,
             //ypos - rectheight / 2 + te.y_bearing / 2,
             //rectwidth, rectheight);
      cairo_rectangle(canvas_cairo,
                      xpos - rectwidth / 2 + te.x_bearing / 2,
                      ypos - rectheight / 2 + te.y_bearing / 2,
                      rectwidth, rectheight);
      if (draw) {
        cairo_path_t *pathcopy;
        pathcopy = cairo_copy_path(canvas_cairo);
        cairo_set_line_width(shape_cairo, 2);

        if (row_selected) {
          cairo_set_source_rgb(canvas_cairo, 0, .3, .3);
        } else {
          cairo_set_source_rgb(canvas_cairo, 0, .2, 0);
        }
        cairo_fill(canvas_cairo);
        cairo_append_path(canvas_cairo, pathcopy);
        cairo_set_source_rgb(canvas_cairo, .8, .8, 0);
        cairo_stroke(canvas_cairo);
        cairo_path_destroy(pathcopy);

        if (row_selected) {
          cairo_set_source_rgb(canvas_cairo, 1, 1, 1);
        } else {
          cairo_set_source_rgb(canvas_cairo, .8, .8, .8);
        }
        cairo_fill(canvas_cairo);
        cairo_move_to(canvas_cairo, xpos - te.width / 2, ypos);
        cairo_show_text(canvas_cairo, label);
      }

      if (apply_clip) {
        clip_rectangles[rect].x = xpos - rectwidth / 2 + te.x_bearing / 2;
        clip_rectangles[rect].y = ypos - rectheight / 2 + te.y_bearing / 2;
        clip_rectangles[rect].width = rectwidth + 1;
        clip_rectangles[rect].height =  rectheight + 1;
        rect++;
      }
      label[0]++;
    }
    label[1]++;
  } /* Draw rectangles and text */
} /* void updategridtext */

void ConfigureKeys::grab_keyboard() {
  int grabstate;
  int grabtries = 0;

  /* This loop is to work around the following scenario:
   * xbindkeys invokes XGrabKeyboard when you press a bound keystroke and
   * doesn't Ungrab until you release a key.
   * Example: (xbindkey '(Control semicolon) "keynav 'start, grid 2x2'")
   * This will only invoke XUngrabKeyboard when you release 'semicolon'
   *
   * The problem is that keynav would be launched as soon as the keydown
   * event 'control + semicolon' occurs, but we could only get the grab on
   * the release.
   *
   * This sleepyloop will keep trying to grab the keyboard until it succeeds.
   *
   * Reported by Colin Shea
   */
  grabstate = XGrabKeyboard(dpy, viewports[wininfo.curviewport].root, False,
                            GrabModeAsync, GrabModeAsync, CurrentTime);
  while (grabstate != GrabSuccess) {
    usleep(10000); /* sleep for 10ms */
    grabstate = XGrabKeyboard(dpy, viewports[wininfo.curviewport].root, False,
                              GrabModeAsync, GrabModeAsync, CurrentTime);
    grabtries += 1;
    if (grabtries >= 20) {
      fprintf(stderr, "XGrabKeyboard failed %d times, giving up...\n",
              grabtries);

      /* Returning from here will result in the appstate.active still
       * being false. */
      return;
    }
  }
  //printf("Got grab!\n");
}

void ConfigureKeys::cmd_resume(char *args) {
  cmd_start_base(args, false);
}

void ConfigureKeys::cmd_start(char *args) {
  cmd_start_base(args, true);
}

void ConfigureKeys::cmd_start_base(char *args, bool ResetCoordinates) {
  XSetWindowAttributes winattr;
  int i;
  int screen;

  screen = query_current_screen();
  wininfo.curviewport = screen;

  appstate.grid_nav_row = -1;
  appstate.grid_nav_col = -1;

  if (ResetCoordinates) {
    wininfo.x = viewports[wininfo.curviewport].x;
    wininfo.y = viewports[wininfo.curviewport].y;
    wininfo.w = viewports[wininfo.curviewport].w;
    wininfo.h = viewports[wininfo.curviewport].h;
  }

  grab_keyboard();

  /* Default start with 4 cells, 2x2 */
  wininfo.grid_rows = 2;
  wininfo.grid_cols = 2;

  wininfo.border_thickness = 3;
  wininfo.center_cut_size = 3;

  if (ISACTIVE)
    return;

  int depth;

  appstate.active = True;
  appstate.need_draw = 1;
  appstate.need_moveresize = 1;

  if (zone == 0) { /* Create our window for the first time */
    auto& viewport = viewports.at(wininfo.curviewport);

    depth = viewport.screen->root_depth;
    wininfo_history_cursor = 0;

    zone = XCreateSimpleWindow(dpy, viewport.root,
                               wininfo.x, wininfo.y, wininfo.w, wininfo.h, 0, 0, 0);
    xdo_set_window_class(xdo, zone, "keynav", "keynav");
    canvas_gc = XCreateGC(dpy, zone, 0, NULL);

    canvas = XCreatePixmap(dpy, zone, viewport.w, viewport.h,
                           viewport.screen->root_depth);
    canvas_surface = cairo_xlib_surface_create(dpy, canvas,
                                               viewport.screen->root_visual,
                                               viewport.w, viewport.h);
    canvas_cairo = cairo_create(canvas_surface);
    cairo_set_antialias(canvas_cairo, CAIRO_ANTIALIAS_NONE);
    cairo_set_line_cap(canvas_cairo, CAIRO_LINE_CAP_SQUARE);

    shape = XCreatePixmap(dpy, zone, viewport.w, viewport.h, 1);
    shape_surface = cairo_xlib_surface_create_for_bitmap(dpy, shape,
                                                         viewport.screen,
                                                         viewport.w,
                                                         viewport.h);
    shape_cairo = cairo_create(shape_surface);
    cairo_set_line_width(shape_cairo, wininfo.border_thickness);
    cairo_set_antialias(canvas_cairo, CAIRO_ANTIALIAS_NONE);
    cairo_set_line_cap(shape_cairo, CAIRO_LINE_CAP_SQUARE);

    /* Tell the window manager not to manage us */
    winattr.override_redirect = 1;
    XChangeWindowAttributes(dpy, zone, CWOverrideRedirect, &winattr);

    XSelectInput(dpy, zone, StructureNotifyMask | ExposureMask
                 | PointerMotionMask | LeaveWindowMask );
  } /* if zone == 0 */
}

void ConfigureKeys::cmd_end(char *args) {
  if (!ISACTIVE)
    return;

  /* kill drag state too */
  if (ISDRAGGING)
    cmd_drag(NULL);

  /* Stop recording if we're in that mode */
  if (appstate.recording != appstate_t::record_off) {
    cmd_record(NULL);
  }

  appstate.active = False;

  //XDestroyWindow(dpy, zone);
  XUnmapWindow(dpy, zone);
  cairo_destroy(shape_cairo);
  cairo_surface_destroy(shape_surface);
  cairo_destroy(canvas_cairo);
  cairo_surface_destroy(canvas_surface);
  XFreePixmap(dpy, shape);
  XFreePixmap(dpy, canvas);
  XFreeGC(dpy, canvas_gc);
  XDestroyWindow(dpy, zone);
  XUngrabKeyboard(dpy, CurrentTime);

  zone = 0;
}

void ConfigureKeys::cmd_toggle_start(char *args) {
  if (ISACTIVE) {
    cmd_end(args);
  } else {
    cmd_start(args);
  }
}

void ConfigureKeys::cmd_history_back(char *args) {
  if (!ISACTIVE)
    return;

  restore_history_point(1);
}

void ConfigureKeys::cmd_loadconfig(char *args) {
  // Trim leading and trailing quotes if they exist
  if (*args == '"') {
    args++;
    *(args + strlen(args) - 1) = '\0';
  }

  parse_config_file(args);
}

void ConfigureKeys::cmd_shell(char *args) {
  // Trim leading and trailing quotes if they exist
  if (*args == '"') {
    args++;
    *(args + strlen(args) - 1) = '\0';
  }

  if (fork() == 0) { /* child */
    int ret;
    char *const shell = "/bin/sh";
    char *const argv[4] = { shell, "-c", args, NULL };
    //printf("Exec: %s\n", args);
    //printf("Shell: %s\n", shell);
    ret = execvp(shell, argv);
    perror("execve");
    exit(1);
  }
}

void ConfigureKeys::cmd_quit(char *args) {
  exit(0);
}

void ConfigureKeys::cmd_restart(char *args) {
  restart();
}

void ConfigureKeys::cmd_cut_up(char *args) {
  if (!ISACTIVE)
    return;
  wininfo.h = percent_of(wininfo.h, args, .5);
}

void ConfigureKeys::cmd_cut_down(char *args) {
  if (!ISACTIVE)
    return;

  int orig = wininfo.h;
  wininfo.h = percent_of(wininfo.h, args, .5);
  wininfo.y += orig - wininfo.h;
}

void ConfigureKeys::cmd_cut_left(char *args) {
  if (!ISACTIVE)
    return;
  wininfo.w = percent_of(wininfo.w, args, .5);
}

void ConfigureKeys::cmd_cut_right(char *args) {
  int orig = wininfo.w;
  if (!ISACTIVE)
    return;
  wininfo.w = percent_of(wininfo.w, args, .5);
  wininfo.x += orig - wininfo.w;
}

void ConfigureKeys::cmd_move_up(char *args) {
  if (!ISACTIVE)
    return;
  wininfo.y -= percent_of(wininfo.h, args, 1);
}

void ConfigureKeys::cmd_move_down(char *args) {
  if (!ISACTIVE)
    return;
  wininfo.y += percent_of(wininfo.h, args, 1);
}

void ConfigureKeys::cmd_move_left(char *args) {
  if (!ISACTIVE)
    return;
  wininfo.x -= percent_of(wininfo.w, args, 1);
}

void ConfigureKeys::cmd_move_right(char *args) {
  if (!ISACTIVE)
    return;
  wininfo.x += percent_of(wininfo.w, args, 1);
}

void ConfigureKeys::cmd_cursorzoom(char *args) {
  int xradius = 0, yradius = 0, width = 0, height = 0;
  int xloc, yloc;
  if (!ISACTIVE)
    return;

  int count = sscanf(args, "%d %d", &width, &height);
  if (count == 0) {
    fprintf(stderr,
            "Invalid usage of 'cursorzoom' (expected at least 1 argument)\n");
  } else if (count == 1) {
    /* If only one argument, assume we want a square. */
    height = width;
  }

  xdo_get_mouse_location(xdo, &xloc, &yloc, NULL);

  wininfo.x = xloc - (width / 2);
  wininfo.y = yloc - (height / 2);
  wininfo.w = width;
  wininfo.h = height;
}

void ConfigureKeys::cmd_windowzoom(char *args) {
  Window curwin;
  Window rootwin;
  Window dummy_win;
  int x, y;
  unsigned int width, height, border_width, depth;

  xdo_get_active_window(xdo, &curwin);
  if (curwin) {
    XGetGeometry(xdo->xdpy, curwin, &rootwin, &x, &y, &width, &height,
                 &border_width, &depth);
    XTranslateCoordinates(xdo->xdpy, curwin, rootwin,
                          -border_width, -border_width, &x, &y, &dummy_win);

    wininfo.x = x;
    wininfo.y = y;
    wininfo.w = width;
    wininfo.h = height;
  }
}

void ConfigureKeys::cmd_warp(char *args) {
  if (!ISACTIVE)
    return;
  int x, y;
  x = wininfo.x + wininfo.w / 2;
  y = wininfo.y + wininfo.h / 2;

  if (mouseinfo.x != -1 && mouseinfo.y != -1) {
    closepixel(dpy, zone, &mouseinfo);
  }

  /* Open pixels hould be relative to the window coordinates,
   * not screen coordinates. */
  mouseinfo.x = x - wininfo.x;
  mouseinfo.y = y - wininfo.y;
  openpixel(dpy, zone, &mouseinfo);

  xdo_move_mouse(xdo, x, y, viewports[wininfo.curviewport].screen_num);
  xdo_wait_for_mouse_move_to(xdo, x, y);

  /* TODO(sissel): do we need to open again? */
  openpixel(dpy, zone, &mouseinfo);
}

void ConfigureKeys::cmd_click(char *args) {
  if (!ISACTIVE)
    return;

  int button;
  button = atoi(args);
  if (button > 0)
    xdo_click_window(xdo, CURRENTWINDOW, button);
  else
    fprintf(stderr, "Negative mouse button is invalid: %d\n", button);
}

void ConfigureKeys::cmd_doubleclick(char *args) {
  if (!ISACTIVE)
    return;
  cmd_click(args);
  cmd_click(args);
}

void ConfigureKeys::cmd_drag(char *args) {
  if (!ISACTIVE)
    return;

  int button;
  if (args == NULL) {
    button = drag_button;
  } else {
    int count = sscanf(args, "%d %127s", &button, drag_modkeys);
    if (count == 0) {
      button = 1; /* Default to left mouse button */
      drag_modkeys[0] = '\0';
    } else if (count == 1) {
      drag_modkeys[0] = '\0';
    }
  }

  if (button <= 0) {
    fprintf(stderr, "Negative or no mouse button given. Not valid. Button read was '%d'\n", button);
    return;
  }

  drag_button = button;

  if (ISDRAGGING) { /* End dragging */
    appstate.dragging = False;
    xdo_mouse_up(xdo, CURRENTWINDOW, button);
  } else { /* Start dragging */
    cmd_warp(NULL);
    appstate.dragging = True;
    xdo_send_keysequence_window_down(xdo, 0, drag_modkeys, 12000);
    xdo_mouse_down(xdo, CURRENTWINDOW, button);

    /* Sometimes we need to move a little to tell the app we're dragging */
    /* TODO(sissel): Make this a 'mousewiggle' command */
    xdo_move_mouse_relative(xdo, 1, 0);
    xdo_move_mouse_relative(xdo, -1, 0);
    XSync(xdo->xdpy, 0);
    xdo_send_keysequence_window_up(xdo, 0, drag_modkeys, 12000);
  }
}

void ConfigureKeys::cmd_grid_nav(char *args) {

  if (!strcmp("on", args)) {
    appstate.grid_label = appstate_t::GRID_LABEL_AA;
  } else if (!strcmp("off", args)) {
    appstate.grid_label = appstate_t::GRID_LABEL_NONE;
  } else if (!strcmp("toggle", args)) {
    if (appstate.grid_label == appstate_t::GRID_LABEL_NONE) {
      appstate.grid_label = appstate_t::GRID_LABEL_AA;
    } else {
      appstate.grid_label = appstate_t::GRID_LABEL_NONE;
    }
  }

  /* Set state grid_nav if grid_label is anything but NONE */
  if (appstate.grid_label == appstate_t::GRID_LABEL_NONE) {
    appstate.grid_nav = 0;
  } else {
    appstate.grid_nav = 1;
    appstate.grid_nav_state = appstate_t::GRID_NAV_ROW;
  }
  appstate.need_draw = 1;
}

void ConfigureKeys::cmd_grid(char *args) {
  int grid_cols, grid_rows;

  // Try to parse 'NxN' where N is a number.
  if (sscanf(args, "%dx%d", &grid_cols, &grid_rows) <= 0) {
    // Otherwise, try parsing a number.
    grid_cols = grid_rows = atoi(args);
  }

  if (grid_cols <= 0 || grid_rows <= 0) {
    fprintf(stderr, "Invalid grid segmentation: %dx%d\n", grid_cols, grid_rows);
    fprintf(stderr, "Grid x and y must both be greater than 0.\n");
    return;
  }

  wininfo.grid_cols = grid_cols;
  wininfo.grid_rows = grid_rows;
}

void ConfigureKeys::cmd_cell_select(char *args) {
  int row, col, z;
  int cell_width, cell_height;
  row = col = z = 0;

  // Try to parse 'NxM' where N and M are a number.
  if (sscanf(args, "%dx%d", &col, &row) < 2) {
    // Otherwise, try parsing just number.
    z = atoi(args);
  }

  // if z > 0, then this means we said "cell-select N"
  if (z > 0) {
    double dx = (double)z / (double)wininfo.grid_rows;
    col = (z / wininfo.grid_rows);
    if ( (double)col != dx ) {
      col++;
    }
    row = (z % wininfo.grid_rows);
    if ( 0 == row ) {
      row = wininfo.grid_rows;
    }
  }

  if (col <= 0 && row <= 0) {
    fprintf(stderr, "Cell number cannot be zero or negative. I was given"
            "columns=%d and rows=%d\n", col, row);
    return;
  }

  if (col > wininfo.grid_cols && row > wininfo.grid_rows) {
    fprintf(stderr, "The active grid is %dx%d, and you selected %dx%d which "
            "does not exist.\n", wininfo.grid_cols, wininfo.grid_rows, col, row);
    return;
  }

  // else, then we said cell-select NxM
  // cell_selection is 0-based, so subtract 1.
  cell_select(col - 1, row - 1);
}

void ConfigureKeys::cell_select(int col, int row) {
  //printf("cell_select: %d, %d\n", col, row);
  wininfo.w = wininfo.w / wininfo.grid_cols;
  wininfo.h = wininfo.h / wininfo.grid_rows;
  wininfo.x = wininfo.x + (wininfo.w * (col));
  wininfo.y = wininfo.y + (wininfo.h * (row));
}

void ConfigureKeys::cmd_daemonize(char *args) {
  if (!is_daemon) {
    daemonize = 1;
  }
}

void ConfigureKeys::cmd_playback(char *args) {
  appstate.playback = 1;
}

void ConfigureKeys::cmd_record(char *args) {
  char *filename;
  if (!ISACTIVE)
    return;

  if (appstate.recording != appstate_t::record_off) {
    appstate.recording = appstate_t::record_off;
    recordings.push_back(active_recording);

    /* Save to file */
    if (!recordings_filename.empty()) {
      recordings_save(recordings_filename);
    }
    } else {
      active_recording.commands.clear(); 
      appstate.recording = appstate_t::record_getkey;
    }
}

void ConfigureKeys::update() {
  if (!ISACTIVE)
    return;

  /* Fix keynav window boundaries if they exceed the screen */
  correct_overflow();
  if (wininfo.w <= 1 || wininfo.h <= 1) {
    cmd_end(NULL);
    return;
  }

  wininfo_t *previous = &(wininfo_history[wininfo_history_cursor - 1]);
  //printf("window: %d,%d @ %d,%d\n", wininfo.w, wininfo.h, wininfo.x, wininfo.y);
  //printf("previous: %d,%d @ %d,%d\n", previous->w, previous->h, previous->x, previous->y);
  int draw = 0, move = 0, resize = 0, clip = 0;
  if (previous->x != wininfo.x || previous->y != wininfo.y) {
    move = 1;
  }

  if (previous->w != wininfo.w || previous->h != wininfo.h) {
    clip = 1;
    draw = 1;
    resize = 1;
  }

  if (appstate.need_draw) {
    clip = 1;
    draw = 1;
    appstate.need_draw = 0;
  }

  if (appstate.need_moveresize) {
    move = 1;
    resize = 1;
    appstate.need_moveresize = 0;
  }

  //printf("move: %d, clip: %d, draw: %d, resize: %d\n", move, clip, draw, resize);

  //clip = 0;
  if (((clip || draw) + (move || resize)) > 1) {
    /* more than one action to perform, unmap to hide move/draws
     * to reduce flickering */
    XUnmapWindow(dpy, zone);
  }

  if (clip || draw) {
    updategrid(zone, &wininfo, clip, draw);

    if (appstate.grid_label != appstate_t::GRID_LABEL_NONE) {
      updategridtext(zone, &wininfo, clip, draw);
    }

    if (draw) {
      XCopyArea(dpy, canvas, zone, canvas_gc, 0, 0, wininfo.w, wininfo.h, 0, 0);
    }
    if (clip) {
      XShapeCombineRectangles(dpy, zone, ShapeBounding, 0, 0,
                              clip_rectangles.data(),
                              clip_rectangles.size(), 
                              ShapeSet, 0);
    }
  }


  if (resize && move) {
    //printf("=> %ld: %dx%d @ %d,%d\n", zone, wininfo.w, wininfo.h, wininfo.x,
           //wininfo.y);
    XMoveResizeWindow(dpy, zone, wininfo.x, wininfo.y, wininfo.w, wininfo.h);

    /* Under Gnome3/GnomeShell, it seems to ignore this move+resize request
     * unless we sync and sleep here. Sigh. Gnome is retarded. */
    XSync(dpy, 0);
    usleep(5000);
  } else if (resize) {
    XResizeWindow(dpy, zone, wininfo.w, wininfo.h);
  } else if (move) {
    XMoveWindow(dpy, zone, wininfo.x, wininfo.y);
  }

  XMapRaised(dpy, zone);
}

void ConfigureKeys::correct_overflow() {
  /* If the window is outside the boundaries of the screen, bump it back
   * or, if possible, move it to the next screen */

  if (wininfo.x < viewports[wininfo.curviewport].x)
    viewport_left();
  if (wininfo.x + wininfo.w >
      viewports[wininfo.curviewport].x + viewports[wininfo.curviewport].w)
    viewport_right();

  /* Fix positioning if we went out of bounds (off the screen) */
  if (wininfo.x < 0) {
    wininfo.x = 0;
  }
  if (wininfo.x + wininfo.w >
      viewports[wininfo.curviewport].x + viewports[wininfo.curviewport].w)
    wininfo.x = viewports[wininfo.curviewport].x + viewports[wininfo.curviewport].w - wininfo.w;

  /* XXX: We don't currently understand how to move around if displays are
   * vertically stacked. */
  if (wininfo.y < 0)
    wininfo.y = 0;
  if (wininfo.y + wininfo.h >
      viewports[wininfo.curviewport].y + viewports[wininfo.curviewport].h)
    wininfo.y = viewports[wininfo.curviewport].h - wininfo.h;
}

void ConfigureKeys::viewport_right() {
  int expand_w = 0, expand_h = 0;

  /* Expand if the current window is the size of the current viewport */
  //printf("right %d] %d,%d vs %d,%d\n", wininfo.curviewport,
         //wininfo.w, wininfo.h,
         //viewports[wininfo.curviewport].w, viewports[wininfo.curviewport].h);
  if (wininfo.curviewport == viewports.size() - 1)
    return;

  if (wininfo.w == viewports[wininfo.curviewport].w)
    expand_w = 1;
  if (wininfo.h == viewports[wininfo.curviewport].h) {
    expand_h = 1;
  }

  wininfo.curviewport++;

  if ((expand_w) || wininfo.w > viewports[wininfo.curviewport].w) {
    wininfo.w = viewports[wininfo.curviewport].w;
  }
  if ((expand_h) || wininfo.h > viewports[wininfo.curviewport].h) {
    wininfo.h = viewports[wininfo.curviewport].h;
  }
  wininfo.x = viewports[wininfo.curviewport].x;
}

void ConfigureKeys::viewport_left() {
  int expand_w = 0, expand_h = 0;

  /* Expand if the current window is the size of the current viewport */
  //printf("left %d] %d,%d vs %d,%d\n", wininfo.curviewport,
         //wininfo.w, wininfo.h,
         //viewports[wininfo.curviewport].w, viewports[wininfo.curviewport].h);
  if (wininfo.curviewport == 0)
    return;

  if (wininfo.w == viewports[wininfo.curviewport].w)
    expand_w = 1;
  if (wininfo.h == viewports[wininfo.curviewport].h) {
    expand_h = 1;
  }

  wininfo.curviewport--;
  if (expand_w || wininfo.w > viewports[wininfo.curviewport].w) {
    wininfo.w = viewports[wininfo.curviewport].w;
  }
  if (expand_h || wininfo.h > viewports[wininfo.curviewport].h) {
    wininfo.h = viewports[wininfo.curviewport].h;
  }
  wininfo.x = viewports[wininfo.curviewport].w - wininfo.w;
}

void ConfigureKeys::handle_keypress(XKeyEvent *e) {
  int i;

  /* Only pay attention to shift. In particular, things not included here are
   * mouse buttons (active when dragging), numlock (including Mod2Mask) */
  e->state &= (ShiftMask | ControlMask | Mod1Mask | Mod3Mask | Mod4Mask | Mod4Mask);

  if (appstate.recording == appstate_t::record_getkey) {
    if (handle_recording(e) == HANDLE_STOP) {
      return;
    }
  }

  if (appstate.playback) {
    /* Loop over known recordings */
    for (auto& rec : recordings) {
      if (e->keycode == rec.keycode) {
        int j = 0;
        for (auto& command : rec.commands) {
          handle_commands(command);
        }
        break;
      }
    }
    appstate.playback = 0;
    return;
  }

  if (appstate.grid_nav) {
    if (handle_gridnav(e) == HANDLE_STOP) {
      return;
    }
  }

  /* Loop over known keybindings */
  for (auto& kbt : keybindings) {
    if ((kbt.keycode == e->keycode) && (kbt.mods == e->state)) {
      handle_commands(kbt.commands);
    }
  }
} /* void handle_keypress */

ConfigureKeys::handler_info_t ConfigureKeys::handle_recording(XKeyEvent *e) {
  int i;
  appstate.recording = appstate.record_ing; /* start recording actions */
  /* TODO(sissel): support keys with keystrokes like shift+a */

  /* check existing recording keycodes if we need to override it */
  for (auto recIter = recordings.begin(); recIter != recordings.end() ; ) {
    if (recIter->keycode == e->keycode) {
      recIter = recordings.erase(recIter);
    }
    else {
      ++recIter;
    }
  }

  //printf("Recording as keycode:%d\n", e->keycode);
  active_recording.keycode = e->keycode;
  return ConfigureKeys::HANDLE_STOP;
}

ConfigureKeys::handler_info_t ConfigureKeys::handle_gridnav(XKeyEvent *e) {
  int index = 0;

  if (e->state & 0x2000) { /* ISO Level3 Shift */
    index += 2;
  }
  if (e->state & ShiftMask) {
    index += 1;
  }

  KeySym sym = XkbKeycodeToKeysym(dpy, e->keycode, 0, index);
  char *key = XKeysymToString(sym);

  if (sym == XK_Escape) {
    cmd_grid_nav("off");
    update();
    return HANDLE_STOP;
  }

  if (!(strlen(key) == 1 && isalpha(*key))) {
    return HANDLE_CONTINUE;
  }
  int val = tolower(*key) - 'a';

  if (val < 0) {
    return HANDLE_CONTINUE;
  }

  /* TODO(sissel): Only GRID_LABEL_AA supported right now */
  if (appstate.grid_nav_state == ConfigureKeys::appstate_t::GRID_NAV_COL) {
    if (val >= wininfo.grid_cols) {
      return HANDLE_CONTINUE; /* Invalid key in this grid, pass */
    }
    appstate.grid_nav_state = ConfigureKeys::appstate_t::GRID_NAV_ROW;
    appstate.grid_nav_col = val;
    appstate.need_draw = 1;
    update();
    save_history_point();

    /* We have a full set of coordinates now; select that grid position */
    cell_select(appstate.grid_nav_col, appstate.grid_nav_row);
    update();
    save_history_point();
    appstate.grid_nav_row = -1;
    appstate.grid_nav_col = -1;
  } else if (appstate.grid_nav_state == ConfigureKeys::appstate_t::GRID_NAV_ROW) {
    if (val >= wininfo.grid_rows) {
      return HANDLE_CONTINUE; /* Invalid key in this grid, pass */
    }
    appstate.grid_nav_row = val;
    appstate.grid_nav_state = ConfigureKeys::appstate_t::GRID_NAV_COL;
    appstate.need_draw = 1;
    update();
    save_history_point();
  }
  return HANDLE_STOP;
}

void ConfigureKeys::handle_commands(const std::string& Commands) {
  char *cmdcopy, *copyptr;
  char *tokctx, *tok, *strptr;
  int is_quoted, is_escaped;
  if (Commands.size() == 0)
    return;

  //printf("Commands; %s\n", commands);
  std::string commandsCopy = Commands;
  // get editable reference to string
  cmdcopy = &commandsCopy[0];
  copyptr = cmdcopy;
  while (*copyptr != '\0') {
    /* Parse with knowledge of quotes and escaping */
    is_quoted = is_escaped = FALSE;
    strptr = tok = copyptr;
    while (*copyptr != '\0' && (is_quoted == TRUE || *copyptr != ',')) {
      if (is_escaped == TRUE) {
        is_escaped = FALSE;
      }

      if (*copyptr == '"' && is_escaped == FALSE) {
        is_quoted = !is_quoted;
      }

      if (*copyptr == '\\' && is_escaped == FALSE) {
        is_escaped = TRUE;
        copyptr++;
      }

      *strptr = *copyptr;

      strptr++;
      copyptr++;
    }

    if (*strptr != '\0') {
      *strptr = '\0';

      copyptr++;
    }

    int i;
    int found = 0;
    strptr = NULL;

    /* Ignore leading whitespace */
    while (isspace(*tok))
      tok++;

    /* Record this command (if the command is not 'record') */
    if (appstate.recording == appstate.record_ing && strncmp(tok, "record", 6)) {
      //printf("Record: %s\n", tok);
      active_recording.commands.push_back(tok);
    }

    for (auto commandSet : dispatch) {
      /* XXX: This approach means we can't have one command be a subset of
       * another. For example, 'grid' and 'grid-foo' will fail because when you
       * use 'grid-foo' it'll match 'grid' first.
       * This hasn't been a problem yet...
       */

      /* If this command starts with a dispatch function, call it */
      size_t cmdlen = commandSet.command.size();
      size_t tokcmdlen = strcspn(tok, " \t");
      if (cmdlen == tokcmdlen && !strncmp(tok, commandSet.command.data(), cmdlen)) {
        /* tok + len + 1 is
         * "command arg1 arg2"
         *          ^^^^^^^^^ <-- this
         */
        char *args = tok + cmdlen;

        if (*args == '\0')
          args = "";
        else
          args++;

        found = 1;
        (this->*(commandSet.func))(args);
        break;
      }
    }

    if (!found)
      fprintf(stderr, "No such command: '%s'\n", tok);
  }

  if (ISACTIVE) {
    if (ISDRAGGING)
      cmd_warp(NULL);
    update();
    save_history_point();
  }

  free(cmdcopy);
}

void ConfigureKeys::save_history_point() {
  /* If the history is full, drop the oldest entry */
  while (wininfo_history_cursor >= WININFO_MAXHIST) {
    int i;
    for (i = 1; i < wininfo_history_cursor; i++) {
      memcpy(&(wininfo_history[i - 1]),
             &(wininfo_history[i]),
             sizeof(wininfo_t));
    }
    wininfo_history_cursor--;
  }

  memcpy(&(wininfo_history[wininfo_history_cursor]),
         &(wininfo),
         sizeof(wininfo));

  wininfo_history_cursor++;
}

void ConfigureKeys::restore_history_point(int moves_ago) {
  wininfo_history_cursor -= moves_ago + 1;
  if (wininfo_history_cursor < 0) {
    wininfo_history_cursor = 0;
  }

  wininfo_t *previous = &(wininfo_history[wininfo_history_cursor]);
  memcpy(&wininfo, previous, sizeof(wininfo));
  appstate.need_draw = 1;
  appstate.need_moveresize = 1;
}


void ConfigureKeys::query_screens() {
  int dummyint;
  if (XineramaQueryExtension(dpy, &dummyint, &dummyint)
      && XineramaIsActive(dpy)) {
    xinerama = True;
    query_screen_xinerama();
  } else { /* No xinerama */
    query_screen_normal();
  }
/* Sort viewports, left to right.
 * This may perform undesirably for vertically-stacked viewports or
 * for other odd configurations */
  std::sort(std::begin(viewports), 
                std::end(viewports), 
                [](const viewport_t& a, const viewport_t& b) {return a.x > b.x; });
}

void ConfigureKeys::query_screen_xinerama() {
  int i;
  XineramaScreenInfo *screeninfo;

  int nviewports = 0;
  screeninfo = XineramaQueryScreens(dpy, &nviewports);
  viewports.clear();
  for (i = 0; i < nviewports; i++) {
    ConfigureKeys::viewport_t viewport;
    viewport.x = screeninfo[i].x_org;
    viewport.y = screeninfo[i].y_org;
    viewport.w = screeninfo[i].width;
    viewport.h = screeninfo[i].height;
    viewport.screen_num = 0;
    viewport.screen = ScreenOfDisplay(dpy, 0);
    viewport.root = DefaultRootWindow(dpy);
    viewports.push_back(viewport);
  }
  XFree(screeninfo);
}

void ConfigureKeys::query_screen_normal() {
  int i;
  Screen *s;
  int nviewports = ScreenCount(dpy);
  viewports.clear();

  for (i = 0; i < nviewports; i++) {
    s = ScreenOfDisplay(dpy, i);
    ConfigureKeys::viewport_t viewport;

    viewport.x = 0;
    viewport.y = 0;
    viewport.w = s->width;
    viewport.h = s->height;
    viewport.root = RootWindowOfScreen(s);
    viewport.screen_num = i;
    viewport.screen = s;
    viewports.push_back(viewport);
  }
}

int ConfigureKeys::query_current_screen() {
  int i;
  if (xinerama) {
    return query_current_screen_xinerama();
  } else {
    return query_current_screen_normal();
  }
}

int ConfigureKeys::query_current_screen_xinerama() {
  int i = 0, dummyint;
  unsigned int dummyuint;
  int x, y;
  Window dummywin;
  assert(viewports.size() > 0);
  Window root = viewports[0].root;
  XQueryPointer(dpy, root, &dummywin, &dummywin,
                &x, &y, &dummyint, &dummyint, &dummyuint);

  /* Figure which display the cursor is on */
  for (auto viewport : viewports) {
    if (pointinrect(x, y, viewport.x, viewport.y,
                    viewport.w, viewport.h)) {
      return i;
    }
  }

  return -1;
}

int ConfigureKeys::query_current_screen_normal() {
  int i = 0, dummyint;
  unsigned int dummyuint;
  int x, y;
  Window dummywin;
  assert(viewports.size() > 0);
  Window root = viewports[0].root;
  /* Query each Screen's root window to ask if the pointer is in it.
   * I don't know of any other better way to ask what Screen is
   * active (where is the cursor) */
  for (auto viewport : viewports) {
    if (!XQueryPointer(dpy, viewport.root, &dummywin, &dummywin,
                      &x, &y, &dummyint, &dummyint, &dummyuint))
      continue;

    if (pointinrect(x, y, viewport.x, viewport.y,
                    viewport.w, viewport.h)) {
      return i;
    }
  }

  return -1;
}

int ConfigureKeys::pointinrect(int px, int py, int rx, int ry, int rw, int rh) {
  return (px >= rx)
          && (px <= rx + rw)
          && (py >= ry)
          && (py <= ry + rh);
}


void ConfigureKeys::recordings_save(const std::string& filename) {
  FILE *output = NULL;
  int i = 0;

  output = fopen(filename.data(), "w");
  if (output == NULL) {
    fprintf(stderr, "Failure opening '%s' for write: %s\n", filename.data(), strerror(errno));
    return; /* Should we exit instead? */
  }

  for (auto& rec : recordings) {

    fprintf(output, "%d ", rec.keycode);
    for (int j = 0; j < rec.commands.size(); j++) {
      fprintf(output, "%s%s",
              rec.commands.data() + j,
              (j + 1 < rec.commands.size() ? ", " : ""));
    }

    fprintf(output, "\n");
  }

  fclose(output);
}

void ConfigureKeys::parse_recordings(const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (fp == NULL)
    return;

  static const int bufsize = 8192;
  char line[bufsize];
  /* fopen succeeded */
  while (fgets(line, bufsize, fp) != NULL) {
    /* Kill the newline */
    *(line + strlen(line) - 1) = '\0';

    int keycode = 0;
    char *command = NULL;
    keycode = atoi(line);
    command = line + strcspn(line, " \t");
    //printf("found recording: %d => %s\n", keycode, command);

    ConfigureKeys::recording_t rec;
    rec.keycode = keycode;
    rec.commands.push_back(command);
    recordings.push_back(rec);
  }
  fclose(fp);
}

void ConfigureKeys::openpixel(Display *dpy, Window zone, mouseinfo_t *mouseinfo) {
  XRectangle rect;
  if (mouseinfo->x == -1 && mouseinfo->y == -1) {
    return;
  }

  rect.x = mouseinfo->x;
  rect.y = mouseinfo->y;
  rect.width = 1;
  rect.height = 1;

  XShapeCombineRectangles(dpy, zone, ShapeBounding, 0, 0, &rect, 1,
                          ShapeSubtract, 0);
} /* void openpixel */

void ConfigureKeys::closepixel(Display *dpy, Window zone, mouseinfo_t *mouseinfo) {
  XRectangle rect;
  if (mouseinfo->x == -1 && mouseinfo->y == -1) {
    return;
  }

  rect.x = mouseinfo->x;
  rect.y = mouseinfo->y;
  rect.width = 1;
  rect.height = 1;

  XShapeCombineRectangles(dpy, zone, ShapeBounding, 0, 0, &rect, 1,
                          ShapeUnion, 0);
} /* void closepixel */



void ConfigureKeys::RunLoop() {
  /* Sync with the X server.
   * This ensure we errors about XGrabKey and other failures
   * before we try to daemonize */
  XSync(dpy, 0);

  /* If xrandr is enabled, ask to receive events for screen configuration
   * changes. */
  int xrandr_event_base = 0;
  int xrandr_error_base = 0;
  int xrandr = XRRQueryExtension (dpy, &xrandr_event_base, &xrandr_error_base);
  if (xrandr) {
    XRRSelectInput(dpy, DefaultRootWindow(dpy), RRScreenChangeNotifyMask);
  }

  if (daemonize) {
    printf("Daemonizing now...\n");
    daemon(0, 0);
    is_daemon = True;
  }

  while (1) {
    XEvent e;
    XNextEvent(dpy, &e);

    switch (e.type) {
      case KeyPress:
        handle_keypress((XKeyEvent *)&e);
        break;

      /* MapNotify means the keynav window is now visible */
      case MapNotify:
        update();
        break;

      // Configure events mean the window was changed (size, property, etc)
      case ConfigureNotify:
        update();
        break;

      case Expose:
        if (zone) {
        XCopyArea(dpy, canvas, zone, canvas_gc, e.xexpose.x, e.xexpose.y,
                  e.xexpose.width, e.xexpose.height,
                  e.xexpose.x, e.xexpose.y);
        }
        break;

      case MotionNotify:
        if (zone) {
        if (mouseinfo.x != -1 && mouseinfo.y != -1) {
          closepixel(dpy, zone, &mouseinfo);
        }
        mouseinfo.x = e.xmotion.x;
        mouseinfo.y = e.xmotion.y;
        openpixel(dpy, zone, &mouseinfo);
        }
        break;

      // Ignorable events.
      case GraphicsExpose:
      case NoExpose:
      case LeaveNotify:   // Mouse left the window
      case KeyRelease:    // key was released
      case DestroyNotify: // window was destroyed
      case UnmapNotify:   // window was unmapped (hidden)
      case MappingNotify: // when keyboard mapping changes
        break;
      default:
        if (e.type == xrandr_event_base + RRScreenChangeNotify) {
          query_screens();
        } else {
          printf("Unexpected X11 event: %d\n", e.type);
        }
        break;
    }
  }

  xdo_free(xdo);
} /* int main */
