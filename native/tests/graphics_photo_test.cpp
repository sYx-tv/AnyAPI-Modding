// AnyGraphics photo mode logic: free camera movement, input consumption and photo controls, without the game.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include "anyapi_services_v1.h"
#include "anyapi_scene_effects_v1.h"
#include "anygraphics_options.h"
#include "anygraphics_photo.h"
using namespace graphics::photocam;

// The game's camera: forward, up, right as the renderer stores them, looking along +Z from (1, 2, 3).
static void fake_transform(double* ret, uint8_t*) {
    const double m[12] = {0, 0, 1, 0, 1, 0, -1, 0, 0, 1, 2, 3};
    std::copy(m, m + 12, ret);
}
static uint32_t key(uint32_t kind, uint32_t vk) { AnyInputV1 e{}; e.kind = kind; e.key = vk; return input(&e); }
static void step(double* out, double seconds) {
    // Pretend the last move was `seconds` ago, so the integration is deterministic.
    LARGE_INTEGER rate; QueryPerformanceFrequency(&rate); QueryPerformanceCounter(&last_move);
    last_move.QuadPart -= LONGLONG(seconds * double(rate.QuadPart));
    hk_transform(out, reinterpret_cast<uint8_t*>(out));
}

int main() {
    hook.original = (void*)&fake_transform;
    hook_ready = true;
    bool game_head = false;
    head = &game_head;
    double m[12];

    // Photo mode off: nothing is consumed and the camera is the game's.
    update(false, true);
    assert(!key(ANY_KEY_DOWN, 'W') && !key(ANY_KEY_DOWN, '2'));
    key(ANY_KEY_UP, 'W'); key(ANY_KEY_UP, '2');
    hk_transform(m, nullptr);
    assert(m[9] == 1 && m[10] == 2 && m[11] == 3);

    // Photo mode on: the camera starts where the view was.
    facing = 0; facing_time = GetTickCount64();   // facing +Z
    update(true, true);
    assert(active && flying && game_head);
    hk_transform(m, nullptr);
    assert(m[9] == 1 && m[10] == 2 && m[11] == 3);
    assert(forward_axis == 0 && forward_sign == 1);

    // W flies forward at the set speed; the key is consumed so the character does not walk.
    assert(key(ANY_KEY_DOWN, 'W'));
    step(m, .05);
    assert(std::fabs(m[11] - (3 + kDefaultSpeed * .05)) < 1e-6 && m[9] == 1 && m[10] == 2);
    key(ANY_KEY_UP, 'W');
    // D strafes to screen right, the opposite of the transform's third basis vector (+X here), Space rises.
    assert(key(ANY_KEY_DOWN, 'D') && key(ANY_KEY_DOWN, VK_SPACE));
    double z = m[11];
    step(m, .05);
    double d = kDefaultSpeed * .05 / std::sqrt(2.0);
    assert(std::fabs(m[9] - (1 + d)) < 1e-6 && std::fabs(m[10] - (2 + d)) < 1e-6 && std::fabs(m[11] - z) < 1e-6);
    key(ANY_KEY_UP, 'D'); key(ANY_KEY_UP, VK_SPACE);
    // Released keys stop the camera.
    double x = m[9];
    step(m, .05);
    assert(m[9] == x);
    // The camera never leaves kRange of where it started.
    assert(key(ANY_KEY_DOWN, 'W') && key(ANY_KEY_DOWN, VK_SHIFT));
    for (int i = 0; i < 200; ++i) step(m, .1);
    double dx = m[9] - 1, dy = m[10] - 2, dz = m[11] - 3;
    assert(std::fabs(std::sqrt(dx * dx + dy * dy + dz * dz) - kRange) < 1e-6);
    key(ANY_KEY_UP, 'W'); key(ANY_KEY_UP, VK_SHIFT);
    // Mouse buttons are consumed while flying, the wheel changes speed.
    AnyInputV1 e{}; e.kind = ANY_MOUSE_DOWN; e.button = 1; assert(input(&e));
    e = {}; e.kind = ANY_MOUSE_WHEEL; e.wheel = WHEEL_DELTA; assert(input(&e));
    assert(std::fabs(speed - kDefaultSpeed * 1.25) < 1e-4);

    // Photo controls: one step per press, preset first, then each choice.
    unsigned before = revision;
    assert(key(ANY_KEY_DOWN, '2')); assert(key(ANY_KEY_DOWN, '2'));   // a held key steps once
    key(ANY_KEY_UP, '2');
    assert(tweaks.dof == 2 && revision == before + 1);
    key(ANY_KEY_DOWN, '4'); key(ANY_KEY_UP, '4'); key(ANY_KEY_DOWN, '5'); key(ANY_KEY_UP, '5');
    AnySceneEffectsParametersV1 p; p.finish = 0; p.exposure = .5f; p.depth_of_field = 0;
    apply(p);
    assert(p.finish == 1 && p.tonemap == 0 && p.depth_of_field == 1 && p.look == 0 && std::fabs(p.exposure - .75f) < 1e-6);
    key(ANY_KEY_DOWN, '5'); key(ANY_KEY_UP, '5');
    p = {}; apply(p); assert(p.look == 1);
    // R resets everything, including the speed.
    key(ANY_KEY_DOWN, 'R'); key(ANY_KEY_UP, 'R');
    assert(tweaks.dof < 0 && tweaks.look < 0 && tweaks.exposure == 0 && speed == float(kDefaultSpeed));
    p = {}; float exposure = p.exposure, dof = p.depth_of_field; uint32_t finish = p.finish; apply(p);
    assert(p.exposure == exposure && p.depth_of_field == dof && p.finish == finish);

    // V hands the camera back: movement keys reach the game, the photo keys still work.
    key(ANY_KEY_DOWN, 'V'); key(ANY_KEY_UP, 'V');
    update(true, true);
    assert(!flying && !key(ANY_KEY_DOWN, 'W') && key(ANY_KEY_DOWN, '3'));
    key(ANY_KEY_UP, 'W'); key(ANY_KEY_UP, '3');
    hk_transform(m, nullptr);
    assert(m[9] == 1 && m[11] == 3 && !have);
    key(ANY_KEY_DOWN, 'V'); key(ANY_KEY_UP, 'V');

    // A menu releases input; the camera stays where it was flown.
    update(true, true);
    key(ANY_KEY_DOWN, 'W'); step(m, .05); key(ANY_KEY_UP, 'W');
    double flown = m[11];
    update(true, false);
    assert(!active && flying && !key(ANY_KEY_DOWN, 'W') && !key(ANY_KEY_DOWN, '2'));
    key(ANY_KEY_UP, 'W'); key(ANY_KEY_UP, '2');
    hk_transform(m, nullptr);
    assert(m[11] == flown);
    // Leaving photo mode returns the game's camera.
    update(false, true);
    hk_transform(m, nullptr);
    assert(m[11] == 3 && !have && !game_head);
    std::puts("PASS: photo free camera, input consumption, range limit, speed, photo controls, reset, menus");
    return 0;
}
