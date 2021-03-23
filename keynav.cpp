/*
 * keynav - Keyboard navigation tool.
 *
 * XXX: Merge 'wininfo' and 'wininfo_history'. The latest history entry is the
 *      same as wininfo, so use that instead.
 */

// cc keynav.o -o keynav -I/usr/include/cairo -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -I/usr/include/pixman-1 -I/usr/include/freetype2 -I/usr/include/libpng12  -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include   -O2 -lcairo -lX11 -lXext -lXinerama -lglib-2.0 -lX11 -lXrandr -Xlinker -rpath=/usr/local/lib -lxdo
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

#ifdef PROFILE_THINGS
#include <time.h>
#endif

#include <xdo.h>
#include "keynav_version.h"

#ifndef GLOBAL_CONFIG_FILE
#define GLOBAL_CONFIG_FILE "/etc/keynavrc"
#endif /* GLOBAL_CONFIG_FILE */

#include "ConfigureKeys.h"


extern char **environ;
char **g_argv;

void sigchld(int sig) {
  int status;
  while (waitpid(-1, &status, WNOHANG) > 0) {
    /* Do nothing, but we needed to waitpid() to collect dead children */
  }
}

void restart() {
  execvp(g_argv[0], g_argv);
}

void sighup(int sig) {
  restart();
}


int main(int argc, char **argv) {
  g_argv = argv;
  char *pcDisplay;
  int ret;
  const char *prog = argv[0];

  if (argc > 1 && (!strcmp(argv[1], "version")
                   || !strcmp(argv[1], "-v")
                   || !strcmp(argv[1], "--version"))) {
    printf("keynav %s\n", KEYNAV_VERSION);
    return EXIT_SUCCESS;  
  }
}
/*
  ConfigureKeys configureKeys(argv);
  
  signal(SIGCHLD, sigchld);
  signal(SIGHUP, sighup);
  signal(SIGUSR1, sighup);

  auto error = configureKeys.Initialize(argc, argv);
  if (error == EXIT_FAILURE)
    return EXIT_FAILURE;


  configureKeys.RunLoop();
}

*/