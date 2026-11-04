#pragma once
// What AnyEngineParts' three parts share (engine_parts_mod.cpp starts them; each lives in its own file).
#include <mutex>
#include <string>
#include "ecu_logic.h"

namespace engine_parts {
// The parts look for their game functions on their own threads, so one that cannot start never holds up the others.
// Some hook the same cells (definitions add, the engine tick), and two hooks swapped into one cell at the same moment
// would fail one of them, so each part installs its hooks holding this lock.
std::mutex& install_mutex();
}  // namespace engine_parts

namespace turbo {
// The boost on a vehicle's engines, from the turbos and superchargers that ticked on it in the last second: Turbo's
// name for the setup ("Single turbo", "Twincharged", ...) and the power its superchargers take through their belts.
struct VehicleBoost { std::string setup = "Naturally aspirated"; double drive_watts = 0, boost_psi = 0, target_psi = 0; };
VehicleBoost vehicle_boost(const void* vehicle);
// A Boost Controller's ask (psi over the air outside) for every turbo on a vehicle, renewed each tick; 0 clears it.
void set_boost_target(const void* vehicle, double psi);
// An ECU's tune (ecu_logic.h) for every engine on a vehicle, renewed each tick by the ECU part (dyno_mod.cpp).
void set_ecu_tune(const void* vehicle, const ecu::Tune& tune);
// What the ECU's outputs show: the vehicle's engines in the last second (fastest RPM, worst knock, its tune's numbers).
struct EcuReport { double rpm = 0, knock = 0, power_scale = 1, knock_limit = 0; };
EcuReport ecu_report(const void* vehicle);
// An ECU's panel tune, saved with the world (the turbo wear file): ecu::pack'd, 1.0 when never set.
double ecu_saved(void* vehicle, const void* component);
void ecu_save(void* vehicle, const void* component, double packed);
}  // namespace turbo

namespace dyno {
// Turbo's engine tick hook reports an engine's knock damage this tick (hot boost air); the Dyno shows it per pull.
void note_knock(const void* engine, double knock);
}  // namespace dyno
