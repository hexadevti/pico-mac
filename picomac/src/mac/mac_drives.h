// mac_drives.h -- the in-game Drives submenu, shared by the Plus and the Mac II cores (as pico-386's
// Disks): one row per drive with the image it holds; Enter on one offers "Choose a disk image" and
// "Eject". The cores apply the change (a restart, or a disk into the empty drive 2 at once).
#pragma once

#include <string>
#include "../frontend/ui.h"
#include "../frontend/browser.h"
#include "../hal/hal.h"
#include "../../core.h"

namespace macdrives {

struct Drive {
  const char *name;            // "Hard disk"
  std::string path;            // the image it holds, "" = empty
  const char *note;            // what a change does ("Restarts the Mac")
  uint8_t icon;
};

inline std::string fileName(const std::string &path) {
  if (path.empty()) return "Empty";
  const size_t sl = path.find_last_of('/');
  return path.substr(sl == std::string::npos ? 0 : sl + 1);
}

// One drive's choices: 0 = back, 1 = `out` holds the image chosen, 2 = eject.
inline int choose(const EmuCore &core, const Drive &d, std::string &out) {
  const bool loaded = !d.path.empty();
  ui::Item items[2];
  int n = 0;
  items[n++] = { "Choose a disk image", nullptr, ui::ICON_FOLDER, d.note };
  if (loaded) items[n++] = { "Eject", nullptr, ui::ICON_EXIT, d.note };
  const std::string sub = "Now: " + fileName(d.path);
  ui::Screen scr;
  scr.title = d.name;
  scr.subtitle = sub.c_str();
  scr.hints = "Enter|Select;Esc|Back";
  int sel = 0;
  for (;;) {
    const uint8_t k = ui::list(scr, items, n, &sel);
    if (k == KEY_ESC || k == KEY_BACKSPACE) return 0;
    if (k != KEY_ENTER) continue;
    if (sel == 1) { out.clear(); return 2; }
    BrowseOpts o;
    const std::string title = std::string("Disk for ") + d.name;
    o.title = title.c_str();
    if (browseRom(core, out, &o)) return 1;
    ui::setBackdrop(hal_video_front());
  }
}

// The submenu. apply(i, path) puts `path` ("" = eject) into drive i: true when done (the in-game
// menu then closes), false to stay here (it has said why). Esc goes back to the in-game menu.
template <class Apply>
bool menu(const EmuCore &core, Drive *drives, int n, Apply apply) {
  int sel = 0;
  for (;;) {
    ui::Item items[4];
    std::string label[4];
    for (int i = 0; i < n && i < 4; i++) {
      label[i] = fileName(drives[i].path);
      items[i] = { drives[i].name, label[i].c_str(), drives[i].icon, drives[i].note };
    }
    ui::Screen scr;
    scr.title = "Drives";
    scr.hints = "Enter|Change;Esc|Back";
    const uint8_t k = ui::list(scr, items, n, &sel);
    if (k == KEY_ESC || k == KEY_BACKSPACE) return false;
    if (k != KEY_ENTER) continue;
    std::string path;
    const int r = choose(core, drives[sel], path);
    ui::setBackdrop(hal_video_front());          // the browser drew over it
    if (!r) continue;
    if (apply(sel, r == 1 ? path : std::string())) return true;
    ui::setBackdrop(hal_video_front());
  }
}

}  // namespace macdrives
