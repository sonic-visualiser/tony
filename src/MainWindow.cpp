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
#include "framework/Document.h"

#include "view/Pane.h"
#include "view/PaneStack.h"
#include "data/model/WaveFileModel.h"
#include "view/ViewManager.h"
#include "base/Preferences.h"
#include "layer/WaveformLayer.h"
#include "layer/TimeInstantLayer.h"
#include "layer/TimeValueLayer.h"
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

#include <iostream>
#include <cstdio>
#include <errno.h>

using std::cerr;
using std::endl;

using std::vector;


MainWindow::MainWindow(bool withAudioOutput, bool withOSCSupport) :
    MainWindowBase(withAudioOutput, withOSCSupport, false),
    m_overview(0),
    m_mainMenusCreated(false),
    m_playbackMenu(0),
    m_recentFilesMenu(0),
    m_rightButtonMenu(0),
    m_rightButtonPlaybackMenu(0),
    m_deleteSelectedAction(0),
    m_ffwdAction(0),
    m_rwdAction(0),
    m_keyReference(new KeyReference())
{
    setWindowTitle(QApplication::applicationName());

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
    cdb->setUseDarkBackground(cdb->addColour(Qt::white, tr("White")), true);
    cdb->setUseDarkBackground(cdb->addColour(Qt::red, tr("Bright Red")), true);
    cdb->setUseDarkBackground(cdb->addColour(QColor(30, 150, 255), tr("Bright Blue")), true);
    cdb->setUseDarkBackground(cdb->addColour(Qt::green, tr("Bright Green")), true);
    cdb->setUseDarkBackground(cdb->addColour(QColor(225, 74, 255), tr("Bright Purple")), true);
    cdb->setUseDarkBackground(cdb->addColour(QColor(255, 188, 80), tr("Bright Orange")), true);

    Preferences::getInstance()->setResampleOnLoad(true);
    Preferences::getInstance()->setSpectrogramSmoothing
        (Preferences::SpectrogramInterpolated);

    QSettings settings;

    settings.beginGroup("LayerDefaults");

    settings.setValue("waveform",
                      QString("<layer scale=\"%1\" channelMode=\"%2\"/>")
                      .arg(int(WaveformLayer::MeterScale))
//                      .arg(int(WaveformLayer::LinearScale))
                      .arg(int(WaveformLayer::MergeChannels)));

    settings.setValue("timevalues",
                      QString("<layer plotStyle=\"%1\"/>")
                      .arg(int(TimeValueLayer::PlotStems)));

    settings.setValue("spectrogram",
                      QString("<layer channel=\"-1\" windowSize=\"2048\" windowHopLevel=\"2\"/>"));

    settings.setValue("melodicrange",
                      QString("<layer channel=\"-1\" gain=\"10\" normalizeVisibleArea=\"false\" normalizeColumns=\"false\" minFrequency=\"100\" maxFrequency=\"1200\" windowSize=\"4096\" windowOverlap=\"75\" binDisplay=\"0\" />"));

    settings.endGroup();

    settings.beginGroup("MainWindow");
    settings.setValue("showstatusbar", false);
    settings.endGroup();

    m_viewManager->setAlignMode(true);
    m_viewManager->setPlaySoloMode(true);
    m_viewManager->setToolMode(ViewManager::NavigateMode);
    m_viewManager->setZoomWheelsEnabled(false);
    m_viewManager->setIlluminateLocalFeatures(false);
    m_viewManager->setShowWorkTitle(true);

    QFrame *frame = new QFrame;
    setCentralWidget(frame);

    QGridLayout *layout = new QGridLayout;
    
    m_descriptionLabel = new QLabel;

    QScrollArea *scroll = new QScrollArea(frame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);

    m_paneStack->setLayoutStyle(PaneStack::NoPropertyStacks);
    scroll->setWidget(m_paneStack);
    
    m_overview = new Overview(frame);
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
    m_overview->hide();

    m_panLayer = new WaveformLayer;
    m_panLayer->setChannelMode(WaveformLayer::MergeChannels);
    m_panLayer->setAggressiveCacheing(true);
    m_overview->addLayer(m_panLayer);

    if (m_viewManager->getGlobalDarkBackground()) {
        m_panLayer->setBaseColour
            (ColourDatabase::getInstance()->getColourIndex(tr("Bright Green")));
    } else {
        m_panLayer->setBaseColour
            (ColourDatabase::getInstance()->getColourIndex(tr("Green")));
    }        

    m_fader = new Fader(frame, false);
    connect(m_fader, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
    connect(m_fader, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));

    m_playSpeed = new AudioDial(frame);
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

    layout->setSpacing(4);
    layout->addWidget(scroll, 0, 0, 1, 6);
    layout->addWidget(m_overview, 1, 1);
    layout->addWidget(m_fader, 1, 2);
    layout->addWidget(m_playSpeed, 1, 3);

    m_paneStack->setPropertyStackMinWidth
        (m_fader->width() + m_playSpeed->width() +
         layout->spacing() * 4);

    layout->setColumnStretch(1, 10);

    frame->setLayout(layout);

    setupMenus();
    setupToolbars();
    setupHelpMenu();

    statusBar();

    newSession();
}

