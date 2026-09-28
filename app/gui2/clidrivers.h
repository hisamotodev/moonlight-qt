#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

#include "cli/pair.h"
#include "cli/quitstream.h"
#include "cli/startstream.h"

class ComputerManager;
class NvComputer;
class Session;
class StreamingPreferences;
class CliActionWindow;

// Phase 4: drivers that replace CliPair.qml / CliQuitStreamSegue.qml /
// CliStartStreamSegue.qml. Each owns the existing pure-C++ Launcher (the
// QML files never had real logic of their own -- see the note in
// cliactionwindow.h) plus a CliActionWindow for status/error/confirm UI,
// and quits the whole app when the action completes, same as the QML
// versions' Qt.quit() calls.

class CliPairDriver : public QObject
{
    Q_OBJECT

public:
    CliPairDriver(const QString& host, const QString& predefinedPin, QObject* parent = nullptr);

    void start(ComputerManager* computerManager);

private slots:
    void handleSearchingComputer();
    void handlePairing(QString pcName, QString pin);
    void handleFailed(QString text);
    void handleSuccess();

private:
    CliPair::Launcher* m_Launcher;
    CliActionWindow* m_Window;
};

class CliQuitDriver : public QObject
{
    Q_OBJECT

public:
    explicit CliQuitDriver(const QString& host, QObject* parent = nullptr);

    void start(ComputerManager* computerManager);

private slots:
    void handleSearchingComputer();
    void handleQuittingApp();
    void handleFailed(QString text);

private:
    CliQuitStream::Launcher* m_Launcher;
    CliActionWindow* m_Window;
};

class CliStreamDriver : public QObject
{
    Q_OBJECT

public:
    CliStreamDriver(const QString& host, const QString& appName, StreamingPreferences* preferences,
                    const QString& remoteRunPath, QObject* parent = nullptr);

    void start(ComputerManager* computerManager);

private slots:
    void handleSearchingComputer();
    void handleSearchingApp();
    void handleSessionCreated(QString appName, Session* session);
    void handleLaunchFailed(QString message);
    void handleAppQuitRequired(QString appName);

    void handleSessionStageStarting(QString stage);
    void handleSessionStageFailed(QString stage, int errorCode, QString failingPorts);
    void handleSessionConnectionStarted();
    void handleSessionDisplayLaunchError(QString text);
    void handleSessionFinished(int portTestResult);
    void handleSessionReadyForDeletion();

private:
    void startSession(Session* session);

    CliStartStream::Launcher* m_Launcher;
    CliActionWindow* m_Window;
    Session* m_ActiveSession;
    QString m_SessionErrorText;
};
