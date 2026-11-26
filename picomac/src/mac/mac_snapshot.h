// mac_snapshot.h -- snapshots of a running Mac, to start from later where it stopped (as
// pico-386's pc_snapshot). See mac_snapshot.cpp.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

struct MacSnapshot { std::string path, name; };

// The machine a snapshot needs: its disks (full paths, "" = none), ROM (relative to the card's
// root, as in the settings), RAM size (RAM_*) and screen mode (SCREEN_*).
struct MacSnapMachine {
  std::string disk, disk2, rom;
  std::string hd2, floppy2, cdrom;   // the Mac II family's other drives
  int ram = 0, screen = 0;
};

// What a core gives: its screen's pixels (0x00RRGGBB) for the picture, and its machine state
// written to / read from the file (read: 0 = done, -2 = another version, nothing loaded; other =
// failed half way).
typedef uint32_t (*MacSnapPixel)(int x, int y);
typedef bool (*MacSnapWrite)(FILE *f);
typedef int (*MacSnapRead)(FILE *f);

// The snapshots of the machine g_settings.machine names (each Mac has its own).
std::vector<MacSnapshot> macsnap_list();                     // newest first
// Saves the running Mac (between frames) as `name`, with its w x h screen as the picture.
// replace: a snapshot this one takes the place of, deleted once the new one is written. null, or
// why not.
const char *macsnap_save(const std::string &name, const std::string &replace, const MacSnapMachine &m,
                         MacSnapPixel pixel, int w, int h, MacSnapWrite state);
bool macsnap_machine(const std::string &snap, MacSnapMachine &m);
// The state into the Mac, once it was started with the snapshot's machine. null, or why not;
// *broken = the Mac was left half loaded and must be started again.
const char *macsnap_restore(const std::string &snap, bool *broken, MacSnapRead state);
// Whether a disk the snapshot had was written since it was taken.
bool macsnap_disks_changed(const std::string &snap);
// A disk's first write since the Mac started or the last snapshot: its generation goes up.
void macsnap_touch(const std::string &diskPath);

// The save screen: "New snapshot" or one to overwrite (replace = its file), then the name.
// false = cancelled.
bool macsnap_pick_save(std::string &replace, std::string &name);
// The list screen: Enter picks one to start from (out = its file), Delete removes one.
bool macsnap_pick(const char *title, std::string &out);

// The snapshot the next mac_loadRom starts from, and taking it (empty when none).
void macsnap_set_pending(const std::string &snap);
std::string macsnap_take_pending();
