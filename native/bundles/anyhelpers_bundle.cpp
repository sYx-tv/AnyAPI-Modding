// AnyHelpers bundle: AnyHelpers (Mod Controls, Mod Settings), AnyInventory, AnyMirror, AnyBalance, MCCode and AnyLights in one
// DLL. See bundle_parts.h. AnyHelpers comes first and cannot be switched off: it draws the other parts' on/off rows in
// Mod Settings, and the others use its settings and controls services.
#include "bundle_parts.h"
ANYAPI_BUNDLE_DECLARE(anyhelpers)
ANYAPI_BUNDLE_DECLARE(anyinventory)
ANYAPI_BUNDLE_DECLARE(anymirror)
ANYAPI_BUNDLE_DECLARE(anybalance)
ANYAPI_BUNDLE_DECLARE(mccode)
ANYAPI_BUNDLE_DECLARE(anylights)
static const AnyBundlePartV1 parts[]={
 ANYAPI_BUNDLE_PART(anyhelpers,"AnyHelpers",nullptr,ANY_BUNDLE_PART_REQUIRED),
 ANYAPI_BUNDLE_PART(anyinventory,"AnyInventory",L"AnyInventory.dll",0),
 ANYAPI_BUNDLE_PART(anymirror,"AnyMirror",L"AnyMirror.dll",0),
 ANYAPI_BUNDLE_PART(anybalance,"AnyBalance",L"AnyBalance.dll",0),
 ANYAPI_BUNDLE_PART(mccode,"MCCode",L"MCCode.dll",0),
 ANYAPI_BUNDLE_PART(anylights,"AnyLights",L"AnyLights.dll",0),
};
ANYAPI_BUNDLE_EXPORTS("anyhelpers","AnyHelpers",parts)
BOOL APIENTRY anyhelpers_DllMain(HMODULE,DWORD,LPVOID);
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID reserved){return anyhelpers_DllMain(module,reason,reserved);}
