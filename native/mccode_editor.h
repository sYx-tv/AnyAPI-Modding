#pragma once
// MCCode text editor model: lines, cursor, selection, undo. Pure logic so tests/mccode_editor_test.cpp runs anywhere.
// Columns are byte offsets into a line; the language is ASCII, so typed characters are limited to printable ASCII.
// Tabs show as kTabWidth columns; visual_col / byte_col convert between the two.
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace mccode {

constexpr int kTabWidth = 4;

struct Pos {
    int line = 0, col = 0;
    bool operator==(const Pos& o) const { return line == o.line && col == o.col; }
    bool operator!=(const Pos& o) const { return !(*this == o); }
    bool operator<(const Pos& o) const { return line < o.line || (line == o.line && col < o.col); }
};

class Editor {
public:
    Editor() : lines_(1) {}
    bool auto_pairs = false;   // see type_char

    // ------------------------------------------------------------------ whole text
    void set_text(const std::string& text, bool keep_undo = false) {
        if (keep_undo) snapshot();
        else { undo_.clear(); redo_.clear(); }
        lines_.clear();
        std::string cur;
        for (char c : text) {
            if (c == '\r') continue;
            if (c == '\n') { lines_.push_back(cur); cur.clear(); } else cur += c;
        }
        lines_.push_back(cur);
        cursor_ = anchor_ = {0, 0};
        last_kind_ = Kind::None;
        ++revision_;
    }
    std::string text() const {
        std::string s;
        for (size_t i = 0; i < lines_.size(); ++i) s += lines_[i] + (i + 1 < lines_.size() ? "\n" : "");
        return s;
    }
    const std::vector<std::string>& lines() const { return lines_; }
    unsigned revision() const { return revision_; }

    // ------------------------------------------------------------------ cursor and selection
    Pos cursor() const { return cursor_; }
    Pos anchor() const { return anchor_; }
    bool has_selection() const { return cursor_ != anchor_; }
    Pos sel_start() const { return std::min(cursor_, anchor_); }
    Pos sel_end() const { return cursor_ < anchor_ ? anchor_ : cursor_; }
    void set_cursor(Pos p, bool extend) {
        cursor_ = clamp(p);
        if (!extend) anchor_ = cursor_;
        want_vcol_ = -1;
        last_kind_ = Kind::None;
    }
    void select_all() { anchor_ = {0, 0}; cursor_ = end_pos(); want_vcol_ = -1; }
    void select_line(int line) {
        line = std::clamp(line, 0, int(lines_.size()) - 1);
        anchor_ = {line, 0};
        cursor_ = line + 1 < int(lines_.size()) ? Pos{line + 1, 0} : Pos{line, int(lines_[size_t(line)].size())};
    }
    void select_word_at(Pos p) {
        p = clamp(p);
        const std::string& l = lines_[size_t(p.line)];
        int a = p.col, b = p.col;
        while (a > 0 && word_char(l[size_t(a - 1)])) --a;
        while (b < int(l.size()) && word_char(l[size_t(b)])) ++b;
        anchor_ = {p.line, a};
        cursor_ = {p.line, b};
    }
    std::string selection_text() const {
        if (!has_selection()) return "";
        Pos a = sel_start(), b = sel_end();
        if (a.line == b.line) return lines_[size_t(a.line)].substr(size_t(a.col), size_t(b.col - a.col));
        std::string s = lines_[size_t(a.line)].substr(size_t(a.col)) + "\n";
        for (int i = a.line + 1; i < b.line; ++i) s += lines_[size_t(i)] + "\n";
        return s + lines_[size_t(b.line)].substr(0, size_t(b.col));
    }

    enum class Move { Left, Right, Up, Down, Home, End, PageUp, PageDown, DocStart, DocEnd, WordLeft, WordRight };
    void move(Move m, bool extend, int page = 20) {
        if (!extend && has_selection() && (m == Move::Left || m == Move::Right)) {
            Pos p = m == Move::Left ? sel_start() : sel_end();
            set_cursor(p, false);
            return;
        }
        Pos p = cursor_;
        int keep_vcol = -1;
        switch (m) {
        case Move::Left:
            if (p.col > 0) --p.col; else if (p.line > 0) { --p.line; p.col = len(p.line); }
            break;
        case Move::Right:
            if (p.col < len(p.line)) ++p.col; else if (p.line + 1 < int(lines_.size())) { ++p.line; p.col = 0; }
            break;
        case Move::Up: case Move::Down: case Move::PageUp: case Move::PageDown: {
            int v = want_vcol_ >= 0 ? want_vcol_ : visual_col(p.line, p.col);
            int d = m == Move::Up ? -1 : m == Move::Down ? 1 : m == Move::PageUp ? -page : page;
            int target = std::clamp(p.line + d, 0, int(lines_.size()) - 1);
            if (target == p.line && (m == Move::Up || m == Move::PageUp)) p.col = 0;
            else if (target == p.line) p.col = len(p.line);
            else { p.line = target; p.col = byte_col(p.line, v); }
            keep_vcol = v;
            break;
        }
        case Move::Home: {   // first press goes to the first non-blank character, the next to column 0
            int first = 0;
            const std::string& l = lines_[size_t(p.line)];
            while (first < int(l.size()) && (l[size_t(first)] == ' ' || l[size_t(first)] == '\t')) ++first;
            p.col = p.col == first ? 0 : first;
            break;
        }
        case Move::End: p.col = len(p.line); break;
        case Move::DocStart: p = {0, 0}; break;
        case Move::DocEnd: p = end_pos(); break;
        case Move::WordLeft: {
            if (p.col == 0) { if (p.line > 0) { --p.line; p.col = len(p.line); } break; }
            const std::string& l = lines_[size_t(p.line)];
            while (p.col > 0 && l[size_t(p.col - 1)] == ' ') --p.col;
            bool w = p.col > 0 && word_char(l[size_t(p.col - 1)]);
            while (p.col > 0 && l[size_t(p.col - 1)] != ' ' && word_char(l[size_t(p.col - 1)]) == w) --p.col;
            break;
        }
        case Move::WordRight: {
            if (p.col == len(p.line)) { if (p.line + 1 < int(lines_.size())) { ++p.line; p.col = 0; } break; }
            const std::string& l = lines_[size_t(p.line)];
            bool w = word_char(l[size_t(p.col)]);
            while (p.col < int(l.size()) && l[size_t(p.col)] != ' ' && word_char(l[size_t(p.col)]) == w) ++p.col;
            while (p.col < int(l.size()) && l[size_t(p.col)] == ' ') ++p.col;
            break;
        }
        }
        cursor_ = p;
        if (!extend) anchor_ = p;
        want_vcol_ = keep_vcol;
        last_kind_ = Kind::None;
    }

    // ------------------------------------------------------------------ editing
    // Inserts text at the cursor, replacing the selection. "\n" splits lines; nothing auto-indents here.
    void insert(const std::string& s, bool typing = false) {
        if (s.empty() && !has_selection()) return;
        begin_edit(typing ? Kind::Typing : Kind::Other);
        erase_selection();
        std::string clean;
        for (char c : s) if (c == '\n' || c == '\t' || (c >= 32 && c < 127)) clean += c; else if (c != '\r') clean += ' ';
        std::string& l = lines_[size_t(cursor_.line)];
        std::string tail = l.substr(size_t(cursor_.col));
        l.erase(size_t(cursor_.col));
        size_t start = 0;
        while (true) {
            size_t nl = clean.find('\n', start);
            std::string piece = clean.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            lines_[size_t(cursor_.line)] += piece;
            cursor_.col = len(cursor_.line);
            if (nl == std::string::npos) break;
            lines_.insert(lines_.begin() + cursor_.line + 1, std::string());
            ++cursor_.line;
            cursor_.col = 0;
            start = nl + 1;
        }
        lines_[size_t(cursor_.line)] += tail;
        anchor_ = cursor_;
        want_vcol_ = -1;
        ++revision_;
    }
    // Enter: keeps the current indentation, one more after "{", and puts a lone "}" back one level.
    void newline() {
        std::string indent = leading_ws(cursor_.line);
        if ((size_t)cursor_.col < indent.size()) indent.resize(size_t(cursor_.col));
        std::string before = lines_[size_t(cursor_.line)].substr(0, size_t(cursor_.col));
        std::string after = lines_[size_t(cursor_.line)].substr(size_t(cursor_.col));
        bool opens = rtrim(before).size() && rtrim(before).back() == '{';
        bool closes_next = ltrim(after).size() && ltrim(after)[0] == '}';
        if (opens && closes_next) {
            insert("\n" + indent + "\t\n" + indent);
            cursor_ = {cursor_.line - 1, int(indent.size()) + 1};
            anchor_ = cursor_;
            return;
        }
        insert("\n" + indent + (opens ? "\t" : ""));
    }
    // Typed character. "}" on an otherwise blank line drops one indent level.
    // With auto_pairs on, ( [ { " type their closing partner, and typing a closer that is already next steps over it.
    void type_char(char c) {
        if (auto_pairs && !has_selection()) {
            const std::string& l = lines_[size_t(cursor_.line)];
            char next = (size_t)cursor_.col < l.size() ? l[size_t(cursor_.col)] : 0;
            char prev = cursor_.col > 0 ? l[size_t(cursor_.col - 1)] : 0;
            bool in_string = std::count(l.begin(), l.begin() + cursor_.col, '"') % 2 == 1;
            if ((c == ')' || c == ']' || c == '}' || c == '"') && next == c) { set_cursor({cursor_.line, cursor_.col + 1}, false); return; }
            bool word_next = next && (std::isalnum((unsigned char)next) || next == '_');
            char close = c == '(' ? ')' : c == '[' ? ']' : c == '{' ? '}' : 0;
            if (c == '"' && !in_string && !(std::isalnum((unsigned char)prev) || prev == '_') && !word_next) close = '"';
            if (close && c != '"' && (in_string || word_next)) close = 0;
            if (close) {
                insert(std::string(1, c) + close, true);
                --cursor_.col;
                anchor_ = cursor_;
                return;
            }
        }
        if (c == '}' && !has_selection()) {
            const std::string& l = lines_[size_t(cursor_.line)];
            bool blank = ltrim(l.substr(0, size_t(cursor_.col))).empty();
            if (blank && cursor_.col > 0 && l[size_t(cursor_.col - 1)] == '\t') {
                begin_edit(Kind::Typing);
                lines_[size_t(cursor_.line)].erase(size_t(cursor_.col - 1), 1);
                --cursor_.col;
                anchor_ = cursor_;
                ++revision_;
            }
        }
        insert(std::string(1, c), true);
    }
    void backspace(bool word = false) {
        if (has_selection()) { begin_edit(Kind::Other); erase_selection(); ++revision_; return; }
        if (cursor_ == Pos{0, 0}) return;
        if (auto_pairs && !word && cursor_.col > 0) {   // backspace inside an empty pair removes both
            const std::string& l = lines_[size_t(cursor_.line)];
            char prev = l[size_t(cursor_.col - 1)], next = (size_t)cursor_.col < l.size() ? l[size_t(cursor_.col)] : 0;
            if ((prev == '(' && next == ')') || (prev == '[' && next == ']') || (prev == '{' && next == '}') || (prev == '"' && next == '"')) {
                begin_edit(Kind::Delete);
                lines_[size_t(cursor_.line)].erase(size_t(cursor_.col - 1), 2);
                --cursor_.col;
                anchor_ = cursor_;
                ++revision_;
                return;
            }
        }
        begin_edit(Kind::Delete);
        Pos from = cursor_;
        move(word ? Move::WordLeft : Move::Left, true);
        std::swap(cursor_, anchor_);
        cursor_ = from;
        erase_selection();
        ++revision_;
    }
    void del(bool word = false) {
        if (has_selection()) { begin_edit(Kind::Other); erase_selection(); ++revision_; return; }
        if (cursor_ == end_pos()) return;
        begin_edit(Kind::Delete);
        Pos from = cursor_;
        move(word ? Move::WordRight : Move::Right, true);
        anchor_ = from;
        erase_selection();
        ++revision_;
    }
    // Tab with a multi-line selection indents every selected line; Shift+Tab removes one level.
    void indent(bool out) {
        if (!has_selection() || sel_start().line == sel_end().line) {
            if (!out) { insert("\t"); return; }
            out_line(cursor_.line);
            return;
        }
        begin_edit(Kind::Other);
        Pos a = sel_start(), b = sel_end();
        int last = b.col == 0 ? b.line - 1 : b.line;
        for (int i = a.line; i <= last; ++i) {
            if (out) {
                std::string& l = lines_[size_t(i)];
                if (!l.empty() && l[0] == '\t') l.erase(0, 1);
                else { size_t n = 0; while (n < 4 && n < l.size() && l[n] == ' ') ++n; l.erase(0, n); }
            } else if (!lines_[size_t(i)].empty()) {
                lines_[size_t(i)].insert(0, "\t");
            }
        }
        anchor_ = clamp({a.line, 0});
        cursor_ = clamp({b.col == 0 ? b.line : b.line, b.col == 0 ? 0 : len(b.line)});
        ++revision_;
    }

    bool undo() { return swap_history(undo_, redo_); }
    bool redo() { return swap_history(redo_, undo_); }

    // ------------------------------------------------------------------ columns
    int visual_col(int line, int col) const {
        if (line < 0 || line >= int(lines_.size())) return 0;
        const std::string& l = lines_[size_t(line)];
        int v = 0;
        for (int i = 0; i < col && i < int(l.size()); ++i) v = l[size_t(i)] == '\t' ? (v / kTabWidth + 1) * kTabWidth : v + 1;
        return v;
    }
    int byte_col(int line, int vcol) const {
        if (line < 0 || line >= int(lines_.size())) return 0;
        const std::string& l = lines_[size_t(line)];
        int v = 0;
        for (int i = 0; i < int(l.size()); ++i) {
            int nv = l[size_t(i)] == '\t' ? (v / kTabWidth + 1) * kTabWidth : v + 1;
            if (vcol < (v + nv + 1) / 2) return i;   // nearer the left edge of this character
            v = nv;
        }
        return int(l.size());
    }

private:
    enum class Kind { None, Typing, Delete, Other };
    struct State { std::vector<std::string> lines; Pos cursor, anchor; };
    std::vector<std::string> lines_;
    Pos cursor_, anchor_;
    int want_vcol_ = -1;
    unsigned revision_ = 0;
    std::vector<State> undo_, redo_;
    Kind last_kind_ = Kind::None;

    static bool word_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
    static std::string ltrim(const std::string& s) { size_t i = s.find_first_not_of(" \t"); return i == std::string::npos ? "" : s.substr(i); }
    static std::string rtrim(const std::string& s) { size_t i = s.find_last_not_of(" \t"); return i == std::string::npos ? "" : s.substr(0, i + 1); }
    std::string leading_ws(int line) const {
        const std::string& l = lines_[size_t(line)];
        size_t i = 0;
        while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) ++i;
        return l.substr(0, i);
    }
    int len(int line) const { return int(lines_[size_t(line)].size()); }
    Pos end_pos() const { return {int(lines_.size()) - 1, len(int(lines_.size()) - 1)}; }
    Pos clamp(Pos p) const {
        p.line = std::clamp(p.line, 0, int(lines_.size()) - 1);
        p.col = std::clamp(p.col, 0, len(p.line));
        return p;
    }
    void out_line(int line) {
        std::string& l = lines_[size_t(line)];
        int removed = 0;
        if (!l.empty() && l[0] == '\t') removed = 1;
        else while (removed < 4 && removed < int(l.size()) && l[size_t(removed)] == ' ') ++removed;
        if (!removed) return;
        begin_edit(Kind::Other);
        l.erase(0, size_t(removed));
        cursor_.col = std::max(0, cursor_.col - removed);
        anchor_ = cursor_;
        ++revision_;
    }
    void snapshot() {
        undo_.push_back({lines_, cursor_, anchor_});
        if (undo_.size() > 300) undo_.erase(undo_.begin());
        redo_.clear();
    }
    // Typing (and deleting) runs merge into one undo step until something else happens.
    void begin_edit(Kind k) {
        bool merge = k != Kind::Other && k == last_kind_;
        if (!merge) snapshot();
        last_kind_ = k == Kind::Other ? Kind::None : k;
    }
    void erase_selection() {
        if (!has_selection()) return;
        Pos a = sel_start(), b = sel_end();
        std::string tail = lines_[size_t(b.line)].substr(size_t(b.col));
        lines_[size_t(a.line)].erase(size_t(a.col));
        lines_[size_t(a.line)] += tail;
        lines_.erase(lines_.begin() + a.line + 1, lines_.begin() + b.line + 1);
        cursor_ = anchor_ = a;
    }
    bool swap_history(std::vector<State>& from, std::vector<State>& to) {
        if (from.empty()) return false;
        to.push_back({lines_, cursor_, anchor_});
        State s = std::move(from.back());
        from.pop_back();
        lines_ = std::move(s.lines);
        cursor_ = clamp(s.cursor);
        anchor_ = clamp(s.anchor);
        last_kind_ = Kind::None;
        ++revision_;
        return true;
    }
};

}  // namespace mccode
