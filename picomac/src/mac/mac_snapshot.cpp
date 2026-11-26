// mac_snapshot.cpp -- snapshots of a running Mac, to start from later where it stopped. Laid out
// like pico-386's pc_snapshot.cpp.
//
// A snapshot is /picomac/snapshots/<hardware>/sN.snp (a Mac II's: <hardware>-macii): a header
// with its name, the machine it needs (the disks, ROM, RAM size and screen mode) and the
// generation of each disk, then the Mac itself, as its core writes it (the Plus: umac_save_state,
// the 68000, the VIA, SCC, RTC, keyboard, drives and all of the RAM; the Mac II: macii_core.cpp).
// Restoring starts a Mac with that machine (the settings follow it) and loads the state into it.
//
// The Mac keeps parts of its disks in RAM (the volume's bitmap and catalog, the file buffers), so
// a snapshot only fits the disks as they were when it was taken. Each disk's generation, in
// /picomac/snapshots/disks.gen, goes up at its first write after a start or a snapshot; restoring
// a snapshot whose disks moved on asks first.
//
// Beside each sN.snp is sN.bmp, the Mac's screen when it was taken (80x54, RGB565 top-down). The
// list shows it at the left of the name. g_settings.machine says which Mac's snapshots these are.
//
// <hardware> keeps each build's snapshots apart: the 68000's context is saved as this build lays
// it out.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <algorithm>
#include <string>
#include <vector>
#include "../../board.h"
#include "mac_snapshot.h"
#include "../frontend/ui.h"
#include "../frontend/settings.h"
#include "../frontend/startups.h"
#include "../hal/hal.h"

namespace {

constexpr char kMagic[8] = { 'P', 'M', 'A', 'C', 'S', 'N', 'P', '1' };
constexpr char kMagicII[8] = { 'P', 'M', 'I', 'I', 'S', 'N', 'P', '1' };
constexpr char kMagicIIci[8] = { 'P', 'M', 'I', 'C', 'S', 'N', 'P', '1' };
constexpr char kMagicQuadra[8] = { 'P', 'M', 'Q', '7', 'S', 'N', 'P', '1' };
bool isII() { return g_settings.machine != MACHINE_PLUS; }
bool isIIci() { return g_settings.machine == MACHINE_IICI; }
bool isQuadra() { return g_settings.machine == MACHINE_QUADRA; }
const char *magic() { return isQuadra() ? kMagicQuadra : isIIci() ? kMagicIIci : isII() ? kMagicII : kMagic; }
constexpr int kThumbW = 80, kThumbH = 54;
std::string s_pending;                 // the snapshot mac_loadRom starts from next

#if defined(BOARD_PICOCALC_P4)
constexpr char kHardware[] = "p4";
#elif defined(EMU_WEB)
constexpr char kHardware[] = "web";
#else
constexpr char kHardware[] = "desktop";
#endif

// A startup's own folder (startups.h), else the machine's; each Mac's snapshots apart.
std::string snapDir() {
  const char *own = startup_snapdir();
  const std::string base = *own ? std::string("picomac/") + own : std::string("picomac/snapshots/") + kHardware;
  return sdPath((base + (isQuadra() ? "-macquadra" : isIIci() ? "-maciici" : isII() ? "-macii" : "")).c_str());
}
std::string genFile() { return sdPath("picomac/snapshots/disks.gen"); }

// Paths are kept relative to the card's root, so a card moved between machines still matches.
std::string relOf(const std::string &path) {
  const std::string root = hal_sd_root();
  if (path.size() > root.size() && !path.compare(0, root.size(), root) && path[root.size()] == '/')
    return path.substr(root.size() + 1);
  return path;
}

std::string fullOf(const std::string &rel) { return rel.empty() ? rel : sdPath(rel.c_str()); }

std::string readAll(const std::string &path) {
  std::string s;
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return s;
  char buf[512];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0 && s.size() < 65536) s.append(buf, n);
  fclose(f);
  return s;
}

