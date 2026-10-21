// AnyGraphics bundle: AnyGraphics (shaders, lighting, photo mode) and ThirdPerson in one DLL. See bundle_parts.h.
#include "bundle_parts.h"
ANYAPI_BUNDLE_DECLARE(anygraphics)
ANYAPI_BUNDLE_DECLARE(thirdperson)
static const AnyBundlePartV1 parts[]={
 ANYAPI_BUNDLE_PART(anygraphics,"AnyGraphics",nullptr,0),
 ANYAPI_BUNDLE_PART(thirdperson,"ThirdPerson",L"ThirdPerson.dll",0),
};
ANYAPI_BUNDLE_EXPORTS("anygraphics","AnyGraphics",parts)
