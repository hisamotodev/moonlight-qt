#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <cstdint>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
struct ImGuiContext;
class ComputerManager;
class SystemProperties;
class NvComputer;
class PcListScreen;
class AppListScreen;
class SettingsScreen;
class Session;

// Hunter's new Dear ImGui frontend (replacing the QML/QtQuick UI, see the
// project plan). Owns its own SDL2 window + SDL_Renderer and pumps a frame
// on a QTimer tick, so it runs cooperatively alongside Qt's normal event
// loop (ComputerManager polling, NvHTTP QNetworkAccessManager callbacks,
// etc. all keep working). This is the opposite of Session::exec()'s
// streaming loop, which deliberately suspends Qt processing for the
// duration of a stream -- once a session starts, our own QTimer simply
// stops firing until Session::exec() returns control to Qt's event loop,
// exactly like the QML UI's event processing does today.
//
// Also owns the shared ComputerManager/SystemProperties instances and the
// PcList <-> AppList navigation between them, plus the session launch
// lifecycle (the same six signals StreamSegue.qml wires up), since both
// screens need to hand off into the same "start a session" path.
class ImGuiWindow : public QObject
{
    Q_OBJECT

public:
    explicit ImGuiWindow(QObject* parent = nullptr);
    ~ImGuiWindow() override;

    // Creates the SDL window/renderer and the ImGui context. Returns false
    // (with an SDL_LogError already emitted) on failure.
    bool initialize();

signals:
    // Emitted once the window has been closed by the user (SDL_QUIT for
    // this window, or its close button). The caller owns teardown/exit.
    void closed();

private slots:
    void tick();
    void handleComputerSelected(NvComputer* computer);
    void handleAppListBackRequested();
    void handleLaunchRequested(Session* session, QString appName);

    void handleSessionStageStarting(QString stage);
    void handleSessionStageFailed(QString stage, int errorCode, QString failingPorts);
    void handleSessionConnectionStarted();
    void handleSessionDisplayLaunchError(QString text);
    void handleSessionFinished(int portTestResult);
    void handleSessionReadyForDeletion();

private:
    void handleEvent(const SDL_Event& event);
    void renderFrame();
    void renderSessionStatusOverlay();
    void shutdown();

    SDL_Window* m_Window;
    SDL_Renderer* m_Renderer;
    ImGuiContext* m_Context;
    uint32_t m_WindowId;
    QTimer m_Timer;
    bool m_Initialized;

    ComputerManager* m_ComputerManager;
    SystemProperties* m_SystemProperties;
    PcListScreen* m_PcListScreen;
    AppListScreen* m_AppListScreen;
    SettingsScreen* m_SettingsScreen;
    bool m_ShowSettings;

    // Session launch state. Mirrors StreamSegue.qml's stageText/spinner
    // handling, minus toasts/warnings (not carried over to v1).
    Session* m_ActiveSession;
    QString m_ActiveAppName;
    QString m_SessionStageText;
    bool m_SessionInFlight;
    bool m_ShowSessionErrorDialog;
    QString m_SessionErrorText;
};
