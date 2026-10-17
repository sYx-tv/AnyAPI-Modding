// MCCode autocomplete and auto-pair tests.
#undef NDEBUG
#include "../mccode_complete.h"
#include "../mccode_editor.h"
#include <cassert>
#include <iostream>

using namespace mccode;
using namespace mccode::complete;

static bool has(const Result& r, const std::string& label) {
    for (auto& i : r.items) if (i.label == label) return true;
    return false;
}

int main() {
    Symbols sy;
    sy.vars["speed"] = "fractional";
    sy.vars["samples"] = "fractional";
    sy.lists.insert("samples");
    sy.ins["engine_rpm"] = "fractional";
    sy.functions.insert("arm");
    sy.function_params["arm"] = {Param{"integer", "limit"}};
    std::vector<PartPort> ports = {{"RearLeft", "rps", true}, {"RearLeft", "brake_factor", false}, {"Actuator_Stepper_Motor", "input", false}};

    // names while typing, prefix matches first
    auto r = suggest("\tx = sp", 7, sy, ports);
    assert(r.start == 5 && r.prefix == "sp" && !r.items.empty() && r.items[0].label == "speed");
    assert(has(suggest("\tx = sq", 7, sy, ports), "sqrt"));
    assert(suggest("\tx = sq", 7, sy, ports).items[0].insert == "sqrt($)");
    // statement start offers blocks, not operators
    r = suggest("\ti", 2, sy, ports);
    assert(has(r, "if") && !has(r, "and"));
    assert(has(suggest("\tx = a", 6, sy, ports), "and"));
    // wired parts, then their channels after the dot
    r = suggest("\tx = Rea", 8, sy, ports);
    assert(has(r, "RearLeft") && r.items[0].insert == "RearLeft.$");
    r = suggest("\tx = RearLeft.", 14, sy, ports);
    assert(r.after_dot && r.prefix.empty() && has(r, "rps") && has(r, "brake_factor") && !has(r, "input"));
    r = suggest("\tRearLeft.br", 12, sy, ports);
    assert(r.items.size() == 1 && r.items[0].label == "brake_factor");
    // list methods
    assert(has(suggest("\tsamples.", 9, sy, ports), "append"));
    // define keywords and types
    assert(has(suggest("define v", 8, sy, ports), "var"));
    assert(has(suggest("define var fr", 13, sy, ports), "fractional"));
    // functions with parameters leave the cursor in the brackets
    r = suggest("\tar", 3, sy, ports);
    assert(has(r, "arm") && r.items[0].insert == "arm($)");
    // nothing inside comments, strings or numbers, or when the word is already complete
    assert(suggest("\t# sp", 5, sy, ports).items.empty());
    assert(suggest("\tx = \"sp", 8, sy, ports).items.empty());
    assert(suggest("\tx = 12", 7, sy, ports).items.empty());
    assert(suggest("\tx = speed", 10, sy, ports).items.empty());
    // case-insensitive
    assert(has(suggest("\tx = rearl", 10, sy, ports), "RearLeft"));

    // auto pairs
    Editor e;
    e.auto_pairs = true;
    for (char c : std::string("x = abs(")) e.type_char(c);
    assert(e.text() == "x = abs()" && e.cursor() == (Pos{0, 8}));
    e.type_char('1');
    e.type_char(')');   // steps over the closer
    assert(e.text() == "x = abs(1)" && e.cursor() == (Pos{0, 10}));
    e.set_text("on_tick ");
    e.set_cursor({0, 8}, false);
    e.type_char('{');
    assert(e.text() == "on_tick {}");
    e.newline();
    assert(e.text() == "on_tick {\n\t\n}" && e.cursor() == (Pos{1, 1}));
    e.set_text("x = ");
    e.set_cursor({0, 4}, false);
    e.type_char('"');
    assert(e.text() == "x = \"\"");
    e.backspace();   // removes the empty pair
    assert(e.text() == "x = ");
    e.set_text("abc");
    e.set_cursor({0, 0}, false);
    e.type_char('(');   // no closer before a word
    assert(e.text() == "(abc");
    // off by default
    Editor plain;
    plain.type_char('(');
    assert(plain.text() == "(");

    std::cout << "mccode_complete: all tests passed\n";
}
