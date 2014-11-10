/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006-2012 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "../version.h"

#include "MainWindow.h"
#include "NetworkPermissionTester.h"
#include "Analyser.h"

#include "framework/Document.h"
#include "framework/VersionTester.h"

#include "view/Pane.h"
#include "view/PaneStack.h"
#include "data/model/WaveFileModel.h"
#include "data/model/NoteModel.h"
#include "data/model/FlexiNoteModel.h"
#include "layer/FlexiNoteLayer.h"
#include "data/model/NoteModel.h"
#include "view/ViewManager.h"
#include "base/Preferences.h"
#include "layer/WaveformLayer.h"
#include "layer/TimeInstantLayer.h"
#include "layer/TimeValueLayer.h"
#include "layer/SpectrogramLayer.h"
#include "widgets/Fader.h"
#include "view/Overview.h"
#include "widgets/AudioDial.h"
#include "widgets/IconLoader.h"
#include "widgets/KeyReference.h"
#include "audioio/AudioCallbackPlaySource.h"
#include "audioio/AudioCallbackPlayTarget.h"
#include "audioio/PlaySpeedRangeMapper.h"
#include "base/Profiler.h"
#include "base/UnitDatabase.h"
#include "layer/ColourDatabase.h"
#include "base/Selection.h"

#include "rdf/RDFImporter.h"
#include "data/fileio/DataFileReaderFactory.h"
#include "data/fileio/CSVFormat.h"
#include "data/fileio/CSVFileWriter.h"
#include "data/fileio/MIDIFileWriter.h"
#include "rdf/RDFExporter.h"

#include "widgets/RangeInputDialog.h"
#include "widgets/ActivityLog.h"

// For version information
#include "vamp/vamp.h"
#include "vamp-sdk/PluginBase.h"
#include "plugin/api/ladspa.h"
#include "plugin/api/dssi.h"

#include <QApplication>
#include <QMessageBox>
#include <QGridLayout>
#include <QLabel>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QInputDialog>
#include <QStatusBar>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QPainter>

#include <iostream>
#include <cstdio>
#include <errno.h>

using std::vector;


MainWindow::MainWindow(bool withAudioOutput, bool withSonification, bool withSpectrogram) :
    MainWindowBase(withAudioOutput, false),
    m_overview(0),
    m_mainMenusCreated(false),
    m_playbackMenu(0),
    m_recentFilesMenu(0), 
    m_rightButtonMenu(0),
    m_rightButtonPlaybackMenu(0),
    m_deleteSelectedAction(0),
    m_ffwdAction(0),
    m_rwdAction(0),
    m_intelligentActionOn(true), //GF: !!! temporary
    m_activityLog(new ActivityLog()),
    m_keyReference(new KeyReference()),
    m_selectionAnchor(0),
    m_withSonification(withSonification),
    m_withSpectrogram(withSpectrogram)
{
    setWindowTitle(QApplication::applicationName());

#ifdef Q_OS_MAC
#if (QT_VERSION >= QT_VERSION_CHECK(5, 2, 0))
    setUnifiedTitleAndToolBarOnMac(true);
#endif
#endif

    UnitDatabase *udb = UnitDatabase::getInstance();
    udb->registerUnit("Hz");
    udb->registerUnit("dB");
    udb->registerUnit("s");

    ColourDatabase *cdb = ColourDatabase::getInstance();
    cdb->addColour(Qt::black, tr("Black"));
    cdb->addColour(Qt::darkRed, tr("Red"));
    cdb->addColour(Qt::darkBlue, tr("Blue"));
    cdb->addColour(Qt::darkGreen, tr("Green"));
    cdb->addColour(QColor(200, 50, 255), tr("Purple"));
    cdb->addColour(QColor(255, 150, 50), tr("Orange"));
    cdb->addColour(QColor(180, 180, 180), tr("Grey"));
    cdb->setUseDarkBackground(cdb->addColour(Qt::white, tr("White")), true);
    cdb->setUseDarkBackground(cdb->addColour(Qt::red, tr("Bright Red")), true);
    cdb->setUseDarkBackground(cdb->addColour(QColor(30, 150, 255), tr("Bright Blue")), true);
    cdb->setUseDarkBackground(cdb->addColour(Qt::green, tr("Bright Green")), true);
    cdb->setUseDarkBackground(cdb->addColour(QColor(225, 74, 255), tr("Bright Purple")), true);
    cdb->setUseDarkBackground(cdb->addColour(QColor(255, 188, 80), tr("Bright Orange")), true);

    Preferences::getInstance()->setResampleOnLoad(true);
    Preferences::getInstance()->setFixedSampleRate(44100);
    Preferences::getInstance()->setSpectrogramSmoothing
        (Preferences::SpectrogramInterpolated);
    Preferences::getInstance()->setNormaliseAudio(true);

    QSettings settings;

    settings.beginGroup("MainWindow");
    settings.setValue("showstatusbar", false);
    settings.endGroup();

    settings.beginGroup("Transformer");
    settings.setValue("use-flexi-note-model", true);
    settings.endGroup();

    settings.beginGroup("LayerDefaults");
    settings.setValue("waveform",
                      QString("<layer scale=\"%1\" channelMode=\"%2\"/>")
                      .arg(int(WaveformLayer::LinearScale))
                      .arg(int(WaveformLayer::MixChannels)));
    settings.endGroup();

    m_viewManager->setAlignMode(false);
    m_viewManager->setPlaySoloMode(false);
    m_viewManager->setToolMode(ViewManager::SelectMode);
    m_viewManager->setZoomWheelsEnabled(false);
    m_viewManager->setIlluminateLocalFeatures(true);
    m_viewManager->setShowWorkTitle(false);
    m_viewManager->setShowCentreLine(false);
    m_viewManager->setShowDuration(false);
    m_viewManager->setOverlayMode(ViewManager::GlobalOverlays);

    connect(m_viewManager, SIGNAL(selectionChangedByUser()),
	    this, SLOT(selectionChangedByUser()));

    QFrame *frame = new QFrame;
    setCentralWidget(frame);

    QGridLayout *layout = new QGridLayout;
    
    QScrollArea *scroll = new QScrollArea(frame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);

    // We have a pane stack: it comes with the territory. However, we
    // have a fixed and known number of panes in it -- it isn't
    // variable
    m_paneStack->setLayoutStyle(PaneStack::NoPropertyStacks);
    m_paneStack->setShowPaneAccessories(false);
    connect(m_paneStack, SIGNAL(doubleClickSelectInvoked(int)),
            this, SLOT(doubleClickSelectInvoked(int)));
    scroll->setWidget(m_paneStack);

    m_overview = new Overview(frame);
    m_overview->setPlaybackFollow(PlaybackScrollPage);
    m_overview->setViewManager(m_viewManager);
    m_overview->setFixedHeight(40);
#ifndef _WIN32
    // For some reason, the contents of the overview never appear if we
    // make this setting on Windows.  I have no inclination at the moment
    // to track down the reason why.
    m_overview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
#endif
    connect(m_overview, SIGNAL(contextHelpChanged(const QString &)),
            this, SLOT(contextHelpChanged(const QString &)));

    m_panLayer = new WaveformLayer;
    m_panLayer->setChannelMode(WaveformLayer::MergeChannels);
    m_panLayer->setAggressiveCacheing(true);
    m_overview->addLayer(m_panLayer);

    if (m_viewManager->getGlobalDarkBackground()) {
        m_panLayer->setBaseColour
            (ColourDatabase::getInstance()->getColourIndex(tr("Bright Green")));
    } else {
        m_panLayer->setBaseColour
            (ColourDatabase::getInstance()->getColourIndex(tr("Blue")));
    }        

    m_fader = new Fader(frame, false);
    connect(m_fader, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
    connect(m_fader, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));

    m_playSpeed = new AudioDial(frame);
    m_playSpeed->setMeterColor(Qt::darkBlue);
    m_playSpeed->setMinimum(0);
    m_playSpeed->setMaximum(200);
    m_playSpeed->setValue(100);
    m_playSpeed->setFixedWidth(24);
    m_playSpeed->setFixedHeight(24);
    m_playSpeed->setNotchesVisible(true);
    m_playSpeed->setPageStep(10);
    m_playSpeed->setObjectName(tr("Playback Speedup"));
    m_playSpeed->setDefaultValue(100);
    m_playSpeed->setRangeMapper(new PlaySpeedRangeMapper(0, 200));
    m_playSpeed->setShowToolTip(true);
    connect(m_playSpeed, SIGNAL(valueChanged(int)),
        this, SLOT(playSpeedChanged(int)));
    connect(m_playSpeed, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
    connect(m_playSpeed, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));

    // Gain controls
    m_gainAudio = new AudioDial(frame);
    m_gainAudio->setMeterColor(Qt::darkRed);
    m_gainAudio->setMinimum(-50);
    m_gainAudio->setMaximum(50);
    m_gainAudio->setValue(0);
    m_gainAudio->setDefaultValue(0);
    //m_gainAudio->setFixedWidth(40);
    m_gainAudio->setFixedWidth(24);
    m_gainAudio->setFixedHeight(24);
    m_gainAudio->setNotchesVisible(true);
    m_gainAudio->setPageStep(10);
    m_gainAudio->setObjectName(tr("Audio Track Gain"));
    m_gainAudio->setRangeMapper(new LinearRangeMapper(-50, 50, -25, 25, tr("dB")));
    m_gainAudio->setShowToolTip(true);
    connect(m_gainAudio, SIGNAL(valueChanged(int)),
            this, SLOT(audioGainChanged(int)));
    connect(m_gainAudio, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
    connect(m_gainAudio, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));

    if (m_withSonification)
    {   
        m_gainPitch = new AudioDial(frame);
        m_gainPitch->setMeterColor(Qt::darkRed);
        m_gainPitch->setMinimum(-50);
        m_gainPitch->setMaximum(50);
        m_gainPitch->setValue(0);
        m_gainPitch->setDefaultValue(0);
        m_gainPitch->setFixedWidth(24);
        m_gainPitch->setFixedHeight(24);
        m_gainPitch->setNotchesVisible(true);
        m_gainPitch->setPageStep(10);
        m_gainPitch->setObjectName(tr("Pitch Track Gain"));
        m_gainPitch->setRangeMapper(new LinearRangeMapper(-50, 50, -25, 25, tr("dB")));
        m_gainPitch->setShowToolTip(true);
        connect(m_gainPitch, SIGNAL(valueChanged(int)),
                this, SLOT(pitchGainChanged(int)));
        connect(m_gainPitch, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
        connect(m_gainPitch, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));

        m_gainNotes = new AudioDial(frame);
        m_gainNotes->setMeterColor(Qt::darkRed);
        m_gainNotes->setMinimum(-50);
        m_gainNotes->setMaximum(50);
        m_gainNotes->setValue(0);
        m_gainNotes->setDefaultValue(0);
        m_gainNotes->setFixedWidth(24);
        m_gainNotes->setFixedHeight(24);
        m_gainNotes->setNotchesVisible(true);
        m_gainNotes->setPageStep(10);
        m_gainNotes->setObjectName(tr("Note Gain"));
        m_gainNotes->setRangeMapper(new LinearRangeMapper(-50, 50, -25, 25, tr("dB")));
        m_gainNotes->setShowToolTip(true);
        connect(m_gainNotes, SIGNAL(valueChanged(int)),
                this, SLOT(notesGainChanged(int)));
        connect(m_gainNotes, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
        connect(m_gainNotes, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));
    }
    // End of Gain controls

    // Pan controls
    m_panAudio = new AudioDial(frame);
    m_panAudio->setMeterColor(Qt::darkGreen);
    m_panAudio->setMinimum(-100);
    m_panAudio->setMaximum(100);
    m_panAudio->setValue(-100);
    m_panAudio->setDefaultValue(-100);
    m_panAudio->setFixedWidth(24);
    //m_panAudio->setFixedWidth(40);
    m_panAudio->setFixedHeight(24);
    m_panAudio->setNotchesVisible(true);
    m_panAudio->setPageStep(10);
    m_panAudio->setObjectName(tr("Audio Track Pan"));
    m_panAudio->setRangeMapper(new LinearRangeMapper(-100, 100, -100, 100, tr("")));
    m_panAudio->setShowToolTip(true);
    connect(m_panAudio, SIGNAL(valueChanged(int)),
            this, SLOT(audioPanChanged(int)));
    connect(m_panAudio, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
    connect(m_panAudio, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));



    if (m_withSonification)
    {   
        m_panPitch = new AudioDial(frame);
        m_panPitch->setMeterColor(Qt::darkGreen);
        m_panPitch->setMinimum(-100);
        m_panPitch->setMaximum(100);
        m_panPitch->setValue(100);
        m_panPitch->setDefaultValue(100);
        m_panPitch->setFixedWidth(24);
        m_panPitch->setFixedHeight(24);
        m_panPitch->setNotchesVisible(true);
        m_panPitch->setPageStep(10);
        m_panPitch->setObjectName(tr("Pitch Track Pan"));
        m_panPitch->setRangeMapper(new LinearRangeMapper(-100, 100, -100, 100, tr("")));
        m_panPitch->setShowToolTip(true);
        connect(m_panPitch, SIGNAL(valueChanged(int)),
                this, SLOT(pitchPanChanged(int)));
        connect(m_panPitch, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
        connect(m_panPitch, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));

        m_panNotes = new AudioDial(frame);
        m_panNotes->setMeterColor(Qt::darkGreen);
        m_panNotes->setMinimum(-100);
        m_panNotes->setMaximum(100);
        m_panNotes->setValue(100);
        m_panNotes->setDefaultValue(100);
        m_panNotes->setFixedWidth(24);
        m_panNotes->setFixedHeight(24);
        m_panNotes->setNotchesVisible(true);
        m_panNotes->setPageStep(10);
        m_panNotes->setObjectName(tr("Note Pan"));
        m_panNotes->setRangeMapper(new LinearRangeMapper(-100, 100, -100, 100, tr("")));
        m_panNotes->setShowToolTip(true);
        connect(m_panNotes, SIGNAL(valueChanged(int)),
                this, SLOT(notesPanChanged(int)));
        connect(m_panNotes, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
        connect(m_panNotes, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));    
    }
    
    // End of Pan controls

    layout->setSpacing(4);
    layout->addWidget(m_overview, 0, 1);
    layout->addWidget(scroll, 1, 1);

    layout->setColumnStretch(1, 10);

    frame->setLayout(layout);

    m_analyser = new Analyser();
    connect(m_analyser, SIGNAL(layersChanged()),
            this, SLOT(updateLayerStatuses()));
    connect(m_analyser, SIGNAL(layersChanged()),
            this, SLOT(updateMenuStates()));

    setupMenus();
    setupToolbars();
    setupHelpMenu();

    statusBar();

    finaliseMenus();

    connect(m_viewManager, SIGNAL(activity(QString)),
            m_activityLog, SLOT(activityHappened(QString)));
    connect(m_playSource, SIGNAL(activity(QString)),
            m_activityLog, SLOT(activityHappened(QString)));
    connect(CommandHistory::getInstance(), SIGNAL(activity(QString)),
            m_activityLog, SLOT(activityHappened(QString)));
    connect(this, SIGNAL(activity(QString)),
            m_activityLog, SLOT(activityHappened(QString)));
    connect(this, SIGNAL(replacedDocument()), this, SLOT(documentReplaced()));
    connect(this, SIGNAL(sessionLoaded()), this, SLOT(analyseNewMainModel()));
    connect(this, SIGNAL(audioFileLoaded()), this, SLOT(analyseNewMainModel()));
    m_activityLog->hide();

    newSession();

    settings.beginGroup("MainWindow");
    settings.setValue("zoom-default", 512);
    settings.endGroup();
    zoomDefault();

    NetworkPermissionTester tester;
    bool networkPermission = tester.havePermission();
    if (networkPermission) {
        m_versionTester = new VersionTester
            ("sonicvisualiser.org", "latest-tony-version.txt", TONY_VERSION);
        connect(m_versionTester, SIGNAL(newerVersionAvailable(QString)),
                this, SLOT(newerVersionAvailable(QString)));
    } else {
        m_versionTester = 0;
    }
}

