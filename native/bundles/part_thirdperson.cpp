// The thirdperson_mod.cpp mod as one part of a bundle: the standalone source, unchanged, with its exports renamed so several
// parts can share one DLL (bundle_parts.h, sdk/include/anyapi_bundle_v1.h).
#define AnyAPI_ModInit thirdperson_ModInit
#define AnyAPI_ModReady thirdperson_ModReady
#include "../thirdperson_mod.cpp"
