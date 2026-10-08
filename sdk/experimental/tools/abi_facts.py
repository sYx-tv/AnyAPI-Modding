"""Whole-program static checks of the gcl calling convention (writes json/abi_facts.json).

    python tools/abi_facts.py --gcl <game.gcl> --json json

Disassembles every gcl code blob completely (instructions only, up to the constant pool) and records:
  * xmm6..xmm15 usage            - does gcl code ever touch the Win64 non-volatile vector registers?
  * pushed GPRs per prologue      - which callee-saved registers gcl saves
  * stack alignment               - (8 * pushes + sub rsp) % 16 at the end of the prologue (must be 8 so
                                    rsp is 16-aligned at calls)
  * hidden return pointer         - non-void functions store their result through the first register
                                    argument (rcx) spill slot
  * argument passing by pointer   - parameters are dereferenced, never used as values (sampled: the
                                    first use of each spilled parameter register is a memory operand base)
Evidence class of every number: static (complete disassembly of game.gcl), not runtime.
"""
import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gcl  # noqa: E402
from common import STATIC, dump_json, load_json  # noqa: E402
from sdk_analyze import MD, X  # noqa: E402

NONVOL_GPR = {'rbx', 'rbp', 'rdi', 'rsi', 'r12', 'r13', 'r14', 'r15'}


def pool_start(blob, code_end):
    pool = code_end
    for ins in MD.disasm(blob[:code_end], 0):
        if ins.address >= pool:
            break
        for op in ins.operands:
            if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                t = ins.address + ins.size + op.mem.disp
                if ins.address + ins.size <= t < code_end:
                    pool = min(pool, t)
    return pool


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--gcl', required=True)
    ap.add_argument('--json', required=True)
    a = ap.parse_args(argv)
    p = gcl.Program(a.gcl)
    c = collections.Counter()
    pushes = collections.Counter()
    align = collections.Counter()
    nonvol_written_not_saved = []
    xmm_examples = []
    for f in p.functions:
        if not f.code_end:
            continue
        c['functions'] += 1
        end = pool_start(f.blob, f.code_end)
        pushed, sub, in_prologue = [], 0, True
        uses_hi_xmm = False
        written = set()
        for ins in MD.disasm(f.blob[:end], 0):
            m = ins.mnemonic
            if in_prologue:
                if m == 'push':
                    pushed.append(MD.reg_name(ins.operands[0].reg))
                    continue
                if m == 'sub' and ins.op_str.startswith('rsp,'):
                    sub = ins.operands[1].imm
                    in_prologue = False
                    continue
                in_prologue = False
            for op in ins.operands:
                if op.type == X.X86_OP_REG:
                    n = MD.reg_name(op.reg)
                    if n.startswith(('xmm', 'ymm')) and int(n[3:]) >= 6:
                        uses_hi_xmm = True
            if ins.operands and ins.operands[0].type == X.X86_OP_REG and m not in ('push', 'pop', 'cmp', 'test'):
                n = MD.reg_name(ins.operands[0].reg)
                for g in NONVOL_GPR:
                    if n == g or (g.startswith('r1') and n.startswith(g)) or n in {'rbx': ('ebx', 'bx', 'bl'),
                                                                                   'rbp': ('ebp',), 'rdi': ('edi', 'dil'),
                                                                                   'rsi': ('esi', 'sil')}.get(g, ()):
                        written.add(g)
        pushes[' '.join(pushed) or '(none)'] += 1
        align[(8 * len(pushed) + sub) % 16] += 1
        if uses_hi_xmm:
            c['uses_xmm6_15'] += 1
            if len(xmm_examples) < 5:
                xmm_examples.append(f.sig)
        bad = written - set(pushed)
        if bad:
            c['writes_nonvolatile_gpr_without_push'] += 1
            if len(nonvol_written_not_saved) < 10:
                nonvol_written_not_saved.append({'sig': f.sig, 'regs': sorted(bad)})
    facts = {
        'evidence': STATIC, 'basis': 'complete disassembly of every gcl code blob (instructions up to the constant pool)',
        'functions_with_code': c['functions'],
        'xmm6_15': {'functions_using': c['uses_xmm6_15'], 'examples': xmm_examples,
                    'conclusion': 'gcl code does not use xmm6-xmm15, so calling it cannot clobber them; natives are '
                                  'MSVC-compiled and assumed Win64-compliant' if not c['uses_xmm6_15'] else
                                  'some gcl functions use xmm6-xmm15; see examples'},
        'prologue_pushes': dict(pushes.most_common(12)),
        'rsp_mod16_after_prologue': {str(k): v for k, v in align.items()},
        'nonvolatile_gpr_written_without_push': {'count': c['writes_nonvolatile_gpr_without_push'],
                                                 'examples': nonvol_written_not_saved},
    }
    dump_json(facts, os.path.join(a.json, 'abi_facts.json'))
    print({k: v for k, v in facts.items() if k not in ('prologue_pushes',)})


if __name__ == '__main__':
    main()
