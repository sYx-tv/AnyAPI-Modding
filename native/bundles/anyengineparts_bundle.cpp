// AnyEngineParts bundle: AnyEngineParts (turbochargers, superchargers, rotary engine, Dyno), EngineSound and the GPS
// Sensor part in one DLL. See bundle_parts.h.
#include "bundle_parts.h"
ANYAPI_BUNDLE_DECLARE(anyengineparts)
ANYAPI_BUNDLE_DECLARE(enginesound)
ANYAPI_BUNDLE_DECLARE(gpspart)
static const AnyBundlePartV1 parts[]={
 ANYAPI_BUNDLE_PART(anyengineparts,"AnyEngineParts",nullptr,0),
 ANYAPI_BUNDLE_PART(enginesound,"EngineSound",L"EngineSound.dll",0),
 ANYAPI_BUNDLE_PART(gpspart,"GpsPart",L"GpsPart.dll",0),
};
ANYAPI_BUNDLE_EXPORTS("anyengineparts","AnyEngineParts",parts)
