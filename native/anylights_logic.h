#pragma once
// AnyLights paint-tool and light-colour logic with no game or Windows dependencies (tested by tests/anylights_logic_test.cpp).
#include <algorithm>
#include <cstdint>
#include <vector>

namespace anylights {

struct Rgb { uint8_t r = 0, g = 0, b = 0; };
inline bool operator==(const Rgb& a, const Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// A freshly placed part's paint slots all hold palette index 79 (gunmetal): such a light keeps its stock colour.
constexpr int32_t kUnpaintedIndex = 79;
constexpr Rgb kStockLight{255, 255, 200};   // what the game's light constructors set
constexpr int kMaxPalette = 256;

// The stock paint tool's modes, in the game's e_mode order.
enum Mode : int32_t { kFill = 0, kBrush = 1, kPencil = 2, kEraseBrush = 3, kErasePencil = 4, kFlood = 5, kModeCount = 6 };
// The order the panel shows them in.
constexpr Mode kModeOrder[kModeCount] = {kFill, kFlood, kBrush, kPencil, kEraseBrush, kErasePencil};

// The paint index a light glows with: its first painted slot, or -1 when every slot is unpainted or out of range.
inline int32_t light_paint_index(const int32_t* slots, int count, int palette_count) {
    for (int i = 0; i < count; ++i)
        if (slots[i] != kUnpaintedIndex && slots[i] >= 0 && slots[i] < palette_count) return slots[i];
    return -1;
}

// Paint colour -> light colour: same hue, full brightness, so dark paints still light up. Greys and black give white.
inline Rgb light_from_paint(Rgb paint) {
    int hi = std::max({int(paint.r), int(paint.g), int(paint.b)}), lo = std::min({int(paint.r), int(paint.g), int(paint.b)});
    if (hi < 24 || hi - lo < 12) return {255, 255, 255};
    auto up = [&](uint8_t v) { return uint8_t(std::min(255, (int(v) * 255 + hi / 2) / hi)); };
    return {up(paint.r), up(paint.g), up(paint.b)};
}

// Undo: the paint of one vehicle's parts and plates before a click. Restoring sends one stock fill event per difference.
struct PaintSlot { int32_t component, slot, colour; };
struct PlateSide { int32_t plate; bool front; int32_t colour; };
struct Snapshot {
    int32_t vehicle = 0;
    std::vector<PaintSlot> slots;
    std::vector<PlateSide> plates;
};
struct Diff { int32_t vehicle = 0; std::vector<PaintSlot> slots; std::vector<PlateSide> plates; bool empty() const { return slots.empty() && plates.empty(); } };

// What has to be repainted to bring `now` back to `before` (same vehicle). Parts or plates removed since are skipped.
inline Diff diff_to_restore(const Snapshot& before, const Snapshot& now) {
    Diff d;
    d.vehicle = before.vehicle;
    if (before.vehicle != now.vehicle) return d;
    for (const auto& b : before.slots)
        for (const auto& n : now.slots)
            if (n.component == b.component && n.slot == b.slot) { if (n.colour != b.colour) d.slots.push_back(b); break; }
    for (const auto& b : before.plates)
        for (const auto& n : now.plates)
            if (n.plate == b.plate && n.front == b.front) { if (n.colour != b.colour) d.plates.push_back(b); break; }
    return d;
}

// Undo history: newest last, bounded.
class UndoStack {
public:
    explicit UndoStack(size_t limit = 40) : limit_(limit) {}
    void push(Snapshot s) {
        if (!items_.empty() && same(items_.back(), s)) return;   // a click that changed nothing yet
        items_.push_back(std::move(s));
        if (items_.size() > limit_) items_.erase(items_.begin());
    }
    // Pops snapshots until one differs from the current paint; `current(vehicle, out)` reads it. Returns the repaint.
    template <class Read> Diff pop_restore(Read&& current) {
        while (!items_.empty()) {
            Snapshot s = std::move(items_.back());
            items_.pop_back();
            Snapshot now;
            if (!current(s.vehicle, now)) continue;
            Diff d = diff_to_restore(s, now);
            if (!d.empty()) return d;
        }
        return {};
    }
    size_t size() const { return items_.size(); }

private:
    static bool same(const Snapshot& a, const Snapshot& b) {
        if (a.vehicle != b.vehicle || a.slots.size() != b.slots.size() || a.plates.size() != b.plates.size()) return false;
        for (size_t i = 0; i < a.slots.size(); ++i)
            if (a.slots[i].component != b.slots[i].component || a.slots[i].slot != b.slots[i].slot || a.slots[i].colour != b.slots[i].colour) return false;
        for (size_t i = 0; i < a.plates.size(); ++i)
            if (a.plates[i].plate != b.plates[i].plate || a.plates[i].front != b.plates[i].front || a.plates[i].colour != b.plates[i].colour) return false;
        return true;
    }
    std::vector<Snapshot> items_;
    size_t limit_;
};

// Palette grid in the panel: `columns` swatches per row. Returns the palette index under (x, y) relative to the grid, or -1.
inline int grid_hit(float x, float y, float cell, float gap, int columns, int count) {
    if (x < 0 || y < 0 || cell <= 0 || columns <= 0) return -1;
    int col = int(x / (cell + gap)), row = int(y / (cell + gap));
    if (col >= columns || x - col * (cell + gap) > cell || y - row * (cell + gap) > cell) return -1;
    int i = row * columns + col;
    return i < count ? i : -1;
}

}  // namespace anylights
