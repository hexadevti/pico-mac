// startups.h -- named Mac setups (as pico-386's startups): each is a small key=value file in
// /picomac/startups holding a machine, its two disks, RAM, screen and speed, and its own snapshot
// folder. The home menu lists them; choosing one loads it into g_settings, and while it is the
// active one every settingsSave() writes it back too, so a startup comes back as it was left.
#pragma once

#include <string>
#include <vector>

struct Startup {
  std::string path;            // full path of its file
  std::string file;            // "start1.cfg"
  std::string name;            // what the home menu shows
};

// Every startup, sorted by name. The first call on a card without any makes them from the old
// settings: one per machine that had a disk (their snapshots stay where they were).
std::vector<Startup> startups_list();
// What its drives hold, for the home menu's second line ("Mac II  System755.dsk + disk1.img").
std::string startup_summary(const std::string &path);

// Loads it into g_settings and makes it the active one (settingsSave writes it back).
bool startup_open(const std::string &path);
// No active startup (back on the home list).
void startup_close();
const char *startup_name();                    // the active one's name, "" when none
// The active one's snapshot folder under /picomac ("snapshots/start2"), "" = the machine's own.
const char *startup_snapdir();
void startup_save_active();                    // g_settings -> its file (settingsSave calls it)

// A new startup (the current machine, no disks): its path, "" when the card cannot be written.
std::string startup_create(const std::string &name);
bool startup_rename(const std::string &name);  // the active one
bool startup_delete();                         // the active one's file (its snapshots stay)
