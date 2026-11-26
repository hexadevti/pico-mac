// settings.cpp -- see settings.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "settings.h"
#include "startups.h"
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

// Each machine's own disk, disk2, screen and speed; g_settings has the current machine's.
struct PerMachine { std::string disk, disk2; int screen = SCREEN_FIT, speed = 1; };
static PerMachine s_per[MACHINE_COUNT];
#if defined(ESP_PLATFORM)
static const char *const kSuffix[MACHINE_COUNT] = { "", "_ii" };
#else
static const char *const kSuffix[MACHINE_COUNT] = { "", "_ii", "_iici" };
#endif

static void keepCurrent() {
  PerMachine &p = s_per[g_settings.machine];
  p.disk = g_settings.disk; p.disk2 = g_settings.disk2; p.screen = g_settings.screen; p.speed = g_settings.speed;
}

static void useMachine(int m) {
  const PerMachine &p = s_per[m];
  g_settings.machine = m;
  g_settings.disk = p.disk; g_settings.disk2 = p.disk2; g_settings.screen = p.screen; g_settings.speed = p.speed;
}

void settingsSetMachine(int m) {
  if (m < 0 || m >= MACHINE_COUNT || m == g_settings.machine) return;
  keepCurrent();
  useMachine(m);
  settingsSave();
}

// "disk_ii" -> machine 1, base "disk" ("disk_iici" -> the IIci); anything else -> machine 0, unchanged.
static int machineKey(char *key) {
  const size_t n = strlen(key);
#if !defined(ESP_PLATFORM)
  if (n > 5 && !strcmp(key + n - 5, "_iici")) { key[n - 5] = 0; return MACHINE_IICI; }
#endif
  if (n > 3 && !strcmp(key + n - 3, "_ii")) { key[n - 3] = 0; return MACHINE_II; }
  return MACHINE_PLUS;
}

static std::string cfgPath() { return sdPath("picomac/picomac.cfg"); }

void settingsLoad() {
  FILE *f = fopen(cfgPath().c_str(), "r");
  if (!f) return;
  char line[300];
  bool sawII = false;   // a cfg from before the per-machine keys: its disk etc. are both machines'
  while (fgets(line, sizeof(line), f)) {
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char *v = eq + 1;
    v[strcspn(v, "\r\n")] = 0;
    {
      char key[40];
      snprintf(key, sizeof key, "%s", line);
      const int m = machineKey(key);
      if (m == MACHINE_II) sawII = true;
      PerMachine &p = s_per[m];
      if (!strcmp(key, "disk"))   { p.disk = v; continue; }
      if (!strcmp(key, "disk2"))  { p.disk2 = v; continue; }
      if (!strcmp(key, "screen")) { p.screen = atoi(v); continue; }
      if (!strcmp(key, "speed"))  { p.speed = atoi(v); continue; }
    }
    if (!strcmp(line, "volume"))        g_settings.volume = atoi(v);
    else if (!strcmp(line, "showfps"))   g_settings.showFps = atoi(v) != 0;
    else if (!strcmp(line, "lastdir"))   g_settings.lastDir = v;
    else if (!strcmp(line, "rom"))       g_settings.romPath = v;
    else if (!strcmp(line, "ram"))       g_settings.ram = atoi(v);
    else if (!strcmp(line, "arrows"))   g_settings.arrows = atoi(v);
    else if (!strcmp(line, "machine"))  g_settings.machine = atoi(v);
    else if (!strcmp(line, "rom2"))     g_settings.romPathII = v;
    else if (!strcmp(line, "ram2"))     g_settings.ramII = atoi(v);
    else if (!strcmp(line, "rom3"))     g_settings.romPathIIci = v;
    else if (!strcmp(line, "ram3"))     g_settings.ramIIci = atoi(v);
    else if (!strcmp(line, "startup"))  g_settings.startup = v;
  }
  fclose(f);
  if (!sawII) s_per[MACHINE_II] = s_per[MACHINE_PLUS];
  if (g_settings.volume < 0 || g_settings.volume > 10) g_settings.volume = 7;
  if (g_settings.ram < 0 || g_settings.ram >= RAM_COUNT) g_settings.ram = RAM_4M;
  for (PerMachine &p : s_per) {
    if (p.screen < 0 || p.screen >= SCREEN_COUNT) p.screen = SCREEN_FIT;
    if (p.speed < 1 || p.speed > 5) p.speed = 1;   // 5 = Max (Mac II/IIci)
  }
  if (g_settings.arrows < 0 || g_settings.arrows > 2) g_settings.arrows = 1;
  if (g_settings.machine < 0 || g_settings.machine >= MACHINE_COUNT) g_settings.machine = MACHINE_PLUS;
  if (g_settings.ramII < 0 || g_settings.ramII >= RAM2_COUNT) g_settings.ramII = RAM2_4M;
  if (g_settings.ramIIci < 0 || g_settings.ramIIci >= RAM2_COUNT) g_settings.ramIIci = 1;
  useMachine(g_settings.machine);
  while (!g_settings.romPath.empty() && g_settings.romPath[0] == '/') g_settings.romPath.erase(0, 1);
  while (!g_settings.romPathII.empty() && g_settings.romPathII[0] == '/') g_settings.romPathII.erase(0, 1);
  while (!g_settings.romPathIIci.empty() && g_settings.romPathIIci[0] == '/') g_settings.romPathIIci.erase(0, 1);
}

void settingsSave() {
  makeDirs(sdPath("picomac"));
  FILE *f = fopen(cfgPath().c_str(), "w");
  if (!f) { hal_log("settings: cannot write %s", cfgPath().c_str()); return; }
  keepCurrent();
  fprintf(f, "volume=%d\nshowfps=%d\nlastdir=%s\nrom=%s\nram=%d\narrows=%d\nmachine=%d\nrom2=%s\nram2=%d\n",
          g_settings.volume, g_settings.showFps ? 1 : 0, g_settings.lastDir.c_str(),
          g_settings.romPath.c_str(), g_settings.ram, g_settings.arrows,
          g_settings.machine, g_settings.romPathII.c_str(), g_settings.ramII);
  fprintf(f, "rom3=%s\nram3=%d\n", g_settings.romPathIIci.c_str(), g_settings.ramIIci);
  for (int m = 0; m < MACHINE_COUNT; m++)
    fprintf(f, "disk%s=%s\ndisk2%s=%s\nscreen%s=%d\nspeed%s=%d\n", kSuffix[m], s_per[m].disk.c_str(),
            kSuffix[m], s_per[m].disk2.c_str(), kSuffix[m], s_per[m].screen, kSuffix[m], s_per[m].speed);
  fclose(f);
  startup_save_active();                         // the startup running, if any, keeps its changes
}
