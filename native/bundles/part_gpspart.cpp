// The gpspart_mod.cpp mod as one part of a bundle: the standalone source, unchanged, with its exports renamed so several
// parts can share one DLL (bundle_parts.h, sdk/include/anyapi_bundle_v1.h).
#define AnyAPI_ModInit gpspart_ModInit
#define AnyAPI_ModReady gpspart_ModReady
#include "../gpspart_mod.cpp"
