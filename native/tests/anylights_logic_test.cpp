#include "../anylights_logic.h"
#include <cassert>
using namespace anylights;

int main() {
    // Lights: first painted slot wins, unpainted (79) and out-of-range slots are skipped.
    int32_t fresh[3] = {79, 79, 79}, painted[3] = {79, 12, 40}, bad[2] = {-1, 200};
    assert(light_paint_index(fresh, 3, 84) == -1);
    assert(light_paint_index(painted, 3, 84) == 12);
    assert(light_paint_index(bad, 2, 84) == -1);
    // Paint -> light: full brightness, same hue; greys and black glow white.
    assert((light_from_paint({128, 0, 0}) == Rgb{255, 0, 0}));
    assert((light_from_paint({100, 50, 0}) == Rgb{255, 128, 0}));
    assert((light_from_paint({0, 0, 0}) == Rgb{255, 255, 255}));
    assert((light_from_paint({120, 124, 118}) == Rgb{255, 255, 255}));

    // Undo: restores only what changed, skips removed parts, and skips clicks that changed nothing.
    Snapshot before{7, {{1, 0, 79}, {1, 1, 79}, {2, 0, 5}}, {{3, true, 10}}};
    Snapshot after{7, {{1, 0, 30}, {1, 1, 79}}, {{3, true, 11}}};
    Diff d = diff_to_restore(before, after);
    assert(d.slots.size() == 1 && d.slots[0].component == 1 && d.slots[0].slot == 0 && d.slots[0].colour == 79);
    assert(d.plates.size() == 1 && d.plates[0].colour == 10);
    assert(diff_to_restore(before, Snapshot{8}).empty());

    UndoStack stack(3);
    stack.push(before);
    stack.push(before);                 // same paint again: not stored twice
    assert(stack.size() == 1);
    Snapshot unchanged = after;
    stack.push(unchanged);              // newest snapshot equals the current paint: undo skips it
    auto read = [&](int32_t vehicle, Snapshot& out) { if (vehicle != 7) return false; out = after; return true; };
    Diff r = stack.pop_restore(read);
    assert(r.slots.size() == 1 && r.plates.size() == 1 && stack.size() == 0);
    assert(stack.pop_restore(read).empty());
    for (int i = 0; i < 5; ++i) stack.push(Snapshot{i});
    assert(stack.size() == 3);

    // Palette grid hit tests.
    assert(grid_hit(0, 0, 20, 4, 12, 84) == 0);
    assert(grid_hit(25, 0, 20, 4, 12, 84) == 1);
    assert(grid_hit(21, 0, 20, 4, 12, 84) == -1);   // in the gap
    assert(grid_hit(0, 24 * 6, 20, 4, 12, 84) == 72);
    assert(grid_hit(0, 24 * 7, 20, 4, 12, 84) == -1);
    assert(grid_hit(-1, 0, 20, 4, 12, 84) == -1);
    return 0;
}
