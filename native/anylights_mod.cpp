// AnyLights: one paint tool for every colour, and lights that glow the colour they are painted.
//
// Paint tool: hold any stock paint tool and right click. A panel shows the game's whole palette as real colour swatches
// with a large preview (and how a light painted that colour glows), plus the paint modes: fill, flood, brush, pencil
// and the two erasers. The pick applies to every paint tool, so one tool paints any colour. Ctrl+Z undoes the last
// click on parts and plates (pixel strokes can't be undone yet).
//
// Lights: spot, omni and rotating lights glow the colour of their paint (same hue at full brightness). Unpainted lights
// keep the stock warm white.
//
// Multiplayer: painting only sends the game's own paint events with a palette colour, which every host accepts as is,
// so the paint saves with the vehicle and every player sees it, modded or not. Light colours are drawn on each player's
// own game from that synced paint: players with AnyLights see coloured lights, players without it see stock lights.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (anylights_bindings.h): Anymaker 0.1.25 /
// Steam build 25904830 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyhelpers_settings_v1.h"
#include "anyapi_experimental.hpp"
#include "anylights_bindings.h"
#include "anylights_logic.h"
#include "vehicle_reads.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

namespace anylights {
namespace {
using namespace vehicle_reads;
constexpr const char* kModId = "anylights";

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
const AnyUiStateV1* ui;
const AnyHelpersSettingsV1* settings;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}

enum Setting { kPaintTool, kLights, kSettingCount };
uint64_t g_tokens[kSettingCount]{}, g_revision = UINT64_MAX;
std::atomic<bool> g_paint_enabled{true}, g_lights_enabled{true};
void refresh_settings() {
    if (!settings) return;
    uint64_t next = settings->revision();
    if (next == g_revision) return;
    g_revision = next;
    std::atomic<bool>* targets[kSettingCount] = {&g_paint_enabled, &g_lights_enabled};
    for (int i = 0; i < kSettingCount; ++i) { AnySettingValueV1 v; if (settings->get(g_tokens[i], &v)) targets[i]->store(v.number != 0); }
}

// Field offsets (reference layouts, metadata, Anymaker 0.1.25).
namespace off {
// client_scene.item_world.vehicle_editor_paint: slave properties, value 8 bytes in
constexpr ptrdiff_t paint_colour = 0x38 + 8;          // m_color (s32 palette index)
constexpr ptrdiff_t paint_mode = 0x48 + 8;            // m_mode (e_mode)
constexpr ptrdiff_t paint_hovered_vehicle = 0x68;     // _m_hovered_vehicle_id
// client_scene.vehicle_component.m_colors: array<replication.slave.property_s32>
constexpr ptrdiff_t component_colours = 0x30;         // buffer +0, count +8, element size +0xc (16), value +8 in each
// client_scene.vehicle_plate
constexpr ptrdiff_t plate_colour_front = 0xc0 + 8, plate_colour_back = 0xd0 + 8;
// light colour (color8 r,g,b,a), per kind
constexpr ptrdiff_t light_colour_spot = 0x150, light_colour_omni = 0x108, light_colour_rotate = 0x120;
constexpr ptrdiff_t client_peer_data = 40 + 104;      // client.m_peers.m_data
}  // namespace off

// ------------------------------------------------------------------ palette (copied on the game thread)
std::mutex g_mutex;                         // everything below that more than one thread touches
Rgb g_palette[kMaxPalette];                // written once, before g_palette_count is published
std::atomic<int> g_palette_count{0};
uint64_t g_palette_tried = 0;               // game thread only, like the lookup
int g_palette_misses = 0;
constexpr int kPaletteTries = 30;

bool read_palette() {
    auto* p = static_cast<const uint8_t*>(anymaker::experimental::global(bind::g_color_palette));
    if (!p || !anymaker::readable(p, sizeof(anymaker::gc_vector_raw))) return false;
    anymaker::gc_vector_raw v; std::memcpy(&v, p, sizeof v);
    if (v.count < 8 || v.count > kMaxPalette || v.element_size != 4 || !anymaker::readable(v.buffer, size_t(v.capacity) * 4)) return false;
    for (int i = 0; i < v.count; ++i) { auto* c = v.at<uint8_t[4]>(i); if (!c) return false; g_palette[i] = {(*c)[0], (*c)[1], (*c)[2]}; }
    g_palette_count = v.count;
    log(0, "palette: %d colours", v.count);
    return true;
}
// Called by the paint tool and by every light tick until it works, so lights glow without a paint tool held first.
// The lookup scans game code: at most every 2 s, and it gives up after a minute of misses.
bool copy_palette() {
    uint64_t now = GetTickCount64();
    if (g_palette_misses >= kPaletteTries || now - g_palette_tried < 2000) return false;
    g_palette_tried = now;
    if (read_palette()) return true;
    if (++g_palette_misses == kPaletteTries) log(2, "could not read the game's palette: lights stay stock and the paint panel stays closed");
    return false;
}

// Reads one component's paint slots. Returns how many were read.
int read_slots(const uint8_t* component, int32_t* out, int cap) {
    if (!anymaker::readable(component + off::component_colours, 16)) return 0;
    auto buffer = rd<const uint8_t*>(component, off::component_colours);
    int32_t count = rd<int32_t>(component, off::component_colours + 8), size = rd<int32_t>(component, off::component_colours + 12);
    if (!buffer || size != 16 || count <= 0 || count > 16 || !anymaker::readable(buffer, size_t(count) * 16)) return 0;
    count = std::min(count, cap);
    for (int i = 0; i < count; ++i) out[i] = rd<int32_t>(buffer, ptrdiff_t(i) * 16 + 8);
    return count;
}

// ------------------------------------------------------------------ the player's choice (panel <-> game thread)
int32_t g_choice_colour = -1;               // palette index, -1 = each tool's own
int32_t g_choice_mode = -1;                 // e_mode, -1 = each tool's own
int32_t g_item_colour = -1, g_item_mode = 0; // what the held tool had before AnyLights (for the panel)
std::atomic<uint64_t> g_paint_seen{0};       // last tick the player held a paint tool
std::atomic<int> g_undo_requests{0};
std::atomic<int> g_undo_done{-1};             // repaint count of the last undo, for the panel
std::wstring g_choice_file;

void save_choice() {
    if (g_choice_file.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_choice_file.c_str(), L"wb") || !f) return;
    fprintf(f, "%d %d\n", g_choice_colour, g_choice_mode);
    fclose(f);
}
void load_choice() {
    if (g_choice_file.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_choice_file.c_str(), L"rb") || !f) return;
    int c = -1, m = -1;
    if (fscanf_s(f, "%d %d", &c, &m) == 2) { g_choice_colour = std::clamp(c, -1, kMaxPalette - 1); g_choice_mode = m >= 0 && m < kModeCount ? m : -1; }
    fclose(f);
}

