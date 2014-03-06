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

#include "MainWindow.h"

#include "system/System.h"
#include "system/Init.h"
#include "base/TempDirectory.h"
#include "base/PropertyContainer.h"
#include "base/Preferences.h"
#include "widgets/TipDialog.h"
#include "transform/TransformFactory.h"

#include <QMetaType>
#include <QApplication>
#include <QDesktopWidget>
#include <QMessageBox>
#include <QTranslator>
#include <QLocale>
#include <QSettings>
#include <QIcon>
#include <QSessionManager>
#include <QSplashScreen>
#include <QFileOpenEvent>
#include <QDir>

#include <iostream>
#include <signal.h>

static QMutex cleanupMutex;
static bool cleanedUp = false;

static void
signalHandler(int /* signal */)
{
    // Avoid this happening more than once across threads

    cerr << "signalHandler: cleaning up and exiting" << endl;
    cleanupMutex.lock();
    if (!cleanedUp) {
        TempDirectory::getInstance()->cleanup();
        cleanedUp = true;
    }
    cleanupMutex.unlock();
    exit(0);
}

class TonyApplication : public QApplication
{
public:
    TonyApplication(int argc, char **argv) :
        QApplication(argc, argv),
        m_mainWindow(0),
        m_readyForFiles(false)
    {
    }
    virtual ~TonyApplication() {
    }

    void setMainWindow(MainWindow *mw) { m_mainWindow = mw; }
    void releaseMainWindow() { m_mainWindow = 0; }

    virtual void commitData(QSessionManager &manager) {
        if (!m_mainWindow) return;
        bool mayAskUser = manager.allowsInteraction();
        bool success = m_mainWindow->commitData(mayAskUser);
        manager.release();
        if (!success) manager.cancel();
    }

    void readyForFiles() {
        m_readyForFiles = true;
    }

    void handleFilepathArgument(QString path, QSplashScreen *splash);

    void handleQueuedPaths(QSplashScreen *splash) {
        foreach (QString f, m_filepathQueue) {
            handleFilepathArgument(f, splash);
        }
    }
    
protected:
    MainWindow *m_mainWindow;
    
    bool m_readyForFiles;
    QStringList m_filepathQueue;

    virtual bool event(QEvent *event) {
        switch (event->type()) {
        case QEvent::FileOpen:
        {
            QString path = static_cast<QFileOpenEvent *>(event)->file();
            if (m_readyForFiles) {
                handleFilepathArgument(path, NULL);
            } else {
                m_filepathQueue.append(path);
            }
            return true;
        }
        default:
            return QApplication::event(event);
        }
    }
};
        
