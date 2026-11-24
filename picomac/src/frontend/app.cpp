// app.cpp -- the frontend (from emu16 / pico-amiga / pico-386): the home menu (boot disk, ROM,
// machine settings), then the machine loop with the in-game menu and the FPS meter.
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
#include "app.h"
#include "ui.h"
#include "browser.h"
#include "settings.h"
#include "../hal/hal.h"
#include "../../core.h"
#include "../mac/mac_snapshot.h"

extern const EmuCore kMacPlus;
extern const EmuCore kMacII;
const EmuCore *const kCores[] = { &kMacPlus, &kMacII };
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
  const std::string title = g_settings.disk.empty() ? std::string("No disk") : fileName(g_settings.disk);
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
  if (core.fpsText) snprintf(b, sizeof(b), "%u fps  %u drawn  %s", (unsigned)fps, (unsigned)drawn, core.fpsText());
  else snprintf(b, sizeof(b), "%u fps  %u drawn  cpu %u%%", (unsigned)fps, (unsigned)drawn, load);
  const int y = PANEL_H - 10;
  ui::fill(0, y, PANEL_W, 10, 0);
  ui::text(PANEL_W / 2, y + 5, b, load > 100 ? ui::kWarn : ui::kGood, 1, ui::MC);
  hal_video_wait();
  hal_panel_push(0, y, PANEL_W, 10, ui::canvas() + y * PANEL_W, PANEL_W);
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
static const EmuCore &machineCore() { return g_settings.machine == MACHINE_II ? kMacII : kMacPlus; }
static const char *const kMachineNames[MACHINE_COUNT] = { "Mac Plus", "Mac II" };
static const char *const kRam2Names[RAM2_COUNT] = { "1 MB", "2 MB", "4 MB", "8 MB" };

// The ROM: any 128 KB file for the Plus (mac_core checks it is a Plus v3 when the Mac starts),
// 256 KB for the Mac II.
static bool acceptRom(const std::string &path, std::string &tag) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fclose(f);
  const bool ii = g_settings.machine == MACHINE_II;
  if (n != (ii ? 256 : 128) * 1024) return false;
  tag = ii ? "256K" : "128K";
  return true;
}

static void chooseRom() {
  BrowseOpts o;
  const bool ii = g_settings.machine == MACHINE_II;
  o.title = ii ? "Choose the Mac II ROM" : "Choose the Mac Plus ROM";
  o.accept = acceptRom;
  o.exts = ".rom.bin";
  const std::string start = sdPath("picomac");
  o.startDir = start.c_str();
  std::string path;
  if (!browseRom(machineCore(), path, &o)) return;
  const std::string root = hal_sd_root();
  (ii ? g_settings.romPathII : g_settings.romPath) = path.size() > root.size() + 1 ? path.substr(root.size() + 1) : path;
  settingsSave();
}

static void chooseDisk() {
  std::string path;
  if (!browseRom(kMacPlus, path)) return;
  g_settings.disk = path;
  settingsSave();
}

static void chooseDisk2() {
  BrowseOpts o;
  o.title = "Choose the second disk";
  std::string path;
  if (!browseRom(kMacPlus, path, &o)) return;
  g_settings.disk2 = path;
  settingsSave();
}

static bool fileExists(const std::string &p) {
  FILE *f = fopen(p.c_str(), "rb");
  if (f) fclose(f);
  return f != nullptr;
}

