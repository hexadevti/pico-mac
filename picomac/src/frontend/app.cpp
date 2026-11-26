// app.cpp -- the frontend (from emu16 / pico-amiga / pico-386): the home menu (the startups:
// named Macs with their disks, startups.h), then the machine loop with the in-game menu and the FPS meter.
//
// Keys while the Mac runs: every key goes to the Mac (mac_core.cpp maps them), except the
// frontend's Ctrl hotkeys:
//                 Ctrl-Esc = menu.
// Anywhere:       Ctrl-Shift-Up (arrives as PgUp) = esp32loader, Ctrl-Shift-F3 (arrives as F8) = restart
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include "app.h"
#include "ui.h"
#include "browser.h"
#include "settings.h"
#include "../hal/hal.h"
#include "../../core.h"
#include "../mac/mac_snapshot.h"
#include "../mac/mac_drives.h"
#include "startups.h"

extern const EmuCore kMacPlus;
extern const EmuCore kMacII;
#if !defined(ESP_PLATFORM)
extern const EmuCore kMacIIci;   // the 68030 Mac: desktop and web only
extern const EmuCore kMacQuadra; // the 68040 Mac: desktop and web only
const EmuCore *const kCores[] = { &kMacPlus, &kMacII, &kMacIIci, &kMacQuadra };
#else
const EmuCore *const kCores[] = { &kMacPlus, &kMacII };
#endif
const int kNumCores = sizeof(kCores) / sizeof(kCores[0]);

static const char *const kRamNames[RAM_COUNT] = { "512 KB", "1 MB", "2 MB", "4 MB" };
static const char *const kScreenNames[SCREEN_COUNT] = { "Fit (grey)", "1:1 pan", "Native 320x320" };

static std::string fileName(const std::string &path) {
  const size_t s = path.find_last_of('/');
  return s == std::string::npos ? path : path.substr(s + 1);
}

// ------------------------------------------------------------------------------------------------
// settings rows shared by the home and in-game menus
// ------------------------------------------------------------------------------------------------
enum SettingRow { ROW_VOLUME, ROW_FPS };

static void adjustSetting(int row, uint8_t key) {
  const int d = key == KEY_LEFT ? -1 : 1;
  switch (row) {
    case ROW_VOLUME:
      g_settings.volume += d;
      if (g_settings.volume < 0) g_settings.volume = 0;
      if (g_settings.volume > 10) g_settings.volume = 10;
      hal_audio_volume(g_settings.volume);
      break;
    case ROW_FPS: g_settings.showFps = !g_settings.showFps; break;
  }
}

static int settingItems(ui::Item *out, int *rowOf) {
  int n = 0;
  rowOf[n] = ROW_VOLUME;
  out[n++] = { "Volume", nullptr, ui::ICON_VOLUME, nullptr, (int8_t)g_settings.volume, true };
  rowOf[n] = ROW_FPS;
  out[n++] = { "Show FPS", g_settings.showFps ? "On" : "Off", ui::ICON_CHART, nullptr, -1, true };
  return n;
}

// ------------------------------------------------------------------------------------------------
// machine
// ------------------------------------------------------------------------------------------------
enum class MenuResult { Resume, Quit };