MainWindow::~MainWindow()
{
    delete m_keyReference;
    Profiles::getInstance()->dump();
}

void
MainWindow::setupMenus()
{
    if (!m_mainMenusCreated) {
        m_rightButtonMenu = new QMenu();
    }

    if (!m_mainMenusCreated) {
        CommandHistory::getInstance()->registerMenu(m_rightButtonMenu);
        m_rightButtonMenu->addSeparator();
    }

    setupFileMenu();
    setupEditMenu();
    setupViewMenu();

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
    action->setStatusTip(tr("Open a file"));
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
    action = new QAction(tr("Scroll &Left"), this);
    action->setShortcut(tr("Left"));
    action->setStatusTip(tr("Scroll the current pane to the left"));
    connect(action, SIGNAL(triggered()), this, SLOT(scrollLeft()));
    connect(this, SIGNAL(canScroll(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
	
    action = new QAction(tr("Scroll &Right"), this);
    action->setShortcut(tr("Right"));
    action->setStatusTip(tr("Scroll the current pane to the right"));
    connect(action, SIGNAL(triggered()), this, SLOT(scrollRight()));
    connect(this, SIGNAL(canScroll(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
	
    action = new QAction(tr("&Jump Left"), this);
    action->setShortcut(tr("Ctrl+Left"));
    action->setStatusTip(tr("Scroll the current pane a big step to the left"));
    connect(action, SIGNAL(triggered()), this, SLOT(jumpLeft()));
    connect(this, SIGNAL(canScroll(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
	
    action = new QAction(tr("J&ump Right"), this);
    action->setShortcut(tr("Ctrl+Right"));
    action->setStatusTip(tr("Scroll the current pane a big step to the right"));
    connect(action, SIGNAL(triggered()), this, SLOT(jumpRight()));
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

    m_keyReference->setCategory(tr("Display Features"));

    QActionGroup *overlayGroup = new QActionGroup(this);
        
    action = new QAction(tr("Show &No Overlays"), this);
    action->setShortcut(tr("0"));
    action->setStatusTip(tr("Hide centre indicator, frame times, layer names and scale"));
    connect(action, SIGNAL(triggered()), this, SLOT(showNoOverlays()));
    action->setCheckable(true);
    action->setChecked(false);
    overlayGroup->addAction(action);
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
        
    action = new QAction(tr("Show &Minimal Overlays"), this);
    action->setShortcut(tr("9"));
    action->setStatusTip(tr("Show centre indicator only"));
    connect(action, SIGNAL(triggered()), this, SLOT(showMinimalOverlays()));
    action->setCheckable(true);
    action->setChecked(false);
    overlayGroup->addAction(action);
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
        
    action = new QAction(tr("Show &Standard Overlays"), this);
    action->setShortcut(tr("8"));
    action->setStatusTip(tr("Show centre indicator, frame times and scale"));
    connect(action, SIGNAL(triggered()), this, SLOT(showStandardOverlays()));
    action->setCheckable(true);
    action->setChecked(true);
    overlayGroup->addAction(action);
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
        
    action = new QAction(tr("Show &All Overlays"), this);
    action->setShortcut(tr("7"));
    action->setStatusTip(tr("Show all texts and scale"));
    connect(action, SIGNAL(triggered()), this, SLOT(showAllOverlays()));
    action->setCheckable(true);
    action->setChecked(false);
    overlayGroup->addAction(action);
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
        
    menu->addSeparator();

    action = new QAction(tr("Show &Zoom Wheels"), this);
    action->setShortcut(tr("Z"));
    action->setStatusTip(tr("Show thumbwheels for zooming horizontally and vertically"));
    connect(action, SIGNAL(triggered()), this, SLOT(toggleZoomWheels()));
    action->setCheckable(true);
    action->setChecked(m_viewManager->getZoomWheelsEnabled());
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
        
    action = new QAction(tr("Show Property Bo&xes"), this);
    action->setShortcut(tr("X"));
    action->setStatusTip(tr("Show the layer property boxes at the side of the main window"));
    connect(action, SIGNAL(triggered()), this, SLOT(togglePropertyBoxes()));
    action->setCheckable(true);
    action->setChecked(false); //!!!
    m_keyReference->registerShortcut(action);
    menu->addAction(action);

    action = new QAction(tr("Show Status &Bar"), this);
    action->setStatusTip(tr("Show context help information in the status bar at the bottom of the window"));
    connect(action, SIGNAL(triggered()), this, SLOT(toggleStatusBar()));
    action->setCheckable(true);
    action->setChecked(true);
    menu->addAction(action);

    QSettings settings;
    settings.beginGroup("MainWindow");
    bool sb = settings.value("showstatusbar", true).toBool();
    if (!sb) {
        action->setChecked(false);
        statusBar()->hide();
    }
    settings.endGroup();
}

void
MainWindow::setupHelpMenu()
{
    QMenu *menu = menuBar()->addMenu(tr("&Help"));
    menu->setTearOffEnabled(true);
    
    m_keyReference->setCategory(tr("Help"));

    IconLoader il;

    QString name = QApplication::applicationName();

    QAction *action = new QAction(il.load("help"),
                                  tr("&Help Reference"), this); 
    action->setShortcut(tr("F1"));
    action->setStatusTip(tr("Open the %1 reference manual").arg(name)); 
    connect(action, SIGNAL(triggered()), this, SLOT(help()));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);

    action = new QAction(tr("&Key and Mouse Reference"), this);
    action->setShortcut(tr("F2"));
    action->setStatusTip(tr("Open a window showing the keystrokes you can use in %1").arg(name));
    connect(action, SIGNAL(triggered()), this, SLOT(keyReference()));
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
                 action->shortcut(),
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
    m_rwdAction->setShortcut(tr("PgUp"));
    m_rwdAction->setStatusTip(tr("Rewind to the previous time instant or time ruler notch"));
    connect(m_rwdAction, SIGNAL(triggered()), this, SLOT(rewind()));
    connect(this, SIGNAL(canRewind(bool)), m_rwdAction, SLOT(setEnabled(bool)));

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
    m_ffwdAction->setShortcut(tr("PgDown"));
    m_ffwdAction->setStatusTip(tr("Fast-forward to the next time instant or time ruler notch"));
    connect(m_ffwdAction, SIGNAL(triggered()), this, SLOT(ffwd()));
    connect(this, SIGNAL(canFfwd(bool)), m_ffwdAction, SLOT(setEnabled(bool)));

    QAction *ffwdEndAction = toolbar->addAction(il.load("ffwd-end"),
                                                tr("Fast Forward to End"));
    ffwdEndAction->setShortcut(tr("End"));
    ffwdEndAction->setStatusTip(tr("Fast-forward to the end"));
    connect(ffwdEndAction, SIGNAL(triggered()), this, SLOT(ffwdEnd()));
    connect(this, SIGNAL(canPlay(bool)), ffwdEndAction, SLOT(setEnabled(bool)));
/*
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

    QAction *soAction = toolbar->addAction(il.load("solo"),
                                           tr("Solo Current Pane"));
    soAction->setCheckable(true);
    soAction->setChecked(m_viewManager->getPlaySoloMode());
    soAction->setShortcut(tr("o"));
    soAction->setStatusTip(tr("Solo the current pane during playback"));
    connect(m_viewManager, SIGNAL(playSoloModeChanged(bool)),
            soAction, SLOT(setChecked(bool)));
    connect(soAction, SIGNAL(triggered()), this, SLOT(playSoloToggled()));
    connect(this, SIGNAL(canPlay(bool)), soAction, SLOT(setEnabled(bool)));

    m_keyReference->registerShortcut(psAction);
    m_keyReference->registerShortcut(plAction);
    m_keyReference->registerShortcut(soAction);
*/
    m_keyReference->registerShortcut(playAction);
    m_keyReference->registerShortcut(m_rwdAction);
    m_keyReference->registerShortcut(m_ffwdAction);
    m_keyReference->registerShortcut(rwdStartAction);
    m_keyReference->registerShortcut(ffwdEndAction);

/*
    menu->addAction(psAction);
    menu->addAction(plAction);
    menu->addAction(soAction);
*/
    menu->addAction(playAction);
    menu->addSeparator();
    menu->addAction(m_rwdAction);
    menu->addAction(m_ffwdAction);
    menu->addSeparator();
    menu->addAction(rwdStartAction);
    menu->addAction(ffwdEndAction);
    menu->addSeparator();

    m_rightButtonPlaybackMenu->addAction(playAction);
/*
    m_rightButtonPlaybackMenu->addAction(psAction);
    m_rightButtonPlaybackMenu->addAction(plAction);
    m_rightButtonPlaybackMenu->addAction(soAction);
*/
    m_rightButtonPlaybackMenu->addSeparator();
    m_rightButtonPlaybackMenu->addAction(m_rwdAction);
    m_rightButtonPlaybackMenu->addAction(m_ffwdAction);
    m_rightButtonPlaybackMenu->addSeparator();
    m_rightButtonPlaybackMenu->addAction(rwdStartAction);
    m_rightButtonPlaybackMenu->addAction(ffwdEndAction);
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
/*
    toolbar = addToolBar(tr("Edit Toolbar"));
    CommandHistory::getInstance()->registerToolbar(toolbar);
*/

    Pane::registerShortcuts(*m_keyReference);
}

void
MainWindow::updateMenuStates()
{
    MainWindowBase::updateMenuStates();

    Pane *currentPane = 0;
    Layer *currentLayer = 0;

    if (m_paneStack) currentPane = m_paneStack->getCurrentPane();
    if (currentPane) currentLayer = currentPane->getSelectedLayer();

    bool haveCurrentPane =
        (currentPane != 0);
    bool haveCurrentLayer =
        (haveCurrentPane &&
         (currentLayer != 0));
    bool haveSelection = 
	(m_viewManager &&
	 !m_viewManager->getSelections().empty());
    bool haveCurrentEditableLayer =
	(haveCurrentLayer &&
	 currentLayer->isLayerEditable());
    bool haveCurrentTimeInstantsLayer = 
	(haveCurrentLayer &&
	 qobject_cast<TimeInstantLayer *>(currentLayer));
    bool haveCurrentTimeValueLayer = 
	(haveCurrentLayer &&
	 qobject_cast<TimeValueLayer *>(currentLayer));

    emit canChangePlaybackSpeed(true);
    int v = m_playSpeed->value();
    emit canSpeedUpPlayback(v < m_playSpeed->maximum());
    emit canSlowDownPlayback(v > m_playSpeed->minimum());

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
MainWindow::updateDescriptionLabel()
{
    if (!getMainModel()) {
	m_descriptionLabel->setText(tr("No audio file loaded."));
	return;
    }

    QString description;

    size_t ssr = getMainModel()->getSampleRate();
    size_t tsr = ssr;
    if (m_playSource) tsr = m_playSource->getTargetSampleRate();

    if (ssr != tsr) {
	description = tr("%1Hz (resampling to %2Hz)").arg(ssr).arg(tsr);
    } else {
	description = QString("%1Hz").arg(ssr);
    }

    description = QString("%1 - %2")
	.arg(RealTime::frame2RealTime(getMainModel()->getEndFrame(), ssr)
	     .toText(false).c_str())
	.arg(description);

    m_descriptionLabel->setText(description);
}

void
MainWindow::documentModified()
{
    //!!!
    MainWindowBase::documentModified();
}

void
MainWindow::documentRestored()
{
    //!!!
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

    connect(pane, SIGNAL(contextHelpChanged(const QString &)),
            this, SLOT(contextHelpChanged(const QString &)));

    Layer *waveform = m_document->createMainModelLayer(LayerFactory::Waveform);
    m_document->addLayerToView(pane, waveform);

    m_overview->registerView(pane);

    CommandHistory::getInstance()->clear();
    CommandHistory::getInstance()->documentSaved();
    documentRestored();
    updateMenuStates();
}

void
MainWindow::closeSession()
{
    if (!checkSaveModified()) return;

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

    FileOpenStatus status = open(path, ReplaceSession);

    if (status == FileOpenFailed) {
        QMessageBox::critical(this, tr("Failed to open file"),
                              tr("<b>File open failed</b><p>File \"%1\" could not be opened").arg(path));
    } else if (status == FileOpenWrongMode) {
        QMessageBox::critical(this, tr("Failed to open file"),
                              tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
    } else {
        configureNewPane(m_paneStack->getCurrentPane());
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

    FileOpenStatus status = open(text, ReplaceSession);

    if (status == FileOpenFailed) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Open failed</b><p>URL \"%1\" could not be opened").arg(text));
    } else if (status == FileOpenWrongMode) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
    } else {
        configureNewPane(m_paneStack->getCurrentPane());
    }
}

void
MainWindow::openRecentFile()
{
    QObject *obj = sender();
    QAction *action = qobject_cast<QAction *>(obj);
    
    if (!action) {
	std::cerr << "WARNING: MainWindow::openRecentFile: sender is not an action"
		  << std::endl;
	return;
    }

    QString path = action->text();
    if (path == "") return;

    FileOpenStatus status = open(path, ReplaceSession);

    if (status == FileOpenFailed) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Open failed</b><p>File or URL \"%1\" could not be opened").arg(path));
    } else if (status == FileOpenWrongMode) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
    } else {
        configureNewPane(m_paneStack->getCurrentPane());
    }
}

void
MainWindow::paneAdded(Pane *pane)
{
    pane->setPlaybackFollow(PlaybackScrollContinuous);
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
//    if (pane) m_paneStack->setCurrentPane(pane);

    for (QStringList::iterator i = uriList.begin(); i != uriList.end(); ++i) {

        FileOpenStatus status = open(*i, ReplaceSession);

        if (status == FileOpenFailed) {
            QMessageBox::critical(this, tr("Failed to open dropped URL"),
                                  tr("<b>Open failed</b><p>Dropped URL \"%1\" could not be opened").arg(*i));
        } else if (status == FileOpenWrongMode) {
            QMessageBox::critical(this, tr("Failed to open dropped URL"),
                                  tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
        } else {
            configureNewPane(m_paneStack->getCurrentPane());
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
MainWindow::configureNewPane(Pane *pane)
{
    std::cerr << "MainWindow::configureNewPane(" << pane << ")" << std::endl;

    if (!pane) return;

    Layer *waveformLayer = 0;

    for (int i = 0; i < pane->getLayerCount(); ++i) {
        Layer *layer = pane->getLayer(i);
        if (!layer) continue;
        if (qobject_cast<WaveformLayer *>(layer)) waveformLayer = layer;
        if (qobject_cast<TimeValueLayer *>(layer)) return;
    }
    if (!waveformLayer) return;

    waveformLayer->setObjectName(tr("Waveform"));

    zoomToFit();
}

void
MainWindow::closeEvent(QCloseEvent *e)
{
//    std::cerr << "MainWindow::closeEvent" << std::endl;

    if (m_openingAudioFile) {
//        std::cerr << "Busy - ignoring close event" << std::endl;
	e->ignore();
	return;
    }

    if (!m_abandoning && !checkSaveModified()) {
//        std::cerr << "Ignoring close event" << std::endl;
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

void
MainWindow::saveSession()
{
    if (m_sessionFile != "") {
	if (!saveSessionFile(m_sessionFile)) {
	    QMessageBox::critical(this, tr("Failed to save file"),
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
MainWindow::saveSessionAs()
{
    QString orig = m_audioFile;
    if (orig == "") orig = ".";
    else orig = QFileInfo(orig).absoluteDir().canonicalPath();

    QString path = getSaveFileName(FileFinder::SessionFile);

    if (path == "") return;

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
MainWindow::renameCurrentLayer()
{
    Pane *pane = m_paneStack->getCurrentPane();
    if (pane) {
	Layer *layer = pane->getSelectedLayer();
	if (layer) {
	    bool ok = false;
	    QString newName = QInputDialog::getText
		(this, tr("Rename Layer"),
		 tr("New name for this layer:"),
		 QLineEdit::Normal, layer->objectName(), &ok);
	    if (ok) {
		layer->setObjectName(newName);
	    }
	}
    }
}

void
MainWindow::playSpeedChanged(int position)
{
    PlaySpeedRangeMapper mapper(0, 200);

    float percent = m_playSpeed->mappedValue();
    float factor = mapper.getFactorForValue(percent);

    std::cerr << "speed = " << position << " percent = " << percent << " factor = " << factor << std::endl;

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
}

void
MainWindow::playMonoToggled()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("playmono", m_playMono->isChecked());
    settings.endGroup();

    playSpeedChanged(m_playSpeed->value());
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
MainWindow::updateVisibleRangeDisplay(Pane *p) const
{
    if (!getMainModel() || !p) {
        return;
    }

    bool haveSelection = false;
    size_t startFrame = 0, endFrame = 0;

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

    statusBar()->showMessage(m_myStatusMessage);
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
MainWindow::sampleRateMismatch(size_t requested, size_t actual,
                               bool willResample)
{
    if (!willResample) {
        //!!! more helpful message needed
        QMessageBox::information
            (this, tr("Sample rate mismatch"),
             tr("The sample rate of this audio file (%1 Hz) does not match\nthe current playback rate (%2 Hz).\n\nThe file will play at the wrong speed and pitch.")
             .arg(requested).arg(actual));
    }        

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
        std::cerr << "A dense time-value model (such as an audio file) has been loaded" << std::endl;
    }
}

void
MainWindow::modelReady()
{
    QObject *s = sender();
    std::cerr << "MainWindow::modelReady(" << s << ")" << std::endl;
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
MainWindow::modelGenerationWarning(QString transformName, QString message)
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
                                     QString transformName, QString message)
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
//    std::cerr << "MainWindow::rightButtonMenuRequested(" << pane << ", " << position.x() << ", " << position.y() << ")" << std::endl;
    m_paneStack->setCurrentPane(pane);
    m_rightButtonMenu->popup(position);
}

void
MainWindow::handleOSCMessage(const OSCMessage &message)
{
    std::cerr << "MainWindow::handleOSCMessage: Not implemented" << std::endl;
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
    openHelpUrl(tr("http://www.sonicvisualiser.org/"));
}

void
MainWindow::help()
{
    openHelpUrl(tr("http://www.sonicvisualiser.org/doc/reference/1.0/en/"));
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

#ifndef BUILD_STATIC
    aboutText += tr("<br>Using Qt v%1 &copy; Trolltech AS").arg(QT_VERSION_STR);
#else
#ifdef QT_SHARED
    aboutText += tr("<br>Using Qt v%1 &copy; Trolltech AS").arg(QT_VERSION_STR);
#endif
#endif

#ifdef BUILD_STATIC
    aboutText += tr("<p>Statically linked");
#ifndef QT_SHARED
    aboutText += tr("<br>With Qt (v%1) &copy; Trolltech AS").arg(QT_VERSION_STR);
#endif
#ifdef HAVE_JACK
#ifdef JACK_VERSION
    aboutText += tr("<br>With JACK audio output (v%1) &copy; Paul Davis and Jack O'Quin").arg(JACK_VERSION);
#else
    aboutText += tr("<br>With JACK audio output &copy; Paul Davis and Jack O'Quin");
#endif
#endif
#ifdef HAVE_PORTAUDIO
    aboutText += tr("<br>With PortAudio audio output &copy; Ross Bencina and Phil Burk");
#endif
#ifdef HAVE_OGGZ
#ifdef OGGZ_VERSION
    aboutText += tr("<br>With Ogg file decoder (oggz v%1, fishsound v%2) &copy; CSIRO Australia").arg(OGGZ_VERSION).arg(FISHSOUND_VERSION);
#else
    aboutText += tr("<br>With Ogg file decoder &copy; CSIRO Australia");
#endif
#endif
#ifdef HAVE_MAD
#ifdef MAD_VERSION
    aboutText += tr("<br>With MAD mp3 decoder (v%1) &copy; Underbit Technologies Inc").arg(MAD_VERSION);
#else
    aboutText += tr("<br>With MAD mp3 decoder &copy; Underbit Technologies Inc");
#endif
#endif
#ifdef HAVE_SAMPLERATE
#ifdef SAMPLERATE_VERSION
    aboutText += tr("<br>With libsamplerate (v%1) &copy; Erik de Castro Lopo").arg(SAMPLERATE_VERSION);
#else
    aboutText += tr("<br>With libsamplerate &copy; Erik de Castro Lopo");
#endif
#endif
#ifdef HAVE_SNDFILE
#ifdef SNDFILE_VERSION
    aboutText += tr("<br>With libsndfile (v%1) &copy; Erik de Castro Lopo").arg(SNDFILE_VERSION);
#else
    aboutText += tr("<br>With libsndfile &copy; Erik de Castro Lopo");
#endif
#endif
#ifdef HAVE_FFTW3F
#ifdef FFTW3_VERSION
    aboutText += tr("<br>With FFTW3 (v%1) &copy; Matteo Frigo and MIT").arg(FFTW3_VERSION);
#else
    aboutText += tr("<br>With FFTW3 &copy; Matteo Frigo and MIT");
#endif
#endif
#ifdef HAVE_VAMP
    aboutText += tr("<br>With Vamp plugin support (API v%1, host SDK v%2) &copy; Chris Cannam").arg(VAMP_API_VERSION).arg(VAMP_SDK_VERSION);
#endif
    aboutText += tr("<br>With LADSPA plugin support (API v%1) &copy; Richard Furse, Paul Davis, Stefan Westerfeld").arg(LADSPA_VERSION);
    aboutText += tr("<br>With DSSI plugin support (API v%1) &copy; Chris Cannam, Steve Harris, Sean Bolton").arg(DSSI_VERSION);
#ifdef HAVE_LIBLO
#ifdef LIBLO_VERSION
    aboutText += tr("<br>With liblo Lite OSC library (v%1) &copy; Steve Harris").arg(LIBLO_VERSION);
#else
    aboutText += tr("<br>With liblo Lite OSC library &copy; Steve Harris").arg(LIBLO_VERSION);
#endif
    if (m_oscQueue && m_oscQueue->isOK()) {
        aboutText += tr("<p>The OSC URL for this instance is: \"%1\"").arg(m_oscQueue->getOSCURL());
    }
#endif
    aboutText += "</p>";
#endif

    aboutText += 
        "<p>Copyright &copy; 2005&ndash;2012 Chris Cannam and<br>"
        "Queen Mary, University of London.</p>"
        "<p>This program is free software; you can redistribute it and/or<br>"
        "modify it under the terms of the GNU General Public License as<br>"
        "published by the Free Software Foundation; either version 2 of the<br>"
        "License, or (at your option) any later version.<br>See the file "
        "COPYING included with this distribution for more information.</p>";
    
    QMessageBox::about(this, tr("About %1").arg(QApplication::applicationName()), aboutText);
}

void
MainWindow::keyReference()
{
    m_keyReference->show();
}


