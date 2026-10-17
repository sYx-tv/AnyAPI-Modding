// MCCode node graph tests: every node kind generates code that compiles, the graph survives the trip through the
// game's text, and hand edits are noticed.
#undef NDEBUG
#include "../mccode_nodes.h"
#include <cassert>
#include <iostream>

using namespace mccode;
using namespace mccode::nodes;

static void check_compiles(const Generated& gen) {
    Compiled c = compile(gen.code);
    if (!c.ok) {
        std::cerr << gen.code << "\n";
        for (auto& d : c.diags) std::cerr << "  diag " << d.line + 1 << ":" << d.col << " " << d.msg << "\n";
        assert(false);
    }
    assert(gen.problems.empty());
}

int main() {
    // every node kind, all inputs wired from a matching source, compiles
    for (auto& d : library()) {
        Graph g;
        Node& n = g.add(d.kind, 300, 100);
        int id = n.id;
        for (size_t i = 0; i < d.ins.size(); ++i) {
            Node& src = g.add(d.ins[i].is_bool ? "part_in_bool" : "part_in", 0, float(i) * 80);
            assert(g.connect(src.id, 0, id, int(i)));
        }
        for (size_t i = 0; i < d.outs.size(); ++i) {
            Node& dst = g.add(d.outs[i].is_bool ? "part_out_bool" : "part_out", 600, float(i) * 80);
            assert(g.connect(id, int(i), dst.id, 0));
        }
        check_compiles(generate(g));
        // unwired inputs too
        Graph lone;
        lone.add(d.kind, 0, 0);
        check_compiles(generate(lone));
    }

    // the traction control from the thread: slip -> ramp -> stepper
    Graph g;
    int rl = g.add("part_in", 0, 0).id, rr = g.add("part_in", 0, 60).id;
    g.node(rr)->params["part"] = "RearRight";
    int avg = g.add("add", 200, 30).id, half = g.add("const", 200, 120).id, div = g.add("div", 360, 60).id;
    g.node(half)->params["value"] = "2";
    int ramp = g.add("ramp", 520, 60).id, out = g.add("part_out", 700, 60).id;
    g.node(out)->params["part"] = "Actuator_Stepper_Motor";
    assert(g.connect(rl, 0, avg, 0) && g.connect(rr, 0, avg, 1));
    assert(g.connect(avg, 0, div, 0) && g.connect(half, 0, div, 1));
    assert(g.connect(div, 0, ramp, 0) && g.connect(ramp, 0, out, 0));
    assert(!g.connect(rl, 0, g.add("not", 0, 0).id, 0));   // number into on/off is refused
    Generated gen = generate(g);
    check_compiles(gen);
    Compiled c = compile(gen.code);
    assert(c.canonical.find("var n1_0 = in RearLeft.rps") != std::string::npos);
    assert(c.canonical.find("var n3_0 = (var n1_0 + var n2_0)") != std::string::npos);
    assert(c.canonical.find("out Actuator_Stepper_Motor.input = var n6_0") != std::string::npos);
    assert(c.canonical.find("var n4_0 = 2.0") != std::string::npos);
    // the output runs after everything that feeds it
    assert(c.canonical.find("var n6_0 =") < c.canonical.find("out Actuator_Stepper_Motor.input"));
    // every generated line is tagged with its node or -1
    size_t lines = 1;
    for (char ch : gen.code) if (ch == '\n') ++lines;
    assert(gen.line_node.size() == lines);

    // round trip through the game's text: Load, then Save, then prettify, then parse
    Pretty back = prettify(c.canonical);
    assert(back.ok);
    Parsed p = parse(back.text);
    assert(p.found && !p.hand_edited);
    assert(p.graph.nodes.size() == g.nodes.size() && p.graph.wires.size() == g.wires.size());
    assert(p.graph.node(out)->params["part"] == "Actuator_Stepper_Motor");
    assert(p.graph.node(ramp)->x == 520);
    assert(p.graph.next_id == g.next_id);
    // a hand edit below the header is noticed
    std::string edited = back.text;
    size_t at = edited.find("+ 0.01");
    assert(at != std::string::npos);
    edited.replace(at, 6, "+ 0.02");
    assert(parse(edited).hand_edited);
    // plain code is not a graph
    assert(!parse("define var integer x\non_tick {\n\tx = 1\n}\n").found);
    assert(!parse("# a comment\ndefine var integer x\n").found);

    // parameter values with spaces and symbols survive the header
    Graph q;
    Node& t = q.add("part_in", 0, 0);
    t.params["type"] = "whole number";
    Parsed pq = parse(generate(q).code);
    assert(pq.found && pq.graph.nodes[0].params["type"] == "whole number");
    assert(compile(generate(q).code).canonical.find("to_fractional (in RearLeft.rps)") != std::string::npos);
    // a bad part name is a problem on that node, not broken code
    Graph bad;
    Node& b = bad.add("part_in", 0, 0);
    b.params["part"] = "Rear Left";
    Generated gb = generate(bad);
    assert(gb.problems.size() == 1 && gb.problems[0].node == b.id && compile(gb.code).ok);
    // loops read last tick's value instead of hanging
    Graph loop;
    int a1 = loop.add("add", 0, 0).id, a2 = loop.add("add", 100, 0).id;
    assert(loop.connect(a1, 0, a2, 0) && loop.connect(a2, 0, a1, 0));
    check_compiles(generate(loop));
    // removing a node removes its wires
    g.remove(div);
    for (auto& w : g.wires) assert(w.from != div && w.to != div);

    std::cout << gen.code << "\n";
    std::cout << "mccode_nodes_test OK\n";
    return 0;
}
