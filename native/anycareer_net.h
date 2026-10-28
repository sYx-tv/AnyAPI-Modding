#pragma once
// AnyCareer co-op: the messages between a joiner's AnyCareer and the host's, over the game's own Steam sessions
// (steam_messages.h). Joiners ask (poll, buy, sell, take, ...); the host does everything in its server tick, as for
// itself, and answers each joiner with his own screen (what he can buy, sell, his job board). Nothing here trusts a
// joiner: every request is checked on the host exactly like the host's own key presses.
// Platform-free (tests build it on Linux): text is UTF-8, integers little-endian, every read bounded.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace anycareer::net {
constexpr uint32_t kMagic = 0x52414341;        // "ACAR"
constexpr uint16_t kProtocol = 1;
constexpr int32_t kChannel = 0x414E5943;       // far from the game's small channel numbers (AnyAPI's hello uses ...00)
constexpr uint32_t kMaxMessage = 48 * 1024;
constexpr uint32_t kMaxRows = 160, kMaxText = 200;
constexpr int kShops = 9;                      // logic::kShopCount (checked in the mod)

enum Kind : uint8_t { kRequest = 1, kView = 2 };
// What a joiner asks for. poll: "send me my screen" (twice a second while in a career world).
enum Action : uint8_t { kPoll = 0, kBuy, kSell, kTake, kAbandon, kDeliver, kClaim, kActionCount };

struct Request {
    uint8_t action = kPoll;
    int8_t shop = -1;          // the trader whose screen is open (buy, sell, take)
    bool open = false;         // the joiner's shop screen is open (the host then reads his inventory for the Sell tab)
    int32_t key = 0;           // the row's key, as on the host
    float yaw = 0;             // where to drop things (in front of him)
};

struct Row { std::string name, id, button; int32_t price = 0, quantity = 0, key = 0; bool off = false; };
struct Spot { bool placed = false; double x = 0, y = 0, z = 0; };
struct View {
    bool placed = false;
    Spot shops[kShops];
    int8_t shop = -1;
    int64_t cash = 0, days = 0, restock_in = 0, board = -1;
    int32_t job_state = 0, job_have = 0, job_count = 0, job_shop = 0, next_tier = 0;
    bool job_kill = false;
    Spot job_at;
    std::string job_text, status;
    std::vector<Row> buy, sell, jobs;
};

// ------------------------------------------------------------------ writing
struct Writer {
    std::vector<uint8_t> out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
    void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
    void u64(uint64_t v) { u32(uint32_t(v)); u32(uint32_t(v >> 32)); }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void i64(int64_t v) { u64(uint64_t(v)); }
    void f64(double v) { uint64_t b; std::memcpy(&b, &v, 8); u64(b); }
    void f32(float v) { uint32_t b; std::memcpy(&b, &v, 4); u32(b); }
    void text(const std::string& s) { size_t n = s.size() > kMaxText ? kMaxText : s.size(); u8(uint8_t(n)); out.insert(out.end(), s.begin(), s.begin() + std::ptrdiff_t(n)); }
    void header(Kind k) { u32(kMagic); u16(kProtocol); u8(k); }
};

inline std::vector<uint8_t> encode(const Request& r) {
    Writer w;
    w.header(kRequest);
    w.u8(r.action); w.u8(uint8_t(r.shop)); w.u8(r.open ? 1 : 0); w.i32(r.key); w.f32(r.yaw);
    return w.out;
}

inline void put_spot(Writer& w, const Spot& s) { w.u8(s.placed ? 1 : 0); w.f64(s.x); w.f64(s.y); w.f64(s.z); }
inline void put_rows(Writer& w, const std::vector<Row>& rows) {
    size_t n = rows.size() > kMaxRows ? kMaxRows : rows.size();
    w.u16(uint16_t(n));
    for (size_t i = 0; i < n; ++i) {
        const Row& r = rows[i];
        w.text(r.name); w.text(r.id); w.text(r.button); w.i32(r.price); w.i32(r.quantity); w.i32(r.key); w.u8(r.off ? 1 : 0);
    }
}
inline std::vector<uint8_t> encode(const View& v) {
    Writer w;
    w.header(kView);
    w.u8(v.placed ? 1 : 0);
    for (const auto& s : v.shops) put_spot(w, s);
    w.u8(uint8_t(v.shop)); w.i64(v.cash); w.i64(v.days); w.i64(v.restock_in); w.i64(v.board);
    w.i32(v.job_state); w.i32(v.job_have); w.i32(v.job_count); w.i32(v.job_shop); w.i32(v.next_tier); w.u8(v.job_kill ? 1 : 0);
    put_spot(w, v.job_at);
    w.text(v.job_text); w.text(v.status);
    put_rows(w, v.buy); put_rows(w, v.sell); put_rows(w, v.jobs);
    return w.out;
}

