// MCCode: a Code view for the microcontroller editor. Write the program as text instead of blocks.
//
// A "</> Code" button in the microcontroller editor opens a full code editor for that microcontroller:
//  - The program is read with the game's own Save action (the server writes the blocks as script text, see
//    server_scene_script.element_root.write) and shown as friendly code (mccode_lang.h prettify).
//  - Apply (Ctrl+Enter) translates the code back to the game's script text and sends it with the game's own Load
//    action (client_peer.data.push_event_microcontroller_load -> server parser), so the blocks, Undo, saving and
//    multiplayer all keep working the normal way. Mistakes are caught with line numbers before anything is sent.
//  - A side panel lists every command, this microcontroller's names and a Watch list; click to insert.
//  - Debugger: breakpoints in the gutter (F9), Pause / Resume (F5), Step in / over / out (F11 / F10 / Shift+F11),
//    the running line highlighted, through the game's own microcontroller debug events.
//
// Lines map to blocks by walking the replicated element tree (client_scene.peer.inspection_data.microcontroller)
// in the same order the game writes the text: one line per statement, "{" / "}" lines for row containers.
// If the counts disagree the editor still works and only the line-based debug features turn off.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (mccode_bindings.h): Anymaker 0.1.27 /
// Steam build 26113663 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_item_catalog_v1.h"
#include "anyapi_experimental.hpp"
#include "mccode_bindings.h"
#include "route_cell.h"
#include "mccode_lang.h"
#include "mccode_editor.h"
#include "mccode_complete.h"
#include "mccode_nodes.h"
#include "mccode_scripts.h"
#include "mccode_parts.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mccode {
namespace {
constexpr const char* kModId = "mccode";
constexpr const char* kTempScript = "mccode_view";   // name of the Save round trip that reads the program

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
const AnyItemCatalogV1* catalog;   // part definitions for the Parts tab (optional)
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[768];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
double now_seconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

// ================================================================== game layout (client_scene.peer.inspection_data)
namespace off {
// replication.slave.property_type_simple<T>: value at +8. property_string: gc string at +8.
constexpr ptrdiff_t prop_value = 8;
// microcontroller
constexpr ptrdiff_t mc_vehicle_id = 0x08, mc_component_id = 0x18, mc_is_step = 0x28, mc_step_cursor = 0x38,
                    mc_can_undo = 0x48, mc_load_error = 0x68, mc_root = 0x78, mc_variables = 0xa8, mc_parameters = 0x168,
                    mc_value_outputs = 0x198, mc_value_inputs = 0x1c8, mc_errors = 0x1f8, mc_debug_values = 0x228,
                    mc_debug_components = 0x258;
// containers: vector<ref<T>> at +8
constexpr ptrdiff_t container_vector = 0x08;
// microcontroller_element
constexpr ptrdiff_t el_id = 0x08, el_index = 0x18, el_is_error = 0x28, el_is_breakpoint = 0x38;
constexpr ptrdiff_t root_elements = 0x58, row_elements = 0x68;
// microcontroller_variable
constexpr ptrdiff_t var_id = 0x08, var_name = 0x18;
// microcontroller_error
constexpr ptrdiff_t err_element = 0x08, err_type = 0x18;
// microcontroller_debug_value (+ typed m_value at +0x28)
constexpr ptrdiff_t dv_id = 0x08, dv_is_param = 0x18, dv_value = 0x28;
// microcontroller_debug_value_component
constexpr ptrdiff_t dc_name = 0x08, dc_value = 0x20, dc_is_input = 0x38;
// microcontroller_data_value
constexpr ptrdiff_t data_component = 0x08, data_name = 0x20;
// messages_server_to_client.microcontroller_save_script_data
constexpr ptrdiff_t msg_filename = 0x08, msg_data = 0x18;
// client -> client_peer.data (frontend_ui_microcontroller.update_ui_load_script passes client + 0x90)
constexpr ptrdiff_t client_peer_data = 0x90;
}  // namespace off

// e_script_element_type values used by the walk
constexpr int kElRoot = 1, kElRowContainer = 2, kElCreateParameter = 16;

template <class T> T rd(const uint8_t* p, ptrdiff_t o) { T v; std::memcpy(&v, p + o, sizeof(T)); return v; }
int32_t prop_s32(const uint8_t* p, ptrdiff_t o) { return rd<int32_t>(p, o + off::prop_value); }
bool prop_bool(const uint8_t* p, ptrdiff_t o) { return rd<uint8_t>(p, o + off::prop_value) != 0; }
std::string gc_str(const uint8_t* p, ptrdiff_t o) {
    auto s = reinterpret_cast<const anymaker::gc_string_view*>(p + o);
    if (!s->data || s->length <= 0 || s->length > (1 << 20) || !anymaker::readable(s->data, size_t(s->length))) return "";
    return std::string(s->data, size_t(s->length));
}
std::string prop_str(const uint8_t* p, ptrdiff_t o) { return gc_str(p, o + off::prop_value); }

// Objects held by a replicated container (vector<ref<T>> at +8), in storage order.
std::vector<const uint8_t*> container_items(const uint8_t* container, size_t min_size) {
    std::vector<const uint8_t*> out;
    if (!container || !anymaker::readable(container, 0x30)) return out;
    auto v = reinterpret_cast<const anymaker::gc_vector_raw*>(container + off::container_vector);
    if (v->count <= 0 || v->count > 4096 || !v->buffer || !anymaker::readable(v->buffer, size_t(v->capacity) * 16)) return out;
    for (int32_t i = 0; i < v->count; ++i) {
        auto ref = v->at<anymaker::gc_ref_raw<uint8_t>>(i);
        if (ref && ref->ptr && anymaker::readable(ref->ptr, min_size)) out.push_back(ref->ptr);
    }
    return out;
}

void* method(const uint8_t* object, int slot) {
    if (!anymaker::readable(object, 8)) return nullptr;
    const uint8_t* ti = *reinterpret_cast<const uint8_t* const*>(object);
    if (!anymaker::readable(ti, 0x88)) return nullptr;
    void* const* table = *reinterpret_cast<void* const* const*>(ti + 0x80);
    if (!anymaker::readable(table, size_t(slot + 1) * 8)) return nullptr;
    void* cell = table[slot];
    if (!anymaker::readable(cell, 8)) return nullptr;
    void* fn = *reinterpret_cast<void**>(cell);
    return route::executable(fn) ? fn : nullptr;
}
// microcontroller_element slot 8: (e_script_element_type) get_script_element_type (microcontroller_element)
int element_type(const uint8_t* e) {
    using fn_t = void (*)(int32_t*, const uint8_t*);
    auto fn = reinterpret_cast<fn_t>(method(e, 8));
    if (!fn) return -1;
    int32_t t = -1;
    fn(&t, e);
    return t;
}
// microcontroller_element slot 9: (ptr<microcontroller_element>) get_element_by_id (microcontroller_element, const s32)
bool element_contains(const uint8_t* e, int32_t id) {
    using fn_t = void (*)(const uint8_t**, const uint8_t*, const int32_t*);
    auto fn = reinterpret_cast<fn_t>(method(e, 9));
    if (!fn) return false;
    const uint8_t* found = nullptr;
    fn(&found, e, &id);
    return found != nullptr;
}

// ================================================================== game calls (main thread)
using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
using string_dtor_t = void (*)(anymaker::gc_string_view*);
using push_string_t = void (*)(void* peer, const int32_t* vehicle, const int32_t* component, const anymaker::gc_string_view* s);
using push_plain_t = void (*)(void* peer, const int32_t* vehicle, const int32_t* component);
using push_id_t = void (*)(void* peer, const int32_t* vehicle, const int32_t* component, const int32_t* id);
string_ctor_t g_string_ctor;
string_dtor_t g_string_dtor;
push_string_t g_push_load, g_push_save;
push_plain_t g_push_pause, g_push_resume, g_push_step_in, g_push_step_over, g_push_step_out, g_push_undo;
push_id_t g_push_breakpoint;
enum DebugKind { kDbgS32, kDbgF64, kDbgBool, kDbgString, kDbgList, kDbgObject, kDbgKinds };
void* g_debug_replicate[kDbgKinds];

anymaker::cell_hook h_update_ui, h_save_data;
using update_ui_t = void (*)(void*, void*, void*, void*, void*, void*, void*, void*);
using save_data_t = void (*)(void*, void*, uint8_t*);

// ================================================================== shared state (g_mutex)
std::mutex g_mutex;

enum class ActKind { Fetch, Load, Pause, Resume, StepIn, StepOver, StepOut, Breakpoint, Undo };
struct Action { ActKind k; std::string text; int32_t id = -1; };

struct Snapshot {
    bool valid = false;
    int32_t vehicle = -1, component = -1;
    bool is_step = false, can_undo = false, load_error = false;
    std::vector<int32_t> row_element;          // per line of the game's text: statement element id, or -1
    std::vector<char> row_breakpoint;
    int step_row = -1;
    std::vector<std::pair<int, int>> error_rows;   // (row, e_script_error_type); row -1 if unplaced
    std::vector<std::string> watch;
    std::vector<std::string> ports;
    std::vector<complete::PartPort> part_ports;   // the same, split for autocomplete
};

struct State {
    // view
    bool open = false, editor_visible = false, captured = false;
    int tab = 0;                 // side panel: 0 commands, 1 this microcontroller, 2 watch, 3 parts guide
    int part_open = -1;          // Parts tab: the part whose channels are shown
    float side_scroll = 0;
    int scroll_line = 0, scroll_col = 0;
    bool follow_cursor = true;
    bool dragging = false;
    double last_click = 0;
    Pos last_click_pos;
    float frame_w = 0, frame_h = 0;
    // text
    Editor ed;
    bool ac_open = false;   // autocomplete popup
    int ac_sel = 0, ac_top = 0, ac_line = 0;
    complete::Result ac;
    unsigned compiled_rev = ~0u;
    Compiled live;               // compile of the current text
    Symbols live_symbols;
    std::vector<int> applied_map;    // game text line -> editor line, for the text the game has
    unsigned applied_rev = ~0u;      // editor revision that matches the game's program
    bool raw = false;                // showing the game's text untranslated
    bool loaded = false;             // the editor holds this microcontroller's program
    // game round trips
    bool fetching = false;
    double fetch_started = 0;
    double applied_at = -1;
    std::string message;             // status line
    uint32_t message_color = 0;
    double message_time = 0;
    // game state
    Snapshot snap;
    int32_t vehicle = -1, component = -1;   // whose program the editor holds
    std::vector<Action> actions;
    // node editor
    bool nodes_mode = false;
    nodes::Graph graph;
    bool graph_found = false;        // the program carries a node graph (or the player started one)
    bool graph_hand_edited = false;  // the code below the graph was changed by hand since
    nodes::Generated gen;            // code of the graph as last generated
    unsigned gen_rev = ~0u;          // editor revision that holds `gen.code`
    std::vector<nodes::Graph> graph_undo, graph_redo;
    float cam_x = 0, cam_y = 0, zoom = 1;
    int selected = -1;               // node id
    enum class NDrag { None, Pan, Move, Wire } ndrag = NDrag::None;
    int drag_node = -1, drag_port = -1;
    bool drag_from_input = false, drag_moved = false;
    float drag_dx = 0, drag_dy = 0, mouse_x = 0, mouse_y = 0;
    int edit_param = -1;             // parameter of the selected node being typed into
    std::string edit_buf;
    int added = 0;
};
State g;
std::atomic<double> g_editor_seen{-1e9};
void load_graph(bool from_game);
void commit_param();
bool g_escape_held = false;      // the Escape that closed the Code view: swallow its key-up too
std::vector<int> g_diag_lines;   // line each row of the problems strip jumps to, -1 for none (as last drawn)

void set_message(const std::string& m, uint32_t color = 0xffaab4bd) { g.message = m; g.message_color = color; g.message_time = now_seconds(); }
bool editor_open() { return now_seconds() - g_editor_seen.load() < 0.3; }

const char* error_text(int t) {
    static const char* names[] = {
        "Error", "Defined in the wrong place", "Member has no type", "Members must come first in the block",
        "Parameter has no type", "Parameters must come first", "Something is missing here", "Expected a type",
        "Value type doesn't match the parameter", "Function used before it is defined", "Type used before it is defined",
        "Parameter used before it is defined", "Variable used before it is defined", "That member isn't part of this parameter's type",
        "That member isn't part of this variable's type", "That type can't be used here", "The two sides have different types",
        "Expected a value", "Expected true or false", "Expected an integer", "Expected a fractional number", "Expected a string",
        "Expected a list", "Expected a number", "Expected a string or a list", "Expected an input or output", "Nothing is connected to this port",
        "The connected part is broken", "This can't be written to", "This can't be read", "Can't read from the connected part",
        "Name is empty", "Names must start with a letter or _"};
    return t >= 0 && t < int(sizeof(names) / sizeof(names[0])) ? names[t] : "Error";
}

// ================================================================== reading the editor's state (main thread)
void walk_rows(const uint8_t* container, std::vector<const uint8_t*>& rows, int depth) {
    if (depth > 48 || rows.size() > 20000) return;
    auto items = container_items(container, 0x48);
    std::vector<std::pair<int32_t, const uint8_t*>> sorted;
    for (auto* e : items) sorted.push_back({prop_s32(e, off::el_index), e});
    std::stable_sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [index, e] : sorted) {
        int t = element_type(e);
        if (t == kElCreateParameter) continue;   // written inside "define function"
        if (t == kElRowContainer && anymaker::readable(e, off::row_elements + 0x30)) {
            rows.push_back(nullptr);
            walk_rows(e + off::row_elements, rows, depth + 1);
            rows.push_back(nullptr);
            continue;
        }
        rows.push_back(e);
    }
}