// The in-game menu: Resume, the core's rows, Reset, settings, Quit.
static MenuResult gameMenu(const EmuCore &core) {
  hal_audio_silence();
  hal_video_wait();
  ui::setBackdrop(hal_video_front());
  ui::setStrip(false);
  const std::string title = *startup_name() ? std::string(startup_name())
                          : g_settings.disk.empty() ? std::string("No disk") : fileName(g_settings.disk);
  ui::Screen scr;
  scr.title = title.c_str();
  scr.subtitle = core.name;
  scr.hints = "Enter|Select;<>|Change;Esc|Resume";
  int sel = 0;
  MenuResult result = MenuResult::Resume;
  enum Kind : uint8_t { K_RESUME, K_RESET, K_CORE, K_SETTING, K_QUIT };
  for (;;) {
    ui::Item items[24];
    Kind kind[24];
    int arg[24];
    int n = 0;
    items[n] = { "Resume", nullptr, ui::ICON_PLAY }; kind[n++] = K_RESUME;
    if (core.menuItems) {
      CoreMenuItem ci[12];
      const int m = core.menuItems(ci, 12);
      for (int i = 0; i < m; i++) {
        items[n] = { ci[i].label, ci[i].value, ci[i].icon ? ci[i].icon : (uint8_t)ui::ICON_PAD, nullptr, -1,
                     ci[i].adjust };
        kind[n] = K_CORE; arg[n++] = i;
      }
    }
    items[n] = { "Reset (power cycle)", nullptr, ui::ICON_RESET }; kind[n++] = K_RESET;
    int rowOf[4];
    const int s = settingItems(items + n, rowOf);
    for (int i = 0; i < s; i++) { kind[n + i] = K_SETTING; arg[n + i] = rowOf[i]; }
    n += s;
    items[n] = { "Quit to home", nullptr, ui::ICON_QUIT }; kind[n++] = K_QUIT;

    const uint8_t k = ui::list(scr, items, n, &sel);
    if (k == KEY_ESC) break;
    const bool change = k == KEY_LEFT || k == KEY_RIGHT || k == KEY_ENTER;
    if (kind[sel] == K_SETTING) {
      if (change) adjustSetting(arg[sel], k == KEY_ENTER ? KEY_RIGHT : k);
      continue;
    }
    if (kind[sel] == K_CORE) {
      if (change && core.menuAction(arg[sel], k)) break;
      ui::setBackdrop(hal_video_front());   // a core action may have opened the browser
      continue;
    }
    if (k != KEY_ENTER) continue;
    if (kind[sel] == K_RESUME) break;
    if (kind[sel] == K_RESET) { core.reset(); break; }
    if (kind[sel] == K_QUIT) { result = MenuResult::Quit; break; }
  }
  settingsSave();
  ui::clearBackdrop();
  if (result == MenuResult::Resume) {
    ui::setStrip(true);
    hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
  }
  return result;
}

// The FPS meter: the bottom 10 rows of the panel.
static void showFps(const EmuCore &core, uint32_t fps, uint32_t drawn, unsigned load) {
  char b[96];
  if (core.fpsText) snprintf(b, sizeof(b), "%u fps  %s", (unsigned)fps, core.fpsText());   // drawn = fps here; room for max
  else snprintf(b, sizeof(b), "%u fps  %u drawn  cpu %u%%", (unsigned)fps, (unsigned)drawn, load);
  const int y = PANEL_H - 10;
  ui::fill(0, y, PANEL_W, 10, 0);
  ui::text(PANEL_W / 2, y + 5, b, load > 100 ? ui::kWarn : ui::kGood, 1, ui::MC);
  hal_video_wait();
  hal_panel_push(0, y, PANEL_W, 10, ui::canvas() + y * PANEL_W, PANEL_W);
#if defined(EMU_WEB)
  hal_web_status(1, b);
#endif
  hal_log("%s", b);
}

