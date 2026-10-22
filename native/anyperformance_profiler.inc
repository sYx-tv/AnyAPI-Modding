// Sampling profiler (AnyPerformance, own thread, off by default). Included by anyperformance_mod.cpp.
//
// Every 2 ms it pauses the main thread and the server thread for a few microseconds, copies the instruction pointer and
// the top of the stack, lets the thread run again, and then walks that copy with the unwind data registered for the
// running code (RtlLookupFunctionEntry / RtlVirtualUnwind). Each sample adds one "self" count to the function that was
// running and one "total" count to every function on the stack. Every two minutes it logs the hottest functions of each
// thread. Game logic (game.gcl) functions are named by their code size and first 32 bytes, which
// native/tools/map_profile.py turns into names with the game's game.gcl; game.exe and other modules by name and offset.
// Nothing is allocated, logged or looked up while a thread is paused.

constexpr int kProfileIntervalMs = 2, kProfileReportSeconds = 120, kProfileTop = 25, kProfileDepth = 64;
constexpr size_t kStackCopy = 128 * 1024, kStackSlack = 64 * 1024;

struct ProfiledThread {
    const char* name;
    DWORD id = 0;
    HANDLE handle = nullptr;
    uint64_t stack_base = 0;      // highest address of the thread's stack (TEB StackBase)
    uint32_t samples = 0;
    SampleTable table;
};
ProfiledThread g_prof_main{"main"}, g_prof_server{"server"};
alignas(16) uint8_t g_stack_copy[kStackCopy + kStackSlack];   // slack: unwind reads past a short copy stay in bounds

using nt_query_thread_t = LONG(NTAPI*)(HANDLE, int, void*, ULONG, ULONG*);
struct ThreadBasicInfo { LONG exit_status; void* teb; void* pid; void* tid; ULONG_PTR affinity; LONG priority, base_priority; };

bool open_profiled(ProfiledThread& t, DWORD id) {
    if (!id) return false;
    if (t.id == id && t.handle) return true;
    if (t.handle) CloseHandle(t.handle);
    t.handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
    t.id = id; t.stack_base = 0;
    if (!t.handle) return false;
    static auto query = reinterpret_cast<nt_query_thread_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    ThreadBasicInfo info{};
    if (query && query(t.handle, 0, &info, sizeof info, nullptr) >= 0 && info.teb &&
        anymaker::readable(static_cast<uint8_t*>(info.teb) + 8, 8))
        t.stack_base = *reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(info.teb) + 8);   // NT_TIB.StackBase
    if (!t.stack_base) { CloseHandle(t.handle); t.handle = nullptr; return false; }
    return true;
}

void sample(ProfiledThread& t) {
    CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (SuspendThread(t.handle) == DWORD(-1)) return;
    bool got = GetThreadContext(t.handle, &ctx) != 0;
    size_t size = 0;
    if (got && ctx.Rsp && ctx.Rsp < t.stack_base) {
        size = size_t(std::min<uint64_t>(t.stack_base - ctx.Rsp, kStackCopy));
        std::memcpy(g_stack_copy, reinterpret_cast<const void*>(ctx.Rsp), size);   // committed: between rsp and the base
    }
    ResumeThread(t.handle);
    if (!got || !size) return;
    ++t.samples;
    const uint64_t real = ctx.Rsp, copy = reinterpret_cast<uint64_t>(g_stack_copy);
    auto in_real = [&](uint64_t a) { return a >= real && a < real + size; };
    auto in_copy = [&](uint64_t a) { return a >= copy && a < copy + size; };
    CONTEXT c = ctx;
    c.Rsp = copy;
    if (in_real(c.Rbp)) c.Rbp = copy + (c.Rbp - real);
    uint64_t seen[kProfileDepth]; int nseen = 0;
    for (int depth = 0; depth < kProfileDepth && c.Rip; ++depth) {
        DWORD64 image = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
        uint64_t key = fn ? image + fn->BeginAddress : (c.Rip & ~63ull) | 1;   // low bit: a 64-byte block, function unknown
        uint32_t code = fn ? fn->EndAddress - fn->BeginAddress : 0;
        if (depth == 0) t.table.add_self(key, code);
        bool dup = false;
        for (int i = 0; i < nseen; ++i) dup |= seen[i] == key;
        if (!dup) { t.table.add_total(key, code); seen[nseen++] = key; }
        if (!fn) {   // a leaf without unwind data: its return address is on top of the stack
            if (depth || !in_copy(c.Rsp)) break;
            c.Rip = *reinterpret_cast<uint64_t*>(c.Rsp); c.Rsp += 8;
            continue;
        }
        void* handler = nullptr; DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, fn, &c, &handler, &frame, nullptr);
        if (in_real(c.Rbp)) c.Rbp = copy + (c.Rbp - real);
        if (!in_copy(c.Rsp)) break;   // walked off the copied part of the stack
    }
}

