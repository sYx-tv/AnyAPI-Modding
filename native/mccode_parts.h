#pragma once
// MCCode parts guide: which parts a microcontroller can read or set, built at run time from the game's own part
// definitions (AnyAPI's item catalog hands over each part's JSON; its "data_descriptors" list the channels the
// game offers a microcontroller). Nothing here ships game data: the list is read from the player's install.
#include "anyapi_asset_json.h"
#include <algorithm>
#include <string>
#include <vector>

namespace mccode::parts {

struct Channel { std::string name, type, doc; };
struct Part { std::string id, name, category; std::vector<Channel> channels; };

// The type a channel carries, in the words the Code view uses.
inline std::string type_label(const std::string& raw) {
    std::string t;
    for (char c : raw) t += char(c >= 'A' && c <= 'Z' ? c + 32 : c);
    if (t.find("bool") != std::string::npos) return "bool";
    if (t.find("analog") != std::string::npos) return "analogue";
    if (t.find("string") != std::string::npos || t == "text") return "string";
    if (t.find("int") != std::string::npos || t.find("s32") != std::string::npos) return "integer";
    if (t.find("f64") != std::string::npos || t.find("float") != std::string::npos || t.find("fraction") != std::string::npos ||
        t.find("double") != std::string::npos || t.find("number") != std::string::npos)
        return "fractional";
    return raw;
}

// What a value of that type means to the player.
inline const char* type_hint(const std::string& label) {
    if (label == "bool") return "on/off: true or false";
    if (label == "integer") return "whole number";
    if (label == "fractional") return "number with decimals";
    if (label == "analogue") return "number from the part, like a gauge";
    if (label == "string") return "text";
    return "";
}

// The name a part gets in code: the game names a new part after itself, spaces as underscores ("Gas Pressure
// Sensor" is Gas_Pressure_Sensor until the player renames it in its properties).
inline std::string code_name(const std::string& display) {
    std::string out;
    for (char c : display) {
        bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (word) out += c;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out = "Part" + out;
    return out;
}

// One part's channels from its definition JSON; false when the JSON can't be read.
inline bool parse_part(const std::string& json, Part& part) {
    try {
        auto root = assetjson::Parser(json).parse();
        auto& list = root.get("data_descriptors");
        if (list.kind != 'a') return true;
        for (auto& d : list.array) {
            if (d.kind != 'o') continue;
            auto& name = d.get("name");
            if (name.kind != 's' || name.text.empty()) continue;
            auto& type = d.get("type");
            auto& doc = d.get("documentation");
            part.channels.push_back({name.text, type_label(type.kind == 's' ? type.text : std::string()), doc.kind == 's' ? doc.text : std::string()});
        }
        return true;
    } catch (...) {
        return false;
    }
}

// Parts with at least one channel, by category then name.
inline void sort_parts(std::vector<Part>& parts) {
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Part& p) { return p.channels.empty(); }), parts.end());
    std::stable_sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) {
        return a.category != b.category ? a.category < b.category : a.name < b.name;
    });
}

// Splits text into lines of at most `width` characters, at spaces.
inline std::vector<std::string> wrap(const std::string& text, size_t width) {
    std::vector<std::string> out;
    std::string line;
    size_t i = 0;
    while (i < text.size()) {
        size_t sp = text.find(' ', i);
        std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        i = sp == std::string::npos ? text.size() : sp + 1;
        if (!line.empty() && line.size() + 1 + word.size() > width) { out.push_back(line); line.clear(); }
        line += (line.empty() ? "" : " ") + word;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

// The setup guide at the top of the Parts tab.
struct GuideLine { const char* text; const char* code; };
inline const std::vector<GuideLine>& setup_guide() {
    static const std::vector<GuideLine> g = {
        {"1. Wire each part to the microcontroller with data cables. A part only shows in This MC once it is wired.", nullptr},
        {"2. Name the part in its properties. Code uses that name, then a dot, then the channel.", nullptr},
        {"3. Read a channel to see what the part reports:", "if Gas_Pressure_Sensor.pressure > 100000 {\n\t$\n}"},
        {"4. Set a channel to make the part act:", "Actuator_Stepper_Motor.input = $1.0"},
        {"5. Put the code inside on_tick { } so it runs every game tick, then Apply (Ctrl+Enter).", "on_tick {\n\t$\n}"},
        {"Parts missing from the list below (gas and liquid valves, for one) have no channels, so code can't reach them "
         "directly. Wire their input to a part that has a channel and an output, like the stepper motor, and set that.", nullptr},
        {"Pressure sensors report pascals: 100 kPa is 100000.", nullptr},
    };
    return g;
}

}  // namespace mccode::parts
