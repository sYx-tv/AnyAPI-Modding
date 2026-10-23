#pragma once
// MCCode language core: turns the code a player types in the Code view into the microcontroller script text the
// game's own parser reads (server_scene_script.parser), and turns the game's script text back into friendly code.
//
// The game's text form is what server_scene_script.element_*.write produce (Anymaker 0.1.25):
//   define var integer rpm            on_tick            if ((var rpm > 4000))        call shift (1)
//   var rpm = (in engine_rpm * 2)     {                  elseif (...) / else          var l append (5)
//   # comment                         <tab>...           repeat (...) / foreach (...) var l insert (0, 5)
//                                     }                  ! (x)  abs (x)  rand (a, b)  var l erase (0) / var l clear
// Every binary operation is bracketed, references carry their kind (var / in / out / param) and a block's "{" and
// "}" sit on their own lines. The friendly form accepted here is a superset: normal operator precedence, bare names
// (resolved from the declarations), "and / or / not", "!=", unary minus, "f(x)" calls, "l.append(x)" list calls and
// "{" on the header line. Every canonical script is also valid friendly code.
//
// No game or Windows dependencies: tests/mccode_lang_test.cpp runs it on any platform.
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mccode {

struct Diag {
    int line = 0, col = 0, len = 1;   // zero-based line and column of the problem in the source text
    std::string msg;
};

// ------------------------------------------------------------------------------------------------ tokens
enum class Tk { End, Newline, Ident, Int, Float, Str, Comment, Sym };
struct Token {
    Tk k = Tk::End;
    std::string s;
    int line = 0, col = 0, len = 0;
};

inline bool ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
inline bool ident_char(char c) { return ident_start(c) || (c >= '0' && c <= '9'); }
inline bool digit(char c) { return c >= '0' && c <= '9'; }

inline std::vector<Token> tokenize(const std::string& src, std::vector<Diag>& diags) {
    std::vector<Token> out;
    int line = 0;
    size_t i = 0, line_start = 0;
    auto push = [&](Tk k, std::string s, size_t start, size_t end) {
        out.push_back(Token{k, std::move(s), line, int(start - line_start), int(end - start)});
    };
    while (i < src.size()) {
        char c = src[i];
        if (c == '\n') { push(Tk::Newline, "", i, i + 1); ++i; ++line; line_start = i; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { ++i; continue; }
        if (c == '#') {
            size_t j = i + 1;
            while (j < src.size() && src[j] != '\n' && src[j] != '\r') ++j;
            push(Tk::Comment, src.substr(i + 1, j - i - 1), i, j);
            i = j;
            continue;
        }
        if (c == '"') {
            size_t j = i + 1;
            while (j < src.size() && src[j] != '"' && src[j] != '\n') ++j;
            if (j >= src.size() || src[j] != '"') {
                diags.push_back({line, int(i - line_start), int(j - i), "This text has no closing \" on the same line"});
                push(Tk::Str, src.substr(i + 1, j - i - 1), i, j);
                i = j;
                continue;
            }
            push(Tk::Str, src.substr(i + 1, j - i - 1), i, j + 1);
            i = j + 1;
            continue;
        }
        if (digit(c) || (c == '.' && i + 1 < src.size() && digit(src[i + 1]))) {
            size_t j = i;
            while (j < src.size() && digit(src[j])) ++j;
            bool is_float = false;
            if (j < src.size() && src[j] == '.' && !(j + 1 < src.size() && ident_start(src[j + 1]))) {
                is_float = true;
                ++j;
                while (j < src.size() && digit(src[j])) ++j;
            }
            std::string text = src.substr(i, j - i);
            if (j < src.size() && ident_char(src[j])) {
                size_t k = j;
                while (k < src.size() && ident_char(src[k])) ++k;
                diags.push_back({line, int(i - line_start), int(k - i), "Names can't start with a digit"});
                j = k;
            }
            if (is_float) {
                if (text.front() == '.') text = "0" + text;
                if (text.back() == '.') text += "0";
            }
            push(is_float ? Tk::Float : Tk::Int, text, i, j);
            i = j;
            continue;
        }
        if (ident_start(c)) {
            size_t j = i;
            while (j < src.size() && ident_char(src[j])) ++j;
            push(Tk::Ident, src.substr(i, j - i), i, j);
            i = j;
            continue;
        }
        static const char* two[] = {"==", "!=", "<=", ">=", "&&", "||"};
        bool done = false;
        for (const char* t : two)
            if (i + 1 < src.size() && src[i] == t[0] && src[i + 1] == t[1]) { push(Tk::Sym, t, i, i + 2); i += 2; done = true; break; }
        if (done) continue;
        if (std::string("()[]{},.=<>+-*/%^!").find(c) != std::string::npos) { push(Tk::Sym, std::string(1, c), i, i + 1); ++i; continue; }
        diags.push_back({line, int(i - line_start), 1, std::string("Unexpected character '") + c + "'"});
        ++i;
    }
    out.push_back(Token{Tk::End, "", line, int(i - line_start), 0});
    return out;
}

// ------------------------------------------------------------------------------------------------ syntax tree
enum class EK { Int, Float, Str, True, False, Ref, Index, Bin, Un, Rand, Neg, Call };
enum class RK { Bare, Var, In, Out, Param };
enum class Op { Or, And, Eq, NotEq, Gt, Lt, Ge, Le, Contains, Add, Sub, Mul, Div, Mod, Pow };
enum class Fn { Not, Abs, Floor, Ceil, Round, Sqrt, Sin, Cos, Tan, Asin, Acos, Atan, Exp, Ln, Log2, Len, ToInt, ToFloat };

struct Expr;
using EP = std::unique_ptr<Expr>;
struct Expr {
    EK k = EK::Int;
    std::string text, member;   // literal text or name; member after "."
    RK ref = RK::Bare;          // for Ref: as written; resolved later into `kind`
    RK kind = RK::Bare;
    Op op = Op::Add;
    Fn fn = Fn::Not;
    EP a, b;
    std::vector<EP> args;       // Call
    int line = 0, col = 0, len = 1;
};

enum class SK { Blank, Comment, OnTick, OnRender, DefType, DefMember, DefFunc, DefVar, DefList, DefIn, DefOut,
                If, ElseIf, Else, Repeat, Foreach, Call, Set, Append, Insert, Erase, Clear };
struct Param { std::string type, name; };
struct Stmt {
    SK k = SK::Blank;
    int line = 0, col = 0;
    std::string type, name, text;
    std::vector<Param> params;
    std::vector<EP> args;   // Call arguments; Insert: index, value; Append / Erase: one
    EP a, b;                // condition / repeat count / list / set target (a) and value (b)
    std::vector<Stmt> body;
    int canon_line = -1;    // line of this statement in the canonical text
    int out_line = -1;      // line of this statement in prettified text
};

inline bool needs_body(SK k) {
    return k == SK::OnTick || k == SK::OnRender || k == SK::DefType || k == SK::DefFunc || k == SK::If || k == SK::ElseIf ||
           k == SK::Else || k == SK::Repeat || k == SK::Foreach;
}

struct FnInfo { const char* name; Fn fn; };
inline const FnInfo kFunctions[] = {
    {"abs", Fn::Abs}, {"floor", Fn::Floor}, {"ceil", Fn::Ceil}, {"round", Fn::Round}, {"sqrt", Fn::Sqrt}, {"sin", Fn::Sin},
    {"cos", Fn::Cos}, {"tan", Fn::Tan}, {"asin", Fn::Asin}, {"acos", Fn::Acos}, {"atan", Fn::Atan}, {"exp", Fn::Exp},
    {"ln", Fn::Ln}, {"log2", Fn::Log2}, {"len", Fn::Len}, {"to_integer", Fn::ToInt}, {"to_fractional", Fn::ToFloat}};
inline const char* fn_name(Fn f) {
    if (f == Fn::Not) return "!";
    for (auto& i : kFunctions) if (i.fn == f) return i.name;
    return "?";
}
inline bool find_fn(const std::string& s, Fn& f) {
    for (auto& i : kFunctions) if (s == i.name) { f = i.fn; return true; }
    return false;
}
inline const char* op_text(Op o) {
    switch (o) {
    case Op::Or: return "||"; case Op::And: return "&&"; case Op::Eq: return "=="; case Op::NotEq: return "!=";
    case Op::Gt: return ">"; case Op::Lt: return "<"; case Op::Ge: return ">="; case Op::Le: return "<=";
    case Op::Contains: return "contains"; case Op::Add: return "+"; case Op::Sub: return "-"; case Op::Mul: return "*";
    case Op::Div: return "/"; case Op::Mod: return "%"; case Op::Pow: return "^";
    }
    return "?";
}
inline int op_prec(Op o) {
    switch (o) {
    case Op::Or: return 1; case Op::And: return 2; case Op::Eq: case Op::NotEq: return 3;
    case Op::Gt: case Op::Lt: case Op::Ge: case Op::Le: case Op::Contains: return 4;
    case Op::Add: case Op::Sub: return 5; case Op::Mul: case Op::Div: case Op::Mod: return 6; case Op::Pow: return 7;
    }
    return 0;
}

inline const std::set<std::string>& keywords() {
    static const std::set<std::string> k = {
        "define", "type", "member", "function", "var", "list", "in", "out", "param", "call", "if", "elseif", "else",
        "repeat", "foreach", "on_tick", "on_render", "append", "insert", "erase", "clear", "contains", "true", "false",
        "and", "or", "not", "bool", "integer", "fractional", "string", "analogue", "rand", "abs", "floor", "ceil", "round",
        "sqrt", "sin", "cos", "tan", "asin", "acos", "atan", "exp", "ln", "log2", "len", "to_integer", "to_fractional"};
    return k;
}
inline bool builtin_type(const std::string& s) {
    return s == "bool" || s == "integer" || s == "fractional" || s == "string" || s == "analogue";
}
inline bool list_op(const std::string& s) { return s == "append" || s == "insert" || s == "erase" || s == "clear"; }

// ------------------------------------------------------------------------------------------------ parser
class Parser {
public:
    Parser(const std::vector<Token>& t, std::vector<Diag>& d) : t_(t), d_(d) {}

    std::vector<Stmt> program() {
        std::vector<Stmt> items = block_items(0);
        while (cur().k != Tk::End) {   // stray "}"
            error(cur(), "This } has no matching {");
            next();
            auto more = block_items(0);
            for (auto& s : more) items.push_back(std::move(s));
        }
        while (!items.empty() && items.back().k == SK::Blank) items.pop_back();
        return items;
    }

private:
    const std::vector<Token>& t_;
    std::vector<Diag>& d_;
    size_t p_ = 0;
    int errors_on_line_ = -1;

    const Token& cur() const { return t_[p_]; }
    const Token& peek(int n = 1) const { return t_[std::min(p_ + size_t(n), t_.size() - 1)]; }
    void next() { if (p_ + 1 < t_.size()) ++p_; }
    bool is_sym(const char* s) const { return cur().k == Tk::Sym && cur().s == s; }
    bool is_ident(const char* s) const { return cur().k == Tk::Ident && cur().s == s; }
    bool at_line_end() const { return cur().k == Tk::Newline || cur().k == Tk::End || cur().k == Tk::Comment || is_sym("}"); }

    void error(const Token& at, const std::string& msg) {
        if (errors_on_line_ == at.line) return;   // one message per line keeps follow-on noise out
        errors_on_line_ = at.line;
        d_.push_back({at.line, at.col, std::max(1, at.len), msg});
    }
    void skip_line() {
        int depth = 0;
        while (cur().k != Tk::End && cur().k != Tk::Newline) {
            if (is_sym("{")) ++depth;
            if (is_sym("}")) { if (depth == 0) return; --depth; }
            next();
        }
    }
    bool expect_sym(const char* s, const char* what) {
        if (is_sym(s)) { next(); return true; }
        error(cur(), std::string("Expected ") + what);
        return false;
    }
    std::string expect_name(const char* what) {
        if (cur().k != Tk::Ident) { error(cur(), std::string("Expected ") + what); return ""; }
        std::string s = cur().s;
        if (keywords().count(s)) { error(cur(), "'" + s + "' is a keyword and can't be used as a name"); }
        next();
        return s;
    }
    std::string expect_type() {
        if (cur().k != Tk::Ident) { error(cur(), "Expected a type: bool, integer, fractional, string, analogue or a defined type"); return ""; }
        std::string s = cur().s;
        next();
        return s;
    }

    std::vector<Stmt> block_items(int depth) {
        std::vector<Stmt> items;
        while (true) {
            const Token& t = cur();
            if (t.k == Tk::End || (t.k == Tk::Sym && t.s == "}")) return items;
            if (t.k == Tk::Newline) { Stmt s; s.k = SK::Blank; s.line = t.line; items.push_back(std::move(s)); next(); continue; }
            if (t.k == Tk::Comment) {
                Stmt s; s.k = SK::Comment; s.line = t.line; s.col = t.col; s.text = t.s;
                items.push_back(std::move(s));
                next();
                if (cur().k == Tk::Newline) next();
                continue;
            }
            statement(items, depth);
        }
    }

    // Ends a statement: an optional trailing comment (moved above the statement), then a line break.
    void end_statement(std::vector<Stmt>& items, size_t index) {
        if (cur().k == Tk::Comment) {
            Stmt c; c.k = SK::Comment; c.line = cur().line; c.col = cur().col; c.text = cur().s;
            items.insert(items.begin() + long(index), std::move(c));
            next();
        }
        if (cur().k == Tk::Newline) { next(); return; }
        if (cur().k == Tk::End || is_sym("}")) return;
        error(cur(), "Expected the end of the line here");
        skip_line();
        if (cur().k == Tk::Newline) next();
    }

    void statement(std::vector<Stmt>& items, int depth) {
        Stmt s;
        s.line = cur().line;
        s.col = cur().col;
        size_t start = p_;
        bool ok = header_or_simple(s);
        if (!ok) {
            if (p_ == start) next();
            skip_line();
            if (cur().k == Tk::Newline) next();
            return;
        }
        size_t index = items.size();
        if (!needs_body(s.k)) {
            items.push_back(std::move(s));
            end_statement(items, index);
            return;
        }
        // block: "{" on this line or alone on a following line
        Token header_end = cur();
        Stmt pre_comment;
        bool has_comment = false;
        if (cur().k == Tk::Comment) {
            pre_comment.k = SK::Comment; pre_comment.line = cur().line; pre_comment.col = cur().col; pre_comment.text = cur().s;
            has_comment = true;
            next();
        }
        while (cur().k == Tk::Newline) next();
        if (!is_sym("{")) {
            error(header_end, "Expected { to start this block");
            if (has_comment) items.push_back(std::move(pre_comment));
            items.push_back(std::move(s));
            return;
        }
        Token open = cur();
        next();
        if (cur().k == Tk::Newline) next();
        s.body = block_items(depth + 1);
        if (!is_sym("}")) {
            Diag d{open.line, open.col, 1, "This { is never closed with }"};
            d_.push_back(d);
        } else {
            next();
        }
        if (has_comment) items.push_back(std::move(pre_comment));
        index = items.size();
        items.push_back(std::move(s));
        if (is_ident("else") || is_ident("elseif")) return;   // "} else {" chains on the same line
        end_statement(items, index);
    }

    bool header_or_simple(Stmt& s) {
        const Token& t = cur();
        if (t.k == Tk::Ident) {
            const std::string& w = t.s;
            if (w == "on_render") { error(t, "on_render isn't supported: the game knows the word but never runs it and its Load rejects it. Use on_tick"); return false; }
            if (w == "on_tick") { s.k = SK::OnTick; next(); return true; }
            if (w == "define") return define(s);
            if (w == "if" || w == "elseif" || w == "repeat" || w == "foreach") {
                s.k = w == "if" ? SK::If : w == "elseif" ? SK::ElseIf : w == "repeat" ? SK::Repeat : SK::Foreach;
                next();
                s.a = expr();
                return s.a != nullptr;
            }
            if (w == "else") {
                next();
                if (is_ident("if")) { next(); s.k = SK::ElseIf; s.a = expr(); return s.a != nullptr; }
                s.k = SK::Else;
                return true;
            }
            if (w == "call") {
                next();
                s.k = SK::Call;
                s.name = expect_name("a function name after call");
                if (s.name.empty()) return false;
                return call_args(s.args);
            }
        }
        if (t.k == Tk::Sym && t.s == "{") { error(t, "A { needs something in front of it, like on_tick, if or repeat"); return false; }
        // expression statement: assignment, list operation or function call
        EP lhs = postfix();
        if (!lhs) return false;
        if (lhs->k == EK::Call) {
            s.k = SK::Call;
            s.name = lhs->text;
            s.args = std::move(lhs->args);
            return true;
        }
        if (is_sym("=")) {
            next();
            s.k = SK::Set;
            s.a = std::move(lhs);
            s.b = expr();
            return s.b != nullptr;
        }
        bool dotted = false;
        if (is_sym(".") && peek().k == Tk::Ident && list_op(peek().s)) { next(); dotted = true; }
        if (cur().k == Tk::Ident && list_op(cur().s)) {
            std::string w = cur().s;
            next();
            s.a = std::move(lhs);
            if (w == "clear") {
                s.k = SK::Clear;
                if (is_sym("(")) { next(); if (!expect_sym(")", ") after clear (")) return false; }
                return true;
            }
            s.k = w == "append" ? SK::Append : w == "insert" ? SK::Insert : SK::Erase;
            if (!call_args(s.args)) return false;
            size_t want = s.k == SK::Insert ? 2 : 1;
            if (s.args.size() != want) {
                error(t, w + (want == 2 ? " takes two values: (index, value)" : " takes one value in brackets"));
                return false;
            }
            return true;
        }
        (void)dotted;
        if (at_line_end()) error(t, "This line doesn't do anything. Did you mean to set it, like x = 1?");
        else error(cur(), "Expected = or the end of the line here");
        return false;
    }

    bool define(Stmt& s) {
        next();   // define
        if (cur().k != Tk::Ident) { error(cur(), "Expected what to define: var, list, in, out, function, type or member"); return false; }
        std::string w = cur().s;
        next();
        if (w == "type") { s.k = SK::DefType; s.name = expect_name("a type name"); return !s.name.empty(); }
        if (w == "function") {
            s.k = SK::DefFunc;
            s.name = expect_name("a function name");
            if (s.name.empty()) return false;
            if (!expect_sym("(", "( after the function name")) return false;
            while (!is_sym(")")) {
                Param p;
                p.type = expect_type();
                p.name = expect_name("a parameter name");
                if (p.type.empty() || p.name.empty()) return false;
                s.params.push_back(p);
                if (is_sym(",")) { next(); continue; }
                if (!is_sym(")")) { error(cur(), "Expected , or ) in the parameter list"); return false; }
            }
            next();
            return true;
        }
        SK k;
        if (w == "var") k = SK::DefVar;
        else if (w == "list") k = SK::DefList;
        else if (w == "in") k = SK::DefIn;
        else if (w == "out") k = SK::DefOut;
        else if (w == "member") k = SK::DefMember;
        else { error(t_[p_ - 1], "Can't define '" + w + "'. Use var, list, in, out, function, type or member"); return false; }
        s.k = k;
        s.type = expect_type();
        s.name = expect_name("a name");
        return !s.type.empty() && !s.name.empty();
    }

    bool call_args(std::vector<EP>& args) {
        if (!expect_sym("(", "(")) return false;
        while (!is_sym(")")) {
            EP e = expr();
            if (!e) return false;
            args.push_back(std::move(e));
            if (is_sym(",")) { next(); continue; }
            if (!is_sym(")")) { error(cur(), "Expected , or )"); return false; }
        }
        next();
        return true;
    }

    EP make(EK k, const Token& t) {
        EP e = std::make_unique<Expr>();
        e->k = k;
        e->line = t.line;
        e->col = t.col;
        e->len = std::max(1, t.len);
        return e;
    }

    EP expr(int min_prec = 1) {
        EP lhs = unary();
        if (!lhs) return nullptr;
        while (true) {
            Op op;
            const Token& t = cur();
            if (!binary_op(t, op) || op_prec(op) < min_prec) return lhs;
            next();
            int p = op_prec(op);
            EP rhs = expr(op == Op::Pow ? p : p + 1);
            if (!rhs) return nullptr;
            EP b = make(EK::Bin, t);
            b->op = op;
            b->a = std::move(lhs);
            b->b = std::move(rhs);
            lhs = std::move(b);
        }
    }
    bool binary_op(const Token& t, Op& op) const {
        if (t.k == Tk::Ident) {
            if (t.s == "or") { op = Op::Or; return true; }
            if (t.s == "and") { op = Op::And; return true; }
            if (t.s == "contains") { op = Op::Contains; return true; }
            return false;
        }
        if (t.k != Tk::Sym) return false;
        static const std::pair<const char*, Op> ops[] = {
            {"||", Op::Or}, {"&&", Op::And}, {"==", Op::Eq}, {"!=", Op::NotEq}, {">", Op::Gt}, {"<", Op::Lt}, {">=", Op::Ge},
            {"<=", Op::Le}, {"+", Op::Add}, {"-", Op::Sub}, {"*", Op::Mul}, {"/", Op::Div}, {"%", Op::Mod}, {"^", Op::Pow}};
        for (auto& o : ops) if (t.s == o.first) { op = o.second; return true; }
        return false;
    }
    EP unary() {
        const Token& t = cur();
        if ((t.k == Tk::Sym && t.s == "!") || (t.k == Tk::Ident && t.s == "not")) {
            next();
            EP a = unary();
            if (!a) return nullptr;
            EP e = make(EK::Un, t);
            e->fn = Fn::Not;
            e->a = std::move(a);
            return e;
        }
        if (t.k == Tk::Sym && t.s == "-") {
            next();
            if ((cur().k == Tk::Int || cur().k == Tk::Float) && !(peek().k == Tk::Sym && peek().s == "^")) {
                EP e = make(cur().k == Tk::Int ? EK::Int : EK::Float, t);
                e->text = "-" + cur().s;
                next();
                return postfix_rest(std::move(e));
            }
            EP a = unary();
            if (!a) return nullptr;
            EP e = make(EK::Neg, t);
            e->a = std::move(a);
            return e;
        }
        return postfix();
    }
    EP postfix() {
        EP e = primary();
        if (!e) return nullptr;
        return postfix_rest(std::move(e));
    }
    EP postfix_rest(EP e) {
        while (true) {
            if (is_sym("[")) {
                const Token& t = cur();
                next();
                EP i = expr();
                if (!i || !expect_sym("]", "]")) return nullptr;
                EP x = make(EK::Index, t);
                x->a = std::move(e);
                x->b = std::move(i);
                e = std::move(x);
                continue;
            }
            if (is_sym(".") && peek().k == Tk::Ident && !list_op(peek().s)) {
                if (e->k != EK::Ref || !e->member.empty()) { error(cur(), "Only a variable, parameter or input can have a .member"); return nullptr; }
                next();
                e->member = cur().s;
                next();
                continue;
            }
            return e;
        }
    }
    EP primary() {
        const Token& t = cur();
        switch (t.k) {
        case Tk::Int: case Tk::Float: { EP e = make(t.k == Tk::Int ? EK::Int : EK::Float, t); e->text = t.s; next(); return e; }
        case Tk::Str: { EP e = make(EK::Str, t); e->text = t.s; next(); return e; }
        case Tk::Sym:
            if (t.s == "(") {
                next();
                EP e = expr();
                if (!e || !expect_sym(")", ")")) return nullptr;
                return e;
            }
            error(t, "Expected a value here");
            return nullptr;
        case Tk::Ident: break;
        default: error(t, "Expected a value here"); return nullptr;
        }
        const std::string& w = t.s;
        if (w == "true" || w == "false") { EP e = make(w == "true" ? EK::True : EK::False, t); next(); return e; }
        if (w == "var" || w == "in" || w == "out" || w == "param" || w == "list") {
            next();
            if (cur().k != Tk::Ident) { error(cur(), "Expected a name after " + w); return nullptr; }
            EP e = make(EK::Ref, t);
            e->ref = w == "in" ? RK::In : w == "out" ? RK::Out : w == "param" ? RK::Param : RK::Var;
            e->text = cur().s;
            e->len = cur().col + cur().len - t.col;
            next();
            return e;
        }
        if (w == "rand") {
            next();
            std::vector<EP> args;
            if (!call_args(args)) return nullptr;
            if (args.size() != 2) { error(t, "rand takes two values: rand(lowest, highest)"); return nullptr; }
            EP e = make(EK::Rand, t);
            e->a = std::move(args[0]);
            e->b = std::move(args[1]);
            return e;
        }
        Fn f;
        if (find_fn(w, f)) {
            next();
            std::vector<EP> args;
            if (!call_args(args)) return nullptr;
            if (args.size() != 1) { error(t, w + " takes one value in brackets"); return nullptr; }
            EP e = make(EK::Un, t);
            e->fn = f;
            e->a = std::move(args[0]);
            return e;
        }
        if (keywords().count(w)) { error(t, "'" + w + "' can't be used here"); return nullptr; }
        next();
        if (is_sym("(")) {
            EP e = make(EK::Call, t);
            e->text = w;
            if (!call_args(e->args)) return nullptr;
            return e;
        }
        EP e = make(EK::Ref, t);
        e->ref = RK::Bare;
        e->text = w;
        return e;
    }
};

// ------------------------------------------------------------------------------------------------ names
enum class VT { Unknown, Bool, Int, Float, Str };
inline VT value_type(const std::string& t) {
    if (t == "bool") return VT::Bool;
    if (t == "integer") return VT::Int;
    if (t == "fractional") return VT::Float;
    if (t == "string") return VT::Str;
    return VT::Unknown;
}

struct Symbols {
    std::map<std::string, std::string> vars, ins, outs;   // name -> type (lists are vars)
    std::set<std::string> lists, types, functions;
    std::map<std::string, std::vector<Param>> function_params;
    std::map<std::string, std::string> params;            // current function's parameters
};

inline void collect(const std::vector<Stmt>& items, Symbols& sy) {
    for (auto& s : items) {
        switch (s.k) {
        case SK::DefVar: sy.vars[s.name] = s.type; break;
        case SK::DefList: sy.vars[s.name] = s.type; sy.lists.insert(s.name); break;
        case SK::DefIn: sy.ins[s.name] = s.type; break;
        case SK::DefOut: sy.outs[s.name] = s.type; break;
        case SK::DefType: sy.types.insert(s.name); break;
        case SK::DefFunc: sy.functions.insert(s.name); sy.function_params[s.name] = s.params; break;
        default: break;
        }
        collect(s.body, sy);
    }
}

class Resolver {
public:
    Resolver(Symbols& sy, std::vector<Diag>& d) : sy_(sy), d_(d) {}

    void items(std::vector<Stmt>& list) {
        for (auto& s : list) stmt(s);
    }

private:
    Symbols& sy_;
    std::vector<Diag>& d_;
    bool target_ = false;   // resolving the left side of "x = ..."

    void error(int line, int col, int len, const std::string& msg) { d_.push_back({line, col, std::max(1, len), msg}); }

    void check_type(const Stmt& s, const std::string& t) {
        if (!t.empty() && !builtin_type(t) && !sy_.types.count(t))
            error(s.line, s.col, 6, "Unknown type '" + t + "'. Use bool, integer, fractional, string, analogue or a defined type");
    }

    void stmt(Stmt& s) {
        switch (s.k) {
        case SK::DefVar: case SK::DefList: case SK::DefIn: case SK::DefOut: case SK::DefMember: check_type(s, s.type); break;
        case SK::DefFunc: {
            for (auto& p : s.params) check_type(s, p.type);
            auto saved = sy_.params;
            sy_.params.clear();
            for (auto& p : s.params) sy_.params[p.name] = p.type;
            items(s.body);
            sy_.params = saved;
            return;
        }
        case SK::Call:
            if (!sy_.functions.count(s.name)) error(s.line, s.col, int(s.name.size()) + 5, "There's no function called '" + s.name + "' (define it with define function)");
            for (auto& a : s.args) expr(*a);
            break;
        case SK::Set:
            target_ = true; expr(*s.a); target_ = false;
            expr(*s.b);
            break;
        case SK::Append: case SK::Insert: case SK::Erase: case SK::Clear:
            expr(*s.a);
            for (auto& a : s.args) expr(*a);
            break;
        default:
            if (s.a) expr(*s.a);
            if (s.b) expr(*s.b);
            break;
        }
        items(s.body);
    }

    void expr(Expr& e) {
        bool target = target_;
        if (e.a) expr(*e.a);   // for a list item on the left of "=", a is the list being written
        target_ = false;       // its index, and everything else inside, is read
        if (e.b) expr(*e.b);
        for (auto& a : e.args) expr(*a);
        target_ = target;
        if (e.k == EK::Call) { error(e.line, e.col, e.len, "A function call can't be used as a value here; call it on its own line"); return; }
        if (e.k != EK::Ref) return;
        auto has = [&](RK k) {
            switch (k) {
            case RK::Var: return sy_.vars.count(e.text) > 0;
            case RK::In: return sy_.ins.count(e.text) > 0;
            case RK::Out: return sy_.outs.count(e.text) > 0;
            case RK::Param: return sy_.params.count(e.text) > 0;
            default: return false;
            }
        };
        // A part wired to the microcontroller (a wheel, an engine) is a port the game provides without a define:
        // RearLeft.rps reads one of its channels, RearLeft.brake_factor = 1 writes one.
        if (!e.member.empty() && (e.ref == RK::In || e.ref == RK::Out)) { e.kind = e.ref; return; }
        if (!e.member.empty() && e.ref == RK::Bare && !has(RK::Var) && !has(RK::Param)) {
            e.kind = target_ ? RK::Out : RK::In;
            return;
        }
        if (e.ref != RK::Bare) {
            if (!has(e.ref)) {
                const char* what = e.ref == RK::Var ? "variable" : e.ref == RK::In ? "input" : e.ref == RK::Out ? "output" : "parameter";
                error(e.line, e.col, e.len, std::string("There's no ") + what + " called '" + e.text + "'");
            }
            e.kind = e.ref;
            return;
        }
        if (has(RK::Param)) { e.kind = RK::Param; return; }
        std::vector<RK> found;
        for (RK k : {RK::Var, RK::In, RK::Out}) if (has(k)) found.push_back(k);
        if (found.size() == 1) { e.kind = found[0]; return; }
        if (found.empty()) { error(e.line, e.col, e.len, "Unknown name '" + e.text + "'. Define it first, like: define var integer " + e.text); return; }
        error(e.line, e.col, e.len, "'" + e.text + "' is more than one thing (variable, input or output). Write var " + e.text + ", in " + e.text + " or out " + e.text);
        e.kind = found[0];
    }
};

inline VT type_of(const Expr& e, const Symbols& sy) {
    switch (e.k) {
    case EK::Int: return VT::Int;
    case EK::Float: return VT::Float;
    case EK::Str: return VT::Str;
    case EK::True: case EK::False: return VT::Bool;
    case EK::Ref: {
        if (!e.member.empty()) return VT::Unknown;
        const std::map<std::string, std::string>* m = e.kind == RK::Var ? &sy.vars : e.kind == RK::In ? &sy.ins : e.kind == RK::Out ? &sy.outs : e.kind == RK::Param ? &sy.params : nullptr;
        if (!m) return VT::Unknown;
        auto it = m->find(e.text);
        if (it == m->end() || (e.kind == RK::Var && sy.lists.count(e.text))) return VT::Unknown;
        return value_type(it->second);
    }
    case EK::Bin:
        switch (e.op) {
        case Op::Or: case Op::And: case Op::Eq: case Op::NotEq: case Op::Gt: case Op::Lt: case Op::Ge: case Op::Le: case Op::Contains: return VT::Bool;
        default: { VT a = type_of(*e.a, sy), b = type_of(*e.b, sy); return a == b ? a : VT::Unknown; }
        }
    case EK::Un:
        switch (e.fn) {
        case Fn::Not: return VT::Bool;
        case Fn::Len: case Fn::ToInt: return VT::Int;
        case Fn::ToFloat: case Fn::Sqrt: case Fn::Sin: case Fn::Cos: case Fn::Tan: case Fn::Asin: case Fn::Acos: case Fn::Atan:
        case Fn::Exp: case Fn::Ln: case Fn::Log2: return VT::Float;
        default: return type_of(*e.a, sy);
        }
    case EK::Neg: return type_of(*e.a, sy);
    case EK::Rand: return type_of(*e.a, sy);
    default: return VT::Unknown;
    }
}

// ------------------------------------------------------------------------------------------------ game text
class CanonWriter {
public:
    CanonWriter(const Symbols& sy, std::vector<Diag>& d) : sy_(sy), d_(d) {}
    std::vector<std::string> lines;

    void items(std::vector<Stmt>& list, int depth) {
        for (auto& s : list) stmt(s, depth);
    }

private:
    const Symbols& sy_;
    std::vector<Diag>& d_;
    std::map<std::string, std::string> params_;

    std::string ref_prefix(RK k) {
        switch (k) { case RK::In: return "in "; case RK::Out: return "out "; case RK::Param: return "param "; default: return "var "; }
    }
    std::string literal(const Expr& e) {
        if (e.text.empty() || e.text[0] != '-') return e.text;
        return std::string(e.k == EK::Int ? "(0 - " : "(0.0 - ") + e.text.substr(1) + ")";
    }
    std::string ex(const Expr& e) {
        switch (e.k) {
        case EK::Int: case EK::Float: return literal(e);
        case EK::Str: return "\"" + e.text + "\"";
        case EK::True: return "true";
        case EK::False: return "false";
        case EK::Ref: return ref_prefix(e.kind) + e.text + (e.member.empty() ? "" : "." + e.member);
        case EK::Index: return ex(*e.a) + "[" + ex(*e.b) + "]";
        case EK::Bin:
            if (e.op == Op::NotEq) return "! ((" + ex(*e.a) + " == " + ex(*e.b) + "))";
            return "(" + ex(*e.a) + " " + op_text(e.op) + " " + ex(*e.b) + ")";
        case EK::Un:
            if (e.fn == Fn::Not) return "! (" + ex(*e.a) + ")";
            return std::string(fn_name(e.fn)) + " (" + ex(*e.a) + ")";
        case EK::Rand: return "rand (" + ex(*e.a) + ", " + ex(*e.b) + ")";
        case EK::Neg: {
            Symbols scoped = sy_;
            scoped.params = params_;
            VT t = type_of(*e.a, scoped);
            if (t == VT::Int) return "(0 - " + ex(*e.a) + ")";
            if (t == VT::Float) return "(0.0 - " + ex(*e.a) + ")";
            d_.push_back({e.line, e.col, e.len, "Can't tell if this is an integer or fractional value; write (0 - x) or (0.0 - x) instead of -x"});
            return "(0 - " + ex(*e.a) + ")";
        }
        case EK::Call: return e.text + " ()";
        }
        return "";
    }
    std::string args(const std::vector<EP>& a) {
        std::string s;
        for (size_t i = 0; i < a.size(); ++i) s += (i ? ", " : "") + ex(*a[i]);
        return s;
    }

    void stmt(Stmt& s, int depth) {
        std::string ind(size_t(depth), '\t'), line;
        switch (s.k) {
        case SK::Blank: line = ""; break;
        case SK::Comment: line = "#" + s.text; break;
        case SK::OnTick: line = "on_tick"; break;
        case SK::OnRender: line = "on_render"; break;
        case SK::DefType: line = "define type " + s.name; break;
        case SK::DefMember: line = "define member " + s.type + " " + s.name; break;
        case SK::DefFunc: {
            line = "define function " + s.name + " (";
            for (size_t i = 0; i < s.params.size(); ++i) line += (i ? ", " : "") + s.params[i].type + " " + s.params[i].name;
            line += ")";
            break;
        }
        case SK::DefVar: line = "define var " + s.type + " " + s.name; break;
        case SK::DefList: line = "define list " + s.type + " " + s.name; break;
        case SK::DefIn: line = "define in " + s.type + " " + s.name; break;
        case SK::DefOut: line = "define out " + s.type + " " + s.name; break;
        case SK::If: line = "if (" + ex(*s.a) + ")"; break;
        case SK::ElseIf: line = "elseif (" + ex(*s.a) + ")"; break;
        case SK::Else: line = "else"; break;
        case SK::Repeat: line = "repeat (" + ex(*s.a) + ")"; break;
        case SK::Foreach: line = "foreach (" + ex(*s.a) + ")"; break;
        case SK::Call: line = "call " + s.name + " (" + args(s.args) + ")"; break;
        case SK::Set: line = ex(*s.a) + " = " + ex(*s.b); break;
        case SK::Append: line = ex(*s.a) + " append (" + args(s.args) + ")"; break;
        case SK::Insert: line = ex(*s.a) + " insert (" + args(s.args) + ")"; break;
        case SK::Erase: line = ex(*s.a) + " erase (" + args(s.args) + ")"; break;
        case SK::Clear: line = ex(*s.a) + " clear"; break;
        }
        s.canon_line = int(lines.size());
        lines.push_back(ind + line);
        if (!needs_body(s.k)) return;
        auto saved = params_;
        if (s.k == SK::DefFunc) { params_.clear(); for (auto& p : s.params) params_[p.name] = p.type; }
        lines.push_back(ind + "{");
        items(s.body, depth + 1);
        lines.push_back(ind + "}");
        params_ = saved;
    }
};

// ------------------------------------------------------------------------------------------------ friendly text
class PrettyWriter {
public:
    explicit PrettyWriter(const Symbols& sy) : sy_(sy) {}
    std::vector<std::string> lines;

    void items(std::vector<Stmt>& list, int depth) {
        for (size_t i = 0; i < list.size(); ++i) {
            Stmt& s = list[i];
            bool chained = i + 1 < list.size() && needs_body(s.k) && (s.k == SK::If || s.k == SK::ElseIf) &&
                           (list[i + 1].k == SK::ElseIf || list[i + 1].k == SK::Else);
            stmt(s, depth, chained);
        }
    }

private:
    const Symbols& sy_;
    std::map<std::string, std::string> params_;
    bool pending_close_ = false;   // the previous block's "}" waits for "elseif" / "else" on the same line

    bool bare_ok(const Expr& e) const {
        if (e.kind == RK::Param) return true;
        if (params_.count(e.text)) return false;   // a parameter would shadow it
        int n = int(sy_.vars.count(e.text)) + int(sy_.ins.count(e.text)) + int(sy_.outs.count(e.text));
        return n == 1;
    }
    static int prec(const Expr& e) {
        switch (e.k) {
        case EK::Bin: return op_prec(e.op);
        case EK::Un: return e.fn == Fn::Not && !(e.a->k == EK::Bin && e.a->op == Op::Eq) ? 8 : (e.fn == Fn::Not ? 3 : 9);
        case EK::Neg: return 8;
        case EK::Int: case EK::Float: return e.text.size() && e.text[0] == '-' ? 8 : 9;
        default: return 9;
        }
    }
    std::string wrap(const Expr& e, bool paren) { return paren ? "(" + ex(e) + ")" : ex(e); }
    std::string ex(const Expr& e) {
        switch (e.k) {
        case EK::Int: case EK::Float: return e.text;
        case EK::Str: return "\"" + e.text + "\"";
        case EK::True: return "true";
        case EK::False: return "false";
        case EK::Ref: {
            std::string p;
            if (!bare_ok(e)) p = e.kind == RK::In ? "in " : e.kind == RK::Out ? "out " : e.kind == RK::Param ? "param " : "var ";
            return p + e.text + (e.member.empty() ? "" : "." + e.member);
        }
        case EK::Index: return wrap(*e.a, prec(*e.a) < 9) + "[" + ex(*e.b) + "]";
        case EK::Bin: {
            int p = op_prec(e.op);
            bool right = e.op == Op::Pow;
            const char* o = e.op == Op::Or ? "or" : e.op == Op::And ? "and" : op_text(e.op);
            return wrap(*e.a, prec(*e.a) < p || (right && prec(*e.a) == p)) + " " + o + " " +
                   wrap(*e.b, prec(*e.b) < p || (!right && prec(*e.b) == p));
        }
        case EK::Un:
            if (e.fn == Fn::Not) {
                if (e.a->k == EK::Bin && e.a->op == Op::Eq)
                    return wrap(*e.a->a, prec(*e.a->a) <= 3) + " != " + wrap(*e.a->b, prec(*e.a->b) <= 3);
                return "not " + wrap(*e.a, prec(*e.a) < 8);
            }
            return std::string(fn_name(e.fn)) + "(" + ex(*e.a) + ")";
        case EK::Rand: return "rand(" + ex(*e.a) + ", " + ex(*e.b) + ")";
        case EK::Neg: return "-" + wrap(*e.a, prec(*e.a) < 9);
        case EK::Call: return e.text + "()";
        }
        return "";
    }
    std::string args(const std::vector<EP>& a) {
        std::string s;
        for (size_t i = 0; i < a.size(); ++i) s += (i ? ", " : "") + ex(*a[i]);
        return s;
    }

    void stmt(Stmt& s, int depth, bool chained) {
        std::string ind(size_t(depth), '\t'), line;
        switch (s.k) {
        case SK::Blank: line = ""; break;
        case SK::Comment: line = "#" + s.text; break;
        case SK::OnTick: line = "on_tick"; break;
        case SK::OnRender: line = "on_render"; break;
        case SK::DefType: line = "define type " + s.name; break;
        case SK::DefMember: line = "define member " + s.type + " " + s.name; break;
        case SK::DefFunc: {
            line = "define function " + s.name + "(";
            for (size_t i = 0; i < s.params.size(); ++i) line += (i ? ", " : "") + s.params[i].type + " " + s.params[i].name;
            line += ")";
            break;
        }
        case SK::DefVar: line = "define var " + s.type + " " + s.name; break;
        case SK::DefList: line = "define list " + s.type + " " + s.name; break;
        case SK::DefIn: line = "define in " + s.type + " " + s.name; break;
        case SK::DefOut: line = "define out " + s.type + " " + s.name; break;
        case SK::If: line = "if " + ex(*s.a); break;
        case SK::ElseIf: line = "elseif " + ex(*s.a); break;
        case SK::Else: line = "else"; break;
        case SK::Repeat: line = "repeat " + ex(*s.a); break;
        case SK::Foreach: line = "foreach " + ex(*s.a); break;
        case SK::Call: line = s.name + "(" + args(s.args) + ")"; break;
        case SK::Set: line = ex(*s.a) + " = " + ex(*s.b); break;
        case SK::Append: line = ex(*s.a) + ".append(" + args(s.args) + ")"; break;
        case SK::Insert: line = ex(*s.a) + ".insert(" + args(s.args) + ")"; break;
        case SK::Erase: line = ex(*s.a) + ".erase(" + args(s.args) + ")"; break;
        case SK::Clear: line = ex(*s.a) + ".clear()"; break;
        }
        if (pending_close_ && (s.k == SK::ElseIf || s.k == SK::Else)) {
            lines.back() += " " + line;
        } else {
            pending_close_ = false;
            lines.push_back(ind + line);
        }
        pending_close_ = false;
        s.out_line = int(lines.size()) - 1;
        if (!needs_body(s.k)) return;
        lines.back() += " {";
        auto saved = params_;
        if (s.k == SK::DefFunc) { params_.clear(); for (auto& p : s.params) params_[p.name] = p.type; }
        items(s.body, depth + 1);
        params_ = saved;
        lines.push_back(ind + "}");
        pending_close_ = chained;
    }
};

// ------------------------------------------------------------------------------------------------ entry points
struct Compiled {
    bool ok = false;
    std::vector<Diag> diags;
    std::string canonical;            // text for client_peer.data.push_event_microcontroller_load
    std::vector<int> canon_to_src;    // per canonical line: the source line of its statement, or -1 for { } lines
};

inline void map_lines(const std::vector<Stmt>& items, std::vector<int>& out, bool use_out_line) {
    for (auto& s : items) {
        if (s.canon_line >= 0 && s.canon_line < int(out.size())) out[size_t(s.canon_line)] = use_out_line ? s.out_line : s.line;
        map_lines(s.body, out, use_out_line);
    }
}

inline Compiled compile(const std::string& src) {
    Compiled c;
    auto tokens = tokenize(src, c.diags);
    Parser parser(tokens, c.diags);
    auto prog = parser.program();
    Symbols sy;
    collect(prog, sy);
    Resolver(sy, c.diags).items(prog);
    CanonWriter w(sy, c.diags);
    w.items(prog, 0);
    for (auto& l : w.lines) c.canonical += l + "\n";
    c.canon_to_src.assign(w.lines.size(), -1);
    map_lines(prog, c.canon_to_src, false);
    c.ok = c.diags.empty();
    return c;
}

struct Pretty {
    bool ok = false;
    std::vector<Diag> diags;
    std::string text;
    std::vector<int> canon_to_src;    // per line of the game's text: the line in `text`, or -1
};

// Game script text (as Save writes it) -> friendly code. On failure `text` is the game text unchanged.
inline Pretty prettify(const std::string& game_text) {
    Pretty p;
    auto tokens = tokenize(game_text, p.diags);
    Parser parser(tokens, p.diags);
    auto prog = parser.program();
    Symbols sy;
    collect(prog, sy);
    Resolver(sy, p.diags).items(prog);
    if (!p.diags.empty()) { p.text = game_text; return p; }
    CanonWriter cw(sy, p.diags);   // numbers the statements the way the game lays them out
    cw.items(prog, 0);
    PrettyWriter w(sy);
    w.items(prog, 0);
    for (size_t i = 0; i < w.lines.size(); ++i) p.text += w.lines[i] + (i + 1 < w.lines.size() ? "\n" : "");
    p.canon_to_src.assign(cw.lines.size(), -1);
    map_lines(prog, p.canon_to_src, true);
    p.ok = true;
    return p;
}

// ------------------------------------------------------------------------------------------------ reference
struct HelpEntry { const char* group; const char* insert; const char* label; const char* help; };
inline const HelpEntry kHelp[] = {   // "$" marks where the cursor lands after inserting
    {"Blocks", "on_tick {\n\t$\n}", "on_tick { }", "Runs every game tick"},
    {"Blocks", "if $ {\n\t\n}", "if x { }", "Runs the block when x is true"},
    {"Blocks", "elseif $ {\n\t\n}", "elseif x { }", "After an if: tried when the tests above were false"},
    {"Blocks", "else {\n\t$\n}", "else { }", "After an if: runs when nothing above matched"},
    {"Blocks", "repeat $ {\n\t\n}", "repeat n { }", "Runs the block n times"},
    {"Blocks", "foreach $ {\n\t\n}", "foreach list { }", "Runs the block once per list item"},
    {"Define", "define var integer $", "define var type name", "A variable (bool, integer, fractional, string, analogue)"},
    {"Define", "define list integer $", "define list type name", "A list of values"},
    {"Define", "define in fractional $", "define in type name", "An input port on this microcontroller"},
    {"Define", "define out fractional $", "define out type name", "An output port on this microcontroller"},
    {"Define", "define function $(integer a) {\n\t\n}", "define function f(type a) { }", "A function; run it with f(1)"},
    {"Define", "define type $ {\n\tdefine member integer value\n}", "define type T { }", "A custom type with members"},
    {"Values", "true$", "true / false", "Yes or no"},
    {"Values", "\"$\"", "\"text\"", "A string"},
    {"Values", "rand($0, 10)", "rand(low, high)", "Random number between low and high"},
    {"Maths", " + $", "+  -  *  /", "Add, subtract, multiply, divide"},
    {"Maths", " % $", "%", "Remainder after dividing"},
    {"Maths", " ^ $", "^", "Power: 2 ^ 3 is 8"},
    {"Maths", "abs($)", "abs(x)", "Distance from zero"},
    {"Maths", "floor($)", "floor(x)", "Round down"},
    {"Maths", "ceil($)", "ceil(x)", "Round up"},
    {"Maths", "round($)", "round(x)", "Round to nearest"},
    {"Maths", "sqrt($)", "sqrt(x)", "Square root"},
    {"Maths", "sin($)", "sin(x) cos tan", "Trig functions (also asin, acos, atan)"},
    {"Maths", "exp($)", "exp(x) ln log2", "Exponent and logarithms"},
    {"Maths", "to_integer($)", "to_integer(x)", "Fractional to integer"},
    {"Maths", "to_fractional($)", "to_fractional(x)", "Integer to fractional"},
    {"Compare", " == $", "==  !=", "Equal, not equal"},
    {"Compare", " > $", ">  <  >=  <=", "Greater, less"},
    {"Compare", " and $", "and  or  not", "Combine true / false tests"},
    {"Compare", " contains $", "contains", "A list or string contains a value"},
    {"Lists", ".append($)", "l.append(x)", "Add to the end"},
    {"Lists", ".insert(0, $)", "l.insert(i, x)", "Insert at position i"},
    {"Lists", ".erase($)", "l.erase(i)", "Remove position i"},
    {"Lists", ".clear()$", "l.clear()", "Remove everything"},
    {"Lists", "len($)", "len(l)", "How many items"},
    {"Lists", "[$]", "l[i]", "Item at position i (from 0)"},
    {"Other", "# $", "# note", "A comment"},
};

}  // namespace mccode
