// The mccode_mod.cpp mod as one part of a bundle: the standalone source, unchanged, with its exports renamed so several
// parts can share one DLL (bundle_parts.h, sdk/include/anyapi_bundle_v1.h).
#define AnyAPI_ModInit mccode_ModInit
#define AnyAPI_ModReady mccode_ModReady
#include "../mccode_mod.cpp"
