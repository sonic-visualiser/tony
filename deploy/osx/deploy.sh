#!/bin/bash

set -e

# Execute this from the top-level directory of the project (the one
# that contains the .app bundle).  Supply the name of the .app bundle
# as argument (the target will use $app.app regardless, but we need
# to know the source)

source="$1"
dmg="$2"
if [ -z "$source" ] || [ ! -d "$source" ] || [ -z "$dmg" ]; then
	echo "Usage: $0 <source.app> <target-dmg-basename>"
	echo "  e.g. $0 MyApplication.app MyApplication"
 	echo "  Version number and .dmg will be appended automatically,"
        echo "  but the .app name must include .app"
	exit 2
fi
app=`basename "$source" .app`

set -u

version=`perl -p -e 's/^[^"]*"([^"]*)".*$/$1/' version.h`
stem=${version%%-*}
case "$stem" in
    [0-9].[0-9]) bundleVersion="$stem".0 ;;
    [0-9].[0-9].[0-9]) bundleVersion="$stem" ;;
    *) echo "Error: Version stem $stem (of version $version) is neither two- nor three-part number"; exit 1 ;;
esac

if file "$source/Contents/MacOS/$app" | grep -q script; then
    echo
    echo "Executable is already a script, leaving it alone."
else
    echo
    echo "Moving aside executable, adding script."

    mv "$source/Contents/MacOS/$app" "$source/Contents/MacOS/$app.bin" || exit 1
    cp "deploy/osx/$app.sh" "$source/Contents/MacOS/$app" || exit 1
    chmod +x "$source/Contents/MacOS/$app"
fi

echo
echo "Copying in plugins from pyin/pyin.dylib and chp/chp.dylib."
echo "(make sure they're present, up-to-date and compiled with optimisation!)"

cp libpyin.dylib "$source/Contents/Resources/pyin.dylib"
cp libchp.dylib "$source/Contents/Resources/chp.dylib"

echo
echo "Copying in frameworks and plugins from Qt installation directory."

deploy/osx/copy-qt.sh "$app" || exit 2

echo
echo "Fixing up paths."

deploy/osx/paths.sh "$app"

echo
echo "Copying in qt.conf to set local-only plugin paths."
echo "Make sure all necessary Qt plugins are in $source/Contents/plugins/*"
echo "You probably want platforms/, accessible/ and imageformats/ subdirectories."
cp deploy/osx/qt.conf "$source"/Contents/Resources/qt.conf

echo
echo "Writing version $bundleVersion in to bundle."
echo "(This should be a three-part number: major.minor.point)"

perl -p -e "s/TONY_VERSION/$bundleVersion/" deploy/osx/Info.plist \
    > "$source"/Contents/Info.plist

echo "Done: check $source/Contents/Info.plist for sanity please"

echo
echo "Making target tree."

volume="$app"-"$version"
target="$volume"/"$app".app
dmg="$dmg"-"$version".dmg

mkdir "$volume" || exit 1

ln -s /Applications "$volume"/Applications
cp README.md "$volume/README.txt"
cp README.OSC "$volume/README-OSC.txt"
cp COPYING "$volume/COPYING.txt"
cp CHANGELOG "$volume/CHANGELOG.txt"
cp CITATION "$volume/CITATION.txt"
cp -rp "$source" "$target"

echo "Done"

echo
echo "Code-signing volume..."

deploy/osx/sign.sh "$volume" || exit 1

echo "Done"

echo
echo "Making dmg..."

hdiutil create -srcfolder "$volume" "$dmg" -volname "$volume" && 
	rm -r "$volume"

echo "Done"
