#!/usr/bin/env python3
"""Fail if Steinberg ASIO SDK files have been committed.

The SDK is fetched into the build tree or supplied with GHOVLZ_ASIO_SDK_DIR.
Its terms do not allow redistribution from this public repository.
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAMES = {
    "asio.h",
    "iasiodrv.h",
    "asiosys.h",
    "asiodrvr.h",
    "asiodrvr.cpp",
    "asiodrivers.h",
    "asiodrivers.cpp",
    "asiolist.h",
    "asiolist.cpp",
}
SKIP = {".git", "build", "build-vs", ".vs"}

hits: list[str] = []
for path in ROOT.rglob("*"):
    if not path.is_file():
        continue
    if any(part in SKIP or part.startswith("build") for part in path.parts):
        continue
    if path.name in NAMES or "ASIOSDK" in path.parts:
        hits.append(str(path.relative_to(ROOT)))

if hits:
    print("ASIO SDK gate failed. These files must not be committed:")
    print("\n".join(hits))
    sys.exit(1)
print("ASIO SDK gate passed")