// ------------------------------------------------------------------ paint tool hook (game thread)
using paint_tick_t = void (*)(uint8_t* item, uint8_t* client, uint8_t* scene, void* actor, const void* transform, const bool* active,
                              const int32_t* location, const double* dt);
using push_fill_component_t = void (*)(uint8_t* peer_data, const int32_t* colour, const int32_t* vehicle, const int32_t* component, const int32_t* slot);
using push_fill_plate_t = void (*)(uint8_t* peer_data, const int32_t* colour, const int32_t* vehicle, const int32_t* plate, const bool* front);
anymaker::cell_hook h_paint_tick;
void** g_push_component_cell;
void** g_push_plate_cell;
UndoStack g_undo;                            // game thread only
const uint8_t* g_undo_scene;                 // the world g_undo belongs to (game thread only)
std::atomic<bool> g_left_world{false};       // main menu or a loading screen seen: the next world reuses vehicle ids
constexpr int32_t kUiMainMenu = 5, kUiLoadingScreen = 25;   // frontend_ui.e_ui_state
bool g_lmb_was_down;
std::atomic<bool> g_panel_shown{false};      // clicks on the panel are not paint clicks
std::atomic<int> g_paint_logs{4};

bool read_vehicle_paint(const uint8_t* scene, int32_t vehicle_id, Snapshot& out) {
    uint8_t* vehicle = find_vehicle(scene, vehicle_id);
    if (!vehicle) return false;
    out = {}; out.vehicle = vehicle_id;
    for_each_component(vehicle, [&](uint8_t* component) {
        int32_t slots[16];
        int n = read_slots(component, slots, 16);
        int32_t id = rd<int32_t>(component, vehicle_reads::off::component_id);
        for (int i = 0; i < n; ++i) out.slots.push_back({id, i, slots[i]});
    });
    for_each_ref(vehicle + vehicle_reads::off::vehicle_plates, 1 << 20, [&](uint8_t* plate) {
        if (!anymaker::readable(plate, off::plate_colour_back + 4)) return true;
        int32_t id = rd<int32_t>(plate, vehicle_reads::off::plate_id);
        out.plates.push_back({id, true, rd<int32_t>(plate, off::plate_colour_front)});
        out.plates.push_back({id, false, rd<int32_t>(plate, off::plate_colour_back)});
        return true;
    });
    return true;
}

