#!/usr/bin/env python3
"""cfprofile — Profiler para programas C-Forge"""

import sys, re, time, subprocess, json, os
from pathlib import Path
from datetime import datetime

VERSION = "3.0.0"

def run_with_profile(cfv_file: str, stdlib_dir: str, interp: str) -> dict:
    """Ejecuta el programa y recolecta datos de perfil"""
    start = time.perf_counter()
    env = os.environ.copy()
    env["CFORGE_STDLIB"] = stdlib_dir
    env["CFORGE_PROFILE"] = "1"

    try:
        result = subprocess.run(
            [interp, cfv_file],
            capture_output=True, text=True, timeout=60, env=env
        )
        elapsed = time.perf_counter() - start
        return {
            "exit_code": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
            "elapsed": elapsed,
            "success": result.returncode == 0
        }
    except subprocess.TimeoutExpired:
        return {"exit_code": -1, "elapsed": 60, "stderr": "Timeout", "success": False}
    except FileNotFoundError:
        return {"exit_code": -1, "elapsed": 0, "stderr": f"Intérprete no encontrado: {interp}", "success": False}

def analyze_source(source: str) -> dict:
    """Analiza el código para encontrar hotspots potenciales"""
    lines = source.splitlines()
    hotspots = []
    loop_depth = 0
    fn_calls = {}

    for i, line in enumerate(lines, 1):
        s = line.strip()
        # Count loop nesting
        if re.match(r'(mientras|para)\s*\(', s):
            loop_depth += 1
            if loop_depth >= 3:
                hotspots.append({
                    "line": i, "type": "nested_loop",
                    "severity": "high",
                    "message": f"Bucle anidado nivel {loop_depth} — posible hotspot"
                })
        if "}" in s:
            loop_depth = max(0, loop_depth - 1)

        # Count function calls
        for m in re.finditer(r'(\w+)\s*\(', s):
            fn = m.group(1)
            if fn not in ["si", "mientras", "para", "funcion", "clase"]:
                fn_calls[fn] = fn_calls.get(fn, 0) + 1

        # Detect O(n²) patterns
        if re.search(r'lista_contiene|lista_buscar', s) and loop_depth > 0:
            hotspots.append({
                "line": i, "type": "linear_search_in_loop",
                "severity": "medium",
                "message": "Búsqueda lineal dentro de bucle — considera usar Conjunto o Mapa"
            })

        # Large string concat in loops
        if re.search(r'(\w+)\s*=\s*\1\s*\+', s) and loop_depth > 0:
            hotspots.append({
                "line": i, "type": "string_concat_in_loop",
                "severity": "low",
                "message": "Concatenación en bucle — considera lista + unir()"
            })

    # Sort functions by call count
    top_calls = sorted(fn_calls.items(), key=lambda x: x[1], reverse=True)[:10]

    return {"hotspots": hotspots, "top_calls": top_calls, "total_lines": len(lines)}