static void runMachine(const EmuCore &core, const std::string &disk) {
  ui::message("Starting", disk.empty() ? (std::string(core.name) + ", no disk").c_str() : fileName(disk).c_str());
  if (!core.init() || !core.loadRom(disk.c_str())) {
    ui::drawBackground();
    ui::message("Error", core.lastError ? core.lastError() : "Cannot start", 3000);
    core.shutdown();
    return;
  }
  hal_audio_volume(g_settings.volume);
  hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
  ui::setStrip(true);

  bool ctrl = false;
  uint32_t fpsT0 = hal_millis(), fpsFrames = 0;
  uint64_t fpsWork = 0;

  for (;;) {
    KeyEvent ev;
    bool menu = false;
    while (hal_key_poll(&ev)) {
      const bool down = ev.state != KEYSTATE_RELEASED;
      if (ev.code == KEY_RELEASE_ALL) {                 // the keyboard lost an event
        ctrl = false;
        core.keyEvent(ev.code, ev.state);
        continue;
      }
      if (ev.code == KEY_CTRL) ctrl = down;             // and falls through: the Mac sees Command
      if (ev.state == KEYSTATE_PRESSED && ctrl) {
        if (ev.code == KEY_ESC) { menu = true; continue; }
        if (ev.code == KEY_PGUP) hal_reboot_to_loader();
        if (ev.code == KEY_F8) hal_reboot();
      }
      core.keyEvent(ev.code, ev.state);
    }
    if (menu) {
      ctrl = false;
      core.keyEvent(KEY_RELEASE_ALL, KEYSTATE_RELEASED);
      hal_job_wait();                                 // the core may still be drawing on core 0
      if (gameMenu(core) == MenuResult::Quit) break;
      fpsT0 = hal_millis(); fpsFrames = 0; fpsWork = 0;
      continue;
    }

    const uint32_t t0 = hal_micros(), w0 = hal_audio_wait_us();
    core.runFrame(true);
    fpsWork += (hal_micros() - t0) - (hal_audio_wait_us() - w0);
    fpsFrames++;
    if (g_settings.showFps && hal_millis() - fpsT0 >= 1000) {
      const uint32_t ms = hal_millis() - fpsT0;
      const unsigned load = (unsigned)(fpsWork * core.frameRate() / 10000 / fpsFrames);
      showFps(core, fpsFrames * 1000 / ms, fpsFrames * 1000 / ms, load);
      fpsT0 = hal_millis(); fpsFrames = 0; fpsWork = 0;
    }
  }
  hal_audio_silence();
  hal_job_wait();
  ui::setStrip(false);
  core.shutdown();
}

// ------------------------------------------------------------------------------------------------
// home
// ------------------------------------------------------------------------------------------------
// The machine the home menu starts.
#if !defined(ESP_PLATFORM)
static const EmuCore &machineCore() {
  return g_settings.machine == MACHINE_II ? kMacII : g_settings.machine == MACHINE_IICI ? kMacIIci
       : g_settings.machine == MACHINE_QUADRA ? kMacQuadra : kMacPlus;
}
static const char *const kMachineNames[MACHINE_COUNT] = { "Mac Plus", "Mac II", "Mac IIci", "Quadra 700" };
#else
static const EmuCore &machineCore() { return g_settings.machine == MACHINE_II ? kMacII : kMacPlus; }
static const char *const kMachineNames[MACHINE_COUNT] = { "Mac Plus", "Mac II" };
#endif
static const char *const kRam2Names[RAM2_COUNT] = { "1 MB", "2 MB", "4 MB", "8 MB" };
static const char *const kRam3Names[RAM2_COUNT] = { "4 MB", "8 MB", "16 MB", "32 MB" };   // the IIci's
static const char *const kRam4Names[RAM2_COUNT] = { "4 MB", "8 MB", "16 MB", "32 MB" };   // the Quadra's

// The current machine's ROM path and (for the Mac IIs) its RAM index.
static std::string &romPathOf() {
  return g_settings.machine == MACHINE_II ? g_settings.romPathII
       : g_settings.machine == MACHINE_IICI ? g_settings.romPathIIci
       : g_settings.machine == MACHINE_QUADRA ? g_settings.romPathQuadra : g_settings.romPath;
}

// The ROM: any 128 KB file for the Plus (mac_core checks it is a Plus v3 when the Mac starts),
// 256 KB for the Mac II, 512 KB for the IIci, 1 MB for the Quadra.
static bool acceptRom(const std::string &path, std::string &tag) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fclose(f);
  const int kb = g_settings.machine == MACHINE_II ? 256 : g_settings.machine == MACHINE_IICI ? 512
               : g_settings.machine == MACHINE_QUADRA ? 1024 : 128;
  if (n != kb * 1024) return false;
  tag = kb == 1024 ? "1M" : kb == 512 ? "512K" : kb == 256 ? "256K" : "128K";
  return true;
}

