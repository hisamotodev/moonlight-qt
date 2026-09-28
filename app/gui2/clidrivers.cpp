#include "clidrivers.h"
#include "cliactionwindow.h"

#include "backend/computermanager.h"
#include "backend/systemproperties.h"
#include "streaming/session.h"

#include <QCoreApplication>

#include <SDL.h>

// ===================== CliPairDriver =====================

CliPairDriver::CliPairDriver(const QString& host, const QString& predefinedPin, QObject* parent)
    : QObject(parent),
      m_Launcher(new CliPair::Launcher(host, predefinedPin, this)),
      m_Window(new CliActionWindow(this))
{
    connect(m_Launcher, &CliPair::Launcher::searchingComputer, this, &CliPairDriver::handleSearchingComputer);
    connect(m_Launcher, &CliPair::Launcher::pairing, this, &CliPairDriver::handlePairing);
    connect(m_Launcher, &CliPair::Launcher::failed, this, &CliPairDriver::handleFailed);
    connect(m_Launcher, &CliPair::Launcher::success, this, &CliPairDriver::handleSuccess);
}

void CliPairDriver::start(ComputerManager* computerManager)
{
    if (!m_Window->initialize()) {
        QCoreApplication::exit(1);
        return;
    }

    m_Launcher->execute(computerManager);
}

void CliPairDriver::handleSearchingComputer()
{
    m_Window->setStatusText(tr("Establishing connection to PC..."));
}

void CliPairDriver::handlePairing(QString pcName, QString pin)
{
    m_Window->setStatusText(tr("Pairing... Please enter '%1' on %2.").arg(pin, pcName));
}

void CliPairDriver::handleFailed(QString text)
{
    m_Window->showErrorAndQuitOnClose(text);
}

void CliPairDriver::handleSuccess()
{
    m_Window->showInfoAndQuitOnClose(tr("Pairing completed successfully"));
}

// ===================== CliQuitDriver =====================

CliQuitDriver::CliQuitDriver(const QString& host, QObject* parent)
    : QObject(parent),
      m_Launcher(new CliQuitStream::Launcher(host, this)),
      m_Window(new CliActionWindow(this))
{
    connect(m_Launcher, &CliQuitStream::Launcher::searchingComputer, this, &CliQuitDriver::handleSearchingComputer);
    connect(m_Launcher, &CliQuitStream::Launcher::quittingApp, this, &CliQuitDriver::handleQuittingApp);
    connect(m_Launcher, &CliQuitStream::Launcher::failed, this, &CliQuitDriver::handleFailed);
}

void CliQuitDriver::start(ComputerManager* computerManager)
{
    if (!m_Window->initialize()) {
        QCoreApplication::exit(1);
        return;
    }

    m_Launcher->execute(computerManager);
}

void CliQuitDriver::handleSearchingComputer()
{
    m_Window->setStatusText(tr("Establishing connection to PC..."));
}

void CliQuitDriver::handleQuittingApp()
{
    m_Window->setStatusText(tr("Quitting app..."));
}

void CliQuitDriver::handleFailed(QString text)
{
    m_Window->showErrorAndQuitOnClose(text);
}

// ===================== CliStreamDriver =====================

CliStreamDriver::CliStreamDriver(const QString& host, const QString& appName, StreamingPreferences* preferences,
                                 const QString& remoteRunPath, QObject* parent)
    : QObject(parent),
      m_Launcher(new CliStartStream::Launcher(host, appName, preferences, this, remoteRunPath)),
      m_Window(new CliActionWindow(this)),
      m_ActiveSession(nullptr)
{
    connect(m_Launcher, &CliStartStream::Launcher::searchingComputer, this, &CliStreamDriver::handleSearchingComputer);
    connect(m_Launcher, &CliStartStream::Launcher::searchingApp, this, &CliStreamDriver::handleSearchingApp);
    connect(m_Launcher, &CliStartStream::Launcher::sessionCreated, this, &CliStreamDriver::handleSessionCreated);
    connect(m_Launcher, &CliStartStream::Launcher::failed, this, &CliStreamDriver::handleLaunchFailed);
    connect(m_Launcher, &CliStartStream::Launcher::appQuitRequired, this, &CliStreamDriver::handleAppQuitRequired);
}

