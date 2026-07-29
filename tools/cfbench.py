#!/usr/bin/env python3
"""
cfbench — C-Forge Benchmark Runner v3.1.0

Ejecuta y compara benchmarks escritos en C-Forge.

Uso:
  cfbench run archivo.cfv                  # Ejecutar benchmarks en un archivo
  cfbench run archivo.cfv --iterations 5000  # Número de iteraciones
  cfbench compare a.cfv b.cfv              # Comparar dos implementaciones
  cfbench report archivo.cfv --format json  # Exportar resultados
  cfbench watch archivo.cfv                # Modo watch (re-ejecuta al cambiar)

Formato esperado del archivo .cfv:
  importar "stdlib/benchmark.cfv"
  
  sea suite = bench_suite("Mi benchmark")
  bench_add(suite, "funcion_rapida", funcion() { ... }, 1000)
  bench_add(suite, "funcion_lenta", funcion() { ... }, 1000)
  bench_run(suite)
"""

import sys
import os
import json
import time
import subprocess
import argparse
import re
import statistics
from pathlib import Path
from typing import List, Dict, Optional, Any
from datetime import datetime

# ── ANSI colors ───────────────────────────────────────────────────────────────
GREEN  = "\033[32m"
RED    = "\033[31m"
YELLOW = "\033[33m"
CYAN   = "\033[36m"
BOLD   = "\033[1m"
DIM    = "\033[2m"
RESET  = "\033[0m"

def colored(text: str, color: str) -> str:
    return f"{color}{text}{RESET}" if sys.stdout.isatty() else text

# ── Benchmark runner ───────────────────────────────────────────────────────────
def find_interpreter() -> str:
    """Find the cforgev interpreter."""
    candidates = [
        os.environ.get("CFORGE_INTERPRETER", ""),
        "cforgev",
        "./cforgev",
        str(Path(__file__).parent.parent / "cforgev"),
    ]
    for c in candidates:
        if c and (Path(c).exists() or subprocess.run(
            ["which", c], capture_output=True
        ).returncode == 0):
            return c
    return "cforgev"

def run_cfv(file_path: str, interpreter: str, stdlib_path: str = "") -> Dict:
    """Run a .cfv file and capture output."""
    env = os.environ.copy()
    if stdlib_path:
        env["CFORGE_STDLIB"] = stdlib_path
    elif "CFORGE_STDLIB" not in env:
        # Try to find stdlib
        for candidate in [
            Path(file_path).parent.parent / "stdlib",
            Path(interpreter).parent.parent / "stdlib",
            Path(__file__).parent.parent / "stdlib",
        ]:
            if candidate.exists():
                env["CFORGE_STDLIB"] = str(candidate)
                break

    start = time.perf_counter()
    result = subprocess.run(
        [interpreter, file_path],
        capture_output=True,
        text=True,
        env=env,
        timeout=120,
    )
    elapsed = time.perf_counter() - start

    return {
        "stdout": result.stdout,
        "stderr": result.stderr,
        "returncode": result.returncode,
        "elapsed_s": elapsed,
    }

def parse_bench_output(stdout: str) -> List[Dict]:
    """Parse benchmark results from C-Forge output."""
    results = []
    current = {}
    suite_name = ""

    lines = stdout.split("\n")
    for line in lines:
        # Suite header: ⚡ Benchmark: NombreSuite
        m = re.search(r"Benchmark:\s*(.+)", line)
        if m:
            suite_name = m.group(1).strip()
            continue

        # Bench name: ├── NombreBench
        m = re.search(r"[├─]\s*[─]+\s*(.+)", line)
        if m:
            if current:
                results.append(current)
            current = {"suite": suite_name, "name": m.group(1).strip()}
            continue

        # Stats lines
        for key, pattern in [
            ("iterations", r"iter:\s*(\d+)"),
            ("total_ms", r"total:\s*([\d.]+)ms"),
            ("avg_ms", r"prom:\s*([\d.]+)ms"),
            ("p50_ms", r"p50:\s*([\d.]+)ms"),
            ("p90_ms", r"p90:\s*([\d.]+)ms"),
            ("p99_ms", r"p99:\s*([\d.]+)ms"),
            ("ops_per_sec", r"ops/s:\s*([\d.]+)"),
        ]:
            m = re.search(pattern, line)
            if m and current:
                try:
                    current[key] = float(m.group(1))
                except ValueError:
                    pass

        # Winner
        m = re.search(r"🏆\s*Ganador:\s*(.+)", line)
        if m and results:
            results[-1]["winner"] = True

    if current:
        results.append(current)

    return results

