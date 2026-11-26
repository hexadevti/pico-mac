// settings.h -- user settings, kept as "key=value" lines in <sd>/picomac/picomac.cfg.
#pragma once

#include <stdint.h>
#include <string>

// Screen modes for the classic 512x342 Mac: scaled to fit 320x214 (grey), or 1:1 with the
// 320x320 window following the pointer. The "native" mode makes the Mac's own screen 320x320.
enum { SCREEN_FIT, SCREEN_PAN, SCREEN_NATIVE, SCREEN_COUNT };
// RAM sizes in KB
enum { RAM_512K, RAM_1M, RAM_2M, RAM_4M, RAM_COUNT };
// The machine the home menu starts (the IIci and the Quadra: not on the P4), and the Mac II's RAM
// sizes (the IIci's four are 4, 8, 16 and 32 MB, the Quadra 700's 4, 8, 20 and 36).
#if defined(ESP_PLATFORM)
enum { MACHINE_PLUS, MACHINE_II, MACHINE_COUNT, MACHINE_IICI = -1, MACHINE_QUADRA = -2 };
#else
enum { MACHINE_PLUS, MACHINE_II, MACHINE_IICI, MACHINE_QUADRA, MACHINE_COUNT };
#endif
enum { RAM2_1M, RAM2_2M, RAM2_4M, RAM2_8M, RAM2_COUNT };

struct Settings {
  int volume = 7;            // 0..10
  bool showFps = false;
  std::string lastDir;       // where the disk browser was last
  std::string romPath = "picomac/macplus.rom";   // relative to the card root
  int ram = RAM_4M;
  int screen = SCREEN_FIT;
  int speed = 1;             // CPU speed multiplier: 1 = a real Mac Plus, up to 4
  std::string disk;          // the boot disk image (full path), "" = none yet
  std::string disk2;         // a second disk (drive 2; the Mac II family's Floppy 1), "" = none
  std::string hd2, floppy2;  // the Mac II family's Hard disk 2 and Floppy 2
  std::string cdrom;         // and its CD-ROM (read-only)
  int arrows = 1;            // what the arrows do: 0 Mac arrow keys, 1 the pointer, 2 both
  int machine = MACHINE_PLUS;
  std::string romPathII = "picomac/macii.rom";   // the Mac II's ROM, relative to the card root
  int ramII = RAM2_4M;
  std::string romPathIIci = "picomac/maciici.rom";   // the Mac IIci's ROM
  int ramIIci = 1;           // 8 MB
  std::string romPathQuadra = "picomac/macquadra.rom";   // the Quadra 700's ROM
  int ramQuadra = 1;         // 8 MB
  std::string startup;       // the startup last chosen (its file in /picomac/startups)
};

extern Settings g_settings;

void settingsLoad();
void settingsSave();
// disk, disk2, hd2, floppy2, cdrom, screen and speed are per machine: g_settings holds the current machine's, and
// this keeps them aside and brings in machine m's (then saves).
void settingsSetMachine(int m);

// <sd root>/<rel>, and a mkdir -p for it (the path's own last component included).
std::string sdPath(const char *rel);
bool makeDirs(const std::string &path);
bool isDir(const std::string &path);