uint32_t genOf(const std::string &rel) {
  FILE *f = fopen(genFile().c_str(), "r");
  if (!f) return 0;
  char line[300];
  uint32_t gen = 0;
  while (fgets(line, sizeof line, f)) {
    char *sp = strchr(line, ' ');
    if (!sp) continue;
    sp[1 + strcspn(sp + 1, "\r\n")] = 0;
    if (!strcasecmp(sp + 1, rel.c_str())) { gen = (uint32_t)strtoul(line, nullptr, 10); break; }
  }
  fclose(f);
  return gen;
}

// ---- the file's pieces -------------------------------------------------------------------------

bool put(FILE *f, const void *p, size_t n) { return fwrite(p, 1, n, f) == n; }
bool get(FILE *f, void *p, size_t n) { return fread(p, 1, n, f) == n; }

bool putStr(FILE *f, const std::string &s) {
  const uint32_t n = (uint32_t)s.size();
  return put(f, &n, 4) && put(f, s.data(), n);
}

bool getStr(FILE *f, std::string &s, size_t max) {
  uint32_t n;
  if (!get(f, &n, 4) || n > max) return false;
  s.resize(n);
  return n == 0 || get(f, &s[0], n);
}

struct Header {
  std::string name, disk, disk2, rom;   // the paths relative to the card
  uint32_t ram = 0, screen = 0, gen[2] = { 0, 0 };
};

// Reads the header; f is left at the machine state.
bool readHeader(FILE *f, Header &h, bool nameOnly) {
  char mg[8];
  if (!get(f, mg, 8) || memcmp(mg, magic(), 8) || !getStr(f, h.name, 200)) return false;
  if (nameOnly) return true;
  return getStr(f, h.disk, 300) && getStr(f, h.disk2, 300) && getStr(f, h.rom, 300) && get(f, &h.ram, 4) &&
         get(f, &h.screen, 4) && get(f, h.gen, sizeof h.gen);
}

bool readHeaderOf(const std::string &snap, Header &h) {
  FILE *f = fopen(snap.c_str(), "rb");
  if (!f) return false;
  const bool ok = readHeader(f, h, false);
  fclose(f);
  return ok;
}

int numberOf(const char *file) {
  if (file[0] != 's' && file[0] != 'S') return -1;
  char *end;
  const long n = strtol(file + 1, &end, 10);
  return end != file + 1 && !strcasecmp(end, ".snp") ? (int)n : -1;
}


// ---- the screen's picture -----------------------------------------------------------------------

std::string pictureOf(const std::string &snap) { return snap.substr(0, snap.size() - 4) + ".bmp"; }

constexpr int kBmpHead = 14 + 40 + 12;         // file header, BITMAPINFOHEADER, the 565 masks

void le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void le32(uint8_t *p, uint32_t v) { le16(p, v); le16(p + 2, v >> 16); }
uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// The Mac's screen as sN.bmp: each picture pixel is the average of the screen pixels it covers
// (the screen is scaled to fit, keeping its shape). A failure only loses the picture.
void savePicture(const std::string &snap, MacSnapPixel pixel, int w, int h) {
  const std::string path = pictureOf(snap);
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return;
  const uint32_t data = kThumbW * kThumbH * 2;
  uint8_t hd[kBmpHead] = { 'B', 'M' };
  le32(hd + 2, kBmpHead + data);
  le32(hd + 10, kBmpHead);
  le32(hd + 14, 40);
  le32(hd + 18, kThumbW);
  le32(hd + 22, (uint32_t)-kThumbH);            // negative: the top row first
  le16(hd + 26, 1);
  le16(hd + 28, 16);
  le32(hd + 30, 3);                              // BI_BITFIELDS
  le32(hd + 34, data);
  le32(hd + 54, 0xF800);
  le32(hd + 58, 0x07E0);
  le32(hd + 62, 0x001F);
  bool ok = put(f, hd, sizeof hd);
  // the picture's size: the screen scaled by the larger of the two ratios
  int pw = kThumbW, ph = h * kThumbW / w;
  if (ph > kThumbH) { ph = kThumbH; pw = w * kThumbH / h; }
  const int ox = (kThumbW - pw) / 2, oy = (kThumbH - ph) / 2;
  uint16_t row[kThumbW];
  for (int ty = 0; ty < kThumbH && ok; ty++) {
    for (int tx = 0; tx < kThumbW; tx++) {
      const int px = tx - ox, py = ty - oy;
      if (px < 0 || py < 0 || px >= pw || py >= ph) { row[tx] = 0; continue; }
      const int x0 = px * w / pw, x1 = std::max(x0 + 1, (px + 1) * w / pw);
      const int y0 = py * h / ph, y1 = std::max(y0 + 1, (py + 1) * h / ph);
      uint32_t r = 0, g = 0, b = 0;
      for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
          const uint32_t c = pixel(x, y);
          r += c >> 16 & 255; g += c >> 8 & 255; b += c & 255;
        }
      const uint32_t n = (uint32_t)((x1 - x0) * (y1 - y0));
      r /= n; g /= n; b /= n;
      row[tx] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
    ok = put(f, row, sizeof row);               // RGB565 little-endian on every target
  }
  if (fclose(f) != 0) ok = false;
  if (!ok) remove(path.c_str());
}

