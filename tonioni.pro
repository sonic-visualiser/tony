TEMPLATE = subdirs
SUBDIRS = svcore svgui svapp sub_tony

sub_tony.file = tonyapp.pro

svgui.depends = svcore
svapp.depends = svcore svgui
sub_tony.depends = svcore svgui svapp
