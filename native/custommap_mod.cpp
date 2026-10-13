// CustomMap: plays worlds built from custom map packs made with the Anymaker Map Maker (mapmaker/).
//
// The world lives in rom/data/tiles/0/tile_<x>_<z>_<t>.json (plus per-tile vegetation in rom/data/tiles/vegetation).
// A map pack is a folder <game>/AnyAPI and Modding/maps/<name>/ holding map.json and its own tiles/ folder. While a
// pack is selected, every game read, existence check and folder listing under rom data/tiles/ is answered from the
// pack instead, by hooking the call cells of the game's file.* natives. tiles/0 is always the pack's own (a tile the
// pack lacks is ocean); anything else under tiles/ (vegetation) falls back to the game's copy when the pack has none.
// Nothing in the game folder is ever written.
//
// Choosing a map: the CustomGame mod's Custom Game page shows a Map row and calls the "custommap.maps" service
// (sdk/include/custommap_v1.h) when a world is started or loaded. Without CustomGame the normal world is used.
// Map changes apply to new worlds; an existing save keeps the props it already stored.
//
// EXPERIMENTAL. Anymaker 0.1.25 / Steam build 25904830 only (custommap_bindings.h). The first runs log the file
// paths the game uses and which path store reads absolute paths; read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "custommap_v1.h"
#include "anyapi_experimental.hpp"
#include "custommap_bindings.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace custommap {
namespace {
namespace fs = std::filesystem;
constexpr const char* kModId = "custommap";

AnyModHostV1 host;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}

// ------------------------------------------------------------------ game types (0.1.25 reference layouts)
struct gstring { const char* p; int32_t len; int32_t reserved; };      // string, 16 bytes
struct gpath { int32_t store; int32_t pad; gstring path; };            // file.path, 24 bytes; store: 0 embedded, 1 system, 2 rom, 3 user
static_assert(sizeof(gstring) == 16 && sizeof(gpath) == 24);
constexpr int32_t kStoreRom = 2;

