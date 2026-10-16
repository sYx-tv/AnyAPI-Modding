// MCCode editor model tests: typing, auto-indent, selection, undo, tab columns.
#undef NDEBUG
#include "../mccode_editor.h"
#include <cassert>
#include <iostream>

using namespace mccode;
using M = Editor::Move;

int main() {
    Editor e;
    for (char c : std::string("on_tick {")) e.type_char(c);
    e.newline();
    assert(e.text() == "on_tick {\n\t" && e.cursor() == (Pos{1, 1}));
    for (char c : std::string("x = 1")) e.type_char(c);
    e.newline();
    e.type_char('}');
    assert(e.text() == "on_tick {\n\tx = 1\n}");
    // typing merges into one undo step; the "}" dedent and newline are their own
    assert(e.undo() && e.text() == "on_tick {\n\tx = 1\n\t");
    assert(e.undo() && e.text() == "on_tick {\n\tx = 1");
    assert(e.redo() && e.redo() && e.text() == "on_tick {\n\tx = 1\n}");

    // Enter between { and } opens an indented line
    e.set_text("if x {}");
    e.set_cursor({0, 6}, false);
    e.newline();
    assert(e.text() == "if x {\n\t\n}" && e.cursor() == (Pos{1, 1}));

    // selection replace, multi-line copy, delete
    e.set_text("abc\ndef\nghi");
    e.set_cursor({0, 1}, false);
    e.set_cursor({2, 1}, true);
    assert(e.selection_text() == "bc\ndef\ng");
    e.insert("X");
    assert(e.text() == "aXhi");
    e.select_all();
    e.backspace();
    assert(e.text() == "" && e.lines().size() == 1);

    // block indent / outdent
    e.set_text("a\nb\nc");
    e.set_cursor({0, 0}, false);
    e.set_cursor({2, 1}, true);
    e.indent(false);
    assert(e.text() == "\ta\n\tb\n\tc");
    e.indent(true);
    assert(e.text() == "a\nb\nc");

    // tabs are 4 columns wide; clicks round to the nearest character edge
    e.set_text("\tx = 1");
    assert(e.visual_col(0, 1) == 4 && e.visual_col(0, 2) == 5);
    assert(e.byte_col(0, 1) == 0 && e.byte_col(0, 3) == 1 && e.byte_col(0, 4) == 1 && e.byte_col(0, 5) == 2 && e.byte_col(0, 99) == 6);

    // up/down keep the column
    e.set_text("abcdef\nab\nabcdef");
    e.set_cursor({0, 5}, false);
    e.move(M::Down, false);
    assert(e.cursor() == (Pos{1, 2}));
    e.move(M::Down, false);
    assert(e.cursor() == (Pos{2, 5}));
    // word moves and word delete
    e.set_text("foo bar(baz)");
    e.set_cursor({0, 12}, false);
    e.move(M::WordLeft, false);
    assert(e.cursor().col == 11);
    e.set_cursor({0, 7}, false);
    e.backspace(true);
    assert(e.text() == "foo (baz)");
    // non-ASCII pasted characters become spaces, CR is dropped
    e.set_text("");
    e.insert("a\r\nb\xc3\xa9");
    assert(e.text() == "a\nb  ");
    std::cout << "mccode_editor_test OK\n";
}