static void chooseRom() {
  BrowseOpts o;
  o.title = g_settings.machine == MACHINE_II ? "Choose the Mac II ROM"
          : g_settings.machine == MACHINE_IICI ? "Choose the Mac IIci ROM"
          : g_settings.machine == MACHINE_QUADRA ? "Choose the Quadra 700 ROM" : "Choose the Mac Plus ROM";
  o.accept = acceptRom;
  o.exts = ".rom.bin";
  const std::string start = sdPath("picomac");
  o.startDir = start.c_str();
  std::string path;
  if (!browseRom(machineCore(), path, &o)) return;
  const std::string root = hal_sd_root();
  romPathOf() = path.size() > root.size() + 1 ? path.substr(root.size() + 1) : path;
  settingsSave();
}

static bool fileExists(const std::string &p) {
  FILE *f = fopen(p.c_str(), "rb");
  if (f) fclose(f);
  return f != nullptr;
}

// A yes/no box: true = the first answer.
static bool confirm(const char *title, const char *yes, const char *no) {
  ui::Item items[2] = { { yes, nullptr, ui::ICON_EXIT }, { no, nullptr, ui::ICON_UP } };
  ui::Screen scr;
  scr.title = title;
  scr.hints = "Enter|Select;Esc|Back";
  int sel = 1;
  return ui::list(scr, items, 2, &sel) == KEY_ENTER && sel == 0;
}

// A drive of the startup: choose an image or eject it (mac_drives.h, as the in-game Drives).
static void chooseDrive(int drive, const char *name) {
  std::string &path = drive ? g_settings.disk2 : g_settings.disk;
  const macdrives::Drive d = { name, path, drive ? "Any disk image" : "The Mac starts from it", ui::ICON_FOLDER };
  std::string out;
  const int r = macdrives::choose(machineCore(), d, out);
  ui::clearBackdrop();                            // choose() sets the in-game one after the browser
  if (r == 0) return;
  path = r == 1 ? out : std::string();
  settingsSave();
}

