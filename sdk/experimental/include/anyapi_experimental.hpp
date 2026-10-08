#pragma once
#include "anymaker_sdk_symbols.hpp"
#include "../../include/anyapi_services_v1.h"
#include "../../include/anyapi_build_v1.h"

namespace anymaker::experimental {
// Call after ModReady and JIT initialization, on the owning thread. Resolving an
// address does not validate a function's ABI, object lifetime or authority.
inline bool matching_build() {
    const auto services = AnyAPI_Services();
    const auto build = services ? static_cast<const AnyBuildV1*>(services->query("anyapi.build", 1)) : nullptr;
    AnyBuildInfoV1 info;
    return build && build->struct_size == sizeof(*build) && build->version == 1 && build->copy &&
        build->copy(&info) && info.status == ANY_BUILD_MATCH &&
        std::memcmp(info.executable_sha256, sym::game_exe_sha256, 65) == 0 &&
        std::memcmp(info.game_data_sha256, sym::game_gcl_sha256, 65) == 0;
}
inline void* function(const func_desc& descriptor) {
    return matching_build() ? anymaker::resolve(descriptor) : nullptr;
}
inline void** hook_cell(const func_desc& descriptor) {
    return matching_build() ? anymaker::cell_of(descriptor) : nullptr;
}
inline void* global(const global_desc& descriptor) {
    if (!matching_build()) return nullptr;
    auto anchor = find_gcl_function(descriptor.anchor_sig);
    return anchor && readable(anchor + descriptor.slot_offset, 8)
        ? global_from_anchor<void>(anchor, descriptor.slot_offset) : nullptr;
}
inline void* native(uint32_t rva) {
    if (!matching_build()) return nullptr;
    auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (!rva || rva >= nt->OptionalHeader.SizeOfImage) return nullptr;
    auto entry = native_at(rva); MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(entry, &region, sizeof(region)) || region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return nullptr;
    return entry;
}
} // namespace anymaker::experimental
