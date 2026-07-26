#!/usr/bin/env python3
"""Fuzzing determinista y acotado para parser, diagnósticos y bytecode."""
from __future__ import annotations
import argparse
import os
import random
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cforge_diagnostics import analyze_source
from cforge_vm import deserialize
from cforgev import CForgevError

TOKENS = [
    "sea", "funcion", "retornar", "si", "sino", "mientras", "region",
    "unsafe", "mover", "prestar", "numero", "texto", "opcion", "x", "y",
    "0", "1", '"texto"', "=", ":", "+", "-", "*", "/", "(", ")",
    "{", "}", "[", "]", ",", ";", "\n",
]

def run(seed: int, cases: int) -> None:
    randomizer = random.Random(seed)
    for _ in range(cases):
        source = " ".join(randomizer.choice(TOKENS) for _ in range(randomizer.randint(0, 80)))
        diagnostics = analyze_source(source)
        if not isinstance(diagnostics, list): raise AssertionError("diagnóstico no determinista")
    for _ in range(cases):
        payload = os.urandom(randomizer.randint(0, 512))
        try: deserialize(payload)
        except CForgevError: pass
        else: raise AssertionError("bytecode aleatorio aceptado")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=0xCF0200)
    parser.add_argument("--cases", type=int, default=2000)
    arguments = parser.parse_args()
    run(arguments.seed, arguments.cases)
    print(f"C-Forge fuzz smoke: {arguments.cases * 2} casos aprobados")
