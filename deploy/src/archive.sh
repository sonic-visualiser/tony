#!/bin/bash

tag=`hg tags | grep '^v[0-9]' | head -1 | awk '{ print $1; }'`

v=`echo "$tag" |sed 's/v//'`

if test -z "$v" ; then
    echo "No suitable tag found!?"
    exit 1
fi

echo "Packaging up version $v from tag $tag..."

hg archive -r"$tag" --subrepos --exclude sv-dependency-builds --exclude pyin/testdata --exclude testdata /tmp/tony-"$v".tar.gz

