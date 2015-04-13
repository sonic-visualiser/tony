#!/bin/bash
# 
# Run this from the build root

set -e

usage() {
    echo
    echo "Usage:"
    echo
    echo "$0 <version> <architecture>"
    echo
    echo "For example: $0 1.0cc1-1 amd64"
    echo
    exit 2
}

set -u

version="$1"
arch="$2"

if [ -z "$version" ] || [ -z "$arch" ]; then
    usage
fi

program=tony
plugins="chp pyin"

depdir=deploy/linux

targetdir="${program}_${version}_${arch}"

echo "Target dir is $targetdir"

if [ -d "$targetdir" ]; then
    echo "Target directory exists, not overwriting"
    exit
fi

mkdir "$targetdir"

cp -r "$depdir"/deb-skeleton/* "$targetdir"/

mkdir -p "$targetdir"/usr/bin "$targetdir"/usr/share/pixmaps

cp "$program" "$targetdir"/usr/bin/

for plug in $plugins; do
    cp "$plug/$plug.so" "$targetdir"/usr/lib/"$program"/
done

cp icons/"$program"-128x128.png "$targetdir"/usr/share/pixmaps/
cp "$program".desktop "$targetdir"/usr/share/applications/
cp README "$targetdir"/usr/share/doc/"$program"/

perl -i -p -e "s/Architecture: .*/Architecture: $arch/" "$targetdir"/DEBIAN/control

deps=`bash "$depdir"/debian-dependencies.sh "$program"`

perl -i -p -e "s/Depends: .*/$deps/" "$targetdir"/DEBIAN/control

bash "$depdir"/fix-lintian-bits.sh "$targetdir"

sudo dpkg-deb --build "$targetdir" && lintian "$targetdir".deb

