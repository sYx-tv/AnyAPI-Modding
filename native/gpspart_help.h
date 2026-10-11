#pragma once
// GpsPart help panel layout: a "GPS outputs" button in the top-right corner of the microcontroller editor that
// opens a list of every GPS Sensor channel and what it means. Pure geometry so tests can check the hit areas.
#include <algorithm>
#include "gpspart_logic.h"

namespace gpspart {

struct Box {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

constexpr int kHelpRows = kChannelCount + 1;    // north_angle plus every GPS channel

struct HelpLayout {
    float s = 1, row_h = 22, name_w = 230;
    Box button, panel, close;
    float rows_y = 0;
};

inline HelpLayout help_layout(float width, float height) {
    HelpLayout l;
    l.s = std::clamp(height / 1080.f, .65f, 1.75f);
    float s = l.s, margin = 20 * s;
    l.button = {width - 170 * s - margin, margin, 170 * s, 32 * s};
    float panel_w = std::min(660 * s, width - 2 * margin);
    float top = l.button.y + l.button.h + 8 * s, header = 46 * s, footer = 34 * s;
    l.row_h = std::clamp((height - top - margin - header - footer) / kHelpRows, 12 * s, 22 * s);
    l.name_w = 236 * s;
    l.panel = {width - panel_w - margin, top, panel_w, header + l.row_h * kHelpRows + footer};
    l.close = {l.panel.x + l.panel.w - 34 * s, l.panel.y + 8 * s, 26 * s, 26 * s};
    l.rows_y = l.panel.y + header;
    return l;
}

}  // namespace gpspart
