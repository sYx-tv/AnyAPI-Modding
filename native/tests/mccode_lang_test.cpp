// MCCode language core tests: friendly code -> game script text, game text -> friendly code, line maps, errors.
#undef NDEBUG
#include "../mccode_lang.h"
#include <cassert>
#include <cstdio>
#include <iostream>

using namespace mccode;

static void expect_canon(const std::string& src, const std::string& want) {
    Compiled c = compile(src);
    if (!c.ok || c.canonical != want) {
        std::cerr << "SOURCE:\n" << src << "\nGOT:\n" << c.canonical << "\nWANT:\n" << want << "\n";
        for (auto& d : c.diags) std::cerr << "  diag " << d.line << ":" << d.col << " " << d.msg << "\n";
        assert(false);
    }
}
static void expect_error(const std::string& src, int line, const std::string& fragment) {
    Compiled c = compile(src);
    bool found = false;
    for (auto& d : c.diags) if (d.line == line && d.msg.find(fragment) != std::string::npos) found = true;
    if (c.ok || !found) {
        std::cerr << "SOURCE:\n" << src << "\nwanted an error on line " << line << " containing '" << fragment << "'\n";
        for (auto& d : c.diags) std::cerr << "  diag " << d.line << ":" << d.col << " " << d.msg << "\n";
        assert(false);
    }
}

