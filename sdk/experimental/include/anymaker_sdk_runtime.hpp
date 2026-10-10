// Anymaker SDK runtime helpers (hand-written; Windows x64, MSVC or clang-cl, C++17).
//
// Everything here encodes rules that validation/probe.py checked in a running game (evidence
// "runtime", see json/runtime_evidence.json) or that game.gcl records directly ("metadata").
// Rules that are only static inference say so next to them.
//
//   locate   : gcl functions live in private executable memory (asmjit), start on 64-byte boundaries
//              [runtime], and their code bytes equal game.gcl [runtime: 26,706 located, 0 differ].
//   slots    : after a function's code (gcl code_end) come 8-byte slots, one per relocation, in record
//              order [runtime]. global slot -> global address [runtime: 870 globals, all slots agree].
//              call slot -> cell, cell -> entry [runtime: 52,511 call sites, 0 mismatches].
//   dispatch : object+0 -> typeinfo; typeinfo+0x28 -> parent typeinfo; typeinfo+0x80 -> table;
//              table + 8*slot -> cell -> entry, slot = zero-based method-table index [runtime: 53,916
//              entries matched, 0 mismatches]. The virtual slot relocation value is 8*slot [runtime].
//   abi      : every argument by pointer; non-void return through a hidden pointer in rcx; rcx, rdx, r8,
//              r9 then stack from [rsp+0x28] [metadata/static: prologue spills of 34,405 functions].
//              Not yet exercised by calling from native code (validation/calltest).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace anymaker {

// ------------------------------------------------------------------------------------ containers
// string: 16 bytes. +0x00 char* UTF-8, NUL-terminated [runtime: 15/15 string globals decoded];
// +0x08 int32 length in bytes [metadata: string.length thunk; runtime: matches the NUL position];
// +0x0C unknown. Never write these fields directly: ownership of the buffer is unknown. Use the
// $string_* natives (json/native_bindings.json gives their RVAs) to construct, copy and destroy.
struct gc_string_view {
    const char* data;
    int32_t length;
    uint32_t _unknown_0c;
    std::string_view view() const { return data && length > 0 ? std::string_view(data, (size_t)length) : std::string_view(); }
};
static_assert(sizeof(gc_string_view) == 16, "gc_string");

// vector<T>: 40 bytes, ring buffer [static: vector<$>.value thunk; runtime: count<=capacity and
// element_size == sizeof(T) for every decoded vector global]. Read-only access helpers; growing or
// shrinking must go through the vector natives because the allocator is unknown.
struct gc_vector_raw {
    uint8_t* buffer;      // +0x00
    int32_t offset;       // +0x08 ring start
    int32_t count;        // +0x0C
    int32_t capacity;     // +0x10
    uint32_t element_size;// +0x14
    uint8_t _unknown_18[0x10];
    template <class T> T* at(int32_t i) const {
        if (!buffer || i < 0 || i >= count || count < 0 || count > capacity || capacity <= 0 || offset < 0 || offset >= capacity || element_size != sizeof(T)) return nullptr;
        return reinterpret_cast<T*>(buffer + (size_t)((int64_t(offset) + i) % capacity) * element_size);
    }
};
static_assert(sizeof(gc_vector_raw) == 40, "gc_vector");

// array<T>: 32 bytes [static: array<$>.value thunk]. Not yet seen at runtime.
struct gc_array_raw {
    uint8_t* buffer;      // +0x00
    int32_t count;        // +0x08
    uint32_t element_size;// +0x0C
    uint8_t _unknown_10[0x10];
    template <class T> T* at(int32_t i) const {
        return (buffer && i >= 0 && i < count && element_size == sizeof(T)) ? reinterpret_cast<T*>(buffer + (size_t)i * element_size) : nullptr;
    }
};
static_assert(sizeof(gc_array_raw) == 32, "gc_array");

