#pragma once
#include <cstdint>
// Copied native physics centre of mass. Not an axle-load or tipping calculation.
// ANY_BALANCE_FLUID_MASS (API 0.34.0+): body_mass_kg and the centre include the
// server-simulated liquid in the creation's tanks (locally hosted worlds only).
constexpr uint32_t ANY_BALANCE_BODY_MASS=1, ANY_BALANCE_CONNECTED_CREATION=2, ANY_BALANCE_FLUID_MASS=4;
struct AnyBalancePointV1 { double x{},y{},z{}; };
struct AnyBalanceScreenV1 { double x{},y{},depth{}; }; // Native NDC; positive depth is in front.
struct AnyCreationBalanceSnapshotV1 {
 uint32_t struct_size{sizeof(AnyCreationBalanceSnapshotV1)},version{1},valid_fields{},body_count{};
 uint64_t context{},sampled_tick{};
 int32_t vehicle_id{-1},tool_item_id{-1};
 double body_mass_kg{},height_m{},offset_x_m{},offset_z_m{};
 AnyBalancePointV1 centre_local{},bounds_min{},bounds_max{},centre_world{};
 AnyBalanceScreenV1 centre{},base{},geometric_centre{},axes[3]{},corners[8]{};
};
struct AnyCreationBalanceV1 {
 uint32_t struct_size{sizeof(AnyCreationBalanceV1)},version{1};
 // False outside focused gameplay, without Properties equipped, without a target,
 // or when the last native overlay sample is older than 150 ms.
 bool (*copy)(AnyCreationBalanceSnapshotV1*){};
};