def run_command(args):
    """cfbench run <file>"""
    file_path = args.file
    if not Path(file_path).exists():
        print(colored(f"✗ Archivo no encontrado: {file_path}", RED))
        sys.exit(1)

    interpreter = args.interpreter or find_interpreter()
    print(colored(f"⚡ cfbench v3.1.0", BOLD))
    print(colored(f"   Archivo: {file_path}", DIM))
    print(colored(f"   Intérprete: {interpreter}", DIM))
    if args.iterations:
        print(colored(f"   Iteraciones: {args.iterations}", DIM))
    print()

    # Inject iterations if specified
    if args.iterations:
        source = Path(file_path).read_text()
        # Override default iterations (simple substitution approach)
        source = re.sub(r'bench_add\(([^,]+),\s*([^,]+),\s*([^,]+),\s*\d+\)',
                        rf'bench_add(\1, \2, \3, {args.iterations})', source)
        tmp = Path(file_path).parent / f".cfbench_tmp_{int(time.time())}.cfv"
        tmp.write_text(source)
        file_path = str(tmp)

    try:
        result = run_cfv(file_path, interpreter, args.stdlib or "")
    finally:
        if args.iterations and Path(file_path).stem.startswith(".cfbench_tmp_"):
            try:
                Path(file_path).unlink()
            except Exception:
                pass

    if result["returncode"] != 0:
        print(colored("✗ Error al ejecutar:", RED))
        print(result["stderr"])
        sys.exit(1)

    # Print raw output
    print(result["stdout"])

    # Parse and optionally export
    if args.format:
        benchmarks = parse_bench_output(result["stdout"])
        export_results(benchmarks, args.format, args.output)

def compare_command(args):
    """cfbench compare <file_a> <file_b>"""
    files = args.files
    if len(files) < 2:
        print(colored("✗ Se necesitan al menos 2 archivos para comparar", RED))
        sys.exit(1)

    interpreter = args.interpreter or find_interpreter()
    all_results = {}

    for f in files:
        if not Path(f).exists():
            print(colored(f"✗ Archivo no encontrado: {f}", RED))
            sys.exit(1)
        print(colored(f"▶ Ejecutando {f}...", CYAN))
        result = run_cfv(f, interpreter, args.stdlib or "")
        if result["returncode"] != 0:
            print(colored(f"✗ Error en {f}:", RED))
            print(result["stderr"])
            continue
        benchmarks = parse_bench_output(result["stdout"])
        all_results[f] = benchmarks
        print(colored(f"  ✓ {len(benchmarks)} benchmarks", GREEN))

    if len(all_results) < 2:
        print(colored("✗ No hay suficientes resultados para comparar", RED))
        sys.exit(1)

    print()
    print(colored("═" * 70, BOLD))
    print(colored("  📊 Comparación de Benchmarks", BOLD))
    print(colored("═" * 70, BOLD))

    # Group by benchmark name and compare
    all_names = set()
    for results in all_results.values():
        for r in results:
            all_names.add(r.get("name", ""))

    for name in sorted(all_names):
        print(f"\n  {colored(name, BOLD)}")
        file_results = {}
        for f, results in all_results.items():
            for r in results:
                if r.get("name") == name:
                    file_results[f] = r

        if len(file_results) < 2:
            print(colored("    (no disponible en todos los archivos)", DIM))
            continue

        # Find fastest
        fastest_f = min(file_results, key=lambda f: file_results[f].get("avg_ms", float("inf")))
        fastest_ms = file_results[fastest_f].get("avg_ms", 0)

        for f, r in file_results.items():
            avg = r.get("avg_ms", 0)
            ops = r.get("ops_per_sec", 0)
            is_fastest = f == fastest_f

            ratio = avg / fastest_ms if fastest_ms > 0 else 1.0
            ratio_str = f"1.00x 🏆" if is_fastest else f"{ratio:.2f}x slower"
            color = GREEN if is_fastest else YELLOW

            fname = Path(f).name
            print(f"    {colored(fname, color)}")
            print(f"      prom: {avg:.3f}ms | ops/s: {int(ops):,} | {ratio_str}")

