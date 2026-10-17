// Shared plumbing for the bundle DLLs in this folder. A bundle carries several mods ("parts"); AnyAPI 0.39.0 and newer
// loads each part as if it were its own DLL (sdk/include/anyapi_bundle_v1.h). Each part is the mod's standalone source,
// compiled unchanged through a part_<id>.cpp wrapper that renames its AnyAPI_ModInit / AnyAPI_ModReady (and DllMain)
// exports to <id>_ModInit / <id>_ModReady.
//
// Adding a mod to a bundle: add part_<id>.cpp, add it to the bundle's sources in native/CMakeLists.txt (with the
// standalone target's link libraries and resources), then ANYAPI_BUNDLE_DECLARE(<id>) and an ANYAPI_BUNDLE_PART row
// below. The mod keeps its id, settings ids, files and part ids, so saves and settings carry over. Give it the old DLL's
// file name as legacy_file when it used to ship alone: while that DLL is still installed, AnyAPI loads it instead.
#pragma once
#define NOMINMAX
#include <windows.h>
#include "anyapi_bundle_v1.h"

#define ANYAPI_BUNDLE_DECLARE(id) \
 extern "C" __declspec(dllexport) bool id##_ModInit(const AnyModHostV1*,AnyModCallbacksV1*); \
 extern "C" __declspec(dllexport) void id##_ModReady();
#define ANYAPI_BUNDLE_PART(id,name,legacy_file,flags) \
 AnyBundlePartV1{sizeof(AnyBundlePartV1),ANYAPI_MOD_ABI,#id,name,legacy_file,id##_ModInit,id##_ModReady,flags}
// AnyAPI_BundleParts is what AnyAPI 0.39.0+ loads. Older builds only call AnyAPI_ModInit, which says why nothing loaded.
#define ANYAPI_BUNDLE_EXPORTS(id,name,table) \
 extern "C" __declspec(dllexport) uint32_t AnyAPI_BundleParts(const AnyBundlePartV1** out){ \
  if(!out)return 0;*out=table;return uint32_t(sizeof(table)/sizeof(table[0]));} \
 extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* host,AnyModCallbacksV1*){ \
  if(host&&host->struct_size==sizeof(*host)&&host->log)host->log(2,id,name " needs AnyAPI 0.39.0 or newer: update AnyAPI in the AnyAPI Manager."); \
  return false;}
