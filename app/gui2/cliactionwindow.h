#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <cstdint>
#include <functional>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

// Phase 4: a minimal ImGui replacement for the three thin QML CLI segue
// views (CliPair.qml, CliQuitStreamSegue.qml, CliStartStreamSegue.qml).
// Those .qml files never held any real logic -- it all already lived in
// the pure-C++ CliPair::Launcher / CliQuitStream::Launcher /
// CliStartStream::Launcher classes (app/cli/*.cpp); the QML was just a
// spinner-and-label wrapper that called Qt.quit() on completion. This
// class is that same wrapper, driven by whichever Launcher main.cpp
// constructs for the requested CLI action.
class CliActionWindow : public QObject
{
    Q_OBJECT

public:
    explicit CliActionWindow(QObject* parent = nullptr);
    ~CliActionWindow() override;

    bool initialize();

    // Updates the centered status line (mirrors stageLabel.text).
    void setStatusText(const QString& text);

    // Shows a dismissible error; OK quits the app (mirrors
    // ErrorMessageDialog's onClosed: Qt.quit()).
    void showErrorAndQuitOnClose(const QString& text);

    // Shows a dismissible message; OK quits the app (mirrors
    // pairCompleteDialog's onClosed: Qt.quit()).
    void showInfoAndQuitOnClose(const QString& text);

    // Shows a Yes/No confirmation (mirrors quitAppDialog).
    void showYesNo(const QString& text, std::function<void()> onYes, std::function<void()> onNo);

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

    QString m_StatusText;

    bool m_ShowError;
    QString m_ErrorText;

    bool m_ShowInfo;
    QString m_InfoText;

    bool m_ShowYesNo;
    QString m_YesNoText;
    std::function<void()> m_OnYes;
    std::function<void()> m_OnNo;
};