// ref<T>: 16 bytes. +0x08 points at the object/value [runtime: ref<f64>, ref<s32>, ref<main_menu>
// globals]; +0x00 is a control/allocation pointer whose layout is unknown. Borrow ref.ptr only; never
// copy a ref<T> bytewise (that would skip the reference count).
template <class T> struct gc_ref_raw {
    void* _control;
    T* ptr;
};
static_assert(sizeof(gc_ref_raw<void>) == 16, "gc_ref");

// ------------------------------------------------------------------------------------ typeinfo
struct gc_typeinfo_view {
    const uint8_t* p;
    uint32_t kind() const { return *reinterpret_cast<const uint32_t*>(p); }  // 13 struct, 14 enum [runtime]
    const uint8_t* parent() const { return *reinterpret_cast<const uint8_t* const*>(p + 0x28); }
    void* const* table() const { return *reinterpret_cast<void* const* const*>(p + 0x80); }
    // entry point of virtual slot `slot` (zero-based index in the dynamic type's method table)
    void* method(int slot) const {
        void* const* t = table();
        if (!t) return nullptr;
        void* cell = t[slot];
        return cell ? *reinterpret_cast<void**>(cell) : nullptr;
    }
};
inline gc_typeinfo_view typeinfo_of(const void* gc_object) {
    return gc_typeinfo_view{*reinterpret_cast<const uint8_t* const*>(gc_object)};
}
// true if `ti` is `target` or derives from it
inline bool typeinfo_is(gc_typeinfo_view ti, const uint8_t* target) {
    for (const uint8_t* p = ti.p; p; p = *reinterpret_cast<const uint8_t* const*>(p + 0x28))
        if (p == target) return true;
    return false;
}

// ------------------------------------------------------------------------------------ memory safety
// Readable committed memory check; use before dereferencing pointers you did not get from the game
// in this same tick. It does not make a dangling game pointer safe, only avoids an access violation.
inline bool readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!p || !VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const auto offset = uintptr_t(p) - uintptr_t(mbi.BaseAddress);
    return offset <= mbi.RegionSize && n <= mbi.RegionSize - offset;
}

// ------------------------------------------------------------------------------------ locating
// Pattern: hex bytes separated by spaces, "??" wildcard (same format as json code_signature and
// natives.json thunk_signature).
struct pattern {
    std::vector<int> bytes;
    explicit pattern(std::string_view s) {
        auto hex=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
        bool concrete=false;
        for(size_t i=0;i<s.size();) {
            while(i<s.size() && (s[i]==' '||s[i]=='\t'))++i;
            if(i==s.size())break;
            if(i+1>=s.size()){bytes.clear();return;}
            if(s[i]=='?'&&s[i+1]=='?')bytes.push_back(-1);
            else {int a=hex(s[i]),b=hex(s[i+1]);if(a<0||b<0){bytes.clear();return;}bytes.push_back(a*16+b);concrete=true;}
            i+=2;if(i<s.size()&&s[i]!=' '&&s[i]!='\t'){bytes.clear();return;}
        }
        if(!concrete)bytes.clear();
    }
    bool match(const uint8_t* p) const {
        for (size_t k = 0; k < bytes.size(); ++k)
            if (bytes[k] >= 0 && p[k] != (uint8_t)bytes[k]) return false;
        return true;
    }
};