// type_hint: the type the code declares for this name, used when the game's value kind can't be told apart
// (the replicate methods MCCode compares against aren't statically locatable on 0.1.25).
std::string debug_value_text(const uint8_t* dv, const std::string& type_hint = "") {
    void* rep = method(dv, 0);
    int kind = -1;
    for (int k = 0; k < kDbgKinds; ++k) if (rep && rep == g_debug_replicate[k]) kind = k;
    if (kind < 0) kind = type_hint == "fractional" ? kDbgF64 : type_hint == "integer" ? kDbgS32 : type_hint == "bool" ? kDbgBool
                         : type_hint == "string" ? kDbgString : type_hint == "list" ? kDbgList : -1;
    char buf[64];
    switch (kind) {
    case kDbgS32: snprintf(buf, sizeof buf, "%d", rd<int32_t>(dv, off::dv_value + off::prop_value)); return buf;
    case kDbgF64: snprintf(buf, sizeof buf, "%.4g", rd<double>(dv, off::dv_value + off::prop_value)); return buf;
    case kDbgBool: return rd<uint8_t>(dv, off::dv_value + off::prop_value) ? "true" : "false";
    case kDbgString: return "\"" + prop_str(dv, off::dv_value) + "\"";
    case kDbgList: return "[" + std::to_string(container_items(dv + off::dv_value, 0x28).size()) + " items]";
    case kDbgObject: return "{object}";
    default: return "(analogue)";
    }
}

int g_walk_countdown = 0;
Snapshot g_last;   // main thread only

void read_snapshot(const uint8_t* mc, Snapshot& s, const std::map<std::string, std::string>& types) {
    s.valid = true;
    s.vehicle = prop_s32(mc, off::mc_vehicle_id);
    s.component = prop_s32(mc, off::mc_component_id);
    s.is_step = prop_bool(mc, off::mc_is_step);
    s.can_undo = prop_bool(mc, off::mc_can_undo);
    s.load_error = prop_bool(mc, off::mc_load_error);

    // statement rows
    std::vector<const uint8_t*> rows;
    auto top = container_items(mc + off::mc_root, 0x48);
    if (top.size() == 1 && element_type(top[0]) == kElRoot && anymaker::readable(top[0], off::root_elements + 0x30))
        walk_rows(top[0] + off::root_elements, rows, 0);
    else
        walk_rows(mc + off::mc_root, rows, 0);
    s.row_element.assign(rows.size(), -1);
    s.row_breakpoint.assign(rows.size(), 0);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i]) continue;
        s.row_element[i] = prop_s32(rows[i], off::el_id);
        s.row_breakpoint[i] = prop_bool(rows[i], off::el_is_breakpoint) ? 1 : 0;
    }
    auto row_of = [&](int32_t id) {
        for (size_t i = 0; i < rows.size(); ++i) if (rows[i] && s.row_element[i] == id) return int(i);
        for (size_t i = 0; i < rows.size(); ++i) if (rows[i] && element_contains(rows[i], id)) return int(i);
        return -1;
    };
    s.step_row = s.is_step ? row_of(prop_s32(mc, off::mc_step_cursor)) : -1;
    for (auto* e : container_items(mc + off::mc_errors, 0x28)) s.error_rows.push_back({row_of(prop_s32(e, off::err_element)), prop_s32(e, off::err_type)});

    // watch: variables and parameters with debug values, then ports
    std::vector<std::pair<int32_t, std::string>> vars, params;
    for (auto* v : container_items(mc + off::mc_variables, 0x30)) vars.push_back({prop_s32(v, off::var_id), prop_str(v, off::var_name)});
    for (auto* v : container_items(mc + off::mc_parameters, 0x30)) params.push_back({prop_s32(v, off::var_id), prop_str(v, off::var_name)});
    for (auto* dv : container_items(mc + off::mc_debug_values, 0x28)) {
        int32_t id = prop_s32(dv, off::dv_id);
        bool is_param = prop_bool(dv, off::dv_is_param);
        std::string name = "#" + std::to_string(id);
        for (auto& [vid, vname] : is_param ? params : vars) if (vid == id) name = vname;
        auto t = types.find(name);
        s.watch.push_back(name + " = " + debug_value_text(dv, t == types.end() ? std::string() : t->second));
    }
    for (auto* c : container_items(mc + off::mc_debug_components, 0x48))
        s.watch.push_back(std::string(prop_bool(c, off::dc_is_input) ? "in " : "out ") + prop_str(c, off::dc_name) + " = " + prop_str(c, off::dc_value));
    for (ptrdiff_t list : {off::mc_value_inputs, off::mc_value_outputs})
        for (auto* d : container_items(mc + list, 0x38)) {
            std::string comp = prop_str(d, off::data_component), name = prop_str(d, off::data_name);
            s.ports.push_back((list == off::mc_value_inputs ? "in  " : "out ") + comp + (comp.empty() || name.empty() ? "" : " \xb7 ") + name);
            s.part_ports.push_back({comp, name, list == off::mc_value_inputs});
        }
}

// The Save round trip that reads a program carries the microcontroller in its name, so an answer that arrives
// after the player moved on to another microcontroller is never shown (and applied) as that one's program.
std::string fetch_name(int32_t vehicle, int32_t component) {
    return std::string(kTempScript) + "_" + std::to_string(vehicle) + "_" + std::to_string(component);
}

void run_action(void* peer, int32_t vehicle, int32_t component, const Action& a) {
    switch (a.k) {
    case ActKind::Fetch: case ActKind::Load: {
        anymaker::gc_string_view s{};
        const std::string& text = a.k == ActKind::Fetch ? fetch_name(vehicle, component) : a.text;
        g_string_ctor(&s, text.c_str());
        (a.k == ActKind::Fetch ? g_push_save : g_push_load)(peer, &vehicle, &component, &s);
        g_string_dtor(&s);
        if (a.k == ActKind::Fetch) log(0, "reading the program of microcontroller %d/%d", vehicle, component);
        else log(0, "loading %d bytes into microcontroller %d/%d", int(a.text.size()), vehicle, component);
        break;
    }
    case ActKind::Pause: g_push_pause(peer, &vehicle, &component); break;
    case ActKind::Resume: g_push_resume(peer, &vehicle, &component); break;
    case ActKind::StepIn: g_push_step_in(peer, &vehicle, &component); break;
    case ActKind::StepOver: g_push_step_over(peer, &vehicle, &component); break;
    case ActKind::StepOut: g_push_step_out(peer, &vehicle, &component); break;
    case ActKind::Undo: g_push_undo(peer, &vehicle, &component); break;
    case ActKind::Breakpoint: { int32_t id = a.id; g_push_breakpoint(peer, &vehicle, &component, &id); break; }
    }
}

void hk_update_ui(void* a, void* b, void* client, void* d, void* e, void* mc, void* g_, void* h) {
    reinterpret_cast<update_ui_t>(h_update_ui.original)(a, b, client, d, e, mc, g_, h);
    g_editor_seen = now_seconds();
    if (!g_ready || !mc || !client || !anymaker::readable(mc, 0x290) || !anymaker::readable(client, off::client_peer_data + 8)) return;
    auto* m = static_cast<const uint8_t*>(mc);
    int32_t vehicle = prop_s32(m, off::mc_vehicle_id), component = prop_s32(m, off::mc_component_id);
    std::vector<Action> actions;
    bool open;
    {
        std::lock_guard lock(g_mutex);
        actions.swap(g.actions);
        open = g.open;
        if (vehicle != g.vehicle || component != g.component) actions.clear();   // they were meant for another microcontroller
    }
    void* peer = static_cast<uint8_t*>(client) + off::client_peer_data;
    for (auto& act : actions) run_action(peer, vehicle, component, act);
    // The tree walk calls a few game methods per element: only while the Code view is up, a few times a second.
    if (!open && g_last.valid && g_last.vehicle == vehicle && g_last.component == component) {
        std::lock_guard lock(g_mutex);
        g.snap.vehicle = vehicle; g.snap.component = component;
        return;
    }
    if (--g_walk_countdown > 0 && actions.empty() && g_last.vehicle == vehicle && g_last.component == component) {
        // cheap fields every frame
        g_last.is_step = prop_bool(m, off::mc_is_step);
        g_last.load_error = prop_bool(m, off::mc_load_error);
        g_last.can_undo = prop_bool(m, off::mc_can_undo);
    } else {
        g_walk_countdown = 6;
        Snapshot s;
        std::map<std::string, std::string> types;   // declared types, for reading debug values
        {
            std::lock_guard lock(g_mutex);
            types = g.live_symbols.vars;
            for (auto& l : g.live_symbols.lists) types[l] = "list";
        }
        read_snapshot(m, s, types);
        g_last = std::move(s);
    }
    std::lock_guard lock(g_mutex);
    g.snap = g_last;
}

void hk_save_data(void* client, void* event, uint8_t* msg) {
    if (msg && anymaker::readable(msg, 0x28)) {
        std::string name = gc_str(msg, off::msg_filename);
        if (name.rfind(kTempScript, 0) == 0) {   // our read-back: keep it out of the player's saved scripts
            std::string data = gc_str(msg, off::msg_data);
            std::lock_guard lock(g_mutex);
            if (g.fetching && name != fetch_name(g.vehicle, g.component)) log(1, "dropped a late program read (%s)", name.c_str());
            else if (g.fetching) {
                g.fetching = false;
                Pretty p = prettify(data);
                g.raw = !p.ok;
                g.ed.set_text(p.text);
                if (p.ok) g.applied_map = p.canon_to_src;
                else {   // untranslated: game line i is editor line i
                    g.applied_map.clear();
                    int n = 0;
                    for (char c : data) if (c == '\n') ++n;
                    for (int i = 0; i < n; ++i) g.applied_map.push_back(i);
                }
                g.applied_rev = g.ed.revision();
                g.loaded = true;
                load_graph(true);
                g.scroll_line = 0;
                g.follow_cursor = true;
                if (p.ok) set_message("Program loaded. Edit it, then Apply (Ctrl+Enter).", 0xff8fd18f);
                else set_message("Some of this program can't be translated, so it's shown in the game's own text. It still applies.", 0xffffc400);
                log(0, "program read: %d bytes, %s", int(data.size()), p.ok ? "translated" : "shown untranslated");
                if (!p.ok && !p.diags.empty()) log(1, "translation stopped at line %d: %s", p.diags[0].line + 1, p.diags[0].msg.c_str());
            }
            return;
        }
    }
    reinterpret_cast<save_data_t>(h_save_data.original)(client, event, msg);
}

