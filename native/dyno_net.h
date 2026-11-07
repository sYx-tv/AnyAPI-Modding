#pragma once
// Dyno and ECU screens for co-op joiners (Nate 2026-11-07: "the dyno and the ecu tuning arent showing up on the
// joiner side"). Both screens read numbers only the host's server ticks make, so a joiner's game has none. The host
// sends each joiner the Dyno graphs and ECUs near him a few times a second over the game's own Steam sessions
// (steam_messages.h, its own far channel); the joiner shows them as if they were his, and his ECU panel changes and
// Dyno clears go back to the host, which checks the sender is a player in the world standing near that part.
// Both ends run the same AnyVehicleOverhaul build: a mismatched build's messages are dropped (sizes in the header).
// Platform-free (tests build it on Linux): little-endian, every read bounded.
#include "dyno_logic.h"
#include "ecu_logic.h"
#include "gauges_logic.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace dyno_net {
using dyno::Recorder;
constexpr uint32_t kMagic = 0x59445641;        // "AVDY"
constexpr uint16_t kProtocol = 1;
constexpr int32_t kChannel = 0x41564459;       // far from the game's small channel numbers and AnyCareer's
constexpr int kMaxDynos = 3, kMaxEcus = 4;
constexpr size_t kSetupChars = 48;
constexpr double kNearJoiner = 60.0;           // the host sends parts within this of the joiner
constexpr double kReachSlack = 6.0;            // an ECU change counts within kEcuReach plus this (lag)
static_assert(std::is_trivially_copyable_v<Recorder>, "the Dyno's recorder goes on the wire as bytes");
static_assert(std::is_trivially_copyable_v<ecu::Tune>, "the ECU tune goes on the wire as bytes");

enum Kind : uint8_t { kSnapshot = 1, kSetTune = 2, kClearDyno = 3 };

struct DynoEntry { uint64_t key = 0; double position[3]{}; char setup[kSetupChars]{}; Recorder recorder; };
struct EcuEntry { uint64_t key = 0; double position[3]{}; ecu::Tune panel; double slots[gauges::kMaxSlots]{}; };
struct Snapshot { std::vector<DynoEntry> dynos; std::vector<EcuEntry> ecus; };
struct Request { uint8_t kind = kSetTune; uint64_t key = 0; ecu::Tune panel; };

struct Header {
    uint32_t magic; uint16_t protocol; uint8_t kind, a, b, pad[3];
    uint32_t recorder_size, tune_size, slots;
};
static_assert(sizeof(Header) == 24, "wire header");
inline Header header(uint8_t kind, uint8_t a = 0, uint8_t b = 0) {
    return {kMagic, kProtocol, kind, a, b, {0, 0, 0}, uint32_t(sizeof(Recorder)), uint32_t(sizeof(ecu::Tune)), uint32_t(gauges::kMaxSlots)};
}
inline bool header_ok(const Header& h) {
    return h.magic == kMagic && h.protocol == kProtocol && h.recorder_size == sizeof(Recorder) && h.tune_size == sizeof(ecu::Tune) &&
           h.slots == uint32_t(gauges::kMaxSlots);
}
template <class T> void put(std::vector<uint8_t>& out, const T& v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    out.insert(out.end(), p, p + sizeof v);
}
template <class T> bool take(const uint8_t*& p, const uint8_t* end, T& v) {
    if (size_t(end - p) < sizeof v) return false;
    std::memcpy(&v, p, sizeof v);
    p += sizeof v;
    return true;
}

inline std::vector<uint8_t> encode(const Snapshot& s) {
    std::vector<uint8_t> out;
    const size_t nd = s.dynos.size() < size_t(kMaxDynos) ? s.dynos.size() : size_t(kMaxDynos);
    const size_t ne = s.ecus.size() < size_t(kMaxEcus) ? s.ecus.size() : size_t(kMaxEcus);
    put(out, header(kSnapshot, uint8_t(nd), uint8_t(ne)));
    for (size_t i = 0; i < nd; ++i) {
        const auto& d = s.dynos[i];
        put(out, d.key); put(out, d.position); put(out, d.setup); put(out, d.recorder);
    }
    for (size_t i = 0; i < ne; ++i) {
        const auto& e = s.ecus[i];
        put(out, e.key); put(out, e.position); put(out, e.panel); put(out, e.slots);
    }
    return out;
}
inline std::vector<uint8_t> encode(const Request& r) {
    std::vector<uint8_t> out;
    put(out, header(r.kind));
    put(out, r.key);
    put(out, r.panel);
    return out;
}
// The message's kind, or 0 when it is not ours (or from another build).
inline uint8_t kind_of(const uint8_t* data, size_t size) {
    Header h{};
    const uint8_t* p = data;
    return data && take(p, data + size, h) && header_ok(h) ? h.kind : 0;
}
inline bool decode(const uint8_t* data, size_t size, Snapshot& s) {
    Header h{};
    const uint8_t* p = data;
    const uint8_t* end = data + size;
    if (!data || !take(p, end, h) || !header_ok(h) || h.kind != kSnapshot || h.a > kMaxDynos || h.b > kMaxEcus) return false;
    s = {};
    for (int i = 0; i < h.a; ++i) {
        DynoEntry d;
        if (!take(p, end, d.key) || !take(p, end, d.position) || !take(p, end, d.setup) || !take(p, end, d.recorder)) return false;
        d.setup[kSetupChars - 1] = 0;
        s.dynos.push_back(d);
    }
    for (int i = 0; i < h.b; ++i) {
        EcuEntry e;
        if (!take(p, end, e.key) || !take(p, end, e.position) || !take(p, end, e.panel) || !take(p, end, e.slots)) return false;
        s.ecus.push_back(e);
    }
    return p == end;
}
inline bool decode(const uint8_t* data, size_t size, Request& r) {
    Header h{};
    const uint8_t* p = data;
    const uint8_t* end = data + size;
    if (!data || !take(p, end, h) || !header_ok(h) || (h.kind != kSetTune && h.kind != kClearDyno)) return false;
    r.kind = h.kind;
    return take(p, end, r.key) && take(p, end, r.panel) && p == end;
}
// A tune from a joiner, kept inside the panel's own ranges (ecu::nudge's limits).
inline ecu::Tune clamp_tune(const ecu::Tune& t) {
    ecu::Tune c = t;
    for (int r = 0; r < ecu::kRowCount; ++r) {
        double v = ecu::get(t, r);
        if (!(v == v)) v = 0;   // NaN
        if (v < ecu::kRows[r].min) v = ecu::kRows[r].min;
        if (v > ecu::kRows[r].max) v = ecu::kRows[r].max;
        ecu::set(c, r, v);
    }
    return c;
}
inline double distance(const double a[3], const double b[3]) {
    double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
inline uint64_t parse_steam_id(const char* s) {
    if (!s || !*s) return 0;
    uint64_t v = 0;
    for (size_t i = 0; s[i]; ++i) {
        if (s[i] < '0' || s[i] > '9' || i >= 20) return 0;
        uint64_t d = uint64_t(s[i] - '0');
        if (v > (UINT64_MAX - d) / 10) return 0;
        v = v * 10 + d;
    }
    return v;
}
}  // namespace dyno_net