int
main(int argc, char **argv)
{
    svSystemSpecificInitialisation();

#ifdef Q_OS_MAC
    if (QSysInfo::MacintoshVersion > QSysInfo::MV_10_8) {
        // Fix for OS/X 10.9 font problem
        QFont::insertSubstitution(".Lucida Grande UI", "Lucida Grande");
    }
#endif

    TonyApplication application(argc, argv);

    // For some weird reason, Mac builds are crashing on startup when
    // this line is present. Eliminate it on that platform for now.
#ifndef Q_OS_MAC
    QStringList args = application.arguments();
#else
    cerr << "NOTE: Command-line arguments are currently disabled on Mac, see comments in main.cpp" << endl;
    QStringList args;
#endif

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

#ifndef Q_OS_WIN32
    signal(SIGHUP,  signalHandler);
    signal(SIGQUIT, signalHandler);
#endif

    bool audioOutput = true;

    if (args.contains("--help") || args.contains("-h") || args.contains("-?")) {
        std::cerr << QApplication::tr(
            "\nTony is a program for interactive note and pitch analysis and annotation.\n\nUsage:\n\n  %1 [--no-audio] [--no-osc] [<file> ...]\n\n  --no-audio: Do not attempt to open an audio output device\n  <file>: One or more Tony (.ton) and audio files may be provided.\n").arg(argv[0]).toStdString() << std::endl;
        exit(2);
    }

    if (args.contains("--no-audio")) audioOutput = false;

    QApplication::setOrganizationName("QMUL");
    QApplication::setOrganizationDomain("qmul.ac.uk");
    QApplication::setApplicationName("Tony");

    QSplashScreen *splash = 0;
    // If we had a splash screen, we would show it here

    QIcon icon;
    int sizes[] = { 16, 22, 24, 32, 48, 64, 128 };
    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
        icon.addFile(QString(":icons/tony-%1x%2.png").arg(sizes[i]).arg(sizes[i]));
    }
    QApplication::setWindowIcon(icon);

    QString language = QLocale::system().name();

    QTranslator qtTranslator;
    QString qtTrName = QString("qt_%1").arg(language);
    std::cerr << "Loading " << qtTrName.toStdString() << "..." << std::endl;
    bool success = false;
    if (!(success = qtTranslator.load(qtTrName))) {
        QString qtDir = getenv("QTDIR");
        if (qtDir != "") {
            success = qtTranslator.load
                (qtTrName, QDir(qtDir).filePath("translations"));
        }
    }
    if (!success) {
        std::cerr << "Failed to load Qt translation for locale" << std::endl;
    }
    application.installTranslator(&qtTranslator);

    StoreStartupLocale();

    // Permit size_t and PropertyName to be used as args in queued signal calls
    qRegisterMetaType<size_t>("size_t");
    qRegisterMetaType<PropertyContainer::PropertyName>("PropertyContainer::PropertyName");

    MainWindow *gui = new MainWindow(audioOutput, false); // no osc support
    application.setMainWindow(gui);
    if (splash) {
        QObject::connect(gui, SIGNAL(hideSplash()), splash, SLOT(hide()));
    }

    QDesktopWidget *desktop = QApplication::desktop();
    QRect available = desktop->availableGeometry();

    int width = (available.width() * 2) / 3;
    int height = available.height() / 2;
    if (height < 450) height = (available.height() * 2) / 3;
    if (width > height * 2) width = height * 2;

    QSettings settings;
    settings.beginGroup("MainWindow");
    QSize size = settings.value("size", QSize(width, height)).toSize();
    gui->resizeConstrained(size);
    if (settings.contains("position")) {
        QRect prevrect(settings.value("position").toPoint(), size);
        if (!(available & prevrect).isEmpty()) {
            gui->move(prevrect.topLeft());
        }
    }
    settings.endGroup();
    
    gui->show();

    application.readyForFiles();
    
    for (QStringList::iterator i = args.begin(); i != args.end(); ++i) {

        if (i == args.begin()) continue;
        if (i->startsWith('-')) continue;

        QString path = *i;

        application.handleFilepathArgument(path, splash);
    }

    application.handleQueuedPaths(splash);
        
    if (splash) splash->finish(gui);
    delete splash;

    int rv = application.exec();

    gui->hide();

    cleanupMutex.lock();

    if (!cleanedUp) {
        TransformFactory::deleteInstance();
        TempDirectory::getInstance()->cleanup();
        cleanedUp = true;
    }

    application.releaseMainWindow();

    delete gui;

    cleanupMutex.unlock();

    return rv;
}

/** Application-global handler for filepaths passed in, e.g. as
 * command-line arguments or apple events */

void TonyApplication::handleFilepathArgument(QString path,
                                             QSplashScreen *splash)
{
    static bool haveSession = false;
    static bool haveMainModel = false;
    static bool havePriorCommandLineModel = false;

    if (!m_mainWindow) return;

    MainWindow::FileOpenStatus status = MainWindow::FileOpenFailed;

#ifdef Q_OS_WIN32
    path.replace("\\", "/");
#endif

    if (path.endsWith("ton")) {
        if (!haveSession) {
            status = m_mainWindow->openSessionFile(path);
            if (status == MainWindow::FileOpenSucceeded) {
                haveSession = true;
                haveMainModel = true;
            }
        } else {
            std::cerr << "WARNING: Ignoring additional session file argument \"" << path << "\"" << std::endl;
            status = MainWindow::FileOpenSucceeded;
        }
    }
    if (status != MainWindow::FileOpenSucceeded) {
        if (!haveMainModel) {
            status = m_mainWindow->open(path, MainWindow::ReplaceSession);
            if (status == MainWindow::FileOpenSucceeded) {
                haveMainModel = true;
            }
        } else {
            if (haveSession && !havePriorCommandLineModel) {
                status = m_mainWindow->open(path, MainWindow::AskUser);
                if (status == MainWindow::FileOpenSucceeded) {
                    havePriorCommandLineModel = true;
                }
            } else {
                status = m_mainWindow->open(path, MainWindow::CreateAdditionalModel);
            }
        }
    }
    if (status == MainWindow::FileOpenFailed) {
        if (splash) splash->hide();
        QMessageBox::critical
            (m_mainWindow, QMessageBox::tr("Failed to open file"),
             QMessageBox::tr("File or URL \"%1\" could not be opened").arg(path));
    } else if (status == MainWindow::FileOpenWrongMode) {
        if (splash) splash->hide();
        QMessageBox::critical
            (m_mainWindow, QMessageBox::tr("Failed to open file"),
             QMessageBox::tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
    }
}

