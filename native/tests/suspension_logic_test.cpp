// AnySuspension logic tests: definitions JSON, spring tuning, rest positions and the slider file.
#include "../suspension_logic.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
using namespace suspension;

namespace {
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
size_t count(const std::string& s, const std::string& what) {
    size_t n = 0;
    for (size_t i = s.find(what); i != std::string::npos; i = s.find(what, i + 1)) ++n;
    return n;
}
}  // namespace

int main() {
    // Four parts, paired with each other, bearing limits wide open.
    std::string json = definitions_json();
    assert(count(json, "\"id\": ") == 4);
    assert(json.find("\"hinge_definition_id_other\": \"bearing_ring\"") != std::string::npos);
    assert(json.find("\"hinge_definition_id_other\": \"bearing_pin\"") != std::string::npos);
    assert(json.find("\"rail_definition_id_other\": \"suspension_slider\"") != std::string::npos);
    assert(json.find("\"rail_slider_definition_id_other\": \"suspension_rail\"") != std::string::npos);
    assert(count(json, "\"hinge_constraint_limit_min\": -3.141592653589793") == 2);
    assert(count(json, "\"hinge_constraint_limit_max\": 3.141592653589793") == 2);
    int depth = 0;
    for (char c : json) { depth += c == '{' || c == '['; depth -= c == '}' || c == ']'; assert(depth >= 0); }
    assert(depth == 0);
    assert(is_suspension_slider_id("suspension_slider", 17) && !is_suspension_slider_id("suspension_rail", 15));
    assert(!is_suspension_slider_id("rail_slider", 11));

    // A 300 kg share at 2 Hz: k = 300 (4 pi)^2 = 47.4 kN/m; sag 6.2 cm whatever the mass.
    Spring s = spring_for(300, 2.0, 0.5);
    assert(near(s.stiffness, 300 * std::pow(4 * kPi, 2), 1e-6));
    assert(near(s.damping, 2 * 0.5 * std::sqrt(s.stiffness * 300), 1e-6));
    assert(near(300 * kGravity / s.stiffness, static_sag(2.0), 1e-9));
    assert(near(static_sag(2.0), 0.0621, 0.0005));
    Spring heavy = spring_for(3000, 2.0, 0.5);
    assert(near(3000 * kGravity / heavy.stiffness, static_sag(2.0), 1e-9));
    // Out of range settings are clamped, never zero or NaN.
    Spring odd = spring_for(NAN, NAN, -1);
    assert(odd.stiffness > 0 && std::isfinite(odd.damping) && odd.damping > 0);

    // The heavier side is sprung, shared between its springs.
    assert(near(sprung_share(1200, 40, 4), 300, 1e-9));
    assert(near(sprung_share(40, 1200, 0), 1200, 1e-9));

    // Rest along the rail survives a rebuild that moves the constraint's zero.
    double rest = rest_from_position(0.0, -0.3);        // placed 0.3 m from the rail's lower end
    assert(near(rest, 0.3, 1e-12));
    assert(near(target_from_rest(rest, -0.24, 0.66), 0.06, 1e-12));   // reloaded 6 cm further along: target 6 cm back
    assert(near(target_from_rest(-1.0, -0.1, 0.2), -0.1, 1e-12));     // clamped to the rail's ends
    assert(near(target_from_rest(5.0, -0.1, 0.2), 0.2, 1e-12));

    // Properties Tool factors: 0-1 like the stock wheel's rows.
    assert(near(frequency_from_factor(0), 1.0, 1e-12) && near(frequency_from_factor(1), 4.0, 1e-12) && near(frequency_from_factor(0.5), 2.5, 1e-12));
    assert(near(frequency_from_factor(7), 4.0, 1e-12) && near(frequency_from_factor(NAN), 1.0, 1e-12));
    assert(near(damping_from_factor(0), 0.1, 1e-12) && near(damping_from_factor(1), 1.0, 1e-12));

    // Slider file round trip; junk is skipped.
    SaveMap m;
    m[RestKey{12, 34}] = SliderSave{0.15, 0.7, 0.3};
    m[RestKey{-1, 7}] = SliderSave{1.25, 0.5, 0.5};
    SaveMap back = decode_sliders(encode_sliders(m));
    assert(back.size() == 2);
    assert(near(back[(RestKey{12, 34})].rest, 0.15, 1e-6) && near(back[(RestKey{12, 34})].stiffness, 0.7, 1e-6) && near(back[(RestKey{12, 34})].damping, 0.3, 1e-6));
    assert(near(back[(RestKey{-1, 7})].rest, 1.25, 1e-6));
    SaveMap junk = decode_sliders("{\"version\": 2, \"sliders\": {\"x:1\": [2, 0, 0], \"3:4\": \"no\", \"8:9\": [1, 2], \"5:6\": [0.5, 3, -1]}}");
    assert(junk.size() == 1 && near(junk[(RestKey{5, 6})].rest, 0.5, 1e-9) && near(junk[(RestKey{5, 6})].stiffness, 1, 1e-9) && near(junk[(RestKey{5, 6})].damping, 0, 1e-9));
    std::printf("suspension logic: ok\n");
    return 0;
}