// Draws snap's picture on the menu canvas at x, y with a frame around it, a row at a time
// straight into the canvas.
void drawPicture(const std::string &snap, int x, int y, bool on) {
  constexpr int W = kThumbW, H = kThumbH;
  const uint16_t edge = on ? ui::kSelText : ui::kLine;
  ui::fill(x - 1, y - 1, W + 2, 1, edge);
  ui::fill(x - 1, y + H, W + 2, 1, edge);
  ui::fill(x - 1, y, 1, H, edge);
  ui::fill(x + W, y, 1, H, edge);
  ui::fill(x, y, W, H, 0);
  if (x < 0 || y < 0 || x + W > PANEL_W || y + H > PANEL_H) return;
  FILE *f = fopen(pictureOf(snap).c_str(), "rb");
  if (!f) return;
  uint8_t h[kBmpHead];
  if (get(f, h, sizeof h) && h[0] == 'B' && h[1] == 'M' && rd32(h + 18) == (uint32_t)W &&
      ((int32_t)rd32(h + 22) == -H || (int32_t)rd32(h + 22) == H) && h[28] == 16 &&
      fseek(f, (long)rd32(h + 10), SEEK_SET) == 0) {
    const bool topDown = (int32_t)rd32(h + 22) < 0;
    uint16_t *cv = ui::canvas();
    for (int r = 0; r < H; r++)
      if (fread(cv + (y + (topDown ? r : H - 1 - r)) * PANEL_W + x, 2, W, f) != (size_t)W) break;
  }
  fclose(f);
}

} // namespace

// ---- generations ---------------------------------------------------------------------------------

void macsnap_touch(const std::string &diskPath) {
  // Small enough to rewrite whole: one line per disk ever written ("7 picomac/system.dsk").
  const std::string rel = relOf(diskPath);
  const std::string all = readAll(genFile());
  std::string out;
  uint32_t gen = 0;
  size_t at = 0;
  while (at < all.size()) {
    size_t nl = all.find('\n', at);
    if (nl == std::string::npos) nl = all.size();
    std::string line = all.substr(at, nl - at);
    at = nl + 1;
    while (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t sp = line.find(' ');
    if (sp == std::string::npos) continue;
    if (!strcasecmp(line.c_str() + sp + 1, rel.c_str())) gen = (uint32_t)strtoul(line.c_str(), nullptr, 10);
    else out += line + "\n";
  }
  out += std::to_string(gen + 1) + " " + rel + "\n";
  makeDirs(sdPath("picomac/snapshots"));
  FILE *f = fopen(genFile().c_str(), "wb");
  if (!f) return;
  fputs(out.c_str(), f);
  fclose(f);
}

// ---- the list ------------------------------------------------------------------------------------

std::vector<MacSnapshot> macsnap_list() {
  std::vector<MacSnapshot> out;
  const std::string dir = snapDir();
  DIR *d = opendir(dir.c_str());
  if (!d) return out;
  std::vector<std::pair<int, std::string>> files;
  while (dirent *e = readdir(d))
    if (numberOf(e->d_name) >= 0) files.push_back({ numberOf(e->d_name), e->d_name });
  closedir(d);
  std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.first > b.first; });   // newest first
  for (const auto &fn : files) {
    const std::string path = dir + "/" + fn.second;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) continue;
    Header h;
    if (readHeader(f, h, true)) out.push_back({ path, h.name });
    fclose(f);
  }
  return out;
}