// Find a gcl function by its code signature. Scans committed MEM_PRIVATE executable regions at
// 64-byte steps (gcl functions start 64-byte aligned [runtime]). Returns nullptr if not found or not
// unique. Call it after the game's JIT load has finished (any time after on_create has run).
// Every gcl function starting with the code signature, up to max_hits (0 if there are more).
inline size_t find_gcl_functions(std::string_view code_signature, uint8_t** hits, size_t max_hits) {
    pattern pat(code_signature);
    if (pat.bytes.empty() || !max_hits) return 0;
    size_t count = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    for (uint8_t* a = nullptr; VirtualQuery(a, &mbi, sizeof(mbi)); a = (uint8_t*)mbi.BaseAddress + mbi.RegionSize) {
        const DWORD x = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || !(mbi.Protect & x) || (mbi.Protect & PAGE_GUARD))
            continue;
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        for (size_t off = 0; off + pat.bytes.size() <= mbi.RegionSize; off += 64)
            if (pat.match(base + off)) {
                if (count == max_hits) return 0;
                hits[count++] = base + off;
            }
        if ((uintptr_t)a + mbi.RegionSize < (uintptr_t)a) break;
    }
    return count;
}
inline uint8_t* find_gcl_function(std::string_view code_signature) {
    pattern pat(code_signature);
    if (pat.bytes.empty()) return nullptr;
    uint8_t* hit = nullptr;
    MEMORY_BASIC_INFORMATION mbi{};
    for (uint8_t* a = nullptr; VirtualQuery(a, &mbi, sizeof(mbi)); a = (uint8_t*)mbi.BaseAddress + mbi.RegionSize) {
        const DWORD x = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || !(mbi.Protect & x) || (mbi.Protect & PAGE_GUARD))
            continue;
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t n = mbi.RegionSize;
        for (size_t off = 0; off + pat.bytes.size() <= n; off += 64) {
            if (pat.match(base + off)) {
                if (hit) return nullptr;   // not unique: refuse rather than guess
                hit = base + off;
            }
        }
        if ((uintptr_t)a + mbi.RegionSize < (uintptr_t)a) break;
    }
    return hit;
}

// Slot k of a located function: functions.json gcl.code_size is code_end; anchors.json gives
// slot_offset = code_end + 8*k directly.
inline uint64_t read_slot(const uint8_t* fn, uint32_t slot_offset) {
    uint64_t value{};if(!fn || uintptr_t(fn)>UINTPTR_MAX-slot_offset)return 0;
    SIZE_T copied{};ReadProcessMemory(GetCurrentProcess(),fn+slot_offset,&value,8,&copied);return copied==8?value:0;
}
// global address from a global_address anchor
template <class T> T* global_from_anchor(const uint8_t* anchor_fn, uint32_t slot_offset) {
    return reinterpret_cast<T*>(read_slot(anchor_fn, slot_offset));
}
// call cell from a call_cell anchor; *cell is the callee entry point
inline void** cell_from_anchor(const uint8_t* anchor_fn, uint32_t slot_offset) {
    return reinterpret_cast<void**>(read_slot(anchor_fn, slot_offset));
}

// game.exe natives: base + RVA from natives.json (static) or native_bindings.json (runtime).
inline void* native_at(uint32_t rva) {
    return (uint8_t*)GetModuleHandleW(nullptr) + rva;
}

// ------------------------------------------------------------------------------------ descriptors
// Generated per build by tools/sdk_codegen.py into anymaker_sdk_symbols.hpp.
struct slot_route {            // root function (unique code signature) + slot offsets; each hop reads a
    const char* root_sig;      // slot (a cell); the next hop starts at *cell. The final read gives the
    uint32_t hops[8];          // cell of the target, so *cell is its entry point.
    uint8_t nhops;
};
struct func_desc {
    const char* sig;           // gcl signature (stable ID is "FN:" + sig)
    const char* code_sig;      // unique code signature, or "" if none
    slot_route call_route;     // route to the target's call cell, nhops == 0 if none
    const char* ti_anchor_sig; // function whose typeinfo slot names the owning type, or ""
    uint32_t ti_slot_offset;
    int32_t method_slot;       // zero-based method-table slot when reached through typeinfo, -1 if none
};
struct global_desc {
    const char* name;
    const char* anchor_sig;
    uint32_t slot_offset;
};

