#include <mutex>
static std::mutex g_detour_install_mutex;
// Called on the framework worker, never inside DllMain or a game hook.
// Prepare allocations and logging before suspending threads: suspended threads
// may own allocator, file, or loader locks.
struct P27PausedThread { HANDLE handle; DWORD id; bool suspended; };
static bool p27_pause_threads(P27PausedThread* threads,size_t& count,uintptr_t target,size_t overwrite) {
    count=0;
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot==INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
    bool ok=true;
    if(!Thread32First(snapshot,&entry)) ok=false;
    else do {
        if(entry.th32OwnerProcessID!=GetCurrentProcessId() || entry.th32ThreadID==GetCurrentThreadId()) continue;
        if(count==2048) {ok=false;break;}
        HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
        if(!h) {ok=false;break;}
        threads[count++]={h,entry.th32ThreadID,false};
    } while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);
    if(!ok) return false;
    for(size_t i=0;i<count;++i) {
        if(SuspendThread(threads[i].handle)==(DWORD)-1) return false;
        threads[i].suspended=true;
        CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(threads[i].handle,&context)) return false;
        // Never alter a thread context or relocate an in-flight instruction.
        if(context.Rip>=target && context.Rip<target+overwrite) return false;
    }
    return true;
}
static void p27_resume_threads(P27PausedThread* threads,size_t count) {
    for(size_t i=count;i>0;--i) {
        auto& t=threads[i-1];
        if(t.suspended) ResumeThread(t.handle);
        CloseHandle(t.handle);
    }
}
static bool install_detour(const char* label,uintptr_t target,const unsigned char* expected,
                           size_t overwrite,uintptr_t hook,void** original_slot) {
    std::lock_guard install_lock(g_detour_install_mutex);
    if(g_p27_resolution_only) return false;
    size_t slot=0;
    for(;slot<std::size(P27_HOOK_SPECS);++slot) if(std::strcmp(label,P27_HOOK_SPECS[slot].label)==0) break;
    if(slot==std::size(P27_HOOK_SPECS) || !target || !original_slot ||
       P27_HOOK_SPECS[slot].body!=expected || P27_HOOK_SPECS[slot].overwrite!=overwrite) return false;
    if(InterlockedCompareExchangePointer((void* volatile*)original_slot,nullptr,nullptr)) return true;
    const auto& spec=P27_HOOK_SPECS[slot];
    if(std::memcmp((void*)target,expected,overwrite)!=0) {
        log_line(ANY_LOG_ERROR,"framework",("PHASE27_HOOK target="+std::string(label)+" status=PROLOGUE_MISMATCH").c_str());return false;
    }
    auto trampoline=(unsigned char*)VirtualAlloc(nullptr,256,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!trampoline) return false;
    std::memcpy(trampoline,expected,overwrite);
    write_abs_jump(trampoline+overwrite,target+overwrite);
    auto table=(PRUNTIME_FUNCTION)(trampoline+64);
    table->BeginAddress=0;table->EndAddress=(DWORD)(overwrite+14);table->UnwindData=80;
    std::memcpy(trampoline+80,spec.unwind,spec.unwind_size);
    DWORD ignored=0;
    if(!VirtualProtect(trampoline,256,PAGE_EXECUTE_READ,&ignored)) {VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    if(!RtlAddFunctionTable(table,1,(DWORD64)trampoline)) {VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),trampoline,256);
    unsigned char patch[48]{};std::memset(patch,0x90,overwrite);write_abs_jump(patch,hook);
    P27PausedThread threads[2048]{};size_t thread_count=0;
    bool paused=p27_pause_threads(threads,thread_count,target,overwrite);
    bool installed=false,restored=true;
    if(paused && std::memcmp((void*)target,expected,overwrite)==0) {
        DWORD old=0;
        if(VirtualProtect((void*)target,overwrite,PAGE_EXECUTE_READWRITE,&old)) {
            // The actual hook's original pointer is visible BEFORE its entry jump.
            InterlockedExchangePointer((void* volatile*)original_slot,trampoline);
            std::memcpy((void*)target,patch,overwrite);
            FlushInstructionCache(GetCurrentProcess(),(void*)target,overwrite);
            installed=true;
            restored=VirtualProtect((void*)target,overwrite,old,&ignored)!=0;
        }
    }
    p27_resume_threads(threads,thread_count);
    if(!installed) {RtlDeleteFunctionTable(table);VirtualFree(trampoline,0,MEM_RELEASE);}
    else {InterlockedExchangePointer((void* volatile*)&g_p272_installed_targets[slot],(void*)target);InterlockedExchange(&g_p27_hook_installed[slot],1);}
    std::string message="PHASE27_HOOK target="+std::string(label)+" status="+
        (installed?"INSTALLED":(paused?"PATCH_REJECTED":"THREAD_BUSY_RETRY"))+
        " unwind_registered="+(installed?std::string("1"):std::string("0"))+
        " protection_restored="+(restored?std::string("1"):std::string("0"));
    log_line(installed&&restored?ANY_LOG_INFO:ANY_LOG_WARN,"framework",message.c_str());
    if(installed&&!restored) InterlockedIncrement64(&g_p27_protection_failures);
    return installed;
}