// ================================================================== layout
struct Box {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
constexpr int kTabCount = 4;
enum Btn { kApply, kReload, kUndo, kPause, kStepIn, kStepOver, kStepOut, kCopy, kPaste, kMode, kClose, kBtnCount };
const wchar_t* kBtnLabel[kBtnCount] = {L"▶ Apply", L"Reload", L"Undo", L"Pause", L"Step in", L"Step over", L"Step out", L"Copy all", L"Paste", L"Nodes", L"×"};
struct Layout {
    float s = 1, font = 15, line_h = 20, cw = 9;
    Box button, panel, toolbar, btn[kBtnCount], editor, gutter, text, diag, side, tabs[kTabCount], side_body;
    int rows = 1, cols = 1;
};
Layout layout(float w, float h) {
    Layout l;
    l.s = std::clamp(h / 1080.f, .65f, 1.75f);
    float s = l.s, m = 20 * s;
    l.button = {w - m - 170 * s - 10 * s - 120 * s, m, 120 * s, 32 * s};   // left of GpsPart's "GPS outputs" button
    l.panel = {m, m + 40 * s, w - 2 * m, h - m - (m + 40 * s)};
    l.toolbar = {l.panel.x, l.panel.y, l.panel.w, 44 * s};
    float bx = l.panel.x + 10 * s, by = l.panel.y + 7 * s, bh = 30 * s;
    const float widths[kBtnCount] = {96, 76, 64, 84, 76, 92, 84, 84, 70, 104, 30};
    for (int i = 0; i < kBtnCount; ++i) {
        if (i == kPause || i == kCopy || i == kMode) bx += 14 * s;
        float bw = widths[i] * s;
        l.btn[i] = {bx, by, bw, bh};
        bx += bw + 6 * s;
    }
    l.btn[kClose] = {l.panel.x + l.panel.w - 40 * s, by, 30 * s, bh};
    float top = l.toolbar.y + l.toolbar.h;
    float side_w = std::min(420 * s, l.panel.w * .34f);
    l.side = {l.panel.x + l.panel.w - side_w, top, side_w, l.panel.y + l.panel.h - top};
    for (int i = 0; i < kTabCount; ++i) l.tabs[i] = {l.side.x + 8 * s + i * (side_w - 16 * s) / kTabCount, top + 6 * s, (side_w - 16 * s) / kTabCount - 4 * s, 28 * s};
    l.side_body = {l.side.x + 8 * s, top + 40 * s, side_w - 16 * s, l.side.h - 48 * s};
    l.font = 15 * s;
    l.line_h = 20 * s;
    l.cw = std::round(l.font * .64f * 4) / 4;
    float diag_h = 5 * 18 * s + 12 * s;
    l.editor = {l.panel.x + 8 * s, top + 6 * s, l.side.x - l.panel.x - 16 * s, l.panel.y + l.panel.h - top - diag_h - 14 * s};
    l.gutter = {l.editor.x, l.editor.y, 58 * s, l.editor.h};
    l.text = {l.editor.x + l.gutter.w + 8 * s, l.editor.y + 4 * s, l.editor.w - l.gutter.w - 12 * s, l.editor.h - 8 * s};
    l.diag = {l.editor.x, l.editor.y + l.editor.h + 6 * s, l.editor.w, diag_h};
    l.rows = std::max(1, int(l.text.h / l.line_h));
    l.cols = std::max(1, int(l.text.w / l.cw));
    return l;
}

// ================================================================== helpers (g_mutex held)
void recompile() {
    if (g.compiled_rev == g.ed.revision()) return;
    g.compiled_rev = g.ed.revision();
    g.live = compile(g.ed.text());
    g.live_symbols = Symbols{};   // names for the side panel and colours, even while the code has errors
    std::vector<Diag> ignore;
    auto tokens = tokenize(g.ed.text(), ignore);
    Parser parser(tokens, ignore);
    auto prog = parser.program();
    collect(prog, g.live_symbols);
}
bool mapping_live() {
    return g.loaded && g.applied_rev == g.ed.revision() && !g.snap.row_element.empty() && g.snap.row_element.size() == g.applied_map.size();
}
int row_to_line(int row) { return row >= 0 && row < int(g.applied_map.size()) ? g.applied_map[size_t(row)] : -1; }
int line_to_element(int line) {
    if (!mapping_live()) return -1;
    for (size_t i = 0; i < g.applied_map.size(); ++i) if (g.applied_map[i] == line && g.snap.row_element[i] >= 0) return g.snap.row_element[i];
    return -1;
}
bool line_has_breakpoint(int line) {
    if (!mapping_live()) return false;
    for (size_t i = 0; i < g.applied_map.size(); ++i) if (g.applied_map[i] == line && g.snap.row_breakpoint[i]) return true;
    return false;
}
void request_fetch() {
    g.fetching = true;
    g.fetch_started = now_seconds();
    g.vehicle = g.snap.vehicle;
    g.component = g.snap.component;
    g.actions.push_back({ActKind::Fetch, ""});
    set_message("Reading the program...");
}
void apply() {
    if (g.nodes_mode) {
        commit_param();
        if (!g.graph_found) { set_message("Add some nodes first. Nothing was sent to the game.", 0xffffc400); return; }
        if (g.ed.revision() == g.gen_rev && !g.gen.problems.empty()) { set_message("Fix the nodes outlined in red first. Nothing was sent to the game.", 0xffff6b6b); return; }
    }
    recompile();
    if (!g.live.ok) {
        if (!g.raw) { set_message("Fix the red lines first. Nothing was sent to the game.", 0xffff6b6b); return; }
        g.actions.push_back({ActKind::Load, g.ed.text() + "\n"});   // untranslated text goes to the game as written
        g.applied_map.clear();
        for (size_t i = 0; i < g.ed.lines().size(); ++i) g.applied_map.push_back(int(i));
    } else {
        g.actions.push_back({ActKind::Load, g.live.canonical});
        g.applied_map = g.live.canon_to_src;
        g.raw = false;
    }
    g.applied_rev = g.ed.revision();
    g.applied_at = now_seconds();
    set_message("Applied. The blocks now match your code.", 0xff8fd18f);
}
void open_view(bool open) {
    if (g.open == open) return;
    g.open = open;
    g.dragging = false;
    g.ac_open = false;
    g.ed.auto_pairs = true;
    if (open) {
        bool same = g.vehicle == g.snap.vehicle && g.component == g.snap.component;
        if (!same) { g.ed.set_text(""); g.loaded = false; g.applied_map.clear(); g.raw = false; }
        if (!g.loaded || g.applied_rev == g.ed.revision()) request_fetch();   // refresh unless there are unapplied edits
        else set_message("Your unapplied edits are still here. Reload to see the blocks' version instead.", 0xffffc400);
    }
    log(0, open ? "Code view opened" : "Code view closed");
}
// ------------------------------------------------------------------ node editor model (g_mutex held)
constexpr float kNodeW = 170, kHeadH = 24, kSumH = 18, kRowH = 22;
float node_height(const nodes::NodeDef& d) { return kHeadH + kSumH + float(std::max<size_t>(std::max(d.ins.size(), d.outs.size()), 1)) * kRowH + 6; }

void frame_graph() {   // camera on the graph's top-left corner
    if (g.graph.nodes.empty()) { g.cam_x = -40; g.cam_y = -40; return; }
    float x = 1e9f, y = 1e9f;
    for (auto& n : g.graph.nodes) { x = std::min(x, n.x); y = std::min(y, n.y); }
    g.cam_x = x - 40; g.cam_y = y - 40;
}
// The editor's text changed from the game: pick up a node graph it carries.
void load_graph(bool from_game) {
    auto parsed = nodes::parse(g.ed.text());
    g.graph_found = parsed.found;
    g.graph = parsed.found ? parsed.graph : nodes::Graph{};
    g.graph_hand_edited = parsed.found && parsed.hand_edited;
    g.graph_undo.clear(); g.graph_redo.clear();
    g.selected = -1; g.edit_param = -1;
    g.gen = nodes::generate(g.graph);
    g.gen_rev = ~0u;
    if (parsed.found && !parsed.hand_edited) {
        // same program: show the code as the graph writes it, so lines map to nodes
        bool matched = g.applied_rev == g.ed.revision();
        g.ed.set_text(g.gen.code);
        g.gen_rev = g.ed.revision();
        if (matched) { g.applied_rev = g.ed.revision(); g.applied_map = compile(g.gen.code).canon_to_src; g.raw = false; }
        if (from_game) g.nodes_mode = true;
    }
    frame_graph();
}
void graph_edit() {   // call before changing the graph
    g.graph_undo.push_back(g.graph);
    if (g.graph_undo.size() > 100) g.graph_undo.erase(g.graph_undo.begin());
    g.graph_redo.clear();
}
void graph_sync() {   // the graph changed: regenerate the code
    g.graph_found = true;
    g.graph_hand_edited = false;
    g.gen = nodes::generate(g.graph);
    g.ed.set_text(g.gen.code, true);
    g.gen_rev = g.ed.revision();
    if (g.selected >= 0 && !g.graph.node(g.selected)) g.selected = -1;
}
void graph_undo_redo(bool redo) {
    auto& from = redo ? g.graph_redo : g.graph_undo;
    auto& to = redo ? g.graph_undo : g.graph_redo;
    if (from.empty()) return;
    to.push_back(g.graph);
    g.graph = from.back();
    from.pop_back();
    g.edit_param = -1;
    graph_sync();
}
void commit_param() {
    if (g.edit_param < 0) return;
    nodes::Node* n = g.graph.node(g.selected);
    const nodes::NodeDef* d = n ? nodes::find_def(n->kind) : nullptr;
    if (d && g.edit_param < int(d->params.size())) {
        auto& prm = d->params[size_t(g.edit_param)];
        std::string v = g.edit_buf;
        if (prm.kind == nodes::PK::Number) v = nodes::number_text(v);
        if (n->params[prm.key] != v) { graph_edit(); g.graph.node(g.selected)->params[prm.key] = v; graph_sync(); }
    }
    g.edit_param = -1;
}
// The selected node is drawn on top without moving it in the node list: that order is saved with the graph and
// decides which node of a feedback loop reads last tick's value, so a click must not change it.
void select_node(int id) {
    if (id != g.selected) commit_param();
    g.selected = id;
}
// Nodes back to front: list order, then the selected node.
std::vector<const nodes::Node*> stacking() {
    std::vector<const nodes::Node*> out;
    const nodes::Node* top = nullptr;
    for (auto& n : g.graph.nodes) { if (n.id == g.selected) top = &n; else out.push_back(&n); }
    if (top) out.push_back(top);
    return out;
}
void add_node(const std::string& kind, float canvas_w, float canvas_h, float z) {
    commit_param();
    graph_edit();
    float x = g.cam_x + canvas_w / z * .5f - kNodeW / 2 + float(g.added % 6) * 18, y = g.cam_y + canvas_h / z * .4f + float(g.added % 6) * 18;
    ++g.added;
    int id = g.graph.add(kind, std::round(x), std::round(y)).id;
    g.selected = id;
    graph_sync();
}
void delete_selected() {
    if (g.selected < 0 || !g.graph.node(g.selected)) return;
    g.edit_param = -1;
    graph_edit();
    g.graph.remove(g.selected);
    g.selected = -1;
    graph_sync();
}
void duplicate_selected() {
    nodes::Node* n = g.graph.node(g.selected);
    if (!n) return;
    commit_param();
    graph_edit();
    nodes::Node copy = *n;
    nodes::Node& added = g.graph.add(copy.kind, copy.x + 30, copy.y + 30);
    added.params = copy.params;
    g.selected = added.id;
    graph_sync();
}
void set_nodes_mode(bool on) {
    if (g.nodes_mode == on) return;
    commit_param();
    g.nodes_mode = on;
    g.tab = 0;
    g.side_scroll = 0;
    if (!on) return;
    if (g.graph_found && g.ed.revision() != g.gen_rev) {   // edited in the Code view since
        auto pr = nodes::parse(g.ed.text());
        if (pr.found && !pr.hand_edited) {
            g.graph = pr.graph;
            g.gen = nodes::generate(g.graph);
            if (g.ed.text() == g.gen.code) g.gen_rev = g.ed.revision();
        } else g.graph_hand_edited = true;
    }
    if (g.graph_hand_edited) set_message("This code was changed by hand. Editing the nodes replaces those changes.", 0xffffc400);
    else if (!g.graph_found) {
        bool has_code = false;
        for (auto& line : g.ed.lines()) {
            size_t a = line.find_first_not_of(" \t");
            if (a != std::string::npos && line[a] != '#') has_code = true;
        }
        if (has_code) set_message("This program was written as code. Adding a node replaces it; Code view and Ctrl+Z bring it back.", 0xffffc400);
        else set_message("Add nodes from the list on the right, then drag between ports to wire them.", 0xff8fd18f);
        frame_graph();
    }
}
// Problems per node: bad parameters, code that doesn't compile, and errors the game reports on its blocks.
std::map<int, std::string> node_problems() {
    std::map<int, std::string> out;
    for (auto& pr : g.gen.problems) out.emplace(pr.node, pr.msg);
    if (g.ed.revision() != g.gen_rev) return out;
    auto node_of_line = [&](int line) { return line >= 0 && line < int(g.gen.line_node.size()) ? g.gen.line_node[size_t(line)] : -1; };
    for (auto& d : g.live.diags) if (int n = node_of_line(d.line); n >= 0) out.emplace(n, d.msg);
    if (mapping_live())
        for (auto& [row, type] : g.snap.error_rows) if (int n = node_of_line(row_to_line(row)); n >= 0) out.emplace(n, error_text(type));
    return out;
}
std::string node_summary(const nodes::Node& n, const nodes::NodeDef& d) {
    auto P = [&](const char* k) { auto it = n.params.find(k); return it == n.params.end() ? std::string() : it->second; };
    if (n.kind.rfind("part_", 0) == 0) return P("part") + (P("channel").empty() ? "" : "." + P("channel")) + (P("type") == "whole number" ? " (whole)" : "");
    std::string s;
    for (auto& prm : d.params) s += (s.empty() ? "" : "  ") + (d.params.size() > 1 ? std::string(prm.label) + " " : std::string()) + P(prm.key);
    return s;
}

// Inserts a side-panel snippet at the cursor, indented like the current line; "$" marks where the cursor goes.
void insert_snippet(const std::string& snippet) {
    std::string indent;
    const std::string& l = g.ed.lines()[size_t(g.ed.cursor().line)];
    for (char c : l) { if (c == ' ' || c == '\t') indent += c; else break; }
    Pos at = g.ed.sel_start(), mark{-1, -1};
    int line = at.line, col = at.col;
    std::string s;
    for (char c : snippet) {
        if (c == '$') { mark = {line, col}; continue; }
        s += c;
        if (c == '\n') { s += indent; ++line; col = int(indent.size()); } else ++col;
    }
    g.ed.insert(s);
    if (mark.line >= 0) g.ed.set_cursor(mark, false);
    g.follow_cursor = true;
}

// ------------------------------------------------------------------ autocomplete
constexpr int kAcRows = 8;
void ac_update(bool open) {
    Pos c = g.ed.cursor();
    if (g.ed.has_selection() || g.nodes_mode) { g.ac_open = false; return; }
    auto r = complete::suggest(g.ed.lines()[size_t(c.line)], c.col, g.live_symbols, g.snap.part_ports);
    bool show = !r.items.empty() && (open || g.ac_open) && (r.after_dot || !r.prefix.empty());
    if (show) {
        if (!g.ac_open || r.start != g.ac.start || c.line != g.ac_line) { g.ac_sel = 0; g.ac_top = 0; }
        g.ac = r;
        g.ac_line = c.line;
        g.ac_sel = std::clamp(g.ac_sel, 0, int(r.items.size()) - 1);
    }
    g.ac_open = show;
}
void ac_accept(int index) {
    if (!g.ac_open || index < 0 || index >= int(g.ac.items.size())) return;
    complete::Item item = g.ac.items[size_t(index)];
    Pos c = g.ed.cursor();
    const std::string& l = g.ed.lines()[size_t(c.line)];
    int end = c.col;
    while (end < int(l.size()) && complete::word_char(l[size_t(end)])) ++end;   // replace the whole word
    g.ed.set_cursor({c.line, g.ac.start}, false);
    g.ed.set_cursor({c.line, end}, true);
    g.ac_open = false;
    insert_snippet(item.insert);
    if (item.insert.size() >= 2 && item.insert.compare(item.insert.size() - 2, 2, ".$") == 0) ac_update(true);   // a part: show its channels
}
// Where the popup sits: under the word, or above it near the bottom of the editor.
bool ac_box(const Layout& l, Box& b, int& count) {
    if (!g.ac_open || g.nodes_mode || g.ac_line < g.scroll_line || g.ac_line >= g.scroll_line + l.rows) return false;
    float s = l.s, row = 20 * s;
    count = std::min(int(g.ac.items.size()), kAcRows);
    if (g.ac_sel < g.ac_top) g.ac_top = g.ac_sel;
    if (g.ac_sel >= g.ac_top + count) g.ac_top = g.ac_sel - count + 1;
    g.ac_top = std::clamp(g.ac_top, 0, std::max(0, int(g.ac.items.size()) - count));
    float x = l.text.x + float(g.ed.visual_col(g.ac_line, g.ac.start) - g.scroll_col) * l.cw - 6 * s;
    float y = l.text.y + float(g.ac_line - g.scroll_line + 1) * l.line_h + 2 * s;
    float w = 380 * s, h = float(count) * row + 6 * s;
    if (y + h > l.editor.y + l.editor.h) y = l.text.y + float(g.ac_line - g.scroll_line) * l.line_h - h - 2 * s;
    x = std::clamp(x, l.editor.x, std::max(l.editor.x, l.editor.x + l.editor.w - w));
    b = {x, y, w, h};
    return true;
}

// A library script: the whole program when the editor is empty, otherwise added at the end.
void insert_script(int index) {
    if (index < 0 || index >= int(script_library().size())) return;
    const Script& sc = script_library()[size_t(index)];
    bool empty = g.ed.text().find_first_not_of(" \t\n") == std::string::npos;
    if (empty) {
        g.ed.select_all();
        g.ed.insert(sc.code);
        g.ed.set_cursor({0, 0}, false);
        set_message(std::string(sc.name) + " added. Rename the parts in it to yours (This MC lists them), then Apply.", 0xff8fd18f);
    } else {
        Pos end{int(g.ed.lines().size()) - 1, int(g.ed.lines().back().size())};
        g.ed.set_cursor(end, false);
        g.ed.insert(std::string(g.ed.lines().back().empty() ? "\n" : "\n\n") + sc.code);
        set_message(std::string(sc.name) + " added at the end. Rename its parts to yours, then Apply.", 0xff8fd18f);
    }
    g.follow_cursor = true;
}

std::string clipboard_text() {
    std::string out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) { out.resize(size_t(n - 1)); WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr); }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}