// A startup's screen (as pico-386's drives screen): Start, its snapshots, the machine and its
// drives, then Rename and Delete. Every change is saved into the startup at once (settingsSave).
// true = the Mac ran (the home list comes back after it).
static bool startupScreen(const std::string &path, bool canDelete) {
  if (!startup_open(path)) { ui::message("Error", "Cannot read the startup", 1500); return false; }
  ui::Screen scr;
  scr.hints = "Enter|Select;<>|Change;Esc|Back";
  int sel = 0;
  enum { S_START, S_SNAP, S_MACHINE, S_DISK, S_DISK2, S_ROM, S_RAM, S_SCREEN, S_SPEED, S_RENAME, S_DELETE };
  bool ran = false;
  while (!ran) {
    ui::Item items[16];
    int kind[16];
    int n = 0;
    const bool ii = g_settings.machine != MACHINE_PLUS;   // the colour Macs
    const bool iici = g_settings.machine == MACHINE_IICI, quadra = g_settings.machine == MACHINE_QUADRA;
    const char *disk1Label = ii ? "Hard disk" : "Boot disk";
    const char *disk2Label = ii ? "Floppy" : "Second disk";
    const std::string diskName = macdrives::fileName(g_settings.disk), disk2Name = macdrives::fileName(g_settings.disk2);
    const std::string &romRel = romPathOf();
    const std::string romShown = fileExists(sdPath(romRel.c_str())) ? fileName(romRel) : "Missing";
    char speed[8], snaps[8];
#if defined(ESP_PLATFORM)
    const int speeds = 4;
#else
    const int speeds = ii ? 5 : 4;                // 5 = Max: the Mac II as fast as this computer allows
#endif
    const int speedNow = g_settings.speed < 1 ? 1 : g_settings.speed > speeds ? speeds : g_settings.speed;
    if (speedNow == 5) snprintf(speed, sizeof speed, "Max");
    else snprintf(speed, sizeof speed, "%dx", speedNow);
    snprintf(snaps, sizeof snaps, "%u", (unsigned)macsnap_list().size());
    scr.title = startup_name();
    scr.subtitle = machineCore().name;
    items[n] = { "Start the Mac", nullptr, ui::ICON_PLAY }; kind[n++] = S_START;
    items[n] = { "Snapshots", snaps, ui::ICON_LOAD, "Start from one, or delete one" }; kind[n++] = S_SNAP;
    items[n] = { "Machine", kMachineNames[g_settings.machine], ui::ICON_GEAR, nullptr, -1, true }; kind[n++] = S_MACHINE;
    items[n] = { disk1Label, diskName.c_str(), ui::ICON_SAVE }; kind[n++] = S_DISK;
    items[n] = { disk2Label, disk2Name.c_str(), ui::ICON_LOAD }; kind[n++] = S_DISK2;
    items[n] = { "ROM", romShown.c_str(), ui::ICON_CART, "Shared by this machine's startups" }; kind[n++] = S_ROM;
    items[n] = { "RAM", quadra ? kRam4Names[g_settings.ramQuadra] : iici ? kRam3Names[g_settings.ramIIci] : ii ? kRam2Names[g_settings.ramII] : kRamNames[g_settings.ram], ui::ICON_GEAR, nullptr, -1, true }; kind[n++] = S_RAM;
#if !defined(EMU_WEB)   // the page shows the Mac's own picture, never the panel's
    // The Mac II has Fit and 1:1 pan only (its Fit is 320x240 in colour).
    items[n] = { "Screen", ii ? (g_settings.screen == SCREEN_PAN ? "1:1 pan" : "Fit 320x240") : kScreenNames[g_settings.screen],
                 ui::ICON_PAD, nullptr, -1, true }; kind[n++] = S_SCREEN;
#endif
#if defined(ESP_PLATFORM)
    if (!ii)   // the P4 runs a Mac II at 1x
#endif
    { items[n] = { "CPU speed", speed, ui::ICON_SKIP, nullptr, -1, true }; kind[n++] = S_SPEED; }
    items[n] = { "Rename", nullptr, ui::ICON_GEAR }; kind[n++] = S_RENAME;
    if (canDelete) { items[n] = { "Delete", nullptr, ui::ICON_EXIT, "Only this startup: the disks stay" }; kind[n++] = S_DELETE; }

    const uint8_t k = ui::list(scr, items, n, &sel);
    if (k == KEY_ESC || k == KEY_BACKSPACE) break;
    const bool change = k == KEY_LEFT || k == KEY_RIGHT || k == KEY_ENTER;
    const int d = k == KEY_LEFT ? -1 : 1;
    switch (kind[sel]) {
      case S_START:
        if (k != KEY_ENTER) break;
        settingsSave();
        runMachine(machineCore(), g_settings.disk);
        ran = true;
        break;
      case S_SNAP: {
        if (k != KEY_ENTER) break;
        std::string snap;
        if (!macsnap_pick("Snapshots", snap)) break;
        macsnap_set_pending(snap);                // mac_loadRom starts from it
        runMachine(machineCore(), g_settings.disk);
        ran = true;
        break;
      }
      case S_MACHINE:
        if (!change) break;
        g_settings.machine = (g_settings.machine + d + MACHINE_COUNT) % MACHINE_COUNT;   // the disks stay
        if (g_settings.machine != MACHINE_PLUS && g_settings.screen == SCREEN_NATIVE) g_settings.screen = SCREEN_FIT;
#if !defined(ESP_PLATFORM)
        // A Mac II/IIci at 1x is a real 16-25 MHz one: games like SimCity 2000 crawl. Max by default.
        if (g_settings.machine != MACHINE_PLUS) g_settings.speed = 5;
        else if (g_settings.speed == 5) g_settings.speed = 1;
#endif
        settingsSave();
        break;
      case S_DISK: if (k == KEY_ENTER) chooseDrive(0, disk1Label); break;
      case S_DISK2: if (k == KEY_ENTER) chooseDrive(1, disk2Label); break;
      case S_ROM: if (k == KEY_ENTER) chooseRom(); break;
      case S_RAM:
        if (!change) break;
        if (quadra) g_settings.ramQuadra = (g_settings.ramQuadra + d + RAM2_COUNT) % RAM2_COUNT;
        else if (iici) g_settings.ramIIci = (g_settings.ramIIci + d + RAM2_COUNT) % RAM2_COUNT;
        else if (ii) g_settings.ramII = (g_settings.ramII + d + RAM2_COUNT) % RAM2_COUNT;
        else g_settings.ram = (g_settings.ram + d + RAM_COUNT) % RAM_COUNT;
        settingsSave();
        break;
      case S_SCREEN:
        if (!change) break;
        if (ii) g_settings.screen = g_settings.screen == SCREEN_PAN ? SCREEN_FIT : SCREEN_PAN;
        else g_settings.screen = (g_settings.screen + d + SCREEN_COUNT) % SCREEN_COUNT;
        settingsSave();
        break;
      case S_SPEED:
        if (change) { g_settings.speed = (speedNow - 1 + d + speeds) % speeds + 1; settingsSave(); }
        break;
      case S_RENAME:
        if (k == KEY_ENTER) {
          char buf[29];
          snprintf(buf, sizeof buf, "%s", startup_name());
          if (ui::editText("Rename", "Its name on the home menu", buf, sizeof buf) && buf[0] && !startup_rename(buf))
            ui::message("Error", "Cannot write the startup", 1500);
        }
        break;
      case S_DELETE:
        if (k == KEY_ENTER && confirm("Delete this startup?", "Delete", "Keep it")) {
          if (!startup_delete()) ui::message("Error", "Cannot delete the startup", 1500);
          return false;
        }
        break;
    }
  }
  startup_close();
  return ran;
}