void CliStreamDriver::start(ComputerManager* computerManager)
{
    if (!m_Window->initialize()) {
        QCoreApplication::exit(1);
        return;
    }

    m_Launcher->execute(computerManager);
}

void CliStreamDriver::handleSearchingComputer()
{
    m_Window->setStatusText(tr("Establishing connection to PC..."));
}

void CliStreamDriver::handleSearchingApp()
{
    m_Window->setStatusText(tr("Loading app list..."));
}

void CliStreamDriver::handleLaunchFailed(QString message)
{
    m_Window->showErrorAndQuitOnClose(message);
}

void CliStreamDriver::handleAppQuitRequired(QString appName)
{
    m_Window->showYesNo(
        tr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName),
        [this]() { m_Launcher->quitRunningApp(); },
        [this]() { QCoreApplication::quit(); });
}

void CliStreamDriver::handleSessionCreated(QString appName, Session* session)
{
    Q_UNUSED(appName);
    startSession(session);
}

void CliStreamDriver::startSession(Session* session)
{
    m_ActiveSession = session;
    m_SessionErrorText.clear();

    connect(session, &Session::stageStarting, this, &CliStreamDriver::handleSessionStageStarting);
    connect(session, &Session::stageFailed, this, &CliStreamDriver::handleSessionStageFailed);
    connect(session, &Session::connectionStarted, this, &CliStreamDriver::handleSessionConnectionStarted);
    connect(session, &Session::displayLaunchError, this, &CliStreamDriver::handleSessionDisplayLaunchError);
    connect(session, &Session::sessionFinished, this, &CliStreamDriver::handleSessionFinished);
    connect(session, &Session::readyForDeletion, this, &CliStreamDriver::handleSessionReadyForDeletion);

    // Mirrors StreamSegue.qml: SystemProperties.waitForAsyncLoad() must
    // pair with a startAsyncLoad() before Session::initialize() runs.
    // The interactive (ImGuiWindow) path starts its own load at startup;
    // this CLI path has no such window yet, so do it here instead.
    // SystemProperties has no QObject parent constructor overload; this
    // CLI process exits shortly after the stream ends either way, so we
    // don't bother explicitly freeing it.
    SystemProperties* sysProps = new SystemProperties();
    sysProps->startAsyncLoad();
    sysProps->waitForAsyncLoad();

    if (!session->initialize(nullptr)) {
        handleSessionFinished(0);
        handleSessionReadyForDeletion();
        return;
    }

    session->start();
}

void CliStreamDriver::handleSessionStageStarting(QString stage)
{
    m_Window->setStatusText(tr("Starting %1...").arg(stage));
}

void CliStreamDriver::handleSessionStageFailed(QString stage, int errorCode, QString failingPorts)
{
    m_SessionErrorText = tr("Starting %1 failed: Error %2").arg(stage).arg(errorCode);
    if (!failingPorts.isEmpty()) {
        m_SessionErrorText += tr("\n\nCheck your firewall and port forwarding rules for port(s): %1")
                .arg(failingPorts);
    }
}

void CliStreamDriver::handleSessionConnectionStarted()
{
    // Nothing to hide -- CliActionWindow is just a status window, unlike
    // ImGuiWindow's PC/app browsing window, so there's no separate window
    // whose visibility needs to be toggled around the stream.
}

void CliStreamDriver::handleSessionDisplayLaunchError(QString text)
{
    m_SessionErrorText = text;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.toUtf8().constData());
}

void CliStreamDriver::handleSessionFinished(int portTestResult)
{
    if (portTestResult != 0 && portTestResult != -1 && !m_SessionErrorText.isEmpty()) {
        m_SessionErrorText += tr("\n\nThis PC's Internet connection is blocking Moonlight. "
                                  "Streaming over the Internet may not work while connected to this network.");
    }

    if (!m_SessionErrorText.isEmpty()) {
        m_Window->showErrorAndQuitOnClose(m_SessionErrorText);
    } else {
        // CLI streams always quit after the session ends (quitAfter=true
        // in StreamSegue.qml terms) -- there's no browsing UI to return to.
        QCoreApplication::quit();
    }
}

void CliStreamDriver::handleSessionReadyForDeletion()
{
    if (m_ActiveSession) {
        m_ActiveSession->deleteLater();
        m_ActiveSession = nullptr;
    }
}
