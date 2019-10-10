#!/bin/bash

set -eu

tag=`hg tags | grep '^v' | head -1 | awk '{ print $1; }'`

v=`echo "$tag" | sed 's/v//' | sed 's/_.*$//'`

current=$(hg id | awk '{ print $1; }')

case "$current" in
    *+) echo "ERROR: Current working copy has been modified - unmodified copy required so we can update to tag and back again safely"; exit 2;;
    *);;
esac
          
echo
echo -n "Packaging up version $v from tag $tag... "

hg update -r"$tag"

./repoint archive /tmp/tony-"$v".tar.gz --exclude sv-dependency-builds repoint.pri testdata

hg update -r"$current"

echo Done
echo