int main() {
    // The game's own text compiles to itself.
    const std::string game =
        "define in fractional engine_rpm\n"
        "define out bool two_step\n"
        "define var integer count\n"
        "define list fractional samples\n"
        "\n"
        "# launch control\n"
        "define function arm (integer limit, bool on)\n"
        "{\n"
        "\tif ((param on && (in engine_rpm > to_fractional (param limit))))\n"
        "\t{\n"
        "\t\tout two_step = true\n"
        "\t}\n"
        "\telseif (! ((var count == 3)))\n"
        "\t{\n"
        "\t\tvar count = ((var count + 1) % 4)\n"
        "\t}\n"
        "\telse\n"
        "\t{\n"
        "\t\tout two_step = false\n"
        "\t}\n"
        "}\n"
        "on_tick\n"
        "{\n"
        "\tcall arm (4000, true)\n"
        "\tvar samples append (in engine_rpm)\n"
        "\tvar samples insert (0, abs (in engine_rpm))\n"
        "\tvar samples erase (0)\n"
        "\trepeat (len (var samples))\n"
        "\t{\n"
        "\t\tvar samples clear\n"
        "\t}\n"
        "\tvar count = rand (0, (var count ^ 2))\n"
        "\tvar count = var samples[0]\n"
        "}\n";
    expect_canon(game, game);

    // Friendly forms.
    expect_canon("define var integer x\non_tick {\n\tx = x + 2 * 3\n}\n",
                 "define var integer x\non_tick\n{\n\tvar x = (var x + (2 * 3))\n}\n");
    expect_canon("define var integer x\non_tick {\n\tx = (x + 2) * 3 ^ 2 ^ 1\n}\n",
                 "define var integer x\non_tick\n{\n\tvar x = ((var x + 2) * (3 ^ (2 ^ 1)))\n}\n");
    expect_canon("define in fractional a\ndefine var bool b\non_tick {\n\tb = a > 1.5 and not b or a != 2.0\n}\n",
                 "define in fractional a\ndefine var bool b\non_tick\n{\n\tvar b = (((in a > 1.5) && ! (var b)) || ! ((in a == 2.0)))\n}\n");
    expect_canon("define var integer x\ndefine var fractional f\non_tick {\n\tx = -5\n\tf = -f * -1.5\n}\n",
                 "define var integer x\ndefine var fractional f\non_tick\n{\n\tvar x = (0 - 5)\n\tvar f = ((0.0 - var f) * (0.0 - 1.5))\n}\n");
    expect_canon("define list integer l\non_tick {\n\tl.append(1)\n\tl.insert(0, 2)\n\tl.erase(1)\n\tl.clear()\n}\n",
                 "define list integer l\non_tick\n{\n\tvar l append (1)\n\tvar l insert (0, 2)\n\tvar l erase (1)\n\tvar l clear\n}\n");
    expect_canon("define var integer x\non_tick {\n\tif x > 1 {\n\t\tx = 0\n\t} else if x < 0 {\n\t\tx = 1\n\t} else {\n\t\tx = 2\n\t}\n}\n",
                 "define var integer x\non_tick\n{\n\tif ((var x > 1))\n\t{\n\t\tvar x = 0\n\t}\n\telseif ((var x < 0))\n\t{\n\t\tvar x = 1\n\t}\n\telse\n\t{\n\t\tvar x = 2\n\t}\n}\n");
    expect_canon("define function go() {\n}\non_tick {\n\tgo()\n}\n",
                 "define function go ()\n{\n}\non_tick\n{\n\tcall go ()\n}\n");
    // A trailing comment moves onto its own line above the statement.
    expect_canon("define var integer x # counter\n", "# counter\ndefine var integer x\n");
    // Parameters shadow variables inside their function.
    expect_canon("define var integer n\ndefine function f(integer n) {\n\tn = n\n}\n",
                 "define var integer n\ndefine function f (integer n)\n{\n\tparam n = param n\n}\n");
    // Line map: canonical line -> source line.
    {
        Compiled c = compile("define var integer x\non_tick {\n\tx = 1\n\n\tx = 2\n}\n");
        assert(c.ok);
        // canonical: 0 define, 1 on_tick, 2 {, 3 x=1, 4 blank, 5 x=2, 6 }
        std::vector<int> want = {0, 1, -1, 2, 3, 4, -1};
        assert(c.canon_to_src == want);
    }

    // Errors carry the right line.
    expect_error("on_tick {\n\tx = 1\n}\n", 1, "Unknown name 'x'");
    expect_error("define var integer x\non_tick {\n\tx = (1 + \n}\n", 2, "Expected a value");
    expect_error("define var integer x\non_tick {\n\tx = 1\n", 1, "never closed");
    expect_error("define var integer x\ndefine in integer x\non_tick {\n\tx = 1\n}\n", 3, "more than one thing");
    expect_error("on_render {\n}\n", 0, "on_render isn't supported");
    expect_error("on_tick {\n\tfoo(1)\n}\n", 1, "no function called 'foo'");
    expect_error("define var integer x\non_tick {\n\tx\n}\n", 2, "doesn't do anything");
    expect_error("define var integer x\non_tick {\n\tx = \"abc\n}\n", 2, "closing");
    expect_error("define var wheel x\n", 0, "Unknown type");
    expect_error("on_tick {\n\tdefine var integer 2x\n}\n", 1, "digit");
    expect_error("}\n", 0, "no matching");

    // Game text -> friendly code -> the same game text.
    {
        Pretty p = prettify(game);
        assert(p.ok);
        Compiled back = compile(p.text);
        if (!back.ok || back.canonical != game) {
            std::cerr << "PRETTY:\n" << p.text << "\nBACK:\n" << back.canonical << "\n";
            for (auto& d : back.diags) std::cerr << "  diag " << d.line << ":" << d.col << " " << d.msg << "\n";
            assert(false);
        }
        assert(p.text.find("if on and engine_rpm > to_fractional(limit) {") != std::string::npos);
        assert(p.text.find("} elseif count != 3 {") != std::string::npos);
        assert(p.text.find("count = (count + 1) % 4") != std::string::npos);
        assert(p.text.find("samples.insert(0, abs(engine_rpm))") != std::string::npos);
        // every statement line of the game text maps to the friendly line that shows it
        assert(p.canon_to_src.size() == back.canon_to_src.size());
        for (size_t i = 0; i < p.canon_to_src.size(); ++i) assert(p.canon_to_src[i] == back.canon_to_src[i]);
        std::cout << p.text << "\n";
    }
    // Ambiguous names keep their prefix when prettified.
    {
        Pretty p = prettify("define var integer x\ndefine in integer x\non_tick\n{\n\tvar x = in x\n}\n");
        assert(p.ok && p.text.find("var x = in x") != std::string::npos);
    }
    // Text the translator can't read comes back unchanged.
    {
        Pretty p = prettify("on_tick\n{\n\tvar nope = 1\n}\n");
        assert(!p.ok && p.text == "on_tick\n{\n\tvar nope = 1\n}\n");
    }
    // Parts wired to the microcontroller need no define: reading a channel is an input, setting one an output.
    {
        Compiled c = compile("define var fractional avg\non_tick {\n\tavg = (RearLeft.rps + RearRight.rps) / 2.0\n\tRearLeft.brake_factor = 1.0\n}\n");
        assert(c.ok);
        assert(c.canonical.find("var avg = ((in RearLeft.rps + in RearRight.rps) / 2.0)") != std::string::npos);
        assert(c.canonical.find("out RearLeft.brake_factor = 1.0") != std::string::npos);
        Pretty p = prettify(c.canonical);
        assert(p.ok);
        Compiled back = compile(p.text);
        assert(back.ok && back.canonical == c.canonical);
        // a channel used as the index of a list item being set is still read
        Compiled item = compile("define list integer l\non_tick {\n\tl[Sensor.index] = 1\n}\n");
        assert(item.ok && item.canonical.find("var l[in Sensor.index] = 1") != std::string::npos);
    }
    std::cout << "mccode_lang_test OK\n";
    return 0;
}
