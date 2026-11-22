// mac_snapshot.h -- snapshots of a running Mac, to start from later where it stopped (as
// pico-386's pc_snapshot). See mac_snapshot.cpp.
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

struct MacSnapshot { std::string path, name; };

// The machine a snapshot needs: its disks (full paths, "" = none), ROM (relative to the card's
// root, as in the settings), RAM size (RAM_*) and screen mode (SCREEN_*).
struct MacSnapMachine {
  std::string disk, disk2, rom;
  int ram = 0, screen = 0;
};

std::vector<MacSnapshot> macsnap_list();                     // newest first
// Saves the running Mac (between frames) as `name`, with fb (w x h, 1 bit a pixel, set = black)
// as its picture. replace: a snapshot this one takes the place of, deleted once the new one is
// written. null, or why not.
const char *macsnap_save(const std::string &name, const std::string &replace, const MacSnapMachine &m,
                         const uint8_t *fb, int w, int h);
bool macsnap_machine(const std::string &snap, MacSnapMachine &m);
// The state into the Mac, once it was started with the snapshot's machine. null, or why not;
// *broken = the Mac was left half loaded and must be started again.
const char *macsnap_restore(const std::string &snap, bool *broken);
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
