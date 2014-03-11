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

#ifndef _MAIN_WINDOW_H_
#define _MAIN_WINDOW_H_

#include "framework/MainWindowBase.h"
#include "Analyser.h"

class VersionTester;

class MainWindow : public MainWindowBase
{
    Q_OBJECT

public:
    MainWindow(bool withAudioOutput = true,
               bool withOSCSupport = true);
    virtual ~MainWindow();

signals:
    virtual void canExportPitchTrack(bool);
    virtual void canExportNotes(bool);

public slots:
    virtual bool commitData(bool mayAskUser); // on session shutdown

protected slots:
    virtual void openFile();
    virtual void openLocation();
    virtual void openRecentFile();
    virtual void saveSession();
    virtual void saveSessionAs();
    virtual void exportPitchLayer();
    virtual void exportNoteLayer();
    virtual void importPitchLayer();
    virtual void newSession();
    virtual void closeSession();

    virtual void toolNavigateSelected();
    virtual void toolEditSelected();
    virtual void toolFreeEditSelected();

    virtual void clearPitches();
    virtual void togglePitchCandidates();
    virtual void switchPitchUp();
    virtual void switchPitchDown();

    virtual void showAudioToggled();
    virtual void showSpectToggled();
    virtual void showPitchToggled();
    virtual void showNotesToggled();

    virtual void playAudioToggled();
    virtual void playPitchToggled();
    virtual void playNotesToggled();

    virtual void editDisplayExtents();

    virtual void doubleClickSelectInvoked(size_t);
    virtual void abandonSelection();

    virtual void paneAdded(Pane *);
    virtual void paneHidden(Pane *);
    virtual void paneAboutToBeDeleted(Pane *);

    virtual void paneDropAccepted(Pane *, QStringList);
    virtual void paneDropAccepted(Pane *, QString);

    virtual void playSpeedChanged(int);
    virtual void playSharpenToggled();
    virtual void playMonoToggled();

    virtual void speedUpPlayback();
    virtual void slowDownPlayback();
    virtual void restoreNormalPlayback();

    virtual void audioGainChanged(int);
    virtual void increaseAudioGain();
    virtual void decreaseAudioGain();
    virtual void restoreNormalAudioGain();

    virtual void pitchGainChanged(int);
    virtual void increasePitchGain();
    virtual void decreasePitchGain();
    virtual void restoreNormalPitchGain();

    virtual void notesGainChanged(int);
    virtual void increaseNotesGain();
    virtual void decreaseNotesGain();
    virtual void restoreNormalNotesGain();

    virtual void audioPanChanged(int);
    virtual void increaseAudioPan();
    virtual void decreaseAudioPan();
    virtual void restoreNormalAudioPan();

    virtual void pitchPanChanged(int);
    virtual void increasePitchPan();
    virtual void decreasePitchPan();
    virtual void restoreNormalPitchPan();

    virtual void notesPanChanged(int);
    virtual void increaseNotesPan();
    virtual void decreaseNotesPan();
    virtual void restoreNormalNotesPan();

    virtual void sampleRateMismatch(size_t, size_t, bool);
    virtual void audioOverloadPluginDisabled();
    virtual void audioTimeStretchMultiChannelDisabled();

    virtual void outputLevelsChanged(float, float);

    virtual void documentModified();
    virtual void documentRestored();

    virtual void updateMenuStates();
    virtual void updateDescriptionLabel();
    virtual void updateLayerStatuses();

    virtual void layerRemoved(Layer *);
    virtual void layerInAView(Layer *, bool);

    virtual void mainModelChanged(WaveFileModel *);
    virtual void modelAdded(Model *);
    virtual void modelAboutToBeDeleted(Model *);

    virtual void modelGenerationFailed(QString, QString);
    virtual void modelGenerationWarning(QString, QString);
    virtual void modelRegenerationFailed(QString, QString, QString);
    virtual void modelRegenerationWarning(QString, QString, QString);
    virtual void alignmentFailed(QString, QString);

    virtual void rightButtonMenuRequested(Pane *, QPoint point);

    virtual void setupRecentFilesMenu();

    virtual void handleOSCMessage(const OSCMessage &);

    virtual void mouseEnteredWidget();
    virtual void mouseLeftWidget();

    virtual void website();
    virtual void help();
    virtual void about();
    virtual void keyReference();

    virtual void selectionChangedByUser();
    virtual void regionOutlined(QRect);

protected:
    Analyser      *m_analyser;

    Overview      *m_overview;
    Fader         *m_fader;
    AudioDial     *m_playSpeed;
    QPushButton   *m_playSharpen;
    QPushButton   *m_playMono;
    WaveformLayer *m_panLayer;

    bool           m_mainMenusCreated;
    QMenu         *m_playbackMenu;
    QMenu         *m_recentFilesMenu;
    QMenu         *m_rightButtonMenu;
    QMenu         *m_rightButtonPlaybackMenu;

    QAction       *m_deleteSelectedAction;
    QAction       *m_ffwdAction;
    QAction       *m_rwdAction;
    QAction       *m_editSelectAction;
    QAction       *m_showCandidatesAction;
    QAction       *m_toggleIntelligenceAction;
    bool           m_intelligentActionOn; // GF: !!! temporary

    QAction       *m_showAudio;
    QAction       *m_showSpect;
    QAction       *m_showPitch;
    QAction       *m_showNotes;
    QAction       *m_playAudio;
    QAction       *m_playPitch;
    QAction       *m_playNotes;
    AudioDial     *m_gainAudio;
    AudioDial     *m_gainPitch;
    AudioDial     *m_gainNotes;
    AudioDial     *m_panAudio;
    AudioDial     *m_panPitch;
    AudioDial     *m_panNotes;

    QLabel        *m_waveformStatus;
    QLabel        *m_pitchStatus;
    QLabel        *m_notesStatus;
    
    KeyReference  *m_keyReference;
    VersionTester *m_versionTester;

    Analyser::FrequencyRange m_pendingConstraint;

    QString exportToSVL(QString path, Layer *layer);
    FileOpenStatus importPitchLayer(FileSource source);

    virtual void setupMenus();
    virtual void setupFileMenu();
    virtual void setupEditMenu();
    virtual void setupViewMenu();
    virtual void setupHelpMenu();
    virtual void setupToolbars();

    virtual void octaveShift(bool up);

    virtual void closeEvent(QCloseEvent *e);
    bool checkSaveModified();

    virtual void updateVisibleRangeDisplay(Pane *p) const;
    virtual void updatePositionStatusDisplays() const;
};


#endif