def report_command(args):
    """cfbench report <file> --format json|csv|html"""
    file_path = args.file
    interpreter = args.interpreter or find_interpreter()

    result = run_cfv(file_path, interpreter, args.stdlib or "")
    if result["returncode"] != 0:
        print(colored("✗ Error al ejecutar", RED))
        print(result["stderr"])
        sys.exit(1)

    benchmarks = parse_bench_output(result["stdout"])
    export_results(benchmarks, args.format or "json", args.output)

def watch_command(args):
    """cfbench watch <file> — Re-run on file change."""
    file_path = args.file
    interpreter = args.interpreter or find_interpreter()
    last_mtime = 0.0

    print(colored(f"👀 Watching {file_path} (Ctrl+C para salir)...", CYAN))

    try:
        while True:
            try:
                mtime = Path(file_path).stat().st_mtime
            except FileNotFoundError:
                time.sleep(0.5)
                continue

            if mtime != last_mtime:
                last_mtime = mtime
                ts = datetime.now().strftime("%H:%M:%S")
                print(colored(f"\n[{ts}] Ejecutando benchmarks...", CYAN))
                result = run_cfv(file_path, interpreter, args.stdlib or "")
                if result["returncode"] != 0:
                    print(colored("✗ Error:", RED))
                    print(result["stderr"])
                else:
                    print(result["stdout"])

            time.sleep(0.3)
    except KeyboardInterrupt:
        print(colored("\n✓ Watch detenido", GREEN))

def export_results(benchmarks: List[Dict], fmt: str, output: Optional[str]):
    """Export benchmark results to a file or stdout."""
    if fmt == "json":
        data = {
            "timestamp": datetime.now().isoformat(),
            "benchmarks": benchmarks,
        }
        content = json.dumps(data, indent=2, ensure_ascii=False)
        ext = ".json"
    elif fmt == "csv":
        lines = ["suite,name,iterations,total_ms,avg_ms,p50_ms,p90_ms,p99_ms,ops_per_sec"]
        for b in benchmarks:
            lines.append(",".join([
                b.get("suite", ""),
                b.get("name", ""),
                str(b.get("iterations", "")),
                str(b.get("total_ms", "")),
                str(b.get("avg_ms", "")),
                str(b.get("p50_ms", "")),
                str(b.get("p90_ms", "")),
                str(b.get("p99_ms", "")),
                str(b.get("ops_per_sec", "")),
            ]))
        content = "\n".join(lines)
        ext = ".csv"
    elif fmt == "html":
        content = generate_html_report(benchmarks)
        ext = ".html"
    elif fmt == "md":
        content = generate_markdown_report(benchmarks)
        ext = ".md"
    else:
        print(colored(f"✗ Formato desconocido: {fmt}", RED))
        return

    if output:
        out_path = output if output.endswith(ext) else output + ext
        Path(out_path).write_text(content, encoding="utf-8")
        print(colored(f"✓ Reporte guardado en: {out_path}", GREEN))
    else:
        print(content)

