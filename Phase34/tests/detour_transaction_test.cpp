#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <iterator>
#include <cassert>
#include <iostream>
using HANDLE=void*;using DWORD=uint32_t;using DWORD64=uint64_t;using LONG=int32_t;using LONG64=int64_t;
struct RUNTIME_FUNCTION {DWORD BeginAddress,EndAddress,UnwindData;};using PRUNTIME_FUNCTION=RUNTIME_FUNCTION*;
struct THREADENTRY32 {DWORD dwSize,th32OwnerProcessID,th32ThreadID;};struct CONTEXT {DWORD ContextFlags;uint64_t Rip;};
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define TH32CS_SNAPTHREAD 4
#define THREAD_SUSPEND_RESUME 1
#define THREAD_GET_CONTEXT 2
#define THREAD_QUERY_INFORMATION 4
#define FALSE 0
#define CONTEXT_CONTROL 1
#define MEM_COMMIT 1
#define MEM_RESERVE 2
#define MEM_RELEASE 4
#define PAGE_READWRITE 4
#define PAGE_EXECUTE_READ 32
#define PAGE_EXECUTE_READWRITE 64
#define ANY_LOG_INFO 0
#define ANY_LOG_WARN 1
#define ANY_LOG_ERROR 2
static bool g_p27_resolution_only=false;
static const unsigned char expected[]={0x53,0x55,0x48,0x83,0xEC,0x48,0x48,0x89,0x4C,0x24,0x20,0x48,0x8B,0x41,0x08};
struct Spec {const char* label;const unsigned char* body;size_t overwrite;unsigned char unwind[48];size_t unwind_size;};
static const Spec P27_HOOK_SPECS[]={{"test",expected,15,{1,6,3,0,6,0x82,2,0x50,1,0x30,0,0},12}};
static uintptr_t g_p272_installed_targets[1]{};
static volatile LONG g_p27_hook_installed[1]{};static volatile LONG64 g_p27_protection_failures=0;
static void* published=nullptr;static unsigned char target[48];static bool busy=false,protect_fail=false,unwind_fail=false,paused=false;
static int frees=0,unwind_adds=0,unwind_deletes=0;
static HANDLE CreateToolhelp32Snapshot(DWORD,DWORD) {return (HANDLE)1;}
static bool Thread32First(HANDLE,THREADENTRY32* e) {e->th32OwnerProcessID=1;e->th32ThreadID=2;return true;}
static bool Thread32Next(HANDLE,THREADENTRY32*) {return false;}
static DWORD GetCurrentProcessId() {return 1;}static DWORD GetCurrentThreadId() {return 1;}
static HANDLE GetCurrentProcess() {return (HANDLE)1;}static HANDLE OpenThread(DWORD,int,DWORD) {return (HANDLE)2;}
static bool CloseHandle(HANDLE) {return true;}
static DWORD SuspendThread(HANDLE) {assert(!paused);paused=true;return 0;}
static bool GetThreadContext(HANDLE,CONTEXT* c) {c->Rip=busy?(uintptr_t)target+4:0;return true;}
static DWORD ResumeThread(HANDLE) {
    assert(paused);if(target[0]==0xff) {assert(published);assert(g_p27_hook_installed[0]==0);}
    paused=false;return 0;
}
static void* VirtualAlloc(void*,size_t n,int,int) {assert(!paused);return std::calloc(1,n);}
static bool VirtualFree(void* p,int,int) {assert(!paused);++frees;std::free(p);return true;}
static bool VirtualProtect(void* p,size_t,DWORD,DWORD* previous) {*previous=PAGE_EXECUTE_READ;return !(p==target && protect_fail);}
static bool RtlAddFunctionTable(PRUNTIME_FUNCTION t,int,DWORD64 b) {assert(!paused);++unwind_adds;assert(t->UnwindData==80 && ((unsigned char*)b)[80]==1);return !unwind_fail;}
static bool RtlDeleteFunctionTable(PRUNTIME_FUNCTION) {++unwind_deletes;return true;}
static bool FlushInstructionCache(HANDLE,void*,size_t) {return true;}
static void* InterlockedCompareExchangePointer(void* volatile* p,void* n,void* old) {auto previous=*p;if(previous==old)*p=n;return previous;}
static void* InterlockedExchangePointer(void* volatile* p,void* n) {auto previous=*p;*p=n;return previous;}
static LONG InterlockedExchange(volatile LONG* p,LONG n) {auto old=*p;*p=n;return old;}
static LONG64 InterlockedIncrement64(volatile LONG64* p) {auto n=*p+1;*p=n;return n;}
static void log_line(int,const char*,const char*) {assert(!paused);}
static void write_abs_jump(unsigned char* p,uintptr_t destination) {p[0]=0xff;p[1]=0x25;std::memset(p+2,0,4);std::memcpy(p+6,&destination,8);}
#include "../phase27_detours.inc"
static void reset() {std::memcpy(target,expected,sizeof(expected));published=nullptr;g_p27_hook_installed[0]=0;}
int main() {
    reset();g_p27_resolution_only=true;assert(!install_detour("test",(uintptr_t)target,expected,15,123,&published));assert(!published && target[0]==expected[0]);g_p27_resolution_only=false;busy=true;assert(!install_detour("test",(uintptr_t)target,expected,15,123,&published));assert(!published && !paused && target[0]==expected[0]);busy=false;
    protect_fail=true;assert(!install_detour("test",(uintptr_t)target,expected,15,123,&published));assert(!published && !paused);protect_fail=false;
    unwind_fail=true;assert(!install_detour("test",(uintptr_t)target,expected,15,123,&published));assert(!published);unwind_fail=false;
    assert(install_detour("test",(uintptr_t)target,expected,15,123,&published));assert(published && g_p27_hook_installed[0] && g_p272_installed_targets[0]==(uintptr_t)target);
    assert(!std::memcmp(published,expected,15));assert(((unsigned char*)published)[15]==0xff);
    assert(install_detour("test",(uintptr_t)target,expected,15,123,&published));std::free(published);
    assert(frees==3 && unwind_adds==4 && unwind_deletes==2);
    std::cout<<"PASS: publication precedes resume; busy/protection/unwind failures leave entry unchanged\n";
}