// ------------------------------------------------------------------ reading
struct Reader {
    const uint8_t* p; size_t n, at = 0; bool ok = true;
    Reader(const uint8_t* data, size_t size) : p(data), n(size) {}
    bool need(size_t k) { if (!ok || n - at < k) ok = false; return ok; }
    uint8_t u8() { if (!need(1)) return 0; return p[at++]; }
    uint16_t u16() { uint16_t lo = u8(); return uint16_t(lo | uint16_t(u8()) << 8); }
    uint32_t u32() { uint32_t lo = u16(); return lo | uint32_t(u16()) << 16; }
    uint64_t u64() { uint64_t lo = u32(); return lo | uint64_t(u32()) << 32; }
    int32_t i32() { return int32_t(u32()); }
    int64_t i64() { return int64_t(u64()); }
    double f64() { uint64_t b = u64(); double v; std::memcpy(&v, &b, 8); return v; }
    float f32() { uint32_t b = u32(); float v; std::memcpy(&v, &b, 4); return v; }
    std::string text() {
        uint8_t len = u8();
        if (len > kMaxText || !need(len)) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p + at), len);
        at += len;
        return s;
    }
    bool header(Kind k) { return u32() == kMagic && u16() == kProtocol && u8() == k && ok; }
};

// The kind of a message (0: not ours).
inline uint8_t kind_of(const uint8_t* p, size_t n) {
    if (!p || n < 7 || n > kMaxMessage) return 0;
    Reader r(p, n);
    if (r.u32() != kMagic || r.u16() != kProtocol) return 0;
    uint8_t k = r.u8();
    return k == kRequest || k == kView ? k : 0;
}

inline bool decode(const uint8_t* p, size_t n, Request& out) {
    out = {};
    if (!p || n > kMaxMessage) return false;
    Reader r(p, n);
    if (!r.header(kRequest)) return false;
    out.action = r.u8(); out.shop = int8_t(r.u8()); out.open = r.u8() != 0; out.key = r.i32(); out.yaw = r.f32();
    return r.ok && r.at == n && out.action < kActionCount && out.shop >= -1 && out.shop < kShops && out.yaw == out.yaw;
}

inline bool get_spot(Reader& r, Spot& s) { s.placed = r.u8() != 0; s.x = r.f64(); s.y = r.f64(); s.z = r.f64(); return r.ok && s.x == s.x && s.z == s.z; }
inline bool get_rows(Reader& r, std::vector<Row>& rows) {
    uint16_t n = r.u16();
    if (!r.ok || n > kMaxRows) return false;
    rows.resize(n);
    for (auto& row : rows) {
        row.name = r.text(); row.id = r.text(); row.button = r.text();
        row.price = r.i32(); row.quantity = r.i32(); row.key = r.i32(); row.off = r.u8() != 0;
    }
    return r.ok;
}
inline bool decode(const uint8_t* p, size_t n, View& v) {
    v = {};
    if (!p || n > kMaxMessage) return false;
    Reader r(p, n);
    if (!r.header(kView)) return false;
    v.placed = r.u8() != 0;
    for (auto& s : v.shops) if (!get_spot(r, s)) return false;
    v.shop = int8_t(r.u8()); v.cash = r.i64(); v.days = r.i64(); v.restock_in = r.i64(); v.board = r.i64();
    v.job_state = r.i32(); v.job_have = r.i32(); v.job_count = r.i32(); v.job_shop = r.i32(); v.next_tier = r.i32(); v.job_kill = r.u8() != 0;
    if (!get_spot(r, v.job_at)) return false;
    v.job_text = r.text(); v.status = r.text();
    if (!get_rows(r, v.buy) || !get_rows(r, v.sell) || !get_rows(r, v.jobs)) return false;
    return r.ok && r.at == n && v.shop >= -1 && v.shop < kShops && v.job_shop >= 0 && v.job_shop < kShops && v.job_state >= 0 && v.job_state <= 2;
}

// Per-sender flood guard on the host: at most kRate messages a second are handled (a joiner polls twice a second).
constexpr uint32_t kRate = 12;
struct Rate { uint64_t window = 0; uint32_t count = 0; };
inline bool admit(Rate& r, uint64_t now_ms) {
    if (now_ms - r.window >= 1000) { r.window = now_ms; r.count = 0; }
    return ++r.count <= kRate;
}

// Strict decimal SteamID64 ("7656...") to integer; 0 means invalid.
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
}  // namespace anycareer::net