static void home() {
  ui::Screen scr;
  scr.title = "pico-mac";
  scr.subtitle = machineCore().name;
  scr.logo = true;
  scr.hints = "Enter|Select;<>|Change;Ctrl+Esc|Menu in the Mac";
  static int sel = 0;
  enum { H_START, H_MACHINE, H_SNAP, H_DISK, H_NODISK, H_DISK2, H_NODISK2, H_ROM, H_RAM, H_SCREEN, H_SPEED, H_SETTING, H_LOADER };
  for (;;) {
    ui::Item items[16];
    int kind[16], arg[16];
    int n = 0;
    const std::string diskName = g_settings.disk.empty() ? "None" : fileName(g_settings.disk);
    const std::string disk2Name = g_settings.disk2.empty() ? "None" : fileName(g_settings.disk2);
    const bool ii = g_settings.machine == MACHINE_II;
    const std::string &romRel = ii ? g_settings.romPathII : g_settings.romPath;
    const std::string romShown = fileExists(sdPath(romRel.c_str())) ? fileName(romRel) : "Missing";
    scr.subtitle = machineCore().name;
    char speed[8];
    snprintf(speed, sizeof speed, "%dx", g_settings.speed);
    items[n] = { "Start the Mac", nullptr, ui::ICON_PLAY }; kind[n++] = H_START;
    items[n] = { "Machine", kMachineNames[g_settings.machine], ui::ICON_GEAR, nullptr, -1, true }; kind[n++] = H_MACHINE;
    // From a snapshot: it brings its own disks, ROM, RAM and screen (as pico-386).
    char snaps[8];
    snprintf(snaps, sizeof snaps, "%u", (unsigned)macsnap_list().size());
    items[n] = { "Load snapshot", snaps, ui::ICON_LOAD }; kind[n++] = H_SNAP;   // the machine's own snapshots
    items[n] = { "Boot disk", diskName.c_str(), ui::ICON_FOLDER }; kind[n++] = H_DISK;
    if (!g_settings.disk.empty()) { items[n] = { "Eject (start with no disk)", nullptr, ui::ICON_UP }; kind[n++] = H_NODISK; }
    items[n] = { "Second disk", disk2Name.c_str(), ui::ICON_FOLDER }; kind[n++] = H_DISK2;
    if (!g_settings.disk2.empty()) { items[n] = { "Eject the second disk", nullptr, ui::ICON_UP }; kind[n++] = H_NODISK2; }
    items[n] = { "ROM", romShown.c_str(), ui::ICON_CART }; kind[n++] = H_ROM;
    items[n] = { "RAM", ii ? kRam2Names[g_settings.ramII] : kRamNames[g_settings.ram], ui::ICON_GEAR, nullptr, -1, true }; kind[n++] = H_RAM;
    // The Mac II has Fit and 1:1 pan only (its Fit is 320x240 in colour).
    items[n] = { "Screen", ii ? (g_settings.screen == SCREEN_PAN ? "1:1 pan" : "Fit 320x240") : kScreenNames[g_settings.screen],
                 ui::ICON_PAD, nullptr, -1, true }; kind[n++] = H_SCREEN;
    if (!ii) { items[n] = { "CPU speed", speed, ui::ICON_SKIP, nullptr, -1, true }; kind[n++] = H_SPEED; }   // the Mac II runs at 1x
    int rowOf[4];
    const int s = settingItems(items + n, rowOf);
    for (int i = 0; i < s; i++) { kind[n + i] = H_SETTING; arg[n + i] = rowOf[i]; }
    n += s;
#if defined(BOARD_PICOCALC_P4)
    items[n] = { "Back to the loader", nullptr, ui::ICON_EXIT }; kind[n++] = H_LOADER;
#endif

    const uint8_t k = ui::list(scr, items, n, &sel);
    const bool change = k == KEY_LEFT || k == KEY_RIGHT || k == KEY_ENTER;
    const int d = k == KEY_LEFT ? -1 : 1;
    switch (kind[sel]) {
      case H_START: if (k == KEY_ENTER) { settingsSave(); return runMachine(machineCore(), g_settings.disk); } break;
      case H_MACHINE: if (change) settingsSetMachine((g_settings.machine + d + MACHINE_COUNT) % MACHINE_COUNT); break;
      case H_SNAP:
        if (k == KEY_ENTER) {
          std::string snap;
          if (!macsnap_pick("Load snapshot", snap)) break;
          macsnap_set_pending(snap);              // mac_loadRom starts from it
          return runMachine(machineCore(), g_settings.disk);
        }
        break;
      case H_DISK: if (k == KEY_ENTER) chooseDisk(); break;
      case H_NODISK: if (k == KEY_ENTER) { g_settings.disk.clear(); settingsSave(); } break;
      case H_DISK2: if (k == KEY_ENTER) chooseDisk2(); break;
      case H_NODISK2: if (k == KEY_ENTER) { g_settings.disk2.clear(); settingsSave(); } break;
      case H_ROM: if (k == KEY_ENTER) chooseRom(); break;
      case H_RAM:
        if (!change) break;
        if (ii) g_settings.ramII = (g_settings.ramII + d + RAM2_COUNT) % RAM2_COUNT;
        else g_settings.ram = (g_settings.ram + d + RAM_COUNT) % RAM_COUNT;
        break;
      case H_SCREEN:
        if (!change) break;
        if (ii) g_settings.screen = g_settings.screen == SCREEN_PAN ? SCREEN_FIT : SCREEN_PAN;
        else g_settings.screen = (g_settings.screen + d + SCREEN_COUNT) % SCREEN_COUNT;
        break;
      case H_SPEED: if (change) g_settings.speed = (g_settings.speed - 1 + d + 4) % 4 + 1; break;
      case H_SETTING: if (change) adjustSetting(arg[sel], k == KEY_ENTER ? KEY_RIGHT : k); break;
      case H_LOADER: if (k == KEY_ENTER) hal_reboot_to_loader(); break;
    }
    if (change && kind[sel] >= H_RAM) settingsSave();
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
  if (const char *m = getenv("MAC_MACHINE"))                                          // test hook: "ii" or "plus"
    settingsSetMachine(!strcmp(m, "ii") ? MACHINE_II : MACHINE_PLUS);
  if (const char *disk2 = getenv("MAC_DISK2")) g_settings.disk2 = disk2;              // test hook
  if (const char *rom = getenv("MAC_ROM2")) g_settings.romPathII = rom;               // test hook (relative to the card)
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
