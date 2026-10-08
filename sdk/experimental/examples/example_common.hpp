// Shared helpers for the SDK examples: a deliberately small "service layer" showing the wrapper
// boundary recommended in docs/guides/api-design.md. Mods talk to Game::*, which hands out ids and
// copies, resolves symbols once per build, checks the build, and owns every hook it installs.
//
// Evidence of what this relies on: see the header comments in include/anymaker_sdk_runtime.hpp.
// Examples are compile-tested (tools/validate_compile.py) and statically reviewed; none is
// runtime-tested yet unless docs/examples.md says so.
#pragma once
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <mutex>
#include <vector>
#include "anymaker_sdk_runtime.hpp"
#include "anymaker_sdk_symbols.hpp"
#include "anymaker_sdk_types.hpp"

namespace example {
using namespace anymaker;

inline void log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

// ---------------------------------------------------------------- build guard
inline bool build_matches() {
    if (exe_pe_timestamp() != sym::game_exe_pe_timestamp) {
        log("[sdk] game.exe build differs from the SDK (Steam build %s); refusing to resolve", sym::steam_build_id);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- game strings
// Owns a game `string` built by the game's own allocator ($string_ctor_cstr / $string_dtor). Use it for
// `const string` arguments. Runtime evidence for these RVAs: native_bindings.json (call cells).
struct game_string {
    gc_string_view s{};
    explicit game_string(const char* text) {
        // $-prefixed runtime helpers take scalar arguments BY VALUE [runtime: calltest step 3 - passing a pointer
        // to the pointer produced a 6-byte garbage string]; regular natives and gcl functions take them by pointer.
        using ctor_t = void (*)(gc_string_view*, const char*);
        ((ctor_t)native_at(sym::string_ctor_cstr_rva))(&s, text);
    }
    ~game_string() {
        using dtor_t = void (*)(gc_string_view*);
        ((dtor_t)native_at(sym::string_dtor_rva))(&s);
    }
    game_string(const game_string&) = delete;
    game_string& operator=(const game_string&) = delete;
};

// file.path: { e_store m_store; string m_path; } (metadata layout, 24 bytes)
struct game_path {
    int32_t store;
    uint32_t _pad;
    game_string path;
    game_path(int32_t st, const char* p) : store(st), _pad(0), path(p) {}
};

// ---------------------------------------------------------------- object graph
// g_server is ref<server>; ref.ptr (+0x08) is the server object [runtime: ref layout]. Null outside a
// hosted game. server.m_scene is ref<server_scene>.
inline uint8_t* server_object() {
    static auto* ref = resolve_global<gc_ref_raw<uint8_t>>(sym::g_server);
    return (ref && readable(ref, 16)) ? ref->ptr : nullptr;
}
inline uint8_t* server_scene_object() {
    uint8_t* srv = server_object();
    if (!srv) return nullptr;
    auto* scene_ref = reinterpret_cast<gc_ref_raw<uint8_t>*>(srv + sym::off_server__m_scene);
    return scene_ref->ptr;
}
inline uint8_t* client_object() {
    static auto* ref = resolve_global<gc_ref_raw<uint8_t>>(sym::g_client);
    return (ref && readable(ref, 16)) ? ref->ptr : nullptr;
}
// client_scene is embedded in client at off_client__m_scene
inline uint8_t* client_scene_object() {
    uint8_t* c = client_object();
    return c ? c + sym::off_client__m_scene : nullptr;
}

// Call virtual method `slot` of a gcl object through its own typeinfo (dispatch rule runtime-validated).
template <class Fn> Fn vmethod(const void* gc_object, int slot) {
    return reinterpret_cast<Fn>(typeinfo_of(gc_object).method(slot));
}

// ---------------------------------------------------------------- thread queues
// Work posted here runs inside the hooked server.tick (server thread) or client.tick (main thread).
class work_queue {
    std::mutex m;
    std::vector<std::function<void()>> q;
public:
    void post(std::function<void()> f) { std::lock_guard<std::mutex> l(m); q.push_back(std::move(f)); }
    void drain() {
        std::vector<std::function<void()>> run;
        { std::lock_guard<std::mutex> l(m); run.swap(q); }
        for (auto& f : run) f();
    }
};

}  // namespace example
