#!/usr/bin/env python3
"""SPEC-007 R9.2 (#734 E8): the protocol table of the specification names the
protocol the engine speaks.

The table maps each build-program protocol to the first mcpp release that
carries it, and plugins state their needs as releases (R9.8). A protocol bump
that does not add its row leaves plugin authors with no release to name, so the
table's newest row must be the engine's `kProtocolVersion`.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def engine_protocol() -> int:
    text = (ROOT / "modules/buildmcpp/src/program_protocol.cppm").read_text()
    m = re.search(r"inline constexpr int kProtocolVersion = (\d+);", text)
    assert m, "kProtocolVersion not found"
    return int(m.group(1))


def table_protocols() -> list[tuple[int, str]]:
    text = (ROOT / "docs/specs/build-plugins.md").read_text()
    rows = re.findall(r"^\s*\|\s*(\d+)\s*\|\s*(\d{4}\.\d+\.\d+\.\d+)\s*\|", text, re.M)
    return [(int(p), r) for p, r in rows]


def main() -> int:
    have = engine_protocol()
    rows = table_protocols()
    if not rows:
        print("FAIL: SPEC-007 has no protocol table")
        return 1
    newest = max(p for p, _ in rows)
    if newest != have:
        print(f"FAIL: SPEC-007's newest protocol row is {newest}; the engine speaks {have}")
        return 1
    print(f"OK: SPEC-007 names protocol {have} and its first release")
    return 0


if __name__ == "__main__":
    sys.exit(main())
