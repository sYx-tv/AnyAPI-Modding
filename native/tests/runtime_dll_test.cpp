#include "anymaker_mod_extension.h"
#include <windows.h>
#include <cassert>
#include <iostream>
int main(int argc,char** argv) {
    assert(argc==2);
    // No FreeLibrary: framework unload/drain is deliberately unsupported.
    HMODULE module=LoadLibraryA(argv[1]);assert(module);
    auto get=reinterpret_cast<AnymakerGetExtensionV1Fn>(GetProcAddress(module,"AnymakerGetExtensionV1"));assert(get);
    assert(!get(0,sizeof(AnymakerModExtensionV1)) && !get(1,sizeof(AnymakerModExtensionV1)-1));
    auto api=get(1,sizeof(AnymakerModExtensionV1));assert(api && api->version==1);
    AnyExtensionInfoV1 info{};assert(api->get_info(&info,sizeof(info))==ANY_EXT_OK);
    assert(info.native_observers_ready==0 && info.session_role==0 && info.external_mod_loading==0);
    AnyObjectTokenV1 token{};size_t count=88,total=88;
    assert(api->enumerate_tokens(ANY_OBJECT_ACTOR,&token,1,&count,&total)==ANY_EXT_UNAVAILABLE && count==0 && total==0);
    std::cout<<"PASS: actual Windows DLL export negotiates extension version/size and fails closed without matching native game inputs\n";
}
