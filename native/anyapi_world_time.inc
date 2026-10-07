#include "world_time_state.h"
#include "runtime_call_cell.h"
namespace world_time_runtime {
static world_time::Store store;static anyapi_runtime::CallCellHook hook;
static std::atomic<uintptr_t> scene{},slot{UINTPTR_MAX};static std::atomic<bool> attached{};static std::atomic<uint64_t> samples{};
// GCL f64 return uses an output pointer in RCX; remaining arguments shift right.
using GetCycle=void(*)(double*,void*,const int32_t*,const int32_t*);
static void observe(double* out,void* self,const int32_t* mode,const int32_t* tick){
 ((GetCycle)hook.original())(out,self,mode,tick);
 if(uintptr_t(self)==scene.load()+0x2168){store.sample(*out,GetTickCount64());if(++samples==1)log_line(ANY_LOG_INFO,"framework",("WORLD_TIME sampled=1 cycle="+std::to_string(*out)+" source=native_day_night_cycle").c_str());}
}
static const unsigned char BUILD[]={0x53,0x55,0x56,0x48,0x81,0xec,0xf0,0x01,0,0,0x48,0x89,0x8c,0x24,0xb8,0,0,0,0x48,0x89,0x94,0x24,0xc0,0,0,0,0x4c,0x89,0x84,0x24,0xc8,0};
static const unsigned char GET[]={0x53,0x55,0x56,0x48,0x81,0xec,0x80,0,0,0,0x48,0x89,0x4c,0x24,0x40,0x48,0x89,0x54,0x24,0x48,0x4c,0x89,0x44,0x24,0x50,0x4c,0x89,0x4c,0x24,0x58,0x48,0x8d};
static void sample(void* object){scene=uintptr_t(object);if(!object||attached||slot==UINTPTR_MAX)return;
 uintptr_t klass{},table{},cell{},body{};auto time=uintptr_t(object)+0x2168;
 if(!safe_read_memory(time,&klass,8)||!klass||!safe_read_memory(klass+0x80,&table,8)||!table||!safe_read_memory(table+slot.load(),&cell,8)||!cell||!safe_read_memory(cell,&body,8)||!p34_executable(body)||!p34_body(body,GET))return;
 if(hook.install((void**)cell,(void*)observe)){attached=true;log_line(ANY_LOG_INFO,"framework","WORLD_TIME ready=1 observer=native_cycle_getter writes_to_world=0");}
}
static bool copy(AnyWorldTimeSnapshotV1* out){return store.copy(out,GetTickCount64());}
static const AnyWorldTimeV1 api{sizeof(AnyWorldTimeV1),1,copy};
static DWORD WINAPI worker(void*){for(unsigned i=0;i<120;++i){auto found=scan_exact(BUILD,sizeof(BUILD));uintptr_t selected=UINTPTR_MAX;unsigned matches=0;for(auto address:found){uintptr_t value{};if(p34_executable(address)&&safe_read_memory(address+1016,&value,8)&&value%8==0&&value<0x10000){selected=value;++matches;}}if(matches==1){slot=selected;log_line(ANY_LOG_INFO,"framework","WORLD_TIME getter_contract=verified awaiting_world=1");return 0;}Sleep(1000);}log_line(ANY_LOG_WARN,"framework","WORLD_TIME unavailable=unmatched_native_contract");return 0;}
}
static const AnyWorldTimeV1* anyapi_world_time_service(){return &world_time_runtime::api;}
static void anyapi_world_time_sample(void* scene){world_time_runtime::sample(scene);}
