#!/usr/bin/env python3
"""Fail if JUCE code, modules, or Projucer files are in the tree.

Markdown under docs/ may discuss the port. Everything else is source.
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NEEDLES = ("juce::", "JuceHeader", "juce_", "JUCE_MODULE", "find_package(JUCE")
SKIP_DIRS = {".git", "build", "build-vs", ".vs", "docs"}
SKIP_FILES = {"NoJuce.cmake", "check_no_juce.py"}
SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx", ".cmake", ".txt", ".yml", ".yaml"}

hits: list[str] = []
for path in ROOT.rglob("*"):
    if not path.is_file():
        continue
    if path.name in SKIP_FILES:
        continue
    if any(part in SKIP_DIRS or part.startswith("build") for part in path.parts):
        continue
    if path.suffix == ".jucer":
        hits.append(f"{path}: Projucer file")
        continue
    if path.name == "CMakeLists.txt" or path.suffix in SUFFIXES:
        text = path.read_text(errors="ignore")
        for needle in NEEDLES:
            if needle in text:
                hits.append(f"{path}: found {needle}")
                break

if hits:
    print("JUCE gate failed:")
    print("\n".join(hits))
    sys.exit(1)
print("JUCE gate passed")
