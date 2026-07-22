#!/usr/bin/env python3
"""Ejecutor reproducible: conserva muestras crudas y calcula la mediana."""

from __future__ import annotations

import argparse
import json
import platform
import statistics
import subprocess
import time
from pathlib import Path


def measured(command: list[str], runs: int) -> dict[str, object]:
    samples = []
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)  # calentamiento
    for _ in range(runs):
        start = time.perf_counter_ns()
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        samples.append((time.perf_counter_ns() - start) / 1_000_000)
    return {"command": command, "samples_ms": samples, "median_ms": statistics.median(samples)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cforge", default="cforge")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--output", type=Path, default=Path("build/benchmarks/results.json"))
    parser.add_argument("files", nargs="*", type=Path)
    args = parser.parse_args()
    if args.runs < 3: parser.error("se requieren al menos 3 repeticiones")
    files = args.files or sorted(Path("benchmarks").glob("[0-9][0-9]_*.cfv"))
    report = {
        "schema": 1,
        "platform": platform.platform(),
        "machine": platform.machine(),
        "python": platform.python_version(),
        "runs_after_warmup": args.runs,
        "results": {str(path): measured([args.cforge, str(path)], args.runs) for path in files},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