// ---- saving ----------------------------------------------------------------------------------------

const char *macsnap_save(const std::string &name, const std::string &replace, const MacSnapMachine &m,
                         MacSnapPixel pixel, int w, int h, MacSnapWrite state) {
  const std::string dir = snapDir();
  if (!makeDirs(dir)) return "Cannot make the snapshot folder";
  int next = 1;
  for (const MacSnapshot &s : macsnap_list()) {
    const int n = numberOf(s.path.c_str() + s.path.find_last_of('/') + 1);
    if (n >= next) next = n + 1;
  }
  const std::string path = dir + "/s" + std::to_string(next) + ".snp";
  Header hd;
  hd.name = name;
  hd.disk = relOf(m.disk);
  hd.disk2 = relOf(m.disk2);
  hd.rom = m.rom;
  hd.ram = (uint32_t)m.ram;
  hd.screen = (uint32_t)m.screen;
  hd.gen[0] = hd.disk.empty() ? 0 : genOf(hd.disk);
  hd.gen[1] = hd.disk2.empty() ? 0 : genOf(hd.disk2);

  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return "Cannot write the snapshot";
  bool ok = put(f, magic(), 8) && putStr(f, hd.name) && putStr(f, hd.disk) && putStr(f, hd.disk2) &&
            putStr(f, hd.rom) && put(f, &hd.ram, 4) && put(f, &hd.screen, 4) && put(f, hd.gen, sizeof hd.gen);
  ok = ok && state(f);
  if (fclose(f) != 0) ok = false;
  if (!ok) {
    remove(path.c_str());
    return "Cannot write the snapshot (card full?)";
  }
  savePicture(path, pixel, w, h);
  // Overwriting saves a new one and then drops the old: a failed save above keeps it, and the
  // list (newest first) shows the overwritten one at the top.
  if (!replace.empty() && replace != path) {
    remove(replace.c_str());
    remove(pictureOf(replace).c_str());
  }
  hal_log("snapshot: %s saved as %s", name.c_str(), path.c_str());
  return nullptr;
}

// ---- restoring -------------------------------------------------------------------------------------

bool macsnap_machine(const std::string &snap, MacSnapMachine &m) {
  Header h;
  if (!readHeaderOf(snap, h) || h.ram >= (uint32_t)(isII() ? RAM2_COUNT : RAM_COUNT) || h.screen >= SCREEN_COUNT) return false;
  m.disk = fullOf(h.disk);
  m.disk2 = fullOf(h.disk2);
  m.rom = h.rom;
  m.ram = (int)h.ram;
  m.screen = (int)h.screen;
  return true;
}

bool macsnap_disks_changed(const std::string &snap) {
  Header h;
  if (!readHeaderOf(snap, h)) return false;
  return (!h.disk.empty() && genOf(h.disk) != h.gen[0]) || (!h.disk2.empty() && genOf(h.disk2) != h.gen[1]);
}

const char *macsnap_restore(const std::string &snap, bool *broken, MacSnapRead state) {
  *broken = false;
  FILE *f = fopen(snap.c_str(), "rb");
  if (!f) return "Cannot open the snapshot";
  Header h;
  const int r = readHeader(f, h, false) ? state(f) : -2;
  fclose(f);
  if (r == -2) return "Snapshot from another version";
  if (r != 0) { *broken = true; return "Cannot read the snapshot"; }
  hal_log("snapshot: %s restored", snap.c_str());
  return nullptr;
}

void macsnap_set_pending(const std::string &snap) { s_pending = snap; }

std::string macsnap_take_pending() {
  std::string s;
  s.swap(s_pending);
  return s;
}

// ---- the list screen ---------------------------------------------------------------------------