void run_undo(uint8_t* client, const uint8_t* scene) {
    if (!client || !g_push_component_cell || !*g_push_component_cell || !g_push_plate_cell || !*g_push_plate_cell) return;
    Diff d = g_undo.pop_restore([&](int32_t vehicle, Snapshot& now) { return read_vehicle_paint(scene, vehicle, now); });
    uint8_t* peer = client + off::client_peer_data;
    if (d.empty()) { g_undo_done = 0; log(0, "undo: the last click changed nothing undo can restore"); return; }
    int32_t vehicle = d.vehicle;
    for (const auto& s : d.slots) {
        int32_t colour = s.colour, component = s.component, slot = s.slot;
        reinterpret_cast<push_fill_component_t>(*g_push_component_cell)(peer, &colour, &vehicle, &component, &slot);
    }
    for (const auto& p : d.plates) {
        int32_t colour = p.colour, plate = p.plate; bool front = p.front;
        reinterpret_cast<push_fill_plate_t>(*g_push_plate_cell)(peer, &colour, &vehicle, &plate, &front);
    }
    g_undo_done = int(d.slots.size() + d.plates.size());
    log(0, "undo: repainted %d part slots and %d plate sides on vehicle %d", int(d.slots.size()), int(d.plates.size()), vehicle);
}

void hk_paint_tick(uint8_t* item, uint8_t* client, uint8_t* scene, void* actor, const void* transform, const bool* active,
                   const int32_t* location, const double* dt) {
    if (g_ready && item && anymaker::readable(item, off::paint_hovered_vehicle + 4)) {
        refresh_settings();
        if (g_palette_count == 0) copy_palette();
        g_paint_seen = GetTickCount64();
        int32_t own_colour = rd<int32_t>(item, off::paint_colour), own_mode = rd<int32_t>(item, off::paint_mode);
        int32_t colour, mode;
        {
            std::lock_guard lock(g_mutex);
            colour = g_choice_colour; mode = g_choice_mode;
            if (own_colour != colour) g_item_colour = own_colour;
            if (own_mode != mode) g_item_mode = own_mode;
        }
        if (g_paint_enabled.load()) {
            // The tool re-reads these before every paint event it sends, so writing them is all it takes.
            if (colour >= 0 && colour < g_palette_count) wr<int32_t>(item, off::paint_colour, colour);
            if (mode >= 0 && mode < kModeCount) wr<int32_t>(item, off::paint_mode, mode);
            if (g_paint_logs.fetch_sub(1) > 0)
                log(0, "paint tool held: own colour %d mode %d, AnyLights colour %d mode %d", own_colour, own_mode, colour, mode);
            // Undo history belongs to one world: another world or server has the same vehicle and part ids.
            if (g_left_world.exchange(false) || scene != g_undo_scene) { g_undo.clear(); g_undo_scene = scene; }
            // Undo: remember the aimed-at vehicle's paint as the click starts, before the tool sends anything.
            bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (lmb && !g_lmb_was_down && !g_panel_shown.load()) {
                Snapshot s;
                int32_t vehicle = rd<int32_t>(item, off::paint_hovered_vehicle);
                if (vehicle > 0 && read_vehicle_paint(scene, vehicle, s)) { g_undo.push(std::move(s)); g_undo_done = -1; }
            }
            g_lmb_was_down = lmb;
            for (int n = g_undo_requests.exchange(0); n > 0; --n) run_undo(client, scene);
        }
    }
    reinterpret_cast<paint_tick_t>(h_paint_tick.original)(item, client, scene, actor, transform, active, location, dt);
}

