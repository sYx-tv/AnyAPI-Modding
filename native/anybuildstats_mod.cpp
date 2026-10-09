// AnyBuildStats (prototype): a spec sheet for the creation you aim the Properties tool at. It shows mass,
// size, part counts (components, edges, plates, nodes), electric motor and alternator power, and the most
// common part categories, next to the AnyBalance card. A key (F9, rebindable) copies the sheet to the
// clipboard so you can share a build.
//
// Mass, size and body count come from the reviewed anyapi.creation_balance service. Part counts read the
// client vehicle through the experimental SDK (anybuildstats_bindings.h): Anymaker 0.1.23 / Steam build
// 25755694 only. Read-only and local: nothing is sent to other players.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_creation_balance_v1.h"
#include "anyhelpers_settings_v1.h"
#include "mod_controls_v1.h"
#include "anyapi_experimental.hpp"
#include "anybuildstats_bindings.h"
#include "anybuildstats_logic.h"
#include "vehicle_reads.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

namespace anybuildstats {
namespace {
using namespace vehicle_reads;
constexpr const char* kModId = "anybuildstats";

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
const AnyUiStateV1* ui;
const AnyCreationBalanceV1* balance;
const AnyHelpersSettingsV1* settings;
const ModControlsV1* controls;
uint64_t g_copy_action;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}

enum Setting { kEnabled, kCategories, kPosition, kOpacity, kSettingCount };
uint64_t g_tokens[kSettingCount]{}, g_revision = UINT64_MAX;
double g_values[kSettingCount]{1, 1, 0, 90};

std::mutex g_mutex;
std::atomic<int32_t> g_target{-1};         // vehicle id the Properties tool is aimed at (render thread writes)
std::atomic<uint64_t> g_target_at{0};
Stats g_stats;                             // last sample (main thread writes under g_mutex)
uint64_t g_stats_at = 0;
// Last shown sheet for the clipboard key.
AnyCreationBalanceSnapshotV1 g_shown{};
bool g_visible = false, g_held[256]{};
uint64_t g_copied_at = 0;

void refresh_settings() {
    if (!settings) return;
    uint64_t next = settings->revision();
    if (next == g_revision) return;
    g_revision = next;
    const double lo[] = {0, 0, 0, 25}, hi[] = {1, 1, 1, 100};
    for (int i = 0; i < kSettingCount; ++i) {
        AnySettingValueV1 v;
        if (settings->get(g_tokens[i], &v) && std::isfinite(v.number)) g_values[i] = std::clamp(v.number, lo[i], hi[i]);
    }
}

// ------------------------------------------------------------------ sampling (main thread)
using vehicle_tick_t = void (*)(uint8_t* vehicle, void* client, void* scene, const double* a, const double* b, const void* c);
anymaker::cell_hook h_tick;
std::atomic<int> g_sample_logs{3};

void sample(const uint8_t* vehicle, int32_t id) {
    Stats s; s.vehicle_id = id;
    s.nodes = std::max(0, vector_count(vehicle + off::vehicle_nodes));
    s.edges = std::max(0, vector_count(vehicle + off::vehicle_edges));
    s.plates = std::max(0, vector_count(vehicle + off::vehicle_plates));
    char category[64], klass[64];
    std::vector<Tally> classes;
    for_each_component(vehicle, [&](uint8_t* component) {
        ++s.components;
        auto* def = rd<const uint8_t*>(component, off::component_definition);
        if (!def || !anymaker::readable(def, off::def_motor_wattage + 8)) { count(s.categories, "Other"); return; }
        copy_string(def + off::def_category, category, sizeof category);
        copy_string(def + off::def_class, klass, sizeof klass);
        count(s.categories, pretty(category[0] ? category : klass));
        count(classes, pretty(klass));
        double motor = rd<double>(def, off::def_motor_wattage), alternator = rd<double>(def, off::def_alternator_wattage);
        if (std::isfinite(motor) && motor > 0 && motor < 1e9) s.motor_watts += motor;
        if (std::isfinite(alternator) && alternator > 0 && alternator < 1e9) s.alternator_watts += alternator;
    });
    sort_tallies(s.categories);
    if (g_sample_logs.fetch_sub(1) > 0) {
        sort_tallies(classes);
        std::string cats, cls;
        for (size_t i = 0; i < s.categories.size() && i < 8; ++i) cats += s.categories[i].name + "=" + std::to_string(s.categories[i].count) + " ";
        for (size_t i = 0; i < classes.size() && i < 8; ++i) cls += classes[i].name + "=" + std::to_string(classes[i].count) + " ";
        log(0, "sampled vehicle %d: %d components, %d nodes, %d edges, %d plates, motors %.0f W, alternators %.0f W; categories %s; classes %s",
            id, s.components, s.nodes, s.edges, s.plates, s.motor_watts, s.alternator_watts, cats.c_str(), cls.c_str());
    }
    std::lock_guard lock(g_mutex);
    g_stats = std::move(s);
    g_stats_at = GetTickCount64();
}

void hk_tick(uint8_t* vehicle, void* client, void* scene, const double* a, const double* b, const void* c) {
    reinterpret_cast<vehicle_tick_t>(h_tick.original)(vehicle, client, scene, a, b, c);
    int32_t target = g_target.load();
    if (target <= 0 || !vehicle || GetTickCount64() - g_target_at.load() > 500) return;
    if (!anymaker::readable(vehicle, 2400) || rd<int32_t>(vehicle, off::vehicle_id) != target) return;
    {
        std::lock_guard lock(g_mutex);
        if (g_stats.vehicle_id == target && GetTickCount64() - g_stats_at < 400) return;
    }
    sample(vehicle, target);
}

void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; part counts stay off"); return; }
        void** tick = hook_cell(bind::client_vehicle_tick);
        if (!tick) { if (attempt == 179) log(2, "could not locate the client vehicle tick; part counts stay off"); continue; }
        if (!h_tick.install(tick, (void*)&hk_tick)) { log(2, "vehicle tick hook cell was taken or not writable; part counts stay off"); return; }
        g_ready = true;
        log(0, "Ready after %d s: equip the Properties tool and aim at a creation.", attempt);
        return;
    }
}

