// AnyEngineParts: engine parts for Anymaker in one mod. It carries three parts sets, each in its own file:
//   - turbo_mod.cpp: exhaust-driven turbochargers (2x2x2 to 5x5x2), inline blow-off valves and a wastegate with
//     antilag (docs/mods/turbo.md);
//   - rotary_mod.cpp: a stretchable 1-4 rotor rotary engine with its own sound (docs/mods/rotary.md);
//   - dyno_mod.cpp: a Dyno sensor part that records power and torque pulls and draws them on screen (docs/mods/dyno.md).
// They keep their part ids, so vehicles built with the old Turbo and Rotary prototypes load unchanged, and they log
// under their own tags ("turbo", "rotary", "dyno"). Each hooks and unhooks on its own: one failing to hook leaves the
// others working.
//
// EXPERIMENTAL. All three hook game functions through the experimental SDK: Anymaker 0.1.25 / Steam build 25904830 only.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include <thread>

namespace turbo { void init(const AnyModHostV1& h); void ready(); void start(); void shutdown(); }
namespace rotary { void init(const AnyModHostV1& h); void start(); void shutdown(); }
namespace dyno { void init(const AnyModHostV1& h); void ready(); void start(); void shutdown(); uint32_t on_input(const AnyInputV1* e, void* user); }

extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    turbo::init(*h);
    rotary::init(*h);
    dyno::init(*h);
    out->id = "anyengineparts";
    out->input = dyno::on_input;
    out->shutdown = [](void*) { turbo::shutdown(); rotary::shutdown(); dyno::shutdown(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    turbo::ready();                                  // Mod Settings rows (AnyHelpers) are registered here
    dyno::ready();                                   // the Dyno's key and graph
    // Resolving scans game memory: keep it off the loader and frame threads. One thread, one part after the other,
    // because both hook some of the same game functions and a cell must not be hooked from two threads at once.
    std::thread([] { turbo::start(); rotary::start(); dyno::start(); }).detach();
}