// ------------------------------------------------------------------ paths
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
// A pack name is a plain folder name: no separators, no "..".
bool valid_name(const std::string& n) {
    if (n.empty() || n.size() > 64 || n == "." || n == "..") return false;
    for (char c : n) if (c == '/' || c == '\\' || c == ':' || uint8_t(c) < 32) return false;
    return true;
}
bool loadable(const fs::path& pack) {
    std::error_code ec;
    if (!fs::is_regular_file(pack / L"map.json", ec)) return false;
    for (auto it = fs::directory_iterator(pack / L"tiles" / L"0", ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->path().extension() == L".json") return true;
    return false;
}

// ------------------------------------------------------------------ selection
std::mutex g_pack_mutex;
std::string g_pack_name;               // "" = normal world
std::string g_pack_tiles;              // "<maps>/<pack>/tiles" with forward slashes while a pack is selected
std::atomic<int32_t> g_abs_store{-1};  // store that reads absolute paths, found by calibration
std::atomic<bool> g_first_read{false}; // log the first redirected read after each selection
std::vector<std::string> g_list;       // last scan, sorted

uint32_t svc_count() {
    std::vector<std::string> names;
    std::error_code ec;
    for (auto it = fs::directory_iterator(maps_dir(), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::string n = utf8(it->path().filename().wstring());
        if (it->is_directory(ec) && valid_name(n) && loadable(it->path())) names.push_back(n);
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
    std::lock_guard l(g_pack_mutex);
    g_list = std::move(names);
    return uint32_t(g_list.size());
}
bool copy_out(const std::string& s, char* buf, uint32_t cap) {
    if (!buf || cap == 0 || s.size() + 1 > cap) return false;
    memcpy(buf, s.c_str(), s.size() + 1);
    return true;
}
bool svc_name(uint32_t i, char* buf, uint32_t cap) {
    std::lock_guard l(g_pack_mutex);
    return i < g_list.size() && copy_out(g_list[i], buf, cap);
}
bool svc_select(const char* name) {
    std::string n = name ? name : "";
    if (n.empty()) {
        std::lock_guard l(g_pack_mutex);
        if (!g_pack_name.empty()) log(0, "normal world selected");
        g_pack_name.clear(); g_pack_tiles.clear();
        return true;
    }
    fs::path pack = maps_dir() / wide(n);
    if (!valid_name(n) || !loadable(pack)) { log(2, "map pack '%s' is missing or has no tiles; using the normal world", n.c_str()); svc_select(""); return false; }
    if (!g_ready || g_abs_store < 0) { log(2, "map pack '%s' chosen, but CustomMap is not ready (see the errors above)", n.c_str()); svc_select(""); return false; }
    std::string root = utf8((pack / L"tiles").wstring());
    std::replace(root.begin(), root.end(), '\\', '/');
    std::lock_guard l(g_pack_mutex);
    if (g_pack_name != n) log(0, "map pack '%s' selected (%s)", n.c_str(), root.c_str());
    g_pack_name = n; g_pack_tiles = root; g_first_read = true;
    return true;
}
bool svc_selected(char* buf, uint32_t cap) {
    std::lock_guard l(g_pack_mutex);
    return copy_out(g_pack_name, buf, cap);
}
const CustomMapsV1 g_service{sizeof(CustomMapsV1), 1, svc_count, svc_name, svc_select, svc_selected};

// ------------------------------------------------------------------ redirection
// A path argument is only trusted when it looks like a file.path: a known store and a readable string.
bool sane_path(const gpath* p) {
    return p && anymaker::readable(p, sizeof(*p)) && p->store >= 0 && p->store <= 3 && p->path.p &&
           p->path.len > 0 && p->path.len <= 4096 && anymaker::readable(p->path.p, size_t(p->path.len));
}
// rom "data/tiles/..." -> the part after "data/tiles" ("" or "/0/tile_1_2_0.json"). False for any other path.
bool is_tile_path(const gpath* p, std::string* rest) {
    if (!sane_path(p) || p->store != kStoreRom || p->path.len < 10) return false;
    std::string s(p->path.p, size_t(p->path.len));
    std::replace(s.begin(), s.end(), '\\', '/');
    size_t start = s.rfind("data/tiles");
    if (start == std::string::npos || (start > 0 && s[start - 1] != '/')) return false;
    if (s.size() > start + 10 && s[start + 10] != '/') return false;   // "data/tiles_foo" is not ours
    if (rest) *rest = s.substr(start + 10);
    return true;
}
bool in_tiles0(const std::string& rest) { return rest == "/0" || rest.rfind("/0/", 0) == 0; }

struct redirect {
    gpath path{}; std::string text; bool active = false;
    explicit redirect(const gpath* p) {
        std::string rest;
        if (!is_tile_path(p, &rest)) return;
        int32_t store = g_abs_store.load();
        if (store < 0) return;
        { std::lock_guard l(g_pack_mutex); if (g_pack_tiles.empty()) return; text = g_pack_tiles + rest; }
        // Outside tiles/0 (vegetation and the like) a file the pack does not have comes from the game.
        std::error_code ec;
        if (!in_tiles0(rest) && !fs::exists(fs::path(wide(text)), ec)) return;
        path = *p; path.store = store; path.path.p = text.c_str(); path.path.len = int32_t(text.size()); path.path.reserved = 0;
        active = true;
        if (g_first_read.exchange(false)) log(0, "the world is reading tiles from the map pack (%s)", text.c_str());
    }
    const gpath* use(const gpath* original) const { return active ? &path : original; }
};

// Diagnostics: the first distinct rom tile folders the game touches.
std::mutex g_seen_mutex;
std::set<std::string> g_seen;
void note_path(const char* what, const gpath* p) {
    if (!sane_path(p) || p->store != kStoreRom) return;
    std::string s(p->path.p, size_t(std::min(p->path.len, 300)));
    size_t slash = s.find_last_of("/\\");
    std::string key = std::string(what) + ":" + (slash == std::string::npos ? s : s.substr(0, slash + 1));
    if (key.find("tiles") == std::string::npos) return;
    std::lock_guard l(g_seen_mutex);
    if (g_seen.size() >= 20 || !g_seen.insert(key).second) return;
    log(0, "%s: %s", what, s.c_str());
}

// ------------------------------------------------------------------ file native hooks (any thread)
anymaker::cell_hook h_read_string, h_read_binary, h_exists, h_files, h_folders;
using read_t = void (*)(bool* ret, const gpath* path, void* out);
using exists_t = void (*)(bool* ret, const gpath* path);
using list_t = void (*)(const gpath* path, void* files);
using list2_t = void (*)(const gpath* path, void* files, void* folders);

void hk_read_string(bool* ret, const gpath* p, void* out) {
    note_path("read", p);
    redirect r(p); read_t(h_read_string.original)(ret, r.use(p), out);
}
void hk_read_binary(bool* ret, const gpath* p, void* out) {
    note_path("read binary", p);
    redirect r(p); read_t(h_read_binary.original)(ret, r.use(p), out);
}
void hk_exists(bool* ret, const gpath* p) {
    redirect r(p); exists_t(h_exists.original)(ret, r.use(p));
}
void hk_files(const gpath* p, void* files) {
    note_path("list", p);
    redirect r(p); list_t(h_files.original)(r.use(p), files);
}
void hk_folders(const gpath* p, void* files, void* folders) {
    note_path("list", p);
    redirect r(p); list2_t(h_folders.original)(r.use(p), files, folders);
}

// ------------------------------------------------------------------ install (background thread)
// The file functions are game.exe natives at fixed thunks (same in 0.1.24 and 0.1.25, SDK natives.json). Their call
// cells are found through gcl callers, which may not resolve while the game is still starting. A cell is only used
// when it is found and holds a game.exe native, and the hooks go in all together or not at all.
struct native_cell { anymaker::cell_hook* h; const anymaker::func_desc* d; void* fn; const char* name; uintptr_t rva; void** cell; };
native_cell g_file_cells[] = {
    {&h_read_string, &bind::read_string, (void*)&hk_read_string, "file.read_string", 0x3f080, nullptr},
    {&h_read_binary, &bind::read_binary, (void*)&hk_read_binary, "file.read_binary", 0x3f0e0, nullptr},
    {&h_exists, &bind::is_exists, (void*)&hk_exists, "file.get_is_exists", 0x3f140, nullptr},
    {&h_files, &bind::files_in_dir, (void*)&hk_files, "file.get_files_in_dir", 0x3edf0, nullptr},
    {&h_folders, &bind::folders_in_dir, (void*)&hk_folders, "file.get_files_folders_in_dir", 0x3eb80, nullptr},
};
bool locate_file_cells(bool report) {
    static std::set<std::string> warned;
    auto base = uintptr_t(GetModuleHandleW(nullptr));
    auto* nt = (const IMAGE_NT_HEADERS64*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    uintptr_t image_size = nt->OptionalHeader.SizeOfImage;
    bool all = true;
    for (auto& c : g_file_cells) {
        c.cell = anymaker::experimental::hook_cell(*c.d);
        if (!c.cell) { if (report) log(2, "could not locate %s", c.name); all = false; continue; }
        auto v = uintptr_t(*c.cell);
        if (v == base + c.rva) continue;
        if (v >= base && v < base + image_size) {   // a game.exe native, just not at the expected thunk
            if (warned.insert(c.name).second)
                log(1, "%s: cell holds game.exe+0x%llx, expected +0x%llx; using it", c.name, (unsigned long long)(v - base), (unsigned long long)c.rva);
            continue;
        }
        if (report) log(2, "%s: the located cell holds %p, which is not a game.exe native; not hooking it", c.name, *c.cell);
        c.cell = nullptr; all = false;
    }
    return all;
}
bool install_file_hooks() {
    for (auto& c : g_file_cells)
        if (!c.h->install(c.cell, c.fn)) {
            log(2, "the %s hook cell was already taken or not writable", c.name);
            for (auto& u : g_file_cells) u.h->remove(u.fn);
            return false;
        }
    return true;
}

// Which path store opens an absolute path? Ask the game about a file we know exists.
void calibrate_store(exists_t exists) {
    std::string exe = utf8((game_dir() / L"game.exe").wstring());
    std::replace(exe.begin(), exe.end(), '\\', '/');
    for (int32_t store : {1, 3, 0, 2}) {
        gpath p{}; p.store = store; p.path = {exe.c_str(), int32_t(exe.size()), 0};
        bool ok = false; exists(&ok, &p);
        if (ok) { g_abs_store = store; log(0, "absolute paths use path store %d", store); return; }
    }
    log(2, "no path store opened an absolute path; map packs cannot be loaded");
}

void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; CustomMap stays off"); return; }
        bool last = attempt == 179;
        if (!locate_file_cells(last)) { if (last) log(2, "file functions not found; CustomMap is off"); continue; }
        if (!install_file_hooks()) { log(2, "file hooks not installed; CustomMap is off"); return; }
        calibrate_store(exists_t(h_exists.original));
        g_ready = g_abs_store >= 0;
        uint32_t packs = svc_count();
        log(g_ready ? 0 : 2, g_ready ? "Ready after %d s: %u map pack(s) in %ls" : "Not ready after %d s; see the errors above",
            attempt, packs, maps_dir().c_str());
        return;
    }
}
}  // namespace
}  // namespace custommap

using namespace custommap;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId;
    out->shutdown = [](void*) {
        g_stop = true;
        for (auto& c : g_file_cells) c.h->remove(c.fn);
    };
    auto services = AnyAPI_Services();
    if (!services || !services->publish || !services->publish("custommap.maps", 1, &g_service))
        log(1, "could not publish custommap.maps: no mod can choose a map");
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    std::thread(install_hooks).detach();
}
