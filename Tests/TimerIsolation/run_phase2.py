"""Execute unchanged ARM timer implementation under Unicorn, without peripherals."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

CASES = [
    ("One-Shot order", "A=300, B=100, C=200 ms; start at 0", "B@100, C@200, A@300; no later repeats"),
    ("Delete root", "Periods 700,100,600,200,500,300,400; delete heap[0] at 50", "Remaining callbacks sorted by deadline; pool/heap 6/0 at finish"),
    ("Delete middle", "Same seven timers; delete heap[2] at 50", "Remaining callbacks sorted by deadline; pool/heap 6/0 at finish"),
    ("Delete tail", "Same seven timers; delete heap[size-1] at 50", "Remaining callbacks sorted by deadline; pool/heap 6/0 at finish"),
    ("Active repeated Start", "A=100,B=150; restart A at each tick 1..50", "Heap stays 2; no callback before 150; exactly two at 150"),
    ("One-Shot callback Stop", "A=100,B=150; A callback stops itself", "A once at 100, B once at 150; heap=0, pool=2"),
    ("One-Shot callback Start", "A=100,B=150; first A callback restarts itself", "A@100,B@150,A@200; heap=0, pool=2"),
    ("One-Shot callback Delete", "A=100,B=150; A callback deletes itself", "A@100,B@150; old handle rejected; heap=0, pool=1"),
    ("Periodic callback Stop", "A periodic=100,B one-shot=150; A callback stops itself", "A@100,B@150; heap=0, pool=2"),
    ("Periodic callback Start", "A periodic=100,B one-shot=150; first A callback restarts itself", "A@100,B@150,A@200; one active A; heap=1, pool=2"),
    ("Periodic callback Delete", "A periodic=100,B one-shot=150; A callback deletes itself", "A@100,B@150; old handle rejected; heap=0, pool=1"),
    ("Generation stale handle", "Create/start/delete A; recreate B in same slot; use old handle", "Generation 1->2; old Start/Stop/Delete rejected; new B@200"),
    ("Tick rollover", "Start at 0xFFFFFF80; A=300,B=100,C=200", "B@FFFFFFE4,C@00000048,A@000000AC; no early callbacks"),
    ("Capacity and lifecycle", "Create/start 16; stop evens; delete odds; refill; expire all; delete all", "17th create rejected; counts 16/0->16/16->16/8->8/0->16/16->16/0->0/0"),
]


def hashes(project):
    suffixes = {".c", ".h", ".s", ".sct", ".uvprojx", ".uvoptx"}
    return {p.relative_to(project).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for directory in ("CM7", "CM4", "Common", "MDK-ARM")
            for p in (project / directory).rglob("*") if p.is_file() and p.suffix.lower() in suffixes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--deps", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if args.deps:
        sys.path.insert(0, str(args.deps))
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
    from elftools.elf.elffile import ELFFile

    project = Path(__file__).resolve().parents[2]
    before = hashes(project)
    with args.image.open("rb") as stream:
        elf = ELFFile(stream)
        segments = [(s["p_vaddr"], s.data()) for s in elf.iter_segments()
                    if s["p_type"] == "PT_LOAD" and s["p_filesz"]]
        symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
        entry = elf.header["e_entry"]

    fields = ("failure_line", "invariant_checks", "callback_count", "callback_order", "pool_count",
              "heap_size", "old_handle", "new_handle", "action_result", "first_callback_tick")
    results = []
    for case_id, (name, inputs, expected) in enumerate(CASES):
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.mem_map(0x08000000, 0x100000)
        uc.mem_map(0x20000000, 0x40000)
        uc.mem_map(0x01000000, 0x1000)
        for address, data in segments:
            uc.mem_write(address, data)
        uc.reg_write(UC_ARM_REG_SP, 0x2003FFF0)
        uc.reg_write(UC_ARM_REG_LR, 0x01000001)
        uc.reg_write(UC_ARM_REG_R0, case_id)
        error = None
        try:
            uc.emu_start(entry | 1, 0x01000000, timeout=10000000, count=10000000)
        except Exception as exc:
            error = str(exc)
        state = dict(zip(fields, struct.unpack("<10I", uc.mem_read(symbols["g_test_result"], 40))))
        returned = uc.reg_read(UC_ARM_REG_PC) == 0x01000000
        count = min(state["callback_count"], 64)
        ids = struct.unpack("<64I", uc.mem_read(symbols["callback_ids"], 256))[:count]
        ticks = struct.unpack("<64I", uc.mem_read(symbols["callback_ticks"], 256))[:count]
        callbacks = [{"id": i, "tick": t} for i, t in zip(ids, ticks)]
        passed = error is None and returned and state["failure_line"] == 0 and uc.reg_read(UC_ARM_REG_R0) == 0
        actual = ", ".join(f"{i}@{t}" for i, t in zip(ids, ticks))
        actual += f"; pool/heap={state['pool_count']}/{state['heap_size']}; invariants={state['invariant_checks']}"
        if state["old_handle"]:
            actual += f"; handle 0x{state['old_handle']:08X}->0x{state['new_handle']:08X}"
        if not passed:
            actual += f"; failed test source line={state['failure_line']}; returned={returned}; emulator error={error}"
        result = dict(case_id=case_id, name=name, input=inputs, expected=expected,
                      actual=actual, result="Pass" if passed else "Fail", callbacks=callbacks, state=state)
        results.append(result)
        print(f"{case_id+1:02d} {result['result']}: {name}: {actual}")

    after = hashes(project)
    unchanged = before == after
    report = dict(scope="CM7 actual source; ARMCC 5.06; Unicorn ARM Thumb M-class; controlled mock tick; no STM32 peripherals",
                  framework_sha256=before["CM7/Framework/framework_timer.c"],
                  project_sources_unchanged=unchanged, results=results)
    args.report.mkdir(parents=True, exist_ok=True)
    (args.report / "phase2_results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    rows = ["# Timer Framework phase 2 isolated acceptance", "", report["scope"], "",
            "| Test | Input | Expected | Actual | Result |", "|---|---|---|---|---|"]
    rows += [f"| {r['name']} | {r['input']} | {r['expected']} | {r['actual']} | {r['result']} |" for r in results]
    rows += ["", f"Production source/config hashes unchanged during execution: {unchanged}",
             f"CM7 framework_timer.c SHA256: {report['framework_sha256']}", "",
             "All public operations and callback state changes check: allocated count, active count, heap bounds,",
             "unique heap entries, parent/child deadline order with an independent signed-difference oracle,",
             "heap-to-pool and pool-to-heap indices, and stopped/previously allocated slots' invalid heap index.",
             "Each case uses a fresh zero-initialized ARM emulator. Actual source is included unchanged so private",
             "state can be inspected without adding production exports or test hooks.", "",
             "This runner executes CM7 only; this report does not claim CM4 timer acceptance.",
             "Simulated ticks test protocol logic, not wall-clock timing accuracy on a physical board."]
    (args.report / "phase2_results.md").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"Total: {sum(r['result']=='Pass' for r in results)}/{len(results)} Pass; source/config unchanged={unchanged}")
    return 0 if unchanged and all(r["result"] == "Pass" for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