// ------------------------------------------------------------------ drawing (render thread)
bool gameplay() { AnyUiSnapshotV1 s; return ui && ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY; }

void key_label(wchar_t* out, size_t n) {
    char name[32] = "F9";
    if (controls && g_copy_action) {
        uint32_t k = controls->key(g_copy_action);
        if (!k) snprintf(name, sizeof name, "unbound");
        else if (!(controls->key_name && controls->key_name(g_copy_action, name, sizeof name))) snprintf(name, sizeof name, "key %u", k);
    }
    swprintf(out, n, L"%hs copies this sheet", name);
}

void draw(const AnyFrameV1* frame, void*) {
    if (!frame || !gpu) return;
    AnyCreationBalanceSnapshotV1 data;
    std::lock_guard lock(g_mutex);
    refresh_settings();
    g_visible = false;
    if (!g_values[kEnabled] || !frame->focused || !gameplay() || !balance->copy(&data)) return;
    g_target.store(data.vehicle_id); g_target_at.store(GetTickCount64());
    g_visible = true; g_shown = data;
    bool have_parts = g_ready && g_stats.vehicle_id == data.vehicle_id && GetTickCount64() - g_stats_at < 2000;
    float s = std::clamp(float(frame->height) / 1080.f, .65f, 1.75f), alpha = float(g_values[kOpacity] / 100);
    int rows = 3 + (have_parts ? 1 : 0) + (have_parts && (g_stats.motor_watts > 0 || g_stats.alternator_watts > 0) ? 1 : 0);
    int cat_rows = have_parts && g_values[kCategories] ? int(std::min<size_t>(g_stats.categories.size(), 8) + 1) / 2 : 0;
    float w = 300 * s, row = 20 * s, h = 34 * s + rows * row + cat_rows * 18 * s + (cat_rows ? 8 * s : 0) + 22 * s;
    w = std::min(w, float(frame->width)); h = std::min(h, float(frame->height));
    // Default: stacked above the AnyBalance card (bottom right). Alternative: bottom left.
    float x = g_values[kPosition] ? 20 * s : std::max(0.f, float(frame->width) - w - 20 * s);
    float y = std::max(0.f, float(frame->height) - h - (g_values[kPosition] ? 105 * s : 105 * s + 132 * s + 10 * s));
    auto box = [&](float bx, float by, float bw, float bh, uint32_t color, float radius, float o) {
        AnyGpuCommandV1 c; c.kind = ANY_GPU_ROUND_RECT; c.rect[0] = bx; c.rect[1] = by; c.rect[2] = bw; c.rect[3] = bh; c.radius = radius; c.color = color; c.opacity = o; gpu->emit(&c); };
    auto text = [&](float tx, float ty, float tw, float th, const wchar_t* t, float size, uint32_t color, uint32_t flags = 0) {
        AnyGpuCommandV1 c; c.kind = ANY_GPU_TEXT; c.rect[0] = tx; c.rect[1] = ty; c.rect[2] = tw; c.rect[3] = th; c.text = t; c.text_length = uint32_t(wcslen(t));
        c.font_size = size; c.color = color; c.opacity = alpha; c.flags = flags | ANY_GPU_NOWRAP | ANY_GPU_VCENTER; gpu->emit(&c); };
    box(x, y, w, h, 0xff191f25, 5 * s, alpha * .92f);
    float in = 14 * s, cy = y + 10 * s;
    wchar_t buf[160];
    text(x + in, cy, w - in * 2, 20 * s, L"Build stats", 13 * s, 0xffedf1f5, ANY_GPU_BOLD);
    if (data.body_count > 1) { swprintf_s(buf, L"%u bodies", data.body_count); text(x + w - in - 80 * s, cy, 80 * s, 20 * s, buf, 11 * s, 0xff929fab, ANY_GPU_CENTER); }
    cy += 26 * s;
    auto pair = [&](const wchar_t* label, const wchar_t* value, uint32_t color) {
        text(x + in, cy, 90 * s, row, label, 11 * s, 0xff929fab);
        text(x + in + 92 * s, cy, w - in * 2 - 92 * s, row, value, 12 * s, color);
        cy += row;
    };
    if (data.valid_fields & ANY_BALANCE_BODY_MASS) {
        swprintf_s(buf, L"%hs kg%ls", grouped(static_cast<long long>(data.body_mass_kg + .5)).c_str(), data.valid_fields & ANY_BALANCE_FLUID_MASS ? L" incl. fluid" : L"");
        pair(L"Mass", buf, 0xffffd47c);
    } else pair(L"Mass", L"not available", 0xff929fab);
    double sx = data.bounds_max.x - data.bounds_min.x, sy = data.bounds_max.y - data.bounds_min.y, sz = data.bounds_max.z - data.bounds_min.z;
    swprintf_s(buf, L"%.2f × %.2f m, %.2f m high", sx, sz, sy);
    pair(L"Size", buf, 0xffced8e2);
    swprintf_s(buf, L"%.2f m above base", data.height_m);
    pair(L"Centre", buf, 0xffced8e2);
    if (have_parts) {
        swprintf_s(buf, L"%hs parts · %hs edges · %hs plates", grouped(g_stats.components).c_str(), grouped(g_stats.edges).c_str(), grouped(g_stats.plates).c_str());
        pair(L"Built from", buf, 0xffced8e2);
        if (g_stats.motor_watts > 0 || g_stats.alternator_watts > 0) {
            if (g_stats.motor_watts > 0 && (data.valid_fields & ANY_BALANCE_BODY_MASS) && data.body_mass_kg > 0)
                swprintf_s(buf, L"%hs motors · %.1f kW/t", power_text(g_stats.motor_watts).c_str(), g_stats.motor_watts / data.body_mass_kg);
            else if (g_stats.motor_watts > 0) swprintf_s(buf, L"%hs motors", power_text(g_stats.motor_watts).c_str());
            else swprintf_s(buf, L"%hs alternators", power_text(g_stats.alternator_watts).c_str());
            pair(L"Electric", buf, 0xff7dd3fc);
        }
        if (cat_rows) {
            cy += 4 * s;
            AnyGpuCommandV1 sep; sep.kind = ANY_GPU_LINE; sep.rect[0] = x + in; sep.rect[1] = cy; sep.rect[2] = x + w - in; sep.rect[3] = cy; sep.color = 0xff3a4048; sep.stroke = 1; sep.opacity = alpha; gpu->emit(&sep);
            cy += 4 * s;
            float col = (w - in * 2) / 2;
            for (size_t i = 0; i < g_stats.categories.size() && i < 8; ++i) {
                swprintf_s(buf, L"%hs  %hs", g_stats.categories[i].name.c_str(), grouped(g_stats.categories[i].count).c_str());
                text(x + in + (i % 2) * col, cy + float(i / 2) * 18 * s, col - 6 * s, 18 * s, buf, 11 * s, 0xffdfe4ea);
            }
            cy += cat_rows * 18 * s;
        }
    } else if (!g_ready) pair(L"Parts", L"counts unavailable on this game build", 0xff929fab);
    else pair(L"Parts", L"counting...", 0xff929fab);
    if (GetTickCount64() - g_copied_at < 1500) wcscpy_s(buf, L"Copied to clipboard");
    else key_label(buf, 160);
    text(x + in, y + h - 22 * s, w - in * 2, 18 * s, buf, 9 * s, 0xff929fab);
}

