TEMPLATE = subdirs
SUBDIRS = sub_bq sub_dataquay svcore svgui svapp checker sub_tony

sub_bq.file = bq.pro
sub_tony.file = tonyapp.pro

sub_dataquay.file = dataquay/lib.pro

svgui.depends = svcore
svapp.depends = svcore svgui
sub_tony.depends = svcore svgui svapp
