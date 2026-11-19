#!/usr/bin/env python3
"""Compare the exported functions of the FrameLink libraries with a baseline.

Usage: check_exports.py <build-dir> [--write]
Same on every platform. Windows reads DLL exports with dumpbin (MSVC
environment); Linux reads the dynamic symbol table with nm. Baselines are
tools/exports/<platform>.txt. Any added or missing export fails: an addition is
reviewed and recorded with --write, a removal breaks the ABI.

Linux lists defined code and data symbols (nm types T, D, B, R). Weak, unique
and typeinfo symbols that libstdc++ headers force visible (W, V, u) are not
listed; the library's own inline functions and classes are hidden.
"""
import os
import pathlib
import subprocess
import sys

LIBRARIES = {
    "windows": ["framelink.dll", "framelink_gst_session.dll", "gstframelink.dll"],
    "linux": ["libframelink.so", "libframelink_gst_session.so", "libgstframelink.so"],
}


def windows_exports(path):
    out = subprocess.run(["dumpbin", "/nologo", "/exports", str(path)], check=True,
                         capture_output=True, text=True).stdout.splitlines()
    names, table = [], False
    for line in out:
        fields = line.split()
        if fields[:3] == ["ordinal", "hint", "RVA"]:
            table = True
        elif table and fields[:1] == ["Summary"]:
            break
        elif table and len(fields) >= 4 and fields[0].isdigit():
            names.append(fields[3])
    return names


def linux_exports(path):
    out = subprocess.run(["nm", "-D", "--defined-only", str(path)], check=True,
                         capture_output=True, text=True).stdout.splitlines()
    return [f[2] for f in (line.split() for line in out) if len(f) == 3 and f[1] in "TDBR"]


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--write"):
        sys.exit(__doc__)
    platform = "windows" if os.name == "nt" else "linux"
    build = pathlib.Path(sys.argv[1])
    read = windows_exports if platform == "windows" else linux_exports
    lines = []
    for library in LIBRARIES[platform]:
        path = build / library
        if not path.exists():
            sys.exit(f"missing {path}")
        names = sorted(set(read(path)))
        lines += [f"## {library} ({len(names)})"] + names
    baseline = pathlib.Path(__file__).with_name("exports") / f"{platform}.txt"
    if len(sys.argv) == 3:
        baseline.write_text("\n".join(lines) + "\n")
        print(f"wrote tools/exports/{baseline.name}")
        return
    expected = baseline.read_text().splitlines()
    if expected == lines:
        print(f"exports match {baseline.name}: " + ", ".join(l[3:] for l in lines if l.startswith("## ")))
        return
    import difflib
    sys.stdout.writelines(difflib.unified_diff([l + "\n" for l in expected], [l + "\n" for l in lines],
                                               f"tools/exports/{baseline.name}", "build"))
    sys.exit("exported symbols differ from the baseline")


if __name__ == "__main__":
    main()
