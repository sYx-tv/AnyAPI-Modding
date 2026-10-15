// AnyEngineParts: engine parts for Anymaker in one mod. It carries three parts sets, each in its own file:
//   - turbo_mod.cpp: exhaust-driven turbochargers (2x2x2 to 5x5x2), inline blow-off valves and a wastegate with
//     antilag (docs/mods/turbo.md);
//   - rotary_mod.cpp: a stretchable 1-4 rotor rotary engine with its own sound (docs/mods/rotary.md);
//   - dyno_mod.cpp: a Dyno sensor part that records power and torque pulls and draws them on screen (docs/mods/dyno.md).
// They keep their part ids, so vehicles built with the old Turbo and Rotary prototypes load unchanged, and they log
// under their own tags ("turbo", "rotary", "dyno"). Each hooks and unhooks on its own: one failing to hook leaves the
// others working, and one still looking for its game functions does not hold the others up.
//
// EXPERIMENTAL. All three hook game functions through the experimental SDK: Anymaker 0.1.25 / Steam build 25904830 only.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "engine_parts_shared.h"
#include <thread>

namespace turbo { void init(const AnyModHostV1& h); void ready(); void start(); void shutdown(); }
namespace rotary { void init(const AnyModHostV1& h); void start(); void shutdown(); }
namespace dyno { void init(const AnyModHostV1& h); void ready(); void start(); void shutdown(); uint32_t on_input(const AnyInputV1* e, void* user); }

std::mutex& engine_parts::install_mutex() { static std::mutex m; return m; }

extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    turbo::init(*h);
    rotary::init(*h);
    dyno::init(*h);
    out->id = "anyengineparts";
    out->input = dyno::on_input;
    // Usually the last hooked, first removed. A hook another sits on top of stays (cell_hook.remove), which is safe:
    // the DLL is not unloaded before the game exits.
    out->shutdown = [](void*) { dyno::shutdown(); rotary::shutdown(); turbo::shutdown(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    turbo::ready();                                  // Mod Settings rows (AnyHelpers) are registered here
    dyno::ready();                                   // the Dyno's key and graph
    // Resolving scans game memory: keep it off the loader and frame threads. A thread per part, so a part that keeps
    // looking (minutes, when a game function is missing) does not delay the others; they install one at a time
    // (engine_parts::install_mutex). Any order works: every hook chains onto whatever sits under it.
    std::thread(turbo::start).detach();
    std::thread(rotary::start).detach();
    std::thread(dyno::start).detach();
}
