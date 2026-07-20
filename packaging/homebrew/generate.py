#!/usr/bin/env python3
"""Genera la fórmula Homebrew para un tag y su archivo fuente."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("version")
parser.add_argument("archive", type=Path)
parser.add_argument("-o", "--output", type=Path, default=Path("dist/Formula/cforgev.rb"))
args = parser.parse_args()
template = Path(__file__).with_name("Formula").joinpath("cforgev.rb.template").read_text(
    encoding="utf-8"
)
digest = hashlib.sha256(args.archive.read_bytes()).hexdigest()
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(
    template.replace("VERSION", args.version).replace("SHA256", digest), encoding="utf-8"
)
print(args.output)
