#pragma once

#include <QObject>
#include <QTimer>
#include <cstdint>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
class PcListScreen;

// Phase 0 groundwork for Hunter's new Dear ImGui frontend (replacing the
// QML/QtQuick UI, see the project plan). Owns its own SDL2 window +
// SDL_Renderer and pumps a frame on a QTimer tick, so it runs cooperatively
// alongside Qt's normal event loop (ComputerManager polling, NvHTTP
// QNetworkAccessManager callbacks, etc. all keep working). This is the
// opposite of Session::exec()'s streaming loop, which deliberately suspends
// Qt processing for the duration of a stream -- there is no such
// requirement here since nothing else needs the CPU while the user is just
// browsing PCs/apps.
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

private:
    void handleEvent(const SDL_Event& event);
    void renderFrame();
    void shutdown();

    SDL_Window* m_Window;
    SDL_Renderer* m_Renderer;
    uint32_t m_WindowId;
    QTimer m_Timer;
    bool m_Initialized;
    PcListScreen* m_PcListScreen;
};
