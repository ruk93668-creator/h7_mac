"""Read-only B0 source inventory. Run from any directory; stdout is JSON, not a hardware verdict."""
from pathlib import Path
import hashlib, json, re, subprocess
ROOT = Path(__file__).resolve().parents[2]
SCOPES = ["CM7", "CM4", "Common", "Board"]
PATTERN = re.compile(r"USART2|UART[234578]|DMA[12]_Stream[0-7]|HAL_NVIC_SetPriority|configSUPPORT_.*ALLOCATION|configUSE_(TIMERS|MUTEXES|RECURSIVE_MUTEXES|COUNTING_SEMAPHORES)|STACK_WORDS|HAL_(ETH|UART|QSPI)_MODULE_ENABLED")
files = []
for scope in SCOPES:
    for p in sorted((ROOT / scope).rglob("*")):
        if not p.is_file() or p.suffix not in (".c", ".h"):
            continue
        raw = p.read_bytes()
        text = raw.decode("utf-8", errors="replace")
        matches = [{"line": n, "text": line.strip()} for n, line in enumerate(text.splitlines(), 1) if PATTERN.search(line)]
        if matches:
            files.append({"path": p.relative_to(ROOT).as_posix(), "sha256": hashlib.sha256(raw).hexdigest(), "matches": matches})
cmd = ["git", "-c", "safe.directory=" + ROOT.as_posix(), "-C", str(ROOT)]
head = subprocess.check_output(cmd + ["rev-parse", "HEAD"], text=True).strip()
print(json.dumps({"source_commit": head, "application_v3_exists": (ROOT / "CM7/ApplicationV3").exists(), "files": files,
                  "limits": "Text inventory only; no board, DMA allocation, IRQ latency, memory fit or other-branch compatibility verification."}, ensure_ascii=False, indent=2))