inline void** follow_route(const slot_route& r) {
    if (!r.nhops || r.nhops > 8 || !r.root_sig || !*r.root_sig) return nullptr;
    uint8_t* fn = find_gcl_function(r.root_sig);
    void** cell = nullptr;
    for (uint8_t i = 0; fn && i < r.nhops; ++i) {
        cell = reinterpret_cast<void**>(read_slot(fn, r.hops[i]));
        if (!readable(cell, 8)) return nullptr;
        fn = reinterpret_cast<uint8_t*>(*cell);
    }
    return cell;
}
inline void** typeinfo_cell_from(const uint8_t* anchor, const func_desc& d) {
    auto* ti = reinterpret_cast<const uint8_t*>(read_slot(anchor, d.ti_slot_offset));
    if (!readable(ti, 0x88)) return nullptr;
    void* const* table = *reinterpret_cast<void* const* const*>(ti + 0x80);
    if (!readable(table, 8 * (d.method_slot + 1))) return nullptr;
    auto cell = reinterpret_cast<void**>(table[d.method_slot]);
    return readable(cell, 8) ? cell : nullptr;
}
inline void** typeinfo_cell(const func_desc& d) {
    if (!d.ti_anchor_sig || !*d.ti_anchor_sig || d.method_slot < 0) return nullptr;
    if (uint8_t* a = find_gcl_function(d.ti_anchor_sig)) return typeinfo_cell_from(a, d);
    // Shared anchor (identical code in several functions, e.g. two tools' state ctors): keep the
    // one candidate whose method cell points at this function's own unique code.
    uint8_t* entry = d.code_sig && *d.code_sig ? find_gcl_function(d.code_sig) : nullptr;
    uint8_t* hits[16];
    size_t n = entry ? find_gcl_functions(d.ti_anchor_sig, hits, 16) : 0;
    void** found = nullptr;
    for (size_t i = 0; i < n; ++i)
        if (void** cell = typeinfo_cell_from(hits[i], d); cell && *cell == entry) {
            if (found && found != cell) return nullptr;
            found = cell;
        }
    return found;
}
// The 8-byte cell every caller (direct and virtual) reads for this function; nullptr if no route.
inline void** cell_of(const func_desc& d) {
    if (void** c = follow_route(d.call_route)) return c;
    return typeinfo_cell(d);
}
// Entry point of the function (cached by the caller; resolving scans memory).
inline void* resolve(const func_desc& d) {
    if (d.code_sig && *d.code_sig)
        if (uint8_t* p = find_gcl_function(d.code_sig)) return p;
    if (void** c = cell_of(d)) return *c;
    return nullptr;
}
template <class T> T* resolve_global(const global_desc& g) {
    uint8_t* a = find_gcl_function(g.anchor_sig);
    return a ? global_from_anchor<T>(a, g.slot_offset) : nullptr;
}

// ------------------------------------------------------------------------------------ cell hooks
// Redirect every call of a gcl function (direct and virtual) by swapping its cell. Runtime facts: one
// cell per function, dispatch tables point at the same cell, cells are PAGE_READWRITE. Install and
// remove only while no other mod swaps the same cell; chain by calling `original` from the hook.
struct cell_hook {
    void** cell = nullptr;
    void* original = nullptr;
    bool install(void** c, void* replacement) {
        if (!c || !replacement || (uintptr_t(c)%8) || !readable(c, 8) || cell) return false;
        MEMORY_BASIC_INFORMATION region{};if(!VirtualQuery(c,&region,sizeof(region)) || !(region.Protect & (PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        void* cur = *c;
        if (InterlockedCompareExchangePointer(c, replacement, cur) != cur) return false;
        cell = c;
        original = cur;
        return true;
    }
    // Restores only if nobody re-hooked on top of us; otherwise leaves the cell alone and returns false.
    bool remove(void* replacement) {
        if (!cell) return true;
        if (InterlockedCompareExchangePointer(cell, original, replacement) != replacement) return false;
        cell = nullptr;
        return true;
    }
};

// ------------------------------------------------------------------------------------ build guard
// Cheap identity check: compare game.exe PE timestamp and game.gcl size with the values the SDK was
// generated from (include/anymaker_sdk_types.hpp ANYMAKER_SDK_* macros carry the hashes for a full
// check). Refuse to resolve anything when this fails.
inline uint32_t exe_pe_timestamp() {
    auto* b = (const uint8_t*)GetModuleHandleW(nullptr);
    auto* dos = (const IMAGE_DOS_HEADER*)b;
    auto* nt = (const IMAGE_NT_HEADERS64*)(b + dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}

}  // namespace anymaker