// ------------------------------------------------------------------ light hooks (game thread)
enum Kind { kSpot, kOmni, kRotate, kKindCount };
const char* const kKindNames[kKindCount] = {"spot", "omni", "rotating"};
constexpr ptrdiff_t kLightColour[kKindCount] = {off::light_colour_spot, off::light_colour_omni, off::light_colour_rotate};
using light_tick_t = void (*)(uint8_t* light, void* client, uint8_t* vehicle, void* scene, const double* dt);
anymaker::cell_hook h_light_tick[kKindCount];
std::unordered_set<const uint8_t*> g_logged_lights;
std::atomic<int> g_light_logs{8};

void colour_light(int kind, uint8_t* light) {
    if (!light || !anymaker::readable(light, kLightColour[kind] + 4)) return;
    Rgb c = kStockLight;
    if (g_lights_enabled.load() && (g_palette_count > 0 || copy_palette())) {
        int32_t slots[16];
        int n = read_slots(light, slots, 16);
        int32_t index = light_paint_index(slots, n, g_palette_count);
        if (index >= 0) c = light_from_paint(g_palette[index]);
        if (n > 0 && g_light_logs.load() > 0 && g_logged_lights.insert(light).second && g_light_logs.fetch_sub(1) > 0)
            log(0, "%s light: %d paint slots, first %d %d %d, glows index %d", kKindNames[kind], n, slots[0], n > 1 ? slots[1] : -1, n > 2 ? slots[2] : -1, index);
    }
    uint32_t alpha = rd<uint32_t>(light, kLightColour[kind]) & 0xff000000u;
    wr<uint32_t>(light, kLightColour[kind], uint32_t(c.r) | uint32_t(c.g) << 8 | uint32_t(c.b) << 16 | alpha);
}
void light_tick(int kind, uint8_t* light, void* client, uint8_t* vehicle, void* scene, const double* dt) {
    if (g_ready) colour_light(kind, light);
    reinterpret_cast<light_tick_t>(h_light_tick[kind].original)(light, client, vehicle, scene, dt);
}
void hk_spot(uint8_t* l, void* c, uint8_t* v, void* s, const double* d) { light_tick(kSpot, l, c, v, s, d); }
void hk_omni(uint8_t* l, void* c, uint8_t* v, void* s, const double* d) { light_tick(kOmni, l, c, v, s, d); }
void hk_rotate(uint8_t* l, void* c, uint8_t* v, void* s, const double* d) { light_tick(kRotate, l, c, v, s, d); }

// ================================================================== install (background thread)
void* const kLightHooks[kKindCount] = {(void*)&hk_spot, (void*)&hk_omni, (void*)&hk_rotate};
void install_hooks() {
    using namespace anymaker::experimental;
    const anymaker::func_desc* light_ticks[kKindCount] = {&bind::client_spot_tick, &bind::client_omni_tick, &bind::client_rotate_tick};
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyLights stays off"); return; }
        void** paint = hook_cell(bind::paint_tick_active);
        void** push_component = hook_cell(bind::push_fill_component);
        void** push_plate = hook_cell(bind::push_fill_plate);
        void** lights[kKindCount];
        bool all = paint && push_component && push_plate;
        for (int k = 0; k < kKindCount; ++k) { lights[k] = hook_cell(*light_ticks[k]); all = all && lights[k]; }
        if (!all) {
            if (attempt == 179)
                log(2, "could not locate the game functions (paint tool %d, fill part %d, fill plate %d, lights %d/%d/%d)", !!paint, !!push_component,
                    !!push_plate, !!lights[kSpot], !!lights[kOmni], !!lights[kRotate]);
            continue;
        }
        g_push_component_cell = push_component; g_push_plate_cell = push_plate;
        bool ok = h_paint_tick.install(paint, (void*)&hk_paint_tick);
        for (int k = 0; k < kKindCount && ok; ++k) ok = h_light_tick[k].install(lights[k], kLightHooks[k]);
        if (!ok) { log(2, "a hook cell was already taken or not writable; AnyLights is off"); return; }
        g_ready = true;
        log(0, "Ready after %d s: hold a paint tool and right click for the palette. Ctrl+Z undoes.", attempt);
        return;
    }
}
void remove_hooks() {
    for (int k = kKindCount - 1; k >= 0; --k) h_light_tick[k].remove(kLightHooks[k]);
    h_paint_tick.remove((void*)&hk_paint_tick);
}

