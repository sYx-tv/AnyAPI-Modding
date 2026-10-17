#pragma once
#include "anyapi_mod_v1.h"

// Bundles: one DLL that carries several mods ("parts"). Export
//   extern "C" __declspec(dllexport) uint32_t AnyAPI_BundleParts(const AnyBundlePartV1** parts);
// returning the part count (at most 16) and a pointer to a static array. AnyAPI then loads every part as if it were
// its own DLL: each gets its own host, callbacks, renderer and textures, input, captured input, menu sections,
// published services and fault isolation. A part that throws is disabled alone; the other parts keep running.
// The loader prefers AnyAPI_BundleParts over AnyAPI_ModInit. Older AnyAPI builds only look for AnyAPI_ModInit, so a
// bundle may export one that logs "needs a newer AnyAPI" and returns false.
//
// Parts load in array order, ModInit for every part of every DLL first, then ModReady (the usual two-phase start).
// A part is skipped (not initialized) when:
//   - its legacy_file (the old standalone DLL, e.g. L"AnyInventory.dll") is present in the mods folder: the old DLL
//     loads instead, so nothing runs twice while players still have it;
//   - the player switched it off (anyapi.bundle_parts, below; saved in "AnyAPI and Modding/bundle-parts.tsv").
// ANY_BUNDLE_PART_REQUIRED parts cannot be switched off.
enum AnyBundlePartFlags : uint32_t { ANY_BUNDLE_PART_REQUIRED=1 };
struct AnyBundlePartV1 {
 uint32_t struct_size{sizeof(AnyBundlePartV1)},abi{ANYAPI_MOD_ABI};
 const char* id{};              // the part's stable mod id, the same id its ModInit returns (1-95 chars: a-z 0-9 . _ -)
 const char* name{};            // display name, e.g. "AnyInventory"
 const wchar_t* legacy_file{};  // optional: the old standalone DLL this part replaces
 AnyModInitV1 init{};           // required: the part's AnyAPI_ModInit
 void (*ready)(){};             // optional: the part's AnyAPI_ModReady
 uint32_t flags{};
};
using AnyBundlePartsExportV1=uint32_t(*)(const AnyBundlePartV1**);

// Service anyapi.bundle_parts version 1: every part AnyAPI found this launch, and the on/off switch for next launch.
// Any thread. Switching applies after a restart (hooks and services cannot be unloaded).
enum AnyBundlePartState : uint32_t { ANY_PART_LOADED=1,ANY_PART_OFF=2,ANY_PART_LEGACY=3,ANY_PART_FAILED=4 };
struct AnyBundlePartInfoV1 {
 uint32_t struct_size{sizeof(AnyBundlePartInfoV1)},state{},flags{},enabled_next{};
 char id[96]{},name[64]{},bundle[64]{};   // bundle = the DLL's file name without ".dll"
};
struct AnyBundlePartsV1 {
 uint32_t struct_size{sizeof(AnyBundlePartsV1)},version{1};
 uint32_t (*count)(){};
 bool (*copy)(uint32_t index,AnyBundlePartInfoV1*){};
 bool (*set_enabled)(const char* id,uint32_t enabled){};   // false for unknown or required parts, or if saving failed
};
