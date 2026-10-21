#include "../anycombat_logic.h"
#include <cassert>
#include <cmath>
#include <cwchar>
using namespace anycombat;
static bool near(double a, double b, double e = 1e-6) { return std::fabs(a - b) < e; }
int main() {
    // Camera at the origin looking down +Z with +Y up and +X right, 90 degree vertical fov.
    Camera c; c.forward = {0, 0, 1}; c.up = {0, 1, 0}; c.right = {1, 0, 0}; c.tan_half_fov = 1; c.near_plane = 0.1;
    assert(c.valid());
    double x, y;
    assert(project(c, {0, 0, 10}, 1920, 1080, &x, &y) && near(x, 960) && near(y, 540));      // straight ahead: centre
    assert(project(c, {0, 10, 10}, 1920, 1080, &x, &y) && near(x, 960) && near(y, 0));       // 45 degrees up: top edge
    assert(project(c, {10 * 1920.0 / 1080, 0, 10}, 1920, 1080, &x, &y) && near(x, 1920));    // right edge
    assert(project(c, {-1, 0, 10}, 1920, 1080, &x, &y) && x < 960);                          // left of centre
    assert(!project(c, {0, 0, -5}, 1920, 1080, &x, &y));                                     // behind
    assert(!project(c, {0, 0, 0.05}, 1920, 1080, &x, &y));                                   // inside the near plane
    Camera bad = c; bad.tan_half_fov = 0; assert(!bad.valid() && !project(bad, {0, 0, 10}, 1920, 1080, &x, &y));
    // Far from the world origin (the game's coordinates run to 80,000+).
    Camera far = c; far.position = {83954, 5.8, 55847};
    assert(project(far, {83954, 5.8, 55857}, 1920, 1080, &x, &y) && near(x, 960, 1e-3) && near(y, 540, 1e-3));

    // Merging: quick hits on one actor add up; another actor or a later hit gets its own number.
    Numbers n;
    n.add(7, 10, {0, 0, 5}, {}, kGun, 0.0);
    n.add(7, 12, {0, 1, 5}, {}, kGun, 0.2);
    assert(n.count() == 1 && near(n.items[0].damage, 22) && near(n.items[0].position.y, 1));
    n.add(8, 5, {0, 0, 5}, {}, kGun, 0.25);
    assert(n.count() == 2);
    n.add(7, 3, {0, 0, 5}, {}, kGun, 0.2 + kMergeSeconds + 0.01);
    assert(n.count() == 3);
    n.expire(0.25 + kNumberSeconds + 0.01);
    assert(n.count() == 1);
    // Unknown actors (-1) never merge; a full pool replaces the oldest.
    Numbers p;
    for (int i = 0; i < kMaxNumbers + 3; ++i) p.add(-1, 1, {}, {}, kMelee, i * 0.01);
    assert(p.count() == kMaxNumbers);
    double oldest = 1e9; for (auto& it : p.items) oldest = std::min(oldest, it.born);
    assert(near(oldest, 0.03));

    // Formatting.
    wchar_t b[24];
    format_damage(35, b, 24); assert(!std::wcscmp(b, L"35"));
    format_damage(35.4, b, 24); assert(!std::wcscmp(b, L"35"));
    format_damage(2.5, b, 24); assert(!std::wcscmp(b, L"2.5"));
    format_damage(3.02, b, 24); assert(!std::wcscmp(b, L"3"));
    format_damage(0.25, b, 24); assert(!std::wcscmp(b, L"0.25"));
    format_damage(-1, b, 24); assert(!std::wcscmp(b, L"0"));
    // Timing curves stay in range and finish.
    assert(near(number_alpha(0), 1) && near(number_alpha(kNumberSeconds), 0) && number_alpha(kNumberSeconds + 1) == 0);
    assert(near(number_rise(0), 0) && near(number_rise(kNumberSeconds), 1));
    assert(near(marker_alpha(0), 1) && marker_alpha(kMarkerSeconds + 0.01) == 0);
    assert(damage_color(false) == kHitColor && damage_color(true) == kKillColor);

    // Kills: the newest number for the actor turns red; only targets that go down soon after your hit count.
    Numbers k;
    k.add(3, 20, {}, {}, kGun, 0.0);
    k.add(3, 5, {}, {}, kGun, 1.0);
    k.mark_kill(3, 1.2);
    assert(!k.items[0].kill && k.items[1].kill && near(k.items[1].born, 1.2));
    KillWatch w;
    w.hit(3, 0.0);
    assert(!w.check(3, false, 0.5) && w.count() == 1);
    assert(w.check(3, true, 0.6) && w.count() == 0);       // went down inside the window: kill, reported once
    assert(!w.check(3, true, 0.7));
    w.hit(4, 0.0);
    assert(!w.check(4, true, kKillWindow + 0.1));           // went down too late (someone else, bleeding out)
    w.hit(5, 0.0); w.hit(5, 1.0);
    assert(w.count() == 1 && w.check(5, true, 1.0 + kKillWindow - 0.1));   // a repeat hit restarts the window
    w.hit(6, 0.0); w.expire(kKillWindow + 0.1); assert(w.count() == 0);
    for (int i = 0; i < kMaxWatched + 4; ++i) w.hit(100 + i, i * 0.01);
    assert(w.count() == kMaxWatched);
    w.hit(-1, 0); assert(w.count() == kMaxWatched);
    return 0;
}