// ================================================================== panel (render / input threads, under g_mutex)
struct Panel {
    bool open = false, focused = false;
    float frame_w = 0, frame_h = 0;          // renderer pixels
    float mouse_x = -1, mouse_y = -1;        // renderer pixels
    int hover = -1, hover_mode = -1;
    // last layout
    float x = 0, y = 0, w = 0, h = 0, s = 1, grid_x = 0, grid_y = 0, cell = 0, gap = 0, mode_y = 0, mode_w = 0, mode_h = 0;
} g_panel;
constexpr int kColumns = 12;
const wchar_t* const kModeNames[kModeCount] = {L"Fill", L"Brush", L"Pencil", L"Erase brush", L"Erase pencil", L"Flood"};
const wchar_t* const kModeHints[kModeCount] = {L"Click a part, plate side, edge or link to paint it", L"Hold and drag to paint plate pixels",
    L"Hold and drag to paint single plate pixels", L"Hold and drag to clear painted pixels", L"Hold and drag to clear single pixels",
    L"Click to repaint everything connected in the same colour"};

bool holding_paint() { return GetTickCount64() - g_paint_seen.load() < 300; }
bool gameplay() { AnyUiSnapshotV1 s; return !ui || (ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY); }
uint32_t argb(Rgb c) { return 0xff000000u | uint32_t(c.r) << 16 | uint32_t(c.g) << 8 | c.b; }

void layout() {
    Panel& p = g_panel;
    p.s = std::clamp(p.frame_h / 1080.f, .7f, 1.8f);
    float s = p.s;
    p.cell = 26 * s; p.gap = 4 * s;
    p.w = kColumns * (p.cell + p.gap) - p.gap + 32 * s;
    int rows = (std::max(g_palette_count.load(), 1) + kColumns - 1) / kColumns;
    p.grid_x = 16 * s; p.grid_y = 112 * s;
    p.mode_y = p.grid_y + rows * (p.cell + p.gap) + 12 * s;
    p.mode_h = 28 * s; p.mode_w = (p.w - 32 * s - 2 * 6 * s) / 3;
    p.h = p.mode_y + 2 * p.mode_h + 6 * s + 56 * s;
    p.x = std::max(0.f, (p.frame_w - p.w) * .5f);
    p.y = std::max(8 * s, (p.frame_h - p.h) * .5f);
}

void hit_test() {
    Panel& p = g_panel;
    p.hover = grid_hit(p.mouse_x - p.x - p.grid_x, p.mouse_y - p.y - p.grid_y, p.cell, p.gap, kColumns, g_palette_count);
    p.hover_mode = -1;
    float mx = p.mouse_x - p.x - 16 * p.s, my = p.mouse_y - p.y - p.mode_y;
    if (mx >= 0 && my >= 0) {
        int col = int(mx / (p.mode_w + 6 * p.s)), row = int(my / (p.mode_h + 6 * p.s));
        if (col < 3 && row < 2 && mx - col * (p.mode_w + 6 * p.s) <= p.mode_w && my - row * (p.mode_h + 6 * p.s) <= p.mode_h) p.hover_mode = row * 3 + col;
    }
}

