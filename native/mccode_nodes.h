// MCCode node graphs: Stormworks-style logic nodes and wires that compile to MCCode code.
//
// A graph is a list of nodes (a kind, a position and named parameters) and wires from an output port to an input
// port. generate() turns it into ordinary MCCode text: one variable per node output, updated every tick in wire
// order (a wire that loops back reads the previous tick's value). That text goes through mccode::compile and the
// game's own Load like typed code, so players without the mod run it as normal blocks.
//
// The graph itself rides along inside the program as "#@" comment lines at the top, so a program made with nodes
// reopens as nodes. parse() regenerates the code from the graph and compares, to tell when someone edited it by hand.
#pragma once
#include "mccode_lang.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mccode::nodes {

// ------------------------------------------------------------------------------------------------ the library
struct PortDef { const char* name; bool is_bool; };
enum class PK { Number, Name, Choice };
struct ParamDef { const char* key; const char* label; const char* def; PK kind; const char* choices = ""; };   // choices: "a|b"
struct NodeDef {
    const char* kind;
    const char* title;
    const char* group;
    std::vector<PortDef> ins, outs;
    std::vector<ParamDef> params;
    const char* help;
};

inline const std::vector<NodeDef>& library() {
    static const std::vector<NodeDef> defs = {
        // parts and constants
        {"part_in", "Part input", "Parts", {}, {{"value", false}}, {{"part", "Part", "RearLeft", PK::Name}, {"channel", "Channel", "rps", PK::Name}, {"type", "Type", "number", PK::Choice, "number|whole number"}},
         "Reads a number channel of a part wired to the microcontroller, like RearLeft.rps"},
        {"part_in_bool", "Part input (on/off)", "Parts", {}, {{"on", true}}, {{"part", "Part", "Switch", PK::Name}, {"channel", "Channel", "on", PK::Name}},
         "Reads an on/off channel of a part"},
        {"part_out", "Part output", "Parts", {{"value", false}}, {}, {{"part", "Part", "Actuator", PK::Name}, {"channel", "Channel", "input", PK::Name}, {"type", "Type", "number", PK::Choice, "number|whole number"}},
         "Sets a number channel of a part every tick"},
        {"part_out_bool", "Part output (on/off)", "Parts", {{"on", true}}, {}, {{"part", "Part", "Light", PK::Name}, {"channel", "Channel", "on", PK::Name}},
         "Sets an on/off channel of a part every tick"},
        {"const", "Number", "Parts", {}, {{"value", false}}, {{"value", "Value", "1.0", PK::Number}}, "A fixed number"},
        {"const_bool", "On/off", "Parts", {}, {{"on", true}}, {{"value", "Value", "on", PK::Choice, "on|off"}}, "A fixed on or off"},
        // maths
        {"add", "Add", "Maths", {{"a", false}, {"b", false}}, {{"a+b", false}}, {}, "a + b"},
        {"sub", "Subtract", "Maths", {{"a", false}, {"b", false}}, {{"a-b", false}}, {}, "a - b"},
        {"mul", "Multiply", "Maths", {{"a", false}, {"b", false}}, {{"a*b", false}}, {}, "a × b"},
        {"div", "Divide", "Maths", {{"a", false}, {"b", false}}, {{"a/b", false}}, {}, "a ÷ b (0 when b is 0)"},
        {"mod", "Remainder", "Maths", {{"a", false}, {"b", false}}, {{"a%b", false}}, {}, "What's left of a ÷ b (0 when b is 0)"},
        {"pow", "Power", "Maths", {{"a", false}, {"b", false}}, {{"a^b", false}}, {}, "a to the power b"},
        {"min", "Min", "Maths", {{"a", false}, {"b", false}}, {{"min", false}}, {}, "The smaller of a and b"},
        {"max", "Max", "Maths", {{"a", false}, {"b", false}}, {{"max", false}}, {}, "The bigger of a and b"},
        {"clamp", "Clamp", "Maths", {{"x", false}}, {{"x", false}}, {{"min", "Min", "0.0", PK::Number}, {"max", "Max", "1.0", PK::Number}}, "Keeps x between min and max"},
        {"abs", "Absolute", "Maths", {{"x", false}}, {{"|x|", false}}, {}, "x without its minus sign"},
        {"neg", "Negate", "Maths", {{"x", false}}, {{"-x", false}}, {}, "Flips the sign of x"},
        {"sqrt", "Square root", "Maths", {{"x", false}}, {{"√x", false}}, {}, "Square root of x"},
        {"sin", "Sine", "Maths", {{"x", false}}, {{"sin", false}}, {}, "Sine of x"},
        {"cos", "Cosine", "Maths", {{"x", false}}, {{"cos", false}}, {}, "Cosine of x"},
        // compare
        {"gt", "Greater than", "Compare", {{"a", false}, {"b", false}}, {{"a>b", true}}, {}, "On when a > b"},
        {"lt", "Less than", "Compare", {{"a", false}, {"b", false}}, {{"a<b", true}}, {}, "On when a < b"},
        {"ge", "At least", "Compare", {{"a", false}, {"b", false}}, {{"a>=b", true}}, {}, "On when a >= b"},
        {"le", "At most", "Compare", {{"a", false}, {"b", false}}, {{"a<=b", true}}, {}, "On when a <= b"},
        {"eq", "Equal", "Compare", {{"a", false}, {"b", false}}, {{"a=b", true}}, {}, "On when a and b are equal"},
        {"ne", "Not equal", "Compare", {{"a", false}, {"b", false}}, {{"a≠b", true}}, {}, "On when a and b differ"},
        {"threshold", "Threshold", "Compare", {{"x", false}}, {{"in range", true}}, {{"min", "Min", "0.0", PK::Number}, {"max", "Max", "1.0", PK::Number}}, "On when x is between min and max"},
        // logic
        {"and", "AND", "Logic", {{"a", true}, {"b", true}}, {{"out", true}}, {}, "On when both are on"},
        {"or", "OR", "Logic", {{"a", true}, {"b", true}}, {{"out", true}}, {}, "On when either is on"},
        {"not", "NOT", "Logic", {{"in", true}}, {{"out", true}}, {}, "On when the input is off"},
        {"xor", "XOR", "Logic", {{"a", true}, {"b", true}}, {{"out", true}}, {}, "On when exactly one is on"},
        {"switch", "Number switch", "Logic", {{"on", true}, {"if on", false}, {"if off", false}}, {{"out", false}}, {}, "Passes one of two numbers, picked by an on/off"},
        // memory
        {"latch", "SR latch", "Memory", {{"set", true}, {"reset", true}}, {{"out", true}}, {}, "Turns on with set and stays on until reset"},
        {"toggle", "Push to toggle", "Memory", {{"press", true}}, {{"out", true}}, {}, "Flips on/off each time the input turns on"},
        {"pulse", "Pulse", "Memory", {{"in", true}}, {{"pulse", true}}, {}, "On for one tick when the input turns on"},
        {"memory", "Memory", "Memory", {{"set", true}, {"value", false}, {"reset", true}}, {{"stored", false}}, {{"reset_value", "Reset to", "0.0", PK::Number}}, "Stores the value while set is on; reset puts it back"},
        {"counter", "Counter", "Memory", {{"up", true}, {"down", true}, {"reset", true}}, {{"count", false}}, {{"step", "Step per tick", "1.0", PK::Number}, {"min", "Min", "0.0", PK::Number}, {"max", "Max", "100.0", PK::Number}},
         "Counts up or down every tick its input is on"},
        {"delta", "Change", "Memory", {{"x", false}}, {{"change", false}}, {}, "How much x changed since last tick"},
        // timing
        {"timer", "Timer", "Timing", {{"run", true}, {"reset", true}}, {{"done", true}, {"ticks", false}}, {{"ticks", "Duration (ticks)", "60.0", PK::Number}}, "Counts ticks while run is on; done once it reaches the duration"},
        {"blinker", "Blinker", "Timing", {{"enable", true}}, {{"out", true}}, {{"on", "On ticks", "30.0", PK::Number}, {"off", "Off ticks", "30.0", PK::Number}}, "Flashes on and off while enabled"},
        // control
        {"ramp", "Ramp", "Control", {{"target", false}}, {{"out", false}}, {{"up", "Up per tick", "0.01", PK::Number}, {"down", "Down per tick", "0.05", PK::Number}},
         "Moves toward the target at a set speed, with its own speed up and down"},
        {"smooth", "Smooth", "Control", {{"x", false}}, {{"out", false}}, {{"factor", "Factor (0-1)", "0.1", PK::Number}}, "Follows x smoothly; a smaller factor is smoother"},
        {"pid", "PID", "Control", {{"setpoint", false}, {"value", false}}, {{"out", false}},
         {{"kp", "P", "1.0", PK::Number}, {"ki", "I", "0.0", PK::Number}, {"kd", "D", "0.0", PK::Number}, {"min", "Min out", "-1.0", PK::Number}, {"max", "Max out", "1.0", PK::Number}},
         "Drives the value toward the setpoint"},
    };
    return defs;
}
inline const NodeDef* find_def(const std::string& kind) {
    for (auto& d : library()) if (kind == d.kind) return &d;
    return nullptr;
}