void set_clipboard(const std::string& s) {
    std::string crlf;
    for (char c : s) { if (c == '\n') crlf += '\r'; crlf += c; }
    int n = MultiByteToWideChar(CP_UTF8, 0, crlf.c_str(), -1, nullptr, 0);
    if (n <= 0 || !OpenClipboard(nullptr)) return;
    EmptyClipboard();
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size_t(n) * sizeof(wchar_t))) {
        if (auto* w = static_cast<wchar_t*>(GlobalLock(h))) {
            MultiByteToWideChar(CP_UTF8, 0, crlf.c_str(), -1, w, n);
            GlobalUnlock(h);
            if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
        } else GlobalFree(h);
    }
    CloseClipboard();
}

// Side panel rows, built the same way for drawing and clicking.
enum class SideAct { None, AddNode, EditParam, DeleteNode, DuplicateNode, InsertScript, TogglePart, PartChannel };
struct SideRow {
    std::string label, help, insert;
    bool header = false;
    uint32_t color = 0xffe8ecef;
    SideAct act = SideAct::None;
    int arg = 0;
    // PartChannel rows: the code view inserts `insert`; the node editor adds a part node for this channel
    std::string part, channel, type;
    bool sets = false;
};
// Parts tab: the setup guide, the parts wired to this microcontroller, then every part that has channels.
// The list is the game's parts (AnyAPI's item catalog) plus the parts mods add, read from the definition files they
// write under "AnyAPI and Modding" (the catalog's entries for those carry no channels). It is built at startup and
// rebuilt when the catalog grows (a world load adds the mods' parts) or the Parts tab is opened.
using PartList = std::shared_ptr<const std::vector<parts::Part>>;
std::mutex g_parts_mu;
PartList g_parts;
uint32_t g_parts_catalog_count = ~0u;

// The catalog's parts, parsed once per entry; entries added later are parsed when they appear.
std::vector<parts::Part> catalog_parts(uint32_t& seen, uint32_t& bad) {
    static std::vector<parts::Part> cache;
    static uint32_t parsed = 0;
    if (!catalog) return cache;
    uint32_t n = std::min<uint32_t>(catalog->count(), 10000);
    std::string json;
    for (; parsed < n; ++parsed) {
        AnyItemDefinitionV1 d;
        if (!catalog->copy(parsed, &d) || !(d.valid_fields & ANY_ITEM_VEHICLE_COMPONENT)) continue;
        uint32_t need = 0;
        catalog->definition_json(parsed, nullptr, 0, &need);
        if (!need || need > (1u << 20)) continue;
        json.assign(need, '\0');
        if (!catalog->definition_json(parsed, json.data(), need, &need)) continue;
        json.resize(strnlen(json.data(), json.size()));
        std::string id = d.id;
        if (id.rfind("component:", 0) == 0) id = id.substr(10);   // the catalog's prefix; mod files use the bare id
        parts::Part part{id, d.name[0] ? d.name : id, d.category, {}, {}};
        if (parts::parse_part(json, part)) cache.push_back(std::move(part)); else ++bad;
    }
    seen = n;
    return cache;
}

// Definition files in "AnyAPI and Modding" and its folders (GpsPart\gps_sensor.json, AnyEngineParts' turbo files...).
std::vector<parts::Part> mod_parts(int& files) {
    std::vector<parts::Part> out;
    files = 0;
    if (!host.game_directory) return out;
    std::error_code ec;
    std::filesystem::path root = std::filesystem::path(host.game_directory) / L"AnyAPI and Modding";
    auto read_dir = [&](const std::filesystem::path& dir, const std::string& source) {
        for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != L".json") continue;
            auto size = it->file_size(ec);
            if (ec || size == 0 || size > (4u << 20)) continue;
            std::ifstream in(it->path(), std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (text.find("\"definitions\"") == std::string::npos) continue;
            auto found = parts::parse_definition_file(text, source);
            if (found.empty()) continue;
            ++files;
            for (auto& p : found) out.push_back(std::move(p));
        }
        ec.clear();
    };
    read_dir(root, "Mods");
    for (auto it = std::filesystem::directory_iterator(root, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        if (it->is_directory(ec) && it->path().filename() != L"mods") read_dir(it->path(), it->path().filename().string());
    return out;
}

void rebuild_parts(const char* why) {
    static std::mutex build_mu;   // one build at a time; readers keep the old list meanwhile
    std::lock_guard<std::mutex> building(build_mu);
    uint32_t seen = 0, bad = 0;
    int files = 0;
    auto list = catalog_parts(seen, bad);
    std::vector<parts::Part> mods;
    try { mods = mod_parts(files); } catch (...) { log(2, "PARTS_GUIDE could not read the mod part files"); }
    size_t mod_count = 0;
    for (auto& m : mods) if (!m.channels.empty()) ++mod_count;
    parts::merge_mod_parts(list, std::move(mods));
    parts::sort_parts(list);
    size_t channels = 0;
    for (auto& p : list) channels += p.channels.size();
    log(1, "PARTS_GUIDE parts_with_channels=%zu channels=%zu mod_parts=%zu mod_files=%d unreadable=%u (%s)", list.size(), channels,
        mod_count, files, bad, why);
    std::lock_guard<std::mutex> lock(g_parts_mu);
    g_parts = std::make_shared<const std::vector<parts::Part>>(std::move(list));
    g_parts_catalog_count = seen;
}
PartList all_parts() {
    uint32_t now = catalog ? std::min<uint32_t>(catalog->count(), 10000) : 0;
    {
        std::lock_guard<std::mutex> lock(g_parts_mu);
        if (g_parts && now == g_parts_catalog_count) return g_parts;
    }
    rebuild_parts(g_parts ? "catalog grew" : "first use");
    std::lock_guard<std::mutex> lock(g_parts_mu);
    return g_parts;
}
const parts::Channel* find_channel(const std::vector<parts::Part>& list, const std::string& name) {
    for (auto& p : list)
        for (auto& c : p.channels) if (c.name == name) return &c;
    return nullptr;
}
void parts_rows(std::vector<SideRow>& rows) {
    constexpr size_t kWrap = 52;
    constexpr uint32_t kDimText = 0xff8b96a0, kCode = 0xff8fd18f;
    rows.push_back({"Set up a microcontroller", "", "", true});
    for (auto& line : parts::setup_guide()) {
        for (auto& w : parts::wrap(line.text, kWrap)) rows.push_back({w, "", "", false, 0xffe8ecef});
        if (line.code) {
            std::string first = line.code;
            first = first.substr(0, first.find('\n'));
            first.erase(std::remove(first.begin(), first.end(), '$'), first.end());
            rows.push_back({"   " + first, "click to insert", line.code, false, kCode});
        }
    }
    PartList snapshot = all_parts();
    const std::vector<parts::Part>& list = *snapshot;
    rows.push_back({"Wired to this MC", "", "", true});
    if (g.snap.part_ports.empty()) rows.push_back({"Nothing wired yet", "", "", false, kDimText});
    for (auto& pp : g.snap.part_ports) {
        if (pp.part.empty() || pp.channel.empty()) continue;
        const parts::Channel* c = find_channel(list, pp.channel);
        std::string help = std::string(pp.input ? "read" : "set") + (c && !c->type.empty() ? ", " + c->type : "");
        SideRow r{pp.part + "." + pp.channel, help, pp.part + "." + pp.channel, false, pp.input ? 0xff7cc8ff : 0xffffb86c, SideAct::PartChannel};
        r.part = pp.part; r.channel = pp.channel; r.type = c ? c->type : ""; r.sets = !pp.input;
        rows.push_back(r);
        if (c && !c->doc.empty())
            for (auto& w : parts::wrap(c->doc, kWrap)) rows.push_back({"   " + w, "", "", false, kDimText});
    }
    if (list.empty()) {
        rows.push_back({"Every part", "", "", true});
        rows.push_back({"The part list needs AnyAPI's item catalog, which didn't load.", "", "", false, kDimText});
        return;
    }
    rows.push_back({"Every part with channels (" + std::to_string(list.size()) + ")", "", "", true});
    rows.push_back({"Click a part to see what code can read and set on it.", "", "", false, kDimText});
    std::string category;
    for (size_t i = 0; i < list.size(); ++i) {
        auto& p = list[i];
        if (p.category != category) { category = p.category; if (!category.empty()) rows.push_back({category, "", "", false, 0xffc586c0}); }
        bool open = g.part_open == int(i);
        std::string count = std::to_string(p.channels.size()) + (p.channels.size() == 1 ? " channel" : " channels");
        if (!p.source.empty()) count += " \xb7 " + p.source;
        rows.push_back({(open ? "- " : "+ ") + p.name, count, "", false, 0xfff1d36b, SideAct::TogglePart, int(i)});
        if (!open) continue;
        std::string name = parts::code_name(p.name);
        for (auto& c : p.channels) {
            const char* hint = parts::type_hint(c.type);
            std::string help = c.type + (*hint ? std::string(": ") + hint : std::string());
            if (c.dir != parts::Dir::Unknown) help = (c.dir == parts::Dir::Read ? "read, " : "set, ") + help;
            SideRow r{"   " + name + "." + c.name, help, name + "." + c.name, false, c.dir == parts::Dir::Set ? 0xffffb86c : 0xff7cc8ff, SideAct::PartChannel};
            r.part = name; r.channel = c.name; r.type = c.type; r.sets = c.dir == parts::Dir::Set;
            rows.push_back(r);
            for (auto& w : parts::wrap(c.doc, kWrap)) rows.push_back({"      " + w, "", "", false, kDimText});
        }
    }
}

std::vector<SideRow> side_rows() {
    std::vector<SideRow> rows;
    if (g.nodes_mode && g.tab == 0) {
        const char* group = "";
        for (size_t i = 0; i < nodes::library().size(); ++i) {
            auto& d = nodes::library()[i];
            if (strcmp(group, d.group)) { group = d.group; rows.push_back({d.group, "", "", true}); }
            SideRow r{d.title, d.help, "", false, 0xffe8ecef, SideAct::AddNode, int(i)};
            rows.push_back(r);
        }
        return rows;
    }
    if (g.nodes_mode && g.tab == 1) {
        const nodes::Node* n = g.graph.node(g.selected);
        const nodes::NodeDef* d = n ? nodes::find_def(n->kind) : nullptr;
        if (!d) { rows.push_back({"Nothing selected", "Click a node to change its settings", ""}); return rows; }
        rows.push_back({std::string(d->title) + "  (node " + std::to_string(n->id) + ")", "", "", true});
        rows.push_back({d->help, "", "", false, 0xff8b96a0});
        for (size_t i = 0; i < d->params.size(); ++i) {
            auto& prm = d->params[i];
            auto it = n->params.find(prm.key);
            std::string v = it == n->params.end() ? "" : it->second;
            if (int(i) == g.edit_param) v = g.edit_buf + (fmod(now_seconds(), 1.0) < .6 ? "|" : " ");
            std::string hint = prm.kind == nodes::PK::Choice ? "  (click to change)" : int(i) == g.edit_param ? "  (Enter to set)" : "  (click to edit)";
            rows.push_back({prm.label, v + hint, "", false, 0xffe8ecef, SideAct::EditParam, int(i)});
        }
        rows.push_back({"Duplicate", "Ctrl+D", "", false, 0xff8fd18f, SideAct::DuplicateNode});
        rows.push_back({"Delete node", "Delete key", "", false, 0xffff6b6b, SideAct::DeleteNode});
        return rows;
    }
    if (g.tab == 0) {
        rows.push_back({"Ready-made scripts", "", "", true});
        for (size_t i = 0; i < script_library().size(); ++i)
            rows.push_back({script_library()[i].name, script_library()[i].help, "", false, 0xfff1d36b, SideAct::InsertScript, int(i)});
        const char* group = "";
        for (auto& h : kHelp) {
            if (strcmp(group, h.group)) { group = h.group; rows.push_back({h.group, "", "", true}); }
            rows.push_back({h.label, h.help, h.insert});
        }
    } else if (g.tab == 1) {
        auto add = [&](const char* title, const std::map<std::string, std::string>& m, uint32_t color, const char* skip_kind) {
            if (m.empty()) return;
            rows.push_back({title, "", "", true});
            for (auto& [name, type] : m) {
                bool list = g.live_symbols.lists.count(name) > 0;
                if (skip_kind && strcmp(skip_kind, "list") == 0 && !list) continue;
                if (skip_kind && strcmp(skip_kind, "var") == 0 && list) continue;
                rows.push_back({name, (list ? "list of " : "") + type, name, false, color});
            }
        };
        add("Inputs", g.live_symbols.ins, 0xff7cc8ff, nullptr);
        add("Outputs", g.live_symbols.outs, 0xffffb86c, nullptr);
        add("Variables", g.live_symbols.vars, 0xffe8ecef, "var");
        add("Lists", g.live_symbols.vars, 0xffe8ecef, "list");
        if (!g.live_symbols.functions.empty()) {
            rows.push_back({"Functions", "", "", true});
            for (auto& f : g.live_symbols.functions) {
                std::string sig;
                for (auto& p : g.live_symbols.function_params[f]) sig += (sig.empty() ? "" : ", ") + p.type + " " + p.name;
                rows.push_back({f + "()", sig, f + "()", false, 0xfff1d36b});
            }
        }
        if (!g.live_symbols.types.empty()) {
            rows.push_back({"Types", "", "", true});
            for (auto& t : g.live_symbols.types) rows.push_back({t, "", t, false, 0xff6fd3c4});
        }
        if (!g.snap.ports.empty()) {
            rows.push_back({"Ports the game reports", "", "", true});
            for (auto& p : g.snap.ports) rows.push_back({p, "", "", false, 0xffaab4bd});
        }
        if (rows.empty()) rows.push_back({"Nothing defined yet", "Add one with define var, define in or define out", ""});
    } else if (g.tab == 3) {
        parts_rows(rows);
    } else {
        rows.push_back({"Live values", "", "", true});
        if (g.snap.watch.empty()) rows.push_back({"No values yet", "They appear while the program runs or is paused", ""});
        for (auto& w : g.snap.watch) rows.push_back({w, "", "", false, 0xffe8ecef});
    }
    return rows;
}

// ================================================================== drawing (render thread)
std::wstring widen(const std::string& s) {
    std::wstring w;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { w += wchar_t(c); continue; }
        int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        if (!extra || i + size_t(extra) >= s.size()) { w += wchar_t(c == 0xb7 ? 0xb7 : '?'); continue; }
        uint32_t cp = c & (0x3f >> extra);
        for (int k = 1; k <= extra; ++k) cp = (cp << 6) | ((unsigned char)s[i + size_t(k)] & 0x3f);
        i += size_t(extra);
        w += cp < 0x10000 ? wchar_t(cp) : L'?';
    }
    return w;
}
struct Painter {
    void box(const Box& b, uint32_t color, float radius = 0, uint32_t flags = 0, float stroke = 1) {
        AnyGpuCommandV1 c; c.kind = radius > 0 ? ANY_GPU_ROUND_RECT : ANY_GPU_RECT;
        c.rect[0] = b.x; c.rect[1] = b.y; c.rect[2] = b.w; c.rect[3] = b.h; c.color = color; c.radius = radius; c.flags = flags; c.stroke = stroke;
        gpu->emit(&c);
    }
    void text(float x, float y, float w, float h, const std::wstring& t, float size, uint32_t color, uint32_t flags = 0) {
        if (t.empty()) return;
        AnyGpuCommandV1 c; c.kind = ANY_GPU_TEXT; c.rect[0] = x; c.rect[1] = y; c.rect[2] = w; c.rect[3] = h;
        c.text = t.c_str(); c.text_length = uint32_t(t.size()); c.font_size = size; c.color = color; c.flags = flags | ANY_GPU_NOWRAP | ANY_GPU_VCENTER;
        gpu->emit(&c);
    }
    void line(float x0, float y0, float x1, float y1, uint32_t color, float stroke) {
        AnyGpuCommandV1 c; c.kind = ANY_GPU_LINE; c.rect[0] = x0; c.rect[1] = y0; c.rect[2] = x1; c.rect[3] = y1; c.color = color; c.stroke = stroke;
        gpu->emit(&c);
    }
    void clip(const Box& b) { AnyGpuCommandV1 c; c.kind = ANY_GPU_CLIP_PUSH; c.rect[0] = b.x; c.rect[1] = b.y; c.rect[2] = b.w; c.rect[3] = b.h; gpu->emit(&c); }
    void unclip() { AnyGpuCommandV1 c; c.kind = ANY_GPU_CLIP_POP; gpu->emit(&c); }
};