void close_panel() {
    g_panel.open = false;
    g_panel_shown = false;
    if (host.capture_input) host.capture_input(0);
}

void frame(const AnyFrameV1* f, AnyCanvasV1*, void*) {
    if (!f) return;
    refresh_settings();
    AnyUiSnapshotV1 state;
    if (ui && ui->copy(&state) && (state.native_state == kUiMainMenu || state.native_state == kUiLoadingScreen)) g_left_world = true;
    std::lock_guard lock(g_mutex);
    g_panel.focused = f->focused != 0;
    g_panel.frame_w = float(f->width); g_panel.frame_h = float(f->height);
    if (g_panel.open && (!g_ready || !g_panel.focused || !g_paint_enabled.load() || !holding_paint() || !gameplay())) close_panel();
    if (g_panel.open && host.capture_input) host.capture_input(1);
}

void draw(const AnyFrameV1* frame, void*) {
    if (!frame || !gpu) return;
    std::lock_guard lock(g_mutex);
    Panel& p = g_panel;
    if (!p.open || g_palette_count == 0) return;
    p.frame_w = float(frame->width); p.frame_h = float(frame->height);
    layout();
    hit_test();
    float s = p.s, x = p.x, y = p.y;
    auto rect = [&](uint32_t kind, float rx, float ry, float rw, float rh, uint32_t color, float radius = 0, uint32_t flags = 0, float stroke = 1, float opacity = 1) {
        AnyGpuCommandV1 c; c.kind = kind; c.rect[0] = rx; c.rect[1] = ry; c.rect[2] = rw; c.rect[3] = rh; c.color = color; c.radius = radius; c.flags = flags;
        c.stroke = stroke; c.opacity = opacity; gpu->emit(&c); };
    auto text = [&](float tx, float ty, float tw, float th, const wchar_t* t, float size, uint32_t color, uint32_t flags = 0) {
        AnyGpuCommandV1 c; c.kind = ANY_GPU_TEXT; c.rect[0] = tx; c.rect[1] = ty; c.rect[2] = tw; c.rect[3] = th; c.text = t; c.text_length = uint32_t(wcslen(t));
        c.font_size = size; c.color = color; c.flags = flags | ANY_GPU_NOWRAP | ANY_GPU_VCENTER; gpu->emit(&c); };
    int selected = g_choice_colour >= 0 && g_choice_colour < g_palette_count ? g_choice_colour : g_item_colour;
    int mode = g_choice_mode >= 0 ? g_choice_mode : g_item_mode;
    int shown = p.hover >= 0 ? p.hover : selected;
    wchar_t buf[160];

    rect(ANY_GPU_ROUND_RECT, x, y, p.w, p.h, 0xf2191c20, 8 * s);
    text(x + 16 * s, y + 8 * s, p.w - 32 * s, 18 * s, L"PAINT", 11 * s, 0xffaeb7c2, ANY_GPU_BOLD);
    // Preview: a big swatch of the colour and the glow a light painted with it gets.
    if (shown >= 0 && shown < g_palette_count) {
        Rgb c = g_palette[shown], glow = light_from_paint(c);
        rect(ANY_GPU_ROUND_RECT, x + 16 * s, y + 30 * s, 150 * s, 68 * s, argb(c), 6 * s);
        rect(ANY_GPU_ROUND_RECT, x + 16 * s, y + 30 * s, 150 * s, 68 * s, 0x40ffffff, 6 * s, ANY_GPU_STROKE, 1 * s);
        swprintf_s(buf, L"%ls %d", p.hover >= 0 && p.hover != selected ? L"Colour" : L"Painting with colour", shown + 1);
        text(x + 180 * s, y + 30 * s, p.w - 260 * s, 24 * s, buf, 16 * s, 0xfff5f7fa, ANY_GPU_BOLD);
        swprintf_s(buf, L"#%02X%02X%02X", c.r, c.g, c.b);
        text(x + 180 * s, y + 54 * s, 120 * s, 18 * s, buf, 12 * s, 0xffaeb7c2);
        text(x + 180 * s, y + 74 * s, p.w - 260 * s, 18 * s, L"Lights painted this colour glow:", 11 * s, 0xff929fab);
        float gx = x + p.w - 66 * s, gy = y + 34 * s;
        rect(ANY_GPU_ELLIPSE, gx - 6 * s, gy - 6 * s, 62 * s, 62 * s, argb(glow), 0, 0, 1, .18f);
        rect(ANY_GPU_ELLIPSE, gx + 6 * s, gy + 6 * s, 38 * s, 38 * s, argb(glow), 0, 0, 1, .45f);
        rect(ANY_GPU_ELLIPSE, gx + 15 * s, gy + 15 * s, 20 * s, 20 * s, 0xffffffff, 0, 0, 1, .9f);
        rect(ANY_GPU_ELLIPSE, gx + 15 * s, gy + 15 * s, 20 * s, 20 * s, argb(glow), 0, 0, 1, .7f);
    }
    // Palette grid, in the game's palette order.
    for (int i = 0; i < g_palette_count; ++i) {
        float cx = x + p.grid_x + float(i % kColumns) * (p.cell + p.gap), cy = y + p.grid_y + float(i / kColumns) * (p.cell + p.gap);
        bool hot = i == p.hover;
        float grow = hot ? 3 * s : 0;
        rect(ANY_GPU_ROUND_RECT, cx - grow, cy - grow, p.cell + 2 * grow, p.cell + 2 * grow, argb(g_palette[i]), 4 * s);
        if (i == selected) {
            rect(ANY_GPU_ROUND_RECT, cx - 3 * s, cy - 3 * s, p.cell + 6 * s, p.cell + 6 * s, 0xffffffff, 6 * s, ANY_GPU_STROKE, 2 * s);
            rect(ANY_GPU_ROUND_RECT, cx - 1 * s, cy - 1 * s, p.cell + 2 * s, p.cell + 2 * s, 0xff191c20, 5 * s, ANY_GPU_STROKE, 1 * s);
        } else if (hot) rect(ANY_GPU_ROUND_RECT, cx - grow, cy - grow, p.cell + 2 * grow, p.cell + 2 * grow, 0xc0ffffff, 5 * s, ANY_GPU_STROKE, 1.5f * s);
    }
    // Modes
    for (int i = 0; i < kModeCount; ++i) {
        Mode m = kModeOrder[i];
        float bx = x + 16 * s + float(i % 3) * (p.mode_w + 6 * s), by = y + p.mode_y + float(i / 3) * (p.mode_h + 6 * s);
        bool on = mode == m, hot = p.hover_mode == i;
        rect(ANY_GPU_ROUND_RECT, bx, by, p.mode_w, p.mode_h, on ? 0xffedc56c : hot ? 0xff4a525c : 0xff3a4048, 4 * s);
        text(bx, by, p.mode_w, p.mode_h, kModeNames[m], 12 * s, on ? 0xff191c20 : 0xffdfe4ea, ANY_GPU_CENTER | (on ? ANY_GPU_BOLD : 0));
    }
    float fy = y + p.mode_y + 2 * p.mode_h + 6 * s + 10 * s;
    int hint_mode = p.hover_mode >= 0 ? kModeOrder[p.hover_mode] : mode;
    if (hint_mode >= 0 && hint_mode < kModeCount) text(x + 16 * s, fy, p.w - 32 * s, 18 * s, kModeHints[hint_mode], 11 * s, 0xffdfe4ea);
    int undone = g_undo_done.load();
    text(x + 16 * s, fy + 20 * s, p.w - 32 * s, 18 * s,
         undone == 0 ? L"Nothing to undo for that click · Right click or Esc closes" : L"Ctrl+Z undoes your last click · Right click or Esc closes", 11 * s, 0xff929fab);
}

