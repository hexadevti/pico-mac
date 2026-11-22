// settings.cpp -- see settings.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "settings.h"
#include "../hal/hal.h"

Settings g_settings;

std::string sdPath(const char *rel) {
  std::string p = hal_sd_root();
  if (rel && *rel) {
    if (rel[0] != '/') p += '/';
    p += rel;
  }
  return p;
}

bool isDir(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool mkOne(const std::string &p) {
#if defined(_WIN32)
  return mkdir(p.c_str()) == 0 || isDir(p);
#else
  return mkdir(p.c_str(), 0777) == 0 || isDir(p);
#endif
}

bool makeDirs(const std::string &path) {
  for (size_t i = 1; i < path.size(); i++)
    if (path[i] == '/' && path[i - 1] != ':') mkOne(path.substr(0, i));
  return mkOne(path);
}

static std::string cfgPath() { return sdPath("picomac/picomac.cfg"); }

void settingsLoad() {
  FILE *f = fopen(cfgPath().c_str(), "r");
  if (!f) return;
  char line[300];
  while (fgets(line, sizeof(line), f)) {
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char *v = eq + 1;
    v[strcspn(v, "\r\n")] = 0;
    if (!strcmp(line, "volume"))         g_settings.volume = atoi(v);
    else if (!strcmp(line, "showfps"))   g_settings.showFps = atoi(v) != 0;
    else if (!strcmp(line, "lastdir"))   g_settings.lastDir = v;
    else if (!strcmp(line, "rom"))       g_settings.romPath = v;
    else if (!strcmp(line, "ram"))       g_settings.ram = atoi(v);
    else if (!strcmp(line, "screen"))    g_settings.screen = atoi(v);
    else if (!strcmp(line, "speed"))     g_settings.speed = atoi(v);
    else if (!strcmp(line, "disk"))     g_settings.disk = v;
    else if (!strcmp(line, "disk2"))    g_settings.disk2 = v;
    else if (!strcmp(line, "arrows"))   g_settings.arrows = atoi(v);
  }
  fclose(f);
  if (g_settings.volume < 0 || g_settings.volume > 10) g_settings.volume = 7;
  if (g_settings.ram < 0 || g_settings.ram >= RAM_COUNT) g_settings.ram = RAM_4M;
  if (g_settings.screen < 0 || g_settings.screen >= SCREEN_COUNT) g_settings.screen = SCREEN_FIT;
  if (g_settings.speed < 1 || g_settings.speed > 4) g_settings.speed = 1;
  if (g_settings.arrows < 0 || g_settings.arrows > 2) g_settings.arrows = 1;
  while (!g_settings.romPath.empty() && g_settings.romPath[0] == '/') g_settings.romPath.erase(0, 1);
}

void settingsSave() {
  makeDirs(sdPath("picomac"));
  FILE *f = fopen(cfgPath().c_str(), "w");
  if (!f) { hal_log("settings: cannot write %s", cfgPath().c_str()); return; }
  fprintf(f, "volume=%d\nshowfps=%d\nlastdir=%s\nrom=%s\nram=%d\nscreen=%d\nspeed=%d\ndisk=%s\ndisk2=%s\narrows=%d\n",
          g_settings.volume, g_settings.showFps ? 1 : 0, g_settings.lastDir.c_str(),
          g_settings.romPath.c_str(), g_settings.ram, g_settings.screen, g_settings.speed,
          g_settings.disk.c_str(), g_settings.disk2.c_str(), g_settings.arrows);
  fclose(f);
}