MainWindow::~MainWindow()
{
    delete m_analyser;
    delete m_keyReference;
    Profiles::getInstance()->dump();
}

void
MainWindow::setupMenus()
{
    if (!m_mainMenusCreated) {

#ifdef Q_OS_LINUX
        // In Ubuntu 14.04 the window's menu bar goes missing entirely
        // if the user is running any desktop environment other than Unity
        // (in which the faux single-menubar appears). The user has a
        // workaround, to remove the appmenu-qt5 package, but that is
        // awkward and the problem is so severe that it merits disabling
        // the system menubar integration altogether. Like this:
	menuBar()->setNativeMenuBar(false);
#endif

        m_rightButtonMenu = new QMenu();
    }

    if (!m_mainMenusCreated) {
        CommandHistory::getInstance()->registerMenu(m_rightButtonMenu);
        m_rightButtonMenu->addSeparator();
    }

    setupFileMenu();
    setupEditMenu();
    setupViewMenu();
    setupAnalysisMenu();

    m_mainMenusCreated = true;
}

void
MainWindow::setupFileMenu()
{
    if (m_mainMenusCreated) return;

    QMenu *menu = menuBar()->addMenu(tr("&File"));
    menu->setTearOffEnabled(true);
    QToolBar *toolbar = addToolBar(tr("File Toolbar"));

    m_keyReference->setCategory(tr("File and Session Management"));

    IconLoader il;
    QIcon icon;
    QAction *action;

    icon = il.load("fileopen");
    icon.addPixmap(il.loadPixmap("fileopen-22"));
    action = new QAction(icon, tr("&Open..."), this);
    action->setShortcut(tr("Ctrl+O"));
    action->setStatusTip(tr("Open a session or audio file"));
    connect(action, SIGNAL(triggered()), this, SLOT(openFile()));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    toolbar->addAction(action);

    action = new QAction(tr("Open Lo&cation..."), this);
    action->setShortcut(tr("Ctrl+Shift+O"));
    action->setStatusTip(tr("Open a file from a remote URL"));
    connect(action, SIGNAL(triggered()), this, SLOT(openLocation()));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);

    m_recentFilesMenu = menu->addMenu(tr("Open &Recent"));
    m_recentFilesMenu->setTearOffEnabled(true);
    setupRecentFilesMenu();
    connect(&m_recentFiles, SIGNAL(recentChanged()),
            this, SLOT(setupRecentFilesMenu()));

    menu->addSeparator();

    icon = il.load("filesave");
    icon.addPixmap(il.loadPixmap("filesave-22"));
    action = new QAction(icon, tr("&Save Session"), this);
    action->setShortcut(tr("Ctrl+S"));
    action->setStatusTip(tr("Save the current session into a %1 session file").arg(QApplication::applicationName()));
    connect(action, SIGNAL(triggered()), this, SLOT(saveSession()));
    connect(this, SIGNAL(canSave(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    toolbar->addAction(action);
	
    icon = il.load("filesaveas");
    icon.addPixmap(il.loadPixmap("filesaveas-22"));
    action = new QAction(icon, tr("Save Session &As..."), this);
    action->setShortcut(tr("Ctrl+Shift+S"));
    action->setStatusTip(tr("Save the current session into a new %1 session file").arg(QApplication::applicationName()));
    connect(action, SIGNAL(triggered()), this, SLOT(saveSessionAs()));
    connect(this, SIGNAL(canSaveAs(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    toolbar->addAction(action);

    action = new QAction(tr("Save Session to Audio &Path"), this);
    action->setShortcut(tr("Ctrl+Alt+S"));
    action->setStatusTip(tr("Save the current session into a %1 session file with the same filename as the audio but a .ton extension.").arg(QApplication::applicationName()));
    connect(action, SIGNAL(triggered()), this, SLOT(saveSessionInAudioPath()));
    connect(this, SIGNAL(canSaveAs(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);

    menu->addSeparator();

    action = new QAction(tr("I&mport Pitch Track Data..."), this);
    action->setStatusTip(tr("Import pitch-track data from a CSV, RDF, or layer XML file"));
    connect(action, SIGNAL(triggered()), this, SLOT(importPitchLayer()));
    connect(this, SIGNAL(canImportLayer(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);

    action = new QAction(tr("E&xport Pitch Track Data..."), this);
    action->setStatusTip(tr("Export pitch-track data to a CSV, RDF, or layer XML file"));
    connect(action, SIGNAL(triggered()), this, SLOT(exportPitchLayer()));
    connect(this, SIGNAL(canExportPitchTrack(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);

    action = new QAction(tr("&Export Note Data..."), this);
    action->setStatusTip(tr("Export note data to a CSV, RDF, layer XML, or MIDI file"));
    connect(action, SIGNAL(triggered()), this, SLOT(exportNoteLayer()));
    connect(this, SIGNAL(canExportNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);

    menu->addSeparator();
    action = new QAction(il.load("exit"), tr("&Quit"), this);
    action->setShortcut(tr("Ctrl+Q"));
    action->setStatusTip(tr("Exit %1").arg(QApplication::applicationName()));
    connect(action, SIGNAL(triggered()), this, SLOT(close()));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
}

void
MainWindow::setupEditMenu()
{
    if (m_mainMenusCreated) return;

    QMenu *menu = menuBar()->addMenu(tr("&Edit"));
    menu->setTearOffEnabled(true);
    CommandHistory::getInstance()->registerMenu(menu);
    menu->addSeparator();

    m_keyReference->setCategory
        (tr("Selection Strip Mouse Actions"));
    m_keyReference->registerShortcut
        (tr("Jump"), tr("Left"), 
         tr("Click left button to move the playback position to a time"));
    m_keyReference->registerShortcut
        (tr("Select"), tr("Left"), 
         tr("Click left button and drag to select a region of time"));
    m_keyReference->registerShortcut
        (tr("Select Note Duration"), tr("Double-Click Left"), 
         tr("Double-click left button to select the region of time corresponding to a note"));

    QToolBar *toolbar = addToolBar(tr("Tools Toolbar"));
    
    CommandHistory::getInstance()->registerToolbar(toolbar);

    QActionGroup *group = new QActionGroup(this);

    IconLoader il;

    m_keyReference->setCategory(tr("Tool Selection"));

    QAction *action = toolbar->addAction(il.load("select"),
                                         tr("Select"));
    action->setCheckable(true);
    action->setChecked(true);
    action->setShortcut(tr("1"));
    action->setStatusTip(tr("Select"));
    connect(action, SIGNAL(triggered()), this, SLOT(toolSelectSelected()));
    connect(this, SIGNAL(replacedDocument()), action, SLOT(trigger()));
    group->addAction(action);
    menu->addAction(action);
    m_keyReference->registerShortcut(action);

/*
    QAction *action = toolbar->addAction(il.load("navigate"),
                                         tr("Navigate"));
    action->setCheckable(true);
    action->setChecked(true);
    action->setShortcut(tr("1"));
    action->setStatusTip(tr("Navigate"));
    connect(action, SIGNAL(triggered()), this, SLOT(toolNavigateSelected()));
    connect(this, SIGNAL(replacedDocument()), action, SLOT(trigger()));
    group->addAction(action);
    menu->addAction(action);
    m_keyReference->registerShortcut(action);

    m_keyReference->setCategory
        (tr("Navigate Tool Mouse Actions"));
    m_keyReference->registerShortcut
        (tr("Navigate"), tr("Left"), 
         tr("Click left button and drag to move around"));
    m_keyReference->registerShortcut
        (tr("Re-Analyse Area"), tr("Shift+Left"), 
         tr("Shift-click left button and drag to define a specific pitch and time range to re-analyse"));
    m_keyReference->registerShortcut
        (tr("Edit"), tr("Double-Click Left"), 
         tr("Double-click left button on an item to edit it"));
    */
    m_keyReference->setCategory(tr("Tool Selection"));
    action = toolbar->addAction(il.load("move"),
				tr("Edit"));
    action->setCheckable(true);
    action->setShortcut(tr("2"));
    action->setStatusTip(tr("Edit with Note Intelligence"));
    connect(action, SIGNAL(triggered()), this, SLOT(toolEditSelected()));
    group->addAction(action);
    menu->addAction(action);
    m_keyReference->registerShortcut(action);

    m_keyReference->setCategory
        (tr("Note Edit Tool Mouse Actions"));
    m_keyReference->registerShortcut
        (tr("Adjust Pitch"), tr("Left"), 
        tr("Click left button on the main part of a note and drag to move it up or down"));
    m_keyReference->registerShortcut
        (tr("Split"), tr("Left"), 
        tr("Click left button on the bottom edge of a note to split it at the click point"));
    m_keyReference->registerShortcut
        (tr("Resize"), tr("Left"), 
        tr("Click left button on the left or right edge of a note and drag to change the time or duration of the note"));
    m_keyReference->registerShortcut
        (tr("Erase"), tr("Shift+Left"), 
        tr("Shift-click left button on a note to remove it"));


/* Remove for now...

    m_keyReference->setCategory(tr("Tool Selection"));
    action = toolbar->addAction(il.load("notes"),
				tr("Free Edit"));
    action->setCheckable(true);
    action->setShortcut(tr("3"));
    action->setStatusTip(tr("Free Edit"));
    connect(action, SIGNAL(triggered()), this, SLOT(toolFreeEditSelected()));
    group->addAction(action);
    m_keyReference->registerShortcut(action);
*/

    menu->addSeparator();
    
    m_keyReference->setCategory(tr("Selection"));

    action = new QAction(tr("Select &All"), this);
    action->setShortcut(tr("Ctrl+A"));
    action->setStatusTip(tr("Select the whole duration of the current session"));
    connect(action, SIGNAL(triggered()), this, SLOT(selectAll()));
    connect(this, SIGNAL(canSelect(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    action = new QAction(tr("C&lear Selection"), this);
    action->setShortcuts(QList<QKeySequence>()
                         << QKeySequence(tr("Esc"))
                         << QKeySequence(tr("Ctrl+Esc")));
    action->setStatusTip(tr("Clear the selection and abandon any pending pitch choices in it"));
    connect(action, SIGNAL(triggered()), this, SLOT(abandonSelection()));
    connect(this, SIGNAL(canClearSelection(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    m_keyReference->registerAlternativeShortcut(action, QKeySequence(tr("Ctrl+Esc")));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    menu->addSeparator();
    m_rightButtonMenu->addSeparator();
    
    m_keyReference->setCategory(tr("Pitch Track"));
    
    action = new QAction(tr("Choose Higher Pitch"), this);
    action->setShortcut(tr("Ctrl+Up"));
    action->setStatusTip(tr("Move pitches up an octave, or to the next higher pitch candidate"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(switchPitchUp()));
    connect(this, SIGNAL(canClearSelection(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);
    
    action = new QAction(tr("Choose Lower Pitch"), this);
    action->setShortcut(tr("Ctrl+Down"));
    action->setStatusTip(tr("Move pitches down an octave, or to the next lower pitch candidate"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(switchPitchDown()));
    connect(this, SIGNAL(canClearSelection(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    m_showCandidatesAction = new QAction(tr("Show Pitch Candidates"), this);
    m_showCandidatesAction->setShortcut(tr("Ctrl+Return"));
    m_showCandidatesAction->setStatusTip(tr("Toggle the display of alternative pitch candidates for the selected region"));
    m_keyReference->registerShortcut(m_showCandidatesAction);
    connect(m_showCandidatesAction, SIGNAL(triggered()), this, SLOT(togglePitchCandidates()));
    connect(this, SIGNAL(canClearSelection(bool)), m_showCandidatesAction, SLOT(setEnabled(bool)));
    menu->addAction(m_showCandidatesAction);
    m_rightButtonMenu->addAction(m_showCandidatesAction);
    
    action = new QAction(tr("Remove Pitches"), this);
    action->setShortcut(tr("Ctrl+Backspace"));
    action->setStatusTip(tr("Remove all pitch estimates within the selected region, making it unvoiced"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(clearPitches()));
    connect(this, SIGNAL(canClearSelection(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    menu->addSeparator();
    m_rightButtonMenu->addSeparator();
    
    m_keyReference->setCategory(tr("Note Track"));

    action = new QAction(tr("Split Note"), this);
    action->setShortcut(tr("/"));
    action->setStatusTip(tr("Split the note at the current playback position into two"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(splitNote()));
    connect(this, SIGNAL(canExportNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    action = new QAction(tr("Merge Notes"), this);
    action->setShortcut(tr("\\"));
    action->setStatusTip(tr("Merge all notes within the selected region into a single note"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(mergeNotes()));
    connect(this, SIGNAL(canSnapNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    action = new QAction(tr("Delete Notes"), this);
    action->setShortcut(tr("Backspace"));
    action->setStatusTip(tr("Delete all notes within the selected region"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(deleteNotes()));
    connect(this, SIGNAL(canSnapNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);
    
    action = new QAction(tr("Form Note from Selection"), this);
    action->setShortcut(tr("="));
    action->setStatusTip(tr("Form a note spanning the selected region, splitting any existing notes at its boundaries"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(formNoteFromSelection()));
    connect(this, SIGNAL(canSnapNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    action = new QAction(tr("Snap Notes to Pitch Track"), this);
    action->setStatusTip(tr("Set notes within the selected region to the median frequency of their underlying pitches, or remove them if there are no underlying pitches"));
    // m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(snapNotesToPitches()));
    connect(this, SIGNAL(canSnapNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);
}

void
MainWindow::setupViewMenu()
{
    if (m_mainMenusCreated) return;

    IconLoader il;

    QAction *action = 0;

    m_keyReference->setCategory(tr("Panning and Navigation"));

    QMenu *menu = menuBar()->addMenu(tr("&View"));
    menu->setTearOffEnabled(true);
    action = new QAction(tr("Peek &Left"), this);
    action->setShortcut(tr("Alt+Left"));
    action->setStatusTip(tr("Scroll the current pane to the left without changing the play position"));
    connect(action, SIGNAL(triggered()), this, SLOT(scrollLeft()));
    connect(this, SIGNAL(canScroll(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    
    action = new QAction(tr("Peek &Right"), this);
    action->setShortcut(tr("Alt+Right"));
    action->setStatusTip(tr("Scroll the current pane to the right without changing the play position"));
    connect(action, SIGNAL(triggered()), this, SLOT(scrollRight()));
    connect(this, SIGNAL(canScroll(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);

    menu->addSeparator();

    m_keyReference->setCategory(tr("Zoom"));

    action = new QAction(il.load("zoom-in"),
                         tr("Zoom &In"), this);
    action->setShortcut(tr("Up"));
    action->setStatusTip(tr("Increase the zoom level"));
    connect(action, SIGNAL(triggered()), this, SLOT(zoomIn()));
    connect(this, SIGNAL(canZoom(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    
    action = new QAction(il.load("zoom-out"),
                         tr("Zoom &Out"), this);
    action->setShortcut(tr("Down"));
    action->setStatusTip(tr("Decrease the zoom level"));
    connect(action, SIGNAL(triggered()), this, SLOT(zoomOut()));
    connect(this, SIGNAL(canZoom(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    
    action = new QAction(tr("Restore &Default Zoom"), this);
    action->setStatusTip(tr("Restore the zoom level to the default"));
    connect(action, SIGNAL(triggered()), this, SLOT(zoomDefault()));
    connect(this, SIGNAL(canZoom(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);

    action = new QAction(il.load("zoom-fit"),
                         tr("Zoom to &Fit"), this);
    action->setShortcut(tr("F"));
    action->setStatusTip(tr("Zoom to show the whole file"));
    connect(action, SIGNAL(triggered()), this, SLOT(zoomToFit()));
    connect(this, SIGNAL(canZoom(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);

    menu->addSeparator();
    
    action = new QAction(tr("Set Displayed Fre&quency Range..."), this);
    action->setStatusTip(tr("Set the minimum and maximum frequencies in the visible display"));
    connect(action, SIGNAL(triggered()), this, SLOT(editDisplayExtents()));
    menu->addAction(action);
}

void
MainWindow::setupAnalysisMenu()
{
    if (m_mainMenusCreated) return;

    IconLoader il;

    QAction *action = 0;

    QMenu *menu = menuBar()->addMenu(tr("&Analysis"));
    menu->setTearOffEnabled(true);


    action = new QAction(tr("&Analyse now!"), this);
    action->setStatusTip(tr("Trigger analysis of pitches and notes. (This will delete all existing pitches and notes.)"));
    connect(action, SIGNAL(triggered()), this, SLOT(analyseNow()));
    menu->addAction(action);
    m_keyReference->registerShortcut(action);

    menu->addSeparator();

    QSettings settings;
    settings.beginGroup("Analyser");
    bool autoAnalyse = settings.value("auto-analysis", true).toBool();
    bool precise = settings.value("precision-analysis", false).toBool();
    bool lowamp = settings.value("lowamp-analysis", true).toBool();
    settings.endGroup();

    action = new QAction(tr("Auto-Analyse &New Audio"), this);
    action->setStatusTip(tr("Automatically trigger analysis upon opening of a new audio file."));
    action->setCheckable(true);
    action->setChecked(autoAnalyse);
    connect(action, SIGNAL(triggered()), this, SLOT(autoAnalysisToggled()));
    menu->addAction(action);

    action = new QAction(tr("&Unbiased Timing (slow)"), this);
    action->setStatusTip(tr("Use a symmetric window in YIN to remove frequency-dependent timing bias. (This is slow!)"));
    action->setCheckable(true);
    action->setChecked(precise);
    connect(action, SIGNAL(triggered()), this, SLOT(precisionAnalysisToggled()));
    menu->addAction(action);

    action = new QAction(tr("&Penalise Soft Pitches"), this);
    action->setStatusTip(tr("Reduce the likelihood of detecting a pitch when the signal has low amplitude."));
    action->setCheckable(true);
    action->setChecked(lowamp);
    connect(action, SIGNAL(triggered()), this, SLOT(lowampAnalysisToggled()));
    menu->addAction(action);

}

void
MainWindow::autoAnalysisToggled()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (!a) return;

    bool set = a->isChecked();

    QSettings settings;
    settings.beginGroup("Analyser");
    settings.setValue("auto-analysis", set);
    settings.endGroup();
}

void
MainWindow::precisionAnalysisToggled()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (!a) return;

    bool set = a->isChecked();

    QSettings settings;
    settings.beginGroup("Analyser");
    settings.setValue("precision-analysis", set);
    settings.endGroup();

    // don't run analyseNow() automatically -- it's a destructive operation
}

void
MainWindow::lowampAnalysisToggled()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (!a) return;

    bool set = a->isChecked();

    QSettings settings;
    settings.beginGroup("Analyser");
    settings.setValue("lowamp-analysis", set);
    settings.endGroup();

    // don't run analyseNow() automatically -- it's a destructive operation
}

void
MainWindow::setupHelpMenu()
{
    QMenu *menu = menuBar()->addMenu(tr("&Help"));
    menu->setTearOffEnabled(true);
    
    m_keyReference->setCategory(tr("Help"));

    IconLoader il;

    QString name = QApplication::applicationName();
    QAction *action;

    action = new QAction(tr("&Key and Mouse Reference"), this);
    action->setShortcut(tr("F2"));
    action->setStatusTip(tr("Open a window showing the keystrokes you can use in %1").arg(name));
    connect(action, SIGNAL(triggered()), this, SLOT(keyReference()));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);

    action = new QAction(il.load("help"),
                                  tr("&Help Reference"), this); 
    action->setShortcut(tr("F1"));
    action->setStatusTip(tr("Open the %1 reference manual").arg(name)); 
    connect(action, SIGNAL(triggered()), this, SLOT(help()));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);

    
    action = new QAction(tr("%1 on the &Web").arg(name), this); 
    action->setStatusTip(tr("Open the %1 website").arg(name)); 
    connect(action, SIGNAL(triggered()), this, SLOT(website()));
    menu->addAction(action);
    
    action = new QAction(tr("&About %1").arg(name), this); 
    action->setStatusTip(tr("Show information about %1").arg(name)); 
    connect(action, SIGNAL(triggered()), this, SLOT(about()));
    menu->addAction(action);
}

void
MainWindow::setupRecentFilesMenu()
{
    m_recentFilesMenu->clear();
    vector<QString> files = m_recentFiles.getRecent();
    for (size_t i = 0; i < files.size(); ++i) {
        QAction *action = new QAction(files[i], this);
        connect(action, SIGNAL(triggered()), this, SLOT(openRecentFile()));
        if (i == 0) {
            action->setShortcut(tr("Ctrl+R"));
            m_keyReference->registerShortcut
                (tr("Re-open"),
                 action->shortcut().toString(),
                 tr("Re-open the current or most recently opened file"));
        }
        m_recentFilesMenu->addAction(action);
    }
}

void
MainWindow::setupToolbars()
{
    m_keyReference->setCategory(tr("Playback and Transport Controls"));

    IconLoader il;

    QMenu *menu = m_playbackMenu = menuBar()->addMenu(tr("Play&back"));
    menu->setTearOffEnabled(true);
    m_rightButtonMenu->addSeparator();
    m_rightButtonPlaybackMenu = m_rightButtonMenu->addMenu(tr("Playback"));

    QToolBar *toolbar = addToolBar(tr("Playback Toolbar"));

    QAction *rwdStartAction = toolbar->addAction(il.load("rewind-start"),
                                                 tr("Rewind to Start"));
    rwdStartAction->setShortcut(tr("Home"));
    rwdStartAction->setStatusTip(tr("Rewind to the start"));
    connect(rwdStartAction, SIGNAL(triggered()), this, SLOT(rewindStart()));
    connect(this, SIGNAL(canPlay(bool)), rwdStartAction, SLOT(setEnabled(bool)));

    QAction *m_rwdAction = toolbar->addAction(il.load("rewind"),
                                              tr("Rewind"));
    m_rwdAction->setShortcut(tr("Left"));
    m_rwdAction->setStatusTip(tr("Rewind to the previous one-second boundary"));
    connect(m_rwdAction, SIGNAL(triggered()), this, SLOT(rewind()));
    connect(this, SIGNAL(canRewind(bool)), m_rwdAction, SLOT(setEnabled(bool)));

    setDefaultFfwdRwdStep(RealTime(1, 0));

    QAction *playAction = toolbar->addAction(il.load("playpause"),
                                             tr("Play / Pause"));
    playAction->setCheckable(true);
    playAction->setShortcut(tr("Space"));
    playAction->setStatusTip(tr("Start or stop playback from the current position"));
    connect(playAction, SIGNAL(triggered()), this, SLOT(play()));
    connect(m_playSource, SIGNAL(playStatusChanged(bool)),
        playAction, SLOT(setChecked(bool)));
    connect(this, SIGNAL(canPlay(bool)), playAction, SLOT(setEnabled(bool)));

    m_ffwdAction = toolbar->addAction(il.load("ffwd"),
                                              tr("Fast Forward"));
    m_ffwdAction->setShortcut(tr("Right"));
    m_ffwdAction->setStatusTip(tr("Fast-forward to the next one-second boundary"));
    connect(m_ffwdAction, SIGNAL(triggered()), this, SLOT(ffwd()));
    connect(this, SIGNAL(canFfwd(bool)), m_ffwdAction, SLOT(setEnabled(bool)));

    QAction *ffwdEndAction = toolbar->addAction(il.load("ffwd-end"),
                                                tr("Fast Forward to End"));
    ffwdEndAction->setShortcut(tr("End"));
    ffwdEndAction->setStatusTip(tr("Fast-forward to the end"));
    connect(ffwdEndAction, SIGNAL(triggered()), this, SLOT(ffwdEnd()));
    connect(this, SIGNAL(canPlay(bool)), ffwdEndAction, SLOT(setEnabled(bool)));

    toolbar = addToolBar(tr("Play Mode Toolbar"));

    QAction *psAction = toolbar->addAction(il.load("playselection"),
                                           tr("Constrain Playback to Selection"));
    psAction->setCheckable(true);
    psAction->setChecked(m_viewManager->getPlaySelectionMode());
    psAction->setShortcut(tr("s"));
    psAction->setStatusTip(tr("Constrain playback to the selected regions"));
    connect(m_viewManager, SIGNAL(playSelectionModeChanged(bool)),
            psAction, SLOT(setChecked(bool)));
    connect(psAction, SIGNAL(triggered()), this, SLOT(playSelectionToggled()));
    connect(this, SIGNAL(canPlaySelection(bool)), psAction, SLOT(setEnabled(bool)));

    QAction *plAction = toolbar->addAction(il.load("playloop"),
                                           tr("Loop Playback"));
    plAction->setCheckable(true);
    plAction->setChecked(m_viewManager->getPlayLoopMode());
    plAction->setShortcut(tr("l"));
    plAction->setStatusTip(tr("Loop playback"));
    connect(m_viewManager, SIGNAL(playLoopModeChanged(bool)),
            plAction, SLOT(setChecked(bool)));
    connect(plAction, SIGNAL(triggered()), this, SLOT(playLoopToggled()));
    connect(this, SIGNAL(canPlay(bool)), plAction, SLOT(setEnabled(bool)));

    QAction *oneLeftAction = new QAction(tr("&One Note Left"), this);
    oneLeftAction->setShortcut(tr("Ctrl+Left"));
    oneLeftAction->setStatusTip(tr("Move cursor to the preceding note (or silence) onset."));
    connect(oneLeftAction, SIGNAL(triggered()), this, SLOT(moveOneNoteLeft()));
    connect(this, SIGNAL(canScroll(bool)), oneLeftAction, SLOT(setEnabled(bool)));
    
    QAction *oneRightAction = new QAction(tr("O&ne Note Right"), this);
    oneRightAction->setShortcut(tr("Ctrl+Right"));
    oneRightAction->setStatusTip(tr("Move cursor to the succeeding note (or silence)."));
    connect(oneRightAction, SIGNAL(triggered()), this, SLOT(moveOneNoteRight()));
    connect(this, SIGNAL(canScroll(bool)), oneRightAction, SLOT(setEnabled(bool)));

    QAction *selectOneLeftAction = new QAction(tr("&Select One Note Left"), this);
    selectOneLeftAction->setShortcut(tr("Ctrl+Shift+Left"));
    selectOneLeftAction->setStatusTip(tr("Select to the preceding note (or silence) onset."));
    connect(selectOneLeftAction, SIGNAL(triggered()), this, SLOT(selectOneNoteLeft()));
    connect(this, SIGNAL(canScroll(bool)), selectOneLeftAction, SLOT(setEnabled(bool)));
    
    QAction *selectOneRightAction = new QAction(tr("S&elect One Note Right"), this);
    selectOneRightAction->setShortcut(tr("Ctrl+Shift+Right"));
    selectOneRightAction->setStatusTip(tr("Select to the succeeding note (or silence)."));
    connect(selectOneRightAction, SIGNAL(triggered()), this, SLOT(selectOneNoteRight()));
    connect(this, SIGNAL(canScroll(bool)), selectOneRightAction, SLOT(setEnabled(bool)));

    m_keyReference->registerShortcut(psAction);
    m_keyReference->registerShortcut(plAction);
    m_keyReference->registerShortcut(playAction);
    m_keyReference->registerShortcut(m_rwdAction);
    m_keyReference->registerShortcut(m_ffwdAction);
    m_keyReference->registerShortcut(rwdStartAction);
    m_keyReference->registerShortcut(ffwdEndAction);
    m_keyReference->registerShortcut(oneLeftAction);
    m_keyReference->registerShortcut(oneRightAction);
    m_keyReference->registerShortcut(selectOneLeftAction);
    m_keyReference->registerShortcut(selectOneRightAction);

    menu->addAction(playAction);
    menu->addAction(psAction);
    menu->addAction(plAction);
    menu->addSeparator();
    menu->addAction(m_rwdAction);
    menu->addAction(m_ffwdAction);
    menu->addSeparator();
    menu->addAction(rwdStartAction);
    menu->addAction(ffwdEndAction);
    menu->addSeparator();
    menu->addAction(oneLeftAction);
    menu->addAction(oneRightAction);
    menu->addAction(selectOneLeftAction);
    menu->addAction(selectOneRightAction);
    menu->addSeparator();

    m_rightButtonPlaybackMenu->addAction(playAction);
    m_rightButtonPlaybackMenu->addAction(psAction);
    m_rightButtonPlaybackMenu->addAction(plAction);
    m_rightButtonPlaybackMenu->addSeparator();
    m_rightButtonPlaybackMenu->addAction(m_rwdAction);
    m_rightButtonPlaybackMenu->addAction(m_ffwdAction);
    m_rightButtonPlaybackMenu->addSeparator();
    m_rightButtonPlaybackMenu->addAction(rwdStartAction);
    m_rightButtonPlaybackMenu->addAction(ffwdEndAction);
    m_rightButtonPlaybackMenu->addSeparator();
    m_rightButtonPlaybackMenu->addAction(oneLeftAction);
    m_rightButtonPlaybackMenu->addAction(oneRightAction);
    m_rightButtonPlaybackMenu->addAction(selectOneLeftAction);
    m_rightButtonPlaybackMenu->addAction(selectOneRightAction);
    m_rightButtonPlaybackMenu->addSeparator();

    QAction *fastAction = menu->addAction(tr("Speed Up"));
    fastAction->setShortcut(tr("Ctrl+PgUp"));
    fastAction->setStatusTip(tr("Time-stretch playback to speed it up without changing pitch"));
    connect(fastAction, SIGNAL(triggered()), this, SLOT(speedUpPlayback()));
    connect(this, SIGNAL(canSpeedUpPlayback(bool)), fastAction, SLOT(setEnabled(bool)));
    
    QAction *slowAction = menu->addAction(tr("Slow Down"));
    slowAction->setShortcut(tr("Ctrl+PgDown"));
    slowAction->setStatusTip(tr("Time-stretch playback to slow it down without changing pitch"));
    connect(slowAction, SIGNAL(triggered()), this, SLOT(slowDownPlayback()));
    connect(this, SIGNAL(canSlowDownPlayback(bool)), slowAction, SLOT(setEnabled(bool)));

    QAction *normalAction = menu->addAction(tr("Restore Normal Speed"));
    normalAction->setShortcut(tr("Ctrl+Home"));
    normalAction->setStatusTip(tr("Restore non-time-stretched playback"));
    connect(normalAction, SIGNAL(triggered()), this, SLOT(restoreNormalPlayback()));
    connect(this, SIGNAL(canChangePlaybackSpeed(bool)), normalAction, SLOT(setEnabled(bool)));

    m_keyReference->registerShortcut(fastAction);
    m_keyReference->registerShortcut(slowAction);
    m_keyReference->registerShortcut(normalAction);

    m_rightButtonPlaybackMenu->addAction(fastAction);
    m_rightButtonPlaybackMenu->addAction(slowAction);
    m_rightButtonPlaybackMenu->addAction(normalAction);

    toolbar = new QToolBar(tr("Playback Controls"));
    addToolBar(Qt::BottomToolBarArea, toolbar);

    toolbar->addWidget(m_playSpeed);
    toolbar->addWidget(m_fader);

    toolbar = addToolBar(tr("Show and Play"));
    addToolBar(Qt::BottomToolBarArea, toolbar);

    m_showAudio = toolbar->addAction(il.load("waveform"), tr("Show Audio"));
    m_showAudio->setCheckable(true);
    connect(m_showAudio, SIGNAL(triggered()), this, SLOT(showAudioToggled()));
    connect(this, SIGNAL(canPlay(bool)), m_showAudio, SLOT(setEnabled(bool)));

    m_playAudio = toolbar->addAction(il.load("speaker"), tr("Play Audio"));
    m_playAudio->setCheckable(true);
    connect(m_playAudio, SIGNAL(triggered()), this, SLOT(playAudioToggled()));
    connect(this, SIGNAL(canPlayWaveform(bool)), m_playAudio, SLOT(setEnabled(bool)));

    toolbar->addWidget(m_gainAudio);
    toolbar->addWidget(m_panAudio);

    // Pitch (f0)
    QLabel *spacer = new QLabel; // blank
    spacer->setFixedWidth(40);
    toolbar->addWidget(spacer);

    m_showPitch = toolbar->addAction(il.load("values"), tr("Show Pitch Track"));
    m_showPitch->setCheckable(true);
    connect(m_showPitch, SIGNAL(triggered()), this, SLOT(showPitchToggled()));
    connect(this, SIGNAL(canPlay(bool)), m_showPitch, SLOT(setEnabled(bool)));

    if (!m_withSonification) 
    {
        m_playPitch = new QAction(tr("Play Pitch Track"), this);
    } else {
        m_playPitch = toolbar->addAction(il.load("speaker"), tr("Play Pitch Track"));
        toolbar->addWidget(m_gainPitch);
        toolbar->addWidget(m_panPitch);
    }
    m_playPitch->setCheckable(true);
    connect(m_playPitch, SIGNAL(triggered()), this, SLOT(playPitchToggled()));
    connect(this, SIGNAL(canPlayPitch(bool)), m_playPitch, SLOT(setEnabled(bool)));

    // Notes
    spacer = new QLabel;
    spacer->setFixedWidth(40);
    toolbar->addWidget(spacer);

    m_showNotes = toolbar->addAction(il.load("notes"), tr("Show Notes"));
    m_showNotes->setCheckable(true);
    connect(m_showNotes, SIGNAL(triggered()), this, SLOT(showNotesToggled()));
    connect(this, SIGNAL(canPlay(bool)), m_showNotes, SLOT(setEnabled(bool)));

    if (!m_withSonification) 
    {
        m_playNotes = new QAction(tr("Play Notes"), this);
    } else {
        m_playNotes = toolbar->addAction(il.load("speaker"), tr("Play Notes"));
        toolbar->addWidget(m_gainNotes);
        toolbar->addWidget(m_panNotes);
    }
    m_playNotes->setCheckable(true);
    connect(m_playNotes, SIGNAL(triggered()), this, SLOT(playNotesToggled()));
    connect(this, SIGNAL(canPlayNotes(bool)), m_playNotes, SLOT(setEnabled(bool)));

    // Spectrogram
    spacer = new QLabel;
    spacer->setFixedWidth(40);
    toolbar->addWidget(spacer);

    if (!m_withSpectrogram)
    {
        m_showSpect = new QAction(tr("Show Spectrogram"), this);
    } else {
        m_showSpect = toolbar->addAction(il.load("spectrogram"), tr("Show Spectrogram"));
    }
    m_showSpect->setCheckable(true);
    connect(m_showSpect, SIGNAL(triggered()), this, SLOT(showSpectToggled()));
    connect(this, SIGNAL(canPlay(bool)), m_showSpect, SLOT(setEnabled(bool)));

    Pane::registerShortcuts(*m_keyReference);

}


void
MainWindow::moveOneNoteRight()
{
    // cerr << "MainWindow::moveOneNoteRight" << endl;
    moveByOneNote(true, false);
}

void
MainWindow::moveOneNoteLeft()
{
    // cerr << "MainWindow::moveOneNoteLeft" << endl;
    moveByOneNote(false, false);
}

void
MainWindow::selectOneNoteRight()
{
    moveByOneNote(true, true);
}

void
MainWindow::selectOneNoteLeft()
{
    moveByOneNote(false, true);
}


void
MainWindow::moveByOneNote(bool right, bool doSelect)
{
    int frame = m_viewManager->getPlaybackFrame();
    cerr << "MainWindow::moveByOneNote startframe: " << frame << endl;
    
    bool isAtSelectionBoundary = false;
    MultiSelection::SelectionList selections = m_viewManager->getSelections();
    if (!selections.empty()) {
        Selection sel = *selections.begin();
        isAtSelectionBoundary = (frame == sel.getStartFrame()) || (frame == sel.getEndFrame());
    }
    if (!doSelect || !isAtSelectionBoundary) {
        m_selectionAnchor = frame;
    }

    Layer *layer = m_analyser->getLayer(Analyser::Notes);
    if (!layer) return;

    FlexiNoteModel *model = qobject_cast<FlexiNoteModel *>(layer->getModel());
    if (!model) return;

    FlexiNoteModel::PointList points = model->getPoints();
    if (points.empty()) return;

    FlexiNoteModel::PointList::iterator i = points.begin();
    std::set<int> snapFrames;
    snapFrames.insert(0);
    while (i != points.end()) {
        snapFrames.insert(i->frame);
        snapFrames.insert(i->frame + i->duration + 1);
        ++i;
    }
    std::set<int>::iterator i2;
    if (snapFrames.find(frame) == snapFrames.end())
    {
        // we're not on an existing snap point, so go to previous
        snapFrames.insert(frame);
    }
    i2 = snapFrames.find(frame);
    if (right)
    {
        i2++;
        if (i2 == snapFrames.end()) i2--;
    } else {
        if (i2 != snapFrames.begin()) i2--;
    }
    frame = *i2;
    m_viewManager->setPlaybackFrame(frame);
    if (doSelect) {
        Selection sel;
        if (frame > m_selectionAnchor) {
            sel = Selection(m_selectionAnchor, frame);
        } else {
            sel = Selection(frame, m_selectionAnchor);
        }
        m_viewManager->setSelection(sel);
    }
    cerr << "MainWindow::moveByOneNote endframe: " << frame << endl;
}

void
MainWindow::toolNavigateSelected()
{
    m_viewManager->setToolMode(ViewManager::NavigateMode);
    m_intelligentActionOn = true;
}

void
MainWindow::toolSelectSelected()
{
    m_viewManager->setToolMode(ViewManager::SelectMode);
    m_intelligentActionOn = true;
}

void
MainWindow::toolEditSelected()
{
    cerr << "MainWindow::toolEditSelected" << endl;
    m_viewManager->setToolMode(ViewManager::NoteEditMode);
    m_intelligentActionOn = true;
    m_analyser->setIntelligentActions(m_intelligentActionOn);
}

void
MainWindow::toolFreeEditSelected()
{
    m_viewManager->setToolMode(ViewManager::NoteEditMode);
    m_intelligentActionOn = false;
    m_analyser->setIntelligentActions(m_intelligentActionOn);
}

void
MainWindow::updateMenuStates()
{
    MainWindowBase::updateMenuStates();

    Pane *currentPane = 0;
    Layer *currentLayer = 0;

    if (m_paneStack) currentPane = m_paneStack->getCurrentPane();
    if (currentPane) currentLayer = currentPane->getSelectedLayer();

    bool haveMainModel =
	(getMainModel() != 0);
    bool havePlayTarget =
	(m_playTarget != 0);
    bool haveCurrentPane =
        (currentPane != 0);
    bool haveCurrentLayer =
        (haveCurrentPane &&
         (currentLayer != 0));
    bool haveSelection = 
        (m_viewManager &&
         !m_viewManager->getSelections().empty());
    bool haveCurrentTimeInstantsLayer = 
        (haveCurrentLayer &&
         qobject_cast<TimeInstantLayer *>(currentLayer));
    bool haveCurrentTimeValueLayer = 
        (haveCurrentLayer &&
         qobject_cast<TimeValueLayer *>(currentLayer));
    bool pitchCandidatesVisible = 
        m_analyser->arePitchCandidatesShown();

    emit canChangePlaybackSpeed(true);
    int v = m_playSpeed->value();
    emit canSpeedUpPlayback(v < m_playSpeed->maximum());
    emit canSlowDownPlayback(v > m_playSpeed->minimum());

    bool haveWaveform =
        m_analyser->isVisible(Analyser::Audio) &&
        m_analyser->getLayer(Analyser::Audio);

    bool havePitchTrack = 
        m_analyser->isVisible(Analyser::PitchTrack) &&
        m_analyser->getLayer(Analyser::PitchTrack);

    bool haveNotes = 
        m_analyser->isVisible(Analyser::Notes) &&
        m_analyser->getLayer(Analyser::Notes);

    emit canExportPitchTrack(havePitchTrack);
    emit canExportNotes(haveNotes);
    emit canSnapNotes(haveSelection && haveNotes);

    emit canPlayWaveform(haveWaveform && haveMainModel && havePlayTarget);
    emit canPlayPitch(havePitchTrack && haveMainModel && havePlayTarget);
    emit canPlayNotes(haveNotes && haveMainModel && havePlayTarget);

    if (pitchCandidatesVisible) {
        m_showCandidatesAction->setText(tr("Hide Pitch Candidates"));
        m_showCandidatesAction->setStatusTip(tr("Remove the display of alternate pitch candidates for the selected region"));
    } else {
        m_showCandidatesAction->setText(tr("Show Pitch Candidates"));
        m_showCandidatesAction->setStatusTip(tr("Show alternate pitch candidates for the selected region"));
    }

    if (m_ffwdAction && m_rwdAction) {
        if (haveCurrentTimeInstantsLayer) {
            m_ffwdAction->setText(tr("Fast Forward to Next Instant"));
            m_ffwdAction->setStatusTip(tr("Fast forward to the next time instant in the current layer"));
            m_rwdAction->setText(tr("Rewind to Previous Instant"));
            m_rwdAction->setStatusTip(tr("Rewind to the previous time instant in the current layer"));
        } else if (haveCurrentTimeValueLayer) {
            m_ffwdAction->setText(tr("Fast Forward to Next Point"));
            m_ffwdAction->setStatusTip(tr("Fast forward to the next point in the current layer"));
            m_rwdAction->setText(tr("Rewind to Previous Point"));
            m_rwdAction->setStatusTip(tr("Rewind to the previous point in the current layer"));
        } else {
            m_ffwdAction->setText(tr("Fast Forward"));
            m_ffwdAction->setStatusTip(tr("Fast forward"));
            m_rwdAction->setText(tr("Rewind"));
            m_rwdAction->setStatusTip(tr("Rewind"));
        }
    }
}

void
MainWindow::showAudioToggled()
{
    m_analyser->toggleVisible(Analyser::Audio);

    QSettings settings;
    settings.beginGroup("MainWindow");

    bool playOn = false;
    if (m_analyser->isVisible(Analyser::Audio)) {
        // just switched layer on; check whether playback was also on previously
        playOn = settings.value("playaudiowas", true).toBool();
    } else {
        settings.setValue("playaudiowas", m_playAudio->isChecked());
    }
    m_analyser->setAudible(Analyser::Audio, playOn);
    m_playAudio->setChecked(playOn);

    settings.endGroup();

    updateMenuStates();
}

void
MainWindow::showPitchToggled()
{
    m_analyser->toggleVisible(Analyser::PitchTrack);

    QSettings settings;
    settings.beginGroup("MainWindow");

    bool playOn = false;
    if (m_analyser->isVisible(Analyser::PitchTrack)) {
        // just switched layer on; check whether playback was also on previously
        playOn = settings.value("playpitchwas", true).toBool();
    } else {
        settings.setValue("playpitchwas", m_playPitch->isChecked());
    }
    m_analyser->setAudible(Analyser::PitchTrack, playOn && m_withSonification);
    m_playPitch->setChecked(playOn);

    settings.endGroup();

    updateMenuStates();
}

void
MainWindow::showSpectToggled()
{
    m_analyser->toggleVisible(Analyser::Spectrogram);
}

void
MainWindow::showNotesToggled()
{
    m_analyser->toggleVisible(Analyser::Notes);

    QSettings settings;
    settings.beginGroup("MainWindow");

    bool playOn = false;
    if (m_analyser->isVisible(Analyser::Notes)) {
        // just switched layer on; check whether playback was also on previously
        playOn = settings.value("playnoteswas", true).toBool();
    } else {
        settings.setValue("playnoteswas", m_playNotes->isChecked());
    }
    m_analyser->setAudible(Analyser::Notes, playOn && m_withSonification);
    m_playNotes->setChecked(playOn);

    settings.endGroup();

    updateMenuStates();
}

void
MainWindow::playAudioToggled()
{
    m_analyser->toggleAudible(Analyser::Audio);
}

void
MainWindow::playPitchToggled()
{
    m_analyser->toggleAudible(Analyser::PitchTrack);
}

void
MainWindow::playNotesToggled()
{
    m_analyser->toggleAudible(Analyser::Notes);
}

void
MainWindow::updateLayerStatuses()
{
    m_showAudio->setChecked(m_analyser->isVisible(Analyser::Audio));
    m_showSpect->setChecked(m_analyser->isVisible(Analyser::Spectrogram));
    m_showPitch->setChecked(m_analyser->isVisible(Analyser::PitchTrack));
    m_showNotes->setChecked(m_analyser->isVisible(Analyser::Notes));
    m_playAudio->setChecked(m_analyser->isAudible(Analyser::Audio));
    m_playPitch->setChecked(m_analyser->isAudible(Analyser::PitchTrack));
    m_playNotes->setChecked(m_analyser->isAudible(Analyser::Notes));
}

void
MainWindow::editDisplayExtents()
{
    float min, max;
    float vmin = 0;
    float vmax = getMainModel()->getSampleRate() /2;
    
    if (!m_analyser->getDisplayFrequencyExtents(min, max)) {
        //!!!
        return;
    }

    RangeInputDialog dialog(tr("Set frequency range"),
                            tr("Enter new frequency range, from %1 to %2 Hz.\nThese values will be rounded to the nearest spectrogram bin.")
                            .arg(vmin).arg(vmax),
                            "Hz", vmin, vmax, this);
    dialog.setRange(min, max);

    if (dialog.exec() == QDialog::Accepted) {
        dialog.getRange(min, max);
        if (min > max) {
            float tmp = max;
            max = min;
            min = tmp;
        }
        m_analyser->setDisplayFrequencyExtents(min, max);
    }
}

void
MainWindow::updateDescriptionLabel()
{
    // Nothing, we don't have one
}

void
MainWindow::documentModified()
{
    MainWindowBase::documentModified();
}

void
MainWindow::documentRestored()
{
    MainWindowBase::documentRestored();
}

void
MainWindow::newSession()
{
    if (!checkSaveModified()) return;

    closeSession();
    createDocument();
    m_document->setAutoAlignment(true);

    Pane *pane = m_paneStack->addPane();
    pane->setPlaybackFollow(PlaybackScrollPage);

    m_viewManager->setGlobalCentreFrame
        (pane->getFrameForX(width() / 2));
    
    connect(pane, SIGNAL(contextHelpChanged(const QString &)),
            this, SLOT(contextHelpChanged(const QString &)));

//    Layer *waveform = m_document->createMainModelLayer(LayerFactory::Waveform);
//    m_document->addLayerToView(pane, waveform);

    m_overview->registerView(pane);

    CommandHistory::getInstance()->clear();
    CommandHistory::getInstance()->documentSaved();
    documentRestored();
    updateMenuStates();
}

void
MainWindow::documentReplaced()
{
    if (m_document) {
        connect(m_document, SIGNAL(activity(QString)),
                m_activityLog, SLOT(activityHappened(QString)));
    }
}

void
MainWindow::closeSession()
{
    if (!checkSaveModified()) return;

    m_analyser->fileClosed();

    while (m_paneStack->getPaneCount() > 0) {

        Pane *pane = m_paneStack->getPane(m_paneStack->getPaneCount() - 1);

        while (pane->getLayerCount() > 0) {
            m_document->removeLayerFromView
                (pane, pane->getLayer(pane->getLayerCount() - 1));
        }
        
        m_overview->unregisterView(pane);
        m_paneStack->deletePane(pane);
    }

    while (m_paneStack->getHiddenPaneCount() > 0) {

        Pane *pane = m_paneStack->getHiddenPane
            (m_paneStack->getHiddenPaneCount() - 1);
        
        while (pane->getLayerCount() > 0) {
            m_document->removeLayerFromView
                (pane, pane->getLayer(pane->getLayerCount() - 1));
        }
        
        m_overview->unregisterView(pane);
        m_paneStack->deletePane(pane);
    }

    delete m_document;
    m_document = 0;
    m_viewManager->clearSelections();
    m_timeRulerLayer = 0; // document owned this

    m_sessionFile = "";

    CommandHistory::getInstance()->clear();
    CommandHistory::getInstance()->documentSaved();
    documentRestored();
}

void
MainWindow::openFile()
{
    QString orig = m_audioFile;
    if (orig == "") orig = ".";
    else orig = QFileInfo(orig).absoluteDir().canonicalPath();

    QString path = getOpenFileName(FileFinder::AnyFile);

    if (path.isEmpty()) return;

    FileOpenStatus status = openPath(path, ReplaceSession);

    if (status == FileOpenFailed) {
        QMessageBox::critical(this, tr("Failed to open file"),
                              tr("<b>File open failed</b><p>File \"%1\" could not be opened").arg(path));
    } else if (status == FileOpenWrongMode) {
        QMessageBox::critical(this, tr("Failed to open file"),
                              tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
    }
}

void
MainWindow::openLocation()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    QString lastLocation = settings.value("lastremote", "").toString();

    bool ok = false;
    QString text = QInputDialog::getText
        (this, tr("Open Location"),
         tr("Please enter the URL of the location to open:"),
         QLineEdit::Normal, lastLocation, &ok);

    if (!ok) return;

    settings.setValue("lastremote", text);

    if (text.isEmpty()) return;

    FileOpenStatus status = openPath(text, ReplaceSession);

    if (status == FileOpenFailed) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Open failed</b><p>URL \"%1\" could not be opened").arg(text));
    } else if (status == FileOpenWrongMode) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
    }
}

void
MainWindow::openRecentFile()
{
    QObject *obj = sender();
    QAction *action = qobject_cast<QAction *>(obj);
    
    if (!action) {
        cerr << "WARNING: MainWindow::openRecentFile: sender is not an action"
             << endl;
        return;
    }

    QString path = action->text();
    if (path == "") return;

    FileOpenStatus status = openPath(path, ReplaceSession);

    if (status == FileOpenFailed) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Open failed</b><p>File or URL \"%1\" could not be opened").arg(path));
    } else if (status == FileOpenWrongMode) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
    }
}

void
MainWindow::paneAdded(Pane *pane)
{
    pane->setPlaybackFollow(PlaybackScrollPage);
    m_paneStack->sizePanesEqually();
    if (m_overview) m_overview->registerView(pane);
}    

void
MainWindow::paneHidden(Pane *pane)
{
    if (m_overview) m_overview->unregisterView(pane); 
}    

void
MainWindow::paneAboutToBeDeleted(Pane *pane)
{
    if (m_overview) m_overview->unregisterView(pane); 
}    

void
MainWindow::paneDropAccepted(Pane *pane, QStringList uriList)
{
    if (pane) m_paneStack->setCurrentPane(pane);

    for (QStringList::iterator i = uriList.begin(); i != uriList.end(); ++i) {

        FileOpenStatus status = openPath(*i, ReplaceSession);

        if (status == FileOpenFailed) {
            QMessageBox::critical(this, tr("Failed to open dropped URL"),
                                  tr("<b>Open failed</b><p>Dropped URL \"%1\" could not be opened").arg(*i));
        } else if (status == FileOpenWrongMode) {
            QMessageBox::critical(this, tr("Failed to open dropped URL"),
                                  tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
        }
    }
}

void
MainWindow::paneDropAccepted(Pane *pane, QString text)
{
    if (pane) m_paneStack->setCurrentPane(pane);

    QUrl testUrl(text);
    if (testUrl.scheme() == "file" || 
        testUrl.scheme() == "http" || 
        testUrl.scheme() == "ftp") {
        QStringList list;
        list.push_back(text);
        paneDropAccepted(pane, list);
        return;
    }

    //!!! open as text -- but by importing as if a CSV, or just adding
    //to a text layer?
}

void
MainWindow::closeEvent(QCloseEvent *e)
{
//    cerr << "MainWindow::closeEvent" << endl;

    if (m_openingAudioFile) {
//        cerr << "Busy - ignoring close event" << endl;
        e->ignore();
        return;
    }

    if (!m_abandoning && !checkSaveModified()) {
//        cerr << "Ignoring close event" << endl;
        e->ignore();
        return;
    }

    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("size", size());
    settings.setValue("position", pos());
    settings.endGroup();

    delete m_keyReference;
    m_keyReference = 0;

    closeSession();

    e->accept();
    return;
}

bool
MainWindow::commitData(bool mayAskUser)
{
    if (mayAskUser) {
        bool rv = checkSaveModified();
        return rv;
    } else {
        if (!m_documentModified) return true;

        // If we can't check with the user first, then we can't save
        // to the original session file (even if we have it) -- have
        // to use a temporary file

        QString svDirBase = ".sv1";
        QString svDir = QDir::home().filePath(svDirBase);

        if (!QFileInfo(svDir).exists()) {
            if (!QDir::home().mkdir(svDirBase)) return false;
        } else {
            if (!QFileInfo(svDir).isDir()) return false;
        }
        
        // This name doesn't have to be unguessable
#ifndef _WIN32
        QString fname = QString("tmp-%1-%2.sv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz"))
            .arg(QProcess().pid());
#else
        QString fname = QString("tmp-%1.sv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz"));
#endif
        QString fpath = QDir(svDir).filePath(fname);
        if (saveSessionFile(fpath)) {
            m_recentFiles.addFile(fpath);
            return true;
        } else {
            return false;
        }
    }
}

bool
MainWindow::checkSaveModified()
{
    // Called before some destructive operation (e.g. new session,
    // exit program).  Return true if we can safely proceed, false to
    // cancel.

    if (!m_documentModified) return true;

    int button = 
        QMessageBox::warning(this,
                             tr("Session modified"),
                             tr("The current session has been modified.\nDo you want to save it?"),
                             QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                             QMessageBox::Yes);

    if (button == QMessageBox::Yes) {
        saveSession();
        if (m_documentModified) { // save failed -- don't proceed!
            return false;
        } else {
            return true; // saved, so it's safe to continue now
        }
    } else if (button == QMessageBox::No) {
        m_documentModified = false; // so we know to abandon it
        return true;
    }

    // else cancel
    return false;
}

bool
MainWindow::waitForInitialAnalysis()
{
    // Called before saving a session. We can't safely save while the
    // initial analysis is happening, because then we end up with an
    // incomplete session on reload. There are certainly theoretically
    // better ways to handle this...
    
    QSettings settings;
    settings.beginGroup("Analyser");
    bool autoAnalyse = settings.value("auto-analysis", true).toBool();
    settings.endGroup();

    if (!autoAnalyse) {
        return true;
    }

    if (!m_analyser || m_analyser->getInitialAnalysisCompletion() >= 100) {
        return true;
    }

    QMessageBox mb(QMessageBox::Information,
                   tr("Waiting for analysis"),
                   tr("Waiting for initial analysis to complete before saving..."),
                   QMessageBox::Cancel,
                   this);

    connect(m_analyser, SIGNAL(initialAnalysisCompleted()), 
            &mb, SLOT(accept()));

    if (mb.exec() == QDialog::Accepted) {
        return true;
    } else {
        return false;
    }
}

void
MainWindow::saveSession()
{
    // We do not want to save mid-analysis regions -- that would cause
    // confusion on reloading
    m_analyser->clearReAnalysis();
    clearSelection();

    if (m_sessionFile != "") {
        if (!saveSessionFile(m_sessionFile)) {
            QMessageBox::critical
                (this, tr("Failed to save file"),
                 tr("Session file \"%1\" could not be saved.").arg(m_sessionFile));
        } else {
            CommandHistory::getInstance()->documentSaved();
            documentRestored();
        }
    } else {
        saveSessionAs();
    }
}

void
MainWindow::saveSessionInAudioPath()
{
    if (m_audioFile == "") return;

    if (!waitForInitialAnalysis()) return;

    // We do not want to save mid-analysis regions -- that would cause
    // confusion on reloading
    m_analyser->clearReAnalysis();
    clearSelection();

    QString filepath = QFileInfo(m_audioFile).absoluteDir().canonicalPath();
    QString basename = QFileInfo(m_audioFile).completeBaseName();

    QString path = QDir(filepath).filePath(basename + ".ton");

    cerr << path << endl;

    // We don't want to overwrite an existing .ton file unless we put
    // it there in the first place
    bool shouldVerify = true;
    if (m_sessionFile == path) {
        shouldVerify = false;
    }

    if (shouldVerify && QFileInfo(path).exists()) {
        if (QMessageBox::question(0, tr("File exists"),
                                  tr("<b>File exists</b><p>The file \"%1\" already exists.\nDo you want to overwrite it?").arg(path),
                                  QMessageBox::Ok,
                                  QMessageBox::Cancel) != QMessageBox::Ok) {
            return;
        }
    }

    if (!waitForInitialAnalysis()) {
        QMessageBox::warning(this, tr("File not saved"),
                             tr("Wait cancelled: the session has not been saved."));
    }

    if (!saveSessionFile(path)) {
        QMessageBox::critical(this, tr("Failed to save file"),
                              tr("Session file \"%1\" could not be saved.").arg(path));
    } else {
        setWindowTitle(tr("%1: %2")
                       .arg(QApplication::applicationName())
                       .arg(QFileInfo(path).fileName()));
        m_sessionFile = path;
        CommandHistory::getInstance()->documentSaved();
        documentRestored();
        m_recentFiles.addFile(path);
    }
}

void
MainWindow::saveSessionAs()
{
    // We do not want to save mid-analysis regions -- that would cause
    // confusion on reloading
    m_analyser->clearReAnalysis();
    clearSelection();

    QString path = getSaveFileName(FileFinder::SessionFile);

    if (path == "") {
        return;
    }

    if (!waitForInitialAnalysis()) {
        QMessageBox::warning(this, tr("File not saved"),
                             tr("Wait cancelled: the session has not been saved."));
        return;
    }

    if (!saveSessionFile(path)) {
        QMessageBox::critical(this, tr("Failed to save file"),
                              tr("Session file \"%1\" could not be saved.").arg(path));
    } else {
        setWindowTitle(tr("%1: %2")
                       .arg(QApplication::applicationName())
                       .arg(QFileInfo(path).fileName()));
        m_sessionFile = path;
        CommandHistory::getInstance()->documentSaved();
        documentRestored();
        m_recentFiles.addFile(path);
    }
}

QString
MainWindow::exportToSVL(QString path, Layer *layer)
{
    Model *model = layer->getModel();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return tr("Failed to open file %1 for writing").arg(path);
    } else {
        QTextStream out(&file);
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<!DOCTYPE sonic-visualiser>\n"
            << "<sv>\n"
            << "  <data>\n";
        
        model->toXml(out, "    ");
        
        out << "  </data>\n"
            << "  <display>\n";
        
        layer->toXml(out, "    ");
        
        out << "  </display>\n"
            << "</sv>\n";

        return "";
    }
}

void
MainWindow::importPitchLayer()
{
    QString path = getOpenFileName(FileFinder::LayerFileNoMidiNonSV);
    if (path == "") return;

    FileOpenStatus status = importPitchLayer(path);

    if (status == FileOpenFailed) {
        emit hideSplash();
        QMessageBox::critical(this, tr("Failed to open file"),
                              tr("<b>File open failed</b><p>Layer file %1 could not be opened.").arg(path));
        return;
    } else if (status == FileOpenWrongMode) {
        emit hideSplash();
        QMessageBox::critical(this, tr("Failed to open file"),
                              tr("<b>Audio required</b><p>Unable to load layer data from \"%1\" without an audio file.<br>Please load at least one audio file before importing annotations.").arg(path));
    }
}

MainWindow::FileOpenStatus
MainWindow::importPitchLayer(FileSource source)
{
    if (!source.isAvailable()) return FileOpenFailed;
    source.waitForData();

    QString path = source.getLocalFilename();

    RDFImporter::RDFDocumentType rdfType = 
        RDFImporter::identifyDocumentType(QUrl::fromLocalFile(path).toString());

    if (rdfType != RDFImporter::NotRDF) {

        //!!!
        return FileOpenFailed;

    } else if (source.getExtension().toLower() == "svl" ||
               (source.getExtension().toLower() == "xml" &&
                (SVFileReader::identifyXmlFile(source.getLocalFilename())
                 == SVFileReader::SVLayerFile))) {
        
        //!!!
        return FileOpenFailed;

    } else {
        
        try {

            CSVFormat format(path);
            format.setSampleRate(getMainModel()->getSampleRate());

            if (format.getModelType() != CSVFormat::TwoDimensionalModel) {
                //!!! error report
                return FileOpenFailed;
            }

            Model *model = DataFileReaderFactory::loadCSV
                (path, format, getMainModel()->getSampleRate());

            if (model) {

                SVDEBUG << "MainWindow::importPitchLayer: Have model" << endl;

                CommandHistory::getInstance()->startCompoundOperation
                    (tr("Import Pitch Track"), true);

                Layer *newLayer = m_document->createImportedLayer(model);

                m_analyser->takePitchTrackFrom(newLayer);

                m_document->deleteLayer(newLayer);

                CommandHistory::getInstance()->endCompoundOperation();

                //!!! swap all data in to existing layer instead of this

                if (!source.isRemote()) {
                    registerLastOpenedFilePath
                        (FileFinder::LayerFile,
                         path); // for file dialog
                }

                return FileOpenSucceeded;
            }
        } catch (DataFileReaderFactory::Exception e) {
            if (e == DataFileReaderFactory::ImportCancelled) {
                return FileOpenCancelled;
            }
        }
    }
    
    return FileOpenFailed;
}

void
MainWindow::exportPitchLayer()
{
    Layer *layer = m_analyser->getLayer(Analyser::PitchTrack);
    if (!layer) return;

    SparseTimeValueModel *model =
        qobject_cast<SparseTimeValueModel *>(layer->getModel());
    if (!model) return;

    FileFinder::FileType type = FileFinder::LayerFileNoMidiNonSV;

    QString path = getSaveFileName(type);

    if (path == "") return;

    if (QFileInfo(path).suffix() == "") path += ".svl";

    QString suffix = QFileInfo(path).suffix().toLower();

    QString error;

    if (suffix == "xml" || suffix == "svl") {

        error = exportToSVL(path, layer);

    } else if (suffix == "ttl" || suffix == "n3") {

        RDFExporter exporter(path, model);
        exporter.write();
        if (!exporter.isOK()) {
            error = exporter.getError();
        }

    } else {

        CSVFileWriter writer(path, model,
                             ((suffix == "csv") ? "," : "\t"));
        writer.write();

        if (!writer.isOK()) {
            error = writer.getError();
        }
    }

    if (error != "") {
        QMessageBox::critical(this, tr("Failed to write file"), error);
    } else {
        emit activity(tr("Export layer to \"%1\"").arg(path));
    }
}

void
MainWindow::exportNoteLayer()
{
    Layer *layer = m_analyser->getLayer(Analyser::Notes);
    if (!layer) return;

    FlexiNoteModel *model = qobject_cast<FlexiNoteModel *>(layer->getModel());
    if (!model) return;

    FileFinder::FileType type = FileFinder::LayerFileNonSV;

    QString path = getSaveFileName(type);

    if (path == "") return;

    if (QFileInfo(path).suffix() == "") path += ".svl";

    QString suffix = QFileInfo(path).suffix().toLower();

    QString error;

    if (suffix == "xml" || suffix == "svl") {

        error = exportToSVL(path, layer);

    } else if (suffix == "mid" || suffix == "midi") {
     
        MIDIFileWriter writer(path, model, model->getSampleRate());
        writer.write();
        if (!writer.isOK()) {
            error = writer.getError();
        }

    } else if (suffix == "ttl" || suffix == "n3") {

        RDFExporter exporter(path, model);
        exporter.write();
        if (!exporter.isOK()) {
            error = exporter.getError();
        }

    } else {

        CSVFileWriter writer(path, model,
                             ((suffix == "csv") ? "," : "\t"));
        writer.write();

        if (!writer.isOK()) {
            error = writer.getError();
        }
    }

    if (error != "") {
        QMessageBox::critical(this, tr("Failed to write file"), error);
    } else {
        emit activity(tr("Export layer to \"%1\"").arg(path));
    }
}

void
MainWindow::doubleClickSelectInvoked(int frame)
{
    int f0, f1;
    m_analyser->getEnclosingSelectionScope(frame, f0, f1);
    
    cerr << "MainWindow::doubleClickSelectInvoked(" << frame << "): [" << f0 << "," << f1 << "]" << endl;

    Selection sel(f0, f1);
    m_viewManager->setSelection(sel);
}

void
MainWindow::abandonSelection()
{
    // Named abandonSelection rather than clearSelection to indicate
    // that this is an active operation -- it restores the original
    // content of the pitch track in the selected region rather than
    // simply un-selecting.

    cerr << "MainWindow::abandonSelection()" << endl;

    CommandHistory::getInstance()->startCompoundOperation(tr("Abandon Selection"), true);

    MultiSelection::SelectionList selections = m_viewManager->getSelections();
    if (!selections.empty()) {
        Selection sel = *selections.begin();
        m_analyser->abandonReAnalysis(sel);
        auxSnapNotes(sel);
    }

    MainWindowBase::clearSelection();

    CommandHistory::getInstance()->endCompoundOperation();
}

void
MainWindow::selectionChangedByUser()
{
    if (!m_document) {
        // we're exiting, most likely
        return;
    }

    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    cerr << "MainWindow::selectionChangedByUser" << endl;

    m_analyser->showPitchCandidates(m_pendingConstraint.isConstrained());

    if (!selections.empty()) {
        Selection sel = *selections.begin();
        cerr << "MainWindow::selectionChangedByUser: have selection" << endl;
        QString error = m_analyser->reAnalyseSelection
            (sel, m_pendingConstraint);
        if (error != "") {
            QMessageBox::critical
                (this, tr("Failed to analyse selection"),
                 tr("<b>Analysis failed</b><p>%2</p>").arg(error));
        }
    }

    m_pendingConstraint = Analyser::FrequencyRange();
}

void
MainWindow::regionOutlined(QRect r)
{
    cerr << "MainWindow::regionOutlined(" << r.x() << "," << r.y() << "," << r.width() << "," << r.height() << ")" << endl;

    Pane *pane = qobject_cast<Pane *>(sender());
    if (!pane) {
        cerr << "MainWindow::regionOutlined: not sent by pane, ignoring" << endl;
        return;
    }

    if (!m_analyser) {
        cerr << "MainWindow::regionOutlined: no analyser, ignoring" << endl;
        return;
    }

    SpectrogramLayer *spectrogram = qobject_cast<SpectrogramLayer *>
        (m_analyser->getLayer(Analyser::Spectrogram));
    if (!spectrogram) {
        cerr << "MainWindow::regionOutlined: no spectrogram layer, ignoring" << endl;
        return;
    }

    int f0 = pane->getFrameForX(r.x());
    int f1 = pane->getFrameForX(r.x() + r.width());
    
    float v0 = spectrogram->getFrequencyForY(pane, r.y() + r.height());
    float v1 = spectrogram->getFrequencyForY(pane, r.y());

    cerr << "MainWindow::regionOutlined: frame " << f0 << " -> " << f1 
         << ", frequency " << v0 << " -> " << v1 << endl;

    m_pendingConstraint = Analyser::FrequencyRange(v0, v1);

    Selection sel(f0, f1);
    m_viewManager->setSelection(sel);
}

void
MainWindow::clearPitches()
{
    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    CommandHistory::getInstance()->startCompoundOperation(tr("Clear Pitches"), true);

    for (MultiSelection::SelectionList::iterator k = selections.begin();
         k != selections.end(); ++k) {
        m_analyser->deletePitches(*k);
        auxSnapNotes(*k);
    }

    CommandHistory::getInstance()->endCompoundOperation();
}

void
MainWindow::octaveShift(bool up)
{
    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    CommandHistory::getInstance()->startCompoundOperation
        (up ? tr("Choose Higher Octave") : tr("Choose Lower Octave"), true);

    for (MultiSelection::SelectionList::iterator k = selections.begin();
         k != selections.end(); ++k) {

        m_analyser->shiftOctave(*k, up);
        auxSnapNotes(*k);
    }

    CommandHistory::getInstance()->endCompoundOperation();
}

void
MainWindow::togglePitchCandidates()
{
    CommandHistory::getInstance()->startCompoundOperation(tr("Toggle Pitch Candidates"), true);

    m_analyser->showPitchCandidates(!m_analyser->arePitchCandidatesShown());

    CommandHistory::getInstance()->endCompoundOperation();

    updateMenuStates();
}

void
MainWindow::switchPitchUp()
{
    if (m_analyser->arePitchCandidatesShown()) {
        if (m_analyser->haveHigherPitchCandidate()) {

            CommandHistory::getInstance()->startCompoundOperation
                (tr("Choose Higher Pitch Candidate"), true);

            MultiSelection::SelectionList selections = m_viewManager->getSelections();

            for (MultiSelection::SelectionList::iterator k = selections.begin();
                 k != selections.end(); ++k) {
                m_analyser->switchPitchCandidate(*k, true);
                auxSnapNotes(*k);
            }

            CommandHistory::getInstance()->endCompoundOperation();
        }
    } else {
        octaveShift(true);
    }
}

void
MainWindow::switchPitchDown()
{
    if (m_analyser->arePitchCandidatesShown()) {
        if (m_analyser->haveLowerPitchCandidate()) {

            CommandHistory::getInstance()->startCompoundOperation
                (tr("Choose Lower Pitch Candidate"), true);

            MultiSelection::SelectionList selections = m_viewManager->getSelections();
            
            for (MultiSelection::SelectionList::iterator k = selections.begin();
                 k != selections.end(); ++k) {
                m_analyser->switchPitchCandidate(*k, false);
                auxSnapNotes(*k);
            }

            CommandHistory::getInstance()->endCompoundOperation();
        }
    } else {
        octaveShift(false);
    }
}

void
MainWindow::snapNotesToPitches()
{
    cerr << "in snapNotesToPitches" << endl;
    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    if (!selections.empty()) {

        CommandHistory::getInstance()->startCompoundOperation
            (tr("Snap Notes to Pitches"), true);
                
        for (MultiSelection::SelectionList::iterator k = selections.begin();
             k != selections.end(); ++k) {
            auxSnapNotes(*k);
        }
        
        CommandHistory::getInstance()->endCompoundOperation();
    }
}

void
MainWindow::auxSnapNotes(Selection s)
{
    cerr << "in auxSnapNotes" << endl;
    FlexiNoteLayer *layer =
        qobject_cast<FlexiNoteLayer *>(m_analyser->getLayer(Analyser::Notes));
    if (!layer) return;

    layer->snapSelectedNotesToPitchTrack(m_analyser->getPane(), s);
}    

void
MainWindow::splitNote()
{
    FlexiNoteLayer *layer =
        qobject_cast<FlexiNoteLayer *>(m_analyser->getLayer(Analyser::Notes));
    if (!layer) return;

    layer->splitNotesAt(m_analyser->getPane(), m_viewManager->getPlaybackFrame());
}

void
MainWindow::mergeNotes()
{
    FlexiNoteLayer *layer =
        qobject_cast<FlexiNoteLayer *>(m_analyser->getLayer(Analyser::Notes));
    if (!layer) return;

    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    if (!selections.empty()) {

        CommandHistory::getInstance()->startCompoundOperation
            (tr("Merge Notes"), true);
                
        for (MultiSelection::SelectionList::iterator k = selections.begin();
             k != selections.end(); ++k) {
            layer->mergeNotes(m_analyser->getPane(), *k, true);
        }
        
        CommandHistory::getInstance()->endCompoundOperation();
    }
}

void
MainWindow::deleteNotes()
{
    FlexiNoteLayer *layer =
        qobject_cast<FlexiNoteLayer *>(m_analyser->getLayer(Analyser::Notes));
    if (!layer) return;

    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    if (!selections.empty()) {

        CommandHistory::getInstance()->startCompoundOperation
            (tr("Delete Notes"), true);
                
        for (MultiSelection::SelectionList::iterator k = selections.begin();
             k != selections.end(); ++k) {
            layer->deleteSelectionInclusive(*k);
        }
        
        CommandHistory::getInstance()->endCompoundOperation();
    }
}


void
MainWindow::formNoteFromSelection()
{
    Layer *layer0 = m_analyser->getLayer(Analyser::Notes);
    FlexiNoteModel *model = qobject_cast<FlexiNoteModel *>(layer0->getModel());

    FlexiNoteLayer *layer =
        qobject_cast<FlexiNoteLayer *>(m_analyser->getLayer(Analyser::Notes));
    if (!layer) return;

    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    if (!selections.empty()) {
    
        CommandHistory::getInstance()->startCompoundOperation
            (tr("Form Note from Selection"), true);
        for (MultiSelection::SelectionList::iterator k = selections.begin();
             k != selections.end(); ++k) {
            if (!model->getNotesWithin(k->getStartFrame(), k->getEndFrame()).empty()) {
                layer->splitNotesAt(m_analyser->getPane(), k->getStartFrame());
                layer->splitNotesAt(m_analyser->getPane(), k->getEndFrame());
                layer->mergeNotes(m_analyser->getPane(), *k, false);
            } else {
                layer->addNoteOn(k->getStartFrame(), 100, 100);
                layer->addNoteOff(k->getEndFrame(), 100);
                layer->mergeNotes(m_analyser->getPane(), *k, false); // only so the note adapts in case of exisitng pitch track
            }
        }

        CommandHistory::getInstance()->endCompoundOperation();     
    }
}

void
MainWindow::playSpeedChanged(int position)
{
    PlaySpeedRangeMapper mapper(0, 200);

    float percent = m_playSpeed->mappedValue();
    float factor = mapper.getFactorForValue(percent);

    cerr << "speed = " << position << " percent = " << percent << " factor = " << factor << endl;

    bool something = (position != 100);

    int pc = lrintf(percent);

    if (!something) {
        contextHelpChanged(tr("Playback speed: Normal"));
    } else {
        contextHelpChanged(tr("Playback speed: %1%2%")
                           .arg(position > 100 ? "+" : "")
                           .arg(pc));
    }

    m_playSource->setTimeStretch(factor);

    updateMenuStates();
}

void
MainWindow::playSharpenToggled()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("playsharpen", m_playSharpen->isChecked());
    settings.endGroup();

    playSpeedChanged(m_playSpeed->value());
    // TODO: pitch gain?
}

void
MainWindow::playMonoToggled()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("playmono", m_playMono->isChecked());
    settings.endGroup();

    playSpeedChanged(m_playSpeed->value());
    // TODO: pitch gain?
}    

void
MainWindow::speedUpPlayback()
{
    int value = m_playSpeed->value();
    value = value + m_playSpeed->pageStep();
    if (value > m_playSpeed->maximum()) value = m_playSpeed->maximum();
    m_playSpeed->setValue(value);
}

void
MainWindow::slowDownPlayback()
{
    int value = m_playSpeed->value();
    value = value - m_playSpeed->pageStep();
    if (value < m_playSpeed->minimum()) value = m_playSpeed->minimum();
    m_playSpeed->setValue(value);
}

void
MainWindow::restoreNormalPlayback()
{
    m_playSpeed->setValue(m_playSpeed->defaultValue());
}

void
MainWindow::audioGainChanged(int position)
{
    float level = m_gainAudio->mappedValue();
    float gain = powf(10, level / 20.0);

    cerr << "gain = " << gain << " (" << position << " dB)" << endl;

    contextHelpChanged(tr("Audio Gain: %1 dB").arg(position));

    m_analyser->setGain(Analyser::Audio, gain);

    updateMenuStates();
} 

void
MainWindow::increaseAudioGain()
{
    int value = m_gainAudio->value();
    value = value + m_gainAudio->pageStep();
    if (value > m_gainAudio->maximum()) value = m_gainAudio->maximum();
    m_gainAudio->setValue(value);
}

void
MainWindow::decreaseAudioGain()
{
    int value = m_gainAudio->value();
    value = value - m_gainAudio->pageStep();
    if (value < m_gainAudio->minimum()) value = m_gainAudio->minimum();
    m_gainAudio->setValue(value);
}

void
MainWindow::restoreNormalAudioGain()
{
    m_gainAudio->setValue(m_gainAudio->defaultValue());
}

void
MainWindow::pitchGainChanged(int position)
{
    float level = m_gainPitch->mappedValue();
    float gain = powf(10, level / 20.0);

    cerr << "gain = " << gain << " (" << position << " dB)" << endl;

    contextHelpChanged(tr("Pitch Gain: %1 dB").arg(position));

    m_analyser->setGain(Analyser::PitchTrack, gain);

    updateMenuStates();
} 

void
MainWindow::increasePitchGain()
{
    int value = m_gainPitch->value();
    value = value + m_gainPitch->pageStep();
    if (value > m_gainPitch->maximum()) value = m_gainPitch->maximum();
    m_gainPitch->setValue(value);
}

void
MainWindow::decreasePitchGain()
{
    int value = m_gainPitch->value();
    value = value - m_gainPitch->pageStep();
    if (value < m_gainPitch->minimum()) value = m_gainPitch->minimum();
    m_gainPitch->setValue(value);
}

void
MainWindow::restoreNormalPitchGain()
{
    m_gainPitch->setValue(m_gainPitch->defaultValue());
}

void
MainWindow::notesGainChanged(int position)
{
    float level = m_gainNotes->mappedValue();
    float gain = powf(10, level / 20.0);

    cerr << "gain = " << gain << " (" << position << " dB)" << endl;

    contextHelpChanged(tr("Notes Gain: %1 dB").arg(position));

    m_analyser->setGain(Analyser::Notes, gain);

    updateMenuStates();
} 

void
MainWindow::increaseNotesGain()
{
    int value = m_gainNotes->value();
    value = value + m_gainNotes->pageStep();
    if (value > m_gainNotes->maximum()) value = m_gainNotes->maximum();
    m_gainNotes->setValue(value);
}

void
MainWindow::decreaseNotesGain()
{
    int value = m_gainNotes->value();
    value = value - m_gainNotes->pageStep();
    if (value < m_gainNotes->minimum()) value = m_gainNotes->minimum();
    m_gainNotes->setValue(value);
}

void
MainWindow::restoreNormalNotesGain()
{
    m_gainNotes->setValue(m_gainNotes->defaultValue());
}

void
MainWindow::audioPanChanged(int position)
{
    float level = m_panAudio->mappedValue();
    float pan = level/100.f;

    cerr << "pan = " << pan << " (" << position << ")" << endl;

    contextHelpChanged(tr("Audio Pan: %1").arg(position));

    m_analyser->setPan(Analyser::Audio, pan);

    updateMenuStates();
} 

void
MainWindow::increaseAudioPan()
{
    int value = m_panAudio->value();
    value = value + m_panAudio->pageStep();
    if (value > m_panAudio->maximum()) value = m_panAudio->maximum();
    m_panAudio->setValue(value);
}

void
MainWindow::decreaseAudioPan()
{
    int value = m_panAudio->value();
    value = value - m_panAudio->pageStep();
    if (value < m_panAudio->minimum()) value = m_panAudio->minimum();
    m_panAudio->setValue(value);
}

void
MainWindow::restoreNormalAudioPan()
{
    m_panAudio->setValue(m_panAudio->defaultValue());
}

void
MainWindow::pitchPanChanged(int position)
{
    float level = m_panPitch->mappedValue();
    float pan = level/100.f;

    cerr << "pan = " << pan << " (" << position << ")" << endl;

    contextHelpChanged(tr("Pitch Pan: %1").arg(position));

    m_analyser->setPan(Analyser::PitchTrack, pan);

    updateMenuStates();
} 

void
MainWindow::increasePitchPan()
{
    int value = m_panPitch->value();
    value = value + m_panPitch->pageStep();
    if (value > m_panPitch->maximum()) value = m_panPitch->maximum();
    m_panPitch->setValue(value);
}

void
MainWindow::decreasePitchPan()
{
    int value = m_panPitch->value();
    value = value - m_panPitch->pageStep();
    if (value < m_panPitch->minimum()) value = m_panPitch->minimum();
    m_panPitch->setValue(value);
}

void
MainWindow::restoreNormalPitchPan()
{
    m_panPitch->setValue(m_panPitch->defaultValue());
}

void
MainWindow::notesPanChanged(int position)
{
    float level = m_panNotes->mappedValue();
    float pan = level/100.f;

    cerr << "pan = " << pan << " (" << position << ")" << endl;

    contextHelpChanged(tr("Notes Pan: %1").arg(position));

    m_analyser->setPan(Analyser::Notes, pan);

    updateMenuStates();
} 

void
MainWindow::increaseNotesPan()
{
    int value = m_panNotes->value();
    value = value + m_panNotes->pageStep();
    if (value > m_panNotes->maximum()) value = m_panNotes->maximum();
    m_panNotes->setValue(value);
}

void
MainWindow::decreaseNotesPan()
{
    int value = m_panNotes->value();
    value = value - m_panNotes->pageStep();
    if (value < m_panNotes->minimum()) value = m_panNotes->minimum();
    m_panNotes->setValue(value);
}

void
MainWindow::restoreNormalNotesPan()
{
    m_panNotes->setValue(m_panNotes->defaultValue());
}

void
MainWindow::updateVisibleRangeDisplay(Pane *p) const
{
    if (!getMainModel() || !p) {
        return;
    }

    bool haveSelection = false;
    int startFrame = 0, endFrame = 0;

    if (m_viewManager && m_viewManager->haveInProgressSelection()) {

        bool exclusive = false;
        Selection s = m_viewManager->getInProgressSelection(exclusive);

        if (!s.isEmpty()) {
            haveSelection = true;
            startFrame = s.getStartFrame();
            endFrame = s.getEndFrame();
        }
    }

    if (!haveSelection) {
        startFrame = p->getFirstVisibleFrame();
        endFrame = p->getLastVisibleFrame();
    }

    RealTime start = RealTime::frame2RealTime
        (startFrame, getMainModel()->getSampleRate());

    RealTime end = RealTime::frame2RealTime
        (endFrame, getMainModel()->getSampleRate());

    RealTime duration = end - start;

    QString startStr, endStr, durationStr;
    startStr = start.toText(true).c_str();
    endStr = end.toText(true).c_str();
    durationStr = duration.toText(true).c_str();

    if (haveSelection) {
        m_myStatusMessage = tr("Selection: %1 to %2 (duration %3)")
            .arg(startStr).arg(endStr).arg(durationStr);
    } else {
        m_myStatusMessage = tr("Visible: %1 to %2 (duration %3)")
            .arg(startStr).arg(endStr).arg(durationStr);
    }
    
    getStatusLabel()->setText(m_myStatusMessage);
}

void
MainWindow::updatePositionStatusDisplays() const
{
    if (!statusBar()->isVisible()) return;

}

void
MainWindow::outputLevelsChanged(float left, float right)
{
    m_fader->setPeakLeft(left);
    m_fader->setPeakRight(right);
}

void
MainWindow::sampleRateMismatch(int /* requested */,
                               int /* actual */,
                               bool /* willResample */)
{
    updateDescriptionLabel();
}

void
MainWindow::audioOverloadPluginDisabled()
{
    QMessageBox::information
        (this, tr("Audio processing overload"),
         tr("<b>Overloaded</b><p>Audio effects plugin auditioning has been disabled due to a processing overload."));
}

void
MainWindow::audioTimeStretchMultiChannelDisabled()
{
    static bool shownOnce = false;
    if (shownOnce) return;
    QMessageBox::information
        (this, tr("Audio processing overload"),
         tr("<b>Overloaded</b><p>Audio playback speed processing has been reduced to a single channel, due to a processing overload."));
    shownOnce = true;
}

void
MainWindow::layerRemoved(Layer *layer)
{
    MainWindowBase::layerRemoved(layer);
}

void
MainWindow::layerInAView(Layer *layer, bool inAView)
{
    MainWindowBase::layerInAView(layer, inAView);
}

void
MainWindow::modelAdded(Model *model)
{
    MainWindowBase::modelAdded(model);
    DenseTimeValueModel *dtvm = qobject_cast<DenseTimeValueModel *>(model);
    if (dtvm) {
        cerr << "A dense time-value model (such as an audio file) has been loaded" << endl;
    }
}

void
MainWindow::modelAboutToBeDeleted(Model *model)
{
    MainWindowBase::modelAboutToBeDeleted(model);
}

void
MainWindow::mainModelChanged(WaveFileModel *model)
{
    m_panLayer->setModel(model);

    MainWindowBase::mainModelChanged(model);

    if (m_playTarget) {
        connect(m_fader, SIGNAL(valueChanged(float)),
                m_playTarget, SLOT(setOutputGain(float)));
    }
}

void
MainWindow::analyseNow()
{
    cerr << "analyseNow called" << endl;
    if (!m_analyser) return;

    CommandHistory::getInstance()->startCompoundOperation
        (tr("Analyse Audio"), true);

    QString error = m_analyser->analyseExistingFile();

    CommandHistory::getInstance()->endCompoundOperation();

    if (error != "") {
        QMessageBox::warning
            (this,
             tr("Failed to analyse audio"),
             tr("<b>Analysis failed</b><p>%1</p>").arg(error),
             QMessageBox::Ok);
    }
}

void
MainWindow::analyseNewMainModel()
{
    WaveFileModel *model = getMainModel();

    cerr << "MainWindow::analyseNewMainModel: main model is " << model << endl;

    cerr << "(document is " << m_document << ", it says main model is " << m_document->getMainModel() << ")" << endl;
    
    if (!model) {
        cerr << "no main model!" << endl;
        return;
    }

    if (!m_paneStack) {
        cerr << "no pane stack!" << endl;
        return;
    }

    int pc = m_paneStack->getPaneCount();
    Pane *pane = 0;
    Pane *selectionStrip = 0;

    if (pc < 2) {
        pane = m_paneStack->addPane();
        selectionStrip = m_paneStack->addPane();
        m_document->addLayerToView
            (selectionStrip,
             m_document->createMainModelLayer(LayerFactory::TimeRuler));
    } else {
        pane = m_paneStack->getPane(0);
        selectionStrip = m_paneStack->getPane(1);
    }

    pane->setPlaybackFollow(PlaybackScrollPage);

    if (selectionStrip) {
        selectionStrip->setPlaybackFollow(PlaybackScrollPage);
        selectionStrip->setFixedHeight(26);
        m_paneStack->sizePanesEqually();
        m_viewManager->clearToolModeOverrides();
        m_viewManager->setToolModeFor(selectionStrip,
                                      ViewManager::SelectMode);
    }

    if (pane) {

        disconnect(pane, SIGNAL(regionOutlined(QRect)),
                   pane, SLOT(zoomToRegion(QRect)));
        connect(pane, SIGNAL(regionOutlined(QRect)),
                this, SLOT(regionOutlined(QRect)));

        QString error = m_analyser->newFileLoaded
            (m_document, getMainModel(), m_paneStack, pane);
        if (error != "") {
            QMessageBox::warning
                (this,
                 tr("Failed to analyse audio"),
                 tr("<b>Analysis failed</b><p>%1</p>").arg(error),
                 QMessageBox::Ok);
        }
    }

    if (!m_withSpectrogram) 
    {
        m_analyser->setVisible(Analyser::Spectrogram, false);
    }

    if (!m_withSonification) 
    {
        m_analyser->setAudible(Analyser::PitchTrack, false);
        m_analyser->setAudible(Analyser::Notes, false);
    }
}

void
MainWindow::modelGenerationFailed(QString transformName, QString message)
{
    if (message != "") {

        QMessageBox::warning
            (this,
             tr("Failed to generate layer"),
             tr("<b>Layer generation failed</b><p>Failed to generate derived layer.<p>The layer transform \"%1\" failed:<p>%2")
             .arg(transformName).arg(message),
             QMessageBox::Ok);
    } else {
        QMessageBox::warning
            (this,
             tr("Failed to generate layer"),
             tr("<b>Layer generation failed</b><p>Failed to generate a derived layer.<p>The layer transform \"%1\" failed.<p>No error information is available.")
             .arg(transformName),
             QMessageBox::Ok);
    }
}

void
MainWindow::modelGenerationWarning(QString /* transformName */, QString message)
{
    QMessageBox::warning
        (this, tr("Warning"), message, QMessageBox::Ok);
}

void
MainWindow::modelRegenerationFailed(QString layerName,
                                    QString transformName, QString message)
{
    if (message != "") {

        QMessageBox::warning
            (this,
             tr("Failed to regenerate layer"),
             tr("<b>Layer generation failed</b><p>Failed to regenerate derived layer \"%1\" using new data model as input.<p>The layer transform \"%2\" failed:<p>%3")
             .arg(layerName).arg(transformName).arg(message),
             QMessageBox::Ok);
    } else {
        QMessageBox::warning
            (this,
             tr("Failed to regenerate layer"),
             tr("<b>Layer generation failed</b><p>Failed to regenerate derived layer \"%1\" using new data model as input.<p>The layer transform \"%2\" failed.<p>No error information is available.")
             .arg(layerName).arg(transformName),
             QMessageBox::Ok);
    }
}

void
MainWindow::modelRegenerationWarning(QString layerName,
                                     QString /* transformName */,
                                     QString message)
{
    QMessageBox::warning
        (this, tr("Warning"), tr("<b>Warning when regenerating layer</b><p>When regenerating the derived layer \"%1\" using new data model as input:<p>%2").arg(layerName).arg(message), QMessageBox::Ok);
}

void
MainWindow::alignmentFailed(QString transformName, QString message)
{
    QMessageBox::warning
        (this,
         tr("Failed to calculate alignment"),
         tr("<b>Alignment calculation failed</b><p>Failed to calculate an audio alignment using transform \"%1\":<p>%2")
         .arg(transformName).arg(message),
         QMessageBox::Ok);
}

void
MainWindow::rightButtonMenuRequested(Pane *pane, QPoint position)
{
//    cerr << "MainWindow::rightButtonMenuRequested(" << pane << ", " << position.x() << ", " << position.y() << ")" << endl;
    m_paneStack->setCurrentPane(pane);
    m_rightButtonMenu->popup(position);
}

void
MainWindow::handleOSCMessage(const OSCMessage &)
{
    cerr << "MainWindow::handleOSCMessage: Not implemented" << endl;
}

void
MainWindow::mouseEnteredWidget()
{
    QWidget *w = qobject_cast<QWidget *>(sender());
    if (!w) return;

    if (w == m_fader) {
        contextHelpChanged(tr("Adjust the master playback level"));
    } else if (w == m_playSpeed) {
        contextHelpChanged(tr("Adjust the master playback speed"));
    } else if (w == m_playSharpen && w->isEnabled()) {
        contextHelpChanged(tr("Toggle transient sharpening for playback time scaling"));
    } else if (w == m_playMono && w->isEnabled()) {
        contextHelpChanged(tr("Toggle mono mode for playback time scaling"));
    }
}

void
MainWindow::mouseLeftWidget()
{
    contextHelpChanged("");
}

void
MainWindow::website()
{
    //!!! todo: URL!
    openHelpUrl(tr("http://code.soundsoftware.ac.uk/projects/tony/"));
}

void
MainWindow::help()
{
    //!!! todo: help URL!
    openHelpUrl(tr("http://code.soundsoftware.ac.uk/projects/tony/wiki/Reference"));
}

void
MainWindow::about()
{
    bool debug = false;
    QString version = "(unknown version)";

#ifdef BUILD_DEBUG
    debug = true;
#endif
    version = tr("Release %1").arg(TONY_VERSION);

    QString aboutText;

    aboutText += tr("<h3>About Tony</h3>");
    aboutText += tr("<p>Tony is a program for interactive note and pitch analysis and annotation.</p>");
    aboutText += tr("<p>%1 : %2 configuration</p>")
        .arg(version)
        .arg(debug ? tr("Debug") : tr("Release"));
    aboutText += tr("<p>Using Qt framework version %1.</p>")
        .arg(QT_VERSION_STR);

    aboutText += 
        "<p>Copyright &copy; 2005&ndash;2014 Chris Cannam, Queen Mary University of London, and the Tony project authors: Matthias Mauch, George Fazekas, Justin Salamon, and Rachel Bittner.</p>"
        "<p>pYIN analysis plugin written by Matthias Mauch.</p>"
        "<p>This program is free software; you can redistribute it and/or "
        "modify it under the terms of the GNU General Public License as "
        "published by the Free Software Foundation; either version 2 of the "
        "License, or (at your option) any later version.<br>See the file "
        "COPYING included with this distribution for more information.</p>";
    
    QMessageBox::about(this, tr("About %1").arg(QApplication::applicationName()), aboutText);
}

void
MainWindow::keyReference()
{
    m_keyReference->show();
}

void
MainWindow::newerVersionAvailable(QString version)
{
    //!!! nicer URL would be nicer
    QSettings settings;
    settings.beginGroup("NewerVersionWarning");
    QString tag = QString("version-%1-available-show").arg(version);
    if (settings.value(tag, true).toBool()) {
        QString title(tr("Newer version available"));
        QString text(tr("<h3>Newer version available</h3><p>You are using version %1 of Tony, but version %2 is now available.</p><p>Please see the <a href=\"http://code.soundsoftware.ac.uk/projects/tony/\">Tony website</a> for more information.</p>").arg(TONY_VERSION).arg(version));
        QMessageBox::information(this, title, text);
        settings.setValue(tag, false);
    }
    settings.endGroup();
}

void
MainWindow::ffwd()
{
    if (!getMainModel()) return;

    int frame = m_viewManager->getPlaybackFrame();
    ++frame;

    size_t sr = getMainModel()->getSampleRate();

    // The step is supposed to scale and be as wide as a step of 
    // m_defaultFfwdRwdStep seconds at zoom level 720 and sr = 44100
    size_t framesPerPixel = m_viewManager->getGlobalZoom();
    size_t defaultZoom = (720 * 44100) / sr;

    float scaler = (framesPerPixel * 1.0f) / defaultZoom;


    frame = RealTime::realTime2Frame
        (RealTime::frame2RealTime(frame, sr) + m_defaultFfwdRwdStep * scaler, sr);
    if (frame > int(getMainModel()->getEndFrame())) {
        frame = getMainModel()->getEndFrame();
    }
        
    if (frame < 0) frame = 0;

    if (m_viewManager->getPlaySelectionMode()) {
        frame = m_viewManager->constrainFrameToSelection(size_t(frame));
    }
    
    m_viewManager->setPlaybackFrame(frame);

    if (frame == (int)getMainModel()->getEndFrame() &&
        m_playSource &&
        m_playSource->isPlaying() &&
        !m_viewManager->getPlayLoopMode()) {
        stop();
    }
}

void
MainWindow::rewind()
{
    if (!getMainModel()) return;

    int frame = m_viewManager->getPlaybackFrame();
    if (frame > 0) --frame;

    size_t sr = getMainModel()->getSampleRate();

    // The step is supposed to scale and be as wide as a step of 
    // m_defaultFfwdRwdStep seconds at zoom level 720 and sr = 44100
    size_t framesPerPixel = m_viewManager->getGlobalZoom();
    size_t defaultZoom = (720 * 44100) / sr;

    float scaler = (framesPerPixel * 1.0f) / defaultZoom;
    frame = RealTime::realTime2Frame
        (RealTime::frame2RealTime(frame, sr) - m_defaultFfwdRwdStep * scaler, sr);
    if (frame < int(getMainModel()->getStartFrame())) {
        frame = getMainModel()->getStartFrame();
    }

    if (frame < 0) frame = 0;

    if (m_viewManager->getPlaySelectionMode()) {
        frame = m_viewManager->constrainFrameToSelection(size_t(frame));
    }

    m_viewManager->setPlaybackFrame(frame);
}
