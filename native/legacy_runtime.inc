#include "legacy_pattern_match.h"
#include "legacy_factory_owner.h"
#include "legacy_dispatch_owner.h"
static volatile LONG64 g_p272_route_null[141]{},g_p272_route_short[141]{};
static uintptr_t g_p272_factory_owner=0,g_p272_factory_actual=0;
static int g_p272_factory_match=-1;
static uintptr_t g_p272_dispatch_owner=0;
static void p272_record_route(size_t i,void* server,void* peer,void* event) {
    if(!server || !peer || !event){InterlockedIncrement64(&g_p272_route_null[i]);return;}
    // Readability only: do not invent IDs from an unaudited event layout.
    unsigned char type_header[8]{};
    if(!safe_read_memory((uintptr_t)event,type_header,sizeof(type_header)))InterlockedIncrement64(&g_p272_route_short[i]);
}
static void p272_retry_routes() {
    P272PatternIndex index;
    for(size_t i=0;i<std::size(g_p272_routes);++i){auto& r=g_p272_routes[i];if(!g_p272_originals[i]){r.candidate=0;r.matches=0;if(!r.dep_offset)index.add({r.body,r.size,i});}}
    // Factory owner is indexed in this same pass; no 139 repeated memory scans.
    if(!g_p272_factory_owner)index.add({P272_FACTORY_OWNER,sizeof(P272_FACTORY_OWNER),std::size(g_p272_routes)});
    if(!g_p272_dispatch_owner)index.add({P272_DISPATCH_OWNER,sizeof(P272_DISPATCH_OWNER),std::size(g_p272_routes)+1});
    SYSTEM_INFO si{};GetSystemInfo(&si);uintptr_t addr=(uintptr_t)si.lpMinimumApplicationAddress,max=(uintptr_t)si.lpMaximumApplicationAddress;
    constexpr SIZE_T CHUNK=1u<<20;std::vector<unsigned char> buffer(CHUNK+index.longest);
    uintptr_t owner_candidate=0,dispatch_candidate=0;size_t owner_matches=0,dispatch_matches=0;
    if(!index.count)addr=max;
    while(addr<max){MEMORY_BASIC_INFORMATION mbi{};if(!VirtualQuery((LPCVOID)addr,&mbi,sizeof(mbi)))break;
        uintptr_t base=(uintptr_t)mbi.BaseAddress;SIZE_T region=mbi.RegionSize;
        if(mbi.State==MEM_COMMIT && executable_protect(mbi.Protect))for(SIZE_T pos=0;pos<region;){
            SIZE_T core=std::min<SIZE_T>(CHUNK,region-pos),want=std::min<SIZE_T>(core+index.longest-1,region-pos),got=0;
            if(ReadProcessMemory(GetCurrentProcess(),(LPCVOID)(base+pos),buffer.data(),want,&got))index.scan(buffer.data(),got,std::min<size_t>(core,got),[&](size_t i,size_t offset){
                if(i==std::size(g_p272_routes)){owner_candidate=base+pos+offset;++owner_matches;}
                else if(i==std::size(g_p272_routes)+1){dispatch_candidate=base+pos+offset;++dispatch_matches;}
                else{auto& r=g_p272_routes[i];r.candidate=base+pos+offset;++r.matches;}
            });
            pos+=core;
        }
        uintptr_t next=base+region;if(next<=addr)break;addr=next;
    }
    if(owner_matches==1)g_p272_factory_owner=owner_candidate;
    if(dispatch_matches==1)g_p272_dispatch_owner=dispatch_candidate;
    for(size_t i=0;i<std::size(g_p272_routes);++i){auto& r=g_p272_routes[i];if(!g_p272_originals[i] && r.dep_offset && g_p272_dispatch_owner){
            uintptr_t cell=0,fn=0;
            if(safe_read_memory(g_p272_dispatch_owner+r.dep_offset,&cell,8) && cell && safe_read_memory(cell,&fn,8) && fn){
                std::vector<unsigned char> bytes(r.size);
                if(safe_read_memory(fn,bytes.data(),bytes.size()) && !std::memcmp(bytes.data(),r.body,r.size)){r.candidate=fn;r.matches=1;}
                else{r.candidate=0;r.matches=0;}
            }else{r.candidate=0;r.matches=0;}
        }
        if(r.dep_offset && !g_p272_dispatch_owner){r.candidate=0;r.matches=0;}
        if(!g_p272_originals[i] && r.matches==1)install_detour(r.label,r.candidate,r.body,r.overwrite,(uintptr_t)r.hook,(void**)&g_p272_originals[i]);}
    if(g_p272_factory_owner){uintptr_t cell=0,fn=0;
        if(safe_read_memory(g_p272_factory_owner+P272_FACTORY_DEP_OFFSET,&cell,8) && cell && safe_read_memory(cell,&fn,8) && fn){
            g_p272_factory_actual=fn;uintptr_t installed=(uintptr_t)InterlockedCompareExchangePointer((void* volatile*)&g_p272_installed_targets[15],nullptr,nullptr);
            if(installed)g_p272_factory_match=fn==installed?1:0;
        }
    }
}
static void p272_report() {
    char message[512]{};
    for(size_t i=0;i<std::size(g_p272_routes);++i){const auto& r=g_p272_routes[i];std::snprintf(message,sizeof(message),"PHASE272_ROUTE name=%s installed=%d calls=%lld null_args=%lld unreadable_headers=%lld candidates=%zu",r.name,g_p272_originals[i]?1:0,(long long)read64(&g_p27_hook_calls[r.slot]),(long long)read64(&g_p272_route_null[i]),(long long)read64(&g_p272_route_short[i]),r.matches);log_line(ANY_LOG_INFO,"framework",message);}
    std::snprintf(message,sizeof(message),"PHASE272_FACTORY_PROVENANCE dispatch_owner_ready=%d owner_ready=%d dependency_ready=%d installed_target_match=%d factory_calls=%lld",g_p272_dispatch_owner?1:0,g_p272_factory_owner?1:0,g_p272_factory_actual?1:0,g_p272_factory_match,(long long)read64(&g_p27_hook_calls[15]));log_line(ANY_LOG_INFO,"framework",message);
}
