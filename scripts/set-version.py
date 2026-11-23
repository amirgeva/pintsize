#!/usr/bin/env python3
"""Set the game's version. The number lives in VERSION; this script copies it everywhere else.

    scripts/set-version.py 0.1.1 "Add a program icon for Windows."
    scripts/set-version.py --sync "What changed."
    scripts/set-version.py --check

The first form writes VERSION, then syncs. --sync copies the number already in VERSION. --check
changes nothing: it lists every file that disagrees with VERSION and exits with 1 if any does.

Synced files:
  CMakeLists.txt          reads VERSION itself; nothing to write
  vcpkg.json              "version-string"
  debian/changelog        a new top entry <version>-1, with the maintainer and distribution of the
                          entry below it and the note as its change line
  data/*.metainfo.xml     a new <release> with today's date and the note as its description
  data/pintsize.6         the .TH line's version and date

A new changelog entry or <release> needs a note saying what changed. Files that already have the
version are left alone, so running the script twice is safe. Line endings are kept as found.
"""
import argparse
import datetime
import email.utils
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
VERSION_FILE = ROOT / "VERSION"
VCPKG = ROOT / "vcpkg.json"
CHANGELOG = ROOT / "debian" / "changelog"
METAINFO = ROOT / "data" / "io.github.amirgeva.pintsize.metainfo.xml"
MANPAGE = ROOT / "data" / "pintsize.6"

VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")


def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        text = f.read()
    return text, ("\r\n" if "\r\n" in text else "\n")


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print(f"  updated {path.relative_to(ROOT).as_posix()}")


def vcpkg_version():
    text, _ = read(VCPKG)
    return re.search(r'"version-string"\s*:\s*"([^"]*)"', text).group(1)


def changelog_version():
    text, _ = read(CHANGELOG)
    return re.match(r"\S+ \(([^)]*)\)", text).group(1)


def metainfo_version():
    text, _ = read(METAINFO)
    m = re.search(r'<release version="([^"]*)"', text)
    return m.group(1) if m else None


def manpage_version():
    text, _ = read(MANPAGE)
    return re.search(r'^\.TH \S+ \S+ "[^"]*" "\S+ ([^"]*)"', text, re.M).group(1)


def mismatches(version):
    expected = {
        "vcpkg.json": (vcpkg_version(), version),
        "debian/changelog": (changelog_version(), f"{version}-1"),
        "data/io.github.amirgeva.pintsize.metainfo.xml": (metainfo_version(), version),
        "data/pintsize.6": (manpage_version(), version),
    }
    return [(name, have, want) for name, (have, want) in expected.items() if have != want]


def sync_vcpkg(version):
    text, _ = read(VCPKG)
    new = re.sub(r'("version-string"\s*:\s*")[^"]*(")', rf"\g<1>{version}\g<2>", text)
    if new != text:
        write(VCPKG, new)


def sync_changelog(version, note, today):
    text, nl = read(CHANGELOG)
    if changelog_version() == f"{version}-1":
        return
    head = re.match(r"(\S+) \([^)]*\) ([^;]+);", text)
    trailer = re.search(r"^ -- (.+?)  ", text, re.M)
    stamp = email.utils.format_datetime(today)
    entry = nl.join([
        f"{head.group(1)} ({version}-1) {head.group(2)}; urgency=medium",
        "",
        f"  * {note}",
        "",
        f" -- {trailer.group(1)}  {stamp}",
        "",
        "",
    ])
    write(CHANGELOG, entry + text)


def sync_metainfo(version, note, today):
    text, nl = read(METAINFO)
    if metainfo_version() == version:
        return
    m = re.search(r"^([ \t]*)<releases>[ \t]*\r?\n([ \t]*)", text, re.M)
    outer, inner = m.group(1), m.group(2)
    step = inner[len(outer):] or "  "
    lines = [
        f'{inner}<release version="{version}" date="{today:%Y-%m-%d}">',
        f"{inner}{step}<description>",
        f"{inner}{step * 2}<p>{note}</p>",
        f"{inner}{step}</description>",
        f"{inner}</release>",
    ]
    pos = m.end(0) - len(inner)
    write(METAINFO, text[:pos] + nl.join(lines) + nl + text[pos:])


def sync_manpage(version, today):
    text, _ = read(MANPAGE)
    new = re.sub(r'^(\.TH \S+ \S+ )"[^"]*" "(\S+) [^"]*"',
                 rf'\g<1>"{today:%Y-%m-%d}" "\g<2> {version}"', text, count=1, flags=re.M)
    if manpage_version() != version:
        write(MANPAGE, new)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("version", nargs="?", help="new version, MAJOR.MINOR.PATCH")
    p.add_argument("note", nargs="?", help="one line on what changed, for the changelog and metainfo")
    p.add_argument("--sync", metavar="NOTE", help="copy the version already in VERSION")
    p.add_argument("--check", action="store_true", help="report files that disagree; change nothing")
    args = p.parse_args()

    if args.check:
        version = VERSION_FILE.read_text(encoding="utf-8").strip()
        bad = mismatches(version)
        for name, have, want in bad:
            print(f"{name}: {have}, expected {want}")
        if not bad:
            print(f"all files agree on {version}")
        return 1 if bad else 0

    if args.sync is not None:
        if args.version:
            p.error("--sync takes the version from VERSION; give only the note")
        version, note = VERSION_FILE.read_text(encoding="utf-8").strip(), args.sync
    elif args.version:
        version, note = args.version, args.note
    else:
        p.error("give a version and a note, --sync NOTE, or --check")
    if not VERSION_RE.match(version):
        p.error(f"version must look like 1.2.3, not {version!r}")

    needs_note = changelog_version() != f"{version}-1" or metainfo_version() != version
    if needs_note and not note:
        p.error("a new changelog entry and <release> need a note saying what changed")

    today = datetime.datetime.now().astimezone().replace(microsecond=0)
    print(f"setting version {version}")
    if VERSION_FILE.read_text(encoding="utf-8").strip() != version:
        VERSION_FILE.write_text(version + "\n", encoding="utf-8")
        print("  updated VERSION")
    sync_vcpkg(version)
    sync_changelog(version, note, today)
    sync_metainfo(version, note, today)
    sync_manpage(version, today)

    bad = mismatches(version)
    for name, have, want in bad:
        print(f"error: {name} still says {have}, expected {want}", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
