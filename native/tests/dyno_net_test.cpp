// Dyno and ECU co-op wire format (dyno_net.h).
#include "../dyno_net.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace dyno_net;

int main() {
    Snapshot s;
    DynoEntry d;
    d.key = 0x1234567890ull;
    d.position[0] = 1; d.position[1] = 2; d.position[2] = 3;
    std::snprintf(d.setup, sizeof d.setup, "%s", "Twin turbo");
    d.recorder.count = 2; d.recorder.runs[1].sum[10] = 5000; d.recorder.runs[1].count[10] = 2; d.recorder.live.rps = 50;
    s.dynos.push_back(d);
    EcuEntry e;
    e.key = 0xabcdefull;
    e.panel.rev_limit = 7000; e.panel.launch = true; e.panel.boost_psi = 18;
    e.slots[ecu::kOutRpm] = 4321;
    s.ecus.push_back(e);
    auto bytes = encode(s);
    assert(kind_of(bytes.data(), bytes.size()) == kSnapshot);
    Snapshot back;
    assert(decode(bytes.data(), bytes.size(), back));
    assert(back.dynos.size() == 1 && back.ecus.size() == 1);
    assert(back.dynos[0].key == d.key && back.dynos[0].position[2] == 3 && std::string(back.dynos[0].setup) == "Twin turbo");
    assert(back.dynos[0].recorder.count == 2 && back.dynos[0].recorder.runs[1].watts(10) == 2500 && back.dynos[0].recorder.live.rps == 50);
    assert(back.ecus[0].panel.rev_limit == 7000 && back.ecus[0].panel.launch && back.ecus[0].slots[ecu::kOutRpm] == 4321);
    // Truncated, padded, or another build's messages are refused.
    assert(!decode(bytes.data(), bytes.size() - 1, back));
    bytes.push_back(0);
    assert(!decode(bytes.data(), bytes.size(), back));
    bytes.pop_back();
    bytes[16] ^= 1;   // recorder size in the header
    assert(!decode(bytes.data(), bytes.size(), back) && kind_of(bytes.data(), bytes.size()) == 0);
    // Too many entries are capped on encode.
    Snapshot many;
    for (int i = 0; i < 9; ++i) { many.dynos.push_back(d); many.ecus.push_back(e); }
    auto big = encode(many);
    assert(decode(big.data(), big.size(), back) && int(back.dynos.size()) == kMaxDynos && int(back.ecus.size()) == kMaxEcus);

    // Requests round trip; a snapshot is not a request and the other way round.
    Request r;
    r.kind = kSetTune; r.key = 77; r.panel.timing = 4;
    auto rb = encode(r);
    Request rr;
    assert(decode(rb.data(), rb.size(), rr) && rr.kind == kSetTune && rr.key == 77 && rr.panel.timing == 4);
    assert(!decode(rb.data(), rb.size(), back));
    auto sb = encode(s);
    assert(!decode(sb.data(), sb.size(), rr));
    // A joiner's tune is held to the panel's ranges.
    ecu::Tune wild;
    wild.rev_limit = 1e9; wild.timing = -1e9; wild.fuel_trim = std::nan("");
    ecu::Tune c = clamp_tune(wild);
    for (int row = 0; row < ecu::kRowCount; ++row) {
        double v = ecu::get(c, row);
        assert(v >= ecu::kRows[row].min && v <= ecu::kRows[row].max);
    }
    assert(parse_steam_id("76561198000000000") == 76561198000000000ull && parse_steam_id("x1") == 0 && parse_steam_id("") == 0);
    return 0;
}
