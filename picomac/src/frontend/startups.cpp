// startups.cpp -- see startups.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include "startups.h"
#include "settings.h"
#include "../hal/hal.h"

namespace {

#if defined(ESP_PLATFORM)
const char *const kMachineNames[MACHINE_COUNT] = { "Mac Plus", "Mac II" };
#else
const char *const kMachineNames[MACHINE_COUNT] = { "Mac Plus", "Mac II", "Mac IIci", "Quadra 700" };
#endif

struct Data {
  std::string name, disk, disk2, hd2, floppy2, cdrom, snapdir;
  int machine = MACHINE_PLUS, ram = -1, screen = SCREEN_FIT, speed = 1, speedVer = 0;
};

Startup s_active;              // path "" = none
Data s_data;                   // the active one's name and snapshot folder

std::string dirPath() { return sdPath("picomac/startups"); }

bool exists(const std::string &p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0;
}

std::string fileOf(const std::string &path) {
  const size_t s = path.find_last_of('/');
  return s == std::string::npos ? path : path.substr(s + 1);
}

bool readData(const std::string &path, Data &d) {
  FILE *f = fopen(path.c_str(), "r");
  if (!f) return false;
  char line[400];
  while (fgets(line, sizeof line, f)) {
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char *v = eq + 1;
    v[strcspn(v, "\r\n")] = 0;
    if (!strcmp(line, "name")) d.name = v;
    else if (!strcmp(line, "machine")) d.machine = atoi(v);
    else if (!strcmp(line, "disk")) d.disk = v;
    else if (!strcmp(line, "disk2")) d.disk2 = v;
    else if (!strcmp(line, "hd2")) d.hd2 = v;
    else if (!strcmp(line, "floppy2")) d.floppy2 = v;
    else if (!strcmp(line, "cdrom")) d.cdrom = v;
    else if (!strcmp(line, "ram")) d.ram = atoi(v);
    else if (!strcmp(line, "screen")) d.screen = atoi(v);
    else if (!strcmp(line, "speed")) d.speed = atoi(v);
    else if (!strcmp(line, "speedver")) d.speedVer = atoi(v);
    else if (!strcmp(line, "snapdir")) d.snapdir = v;
  }
  fclose(f);
  if (d.machine < 0 || d.machine >= MACHINE_COUNT) d.machine = MACHINE_PLUS;   // a IIci startup on the P4
  if (d.screen < 0 || d.screen >= SCREEN_COUNT) d.screen = SCREEN_FIT;
  if (d.speed < 1 || d.speed > 5) d.speed = 1;      // 5 = Max (Mac II/IIci)
#if !defined(ESP_PLATFORM)
  // Written before Max existed (no speedver): a Mac II/IIci at 1x goes to Max.
  if (d.speedVer < 2 && d.machine != MACHINE_PLUS && d.speed == 1) d.speed = 5;
#endif
  if (d.name.empty()) d.name = fileOf(path);
  return true;
}

bool writeData(const std::string &path, const Data &d) {
  makeDirs(dirPath());
  FILE *f = fopen(path.c_str(), "w");
  if (!f) { hal_log("startups: cannot write %s", path.c_str()); return false; }
  fprintf(f, "name=%s\nmachine=%d\ndisk=%s\ndisk2=%s\nhd2=%s\nfloppy2=%s\ncdrom=%s\nram=%d\nscreen=%d\nspeed=%d\nspeedver=2\nsnapdir=%s\n",
          d.name.c_str(), d.machine, d.disk.c_str(), d.disk2.c_str(), d.hd2.c_str(), d.floppy2.c_str(), d.cdrom.c_str(),
          d.ram, d.screen, d.speed, d.snapdir.c_str());
  fclose(f);
  return true;
}

int &ramOf(int machine) {
  return machine == MACHINE_II ? g_settings.ramII : machine == MACHINE_IICI ? g_settings.ramIIci
       : machine == MACHINE_QUADRA ? g_settings.ramQuadra : g_settings.ram;
}

// g_settings -> d (the machine's part; name and snapdir stay).
void capture(Data &d) {
  d.machine = g_settings.machine;
  d.disk = g_settings.disk;
  d.disk2 = g_settings.disk2;
  d.hd2 = g_settings.hd2;
  d.floppy2 = g_settings.floppy2;
  d.cdrom = g_settings.cdrom;
  d.ram = ramOf(d.machine);
  d.screen = g_settings.screen;
  d.speed = g_settings.speed;
}

// The first free startN.cfg.
std::string freePath(int *num) {
  for (int i = 1; i < 10000; i++) {
    const std::string p = dirPath() + "/start" + std::to_string(i) + ".cfg";
    if (!exists(p)) { if (num) *num = i; return p; }
  }
  return "";
}

// A card without startups: one per machine that had a disk in the old settings (or the current
// machine), with no snapshot folder of their own, so they keep the machine's snapshots.
void migrate() {
  const int orig = g_settings.machine;
  for (int m = 0; m < MACHINE_COUNT; m++) {
    settingsSetMachine(m);
    if (m != orig && g_settings.disk.empty()) continue;
    Data d;
    capture(d);
#if !defined(ESP_PLATFORM)
    if (m != MACHINE_PLUS && d.speed == 1) d.speed = 5;   // Max
#endif
    d.name = kMachineNames[m];
    const std::string p = freePath(nullptr);
    if (p.empty() || !writeData(p, d)) break;
    if (m == orig) g_settings.startup = fileOf(p);
  }
  settingsSetMachine(orig);
}

}  // namespace

