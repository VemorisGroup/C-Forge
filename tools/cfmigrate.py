#!/usr/bin/env python3
"""cfmigrate — Gestor de migraciones de base de datos para C-Forge"""

import sys, os, re, json, sqlite3, hashlib
from pathlib import Path
from datetime import datetime

VERSION = "3.0.0"

MIGRATIONS_DIR = Path("migrations")
HISTORY_TABLE = "__cforge_migrations"

def get_db(db_path: str):
    conn = sqlite3.connect(db_path)
    conn.execute(f"""CREATE TABLE IF NOT EXISTS {HISTORY_TABLE} (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        version TEXT NOT NULL UNIQUE,
        name TEXT NOT NULL,
        hash TEXT NOT NULL,
        applied_at TEXT NOT NULL,
        rolled_back INTEGER DEFAULT 0
    )""")
    conn.commit()
    return conn

def list_migrations():
    if not MIGRATIONS_DIR.exists():
        return []
    files = sorted(MIGRATIONS_DIR.glob("*.sql")) + sorted(MIGRATIONS_DIR.glob("*.cfv"))
    return files

def get_version(filename: str) -> str:
    m = re.match(r'^(\d+)', Path(filename).name)
    return m.group(1) if m else "0"

def get_applied(conn):
    rows = conn.execute(f"SELECT version FROM {HISTORY_TABLE} WHERE rolled_back=0").fetchall()
    return {r[0] for r in rows}

def apply_migration(conn, migration_path: Path, dry_run=False):
    version = get_version(migration_path.name)
    name = migration_path.stem
    content = migration_path.read_text(encoding="utf-8")
    content_hash = hashlib.md5(content.encode()).hexdigest()

    if migration_path.suffix == ".sql":
        if dry_run:
            print(f"  [DRY RUN] Aplicaría: {migration_path.name}")
            print(f"  SQL: {content[:200]}...")
            return True
        try:
            # Split by semicolon and execute each statement
            statements = [s.strip() for s in content.split(";") if s.strip()]
            for stmt in statements:
                if stmt and not stmt.startswith("--"):
                    conn.execute(stmt)
            conn.execute(f"INSERT INTO {HISTORY_TABLE} (version, name, hash, applied_at) VALUES (?,?,?,?)",
                        (version, name, content_hash, datetime.now().isoformat()))
            conn.commit()
            print(f"  \033[32m✓ {migration_path.name}\033[0m")
            return True
        except Exception as e:
            conn.rollback()
            print(f"  \033[31m✗ {migration_path.name}: {e}\033[0m")
            return False
    return True

def rollback_migration(conn, migration_path: Path):
    version = get_version(migration_path.name)
    # Look for corresponding rollback file
    rollback = migration_path.parent / f"{migration_path.stem}_rollback.sql"
    if rollback.exists():
        content = rollback.read_text(encoding="utf-8")
        try:
            statements = [s.strip() for s in content.split(";") if s.strip()]
            for stmt in statements:
                if stmt:
                    conn.execute(stmt)
            conn.execute(f"UPDATE {HISTORY_TABLE} SET rolled_back=1 WHERE version=?", (version,))
            conn.commit()
            print(f"  \033[33m↩ Rollback: {migration_path.name}\033[0m")
            return True
        except Exception as e:
            conn.rollback()
            print(f"  \033[31m✗ Rollback fallido: {e}\033[0m")
            return False
    else:
        print(f"  \033[31m✗ No hay archivo rollback para {migration_path.name}\033[0m")
        return False

