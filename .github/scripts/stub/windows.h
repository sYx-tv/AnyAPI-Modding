#pragma once
// Minimal stand-in for <windows.h> so examples can be syntax-checked on Linux CI.
// It declares only what the public SDK headers and examples use.
typedef void* HMODULE;
typedef void (*FARPROC)();
extern "C" HMODULE GetModuleHandleW(const wchar_t*);
extern "C" FARPROC GetProcAddress(HMODULE, const char*);
#define VK_F7 0x76
