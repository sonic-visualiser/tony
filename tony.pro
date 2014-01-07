TEMPLATE = subdirs
SUBDIRS = sub_dataquay svcore svgui svapp sub_tony

sub_tony.file = tonyapp.pro

sub_dataquay.file = dataquay/lib.pro

svgui.depends = svcore
svapp.depends = svcore svgui
sub_tony.depends = svcore svgui svapp
