#pragma once
// What AnyEngineParts' three parts share (engine_parts_mod.cpp starts them; each lives in its own file).
#include <mutex>
#include <string>

namespace engine_parts {
// The parts look for their game functions on their own threads, so one that cannot start never holds up the others.
// Some hook the same cells (definitions add, the engine tick), and two hooks swapped into one cell at the same moment
// would fail one of them, so each part installs its hooks holding this lock.
std::mutex& install_mutex();
}  // namespace engine_parts

namespace turbo {
// The boost on a vehicle's engines, from the turbos and superchargers that ticked on it in the last second: Turbo's
// name for the setup ("Single turbo", "Twincharged", ...) and the power its superchargers take through their belts.
struct VehicleBoost { std::string setup = "Naturally aspirated"; double drive_watts = 0; };
VehicleBoost vehicle_boost(const void* vehicle);
}  // namespace turbo

namespace dyno {
// Turbo's engine tick hook reports an engine's knock damage this tick (hot boost air); the Dyno shows it per pull.
void note_knock(const void* engine, double knock);
}  // namespace dyno