// Volume and the FPS meter (the frontend's own; the rest belongs to each startup).
static void settingsMenu() {
  ui::Screen scr;
  scr.title = "Settings";
  scr.subtitle = "Saved to /picomac/picomac.cfg";
  scr.hints = "<>|Change;Esc|Back";
  int sel = 0;
  for (;;) {
    ui::Item items[4];
    int rowOf[4];
    const int n = settingItems(items, rowOf);
    const uint8_t k = ui::list(scr, items, n, &sel);
    if (k == KEY_ESC || k == KEY_BACKSPACE) break;
    if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_ENTER) adjustSetting(rowOf[sel], k == KEY_ENTER ? KEY_RIGHT : k);
  }
  settingsSave();
}

// The home menu (as pico-386's): the startups, Create new startup, Settings.
static void home() {
  static int sel = -1;
  for (;;) {
    const std::vector<Startup> st = startups_list();
    const int ns = (int)st.size();
#if defined(EMU_WEB)
    const int top = 1;                    // the browser's Files row first: the ROM and disks come in there
#else
    const int top = 0;
#endif
    const int rowNew = top + ns, rowSettings = rowNew + 1;
#if defined(BOARD_PICOCALC_P4)
    const int rowLoader = rowNew + 2, rows = rowNew + 3;
#else
    const int rowLoader = -1, rows = rowNew + 2;
#endif
    if (sel < 0) {                        // the startup last chosen
      sel = top;
      for (int i = 0; i < ns; i++)
        if (st[i].file == g_settings.startup) sel = top + i;
    }
    if (sel >= rows) sel = rows - 1;
    std::vector<std::string> lines(ns);
    std::vector<ui::Item> items(rows);
    for (int i = 0; i < ns; i++) {
      lines[i] = startup_summary(st[i].path);
      items[top + i] = { st[i].name.c_str(), nullptr, ui::ICON_PLAY, lines[i].c_str() };
    }
#if defined(EMU_WEB)
    items[0] = { "Files", nullptr, ui::ICON_FOLDER, "Upload, receive from a PC, download, delete" };
#endif
    items[rowNew] = { "Create new startup", nullptr, ui::ICON_FOLDER, "A named Mac: machine, disks, RAM" };
    items[rowSettings] = { "Settings", nullptr, ui::ICON_GEAR, "Volume, FPS meter" };
#if defined(BOARD_PICOCALC_P4)
    items[rowLoader] = { "Back to the loader", nullptr, ui::ICON_EXIT, "The esp32loader menu" };
#endif
    ui::Screen scr;
    scr.title = "pico-mac";
    scr.subtitle = "Startups";
    scr.logo = true;
    scr.hints = "Enter|Select;Ctrl+Esc|Menu in the Mac";
    const uint8_t k = ui::list(scr, items.data(), rows, &sel);
    if (k != KEY_ENTER) continue;
#if defined(EMU_WEB)
    if (sel == 0) { hal_web_files(); continue; }
#endif
    if (sel >= top && sel < top + ns) {
      g_settings.startup = st[sel - top].file;
      settingsSave();
      const bool ran = startupScreen(st[sel - top].path, ns > 1);
      sel = -1;                           // renamed (re-sorted) or deleted: find it again
      if (ran) return;
      continue;
    }
    if (sel == rowNew) {
      char buf[29] = "New startup";
      if (!ui::editText("Create new startup", "Its name on the home menu", buf, sizeof buf) || !buf[0]) continue;
      const std::string path = startup_create(buf);
      if (path.empty()) { ui::message("Error", "Cannot write the startup (card full?)", 2000); continue; }
      g_settings.startup = path.substr(path.find_last_of('/') + 1);
      settingsSave();
      sel = -1;
      if (startupScreen(path, true)) return;
      continue;
    }
    if (sel == rowSettings) { settingsMenu(); continue; }
    if (sel == rowLoader) hal_reboot_to_loader();
  }
}