// ------------------------------------------------------------------------------------------------ the graph
struct Node {
    int id = 0;
    std::string kind;
    float x = 0, y = 0;
    std::map<std::string, std::string> params;
};
struct Wire { int from = 0, from_port = 0, to = 0, to_port = 0; };
struct Graph {
    std::vector<Node> nodes;
    std::vector<Wire> wires;
    int next_id = 1;

    Node* node(int id) { for (auto& n : nodes) if (n.id == id) return &n; return nullptr; }
    const Node* node(int id) const { for (auto& n : nodes) if (n.id == id) return &n; return nullptr; }
    Node& add(const std::string& kind, float x, float y) {
        Node n;
        n.id = next_id++;
        n.kind = kind;
        n.x = x; n.y = y;
        if (auto* d = find_def(kind)) for (auto& p : d->params) n.params[p.key] = p.def;
        nodes.push_back(n);
        return nodes.back();
    }
    void remove(int id) {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const Node& n) { return n.id == id; }), nodes.end());
        wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& w) { return w.from == id || w.to == id; }), wires.end());
    }
    const Wire* wire_into(int to, int port) const { for (auto& w : wires) if (w.to == to && w.to_port == port) return &w; return nullptr; }
    void unwire_input(int to, int port) {
        wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& w) { return w.to == to && w.to_port == port; }), wires.end());
    }
    // Connects an output to an input of the same kind (number or on/off); an input takes one wire.
    bool connect(int from, int from_port, int to, int to_port) {
        const Node *a = node(from), *b = node(to);
        if (!a || !b || from == to) return false;
        auto *da = find_def(a->kind), *db = find_def(b->kind);
        if (!da || !db || from_port < 0 || from_port >= int(da->outs.size()) || to_port < 0 || to_port >= int(db->ins.size())) return false;
        if (da->outs[size_t(from_port)].is_bool != db->ins[size_t(to_port)].is_bool) return false;
        unwire_input(to, to_port);
        wires.push_back({from, from_port, to, to_port});
        return true;
    }
};

