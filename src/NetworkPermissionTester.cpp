/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "NetworkPermissionTester.h"

#include "../version.h"

#include <QWidget>
#include <QString>
#include <QSettings>
#include <QCoreApplication>
#include <QDialog>
#include <QGridLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QCheckBox>

bool
NetworkPermissionTester::havePermission()
{
    QSettings settings;
    settings.beginGroup("Preferences");
    
    QString tag = QString("network-permission-%1").arg(TONY_VERSION);

    bool permish = false;

    if (settings.contains(tag)) {
	permish = settings.value(tag, false).toBool();
    } else {

	QDialog d;
	d.setWindowTitle(QCoreApplication::translate
                         ("NetworkPermissionTester", 
                          "Tony: a tool for melody annotation"));

	QGridLayout *layout = new QGridLayout;
	d.setLayout(layout);

	QLabel *label = new QLabel;
	label->setWordWrap(true);
	label->setText
	    (QCoreApplication::translate
	     ("NetworkPermissionTester",
	      "<h2>Tony: a tool for melody annotation</h2>"
	      "<p><img src=\":icons/qm-logo-smaller.png\" style=\"float:right\">Tony is a program for computer-aided melody annotation.<br>This is a very preliminary release, with many unfinished and provisional features.</p>"
	      "<p>Developed in the Centre for Digital Music at Queen Mary, University of London, Tony is provided free as open source software under the GNU General Public License.</p>"
              "<p><hr></p>"
	      "<p><b>Before we go on...</b></p>"
	      "<p>Tony needs to make occasional network requests to our servers.</p>"
	      "<p>This is to:</p>"
	      "<ul><li> tell you when updates are available.</li></ul>"
	      "<p>No personal information will be sent, no tracking is carried out, and all requests happen in the background without interrupting your work.</p>"
	      "<p>We recommend that you allow this. But if you do not wish to do so, please un-check the box below.<br></p>"));
	layout->addWidget(label, 0, 0);

	QCheckBox *cb = new QCheckBox(QCoreApplication::translate("NetworkPermissionTester", "Allow this"));
	cb->setChecked(true);
	layout->addWidget(cb, 1, 0);
	
	QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok);
	QObject::connect(bb, SIGNAL(accepted()), &d, SLOT(accept()));
	layout->addWidget(bb, 2, 0);
	
	d.exec();

	bool permish = cb->isChecked();
	settings.setValue(tag, permish);
    }

    settings.endGroup();

    return permish;
}

   

