#!/usr/bin/env python3
"""Rebuilds the file list in Natron.jucer from the source/ tree.

The desktop CMake lists files explicitly; the Projucer project (used for the Android export)
needs the same files. This script mirrors source/ into the .jucer's MAINGROUP, one GROUP per
directory, keeping existing FILE ids so Projucer's diffs stay small. .cpp files are compiled,
everything else is listed for reference. Afterwards, regenerate the Android project:

    Projucer --resave natron/Natron.jucer

Usage: python3 natron/scripts/sync-jucer.py
"""

import pathlib
import random
import string
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
JUCER = ROOT / "Natron.jucer"
SOURCE = ROOT / "source"
EXTENSIONS = {".h", ".cpp"}


def new_id(taken):
    while True:
        candidate = "".join(random.choice(string.ascii_letters) for _ in range(6))
        if candidate not in taken:
            taken.add(candidate)
            return candidate


def build_group(directory, existing_ids, taken, group_ids):
    relative = directory.relative_to(ROOT).as_posix()
    group = ET.Element("GROUP", {"id": group_ids.get(relative) or "{" + new_id(taken) + "}", "name": directory.name})
    for child in sorted(p for p in directory.iterdir() if p.is_file() and p.suffix in EXTENSIONS):
        path = child.relative_to(ROOT).as_posix()
        ET.SubElement(group, "FILE", {
            "id": existing_ids.get(path) or new_id(taken),
            "name": child.name,
            "compile": "1" if child.suffix == ".cpp" else "0",
            "resource": "0",
            "file": path,
        })
    for child in sorted(p for p in directory.iterdir() if p.is_dir()):
        sub = build_group(child, existing_ids, taken, group_ids)
        if len(sub):
            group.append(sub)
    return group


def main():
    tree = ET.parse(JUCER)
    project = tree.getroot()
    main_group = project.find("MAINGROUP")

    existing_ids = {f.get("file"): f.get("id") for f in main_group.iter("FILE")}
    taken = set(existing_ids.values())

    # Remember group ids by the directory they mirror (derived from their first file).
    group_ids = {}
    for group in main_group.iter("GROUP"):
        first = group.find("FILE")
        if first is not None:
            group_ids[pathlib.PurePosixPath(first.get("file")).parent.as_posix()] = group.get("id")

    for child in list(main_group):
        main_group.remove(child)
    for directory in sorted(p for p in SOURCE.iterdir() if p.is_dir()):
        main_group.append(build_group(directory, existing_ids, taken, group_ids))

    ET.indent(tree, space="  ")
    tree.write(JUCER, encoding="UTF-8", xml_declaration=True)
    count = sum(1 for _ in main_group.iter("FILE"))
    print(f"{JUCER.name}: {count} files")


if __name__ == "__main__":
    main()