void to_renderer(const AnyInputV1* e) {
    // Window-client pixels to renderer pixels, for non-native resolutions.
    RECT r{}; HWND window = GetForegroundWindow(); DWORD pid{};
    if (window) GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() && GetClientRect(window, &r) && r.right > 0 && r.bottom > 0 && g_panel.frame_w > 0) {
        g_panel.mouse_x = float(e->x) * g_panel.frame_w / float(r.right); g_panel.mouse_y = float(e->y) * g_panel.frame_h / float(r.bottom);
    } else { g_panel.mouse_x = float(e->x); g_panel.mouse_y = float(e->y); }
}

uint32_t input(const AnyInputV1* e, void*) {
    if (!e) return 0;
    std::lock_guard lock(g_mutex);
    Panel& p = g_panel;
    if (e->kind == ANY_FOCUS_LOST) { if (p.open) close_panel(); return 0; }
    if (!g_ready || !g_paint_enabled.load() || !holding_paint() || !gameplay()) { if (p.open) close_panel(); return 0; }
    // Ctrl+Z undo while holding a paint tool.
    if (e->kind == ANY_KEY_DOWN && e->key == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) { ++g_undo_requests; return 1; }
    if (!p.open) {
        // Only when the panel can be drawn: an invisible panel would still hold the keyboard and mouse.
        if (e->kind == ANY_MOUSE_DOWN && e->button == 2 && gpu && g_palette_count > 0) {
            p.open = true; p.hover = p.hover_mode = -1; g_panel_shown = true;
            if (host.capture_input) host.capture_input(1);
            return 1;
        }
        return e->kind == ANY_MOUSE_UP && e->button == 2;   // swallow the release of the click that closed the panel
    }
    if (e->kind == ANY_KEY_DOWN && e->key == VK_ESCAPE) { close_panel(); return 1; }
    if (e->kind == ANY_MOUSE_MOVE) { to_renderer(e); return 1; }
    if (e->kind == ANY_MOUSE_DOWN && e->button == 2) { close_panel(); return 1; }
    if (e->kind == ANY_MOUSE_DOWN && e->button == 1) {
        to_renderer(e);
        layout(); hit_test();
        if (p.hover >= 0) { g_choice_colour = p.hover; g_undo_done = -1; save_choice(); }
        else if (p.hover_mode >= 0) { g_choice_mode = kModeOrder[p.hover_mode]; save_choice(); }
        else if (p.mouse_x < p.x || p.mouse_x > p.x + p.w || p.mouse_y < p.y || p.mouse_y > p.y + p.h) close_panel();
        return 1;
    }
    return 1;   // the panel owns the keyboard and mouse while open
}
}  // namespace
}  // namespace anylights