constexpr uint32_t kYellow = 0xffffc400, kPanel = 0xf2161b20, kEditorBg = 0xff0f1317, kLine = 0xff3a434d, kLight = 0xffe8ecef,
                   kDim = 0xff8b96a0, kRed = 0xffff6b6b;

// Colours for one line of code: one ARGB value per byte.
std::vector<uint32_t> colour_line(const std::string& l) {
    std::vector<uint32_t> c(l.size(), kLight);
    size_t i = 0;
    while (i < l.size()) {
        char ch = l[i];
        if (ch == '#') { for (; i < l.size(); ++i) c[i] = 0xff6a9955; break; }
        if (ch == '"') { size_t j = i + 1; while (j < l.size() && l[j] != '"') ++j; for (size_t k = i; k <= j && k < l.size(); ++k) c[k] = 0xffce9178; i = j + 1; continue; }
        if (digit(ch)) { size_t j = i; while (j < l.size() && (digit(l[j]) || l[j] == '.')) ++j; for (size_t k = i; k < j; ++k) c[k] = 0xffb5cea8; i = j; continue; }
        if (ident_start(ch)) {
            size_t j = i; while (j < l.size() && ident_char(l[j])) ++j;
            std::string w = l.substr(i, j - i);
            uint32_t col = kLight;
            Fn f;
            if (builtin_type(w)) col = 0xff4ec9b0;
            else if (w == "true" || w == "false") col = 0xff569cd6;
            else if (find_fn(w, f) || w == "rand") col = 0xffdcdcaa;
            else if (keywords().count(w)) col = 0xffc586c0;
            else if (g.live_symbols.ins.count(w)) col = 0xff7cc8ff;
            else if (g.live_symbols.outs.count(w)) col = 0xffffb86c;
            else if (g.live_symbols.functions.count(w)) col = 0xfff1d36b;
            for (size_t k = i; k < j; ++k) c[k] = col;
            i = j;
            continue;
        }
        if (std::string("(){}[],.").find(ch) != std::string::npos) c[i] = 0xffa0a8b0;
        else if (ch != ' ' && ch != '\t') c[i] = 0xffd4d4d4;
        ++i;
    }
    return c;
}

// ------------------------------------------------------------------ node canvas
// Canvas units are the graph's own; on screen they scale by z = layout scale * zoom.
struct NodeHit { int node = -1, port = -1; bool input = false, on_port = false; };
float canvas_z(const Layout& l) { return l.s * g.zoom; }
void port_pos(const Layout& l, const nodes::Node& n, int port, bool input, float& sx, float& sy) {
    float z = canvas_z(l);
    sx = l.editor.x + ((input ? n.x : n.x + kNodeW) - g.cam_x) * z;
    sy = l.editor.y + (n.y + kHeadH + kSumH + (float(port) + .5f) * kRowH - g.cam_y) * z;
}
NodeHit hit_node(const Layout& l, float x, float y) {
    NodeHit h;
    float z = canvas_z(l), r = std::max(9.f, 9 * z);
    auto order = stacking();
    for (auto top = order.rbegin(); top != order.rend(); ++top) {
        const nodes::Node* it = *top;
        const nodes::NodeDef* d = nodes::find_def(it->kind);
        if (!d) continue;
        for (int side = 0; side < 2; ++side) {
            size_t count = side == 0 ? d->ins.size() : d->outs.size();
            for (size_t i = 0; i < count; ++i) {
                float px, py;
                port_pos(l, *it, int(i), side == 0, px, py);
                if (std::abs(x - px) <= r && std::abs(y - py) <= r * .9f) return {it->id, int(i), side == 0, true};
            }
        }
        float nx = l.editor.x + (it->x - g.cam_x) * z, ny = l.editor.y + (it->y - g.cam_y) * z;
        if (x >= nx && x < nx + kNodeW * z && y >= ny && y < ny + node_height(*d) * z) return {it->id, -1, false, false};
    }
    return h;
}
std::map<std::string, std::string> live_values() {
    std::map<std::string, std::string> v;
    if (!g.loaded || g.applied_rev != g.ed.revision() || g.ed.revision() != g.gen_rev) return v;   // only for the applied graph
    for (auto& w : g.snap.watch) {
        size_t eq = w.find(" = ");
        if (eq != std::string::npos) v[w.substr(0, eq)] = w.substr(eq + 3);
    }
    return v;
}
uint32_t group_colour(const char* group) {
    std::string s = group;
    if (s == "Parts") return 0xff2f6f8f;
    if (s == "Maths") return 0xff3a5f9a;
    if (s == "Compare") return 0xff7a5a1f;
    if (s == "Logic") return 0xff2f7a4a;
    if (s == "Memory") return 0xff6a3f8a;
    if (s == "Timing") return 0xff8a3f5f;
    return 0xff6a6a2f;
}
void ellipse(float cx, float cy, float r, uint32_t color, bool stroke = false, float width = 1) {
    AnyGpuCommandV1 c; c.kind = ANY_GPU_ELLIPSE; c.rect[0] = cx - r; c.rect[1] = cy - r; c.rect[2] = 2 * r; c.rect[3] = 2 * r; c.color = color;
    if (stroke) { c.flags = ANY_GPU_STROKE; c.stroke = width; }
    gpu->emit(&c);
}
void wire_curve(Painter& p, float x0, float y0, float x1, float y1, uint32_t color, float width) {
    float dx = std::max(std::abs(x1 - x0) * .5f, 40 * width);
    float px = x0, py = y0;
    for (int i = 1; i <= 16; ++i) {
        float t = float(i) / 16, u = 1 - t;
        float x = u * u * u * x0 + 3 * u * u * t * (x0 + dx) + 3 * u * t * t * (x1 - dx) + t * t * t * x1;
        float y = u * u * u * y0 + 3 * u * u * t * y0 + 3 * u * t * t * y1 + t * t * t * y1;
        p.line(px, py, x, y, color, width);
        px = x; py = y;
    }
}
constexpr uint32_t kNumWire = 0xff7cc8ff, kBoolWire = 0xff8fd18f;

void draw_node_canvas(const Layout& l, Painter& p) {
    float s = l.s, z = canvas_z(l);
    p.box(l.editor, kEditorBg, 6 * s);
    p.clip(l.editor);
    // grid
    float step = 40 * z;
    if (step >= 8) {
        float gx = l.editor.x - std::fmod(g.cam_x * z, step); if (gx > l.editor.x) gx -= step;
        float gy = l.editor.y - std::fmod(g.cam_y * z, step); if (gy > l.editor.y) gy -= step;
        for (float x = gx; x < l.editor.x + l.editor.w; x += step) p.line(x, l.editor.y, x, l.editor.y + l.editor.h, 0xff171d22, 1);
        for (float y = gy; y < l.editor.y + l.editor.h; y += step) p.line(l.editor.x, y, l.editor.x + l.editor.w, y, 0xff171d22, 1);
    }
    auto values = live_values();
    auto problems = node_problems();
    // wires
    for (auto& w : g.graph.wires) {
        const nodes::Node *a = g.graph.node(w.from), *b = g.graph.node(w.to);
        const nodes::NodeDef* da = a ? nodes::find_def(a->kind) : nullptr;
        if (!a || !b || !da || w.from_port >= int(da->outs.size())) continue;
        float x0, y0, x1, y1;
        port_pos(l, *a, w.from_port, false, x0, y0);
        port_pos(l, *b, w.to_port, true, x1, y1);
        wire_curve(p, x0, y0, x1, y1, da->outs[size_t(w.from_port)].is_bool ? kBoolWire : kNumWire, 2 * z);
    }
    // the wire being dragged
    if (g.ndrag == State::NDrag::Wire) {
        if (const nodes::Node* n = g.graph.node(g.drag_node)) {
            const nodes::NodeDef* d = nodes::find_def(n->kind);
            float px, py;
            port_pos(l, *n, g.drag_port, g.drag_from_input, px, py);
            bool is_bool = d && (g.drag_from_input ? d->ins[size_t(g.drag_port)].is_bool : d->outs[size_t(g.drag_port)].is_bool);
            if (g.drag_from_input) wire_curve(p, g.mouse_x, g.mouse_y, px, py, is_bool ? kBoolWire : kNumWire, 2 * z);
            else wire_curve(p, px, py, g.mouse_x, g.mouse_y, is_bool ? kBoolWire : kNumWire, 2 * z);
        }
    }
    // nodes, back to front
    float fs = 13 * z, text_s = 11.5f * z;
    bool labels = z >= .5f;
    for (const nodes::Node* np : stacking()) {
        const nodes::Node& n = *np;
        const nodes::NodeDef* d = nodes::find_def(n.kind);
        if (!d) continue;
        float nx = l.editor.x + (n.x - g.cam_x) * z, ny = l.editor.y + (n.y - g.cam_y) * z;
        float nw = kNodeW * z, nh = node_height(*d) * z;
        if (nx > l.editor.x + l.editor.w || ny > l.editor.y + l.editor.h || nx + nw + 80 * z < l.editor.x || ny + nh + 30 * z < l.editor.y) continue;
        auto pr = problems.find(n.id);
        p.box({nx, ny, nw, nh}, 0xf0222930, 6 * z);
        p.box({nx, ny, nw, kHeadH * z}, group_colour(d->group), 6 * z);
        p.box({nx, ny + kHeadH * z - 6 * z, nw, 6 * z}, 0xf0222930);
        if (labels) {
            p.text(nx + 8 * z, ny, nw - 16 * z, kHeadH * z - 4 * z, widen(d->title), fs, kLight, ANY_GPU_BOLD);
            p.text(nx + 8 * z, ny + kHeadH * z - 4 * z, nw - 16 * z, kSumH * z, widen(node_summary(n, *d)), text_s, 0xffd8c27a);
        }
        uint32_t outline = n.id == g.selected ? kYellow : pr != problems.end() ? kRed : 0xff3a434d;
        p.box({nx, ny, nw, nh}, outline, 6 * z, ANY_GPU_STROKE, (n.id == g.selected || pr != problems.end() ? 2 : 1) * z);
        for (size_t i = 0; i < d->ins.size(); ++i) {
            float px, py;
            port_pos(l, n, int(i), true, px, py);
            bool wired = g.graph.wire_into(n.id, int(i));
            uint32_t col = d->ins[i].is_bool ? kBoolWire : kNumWire;
            ellipse(px, py, 5 * z, wired ? col : 0xff222930);
            ellipse(px, py, 5 * z, col, true, 1.5f * z);
            if (labels) p.text(px + 9 * z, py - kRowH * z / 2, nw / 2, kRowH * z, widen(d->ins[i].name), text_s, 0xffb8c2cb);
        }
        for (size_t i = 0; i < d->outs.size(); ++i) {
            float px, py;
            port_pos(l, n, int(i), false, px, py);
            uint32_t col = d->outs[i].is_bool ? kBoolWire : kNumWire;
            ellipse(px, py, 5 * z, col);
            if (labels && d->outs[i].name[0]) {
                std::wstring name = widen(d->outs[i].name);
                float tw = float(name.size()) * text_s * .6f;
                p.text(px - 9 * z - tw, py - kRowH * z / 2, tw, kRowH * z, name, text_s, 0xffb8c2cb);
            }
            auto v = values.find(nodes::out_var(n.id, int(i)));
            if (labels && v != values.end()) {
                std::wstring t = widen(v->second);
                float tw = float(t.size()) * text_s * .62f + 8 * z;
                Box tag{px + 8 * z, py - 9 * z - kRowH * z / 2, tw, 16 * z};
                p.box(tag, 0xe0101418, 3 * z);
                p.text(tag.x + 4 * z, tag.y, tag.w, tag.h, t, text_s, kYellow);
            }
        }
        if (pr != problems.end() && labels)
            p.text(nx, ny + nh + 2 * z, 360 * z, 16 * z, widen(pr->second), text_s, kRed);
    }
    if (g.graph.nodes.empty())
        p.text(l.editor.x, l.editor.y, l.editor.w, l.editor.h, L"No nodes yet. Pick one from Add node on the right.", 15 * s, 0xff5d6873, ANY_GPU_CENTER);
    // zoom readout
    p.text(l.editor.x + 8 * s, l.editor.y + l.editor.h - 22 * s, 300 * s, 18 * s,
           L"Zoom " + std::to_wstring(int(std::lround(g.zoom * 100))) + L"%  •  drag empty space to pan", 11.5f * s, 0xff5d6873);
    p.unclip();
}