// ------------------------------------------------------------------ clipboard (input thread)
void copy_sheet() {
    double size[3] = {g_shown.bounds_max.x - g_shown.bounds_min.x, g_shown.bounds_max.y - g_shown.bounds_min.y, g_shown.bounds_max.z - g_shown.bounds_min.z};
    Stats parts = g_stats.vehicle_id == g_shown.vehicle_id ? g_stats : Stats{};
    std::string text = sheet(parts, g_shown.body_mass_kg, (g_shown.valid_fields & ANY_BALANCE_BODY_MASS) != 0, size, int(g_shown.body_count));
    int wide = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wide <= 0 || !OpenClipboard(nullptr)) return;
    EmptyClipboard();
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size_t(wide) * sizeof(wchar_t))) {
        if (auto* p = static_cast<wchar_t*>(GlobalLock(mem))) {
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, p, wide);
            GlobalUnlock(mem);
            if (SetClipboardData(CF_UNICODETEXT, mem)) { g_copied_at = GetTickCount64(); mem = nullptr; }
        }
        if (mem) GlobalFree(mem);
    }
    CloseClipboard();
    log(0, "copied build sheet for vehicle %d", g_shown.vehicle_id);
}

uint32_t input(const AnyInputV1* e, void*) {
    if (!e) return 0;
    std::lock_guard lock(g_mutex);
    if (e->kind == ANY_FOCUS_LOST) { std::fill(std::begin(g_held), std::end(g_held), false); return 0; }
    if (e->key >= 256 || (e->kind != ANY_KEY_DOWN && e->kind != ANY_KEY_UP)) return 0;
    if (e->kind == ANY_KEY_UP) { bool captured = g_held[e->key]; g_held[e->key] = false; return captured; }
    uint32_t key = controls && g_copy_action ? controls->key(g_copy_action) : uint32_t(VK_F9);
    if (!key || e->key != key || !g_visible) return 0;
    if (!g_held[e->key]) { g_held[e->key] = true; copy_sheet(); }
    return 1;
}
}  // namespace
}  // namespace anybuildstats