using namespace anylights;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId; out->input = input; out->render = frame;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    if (host.plugin_directory) { g_choice_file = host.plugin_directory; g_choice_file += L"\\anylights_paint.txt"; load_choice(); }
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services(); if (!services) return;
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    ui = static_cast<const AnyUiStateV1*>(services->query("anyapi.ui_state", 1));
    if (ui && (ui->struct_size != sizeof(*ui) || ui->version != 1 || !ui->copy)) ui = nullptr;
    if (!gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer || !gpu->register_renderer(draw, nullptr)) {
        gpu = nullptr; log(1, "GPU drawing unavailable: the paint panel is hidden; your last colour and coloured lights still work");
    }
    settings = static_cast<const AnyHelpersSettingsV1*>(services->query("anyhelpers.settings", 1));
    if (settings && settings->struct_size == sizeof(*settings) && settings->version == 1 && settings->register_setting && settings->get && settings->revision) {
        const char* ids[] = {"paint_tool", "lights"};
        const char* labels[] = {"One paint tool for every colour", "Lights glow their paint colour"};
        const char* descriptions[] = {"Hold any paint tool and right click for the palette and modes. Ctrl+Z undoes.",
            "Spot, omni and rotating lights take the colour they are painted. Unpainted lights stay stock."};
        for (int i = 0; i < kSettingCount; ++i) {
            AnyModSettingV1 d; d.mod_id = kModId; d.mod_name = "AnyLights"; d.setting_id = ids[i]; d.label = labels[i]; d.order = i; d.kind = ANY_SETTING_BOOL;
            d.default_number = 1; d.minimum = 0; d.maximum = 1; d.description = descriptions[i];
            g_tokens[i] = settings->register_setting(&d);
        }
    } else settings = nullptr;
    std::thread(install_hooks).detach();
}