std::vector<Startup> startups_list() {
  std::vector<Startup> v;
  for (int pass = 0; pass < 2 && v.empty(); pass++) {
    if (pass) migrate();
    if (DIR *dir = opendir(dirPath().c_str())) {
      while (dirent *e = readdir(dir)) {
        const std::string f = e->d_name;
        if (f.size() < 5 || strcasecmp(f.c_str() + f.size() - 4, ".cfg")) continue;
        Data d;
        const std::string p = dirPath() + "/" + f;
        if (readData(p, d)) v.push_back({ p, f, d.name });
      }
      closedir(dir);
    }
  }
  std::sort(v.begin(), v.end(), [](const Startup &a, const Startup &b) {
    const int c = strcasecmp(a.name.c_str(), b.name.c_str());
    return c ? c < 0 : a.file < b.file;
  });
  return v;
}

std::string startup_summary(const std::string &path) {
  Data d;
  if (!readData(path, d)) return "";
  std::string s = kMachineNames[d.machine];
  s += "  ";
  s += d.disk.empty() ? std::string("No disk") : fileOf(d.disk);
  for (const std::string *x : { &d.hd2, &d.disk2, &d.floppy2, &d.cdrom })
    if (!x->empty() && (d.machine != MACHINE_PLUS || x == &d.disk2)) s += " + " + fileOf(*x);
  return s;
}

bool startup_open(const std::string &path) {
  Data d;
  if (!readData(path, d)) return false;
  // The machine's own fields first (settingsSetMachine brings in its old disks and saves, so no
  // startup may be active yet), then the startup's.
  startup_close();
  settingsSetMachine(d.machine);
  s_active = { path, fileOf(path), d.name };
  s_data = d;
  g_settings.disk = d.disk;
  g_settings.disk2 = d.disk2;
  g_settings.hd2 = d.hd2;
  g_settings.floppy2 = d.floppy2;
  g_settings.cdrom = d.cdrom;
  g_settings.screen = d.screen;
  g_settings.speed = d.speed;
  if (d.ram >= 0) {
    const int n = d.machine == MACHINE_PLUS ? (int)RAM_COUNT : (int)RAM2_COUNT;
    if (d.ram < n) ramOf(d.machine) = d.ram;
  }
  g_settings.startup = s_active.file;
  settingsSave();
  return true;
}

void startup_close() { s_active = Startup(); }

const char *startup_name() { return s_active.path.empty() ? "" : s_data.name.c_str(); }

const char *startup_snapdir() { return s_active.path.empty() ? "" : s_data.snapdir.c_str(); }

void startup_save_active() {
  if (s_active.path.empty()) return;
  capture(s_data);
  writeData(s_active.path, s_data);
}

std::string startup_create(const std::string &name) {
  int num = 0;
  const std::string p = freePath(&num);
  if (p.empty()) return "";
  Data d;
  d.machine = g_settings.machine;
  d.ram = ramOf(d.machine);
  d.name = name;
#if !defined(ESP_PLATFORM)
  if (d.machine != MACHINE_PLUS) d.speed = 5;      // Max
#endif
  d.snapdir = "snapshots/start" + std::to_string(num);
  return writeData(p, d) ? p : "";
}

bool startup_rename(const std::string &name) {
  if (s_active.path.empty()) return false;
  s_data.name = s_active.name = name;
  capture(s_data);
  return writeData(s_active.path, s_data);
}

bool startup_delete() {
  if (s_active.path.empty()) return false;
  const bool ok = remove(s_active.path.c_str()) == 0;
  if (ok) {
    if (g_settings.startup == s_active.file) g_settings.startup.clear();
    startup_close();
  }
  return ok;
}
