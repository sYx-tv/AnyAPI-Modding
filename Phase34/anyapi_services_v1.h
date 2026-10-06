#pragma once
#include "anyapi_mod_v1.h"
#ifdef _WIN32
#include <windows.h>
#endif
// Optional extensions leave AnyModHostV1 and AnyModCallbacksV1 unchanged.
struct AnyServicesV1 {
 uint32_t struct_size{sizeof(AnyServicesV1)},version{1};
 const void* (*query)(const char* id,uint32_t version){};
 bool (*publish)(const char* id,uint32_t version,const void* service){};
 bool (*input_filter)(uint32_t (*callback)(const AnyInputV1*,void*),void* user,int32_t priority){};
};
using AnyGetServicesV1=const AnyServicesV1*(*)(uint32_t);
inline const AnyServicesV1* AnyAPI_Services(){
 auto module=GetModuleHandleW(L"dinput8.dll");
 auto get=module?(AnyGetServicesV1)GetProcAddress(module,"AnyAPI_GetServices"):nullptr;
 return get?get(1):nullptr;
}
// Optional AnyAPI_ModReady() export runs once after every DLL's ModInit.