def create_migration(name: str):
    MIGRATIONS_DIR.mkdir(exist_ok=True)
    existing = list_migrations()
    next_num = len(existing) + 1
    timestamp = datetime.now().strftime("%Y%m%d%H%M%S")
    slug = re.sub(r'[^a-z0-9]+', '_', name.lower())
    filename = f"{next_num:04d}_{timestamp}_{slug}.sql"
    path = MIGRATIONS_DIR / filename
    rollback_path = MIGRATIONS_DIR / f"{next_num:04d}_{timestamp}_{slug}_rollback.sql"

    path.write_text(f"""-- Migración: {name}
-- Creada: {datetime.now().isoformat()}
-- Versión: {next_num:04d}

-- Escribe tu SQL de migración aquí:
-- CREATE TABLE ejemplo (
--     id INTEGER PRIMARY KEY AUTOINCREMENT,
--     nombre TEXT NOT NULL,
--     creado_en TEXT DEFAULT CURRENT_TIMESTAMP
-- );

""")
    rollback_path.write_text(f"""-- Rollback de: {name}

-- Escribe el SQL para revertir la migración:
-- DROP TABLE IF EXISTS ejemplo;

""")

    print(f"\033[32m✓ Creadas migraciones:\033[0m")
    print(f"  {path}")
    print(f"  {rollback_path}")
    return path

def status_report(conn, migrations):
    applied = get_applied(conn)
    print(f"\n{'Versión':<12} {'Estado':<12} {'Nombre'}")
    print("-" * 60)
    for m in migrations:
        v = get_version(m.name)
        estado = "\033[32mapplicada\033[0m" if v in applied else "\033[33mpendiente\033[0m"
        print(f"{v:<12} {estado:<20} {m.stem}")

    pending = [m for m in migrations if get_version(m.name) not in applied]
    print(f"\n{len(applied)} aplicadas, {len(pending)} pendientes")

def main():
    import argparse
    p = argparse.ArgumentParser(description=f"cfmigrate v{VERSION} — Gestor de migraciones")
    sub = p.add_subparsers(dest="cmd")

    sub_up = sub.add_parser("up", help="Aplicar migraciones pendientes")
    sub_up.add_argument("--db", default="cforge.db")
    sub_up.add_argument("--steps", type=int, help="Número de pasos")
    sub_up.add_argument("--dry-run", action="store_true")

    sub_down = sub.add_parser("down", help="Revertir última migración")
    sub_down.add_argument("--db", default="cforge.db")
    sub_down.add_argument("--steps", type=int, default=1)

    sub_new = sub.add_parser("new", help="Crear nueva migración")
    sub_new.add_argument("name", help="Nombre descriptivo")

    sub_status = sub.add_parser("status", help="Ver estado de migraciones")
    sub_status.add_argument("--db", default="cforge.db")

    sub_reset = sub.add_parser("reset", help="Revertir TODAS las migraciones")
    sub_reset.add_argument("--db", default="cforge.db")
    sub_reset.add_argument("--confirm", action="store_true")

    p.add_argument("--version", action="version", version=f"cfmigrate {VERSION}")
    args = p.parse_args()

    if args.cmd == "new":
        create_migration(args.name)
        return

    if not args.cmd:
        p.print_help()
        return

    db_path = getattr(args, "db", "cforge.db")
    conn = get_db(db_path)
    migrations = list_migrations()

    if args.cmd == "status":
        status_report(conn, migrations)

    elif args.cmd == "up":
        applied = get_applied(conn)
        pending = [m for m in migrations if get_version(m.name) not in applied]
        if args.steps:
            pending = pending[:args.steps]
        if not pending:
            print("\033[32m✓ Todas las migraciones están aplicadas\033[0m")
            return
        print(f"\033[34mcfmigrate — Aplicando {len(pending)} migración(es)...\033[0m")
        for m in pending:
            apply_migration(conn, m, dry_run=getattr(args, "dry_run", False))

    elif args.cmd == "down":
        applied = get_applied(conn)
        to_rollback = [m for m in reversed(migrations) if get_version(m.name) in applied]
        steps = getattr(args, "steps", 1)
        for m in to_rollback[:steps]:
            rollback_migration(conn, m)

    elif args.cmd == "reset":
        if not getattr(args, "confirm", False):
            print("\033[31mUsa --confirm para revertir TODAS las migraciones\033[0m")
            return
        applied = get_applied(conn)
        for m in reversed(migrations):
            if get_version(m.name) in applied:
                rollback_migration(conn, m)

    conn.close()

if __name__ == "__main__":
    main()