def generate_html_report(profile_data: dict, output_path: Path):
    elapsed = profile_data.get("elapsed", 0)
    hotspots = profile_data.get("hotspots", [])
    top_calls = profile_data.get("top_calls", [])

    severity_colors = {"high": "#dc3545", "medium": "#ffc107", "low": "#17a2b8"}

    hotspot_rows = ""
    for h in hotspots:
        color = severity_colors.get(h["severity"], "#6c757d")
        hotspot_rows += f'<tr><td>{h["line"]}</td><td style="color:{color}">{h["severity"].upper()}</td><td>{h["type"]}</td><td>{h["message"]}</td></tr>'

    call_rows = ""
    if top_calls:
        max_calls = top_calls[0][1] if top_calls else 1
        for fn, count in top_calls:
            pct = count / max_calls * 100
            call_rows += f'<tr><td>{fn}</td><td>{count}</td><td><div style="background:#007bff;height:12px;width:{pct:.0f}%;border-radius:3px;min-width:2px"></div></td></tr>'

    html = f"""<!DOCTYPE html>
<html lang="es"><head><meta charset="UTF-8">
<title>C-Forge Profile Report</title>
<style>
body{{font-family:-apple-system,sans-serif;margin:2rem;background:#f8f9fa}}
h1{{color:#333}}.card{{background:white;padding:1.5rem;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,.1);margin-bottom:1.5rem}}
table{{width:100%;border-collapse:collapse}}th{{background:#343a40;color:white;padding:.6rem 1rem;text-align:left}}
td{{padding:.5rem 1rem;border-bottom:1px solid #dee2e6}}.stat{{font-size:2rem;font-weight:bold;color:#007bff}}
</style></head><body>
<h1>C-Forge Profile Report</h1>
<div class="card">
  <h2>Resumen de ejecución</h2>
  <div class="stat">{elapsed:.3f}s</div>
  <p>Tiempo total de ejecución &nbsp;|&nbsp; {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
  <p>Estado: {'✅ Exitoso' if profile_data.get('success') else '❌ Error'}</p>
</div>
<div class="card">
  <h2>Hotspots ({len(hotspots)})</h2>
  {'<p style="color:#28a745">✓ Sin hotspots detectados</p>' if not hotspots else ''}
  <table><tr><th>Línea</th><th>Severidad</th><th>Tipo</th><th>Descripción</th></tr>{hotspot_rows}</table>
</div>
<div class="card">
  <h2>Funciones más llamadas</h2>
  <table><tr><th>Función</th><th>Llamadas</th><th>Frecuencia relativa</th></tr>{call_rows}</table>
</div>
</body></html>"""

    output_path.write_text(html, encoding="utf-8")

def main():
    import argparse
    p = argparse.ArgumentParser(description=f"cfprofile v{VERSION} — Profiler para C-Forge")
    p.add_argument("file", help="Archivo .cfv a perfilar")
    p.add_argument("--output", "-o", default="profile_report.html")
    p.add_argument("--stdlib", default=os.environ.get("CFORGE_STDLIB", "./stdlib"))
    p.add_argument("--interp", default="./cforgev")
    p.add_argument("--runs", type=int, default=1, help="Número de ejecuciones para promediar")
    p.add_argument("--version", action="version", version=f"cfprofile {VERSION}")
    args = p.parse_args()

    cfv = Path(args.file)
    if not cfv.exists():
        print(f"Error: {cfv} no existe", file=sys.stderr)
        sys.exit(1)

    print(f"\033[34mcfprofile v{VERSION} — Perfilando {cfv}...\033[0m")

    # Analyze source
    source = cfv.read_text(encoding="utf-8")
    analysis = analyze_source(source)

    # Run program
    total_time = 0
    success = True
    for i in range(args.runs):
        result = run_with_profile(str(cfv), args.stdlib, args.interp)
        total_time += result["elapsed"]
        if not result["success"]:
            success = False
            print(f"\033[31mError en ejecución {i+1}: {result['stderr'][:200]}\033[0m")

    avg_time = total_time / args.runs

    profile_data = {
        "file": str(cfv),
        "elapsed": avg_time,
        "runs": args.runs,
        "success": success,
        "hotspots": analysis["hotspots"],
        "top_calls": analysis["top_calls"],
        "total_lines": analysis["total_lines"]
    }

    output = Path(args.output)
    generate_html_report(profile_data, output)

    print(f"\033[32m✓ Tiempo promedio: {avg_time:.3f}s ({args.runs} ejecución(es))\033[0m")
    if analysis["hotspots"]:
        high = sum(1 for h in analysis["hotspots"] if h["severity"] == "high")
        print(f"\033[33m⚠ {len(analysis['hotspots'])} hotspot(s) detectados ({high} críticos)\033[0m")
    print(f"  Reporte: {output}")

if __name__ == "__main__":
    main()
