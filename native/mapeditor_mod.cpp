// MapEditor: opens Anymaker's own built-in world editor in game, and keeps custom maps as map packs.
//
// How: the game ships its developers' world editor (terrain chunks, junctions and roads, props with a fence mode,
// loot, enemy spawns, dead drops, prefabs) but nothing calls its entry point, update_ui() in main.gc, in the release
// build. Its windows are still drawn by the renderer and fed input. This mod hooks client_scene.update_ui (the in-world
// UI pass, main thread) and, while the editor is switched on, calls update_ui() after it, which draws the editor's menu
// bar; the editor windows open from that menu. While the editor is on, the mouse cursor is released unless the right
// mouse button is held (hooking frontend_ui.get_is_mouse_capture_requested).
//
// Map packs: the world lives in rom/data/tiles/0/tile_<x>_<z>_<t>.json. When a map pack is chosen (setting "Map
// pack"), every game read, write, existence check and folder listing under rom data/tiles/ is redirected to
// <game>/AnyAPI and Modding/maps/<pack>/tiles/ by hooking the file.* natives' call cells. A new pack starts as a copy
// of the stock tiles. With no pack chosen the editor stays closed and the stock tiles are never written.
// Map changes apply to new worlds; an existing save keeps the props it already stored.
//
// Controls: F7 toggles the editor in a world (rebindable in Controls under MapEditor when AnyHelpers is installed).
//
// EXPERIMENTAL and diagnostic. Anymaker 0.1.24 / Steam build 25826614 only (mapeditor_bindings.h). The first runs
// log the file paths the game uses and which path store reads absolute paths; read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyhelpers_settings_v1.h"
#include "mod_controls_v1.h"
#include "anyapi_experimental.hpp"
#include "mapeditor_bindings.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace mapeditor {
namespace {
namespace fs = std::filesystem;
constexpr const char* kModId = "mapeditor";

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
const AnyUiStateV1* ui;
const AnyHelpersSettingsV1* settings;
const ModControlsV1* controls;
uint64_t g_pack_token, g_action, g_settings_revision = ~0ull;
std::atomic<bool> g_ready{false}, g_stop{false}, g_editor{false}, g_ui_failed{false};
std::mutex g_input_mutex;
bool g_key_held;
std::atomic<ULONGLONG> g_toast_until{0};
wchar_t g_toast[160];

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
void toast(const wchar_t* fmt, ...) {
    va_list ap; va_start(ap, fmt); vswprintf(g_toast, 160, fmt, ap); va_end(ap);
    g_toast_until = GetTickCount64() + 2600;
}
bool gameplay() { AnyUiSnapshotV1 s; return ui && ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY; }

// ------------------------------------------------------------------ game types (0.1.24 reference layouts)
struct gstring { const char* p; int32_t len; int32_t reserved; };      // string, 16 bytes
struct gpath { int32_t store; int32_t pad; gstring path; };            // file.path, 24 bytes; store: 0 embedded, 1 system, 2 rom, 3 user
static_assert(sizeof(gstring) == 16 && sizeof(gpath) == 24);
constexpr int32_t kStoreRom = 2;

// ------------------------------------------------------------------ map pack state
std::mutex g_pack_mutex;
std::string g_pack_name;              // chosen in settings ("" = stock world)
std::string g_pack_tiles;             // "<maps>/<pack>/tiles" with forward slashes, once ready; "" = no redirect
std::atomic<int32_t> g_abs_store{-1}; // store that reads absolute paths, found by calibration
std::atomic<bool> g_copying{false};

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}
fs::path game_dir() { return fs::path(host.game_directory ? host.game_directory : L"."); }
fs::path maps_dir() { return game_dir() / L"AnyAPI and Modding" / L"maps"; }
std::string safe_name(const char* text) {
    std::string out;
    for (const char* c = text; *c && out.size() < 64; ++c)
        out += (std::isalnum(uint8_t(*c)) || *c == '-' || *c == '_' || *c == ' ') ? *c : '_';
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

// A new pack is a full copy of the stock tiles, made once in the background.
void prepare_pack(std::string name) {
    if (name.empty()) { std::lock_guard l(g_pack_mutex); g_pack_tiles.clear(); return; }
    g_copying = true;
    std::error_code ec;
    fs::path pack = maps_dir() / wide(name), tiles = pack / L"tiles", stock = game_dir() / L"rom" / L"data" / L"tiles";
    if (!fs::exists(tiles / L"0", ec)) {
        log(0, "creating map pack '%s' from the stock tiles", name.c_str());
        fs::create_directories(tiles, ec);
        fs::copy(stock, tiles, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
        if (ec) { log(2, "could not copy the stock tiles into the map pack: %s", ec.message().c_str()); g_copying = false; return; }
        if (FILE* f = _wfopen((pack / L"map.json").c_str(), L"wb")) {
            fprintf(f, "{\n  \"format\": 1,\n  \"name\": \"%s\",\n  \"base\": \"stock\",\n  \"game_build\": \"25826614\"\n}\n", name.c_str());
            fclose(f);
        }
    }
    size_t count = 0;
    for (auto it = fs::directory_iterator(tiles / L"0", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) ++count;
    std::string root = utf8(tiles.wstring());
    std::replace(root.begin(), root.end(), '\\', '/');
    { std::lock_guard l(g_pack_mutex); if (g_pack_name == name) g_pack_tiles = root; }
    g_copying = false;
    log(0, "map pack '%s' ready: %zu tile files in %s", name.c_str(), count, root.c_str());
}

// rom "data/tiles/..." -> absolute pack path. Returns false when the path is not a tile path or no pack is active.
bool is_tile_path(const gpath* p, std::string* rest) {
    if (!p || p->store != kStoreRom || !p->path.p || p->path.len < 11) return false;
    std::string s(p->path.p, size_t(p->path.len));
    std::replace(s.begin(), s.end(), '\\', '/');
    size_t start = s.rfind("data/tiles", std::string::npos);
    if (start == std::string::npos || (start > 0 && s[start - 1] != '/')) return false;
    if (s.size() > start + 10 && s[start + 10] != '/') return false;   // "data/tiles_foo" is not ours
    if (rest) *rest = s.substr(start + 10);                            // "" or "/0/tile_1_2_0.json"
    return true;
}
struct redirect {
    gpath path{}; std::string text; bool active = false;
    explicit redirect(const gpath* p) {
        std::string rest;
        if (!is_tile_path(p, &rest)) return;
        int32_t store = g_abs_store.load();
        if (store < 0) return;
        { std::lock_guard l(g_pack_mutex); if (g_pack_tiles.empty()) return; text = g_pack_tiles + rest; }
        path = *p; path.store = store; path.path.p = text.c_str(); path.path.len = int32_t(text.size()); path.path.reserved = 0;
        active = true;
    }
    const gpath* use(const gpath* original) const { return active ? &path : original; }
};

// Diagnostics: the first distinct rom folders the game touches.
std::mutex g_seen_mutex;
std::set<std::string> g_seen;
void note_path(const char* what, const gpath* p) {
    if (!p || !p->path.p || p->path.len <= 0) return;
    std::string s(p->path.p, size_t(std::min(p->path.len, 300)));
    size_t slash = s.find_last_of("/\\");
    std::string key = std::to_string(p->store) + ":" + (slash == std::string::npos ? s : s.substr(0, slash + 1));
    std::lock_guard l(g_seen_mutex);
    if (g_seen.size() >= 60 || !g_seen.insert(key).second) return;
    log(0, "%s store %d: %s", what, p->store, s.c_str());
}

// ------------------------------------------------------------------ file native hooks (any thread)
anymaker::cell_hook h_read_string, h_read_binary, h_write_string, h_exists, h_files, h_folders, h_tile_save, h_scene_ui, h_capture;
using read_t = void (*)(bool* ret, const gpath* path, void* out);
using exists_t = void (*)(bool* ret, const gpath* path);
using list_t = void (*)(const gpath* path, void* files);
using list2_t = void (*)(const gpath* path, void* files, void* folders);

void hk_read_string(bool* ret, const gpath* p, void* out) {
    note_path("read", p);
    redirect r(p); auto original = read_t(h_read_string.original);
    original(ret, r.use(p), out);
}
void hk_read_binary(bool* ret, const gpath* p, void* out) {
    note_path("read binary", p);
    redirect r(p); auto original = read_t(h_read_binary.original);
    original(ret, r.use(p), out);
}
void hk_write_string(bool* ret, const gpath* p, void* text) {
    note_path("write", p);
    redirect r(p); auto original = read_t(h_write_string.original);
    if (g_editor && p && p->store == kStoreRom && !is_tile_path(p, nullptr)) {
        // The editor's other panels can save game definition files; keep the install untouched.
        log(1, "blocked an editor write to game data (%.*s)", p->path.len, p->path.p);
        *ret = false; return;
    }
    if (!r.active && is_tile_path(p, nullptr)) {
        // Never write over the stock world. The editor only opens with a pack, so this is a safety net.
        log(1, "blocked a write to the stock tiles (%.*s): choose a map pack first", p->path.len, p->path.p);
        *ret = false; return;
    }
    original(ret, r.use(p), text);
    if (r.active) log(0, "saved %s (%s)", r.text.c_str(), *ret ? "ok" : "FAILED");
}
void hk_exists(bool* ret, const gpath* p) {
    redirect r(p); auto original = exists_t(h_exists.original);
    original(ret, r.use(p));
}
void hk_files(const gpath* p, void* files) {
    note_path("list", p);
    redirect r(p); auto original = list_t(h_files.original);
    original(r.use(p), files);
}
void hk_folders(const gpath* p, void* files, void* folders) {
    note_path("list", p);
    redirect r(p); auto original = list2_t(h_folders.original);
    original(r.use(p), files, folders);
}
using tile_save_t = void (*)(uint8_t* tile);
void hk_tile_save(uint8_t* tile) {
    int32_t g[3]{}; if (tile) std::memcpy(g, tile + 368, sizeof g);   // client_scene.environment_tile.m_grid_position
    log(0, "editor saved tile grid %d %d %d", g[0], g[1], g[2]);
    tile_save_t(h_tile_save.original)(tile);
}

// ------------------------------------------------------------------ editor UI (main thread)
using scene_ui_t = void (*)(void* scene, void* frontend, void* settings, const double* dt, void* client);
using update_ui_t = void (*)();
update_ui_t g_update_ui;

// SEH needs a function without C++ objects to unwind.
bool call_update_ui_guarded() {
    __try { g_update_ui(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void hk_scene_ui(void* scene, void* frontend, void* settings_, const double* dt, void* client) {
    scene_ui_t(h_scene_ui.original)(scene, frontend, settings_, dt, client);
    if (!g_editor || !g_update_ui || g_ui_failed) return;
    if (!call_update_ui_guarded()) {
        g_ui_failed = true; g_editor = false;
        log(2, "the game's editor UI crashed when called; MapEditor switched it off for this session");
        toast(L"Map editor failed to open (see the log)");
    }
}
using capture_t = void (*)(bool* ret, void* frontend);
void hk_capture(bool* ret, void* frontend) {
    capture_t(h_capture.original)(ret, frontend);
    if (g_editor && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) *ret = false;   // cursor free, hold right mouse to look
}

// ------------------------------------------------------------------ settings, input, toast
void read_settings() {
    if (!settings || !g_pack_token) return;
    uint64_t revision = settings->revision();
    if (revision == g_settings_revision) return;
    g_settings_revision = revision;
    AnySettingValueV1 v;
    if (!settings->get(g_pack_token, &v)) return;
    std::string name = safe_name(v.text);
    { std::lock_guard l(g_pack_mutex); if (name == g_pack_name) return; g_pack_name = name; g_pack_tiles.clear(); }
    if (name.empty()) { g_editor = false; log(0, "map pack cleared: the stock world is used"); return; }
    std::thread(prepare_pack, name).detach();
}

uint32_t toggle_key() {
    uint32_t k = controls && g_action ? controls->key(g_action) : 0;
    return k ? k : (controls && g_action ? 0 : VK_F7);
}

void toggle_editor() {
    if (g_ui_failed) { toast(L"Map editor is off after an error (see the log)"); return; }
    if (g_editor) { g_editor = false; toast(L"Map editor closed"); log(0, "editor closed"); return; }
    std::string name, tiles;
    { std::lock_guard l(g_pack_mutex); name = g_pack_name; tiles = g_pack_tiles; }
    if (name.empty()) { toast(L"Choose a map pack name in Settings > Mods > MapEditor first"); return; }
    if (g_copying || tiles.empty()) { toast(L"Still preparing the map pack, try again in a moment"); return; }
    g_editor = true;
    toast(L"Map editor: %ls  (hold right mouse to look around)", wide(name).c_str());
    log(0, "editor opened on map pack '%s'", name.c_str());
}

uint32_t input(const AnyInputV1* e, void*) {
    if (!e) return 0;
    std::lock_guard lock(g_input_mutex);
    if (e->kind == ANY_FOCUS_LOST) { g_key_held = false; return 0; }
    if (!g_ready) return 0;
    uint32_t key = toggle_key();
    if (e->kind == ANY_KEY_UP && key && e->key == key) { bool held = g_key_held; g_key_held = false; return held; }
    if (e->kind == ANY_KEY_DOWN && key && e->key == key) {
        if (!gameplay()) return 0;
        if (!g_key_held) { g_key_held = true; toggle_editor(); }
        return 1;
    }
    return 0;
}

void draw(const AnyFrameV1* f, void*) {
    read_settings();
    if (!f || !f->focused || !gpu) return;
    ULONGLONG now = GetTickCount64(), until = g_toast_until.load();
    if (now >= until) return;
    float fade = std::min(1.f, float(until - now) / 300.f), s = std::max(1.f, float(f->height) / 1080.f);
    int n = int(wcsnlen(g_toast, 160));
    float w = std::min(float(f->width) - 40, (40 + 8.5f * float(n)) * s), h = 34 * s, x = (float(f->width) - w) / 2, y = float(f->height) * 0.82f;
    AnyGpuCommandV1 bg; bg.kind = ANY_GPU_ROUND_RECT; bg.rect[0] = x; bg.rect[1] = y; bg.rect[2] = w; bg.rect[3] = h;
    bg.radius = 6 * s; bg.color = 0xff191c20; bg.opacity = 0.85f * fade; gpu->emit(&bg);
    AnyGpuCommandV1 t; t.kind = ANY_GPU_TEXT; t.rect[0] = x; t.rect[1] = y; t.rect[2] = w; t.rect[3] = h;
    t.font_size = 15 * s; t.color = 0xfff5f7fa; t.opacity = fade; t.text = g_toast; t.text_length = uint32_t(n);
    t.flags = ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP | ANY_GPU_BOLD; gpu->emit(&t);
}

// ------------------------------------------------------------------ install (background thread)
// Which path store opens an absolute path? Ask the game about a file we know exists.
void calibrate_store(exists_t exists) {
    std::string exe = utf8((game_dir() / L"game.exe").wstring());
    std::replace(exe.begin(), exe.end(), '\\', '/');
    for (int32_t store : {1, 3, 0, 2}) {
        gpath p{}; p.store = store; p.path = {exe.c_str(), int32_t(exe.size()), 0};
        bool ok = false; exists(&ok, &p);
        log(0, "absolute path check: store %d -> %s", store, ok ? "found" : "not found");
        if (ok) { g_abs_store = store; log(0, "absolute paths use path store %d", store); return; }
    }
    log(2, "no path store opened an absolute path; map packs cannot be loaded (the editor stays closed)");
}

bool hook(anymaker::cell_hook& h, const anymaker::func_desc& d, void* fn, const char* name) {
    void** cell = anymaker::experimental::hook_cell(d);
    if (!cell) { log(2, "could not locate %s", name); return false; }
    if (!h.install(cell, fn)) { log(2, "the %s hook cell was already taken or not writable", name); return false; }
    return true;
}

void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; MapEditor stays off"); return; }
        if (!hook_cell(bind::read_string)) { if (attempt == 179) log(2, "game functions not found; MapEditor is off"); continue; }
        // File redirection first: it must be in place before any world loads tiles.
        bool files = hook(h_read_string, bind::read_string, (void*)&hk_read_string, "file.read_string") &&
                     hook(h_exists, bind::is_exists, (void*)&hk_exists, "file.get_is_exists") &&
                     hook(h_write_string, bind::write_string, (void*)&hk_write_string, "file.write_string");
        hook(h_read_binary, bind::read_binary, (void*)&hk_read_binary, "file.read_binary");
        hook(h_files, bind::files_in_dir, (void*)&hk_files, "file.get_files_in_dir");
        hook(h_folders, bind::folders_in_dir, (void*)&hk_folders, "file.get_files_folders_in_dir");
        if (files) calibrate_store(exists_t(h_exists.original));
        else log(2, "file hooks incomplete: map packs are off");
        hook(h_tile_save, bind::tile_save, (void*)&hk_tile_save, "client_scene.environment_tile.save");
        g_update_ui = update_ui_t(function(bind::update_ui));
        if (!g_update_ui) log(2, "could not locate update_ui (the editor's entry point)");
        bool ui_ok = hook(h_scene_ui, bind::client_scene_update_ui, (void*)&hk_scene_ui, "client_scene.update_ui");
        hook(h_capture, bind::mouse_capture, (void*)&hk_capture, "frontend_ui.get_is_mouse_capture_requested");
        g_ready = files && ui_ok && g_update_ui && g_abs_store >= 0;
        log(g_ready ? 0 : 2, g_ready ? "Ready after %d s. Choose a map pack in Settings, enter a world and press F7."
                                     : "Not ready after %d s; see the errors above", attempt);
        return;
    }
}
}  // namespace
}  // namespace mapeditor

using namespace mapeditor;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId; out->input = input;
    out->shutdown = [](void*) {
        g_stop = true; g_editor = false;
        h_scene_ui.remove((void*)&hk_scene_ui); h_capture.remove((void*)&hk_capture); h_tile_save.remove((void*)&hk_tile_save);
        h_read_string.remove((void*)&hk_read_string); h_read_binary.remove((void*)&hk_read_binary);
        h_write_string.remove((void*)&hk_write_string); h_exists.remove((void*)&hk_exists);
        h_files.remove((void*)&hk_files); h_folders.remove((void*)&hk_folders);
    };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services(); if (!services) return;
    ui = static_cast<const AnyUiStateV1*>(services->query("anyapi.ui_state", 1));
    if (!ui || ui->struct_size != sizeof(*ui) || ui->version != 1 || !ui->copy) { ui = nullptr; log(2, "ui_state unavailable; MapEditor is off"); return; }
    controls = static_cast<const ModControlsV1*>(services->query("anyhelpers.controls", 1));
    if (controls && controls->struct_size == sizeof(*controls) && controls->version == 1 && controls->register_action && controls->key) {
        ModControlActionV1 d; d.mod_id = kModId; d.mod_name = "MapEditor"; d.action_id = "toggle_editor";
        d.label = "Open / close the map editor"; d.default_key = VK_F7;
        g_action = controls->register_action(&d);
    } else controls = nullptr;
    settings = static_cast<const AnyHelpersSettingsV1*>(services->query("anyhelpers.settings", 1));
    if (settings && settings->struct_size == sizeof(*settings) && settings->version == 1 && settings->register_setting && settings->get && settings->revision) {
        AnyModSettingV1 d; d.mod_id = kModId; d.mod_name = "MapEditor";
        d.setting_id = "map_pack"; d.label = "Map pack"; d.kind = ANY_SETTING_TEXT; d.order = 0; d.default_text = ""; d.text_limit = 64;
        d.description = "Name of the custom map to edit and play (kept in AnyAPI and Modding\\maps). Empty = the normal world. "
                        "A new name starts as a copy of the normal world. Takes effect for worlds you create or load next.";
        g_pack_token = settings->register_setting(&d);
    } else { settings = nullptr; log(1, "AnyHelpers settings unavailable: no way to choose a map pack, so the editor stays closed"); }
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    if (!gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer || !gpu->register_renderer(draw, nullptr))
        { gpu = nullptr; log(1, "GPU drawing unavailable: no on-screen messages"); }
    read_settings();
    std::thread(install_hooks).detach();
}