using namespace anybuildstats;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId; out->input = input;
    out->shutdown = [](void*) { g_stop = true; h_tick.remove((void*)&hk_tick); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services(); if (!services) return;
    balance = static_cast<const AnyCreationBalanceV1*>(services->query("anyapi.creation_balance", 1));
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    ui = static_cast<const AnyUiStateV1*>(services->query("anyapi.ui_state", 1));
    if (!balance || balance->struct_size != sizeof(*balance) || balance->version != 1 || !balance->copy || !ui || ui->struct_size != sizeof(*ui) ||
        ui->version != 1 || !ui->copy || !gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer) {
        log(2, "AnyAPI 0.33 or newer is required (creation_balance, gpu_draw, ui_state)");
        return;
    }
    controls = static_cast<const ModControlsV1*>(services->query("anyhelpers.controls", 1));
    if (controls && controls->struct_size == sizeof(*controls) && controls->version == 1 && controls->register_action && controls->key) {
        ModControlActionV1 d; d.mod_id = kModId; d.mod_name = "AnyBuildStats"; d.action_id = "copy_sheet"; d.label = "Copy build stats to clipboard"; d.default_key = VK_F9;
        g_copy_action = controls->register_action(&d);
    } else controls = nullptr;
    settings = static_cast<const AnyHelpersSettingsV1*>(services->query("anyhelpers.settings", 1));
    if (settings && settings->struct_size == sizeof(*settings) && settings->version == 1 && settings->register_setting && settings->get && settings->revision) {
        const char* ids[] = {"enabled", "categories", "position", "opacity"};
        const char* labels[] = {"Show build stats with the Properties tool", "Show part categories", "Card position", "Card opacity (%)"};
        const char* positions[] = {"Right, above AnyBalance", "Bottom left"};
        uint32_t kinds[] = {ANY_SETTING_BOOL, ANY_SETTING_BOOL, ANY_SETTING_CHOICE, ANY_SETTING_INTEGER};
        double lo[] = {0, 0, 0, 25}, hi[] = {1, 1, 1, 100}, step[] = {1, 1, 1, 5};
        for (int i = 0; i < kSettingCount; ++i) {
            AnyModSettingV1 d; d.mod_id = kModId; d.mod_name = "AnyBuildStats"; d.setting_id = ids[i]; d.label = labels[i]; d.order = i; d.kind = kinds[i];
            d.default_number = g_values[i]; d.minimum = lo[i]; d.maximum = hi[i]; d.step = step[i];
            if (i == kPosition) { d.choices = positions; d.choice_count = 2; }
            if (i == 0) d.description = "Mass and size cover the whole creation. Part counts cover the body the Properties tool reports.";
            g_tokens[i] = settings->register_setting(&d);
        }
    } else settings = nullptr;
    if (!gpu->register_renderer(draw, nullptr)) { log(2, "GPU renderer registration failed"); return; }
    std::thread(install_hooks).detach();
}
