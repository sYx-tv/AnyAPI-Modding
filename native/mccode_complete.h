#pragma once
// MCCode autocomplete: what to suggest at the cursor while typing in the Code view. Pure logic so
// tests/mccode_complete_test.cpp runs anywhere.
#include "mccode_lang.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace mccode::complete {

struct PartPort { std::string part, channel; bool input = true; };
// insert uses the side panel's snippet form: "$" is where the cursor lands, "\n" lines take the line's indent.
struct Item { std::string label, detail, insert; uint32_t color = 0xffe8ecef; };
struct Result {
    int start = 0;          // column where the word being completed starts
    std::string prefix;     // what has been typed of it
    bool after_dot = false;
    std::vector<Item> items;
};

inline bool word_char(char c) { return std::isalnum((unsigned char)c) || c == '_'; }
inline std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }

inline Result suggest(const std::string& line, int col, const Symbols& sy, const std::vector<PartPort>& ports) {
    Result r;
    col = std::clamp(col, 0, int(line.size()));
    bool in_string = false;
    for (int i = 0; i < col; ++i) {
        if (line[size_t(i)] == '"') in_string = !in_string;
        else if (line[size_t(i)] == '#' && !in_string) return r;   // comment
    }
    if (in_string) return r;
    int start = col;
    while (start > 0 && word_char(line[size_t(start - 1)])) --start;
    r.start = start;
    r.prefix = line.substr(size_t(start), size_t(col - start));
    if (!r.prefix.empty() && std::isdigit((unsigned char)r.prefix[0])) return r;
    // the word after the cursor, so accepting doesn't double up text that is already there
    std::vector<Item> all;
    auto add = [&](std::string label, std::string detail, std::string insert, uint32_t color) {
        all.push_back({std::move(label), std::move(detail), std::move(insert), color});
    };
    if (start > 0 && line[size_t(start - 1)] == '.') {
        r.after_dot = true;
        int o = start - 1;
        while (o > 0 && word_char(line[size_t(o - 1)])) --o;
        std::string obj = line.substr(size_t(o), size_t(start - 1 - o));
        if (obj.empty()) return r;
        for (auto& p : ports)
            if (p.part == obj && !p.channel.empty()) add(p.channel, p.input ? "part input" : "part output", p.channel + "$", p.input ? 0xff7cc8ff : 0xffffb86c);
        if (sy.lists.count(obj)) {
            add("append", "add to the end", "append($)", 0xffdcdcaa);
            add("insert", "insert at a position", "insert(0, $)", 0xffdcdcaa);
            add("erase", "remove a position", "erase($)", 0xffdcdcaa);
            add("clear", "remove everything", "clear()$", 0xffdcdcaa);
        }
    } else {
        if (r.prefix.empty()) return r;
        std::string before = line.substr(0, size_t(start));
        bool stmt_start = before.find_first_not_of(" \t") == std::string::npos;
        std::string trimmed = before;
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();
        auto ends_with = [&](const std::string& w) {
            return trimmed.size() >= w.size() && trimmed.compare(trimmed.size() - w.size(), w.size(), w) == 0 &&
                   (trimmed.size() == w.size() || !word_char(trimmed[trimmed.size() - w.size() - 1]));
        };
        if (ends_with("define")) {
            for (const char* k : {"var", "list", "in", "out", "function", "type", "member"}) add(k, "define", std::string(k) + " $", 0xffc586c0);
        } else if (ends_with("var") || ends_with("list") || ends_with("in") || ends_with("out") || ends_with("member")) {
            for (const char* t : {"bool", "integer", "fractional", "string", "analogue"}) add(t, "type", std::string(t) + " $", 0xff4ec9b0);
            for (auto& t : sy.types) add(t, "your type", t + " $", 0xff4ec9b0);
        } else {
            if (stmt_start) {
                add("on_tick", "runs every game tick", "on_tick {\n\t$\n}", 0xffc586c0);
                add("if", "block", "if $ {\n\t\n}", 0xffc586c0);
                add("elseif", "block", "elseif $ {\n\t\n}", 0xffc586c0);
                add("else", "block", "else {\n\t$\n}", 0xffc586c0);
                add("repeat", "block", "repeat $ {\n\t\n}", 0xffc586c0);
                add("foreach", "block", "foreach $ {\n\t\n}", 0xffc586c0);
                add("define", "var, list, in, out, function, type", "define $", 0xffc586c0);
            } else {
                for (const char* k : {"and", "or", "not", "contains"}) add(k, "operator", std::string(k) + " $", 0xffc586c0);
                add("true", "value", "true$", 0xff569cd6);
                add("false", "value", "false$", 0xff569cd6);
                for (auto& f : kFunctions) add(f.name, "function", std::string(f.name) + "($)", 0xffdcdcaa);
                add("rand", "random between low and high", "rand($0, 10)", 0xffdcdcaa);
            }
            for (auto& [n, t] : sy.vars) add(n, (sy.lists.count(n) ? "list of " : "") + t, n + "$", 0xffe8ecef);
            for (auto& [n, t] : sy.ins) add(n, "in " + t, n + "$", 0xff7cc8ff);
            for (auto& [n, t] : sy.outs) add(n, "out " + t, n + "$", 0xffffb86c);
            for (auto& [n, t] : sy.params) add(n, "parameter " + t, n + "$", 0xffe8ecef);
            for (auto& f : sy.functions) {
                bool args = sy.function_params.count(f) && !sy.function_params.at(f).empty();
                add(f, "your function", f + (args ? "($)" : "()$"), 0xfff1d36b);
            }
            std::vector<std::string> parts;
            for (auto& p : ports) if (!p.part.empty() && std::find(parts.begin(), parts.end(), p.part) == parts.end()) parts.push_back(p.part);
            for (auto& p : parts) add(p, "wired part", p + ".$", 0xff6fd3c4);
        }
    }
    // filter: prefix matches first (case-insensitive), then names containing it
    std::string lp = lower(r.prefix);
    std::vector<std::pair<int, Item>> ranked;
    for (auto& it : all) {
        std::string ll = lower(it.label);
        int rank = lp.empty() ? 0 : ll.compare(0, lp.size(), lp) == 0 ? (it.label.compare(0, r.prefix.size(), r.prefix) == 0 ? 0 : 1)
                                  : ll.find(lp) != std::string::npos ? 2 : -1;
        if (rank < 0) continue;
        if (std::any_of(ranked.begin(), ranked.end(), [&](auto& x) { return x.second.label == it.label; })) continue;
        ranked.push_back({rank, it});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second.label.size() < b.second.label.size();
    });
    for (auto& [rank, it] : ranked) r.items.push_back(it);
    // nothing to add when the only suggestion is exactly what's typed
    if (r.items.size() == 1 && r.items[0].label == r.prefix && !r.after_dot) r.items.clear();
    if (r.items.size() > 40) r.items.resize(40);
    return r;
}

}  // namespace mccode::complete
