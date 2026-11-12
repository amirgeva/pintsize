#!/bin/sh
# Build Pint-Size Prix and package it as a Debian source and binary package.
#
# Usage: scripts/build-deb.sh [options]
#   --install-deps   Install missing build dependencies with apt (uses sudo)
#   --binary-only    Build only the .deb, not the source package (.dsc)
#   --sign           Sign the source and .changes with your GPG key
#   --no-check       Skip the test suite (sets DEB_BUILD_OPTIONS=nocheck)
#   --no-lintian     Do not run lintian on the result
#   --out DIR        Where to put the results (default: build/deb)
#   -h, --help       Show this help
#
# The packaging lives in debian/. This script creates the upstream tarball
# (pintsize_<version>.orig.tar.gz) from the files git tracks, unpacks it in a
# clean directory, adds debian/ and runs dpkg-buildpackage there, so the
# result is the same as what a Debian buildd or sponsor would produce.
#
# Build on the oldest Debian/Ubuntu release you want to support: the
# package's library dependencies are computed from the system it is built on.
set -eu

die() { echo "build-deb: error: $*" >&2; exit 1; }
info() { echo "build-deb: $*"; }
usage() { sed -n '2,/^#   -h/s/^# \{0,1\}//p' "$0"; }

install_deps=0 binary_only=0 sign=0 no_check=0 lintian=1 out=
while [ $# -gt 0 ]; do
    case "$1" in
        --install-deps) install_deps=1 ;;
        --binary-only)  binary_only=1 ;;
        --sign)         sign=1 ;;
        --no-check)     no_check=1 ;;
        --no-lintian)   lintian=0 ;;
        --out)          [ $# -ge 2 ] || die "--out needs a directory"; out=$2; shift ;;
        --out=*)        out=${1#--out=} ;;
        -h|--help)      usage; exit 0 ;;
        *)              usage >&2; die "unknown option: $1" ;;
    esac
    shift
done

top=$(cd "$(dirname "$0")/.." && pwd)
cd "$top"
[ -f debian/control ] || die "debian/control not found in $top"
out=${out:-$top/build/deb}
mkdir -p "$out"
out=$(cd "$out" && pwd)

# --- Tools and build dependencies -------------------------------------------
for tool in dpkg-buildpackage dpkg-parsechangelog dpkg-checkbuilddeps tar gzip; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool not found (sudo apt install dpkg-dev)"
done

if ! missing=$(dpkg-checkbuilddeps 2>&1); then
    if [ "$install_deps" = 1 ]; then
        info "installing build dependencies"
        sudo apt-get update
        # Reads Build-Depends from debian/control (apt >= 1.1).
        sudo apt-get install -y --no-install-recommends dpkg-dev
        sudo apt-get build-dep -y "$top"
    else
        echo "$missing" >&2
        die "missing build dependencies; rerun with --install-deps or install them with: sudo apt-get build-dep ."
    fi
fi

# --- Version and upstream tarball -------------------------------------------
source=$(dpkg-parsechangelog -S Source)
version=$(dpkg-parsechangelog -S Version)
upstream=${version#*:}         # drop epoch
upstream=${upstream%-*}        # drop Debian revision
timestamp=$(dpkg-parsechangelog -S Timestamp)
orig="$out/${source}_${upstream}.orig.tar.gz"
srcdir="$out/${source}-${upstream}"

cmake_version=$(sed -n 's/^project([^ ]* VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
if [ -n "$cmake_version" ] && [ "$cmake_version" != "$upstream" ]; then
    die "debian/changelog says $upstream but CMakeLists.txt says $cmake_version"
fi

info "packaging $source $version"

# Upstream files: everything git tracks or would track, except debian/.
if git -C "$top" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if [ -n "$(git -C "$top" status --porcelain)" ]; then
        info "warning: working tree has uncommitted changes; they are included"
    fi
    list_files() {
        git -C "$top" ls-files -z --cached --others --exclude-standard \
            | tr '\0' '\n' | grep -v '^debian/' | while IFS= read -r f; do
                [ -e "$f" ] && printf '%s\n' "$f"   # skip deleted files
            done
    }
else
    list_files() {
        find . -path ./debian -prune -o -path ./build -prune -o -path './build-*' -prune \
            -o -path ./.git -prune -o -type f -print | sed 's|^\./||'
    }
fi

# Reproducible tarball: fixed order, owner and mtime (taken from the changelog).
tmplist=$(mktemp)
trap 'rm -f "$tmplist"' EXIT
list_files | LC_ALL=C sort > "$tmplist"
[ -s "$tmplist" ] || die "no source files found"
tar --create --format=gnu --no-recursion \
    --transform="s|^|${source}-${upstream}/|" \
    --owner=0 --group=0 --numeric-owner --mtime="@$timestamp" \
    --use-compress-program="gzip -9n" \
    --file="$orig.tmp" --files-from="$tmplist" || die "creating $orig failed"
mv "$orig.tmp" "$orig"
info "wrote $orig"

# --- Build in a clean tree --------------------------------------------------
rm -rf "$srcdir"
tar -xzf "$orig" -C "$out"
cp -a debian "$srcdir/debian"

set -- -us -uc
[ "$sign" = 1 ] && set --
if [ "$binary_only" = 1 ]; then set -- "$@" -b; else set -- "$@" -sa; fi
if [ "$no_check" = 1 ]; then
    DEB_BUILD_OPTIONS="${DEB_BUILD_OPTIONS:+$DEB_BUILD_OPTIONS }nocheck"
    export DEB_BUILD_OPTIONS
fi

info "running dpkg-buildpackage $*"
(cd "$srcdir" && dpkg-buildpackage "$@")

# --- Check the result -------------------------------------------------------
arch=$(dpkg-architecture -qDEB_HOST_ARCH)
changes="$out/${source}_${version#*:}_${arch}.changes"

if [ "$lintian" = 1 ]; then
    if command -v lintian >/dev/null 2>&1; then
        info "running lintian"
        lintian --info --display-info --pedantic "$changes" || \
            info "lintian reported problems (see above)"
    else
        info "lintian not installed; skipping (sudo apt install lintian)"
    fi
fi

info "done; packages are in $out:"
ls -1 "$out" | grep -E "^${source}_" | sed 's/^/  /'
