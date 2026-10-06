#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>

// Internal primitive, not a public raw-pointer API. Providers stay loaded for the
// process lifetime. Removal detaches future cell loads; it does not permit unloading
// code already fetched by another thread. A hook object may only own one cell.
namespace anyapi_runtime {
class CallCellHook {
    std::mutex mutex_;
    void** cell_{};
    void* replacement_{};
    std::atomic<void*> original_{};
    bool attached_{};
public:
    CallCellHook() = default;
    CallCellHook(const CallCellHook&) = delete;
    CallCellHook& operator=(const CallCellHook&) = delete;
    void* original() const { return original_.load(std::memory_order_acquire); }
    bool install(void** cell, void* replacement) {
        std::lock_guard lock(mutex_);
        if (!cell || !replacement || (uintptr_t(cell) % alignof(void*)) || attached_
            || (cell_ && cell_ != cell)) return false;
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(cell, &region, sizeof(region)) || region.State != MEM_COMMIT
            || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            || !(region.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))
            || uintptr_t(cell) + sizeof(void*) > uintptr_t(region.BaseAddress) + region.RegionSize)
            return false;
        auto current = InterlockedCompareExchangePointer(cell, nullptr, nullptr);
        if (!current || current == replacement) return false;
        // A detached hook can still have in-flight calls. Its original must remain
        // stable; don't reattach atop a different chain or retarget its thunk.
        if (original() && (original() != current || replacement_ != replacement)) return false;
        original_.store(current, std::memory_order_release);
        replacement_ = replacement;
        if (InterlockedCompareExchangePointer(cell, replacement, current) != current) return false;
        cell_ = cell; replacement_ = replacement; attached_ = true;
        return true;
    }
    bool remove() {
        std::lock_guard lock(mutex_);
        if (!attached_) return true;
        if (InterlockedCompareExchangePointer(cell_, original(), replacement_) != replacement_)
            return false; // another owner is above us; leave its chain intact
        attached_ = false;
        return true;
    }
};
}
