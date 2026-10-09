#pragma once
// Finds the call cell of a gcl function that the experimental SDK can locate by code signature but has no
// route to its cell (no caller route, no typeinfo anchor). Every call to a gcl function, direct or virtual,
// reads one 8-byte cell holding its entry point, and the cells live in writable private memory. So the cell
// is the one aligned 8-byte word in that memory equal to the entry point. Searches the allocation holding a
// known cell first, then all writable private memory, and returns nothing unless exactly one word matches.
// Slow (a full scan can take a second or two): call once from a background thread.
#include "anymaker_sdk_runtime.hpp"
#include <cstdint>

namespace cell_scan {
inline bool writable_private(const MEMORY_BASIC_INFORMATION& m) {
    return m.State == MEM_COMMIT && m.Type == MEM_PRIVATE && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           (m.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
}
// Counts matches in [base, base + size); stops counting at 2. Reads through ReadProcessMemory in chunks so a
// region the game frees mid-scan cannot fault.
inline int scan_region(uint8_t* base, size_t size, uint64_t value, void**& hit) {
    static thread_local uint64_t buf[8192];
    int found = 0;
    for (size_t off = 0; off < size; off += sizeof buf) {
        size_t want = size - off < sizeof buf ? size - off : sizeof buf;
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), base + off, buf, want, &got)) continue;
        for (size_t i = 0; i < got / 8; ++i) {
            if (buf[i] != value) continue;
            hit = reinterpret_cast<void**>(base + off + i * 8);
            if (++found > 1) return found;
        }
    }
    return found;
}
inline void** find_cell(const void* entry, const void* near_cell) {
    if (!entry) return nullptr;
    uint64_t value = reinterpret_cast<uintptr_t>(entry);
    MEMORY_BASIC_INFORMATION m{};
    void** hit = nullptr;
    // 1. The allocation that already holds a known cell of the same program.
    if (near_cell && VirtualQuery(near_cell, &m, sizeof m)) {
        void* allocation = m.AllocationBase; int found = 0;
        for (uint8_t* a = static_cast<uint8_t*>(allocation); VirtualQuery(a, &m, sizeof m) && m.AllocationBase == allocation;
             a = static_cast<uint8_t*>(m.BaseAddress) + m.RegionSize)
            if (writable_private(m) && (found += scan_region(static_cast<uint8_t*>(m.BaseAddress), m.RegionSize, value, hit)) > 1) return nullptr;
        if (found == 1) return hit;
    }
    // 2. All writable private memory.
    int found = 0;
    for (uint8_t* a = nullptr; VirtualQuery(a, &m, sizeof m); a = static_cast<uint8_t*>(m.BaseAddress) + m.RegionSize) {
        if (writable_private(m) && (found += scan_region(static_cast<uint8_t*>(m.BaseAddress), m.RegionSize, value, hit)) > 1) return nullptr;
        if (uintptr_t(m.BaseAddress) + m.RegionSize < uintptr_t(m.BaseAddress)) break;
    }
    return found == 1 ? hit : nullptr;
}
}  // namespace cell_scan
