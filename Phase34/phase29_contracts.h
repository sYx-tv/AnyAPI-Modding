#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// A pointer is an address, not an identity. Tickets are INTERNAL in API v16.
// Owners must hold their registry lock while issuing/checking a ticket.
struct P29Ticket {
    uintptr_t address{};
    uint64_t generation{};
    uintptr_t owner{};
    bool operator==(const P29Ticket&) const = default;
};
struct P29Identity {
    P29Ticket ticket{};
    bool live{};
    bool begin(uintptr_t address, uintptr_t owner, uint64_t generation) {
        if (!address || !generation) { live=false; return false; }
        ticket = {address, generation, owner}; live = true; return true;
    }
    bool accepts(P29Ticket candidate) const {
        return live && candidate.generation && ticket == candidate;
    }
    void retire() { live = false; }
};

// Reattachment at a dead address starts a new lifetime. Do not merge metadata
// from the previous object; live factory metadata still participates in checks.
template<class Entry> bool p29_reset_retired_component(Entry& entry,
    uintptr_t address, uintptr_t owner, uint64_t generation) {
    if(entry.identity.live) return false;
    entry.class_name[0]=0;
    entry.metadata_definition=0;
    entry.class_source=0;
    entry.vehicle=0;
    return entry.identity.begin(address,owner,generation);
}

// No guessed native writes: this bounded scheduler carries COPIED values only.
// Synchronization belongs to the caller (SRWLOCK in the DLL).
template<class T, size_t Capacity> struct P29SnapshotQueue {
    struct Row { P29Ticket identity; T value; };
    std::array<Row, Capacity> rows{};
    size_t head{}, tail{}, count{};
    uint64_t dropped{}, stale{};
    bool push(P29Ticket identity, const T& value) {
        if (count == Capacity) { ++dropped; return false; }
        rows[tail] = {identity, value}; tail = (tail + 1) % Capacity; ++count;
        return true;
    }
    template<class Accept> bool pop(T& out, Accept accept) {
        while (count) {
            const auto row = rows[head]; head = (head + 1) % Capacity; --count;
            if (!accept(row.identity)) { ++stale; continue; }
            out = row.value; return true;
        }
        return false;
    }
};

// Lock-free scalar telemetry; multiple native producers can share a hook.
struct P29ThreadRecord {
    std::atomic<uint32_t> first{}, last{};
    std::atomic<uint64_t> calls{}, other_thread_calls{}, transitions{};
    void observe(uint32_t id) {
        uint32_t zero = 0; first.compare_exchange_strong(zero, id);
        if (id != first.load()) other_thread_calls.fetch_add(1);
        const auto previous = last.exchange(id);
        if (previous && previous != id) transitions.fetch_add(1);
        calls.fetch_add(1);
    }
};

// Pinned actor world mat34: 4 contiguous columns of vec3<f64>.
// Component local-to-grid transforms must not be mislabeled as world-space.
inline bool p29_translation(const double matrix[12], double out[3]) {
    for (size_t i = 0; i < 12; ++i) if (!std::isfinite(matrix[i])) return false;
    for (size_t i = 0; i < 3; ++i) out[i] = matrix[9 + i];
    return true;
}

template<class Entry, class State> bool p29_copy_live_snapshot(
    const Entry* entries, size_t count, uintptr_t actor, State* out) {
    if (!out) return false;
    *out = {};
    if (!actor) return false;
    for (size_t i = 0; i < count; ++i) {
        const auto& entry = entries[i];
        if (entry.actor == actor && entry.live && entry.have_snapshot && entry.identity.live) {
            *out = entry.last_state; return true;
        }
    }
    return false;
}