void draw_autocomplete(const Layout& l, Painter& p) {
    Box b;
    int count = 0;
    if (!ac_box(l, b, count)) return;
    float s = l.s, row = 20 * s;
    p.box({b.x + 3 * s, b.y + 4 * s, b.w, b.h}, 0x60000000, 5 * s);
    p.box(b, 0xfa1e252c, 5 * s);
    p.box(b, kLine, 5 * s, ANY_GPU_STROKE, 1 * s);
    for (int i = 0; i < count; ++i) {
        int idx = g.ac_top + i;
        const auto& it = g.ac.items[size_t(idx)];
        float y = b.y + 3 * s + float(i) * row;
        if (idx == g.ac_sel) p.box({b.x + 3 * s, y, b.w - 6 * s, row}, 0xff2f4f6f, 3 * s);
        p.text(b.x + 10 * s, y, b.w * .55f, row, widen(it.label), 13.5f * s, it.color, idx == g.ac_sel ? uint32_t(ANY_GPU_BOLD) : 0u);
        p.text(b.x + b.w * .55f, y, b.w * .45f - 10 * s, row, widen(it.detail), 11.5f * s, kDim);
    }
    if (int(g.ac.items.size()) > count)
        p.text(b.x, b.y + b.h, b.w - 8 * s, 16 * s, std::to_wstring(g.ac_sel + 1) + L" of " + std::to_wstring(g.ac.items.size()) + L"  •  Tab or Enter picks, Esc closes", 11 * s, 0xff5d6873);
    else
        p.text(b.x, b.y + b.h, b.w - 8 * s, 16 * s, L"Tab or Enter picks, Esc closes", 11 * s, 0xff5d6873);
}

void draw(const AnyFrameV1* frame, void*) {
    if (!frame || !gpu || !g_ready) return;
    std::lock_guard lock(g_mutex);
    g.frame_w = float(frame->width);
    g.frame_h = float(frame->height);
    bool visible = editor_open();
    if (!visible) {
        if (g.open) { g.open = false; log(0, "Code view closed with the microcontroller editor"); }
        g.editor_visible = false;
        return;
    }
    g.editor_visible = true;
    Layout l = layout(g.frame_w, g.frame_h);
    Painter p;
    float s = l.s;
    // Code button
    p.box(l.button, g.open ? 0xff2b333b : 0xf0191f25, 6 * s);
    p.box(l.button, kYellow, 6 * s, ANY_GPU_STROKE, 1.5f * s);
    p.text(l.button.x, l.button.y, l.button.w, l.button.h, g.open ? L"</> Blocks" : L"</> Code", 15 * s, kYellow, ANY_GPU_BOLD | ANY_GPU_CENTER);
    if (!g.open) return;

    double now = now_seconds();
    if (g.fetching && now - g.fetch_started > 4) { g.fetching = false; set_message("The game didn't send the program back. Press Reload to try again.", kRed); }
    if (g.applied_at > 0 && now - g.applied_at > .3 && now - g.applied_at < 3 && g.snap.load_error) {
        g.applied_at = -1;
        set_message("The game couldn't read the program. Press Undo to get the previous blocks back.", kRed);
    }
    recompile();
    {   // one log line whenever the line map and the game's blocks disagree (debug features then stay off)
        static size_t logged_rows = 0, logged_map = 0;
        size_t rows = g.snap.row_element.size(), map = g.applied_map.size();
        if (g.loaded && g.applied_rev == g.ed.revision() && rows && rows != map && (rows != logged_rows || map != logged_map)) {
            logged_rows = rows; logged_map = map;
            log(1, "line map off: the game has %d block rows, the code has %d lines of game text; breakpoints and the running line are off", int(rows), int(map));
        }
    }

    p.box(l.panel, kPanel, 8 * s);
    p.box(l.panel, kLine, 8 * s, ANY_GPU_STROKE, 1 * s);
    // toolbar
    bool paused = g.snap.is_step;
    for (int i = 0; i < kBtnCount; ++i) {
        bool enabled = true;
        if (i == kStepIn || i == kStepOver || i == kStepOut) enabled = paused;
        if (i == kUndo) enabled = g.snap.can_undo;
        uint32_t fill = i == kApply ? 0xff2f6f3a : 0xff252c33;
        p.box(l.btn[i], fill, 5 * s);
        std::wstring label = i == kPause ? (paused ? L"▶ Resume" : L"⏸ Pause") : i == kMode ? (g.nodes_mode ? L"Code" : L"Nodes") : kBtnLabel[i];
        p.text(l.btn[i].x, l.btn[i].y, l.btn[i].w, l.btn[i].h, label, (i == kClose ? 20 : 13.5f) * s, enabled ? kLight : 0xff5a636c, ANY_GPU_CENTER | (i == kApply ? uint32_t(ANY_GPU_BOLD) : 0u));
    }
    // status, right of the buttons
    {
        std::wstring st;
        uint32_t color = kDim;
        if (paused && mapping_live()) {
            int line = row_to_line(g.snap.step_row);
            st = line >= 0 ? L"Paused at line " + std::to_wstring(line + 1) : L"Paused";
            color = kYellow;
        } else if (paused) { st = L"Paused (apply your edits to follow the running line)"; color = kYellow; }
        else if (g.fetching) st = L"Reading...";
        else if (g.loaded && g.applied_rev != g.ed.revision()) { st = L"Edited • Ctrl+Enter applies"; color = 0xffffd27f; }
        else if (g.loaded) { st = L"Blocks match the code"; color = 0xff8fd18f; }
        float x = l.btn[kMode].x + l.btn[kMode].w + 14 * s;
        p.text(x, l.toolbar.y, l.btn[kClose].x - x - 8 * s, l.toolbar.h, st, 13 * s, color);
    }
    std::vector<int> error_lines;
    std::vector<std::string> game_errors;
    for (auto& [row, type] : g.snap.error_rows) {
        int line = mapping_live() ? row_to_line(row) : -1;
        error_lines.push_back(line);
        game_errors.push_back((line >= 0 ? "Line " + std::to_string(line + 1) + ": " : std::string("Game: ")) + error_text(type));
    }
    if (g.nodes_mode) draw_node_canvas(l, p);
    else {
    // editor
    p.box(l.editor, kEditorBg, 6 * s);
    const auto& lines = g.ed.lines();
    int total = int(lines.size());
    Pos cur = g.ed.cursor();
    if (g.follow_cursor) {
        if (cur.line < g.scroll_line) g.scroll_line = cur.line;
        if (cur.line >= g.scroll_line + l.rows) g.scroll_line = cur.line - l.rows + 1;
        int vc = g.ed.visual_col(cur.line, cur.col);
        if (vc < g.scroll_col) g.scroll_col = std::max(0, vc - 8);
        if (vc >= g.scroll_col + l.cols - 2) g.scroll_col = vc - l.cols + 8;
        g.follow_cursor = false;
    }
    if (paused && mapping_live()) {   // keep the running line on screen
        int line = row_to_line(g.snap.step_row);
        if (line >= 0 && (line < g.scroll_line || line >= g.scroll_line + l.rows)) g.scroll_line = std::max(0, line - l.rows / 3);
    }
    g.scroll_line = std::clamp(g.scroll_line, 0, std::max(0, total - 1));
    p.clip(l.editor);
    int step_line = paused && mapping_live() ? row_to_line(g.snap.step_row) : -1;
    Pos a = g.ed.sel_start(), b = g.ed.sel_end();
    int glyphs = 0;
    for (int r = 0; r < l.rows + 1; ++r) {
        int li = g.scroll_line + r;
        if (li >= total) break;
        float y = l.text.y + r * l.line_h;
        const std::string& text = lines[size_t(li)];
        if (li == step_line) p.box({l.editor.x, y, l.editor.w, l.line_h}, 0x55ffc400);
        else if (std::find(error_lines.begin(), error_lines.end(), li) != error_lines.end()) p.box({l.editor.x, y, l.editor.w, l.line_h}, 0x40ff4040);
        else if (li == cur.line && !g.ed.has_selection()) p.box({l.text.x - 4 * s, y, l.text.w + 4 * s, l.line_h}, 0x14ffffff);
        // selection
        if (g.ed.has_selection() && li >= a.line && li <= b.line) {
            int c0 = li == a.line ? g.ed.visual_col(li, a.col) : 0;
            int c1 = li == b.line ? g.ed.visual_col(li, b.col) : g.ed.visual_col(li, int(text.size())) + 1;
            float x0 = l.text.x + (c0 - g.scroll_col) * l.cw, x1 = l.text.x + (c1 - g.scroll_col) * l.cw;
            x0 = std::max(x0, l.text.x);
            if (x1 > x0) p.box({x0, y, x1 - x0, l.line_h}, 0x663a6ea5);
        }
        // gutter: breakpoint dot and line number
        bool bp = line_has_breakpoint(li);
        if (bp) { AnyGpuCommandV1 c; c.kind = ANY_GPU_ELLIPSE; c.rect[0] = l.gutter.x + 6 * s; c.rect[1] = y + 5 * s; c.rect[2] = 10 * s; c.rect[3] = 10 * s; c.color = 0xffe04848; gpu->emit(&c); }
        else if (mapping_live() && line_to_element(li) >= 0 && li == cur.line) {
            AnyGpuCommandV1 c; c.kind = ANY_GPU_ELLIPSE; c.flags = ANY_GPU_STROKE; c.stroke = 1 * s; c.rect[0] = l.gutter.x + 6 * s; c.rect[1] = y + 5 * s; c.rect[2] = 10 * s; c.rect[3] = 10 * s; c.color = 0x80e04848; gpu->emit(&c);
        }
        if (li == step_line) p.text(l.gutter.x + 2 * s, y, 14 * s, l.line_h, L"▶", 12 * s, kYellow);
        p.text(l.gutter.x + 16 * s, y, l.gutter.w - 20 * s, l.line_h, std::to_wstring(li + 1), 12.5f * s, li == cur.line ? kLight : 0xff5d6873);
        // code, one glyph per cell so columns line up
        auto colours = colour_line(text);
        int v = 0;
        for (size_t k = 0; k < text.size(); ++k) {
            char ch = text[k];
            int nv = ch == '\t' ? (v / kTabWidth + 1) * kTabWidth : v + 1;
            if (ch != ' ' && ch != '\t' && v >= g.scroll_col && v < g.scroll_col + l.cols + 1 && glyphs < 6000) {
                float x = l.text.x + (v - g.scroll_col) * l.cw;
                wchar_t w[2] = {wchar_t((unsigned char)ch), 0};
                p.text(x - l.cw * .5f, y, l.cw * 2, l.line_h, w, l.font, colours[k], ANY_GPU_CENTER);
                ++glyphs;
            }
            v = nv;
        }
        // red underline for problems on this line
        for (auto& d : g.live.diags) {
            if (d.line != li) continue;
            int c0 = g.ed.visual_col(li, d.col), c1 = std::max(c0 + 1, g.ed.visual_col(li, d.col + d.len));
            float x0 = l.text.x + (c0 - g.scroll_col) * l.cw, x1 = l.text.x + (c1 - g.scroll_col) * l.cw;
            p.line(x0, y + l.line_h - 2 * s, x1, y + l.line_h - 2 * s, kRed, 2 * s);
        }
    }
    // caret
    if (cur.line >= g.scroll_line && cur.line < g.scroll_line + l.rows + 1 && fmod(now, 1.0) < .6) {
        float x = l.text.x + (g.ed.visual_col(cur.line, cur.col) - g.scroll_col) * l.cw;
        float y = l.text.y + (cur.line - g.scroll_line) * l.line_h;
        if (x >= l.text.x - 1) p.box({x - 1 * s, y + 2 * s, 2 * s, l.line_h - 4 * s}, kYellow);
    }
    p.unclip();
    p.line(l.gutter.x + l.gutter.w, l.editor.y + 4 * s, l.gutter.x + l.gutter.w, l.editor.y + l.editor.h - 4 * s, 0xff222a31, 1 * s);
    }
    // problems / status strip
    p.box(l.diag, 0xff12171b, 6 * s);
    {
        struct Row { std::string text; uint32_t color; int line; };
        std::vector<Row> rows;
        if (!g.message.empty() && now - g.message_time < 8) rows.push_back({g.message, g.message_color, -1});
        if (g.nodes_mode) {
            for (auto& [id, msg] : node_problems()) {
                const nodes::Node* n = g.graph.node(id);
                const nodes::NodeDef* d = n ? nodes::find_def(n->kind) : nullptr;
                rows.push_back({std::string(d ? d->title : "Node") + " (node " + std::to_string(id) + "): " + msg, kRed, -2 - id});
            }
            if (g.graph_found && g.ed.revision() != g.gen_rev && g.graph_hand_edited) rows.push_back({"The code was changed by hand, so the nodes may not show all of it.", 0xffffc400, -1});
        } else {
        for (auto& d : g.live.diags) rows.push_back({"Line " + std::to_string(d.line + 1) + ": " + d.msg, kRed, d.line});
        for (size_t i = 0; i < game_errors.size(); ++i) rows.push_back({game_errors[i], 0xffff9a6b, error_lines[i]});
        }
        if (rows.empty()) rows.push_back({"No problems found.", 0xff8fd18f, -1});
        g_diag_lines.clear();
        for (auto& r : rows) g_diag_lines.push_back(r.line);
        float y = l.diag.y + 6 * s;
        for (size_t i = 0; i < rows.size() && i < 5; ++i) {
            std::string t = rows[i].text;
            if (i == 4 && rows.size() > 5) t = "+" + std::to_string(rows.size() - 4) + " more";
            p.text(l.diag.x + 12 * s, y, l.diag.w - 24 * s, 18 * s, widen(t), 13 * s, rows[i].color);
            y += 18 * s;
        }
    }
    // side panel
    p.box(l.side, 0xff13181d, 6 * s);
    const wchar_t* tabs[kTabCount] = {L"Commands", L"This MC", L"Watch", L"Parts"};
    if (g.nodes_mode) { tabs[0] = L"Add node"; tabs[1] = L"Selected"; }
    for (int i = 0; i < kTabCount; ++i) {
        p.box(l.tabs[i], g.tab == i ? 0xff2b333b : 0xff1b2127, 5 * s);
        p.text(l.tabs[i].x, l.tabs[i].y, l.tabs[i].w, l.tabs[i].h, tabs[i], 13 * s, g.tab == i ? kYellow : kDim, ANY_GPU_CENTER | (g.tab == i ? uint32_t(ANY_GPU_BOLD) : 0u));
    }
    auto rows = side_rows();
    float row_h = 22 * s;
    float max_scroll = std::max(0.f, rows.size() * row_h - l.side_body.h);
    g.side_scroll = std::clamp(g.side_scroll, 0.f, max_scroll);
    p.clip(l.side_body);
    for (size_t i = 0; i < rows.size(); ++i) {
        float y = l.side_body.y + i * row_h - g.side_scroll;
        if (y + row_h < l.side_body.y || y > l.side_body.y + l.side_body.h) continue;
        auto& r = rows[i];
        if (r.header) { p.text(l.side_body.x + 2 * s, y + 4 * s, l.side_body.w, row_h - 4 * s, widen(r.label), 13 * s, kYellow, ANY_GPU_BOLD); continue; }
        bool clickable = !r.insert.empty() || r.act != SideAct::None;
        if (r.act == SideAct::EditParam && r.arg == g.edit_param) p.box({l.side_body.x, y, l.side_body.w, row_h}, 0x403a6ea5, 3 * s);
        else if (clickable && i % 2) p.box({l.side_body.x, y, l.side_body.w, row_h}, 0x0cffffff, 3 * s);
        float name_w = r.help.empty() ? l.side_body.w - 8 * s : std::min(l.side_body.w * .45f, 170 * s);
        p.text(l.side_body.x + 8 * s, y, name_w, row_h, widen(r.label), 13 * s, r.color, clickable ? uint32_t(ANY_GPU_BOLD) : 0u);
        if (!r.help.empty()) p.text(l.side_body.x + 12 * s + name_w, y, l.side_body.w - name_w - 16 * s, row_h, widen(r.help), 12 * s, kDim);
    }
    p.unclip();
    if (g.tab == 0) p.text(l.side.x + 10 * s, l.side.y + l.side.h - 20 * s, l.side.w - 20 * s, 18 * s,
                           g.nodes_mode ? L"Click to add • drag ports to wire • wheel zooms" : L"Click to insert • F9 breakpoint • F5 pause", 11.5f * s, 0xff5d6873);
    draw_autocomplete(l, p);
}

