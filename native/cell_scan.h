#pragma once
// Finds the call cell of a gcl function that the experimental SDK can locate by code signature but has no
// route to its cell (no caller route, no typeinfo anchor). Every call to a gcl function, direct or virtual,
// reads one 8-byte cell holding its entry point. Cells live in one writable allocation, where the game keeps
// the cell and a copy of it side by side (seen in game: two adjacent words per function there, plus copies
// in other allocations). Slow: call once from a background thread, before hooking the known cell.
#include "anymaker_sdk_runtime.hpp"
#include <cstdint>

namespace cell_scan {
inline bool writable_private(const MEMORY_BASIC_INFORMATION& m) {
    return m.State == MEM_COMMIT && m.Type == MEM_PRIVATE && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           (m.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
}
// Collects up to `max` aligned words equal to `value` in the writable private regions of the allocation
// containing `inside`, returning how many there are. Reads through ReadProcessMemory in chunks so a region
// the game frees mid-scan cannot fault. This thread's buffer and stack hold our own copies: skipped.
inline int scan_allocation(const void* inside, uint64_t value, void** out[], int max) {
    static thread_local uint64_t buf[8192];
    ULONG_PTR stack_lo = 0, stack_hi = 0;
    GetCurrentThreadStackLimits(&stack_lo, &stack_hi);
    MEMORY_BASIC_INFORMATION m{};
    if (!inside || !VirtualQuery(inside, &m, sizeof m)) return 0;
    void* allocation = m.AllocationBase; int found = 0;
    for (uint8_t* a = static_cast<uint8_t*>(allocation); VirtualQuery(a, &m, sizeof m) && m.AllocationBase == allocation;
         a = static_cast<uint8_t*>(m.BaseAddress) + m.RegionSize) {
        if (!writable_private(m)) continue;
        uint8_t* base = static_cast<uint8_t*>(m.BaseAddress);
        for (size_t off = 0; off < m.RegionSize; off += sizeof buf) {
            size_t want = m.RegionSize - off < sizeof buf ? m.RegionSize - off : sizeof buf;
            SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), base + off, buf, want, &got)) continue;
            for (size_t i = 0; i < got / 8; ++i) {
                if (buf[i] != value) continue;
                uintptr_t at = uintptr_t(base + off + i * 8);
                if ((at >= uintptr_t(buf) && at < uintptr_t(buf) + sizeof buf) || (at >= stack_lo && at < stack_hi)) continue;
                if (found < max) out[found] = reinterpret_cast<void**>(at);
                ++found;
            }
        }
    }
    return found;
}

// The cell of `entry`. `known_cell` is the cell of another function of the same program, found through an
// SDK route and NOT hooked yet: it marks the allocation to search and shows the layout. The game keeps two
// adjacent words holding each entry point there (the call cell and a copy next to it), so a pair is
// resolved to the same side the known cell sits on within its own pair.
inline void** find_cell(const void* entry, void** known_cell) {
    if (!entry || !known_cell || !anymaker::readable(known_cell - 1, 24)) return nullptr;
    void** hits[3]{};
    int n = scan_allocation(known_cell, reinterpret_cast<uintptr_t>(entry), hits, 3);
    if (n == 1) return hits[0];
    if (n != 2) return nullptr;
    void** lo = hits[0] < hits[1] ? hits[0] : hits[1];
    void** hi = hits[0] < hits[1] ? hits[1] : hits[0];
    if (hi != lo + 1) return nullptr;
    void* known_entry = *known_cell;
    if (known_cell[1] == known_entry && known_cell[-1] != known_entry) return lo;   // known cell is the lower word
    if (known_cell[-1] == known_entry && known_cell[1] != known_entry) return hi;   // known cell is the upper word
    return nullptr;
}
}  // namespace cell_scan