def generate_html_report(benchmarks: List[Dict]) -> str:
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    rows = ""
    for b in benchmarks:
        rows += f"""
        <tr>
          <td>{b.get('suite','')}</td>
          <td><strong>{b.get('name','')}</strong></td>
          <td>{b.get('iterations','')}</td>
          <td>{b.get('avg_ms',''):.3f}ms</td>
          <td>{b.get('p50_ms',''):.3f}ms</td>
          <td>{b.get('p90_ms',''):.3f}ms</td>
          <td>{b.get('p99_ms',''):.3f}ms</td>
          <td>{int(b.get('ops_per_sec',0)):,}</td>
        </tr>"""
    return f"""<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<title>C-Forge Benchmark Report</title>
<style>
  body {{ font-family: 'Segoe UI', sans-serif; background: #1e1e2e; color: #cdd6f4; padding: 2rem; }}
  h1 {{ color: #cba6f7; }} h2 {{ color: #89b4fa; border-bottom: 1px solid #313244; padding-bottom: .5rem; }}
  table {{ border-collapse: collapse; width: 100%; margin-top: 1rem; }}
  th {{ background: #313244; color: #cba6f7; padding: 8px 12px; text-align: left; }}
  td {{ padding: 8px 12px; border-bottom: 1px solid #313244; }}
  tr:hover {{ background: #313244; }}
  .badge {{ background: #a6e3a1; color: #1e1e2e; padding: 2px 8px; border-radius: 4px; font-size: 12px; }}
  .ts {{ color: #6c7086; font-size: 12px; }}
</style>
</head>
<body>
<h1>⚡ C-Forge Benchmark Report</h1>
<p class="ts">Generado: {ts}</p>
<h2>Resultados</h2>
<table>
<thead>
  <tr><th>Suite</th><th>Nombre</th><th>Iters</th><th>Promedio</th><th>P50</th><th>P90</th><th>P99</th><th>Ops/s</th></tr>
</thead>
<tbody>{rows}</tbody>
</table>
</body>
</html>"""

def generate_markdown_report(benchmarks: List[Dict]) -> str:
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines = [
        "# ⚡ C-Forge Benchmark Report",
        f"*Generado: {ts}*",
        "",
        "| Suite | Nombre | Iters | Promedio | P50 | P90 | P99 | Ops/s |",
        "|-------|--------|-------|----------|-----|-----|-----|-------|",
    ]
    for b in benchmarks:
        lines.append(
            f"| {b.get('suite','')} | **{b.get('name','')}** | "
            f"{b.get('iterations','')} | {b.get('avg_ms',0):.3f}ms | "
            f"{b.get('p50_ms',0):.3f}ms | {b.get('p90_ms',0):.3f}ms | "
            f"{b.get('p99_ms',0):.3f}ms | {int(b.get('ops_per_sec',0)):,} |"
        )
    return "\n".join(lines)

# ── CLI ────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        prog="cfbench",
        description="C-Forge Benchmark Runner v3.1.0",
    )
    parser.add_argument("--interpreter", "-i", help="Ruta al intérprete cforgev")
    parser.add_argument("--stdlib", "-s", help="Ruta al directorio stdlib/ (CFORGE_STDLIB)")

    sub = parser.add_subparsers(dest="command")

    # run
    run_p = sub.add_parser("run", help="Ejecutar benchmarks")
    run_p.add_argument("file", help="Archivo .cfv con benchmarks")
    run_p.add_argument("--iterations", "-n", type=int, help="Número de iteraciones")
    run_p.add_argument("--format", "-f", choices=["json","csv","html","md"], help="Exportar resultados")
    run_p.add_argument("--output", "-o", help="Archivo de salida (sin extensión)")

    # compare
    cmp_p = sub.add_parser("compare", help="Comparar varios archivos")
    cmp_p.add_argument("files", nargs="+", help="Archivos .cfv a comparar")
    cmp_p.add_argument("--format", "-f", choices=["json","csv","html","md"])
    cmp_p.add_argument("--output", "-o")

    # report
    rep_p = sub.add_parser("report", help="Generar reporte de benchmarks")
    rep_p.add_argument("file", help="Archivo .cfv")
    rep_p.add_argument("--format", "-f", choices=["json","csv","html","md"], default="json")
    rep_p.add_argument("--output", "-o")

    # watch
    watch_p = sub.add_parser("watch", help="Re-ejecutar al detectar cambios")
    watch_p.add_argument("file", help="Archivo .cfv a observar")

    args = parser.parse_args()

    if args.command == "run":
        run_command(args)
    elif args.command == "compare":
        compare_command(args)
    elif args.command == "report":
        report_command(args)
    elif args.command == "watch":
        watch_command(args)
    else:
        # Default: if a .cfv file is given directly, run it
        if len(sys.argv) > 1 and sys.argv[1].endswith(".cfv"):
            sys.argv.insert(1, "run")
            main()
        else:
            parser.print_help()

if __name__ == "__main__":
    main()
