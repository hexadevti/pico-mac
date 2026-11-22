// settings.h -- user settings, kept as "key=value" lines in <sd>/picomac/picomac.cfg.
#pragma once

#include <stdint.h>
#include <string>

// Screen modes for the classic 512x342 Mac: scaled to fit 320x214 (grey), or 1:1 with the
// 320x320 window following the pointer. The "native" mode makes the Mac's own screen 320x320.
enum { SCREEN_FIT, SCREEN_PAN, SCREEN_NATIVE, SCREEN_COUNT };
// RAM sizes in KB
enum { RAM_512K, RAM_1M, RAM_2M, RAM_4M, RAM_COUNT };

struct Settings {
  int volume = 7;            // 0..10
  bool showFps = false;
  std::string lastDir;       // where the disk browser was last
  std::string romPath = "picomac/macplus.rom";   // relative to the card root
  int ram = RAM_4M;
  int screen = SCREEN_FIT;
  int speed = 1;             // CPU speed multiplier: 1 = a real Mac Plus, up to 4
  std::string disk;          // the boot disk image (full path), "" = none yet
  std::string disk2;         // a second disk (drive 2), "" = none
  int arrows = 1;            // what the arrows do: 0 Mac arrow keys, 1 the pointer, 2 both
};

extern Settings g_settings;

void settingsLoad();
void settingsSave();

// <sd root>/<rel>, and a mkdir -p for it (the path's own last component included).
std::string sdPath(const char *rel);
bool makeDirs(const std::string &path);
bool isDir(const std::string &path);
