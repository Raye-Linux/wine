#!/bin/sh
# Builds raye-wine: the Wine release named in UPSTREAM with the Raye
# patches on top, installed in /opt/raye-wine so it never meets another
# Wine on the system. Runs in a resolute system that has git and the build
# dependencies of debian/control (a chroot is fine).
#
#   ./build.sh             patched tree in work/, then the .deb in out/
#   ./build.sh --prepare   only the patched tree
#   ./build.sh --check     check that every patch applies, build nothing
#
# Patches come from patches/base, then patches/audio, then
# patches/capture, each folder in the order of its series file. DXVK is
# built from source too: patches/*/dxvk/*.patch go on it, same folder
# order, by name within a folder. A patch
# that does not apply stops the build. Patches are plain git diffs,
# binary ones included.
#
# JOBS=n   parallel jobs (default: every CPU)
# WORK=dir where the tree is made (default: ./work)
# CACHE=dir downloads (default: third-party/cache)

set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-$here/work}
jobs=${JOBS:-$(nproc)}
cache=${CACHE:-$here/third-party/cache}
out=$here/out

say() { printf '==> %s\n' "$*"; }
die() { echo "build.sh: $*" >&2; exit 1; }

upstream() { sed -n "s/^$1=//p" "$here/UPSTREAM"; }
version=$(upstream version)
url=$(upstream url)
sum=$(upstream sha256)
[ -n "$version" ] && [ -n "$url" ] && [ -n "$sum" ] || die "UPSTREAM is incomplete"
tarball=$cache/${url##*/}
tree=$work/raye-wine-$version

# patch files of one folder, in series order
series() {
    [ -f "$here/patches/$1/series" ] || return 0
    grep -v '^#' "$here/patches/$1/series" | grep -v '^[[:space:]]*$' | while read -r p; do
        [ -f "$here/patches/$1/$p" ] || die "patches/$1/series names $p, which is not there"
        echo "$here/patches/$1/$p"
    done
}

get_upstream() {
    mkdir -p "$cache"
    if ! echo "$sum  $tarball" | sha256sum -c --status 2>/dev/null; then
        say "getting wine-$version"
        curl -fsSL --retry 3 -o "$tarball.part" "$url" || die "could not download $url"
        echo "$sum  $tarball.part" | sha256sum -c --status || {
            rm -f "$tarball.part"
            die "the upstream tarball does not have the SHA-256 in UPSTREAM"
        }
        mv "$tarball.part" "$tarball"
    fi
}

# a fresh tree with every patch applied
prepare() {
    get_upstream
    rm -rf "$tree"
    mkdir -p "$work"
    tar -C "$work" -xJf "$tarball"
    mv "$work/wine-$version" "$tree"
    for folder in base audio capture; do
        for p in $(series "$folder"); do
            say "applying $folder/${p##*/}"
            # git apply takes binary hunks (icons); the ceiling keeps it
            # from treating this repository as the tree to patch
            GIT_CEILING_DIRECTORIES=$work git -C "$tree" apply --whitespace=nowarn "$p" ||
                die "$folder/${p##*/} does not apply to wine-$version"
        done
    done
    # Wine looks for exactly the Mono and Gecko versions it was made for
    for kind in mono gecko; do
        want=$(sed -n "s/^#define $(echo $kind | tr a-z A-Z)_VERSION \"\(.*\)\"/\1/p" \
               "$tree/dlls/appwiz.cpl/addons.c")
        have=$(sh "$here/tools/fetch-third-party.sh" --version "$([ $kind = gecko ] && echo gecko64 || echo mono)")
        [ "$want" = "$have" ] || die "wine-$version wants $kind $want, third-party/sources.txt has $have"
    done
}

# a patched copy of the DXVK checkout, next to the tree
prepare_dxvk() {
    v=$(sh "$here/tools/fetch-third-party.sh" --version dxvk-src)
    dest=$tree/third-party/dxvk
    rm -rf "$dest"
    mkdir -p "$tree/third-party"
    cp -a "$cache/dxvk-$v" "$dest"
    for folder in base audio capture; do
        for p in "$here/patches/$folder"/dxvk/*.patch; do
            [ -f "$p" ] || continue
            say "applying $folder/dxvk/${p##*/}"
            git -C "$dest" apply --whitespace=nowarn "$p" ||
                die "$folder/dxvk/${p##*/} does not apply to dxvk $v"
        done
    done
}

case ${1:-} in
    --check)
        WORK=$(mktemp -d) && work=$WORK && tree=$work/raye-wine-$version
        trap 'rm -rf "$work"' EXIT
        prepare
        if [ -d "$cache/dxvk-$(sh "$here/tools/fetch-third-party.sh" --version dxvk-src)" ]; then
            prepare_dxvk
        else
            say "no DXVK checkout in the cache yet: its patches were not tried"
        fi
        say "every patch applies"
        exit 0 ;;
    --prepare) prepare; exit 0 ;;
    "") ;;
    *) echo "usage: build.sh [--prepare | --check]" >&2; exit 2 ;;
esac

python3 -I -B -m unittest discover -q -s "$here/tests" || die "the tree's own tests fail"
prepare
CACHE=$cache sh "$here/tools/fetch-third-party.sh"
cp -a "$here/debian" "$here/tools" "$here/capture" "$here/release" "$tree/"
mkdir -p "$tree/third-party"
cp "$here/third-party/sources.txt" "$tree/third-party/"
cp -a "$cache" "$tree/third-party/cache"
prepare_dxvk

say "building raye-wine with $jobs jobs"
(cd "$tree" && DEB_BUILD_OPTIONS="parallel=$jobs ${DEB_BUILD_OPTIONS:-}" dpkg-buildpackage -b -us -uc)
mkdir -p "$out"
deb_version=$(dpkg-parsechangelog -l "$here/debian/changelog" -S Version)
mv "$work"/raye-wine*_"$deb_version"_*.deb "$work"/raye-wine_*.buildinfo "$work"/raye-wine_*.changes "$out"/ 2>/dev/null || true
python3 -I -B "$here/tools/check-debs.py" "$out"/raye-wine*_"$deb_version"_*.deb || die "a package is too big or files went missing"
say "done: $(ls "$out"/raye-wine*_"$deb_version"_*.deb)"