// "game.exe+0x1a2b3c", "AnyAPI.dll+0x1234" or "gcl size 640 code 53 55 56 48 ..." for one table key.
void describe_code(char* out, size_t cap, uint64_t key, uint32_t code) {
    bool block = key & 1;
    auto* at = reinterpret_cast<uint8_t*>(key & ~1ull);
    HMODULE module = nullptr; wchar_t path[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(at), &module) && module &&
        GetModuleFileNameW(module, path, MAX_PATH)) {
        const wchar_t* file = std::wcsrchr(path, L'\\'); file = file ? file + 1 : path;
        std::snprintf(out, cap, "%ls+0x%llx%s", file, (unsigned long long)(at - reinterpret_cast<uint8_t*>(module)),
                      block ? " (block)" : "");
        return;
    }
    int n = block ? std::snprintf(out, cap, "gcl block code") : std::snprintf(out, cap, "gcl size %u code", code);
    if (anymaker::readable(at, 32))
        for (int i = 0; i < 32 && n > 0 && size_t(n) + 4 < cap; ++i) n += std::snprintf(out + n, cap - n, " %02x", at[i]);
}

void report_profile(ProfiledThread& t, double seconds) {
    if (!t.samples) return;
    log(0, "Profile %s thread: %u samples in %.0f s (every %d ms). self = running that function, total = it or what it called",
        t.name, t.samples, seconds, kProfileIntervalMs);
    int idx[kProfileTop];
    for (int pass = 0; pass < 2; ++pass) {
        int n = t.table.top(idx, kProfileTop, pass == 1);
        for (int i = 0; i < n; ++i) {
            char what[200];
            int k = idx[i];
            describe_code(what, sizeof what, t.table.key[k], t.table.extra[k]);
            log(0, "Profile %s %s #%d: self %.1f%% total %.1f%% | %s", t.name, pass ? "total" : "self", i + 1,
                100.0 * t.table.self[k] / t.samples, 100.0 * t.table.total[k] / t.samples, what);
        }
    }
    t.table.clear(); t.samples = 0;
}

void profiler_thread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    double started = now_ms();
    bool was_on = false;
    while (!g_stop) {
        if (!g_profiler) {
            if (was_on) { g_prof_main.table.clear(); g_prof_main.samples = 0; g_prof_server.table.clear(); g_prof_server.samples = 0; }
            was_on = false; Sleep(250); continue;
        }
        if (!was_on) { was_on = true; started = now_ms(); log(0, "Profiler on: sampling the main and server threads every %d ms", kProfileIntervalMs); }
        if (open_profiled(g_prof_main, g_main_thread.load())) sample(g_prof_main);
        if (open_profiled(g_prof_server, g_server_thread.load(std::memory_order_relaxed))) sample(g_prof_server);
        double now = now_ms();
        if (now - started >= kProfileReportSeconds * 1000.0) {
            report_profile(g_prof_main, (now - started) / 1000.0);
            report_profile(g_prof_server, (now - started) / 1000.0);
            started = now;
        }
        Sleep(kProfileIntervalMs);
    }
    if (g_prof_main.handle) CloseHandle(g_prof_main.handle);
    if (g_prof_server.handle) CloseHandle(g_prof_server.handle);
}
