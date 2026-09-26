#pragma once

#include <QObject>
#include <QVariant>

class ComputerManager;
class NvComputer;
class Session;
class StreamingPreferences;

namespace CliStartStream
{

class Event;
class LauncherPrivate;

class Launcher : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE_D(m_DPtr, Launcher)

public:
    // PoC 6 (agent.md section 11): when remoteRunPath is non-empty, app is
    // ignored and the real app name is resolved via Titan's remote-run API
    // once the target computer is found -- see docs/research/poc6-remote-run.md
    // (Destiny superproject) for why this hooks in here rather than in
    // Session/NvHTTP::startApp().
    explicit Launcher(QString computer, QString app,
                      StreamingPreferences* preferences,
                      QObject *parent = nullptr,
                      QString remoteRunPath = QString());
    ~Launcher();
    Q_INVOKABLE void execute(ComputerManager *manager);
    Q_INVOKABLE void quitRunningApp();
    Q_INVOKABLE bool isExecuted() const;

signals:
    void searchingComputer();
    void searchingApp();
    void sessionCreated(QString appName, Session *session);
    void failed(QString text);
    void appQuitRequired(QString appName);

private slots:
    void onComputerFound(NvComputer *computer);
    void onComputerUpdated(NvComputer *computer);
    void onTimeout();
    void onQuitAppCompleted(QVariant error);

private:
    QScopedPointer<LauncherPrivate> m_DPtr;
};

}