// ------------------------------------------------------------------------------------------------
void app_setup() {
  hal_init();
  if (!ui::begin()) { hal_log("out of memory for the UI canvas"); for (;;) hal_delay(1000); }
  hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
  if (hal_sd_mounted()) settingsLoad();
  hal_audio_volume(g_settings.volume);
}

void app_loop() {
  if (!hal_sd_mounted()) {
    ui::drawBackground();
    ui::message("No SD card", "Insert a card with /picomac and restart", 0);
    for (;;) ui::waitKey();   // its hotkeys still reach the loader / restart
  }
#if defined(BOARD_DESKTOP)
  if (const char *m = getenv("MAC_MACHINE"))                                          // test hook: "ii", "iici", "quadra" or "plus"
    settingsSetMachine(!strcmp(m, "ii") ? MACHINE_II : !strcmp(m, "iici") ? MACHINE_IICI
                       : !strcmp(m, "quadra") ? MACHINE_QUADRA : MACHINE_PLUS);
  if (const char *disk2 = getenv("MAC_DISK2")) g_settings.disk2 = disk2;              // test hook
  if (const char *rom = getenv("MAC_ROM2")) g_settings.romPathII = rom;               // test hook (relative to the card)
  if (const char *rom = getenv("MAC_ROM3")) g_settings.romPathIIci = rom;             // test hook: the IIci's
  if (const char *rom = getenv("MAC_ROM4")) g_settings.romPathQuadra = rom;           // test hook: the Quadra's
  if (const char *ram = getenv("MAC_RAM")) {                                         // test hook: the RAM row's index
    const int r = atoi(ram);
    if (g_settings.machine == MACHINE_QUADRA) g_settings.ramQuadra = r;
    else if (g_settings.machine == MACHINE_IICI) g_settings.ramIIci = r;
    else if (g_settings.machine == MACHINE_II) g_settings.ramII = r;
    else g_settings.ram = r;
  }
  if (const char *snap = getenv("MAC_SNAP")) {                                        // test hook: start from it
    macsnap_set_pending(snap);
    runMachine(machineCore(), g_settings.disk);
    exit(0);
  }
  if (const char *disk = getenv("MAC_DISK")) {                                        // test hook
    runMachine(machineCore(), disk);
    exit(0);
  }
#endif
  home();
}
