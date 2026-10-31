#pragma once
// Call cells of bindings reached through a call route, tolerant of a route root that matches twice.
//
// The SDK's cell_of() refuses when the code signature of a route's root matches more than once. On the laptop
// some sessions have a second copy of client_scene.create's code in memory (the root of the definition
// functions' routes), so mods adding parts found nothing and stayed off. Here every match is followed, and the
// cell is taken when all the matches that lead to a live function agree on it. (Rotary carries the same fix.)
#include <windows.h>
#include "anyapi_experimental.hpp"

namespace route {

inline bool executable(const void* p) {
    MEMORY_BASIC_INFORMATION r{};
    const DWORD x = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return p && VirtualQuery(p, &r, sizeof(r)) && r.State == MEM_COMMIT && (r.Protect & x) && !(r.Protect & PAGE_GUARD);
}

// The binding's call cell, or nullptr. `roots`, if given, gets how many times the route's root matched (-1 when
// the SDK's own lookup already worked or the binding has no route).
inline void** cell(const anymaker::func_desc& d, int* roots = nullptr) {
    if (roots) *roots = -1;
    if (!anymaker::experimental::matching_build()) return nullptr;
    if (void** c = anymaker::cell_of(d)) return c;
    const anymaker::slot_route& r = d.call_route;
    if (!r.nhops || r.nhops > 8 || !r.root_sig || !*r.root_sig) return nullptr;
    uint8_t* hits[8];
    size_t n = anymaker::find_gcl_functions(r.root_sig, hits, 8);
    if (roots) *roots = int(n);
    void** found = nullptr;
    for (size_t i = 0; i < n; ++i) {
        uint8_t* fn = hits[i];
        void** c = nullptr;
        for (uint8_t h = 0; fn && h < r.nhops; ++h) {
            c = reinterpret_cast<void**>(anymaker::read_slot(fn, r.hops[h]));
            fn = anymaker::readable(c, 8) ? reinterpret_cast<uint8_t*>(*c) : nullptr;
        }
        if (!fn || !executable(fn)) continue;
        if (found && found != c) return nullptr;   // the copies disagree: refuse rather than guess
        found = c;
    }
    return found;
}
// The function a binding's route leads to, or nullptr. Tries the SDK's own resolve first (unique code first).
// The method cell at `slot` of the type whose method table also holds `known` (a function with unique code).
// For a method whose code is byte-identical to another type's, so it has no route of its own.
inline void** sibling_cell(const anymaker::func_desc& known, int32_t slot) {
    if (!anymaker::experimental::matching_build() || slot < 0 || known.method_slot < 0) return nullptr;
    if (!known.code_sig || !*known.code_sig || !known.ti_anchor_sig || !*known.ti_anchor_sig) return nullptr;
    uint8_t* entry = anymaker::find_gcl_function(known.code_sig);
    uint8_t* hits[64];
    size_t n = entry ? anymaker::find_gcl_functions(known.ti_anchor_sig, hits, 64) : 0;
    anymaker::func_desc other = known;
    other.method_slot = slot;
    void** found = nullptr;
    for (size_t i = 0; i < n; ++i) {
        void** mine = anymaker::typeinfo_cell_from(hits[i], known);
        if (!mine || *mine != entry) continue;
        void** c = anymaker::typeinfo_cell_from(hits[i], other);
        if (!c || !executable(*c)) continue;
        if (found && found != c) return nullptr;
        found = c;
    }
    return found;
}
inline void* function(const anymaker::func_desc& d, int* roots = nullptr) {
    if (roots) *roots = -1;
    if (!anymaker::experimental::matching_build()) return nullptr;
    if (void* f = anymaker::resolve(d)) return f;
    void** c = cell(d, roots);
    return c ? *c : nullptr;
}

// Identity checks for the part definition functions, from their first code bytes (Anymaker 0.1.25; only the same
// functions of the other definition containers share them). Their routes have led to the wrong function: Rotary's
// to vehicle_component_definition_container.load (laptop, 2026-10-13), and on 2026-10-17 Turbo, which did not
// check, hooked a cell holding container.load as if it were _add_definitions, so the game was handed Turbo's
// definition file as a file path and crashed starting a world. Before calling or hooking, a mod checks that the
// add cell holds _add_definitions (or another mod's hook on it, which lives in a mod DLL) and that the load
// function is vehicle_component_definition_file.load, and otherwise looks again.
inline constexpr const char* kSigAddDefinitions =
    "53 48 81 ec 90 00 00 00 48 89 4c 24 48 48 89 54 24 50 48 8b 44 24 28 48 89 44 24 58 4c 8d 44 24 58 48 8d 44 24 60 "
    "48 89 84 24 80 00 00 00 48 8d 02 48 8b 15 c8 00 00 00";
inline constexpr const char* kSigContainerLoad = "53 48 81 ec a0 00 00 00 48 89 4c 24 40 48 89 54 24 48 48 8d 01 48 8b 1d";
inline constexpr const char* kSigFileLoad =
    "53 55 48 81 ec 98 00 00 00 48 89 4c 24 48 48 89 54 24 50 4c 89 44 24 58 48 8d 44 24 60 48 8d 1d 14 01 00 00 48 8b 2d 15";
inline bool code_is(const void* fn, const char* sig) {
    anymaker::pattern pat(sig);
    return fn && !pat.bytes.empty() && anymaker::readable(fn, pat.bytes.size()) && pat.match(static_cast<const uint8_t*>(fn));
}
// A function inside a loaded DLL other than the game: another mod's hook sitting on the cell.
inline bool in_other_module(const void* fn) {
    HMODULE m = nullptr;
    return fn && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)fn, &m) &&
           m && m != GetModuleHandleW(nullptr);
}
inline bool add_cell_ok(void** cell) {
    void* fn = cell && anymaker::readable(cell, 8) ? *cell : nullptr;
    if (!fn || code_is(fn, kSigContainerLoad) || code_is(fn, kSigFileLoad)) return false;
    return code_is(fn, kSigAddDefinitions) || in_other_module(fn);
}
inline bool file_load_ok(const void* fn) { return code_is(fn, kSigFileLoad); }
// For the log: what a definition route led to.
inline const char* what_is(const void* fn) {
    return !fn ? "nothing" : code_is(fn, kSigAddDefinitions) ? "_add_definitions" : code_is(fn, kSigContainerLoad) ? "container.load"
         : code_is(fn, kSigFileLoad) ? "file.load" : in_other_module(fn) ? "another mod's hook" : "unknown code";
}

}  // namespace route