// Variable names the generated code uses, so the editor can show live values on the wires.
inline std::string out_var(int id, int port) { return "n" + std::to_string(id) + "_" + std::to_string(port); }
inline std::string state_var(int id, int k) { return "n" + std::to_string(id) + "_s" + std::to_string(k); }

// ------------------------------------------------------------------------------------------------ generating code
inline std::string number_text(const std::string& s) {
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (!std::isfinite(v)) v = 0;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6f", v);
    std::string t = buf;
    while (t.size() > 1 && t.back() == '0' && t[t.size() - 2] != '.') t.pop_back();
    if (t == "-0.0") t = "0.0";
    return t;
}
inline bool valid_name(const std::string& s) {
    if (s.empty() || !ident_start(s[0])) return false;
    for (char c : s) if (!ident_char(c)) return false;
    return !keywords().count(s);
}

struct NodeProblem { int node; std::string msg; };
struct Generated {
    std::string code;                 // MCCode text, graph header included
    std::vector<int> line_node;       // per line of `code`: the node that line belongs to, or -1
    std::vector<NodeProblem> problems;
};

inline std::string escape(const std::string& s) {
    std::string out;
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if (ident_char(char(c)) || c == '.' || c == '-') out += char(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out.empty() ? "%" : out;   // a lone % stands for an empty value
}
inline std::string unescape(const std::string& s) {
    if (s == "%") return "";
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out += char(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else out += s[i];
    }
    return out;
}

// Nodes in the order they run: each after the nodes that feed it. Nodes in a loop keep their id order, and the
// wire that closes the loop reads last tick's value.
inline std::vector<const Node*> run_order(const Graph& g) {
    std::map<int, int> pending;
    std::map<int, std::vector<int>> feeds;
    for (auto& n : g.nodes) pending[n.id] = 0;
    for (auto& w : g.wires) if (pending.count(w.from) && pending.count(w.to) && w.from != w.to) { ++pending[w.to]; feeds[w.from].push_back(w.to); }
    std::vector<const Node*> order;
    std::set<int> done;
    while (order.size() < g.nodes.size()) {
        const Node* next = nullptr;
        for (auto& n : g.nodes) if (!done.count(n.id) && pending[n.id] == 0) { next = &n; break; }
        if (!next) for (auto& n : g.nodes) if (!done.count(n.id)) { next = &n; break; }   // a loop: break it at the lowest id
        done.insert(next->id);
        order.push_back(next);
        for (int t : feeds[next->id]) --pending[t];
    }
    // part outputs last, so they send this tick's values
    std::stable_partition(order.begin(), order.end(), [](const Node* n) { return n->kind != "part_out" && n->kind != "part_out_bool"; });
    return order;
}

inline Generated generate(const Graph& g) {
    Generated out;
    std::vector<std::string> body;            // lines inside on_tick, indented
    std::vector<int> body_node;
    std::vector<std::string> defs;            // define lines
    std::vector<int> defs_node;
    auto def = [&](int node, const std::string& type, const std::string& name) { defs.push_back("define var " + type + " " + name); defs_node.push_back(node); };

    for (const Node* n : run_order(g)) {
        const NodeDef* d = find_def(n->kind);
        if (!d) { out.problems.push_back({n->id, "Unknown node kind '" + n->kind + "'"}); continue; }
        int id = n->id;
        auto P = [&](const char* key) { auto it = n->params.find(key); return it == n->params.end() ? std::string() : it->second; };
        auto num = [&](const char* key) { return number_text(P(key)); };
        auto in = [&](int port) -> std::string {
            if (const Wire* w = g.wire_into(id, port)) if (g.node(w->from)) return out_var(w->from, w->from_port);
            return d->ins[size_t(port)].is_bool ? "false" : "0.0";
        };
        auto o = [&](int port) { return out_var(id, port); };
        auto s = [&](int k) { return state_var(id, k); };
        auto line = [&](int depth, const std::string& t) { body.push_back(std::string(size_t(depth), '\t') + t); body_node.push_back(id); };
        for (size_t i = 0; i < d->outs.size(); ++i) def(id, d->outs[i].is_bool ? "bool" : "fractional", o(int(i)));
        auto part_name = [&]() -> std::string {
            std::string part = P("part"), ch = P("channel");
            if (!valid_name(part)) { out.problems.push_back({id, "'" + part + "' isn't a valid part name (letters, digits and _)"}); return ""; }
            if (!ch.empty() && !valid_name(ch)) { out.problems.push_back({id, "'" + ch + "' isn't a valid channel name"}); return ""; }
            return ch.empty() ? part : part + "." + ch;
        };
        auto bin = [&](const char* op) { line(1, o(0) + " = " + in(0) + " " + op + " " + in(1)); };
        const std::string k = n->kind;
        if (k == "part_in") {
            std::string p = part_name();
            if (!p.empty()) line(1, o(0) + " = " + (P("type") == "whole number" ? "to_fractional(" + p + ")" : p));
        } else if (k == "part_in_bool") {
            std::string p = part_name();
            if (!p.empty()) line(1, o(0) + " = " + p);
        } else if (k == "part_out") {
            std::string p = part_name();
            if (!p.empty()) line(1, p + " = " + (P("type") == "whole number" ? "to_integer(" + in(0) + ")" : in(0)));
        } else if (k == "part_out_bool") {
            std::string p = part_name();
            if (!p.empty()) line(1, p + " = " + in(0));
        } else if (k == "const") line(1, o(0) + " = " + num("value"));
        else if (k == "const_bool") line(1, o(0) + " = " + std::string(P("value") == "off" ? "false" : "true"));
        else if (k == "add") bin("+");
        else if (k == "sub") bin("-");
        else if (k == "mul") bin("*");
        else if (k == "pow") bin("^");
        else if (k == "div" || k == "mod") {
            line(1, "if " + in(1) + " == 0.0 {");
            line(2, o(0) + " = 0.0");
            line(1, "} else {");
            line(2, o(0) + " = " + in(0) + (k == "div" ? " / " : " % ") + in(1));
            line(1, "}");
        } else if (k == "min" || k == "max") {
            line(1, "if " + in(0) + (k == "min" ? " < " : " > ") + in(1) + " {");
            line(2, o(0) + " = " + in(0));
            line(1, "} else {");
            line(2, o(0) + " = " + in(1));
            line(1, "}");
        } else if (k == "clamp") {
            line(1, o(0) + " = " + in(0));
            line(1, "if " + o(0) + " < " + num("min") + " {");
            line(2, o(0) + " = " + num("min"));
            line(1, "}");
            line(1, "if " + o(0) + " > " + num("max") + " {");
            line(2, o(0) + " = " + num("max"));
            line(1, "}");
        } else if (k == "abs" || k == "sqrt" || k == "sin" || k == "cos") line(1, o(0) + " = " + k + "(" + in(0) + ")");
        else if (k == "neg") line(1, o(0) + " = 0.0 - " + in(0));
        else if (k == "gt") bin(">");
        else if (k == "lt") bin("<");
        else if (k == "ge") bin(">=");
        else if (k == "le") bin("<=");
        else if (k == "eq") bin("==");
        else if (k == "ne") bin("!=");
        else if (k == "threshold") line(1, o(0) + " = " + in(0) + " >= " + num("min") + " and " + in(0) + " <= " + num("max"));
        else if (k == "and") bin("and");
        else if (k == "or") bin("or");
        else if (k == "not") line(1, o(0) + " = not " + in(0));
        else if (k == "xor") bin("!=");
        else if (k == "switch") {
            line(1, "if " + in(0) + " {");
            line(2, o(0) + " = " + in(1));
            line(1, "} else {");
            line(2, o(0) + " = " + in(2));
            line(1, "}");
        } else if (k == "latch") {
            line(1, "if " + in(0) + " {"); line(2, o(0) + " = true"); line(1, "}");
            line(1, "if " + in(1) + " {"); line(2, o(0) + " = false"); line(1, "}");
        } else if (k == "toggle" || k == "pulse") {
            def(id, "bool", s(0));
            if (k == "toggle") { line(1, "if " + in(0) + " and not " + s(0) + " {"); line(2, o(0) + " = not " + o(0)); line(1, "}"); }
            else line(1, o(0) + " = " + in(0) + " and not " + s(0));
            line(1, s(0) + " = " + in(0));
        } else if (k == "memory") {
            line(1, "if " + in(0) + " {"); line(2, o(0) + " = " + in(1)); line(1, "}");
            line(1, "if " + in(2) + " {"); line(2, o(0) + " = " + num("reset_value")); line(1, "}");
        } else if (k == "counter") {
            line(1, "if " + in(0) + " {"); line(2, o(0) + " = " + o(0) + " + " + num("step")); line(1, "}");
            line(1, "if " + in(1) + " {"); line(2, o(0) + " = " + o(0) + " - " + num("step")); line(1, "}");
            line(1, "if " + in(2) + " {"); line(2, o(0) + " = " + num("min")); line(1, "}");
            line(1, "if " + o(0) + " < " + num("min") + " {"); line(2, o(0) + " = " + num("min")); line(1, "}");
            line(1, "if " + o(0) + " > " + num("max") + " {"); line(2, o(0) + " = " + num("max")); line(1, "}");
        } else if (k == "delta") {
            def(id, "fractional", s(0));
            line(1, o(0) + " = " + in(0) + " - " + s(0));
            line(1, s(0) + " = " + in(0));
        } else if (k == "timer") {
            line(1, "if " + in(1) + " {");
            line(2, o(1) + " = 0.0");
            line(1, "} elseif " + in(0) + " and " + o(1) + " < " + num("ticks") + " {");
            line(2, o(1) + " = " + o(1) + " + 1.0");
            line(1, "}");
            line(1, o(0) + " = " + o(1) + " >= " + num("ticks"));
        } else if (k == "blinker") {
            def(id, "fractional", s(0));
            line(1, "if " + in(0) + " {");
            line(2, s(0) + " = " + s(0) + " + 1.0");
            line(2, "if " + s(0) + " >= " + num("on") + " + " + num("off") + " {");
            line(3, s(0) + " = 0.0");
            line(2, "}");
            line(2, o(0) + " = " + s(0) + " < " + num("on"));
            line(1, "} else {");
            line(2, s(0) + " = 0.0");
            line(2, o(0) + " = false");
            line(1, "}");
        } else if (k == "ramp") {
            line(1, "if " + o(0) + " < " + in(0) + " {");
            line(2, o(0) + " = " + o(0) + " + " + num("up"));
            line(2, "if " + o(0) + " > " + in(0) + " {");
            line(3, o(0) + " = " + in(0));
            line(2, "}");
            line(1, "} else {");
            line(2, o(0) + " = " + o(0) + " - " + num("down"));
            line(2, "if " + o(0) + " < " + in(0) + " {");
            line(3, o(0) + " = " + in(0));
            line(2, "}");
            line(1, "}");
        } else if (k == "smooth") line(1, o(0) + " = " + o(0) + " + (" + in(0) + " - " + o(0) + ") * " + num("factor"));
        else if (k == "pid") {
            def(id, "fractional", s(0));   // error
            def(id, "fractional", s(1));   // integral
            def(id, "fractional", s(2));   // last error
            line(1, s(0) + " = " + in(0) + " - " + in(1));
            line(1, s(1) + " = " + s(1) + " + " + s(0) + " * " + num("ki"));
            line(1, "if " + s(1) + " < " + num("min") + " {"); line(2, s(1) + " = " + num("min")); line(1, "}");
            line(1, "if " + s(1) + " > " + num("max") + " {"); line(2, s(1) + " = " + num("max")); line(1, "}");
            line(1, o(0) + " = " + s(0) + " * " + num("kp") + " + " + s(1) + " + (" + s(0) + " - " + s(2) + ") * " + num("kd"));
            line(1, s(2) + " = " + s(0));
            line(1, "if " + o(0) + " < " + num("min") + " {"); line(2, o(0) + " = " + num("min")); line(1, "}");
            line(1, "if " + o(0) + " > " + num("max") + " {"); line(2, o(0) + " = " + num("max")); line(1, "}");
        }
    }

    std::vector<std::string> code_lines;
    std::vector<int> code_nodes;
    for (size_t i = 0; i < defs.size(); ++i) { code_lines.push_back(defs[i]); code_nodes.push_back(defs_node[i]); }
    code_lines.push_back("on_tick {"); code_nodes.push_back(-1);
    for (size_t i = 0; i < body.size(); ++i) { code_lines.push_back(body[i]); code_nodes.push_back(body_node[i]); }
    code_lines.push_back("}"); code_nodes.push_back(-1);
    std::string code_text;
    for (auto& l : code_lines) code_text += l + "\n";

    std::vector<std::string> header;
    header.push_back("#@nodes 1 - made in the MCCode node editor; edit it there");
    for (auto& n : g.nodes) {
        std::string l = "#@n " + std::to_string(n.id) + " " + n.kind + " " + std::to_string(int(std::lround(n.x))) + " " + std::to_string(int(std::lround(n.y)));
        for (auto& [key, v] : n.params) l += " " + key + "=" + escape(v);
        header.push_back(l);
    }
    for (auto& w : g.wires) header.push_back("#@w " + std::to_string(w.from) + " " + std::to_string(w.from_port) + " " + std::to_string(w.to) + " " + std::to_string(w.to_port));
    header.push_back("#@end");

    for (auto& h : header) { out.code += h + "\n"; out.line_node.push_back(-1); }
    out.code += code_text;
    out.line_node.insert(out.line_node.end(), code_nodes.begin(), code_nodes.end());
    if (!out.code.empty() && out.code.back() == '\n') out.code.pop_back();
    return out;
}

// ------------------------------------------------------------------------------------------------ reading a graph back
struct Parsed {
    bool found = false;        // the text starts with a node graph header
    bool hand_edited = false;  // the code below the header no longer matches what the graph generates
    Graph graph;
};
inline std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> w;
    std::string cur;
    for (char c : s) { if (c == ' ' || c == '\t') { if (!cur.empty()) w.push_back(cur), cur.clear(); } else cur += c; }
    if (!cur.empty()) w.push_back(cur);
    return w;
}
inline Parsed parse(const std::string& text) {
    Parsed p;
    size_t pos = 0;
    bool ended = false;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        line = start == std::string::npos ? "" : line.substr(start);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        if (line.rfind("#@", 0) != 0) { if (!line.empty() && p.found) break; if (!line.empty()) return p; continue; }
        auto w = split_words(line.substr(2));
        if (w.empty()) continue;
        if (w[0] == "nodes") p.found = true;
        else if (!p.found) return p;
        else if (w[0] == "n" && w.size() >= 5) {
            Node n;
            n.id = std::atoi(w[1].c_str());
            n.kind = w[2];
            n.x = float(std::atof(w[3].c_str()));
            n.y = float(std::atof(w[4].c_str()));
            if (auto* d = find_def(n.kind)) for (auto& prm : d->params) n.params[prm.key] = prm.def;
            for (size_t i = 5; i < w.size(); ++i) {
                size_t eq = w[i].find('=');
                if (eq != std::string::npos) n.params[w[i].substr(0, eq)] = unescape(w[i].substr(eq + 1));
            }
            if (n.id > 0 && find_def(n.kind)) { p.graph.nodes.push_back(n); p.graph.next_id = std::max(p.graph.next_id, n.id + 1); }
        } else if (w[0] == "w" && w.size() >= 5) {
            p.graph.wires.push_back({std::atoi(w[1].c_str()), std::atoi(w[2].c_str()), std::atoi(w[3].c_str()), std::atoi(w[4].c_str())});
        } else if (w[0] == "end") {
            ended = true;
            break;
        }
    }
    if (!p.found) return p;
    // drop wires to nodes or ports that aren't there
    auto& gr = p.graph;
    std::vector<Wire> ok;
    for (auto& wr : gr.wires) {
        const Node *a = gr.node(wr.from), *b = gr.node(wr.to);
        if (!a || !b) continue;
        auto *da = find_def(a->kind), *db = find_def(b->kind);
        if (wr.from_port < 0 || wr.from_port >= int(da->outs.size()) || wr.to_port < 0 || wr.to_port >= int(db->ins.size())) continue;
        if (da->outs[size_t(wr.from_port)].is_bool != db->ins[size_t(wr.to_port)].is_bool) continue;
        bool dup = false;
        for (auto& o : ok) if (o.to == wr.to && o.to_port == wr.to_port) dup = true;
        if (!dup) ok.push_back(wr);
    }
    gr.wires = ok;
    // hand edits: compare the game text the code and the graph turn into, leaving out comments
    auto canon = [](const std::string& t) {
        Compiled c = compile(t);
        if (!c.ok) return std::string("!");
        std::string out;
        size_t i = 0;
        while (i < c.canonical.size()) {
            size_t nl = c.canonical.find('\n', i);
            std::string l = c.canonical.substr(i, nl - i);
            i = nl == std::string::npos ? c.canonical.size() : nl + 1;
            if (l.find_first_not_of('\t') != std::string::npos && l[l.find_first_not_of('\t')] != '#') out += l + "\n";
        }
        return out;
    };
    p.hand_edited = !ended || canon(text) != canon(generate(gr).code);
    return p;
}

}  // namespace mccode::nodes
