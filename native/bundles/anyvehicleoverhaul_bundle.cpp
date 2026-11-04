// AnyVehicleOverhaul bundle (AnyVehicleOverhaul.dll): every vehicle part mod in one DLL. Until 2026-11 these shipped
// as the AnyEngineParts bundle (AnyEngineParts.dll) and the standalone AnyMoto (AnyMoto.dll). See bundle_parts.h and
// docs/mods/vehicle-overhaul.md.
//
// Parts, in load order:
//   anyengineparts  turbochargers, superchargers, upkeep parts, rotary engine, Dyno (engine_parts_mod.cpp)
//   enginesound     generated engine sounds (enginesound_mod.cpp)
//   gpspart         GPS Sensor part (gpspart_mod.cpp)
//   anymoto         motorcycle fork, swingarm, seat, handlebars, Lean Assist (anymoto_mod.cpp)
//
// COMPATIBILITY: never rename a part id below. Each id is also the mod id its source registers with, and it keys the
// player's Mod Settings, keybinds and on/off switch (bundle-parts.tsv). Game part ids, mesh names and data folders
// live in the part sources and are unchanged by the bundle's name.
//
// legacy_file: while that DLL is still in the mods folder AnyAPI runs it instead of the bundled part, so nothing is
// hooked twice. AnyEngineParts.dll is the previous name of this bundle; its old enginesound and gpspart parts are also
// covered by AnyAPI's "a loaded mod already uses this id" rule, whichever DLL sorts first.
#include "bundle_parts.h"
ANYAPI_BUNDLE_DECLARE(anyengineparts)
ANYAPI_BUNDLE_DECLARE(enginesound)
ANYAPI_BUNDLE_DECLARE(gpspart)
ANYAPI_BUNDLE_DECLARE(anymoto)
static const AnyBundlePartV1 parts[]={
 ANYAPI_BUNDLE_PART(anyengineparts,"AnyEngineParts",L"AnyEngineParts.dll",0),
 ANYAPI_BUNDLE_PART(enginesound,"EngineSound",L"EngineSound.dll",0),
 ANYAPI_BUNDLE_PART(gpspart,"GpsPart",L"GpsPart.dll",0),
 ANYAPI_BUNDLE_PART(anymoto,"AnyMoto",L"AnyMoto.dll",0),
};
ANYAPI_BUNDLE_EXPORTS("anyvehicleoverhaul","AnyVehicleOverhaul",parts)