// ================================================================== input
bool ctrl_down() { return (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000); }
bool shift_down() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }

Pos hit_text(const Layout& l, float x, float y) {
    int line = g.scroll_line + int(std::floor((y - l.text.y) / l.line_h));
    line = std::clamp(line, 0, int(g.ed.lines().size()) - 1);
    int vcol = int(std::floor((x - l.text.x) / l.cw + .5f)) + g.scroll_col;
    return {line, g.ed.byte_col(line, std::max(0, vcol))};
}
void toggle_breakpoint_line(int line) {
    int el = line_to_element(line);
    if (el < 0) {
        set_message(mapping_live() ? "No block on this line to break at." : "Apply your code first, then set breakpoints.", 0xffffc400);
        return;
    }
    g.actions.push_back({ActKind::Breakpoint, "", el});
}
void press(int b) {
    switch (b) {
    case kApply: apply(); break;
    case kReload: g.ed.set_text("", false); g.loaded = false; request_fetch(); break;
    case kUndo: if (g.snap.can_undo) { g.actions.push_back({ActKind::Undo, ""}); g.loaded = false; request_fetch(); } break;
    case kPause: g.actions.push_back({g.snap.is_step ? ActKind::Resume : ActKind::Pause, ""}); break;
    case kStepIn: if (g.snap.is_step) g.actions.push_back({ActKind::StepIn, ""}); break;
    case kStepOver: if (g.snap.is_step) g.actions.push_back({ActKind::StepOver, ""}); break;
    case kStepOut: if (g.snap.is_step) g.actions.push_back({ActKind::StepOut, ""}); break;
    case kCopy: set_clipboard(g.ed.text()); set_message("Copied the whole program.", 0xff8fd18f); break;
    case kPaste: if (g.nodes_mode) { set_message("Paste works in the Code view.", 0xffffc400); break; } { std::string t = clipboard_text(); if (!t.empty()) { g.ed.insert(t); g.follow_cursor = true; } break; }
    case kMode: set_nodes_mode(!g.nodes_mode); break;
    case kClose: open_view(false); break;
    }
}

// ------------------------------------------------------------------ node canvas input
uint32_t nodes_key_down(uint32_t key) {
    bool c = ctrl_down(), sh = shift_down();
    if (g.edit_param >= 0) {
        switch (key) {
        case VK_ESCAPE: g.edit_param = -1; return 1;
        case VK_RETURN: if (c) apply(); else commit_param(); return 1;
        case VK_BACK: if (!g.edit_buf.empty()) g.edit_buf.pop_back(); return 1;
        case VK_TAB: commit_param(); return 1;
        default: break;
        }
        if (c && key == 'V') { for (char ch : clipboard_text()) if (ch >= 32 && ch < 127) g.edit_buf += ch; return 1; }
        if (!c) return 1;   // letters arrive as text input
    }
    switch (key) {
    case VK_ESCAPE:
        if (g.ndrag != State::NDrag::None) { g.ndrag = State::NDrag::None; return 1; }
        open_view(false); g_escape_held = true; return 1;
    case VK_RETURN: if (c) apply(); else if (g.selected >= 0) { g.tab = 1; g.side_scroll = 0; } return 1;
    case VK_DELETE: case VK_BACK: delete_selected(); return 1;
    case VK_HOME: frame_graph(); g.zoom = 1; return 1;
    case VK_F5: press(kPause); return 1;
    case VK_F10: press(kStepOver); return 1;
    case VK_F11: press(sh ? kStepOut : kStepIn); return 1;
    default: break;
    }
    if (c) {
        switch (key) {
        case 'Z': graph_undo_redo(sh); return 1;
        case 'Y': graph_undo_redo(true); return 1;
        case 'S': apply(); return 1;
        case 'D': duplicate_selected(); return 1;
        default: break;
        }
    }
    return 1;
}
// A Parts tab channel clicked in the node editor: a Part input node for a channel code reads, a Part output node
// for one it sets, already filled in with the part, the channel and its type.
void add_part_node(const SideRow& r) {
    bool on_off = r.type == "bool";
    std::string kind = r.sets ? (on_off ? "part_out_bool" : "part_out") : (on_off ? "part_in_bool" : "part_in");
    Layout l = layout(g.frame_w, g.frame_h);
    add_node(kind, l.editor.w, l.editor.h, canvas_z(l));
    nodes::Node* n = g.graph.node(g.selected);
    if (!n) return;
    n->params["part"] = r.part;
    n->params["channel"] = r.channel;
    if (!on_off) n->params["type"] = r.type == "integer" ? "whole number" : "number";
    graph_sync();
    set_message("Added " + r.part + "." + r.channel + (r.sets ? " (set)" : " (read)") + ". Wire it up, then Apply.", 0xff8fd18f);
}
void nodes_side_click(const SideRow& r) {
    switch (r.act) {
    case SideAct::AddNode: {
        Layout l = layout(g.frame_w, g.frame_h);
        add_node(nodes::library()[size_t(r.arg)].kind, l.editor.w, l.editor.h, canvas_z(l));
        break;
    }
    case SideAct::EditParam: {
        nodes::Node* n = g.graph.node(g.selected);
        const nodes::NodeDef* d = n ? nodes::find_def(n->kind) : nullptr;
        if (!d || r.arg >= int(d->params.size())) break;
        auto& prm = d->params[size_t(r.arg)];
        if (prm.kind == nodes::PK::Choice) {
            commit_param();
            std::vector<std::string> ch;
            for (std::string rest = prm.choices; !rest.empty();) {
                size_t bar = rest.find('|');
                ch.push_back(rest.substr(0, bar));
                rest = bar == std::string::npos ? "" : rest.substr(bar + 1);
            }
            if (ch.empty()) break;
            auto it = std::find(ch.begin(), ch.end(), n->params[prm.key]);
            std::string next = it == ch.end() || it + 1 == ch.end() ? ch.front() : *(it + 1);
            graph_edit();
            g.graph.node(g.selected)->params[prm.key] = next;
            graph_sync();
        } else if (g.edit_param == r.arg) commit_param();
        else { commit_param(); g.edit_param = r.arg; g.edit_buf = g.graph.node(g.selected)->params[prm.key]; }
        break;
    }
    case SideAct::DeleteNode: delete_selected(); break;
    case SideAct::DuplicateNode: duplicate_selected(); break;
    default: break;
    }
}
void nodes_mouse_down(const Layout& l, float x, float y, int button) {
    NodeHit h = hit_node(l, x, y);
    g.mouse_x = x; g.mouse_y = y;
    g.drag_moved = false;
    if (button == 2) {   // right-click an input: unplug it
        if (h.on_port && h.input && g.graph.wire_into(h.node, h.port)) { graph_edit(); g.graph.unwire_input(h.node, h.port); graph_sync(); }
        return;
    }
    if (h.on_port && !h.input) {
        g.ndrag = State::NDrag::Wire; g.drag_node = h.node; g.drag_port = h.port; g.drag_from_input = false;
        return;
    }
    if (h.on_port && h.input) {
        if (const nodes::Wire* w = g.graph.wire_into(h.node, h.port)) {   // pick the wire up off this input
            nodes::Wire wire = *w;
            graph_edit();
            g.graph.unwire_input(h.node, h.port);
            g.drag_moved = true;   // marks the unplug as already recorded for undo
            g.ndrag = State::NDrag::Wire; g.drag_node = wire.from; g.drag_port = wire.from_port; g.drag_from_input = false;
        } else {
            g.ndrag = State::NDrag::Wire; g.drag_node = h.node; g.drag_port = h.port; g.drag_from_input = true;
        }
        return;
    }
    if (h.node >= 0) {
        double now = now_seconds();
        static int last_node = -1;
        bool dbl = now - g.last_click < .35 && last_node == h.node;
        g.last_click = now; last_node = h.node;
        select_node(h.node);
        if (dbl) { g.tab = 1; g.side_scroll = 0; }
        const nodes::Node* n = g.graph.node(h.node);
        float z = canvas_z(l);
        g.drag_dx = (x - l.editor.x) / z + g.cam_x - n->x;
        g.drag_dy = (y - l.editor.y) / z + g.cam_y - n->y;
        g.drag_node = h.node;
        g.ndrag = State::NDrag::Move;
        return;
    }
    commit_param();
    g.selected = -1;
    g.ndrag = State::NDrag::Pan;
}
void nodes_mouse_move(const Layout& l, float x, float y) {
    float z = canvas_z(l);
    if (g.ndrag == State::NDrag::Pan) { g.cam_x -= (x - g.mouse_x) / z; g.cam_y -= (y - g.mouse_y) / z; }
    else if (g.ndrag == State::NDrag::Move) {
        if (nodes::Node* n = g.graph.node(g.drag_node)) {
            float nx = std::round((x - l.editor.x) / z + g.cam_x - g.drag_dx), ny = std::round((y - l.editor.y) / z + g.cam_y - g.drag_dy);
            if (!g.drag_moved && (nx != n->x || ny != n->y)) { graph_edit(); g.drag_moved = true; n = g.graph.node(g.drag_node); }
            if (g.drag_moved) { n->x = nx; n->y = ny; }
        }
    }
    g.mouse_x = x; g.mouse_y = y;
}
void nodes_mouse_up(const Layout& l, float x, float y) {
    auto mode = g.ndrag;
    g.ndrag = State::NDrag::None;
    if (mode == State::NDrag::Move && g.drag_moved) graph_sync();
    if (mode != State::NDrag::Wire) return;
    bool unplugged = g.drag_moved;
    NodeHit h = hit_node(l, x, y);
    bool ok = false;
    if (h.on_port && h.input != g.drag_from_input) {
        nodes::Graph trial = g.graph;
        ok = g.drag_from_input ? trial.connect(h.node, h.port, g.drag_node, g.drag_port) : trial.connect(g.drag_node, g.drag_port, h.node, h.port);
        if (ok) { if (!unplugged) graph_edit(); g.graph = trial; graph_sync(); return; }
        if (h.node == g.drag_node) set_message("A node can't wire to itself.", 0xffffc400);
        else set_message("Number ports (blue) and on/off ports (green) don't connect to each other.", 0xffffc400);
    }
    if (unplugged) graph_sync();   // dropped the picked-up wire on nothing: it stays unplugged
}
void nodes_wheel(const Layout& l, float x, float y, int notches) {
    float z = canvas_z(l);
    float cx = g.cam_x + (x - l.editor.x) / z, cy = g.cam_y + (y - l.editor.y) / z;
    g.zoom = std::clamp(g.zoom * std::pow(1.15f, float(notches)), .4f, 2.f);
    float z2 = canvas_z(l);
    g.cam_x = cx - (x - l.editor.x) / z2;
    g.cam_y = cy - (y - l.editor.y) / z2;
}

