#!/bin/sh
# Gets the files raye-wine ships next to Wine and puts them where it looks
# for them, so nothing is downloaded when a program starts:
#
#   DXVK          Direct3D 8 to 11 on Vulkan    built from source by debian/rules
#   VKD3D-Proton  Direct3D 12 on Vulkan         share/wine/raye/<arch>-windows
#   DXVK-NVAPI    NVAPI for NVIDIA cards        share/wine/raye/<arch>-windows
#   Wine Mono     .NET programs                 share/wine/mono
#   Wine Gecko    web pages inside programs     share/wine/gecko
#
# third-party/sources.txt lists each file with the SHA-256 it must have.
# Nothing is used unless it matches. DXVK is a git checkout of the release
# tag (with its submodules) in third-party/cache/dxvk-<version>; the
# commit decides, as the SHA-256 does for the archives.
#
#   fetch-third-party.sh                 download what is missing
#   fetch-third-party.sh --check         check the cache, offline
#   fetch-third-party.sh --unpack PREFIX check, then unpack into PREFIX
#   fetch-third-party.sh --version KIND  the version of one kind
#
# CACHE=dir keeps the downloads somewhere else than third-party/cache.

set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
list=$here/third-party/sources.txt
cache=${CACHE:-$here/third-party/cache}

die() { echo "fetch-third-party: $*" >&2; exit 1; }

entries() { grep -v '^#' "$list" | grep -v '^[[:space:]]*$'; }

field() {   # field KIND N
    entries | awk -v k="$1" -v n="$2" '$1 == k { print $n; found=1 } END { exit !found }' ||
        die "no $1 in sources.txt"
}

check_one() {   # check_one FILE SHA256
    [ -f "$cache/$1" ] || return 1
    echo "$2  $cache/$1" | sha256sum -c --status
}

# a checkout of exactly that commit, nothing changed, every submodule there
check_git() {   # check_git DIR COMMIT
    [ -d "$1/.git" ] || [ -f "$1/.git" ] || return 1
    [ "$(git -C "$1" rev-parse HEAD 2>/dev/null)" = "$2" ] || return 1
    [ -z "$(git -C "$1" status --porcelain --ignore-submodules=none)" ] || return 1
    ! git -C "$1" submodule status --recursive | grep -q '^[-+U]'
}

fetch() {
    mkdir -p "$cache"
    entries | while read -r kind version sum url; do
        file=${url##*/}
        if [ "$kind" = dxvk-src ]; then
            dir=$cache/dxvk-$version
            check_git "$dir" "$sum" && continue
            echo "getting dxvk $version"
            rm -rf "$dir"
            git clone -q --depth 1 --branch "v$version" --recurse-submodules --shallow-submodules \
                "$url" "$dir" || die "could not clone $url"
            check_git "$dir" "$sum" || die "dxvk v$version is not commit $sum"
            continue
        fi
        check_one "$file" "$sum" && continue
        echo "getting $file"
        curl -fsSL --retry 3 -o "$cache/$file.part" "$url" || die "could not download $url"
        echo "$sum  $cache/$file.part" | sha256sum -c --status || {
            rm -f "$cache/$file.part"
            die "$file does not have the expected SHA-256"
        }
        mv "$cache/$file.part" "$cache/$file"
    done
}

check() {
    entries | while read -r kind version sum url; do
        if [ "$kind" = dxvk-src ]; then
            check_git "$cache/dxvk-$version" "$sum" ||
                die "dxvk-$version is missing or changed, run fetch-third-party.sh"
        else
            check_one "${url##*/}" "$sum" || die "${url##*/} is missing or changed, run fetch-third-party.sh"
        fi
    done
}

unpack() {
    prefix=$1
    check
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    data=$prefix/share/wine
    mkdir -p "$data/mono" "$data/gecko" "$data/raye/x86_64-windows" \
             "$data/raye/i386-windows" "$data/raye/licenses"
    : > "$data/raye/versions"
    entries | while read -r kind version sum url; do
        file=$cache/${url##*/}
        echo "$kind $version" >> "$data/raye/versions"
        case $kind in
            mono)
                tar -C "$data/mono" -xJf "$file" ;;
            gecko32|gecko64)
                tar -C "$data/gecko" -xJf "$file" ;;
            dxvk-src)
                ;;  # built and installed by debian/rules
            vkd3d)
                tar -C "$tmp" --zstd -xf "$file"
                cp "$tmp/vkd3d-proton-$version"/x64/*.dll "$data/raye/x86_64-windows/"
                cp "$tmp/vkd3d-proton-$version"/x86/*.dll "$data/raye/i386-windows/"
                cp "$tmp/vkd3d-proton-$version/LICENSE" "$data/raye/licenses/vkd3d-proton" 2>/dev/null || true ;;
            nvapi)
                mkdir "$tmp/nvapi"
                tar -C "$tmp/nvapi" -xzf "$file"
                cp "$tmp/nvapi/x64/nvapi64.dll" "$tmp/nvapi/x64/nvofapi64.dll" "$data/raye/x86_64-windows/"
                cp "$tmp/nvapi/x32/nvapi.dll" "$data/raye/i386-windows/"
                cp "$tmp/nvapi/LICENSE" "$data/raye/licenses/dxvk-nvapi" ;;
            *) die "sources.txt: unknown kind $kind" ;;
        esac
    done
    # wine.inf copies exactly these: a missing one would stop the copy.
    # DXVK's must be installed before this runs.
    for f in d3d8 d3d9 d3d10core d3d11 dxgi d3d12 d3d12core; do
        [ -f "$data/raye/x86_64-windows/$f.dll" ] && [ -f "$data/raye/i386-windows/$f.dll" ] ||
            die "$f.dll is missing after unpacking"
    done
    [ -f "$data/raye/x86_64-windows/nvapi64.dll" ] &&
        [ -f "$data/raye/x86_64-windows/nvofapi64.dll" ] && [ -f "$data/raye/i386-windows/nvapi.dll" ] ||
        die "nvapi is missing after unpacking"
}

case ${1:-} in
    "") fetch ;;
    --check) check ;;
    --unpack) [ $# -eq 2 ] || die "usage: --unpack PREFIX"; unpack "$2" ;;
    --version) [ $# -eq 2 ] || die "usage: --version KIND"; field "$2" 2 ;;
    *) echo "usage: fetch-third-party.sh [--check | --unpack PREFIX | --version KIND]" >&2; exit 2 ;;
esac