namespace {

// The snapshots' list, with their pictures. withNew: row 0 is "New snapshot" and the snapshots
// follow. The key that ended it (ui::list).
uint8_t snapList(const std::vector<MacSnapshot> &snaps, bool withNew, const char *title, const char *subtitle,
                 int *sel) {
  struct Ctx { const std::vector<MacSnapshot> *snaps; bool withNew; } ctx = { &snaps, withNew };
  const int off = withNew ? 1 : 0, n = (int)snaps.size() + off;
  std::vector<ui::Item> items(n);
  if (withNew) items[0] = { "New snapshot", nullptr, ui::ICON_NONE, "Keep the others" };
  for (size_t i = 0; i < snaps.size(); i++)
    items[i + off] = { snaps[i].name.c_str(), nullptr, ui::ICON_NONE, withNew ? (i ? "Overwrite" : "Overwrite the last one") : nullptr };
  ui::Screen scr;
  scr.title = title;
  scr.subtitle = subtitle;
  scr.hints = "Enter|Select;Esc|Back";
  scr.rowH = kThumbH + 6;
  scr.pictureW = kThumbW;
  scr.ctx = &ctx;
  scr.picture = [](void *c, int i, int x, int y, bool on) {
    const Ctx &k = *(const Ctx *)c;
    if (k.withNew && i == 0) {                 // an empty frame with a plus
      const uint16_t edge = on ? ui::kSelText : ui::kLine;
      ui::fill(x - 1, y - 1, kThumbW + 2, 1, edge);
      ui::fill(x - 1, y + kThumbH, kThumbW + 2, 1, edge);
      ui::fill(x - 1, y, 1, kThumbH, edge);
      ui::fill(x + kThumbW, y, 1, kThumbH, edge);
      const int cx = x + kThumbW / 2, cy = y + kThumbH / 2;
      ui::fill(cx - 12, cy - 2, 24, 4, edge);
      ui::fill(cx - 2, cy - 12, 4, 24, edge);
      return;
    }
    drawPicture((*k.snaps)[i - (k.withNew ? 1 : 0)].path, x, y, on);
  };
  if (*sel >= n) *sel = n - 1;
  return ui::list(scr, items.data(), n, sel);
}

} // namespace

bool macsnap_pick_save(std::string &replace, std::string &name) {
  const std::vector<MacSnapshot> snaps = macsnap_list();
  replace.clear();
  // No default name: "Snapshot 3" says nothing a week later, so the user types one.
  char buf[40] = "";
  if (!snaps.empty()) {
    int sel = 0;
    if (snapList(snaps, true, "Save snapshot", "A new one, or one to overwrite", &sel) != KEY_ENTER) return false;
    if (sel > 0) {
      replace = snaps[sel - 1].path;
      snprintf(buf, sizeof buf, "%s", snaps[sel - 1].name.c_str());
    }
  }
  if (!ui::editText("Save snapshot", replace.empty() ? "Type a name, e.g. Finder ready" : "Overwrite it as", buf,
                    sizeof buf))
    return false;
  name = buf;
  return true;
}

bool macsnap_pick(const char *title, std::string &out) {
  int sel = 0;
  for (;;) {
    const std::vector<MacSnapshot> snaps = macsnap_list();
    if (snaps.empty()) {
      ui::message(title, "No snapshots yet: save one from the menu (Ctrl+Esc)", 2200);
      return false;
    }
    if (snapList(snaps, false, title, "Start from where a snapshot stopped", &sel) != KEY_ENTER) return false;

    const MacSnapshot &s = snaps[sel];
    const bool changed = macsnap_disks_changed(s.path);
    ui::Item acts[3] = {
      { "Start from it", nullptr, ui::ICON_PLAY,
        changed ? "A disk was written since: the Mac may damage it" : "The disks it had come back too" },
      { "Delete", nullptr, ui::ICON_EXIT },
      { "Back", nullptr, ui::ICON_SKIP },
    };
    ui::Screen as;
    as.title = s.name.c_str();
    as.subtitle = title;
    as.hints = "Enter|Select;Esc|Back";
    int a = 0;
    if (ui::list(as, acts, 3, &a) != KEY_ENTER || a == 2) continue;
    if (a == 1) {
      if (remove(s.path.c_str()) != 0) ui::message("Error", "Cannot delete the snapshot", 1500);
      else remove(pictureOf(s.path).c_str());
      continue;
    }
    out = s.path;
    return true;
  }
}