uint32_t key_down(uint32_t key) {
    if (g.nodes_mode) return nodes_key_down(key);
    if (g.ac_open && !ctrl_down()) {
        int n = int(g.ac.items.size());
        switch (key) {
        case VK_UP: g.ac_sel = (g.ac_sel + n - 1) % n; return 1;
        case VK_DOWN: g.ac_sel = (g.ac_sel + 1) % n; return 1;
        case VK_PRIOR: g.ac_sel = std::max(0, g.ac_sel - kAcRows); return 1;
        case VK_NEXT: g.ac_sel = std::min(n - 1, g.ac_sel + kAcRows); return 1;
        case VK_TAB: case VK_RETURN: ac_accept(g.ac_sel); return 1;
        case VK_ESCAPE: g.ac_open = false; g_escape_held = true; return 1;
        case VK_BACK: g.ed.backspace(false); g.follow_cursor = true; ac_update(false); return 1;
        case VK_SHIFT: case VK_CONTROL: case VK_MENU: return 1;
        default: g.ac_open = false; break;
        }
    } else if (key != VK_SHIFT && key != VK_CONTROL && key != VK_MENU) g.ac_open = false;
    using M = Editor::Move;
    bool c = ctrl_down(), sh = shift_down();
    Editor& ed = g.ed;
    g.follow_cursor = true;
    switch (key) {
    case VK_ESCAPE: open_view(false); g_escape_held = true; return 1;
    case VK_RETURN: if (c) apply(); else ed.newline(); return 1;
    case VK_BACK: ed.backspace(c); return 1;
    case VK_DELETE: ed.del(c); return 1;
    case VK_TAB: ed.indent(sh); return 1;
    case VK_LEFT: ed.move(c ? M::WordLeft : M::Left, sh); return 1;
    case VK_RIGHT: ed.move(c ? M::WordRight : M::Right, sh); return 1;
    case VK_UP: if (c) { g.scroll_line = std::max(0, g.scroll_line - 1); g.follow_cursor = false; } else ed.move(M::Up, sh); return 1;
    case VK_DOWN: if (c) { ++g.scroll_line; g.follow_cursor = false; } else ed.move(M::Down, sh); return 1;
    case VK_HOME: ed.move(c ? M::DocStart : M::Home, sh); return 1;
    case VK_END: ed.move(c ? M::DocEnd : M::End, sh); return 1;
    case VK_PRIOR: ed.move(M::PageUp, sh, layout(g.frame_w, g.frame_h).rows - 1); return 1;
    case VK_NEXT: ed.move(M::PageDown, sh, layout(g.frame_w, g.frame_h).rows - 1); return 1;
    case VK_F5: press(kPause); return 1;
    case VK_F9: toggle_breakpoint_line(ed.cursor().line); return 1;
    case VK_F10: press(kStepOver); return 1;
    case VK_F11: press(sh ? kStepOut : kStepIn); return 1;
    default: break;
    }
    if (c) {
        switch (key) {
        case 'A': ed.select_all(); return 1;
        case 'C': if (ed.has_selection()) set_clipboard(ed.selection_text()); return 1;
        case 'X': if (ed.has_selection()) { set_clipboard(ed.selection_text()); ed.backspace(); } return 1;
        case 'V': { std::string t = clipboard_text(); if (!t.empty()) ed.insert(t); return 1; }
        case 'Z': if (sh) ed.redo(); else ed.undo(); return 1;
        case 'Y': ed.redo(); return 1;
        case 'S': apply(); return 1;
        case 'D': {   // duplicate line
            int line = ed.cursor().line;
            std::string t = ed.lines()[size_t(line)];
            ed.set_cursor({line, int(t.size())}, false);
            ed.insert("\n" + t);
            return 1;
        }
        default: break;
        }
    }
    g.follow_cursor = false;
    return 1;   // the Code view owns the keyboard while open
}

uint32_t input(const AnyInputV1* e, void*) {
    if (!e || !gpu || !g_ready) return 0;
    std::lock_guard lock(g_mutex);
    auto capture = [&](bool on) { if (g.captured != on && host.capture_input) { host.capture_input(on ? 1 : 0); g.captured = on; } };
    if (e->kind == ANY_FOCUS_LOST) { g.dragging = false; g_escape_held = false; return 0; }
    if (e->kind == ANY_KEY_UP && e->key == VK_ESCAPE && g_escape_held) { g_escape_held = false; return 1; }
    if (!g.editor_visible || !editor_open() || g.frame_h <= 0) { capture(false); if (g.open) g.open = false; return 0; }
    Layout l = layout(g.frame_w, g.frame_h);
    float x = float(e->x), y = float(e->y);
    if (!g.open) {
        capture(false);
        if (e->kind == ANY_MOUSE_DOWN && l.button.contains(x, y)) { if (e->button == 1) open_view(true); capture(g.open); return 1; }
        if ((e->kind == ANY_MOUSE_UP) && l.button.contains(x, y)) return 1;
        return 0;
    }
    capture(true);
    switch (e->kind) {
    case ANY_KEY_DOWN: { uint32_t r = key_down(e->key); capture(g.open); return r; }
    case ANY_KEY_UP: return 1;
    case ANY_TEXT_INPUT:
        if (g.nodes_mode) { if (g.edit_param >= 0 && e->key >= 32 && e->key < 127 && !ctrl_down()) g.edit_buf += char(e->key); return 1; }
        if (e->key >= 32 && e->key < 127 && !ctrl_down()) {
            char ch = char(e->key);
            g.ed.type_char(ch);
            g.follow_cursor = true;
            if (complete::word_char(ch) || ch == '.') { recompile(); ac_update(true); } else g.ac_open = false;
        }
        return 1;
    case ANY_MOUSE_WHEEL: {
        int notches = e->wheel / 120;
        if (!notches) notches = e->wheel > 0 ? 1 : -1;
        if (l.side.contains(x, y)) g.side_scroll -= notches * 3 * 22 * l.s;
        else if (g.nodes_mode && l.editor.contains(x, y)) nodes_wheel(l, x, y, notches);
        else if (l.editor.contains(x, y)) {
            if (shift_down()) g.scroll_col = std::max(0, g.scroll_col - notches * 6);
            else g.scroll_line = std::clamp(g.scroll_line - notches * 3, 0, std::max(0, int(g.ed.lines().size()) - 1));
        }
        return l.panel.contains(x, y) ? 1 : 0;
    }
    case ANY_MOUSE_MOVE:
        if (g.nodes_mode) { nodes_mouse_move(l, x, y); return 0; }
        if (g.dragging) {
            Pos p = hit_text(l, x, y);
            g.ed.set_cursor(p, true);
            if (y < l.text.y) g.scroll_line = std::max(0, g.scroll_line - 1);
            if (y > l.text.y + l.text.h) { ++g.scroll_line; g.follow_cursor = true; }
        }
        return 0;
    case ANY_MOUSE_UP:
        g.dragging = false;
        if (g.nodes_mode && g.ndrag != State::NDrag::None) { nodes_mouse_up(l, x, y); return 1; }
        return l.panel.contains(x, y) || l.button.contains(x, y) ? 1 : 0;
    case ANY_MOUSE_DOWN: {
        if (l.button.contains(x, y)) { if (e->button == 1) open_view(false); capture(g.open); return 1; }
        if (!l.panel.contains(x, y)) return 0;
        if (g.nodes_mode && e->button == 2 && l.editor.contains(x, y)) { nodes_mouse_down(l, x, y, 2); return 1; }
        if (e->button != 1) return 1;
        for (int i = 0; i < kBtnCount; ++i) if (l.btn[i].contains(x, y)) { press(i); capture(g.open); return 1; }
        for (int i = 0; i < kTabCount; ++i) if (l.tabs[i].contains(x, y)) {
            if (i == 3 && g.tab != 3) std::thread([] { rebuild_parts("tab opened"); }).detach();   // picks up parts mods wrote since
            g.tab = i; g.side_scroll = 0; return 1;
        }
        if (l.side_body.contains(x, y)) {
            auto rows = side_rows();
            int i = int((y - l.side_body.y + g.side_scroll) / (22 * l.s));
            if (i >= 0 && i < int(rows.size()) && rows[size_t(i)].act == SideAct::InsertScript) insert_script(rows[size_t(i)].arg);
            else if (i >= 0 && i < int(rows.size()) && rows[size_t(i)].act == SideAct::TogglePart) {
                g.part_open = g.part_open == rows[size_t(i)].arg ? -1 : rows[size_t(i)].arg;
            }
            else if (i >= 0 && i < int(rows.size()) && rows[size_t(i)].act == SideAct::PartChannel) {
                if (g.nodes_mode) add_part_node(rows[size_t(i)]); else insert_snippet(rows[size_t(i)].insert);
            }
            else if (i >= 0 && i < int(rows.size()) && rows[size_t(i)].act != SideAct::None) nodes_side_click(rows[size_t(i)]);
            else if (i >= 0 && i < int(rows.size()) && !rows[size_t(i)].insert.empty() && !g.nodes_mode) insert_snippet(rows[size_t(i)].insert);
            return 1;
        }
        if (l.diag.contains(x, y)) {   // click a problem to jump to it
            int i = int((y - l.diag.y - 6 * l.s) / (18 * l.s));
            if (g.nodes_mode) {   // click a node's problem to select it and bring it into view
                if (i >= 0 && i < int(g_diag_lines.size()) && g_diag_lines[size_t(i)] <= -2) {
                    int id = -2 - g_diag_lines[size_t(i)];
                    if (const nodes::Node* n = g.graph.node(id)) {
                        float z = canvas_z(l);
                        g.cam_x = n->x - l.editor.w / z * .4f; g.cam_y = n->y - l.editor.h / z * .4f;
                        select_node(id);
                    }
                }
                return 1;
            }
            if (i >= 0 && i < int(g_diag_lines.size()) && g_diag_lines[size_t(i)] >= 0) {
                int line = g_diag_lines[size_t(i)];
                int col = 0;
                for (auto& d : g.live.diags) if (d.line == line) { col = d.col; break; }
                g.ed.set_cursor({line, col}, false);
                g.follow_cursor = true;
            }
            return 1;
        }
        if (g.nodes_mode && l.editor.contains(x, y)) { nodes_mouse_down(l, x, y, 1); return 1; }
        {
            Box ab;
            int count = 0;
            if (ac_box(l, ab, count)) {
                if (ab.contains(x, y)) { ac_accept(g.ac_top + std::clamp(int((y - ab.y - 3 * l.s) / (20 * l.s)), 0, count - 1)); return 1; }
                g.ac_open = false;
            }
        }
        if (l.gutter.contains(x, y)) {
            Pos p = hit_text(l, l.text.x, y);
            toggle_breakpoint_line(p.line);
            return 1;
        }
        if (l.editor.contains(x, y)) {
            Pos p = hit_text(l, x, y);
            double now = now_seconds();
            if (now - g.last_click < .35 && p.line == g.last_click_pos.line && std::abs(p.col - g.last_click_pos.col) <= 1) {
                g.ed.select_word_at(p);
                g.last_click = 0;
                return 1;
            }
            g.last_click = now;
            g.last_click_pos = p;
            g.ed.set_cursor(p, shift_down());
            g.dragging = true;
            return 1;
        }
        return 1;
    }
    default: return 0;
    }
}

// ================================================================== install (background thread)
void remove_hooks() {
    h_save_data.remove((void*)&hk_save_data);
    h_update_ui.remove((void*)&hk_update_ui);
}
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; MCCode stays off"); return; }
        void** ui_cell = route::cell(bind::client_microcontroller_update_ui);
        void** save_cell = route::cell(bind::client_on_save_script_data);
        auto f = [](const anymaker::func_desc& d) { return route::function(d); };
        void* fns[] = {f(bind::push_load), f(bind::push_save), f(bind::push_pause), f(bind::push_resume), f(bind::push_step_in),
                       f(bind::push_step_over), f(bind::push_step_out), f(bind::push_toggle_breakpoint), f(bind::push_undo)};
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        auto sd = reinterpret_cast<string_dtor_t>(native(anymaker::sym::string_dtor_rva));
        bool all = ui_cell && save_cell && sc && sd;
        for (void* p : fns) all = all && p;
        if (!all) {
            if (attempt == 20 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "%s the game functions (editor=%d save=%d load=%d save_push=%d pause=%d resume=%d steps=%d/%d/%d breakpoint=%d undo=%d string=%d/%d)",
                    attempt == 179 ? "could not locate" : "still looking for", !!ui_cell, !!save_cell, !!fns[0], !!fns[1], !!fns[2], !!fns[3], !!fns[4],
                    !!fns[5], !!fns[6], !!fns[7], !!fns[8], !!sc, !!sd);
            continue;
        }
        g_push_load = reinterpret_cast<push_string_t>(fns[0]);
        g_push_save = reinterpret_cast<push_string_t>(fns[1]);
        g_push_pause = reinterpret_cast<push_plain_t>(fns[2]);
        g_push_resume = reinterpret_cast<push_plain_t>(fns[3]);
        g_push_step_in = reinterpret_cast<push_plain_t>(fns[4]);
        g_push_step_over = reinterpret_cast<push_plain_t>(fns[5]);
        g_push_step_out = reinterpret_cast<push_plain_t>(fns[6]);
        g_push_breakpoint = reinterpret_cast<push_id_t>(fns[7]);
        g_push_undo = reinterpret_cast<push_plain_t>(fns[8]);
        g_string_ctor = sc;
        g_string_dtor = sd;
        const anymaker::func_desc* dbg[kDbgKinds] = {&bind::debug_s32_replicate, &bind::debug_f64_replicate, &bind::debug_bool_replicate,
                                                     &bind::debug_string_replicate, &bind::debug_list_replicate, &bind::debug_object_replicate};
        int found = 0;
        for (int k = 0; k < kDbgKinds; ++k) if ((g_debug_replicate[k] = route::function(*dbg[k]))) ++found;
        if (found < kDbgKinds) log(0, "found %d of %d debug value kinds; Watch reads the rest by their declared type", found, kDbgKinds);
        bool ok = h_save_data.install(save_cell, (void*)&hk_save_data) && h_update_ui.install(ui_cell, (void*)&hk_update_ui);
        if (!ok) { log(2, "a hook cell was not writable; MCCode is off"); remove_hooks(); return; }
        g_ready = true;
        log(0, "Ready after %d s. Open a microcontroller and press </> Code (top right).", attempt);
        rebuild_parts("startup");   // the game's parts and the ones mods wrote at load; a world load adds more
        return;
    }
}
}  // namespace
}  // namespace mccode

using namespace mccode;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h;
    out->id = kModId;
    out->input = input;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    if (auto services = AnyAPI_Services()) {
        auto* gd = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
        if (gd && gd->struct_size == sizeof(*gd) && gd->version == 1 && gd->emit && gd->register_renderer && gd->register_renderer(draw, nullptr)) gpu = gd;
        auto* cat = static_cast<const AnyItemCatalogV1*>(services->query("anyapi.item_catalog", 1));
        if (cat && cat->struct_size == sizeof(*cat) && cat->version == 1 && cat->count && cat->copy && cat->definition_json) catalog = cat;
    }
    if (!gpu) { log(2, "anyapi.gpu_draw is missing; MCCode needs it to draw the Code view"); return; }
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
