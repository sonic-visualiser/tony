#!/bin/bash

# Execute this from the top-level directory of the project (the one
# that contains the .app bundle).  Supply the name of the .app bundle
# as argument (the target will use $app.app regardless, but we need
# to know the source)

#!!! Special args for constructing experimental scripts. Do not merge
#!!! to default branch.
shext=""
if [ "$1" = "--no-sonification" ]; then
    shext=".no-sonification"
    shift
elif [ "$1" = "--no-spectrogram" ]; then
    shext=".no-spectrogram"
    shift
elif [ "$1" = "--all-features" ]; then
    shext=""
    shift
else
    echo "Error: Must supply one of --all-features, --no-sonification, --no-spectrogram"
    exit 2
fi

echo "Shell file extension: $shext"

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

version=`perl -p -e 's/^[^"]*"([^"]*)".*$/$1/' version.h`
case "$version" in
    [0-9].[0-9]) bundleVersion="$version".0 ;;
    [0-9].[0-9].[0-9]) bundleVersion="$version" ;;
    *) echo "Error: Version $version is neither two- nor three-part number" ;;
esac

if file "$source/Contents/MacOS/$app" | grep -q script; then
    echo
    echo "Executable is already a script, replacing it."

    cp "deploy/osx/$app.sh$shext" "$source/Contents/MacOS/$app" || exit 1
    chmod +x "$source/Contents/MacOS/$app"
else
    echo
    echo "Moving aside executable, adding script."

    mv "$source/Contents/MacOS/$app" "$source/Contents/MacOS/$app.bin" || exit 1
    cp "deploy/osx/$app.sh$shext" "$source/Contents/MacOS/$app" || exit 1
    chmod +x "$source/Contents/MacOS/$app"
fi

echo
echo "Copying in plugins from pyin/pyin.dylib and chp/chp.dylib."
echo "(make sure they're present, up-to-date and compiled with optimisation!)"

cp pyin/pyin.{dylib,cat,n3} chp/chp.{dylib,cat,n3} "$source/Contents/Resources/"

echo
echo "Fixing up paths."

deploy/osx/paths.sh "$app"

echo
echo "Making target tree."

volume="$app"-"$version"
target="$volume"/"$app".app
dmg="$dmg"-"$version$shext".dmg

mkdir "$volume" || exit 1

ln -s /Applications "$volume"/Applications
cp README README.OSC COPYING CHANGELOG "$volume/"
cp -r "$source" "$target"

echo "Done"

echo
echo "Copying in qt.conf to set local-only plugin paths."
echo "Make sure all necessary Qt plugins are in $target/Contents/plugins/*"
echo "You probably want platforms/, accessible/ and imageformats/ subdirectories."
cp deploy/osx/qt.conf "$target"/Contents/Resources/qt.conf

echo
echo "Writing version $bundleVersion in to bundle."
echo "(This should be a three-part number: major.minor.point)"

perl -p -e "s/TONY_VERSION/$bundleVersion/" deploy/osx/Info.plist \
    > "$target"/Contents/Info.plist

echo "Done: check $target/Contents/Info.plist for sanity please"

deploy/osx/sign.sh "$volume" || exit 1

echo
echo "Making dmg..."

hdiutil create -srcfolder "$volume" "$dmg" -volname "$volume" && 
	rm -r "$volume"

echo "Done"
