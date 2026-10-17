// AnyMap bundle: AnyMap (map and minimap) in one DLL, with room for CustomMap and CustomGame. See bundle_parts.h.
// AnyMap's road graph is resource 101; another part's resources need other ids.
#include "bundle_parts.h"
ANYAPI_BUNDLE_DECLARE(anymap)
static const AnyBundlePartV1 parts[]={
 ANYAPI_BUNDLE_PART(anymap,"AnyMap",nullptr,0),
};
ANYAPI_BUNDLE_EXPORTS("anymap","AnyMap",parts)
BOOL APIENTRY anymap_DllMain(HMODULE,DWORD,LPVOID);
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID reserved){return anymap_DllMain(module,reason,reserved);}
