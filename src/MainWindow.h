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

#include <QFrame>
#include <QString>
#include <QUrl>
#include <QMainWindow>
#include <QPointer>

#include "framework/MainWindowBase.h"
#include "base/Command.h"
#include "view/ViewManager.h"
#include "base/PropertyContainer.h"
#include "base/RecentFiles.h"
#include "layer/LayerFactory.h"
#include "transform/Transform.h"
#include "framework/SVFileReader.h"
#include "widgets/InteractiveFileFinder.h"
#include <map>

class Document;
class PaneStack;
class Pane;
class View;
class Fader;
class Overview;
class Layer;
class WaveformLayer;
class WaveFileModel;
class AudioCallbackPlaySource;
class AudioCallbackPlayTarget;
class CommandHistory;
class QMenu;
class AudioDial;
class QLabel;
class QCheckBox;
class QTreeView;
class QPushButton;
class OSCQueue;
class OSCMessage;
class KeyReference;


class MainWindow : public MainWindowBase
{
    Q_OBJECT

public:
    MainWindow(bool withAudioOutput = true,
               bool withOSCSupport = true);
    virtual ~MainWindow();

public slots:
    virtual bool commitData(bool mayAskUser); // on session shutdown

protected slots:
    virtual void openFile();
    virtual void openLocation();
    virtual void openRecentFile();
    virtual void saveSession();
    virtual void saveSessionAs();
    virtual void newSession();
    virtual void closeSession();

    virtual void renameCurrentLayer();

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

    virtual void sampleRateMismatch(size_t, size_t, bool);
    virtual void audioOverloadPluginDisabled();
    virtual void audioTimeStretchMultiChannelDisabled();

    virtual void outputLevelsChanged(float, float);

    virtual void documentModified();
    virtual void documentRestored();

    virtual void updateMenuStates();
    virtual void updateDescriptionLabel();

    virtual void layerRemoved(Layer *);
    virtual void layerInAView(Layer *, bool);

    virtual void mainModelChanged(WaveFileModel *);
    virtual void modelAdded(Model *);
    virtual void modelReady();
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

protected:
    Overview                *m_overview;
    Fader                   *m_fader;
    AudioDial               *m_playSpeed;
    QPushButton             *m_playSharpen;
    QPushButton             *m_playMono;
    WaveformLayer           *m_panLayer;

    bool                     m_mainMenusCreated;
    QMenu                   *m_playbackMenu;
    QMenu                   *m_recentFilesMenu;
    QMenu                   *m_rightButtonMenu;
    QMenu                   *m_rightButtonPlaybackMenu;

    QAction                 *m_deleteSelectedAction;
    QAction                 *m_ffwdAction;
    QAction                 *m_rwdAction;

    KeyReference            *m_keyReference;

    virtual void setupMenus();
    virtual void setupFileMenu();
    virtual void setupEditMenu();
    virtual void setupViewMenu();
    virtual void setupHelpMenu();
    virtual void setupToolbars();

    virtual void closeEvent(QCloseEvent *e);
    bool checkSaveModified();

    virtual void configureNewPane(Pane *p);
    virtual void updateVisibleRangeDisplay(Pane *p) const;
    virtual void updatePositionStatusDisplays() const;
};


#endif
