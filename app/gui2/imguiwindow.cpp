#include "imguiwindow.h"
#include "pclistscreen.h"
#include "applistscreen.h"
#include "settingsscreen.h"

#include "backend/computermanager.h"
#include "backend/systemproperties.h"
#include "settings/streamingpreferences.h"
#include "streaming/session.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

ImGuiWindow::ImGuiWindow(QObject* parent)
    : QObject(parent),
      m_Window(nullptr),
      m_Renderer(nullptr),
      m_Context(nullptr),
      m_WindowId(0),
      m_Initialized(false),
      m_ComputerManager(nullptr),
      m_SystemProperties(nullptr),
      m_PcListScreen(nullptr),
      m_AppListScreen(nullptr),
      m_SettingsScreen(nullptr),
      m_ShowSettings(false),
      m_ActiveSession(nullptr),
      m_SessionInFlight(false),
      m_ShowSessionErrorDialog(false)
{
    connect(&m_Timer, &QTimer::timeout, this, &ImGuiWindow::tick);
}

ImGuiWindow::~ImGuiWindow()
{
    shutdown();

    if (m_ComputerManager) {
        m_ComputerManager->stopPollingAsync();
    }
}

bool ImGuiWindow::initialize()
{
    // SDL_INIT_VIDEO is already brought up unconditionally on Windows by
    // main.cpp before this window is created, so we don't call
    // SDL_InitSubSystem() again here.
    // Deliberately NOT SDL_WINDOW_ALLOW_HIGHDPI: Qt's QGuiApplication
    // (main.cpp, constructed before this window) already sets the
    // process's Windows DPI-awareness mode, which SDL cannot change
    // afterward. Found in live testing on a second, differently-DPI-scaled
    // PC: with ALLOW_HIGHDPI set, SDL2's own per-monitor-DPI mouse
    // coordinate math ends up out of sync with whatever awareness mode
    // Qt already locked in -- OS-level window operations (drag/resize,
    // handled by the window manager's own hit-testing) still worked fine,
    // but every ImGui widget's click hit-test silently failed. Omitting it
    // makes Windows apply its own DPI virtualization (bitmap stretching)
    // to this window instead, which keeps mouse coordinates and rendered
    // widget positions in the same coordinate space -- at the cost of a
    // slightly soft/blurry look on a scaled display, which is far less
    // bad than the UI being unusable. See the style-scaling call below for
    // the (separate) "make widgets a sane physical size" concern.
    m_Window = SDL_CreateWindow("Moonlight",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 1280, 800,
                                 SDL_WINDOW_RESIZABLE);
    if (!m_Window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ImGuiWindow: SDL_CreateWindow() failed: %s", SDL_GetError());
        return false;
    }

    m_Renderer = SDL_CreateRenderer(m_Window, -1, SDL_RENDERER_ACCELERATED);
    if (!m_Renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ImGuiWindow: SDL_CreateRenderer() failed: %s", SDL_GetError());
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
        return false;
    }

    m_WindowId = SDL_GetWindowID(m_Window);

    IMGUI_CHECKVERSION();
    m_Context = ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForSDLRenderer(m_Window, m_Renderer);
    ImGui_ImplSDLRenderer2_Init(m_Renderer);

    // Shared with both PcListScreen and AppListScreen -- NvComputer
    // pointers are only valid against the ComputerManager that owns them,
    // so there must be exactly one instance for the lifetime of the window
    // (mirrors the single ComputerManager singleton QML registers).
    m_ComputerManager = new ComputerManager(StreamingPreferences::get());

    // Mirrors main.qml's startup: kick off the async hardware/gamepad
    // capability probe once, up front. Session::initialize() requires
    // waitForAsyncLoad() to have been paired with this before it runs.
    m_SystemProperties = new SystemProperties();
    m_SystemProperties->startAsyncLoad();

    // SystemProperties' constructor creates a hidden test window for the
    // decoder probe (StreamUtils::createTestWindow()), which unconditionally
    // calls SDL_StopTextInput() to avoid triggering an IME popup on that
    // window. SDL's text-input-enabled state is process-global, not
    // per-window, so that leaves it off for *our* window too -- and unlike
    // QML (whose TextFields never went through SDL text input at all), every
    // ImGui::InputText() in the new UI depends on SDL_TEXTINPUT events, so
    // this silently broke all keyboard input into them. Re-enable it now
    // that the probe's test window is gone.
    SDL_StartTextInput();

    m_PcListScreen = new PcListScreen(m_ComputerManager, this);
    connect(m_PcListScreen, &PcListScreen::computerSelected,
            this, &ImGuiWindow::handleComputerSelected);

    m_SettingsScreen = new SettingsScreen(this);

    m_ComputerManager->startPolling();

    m_Initialized = true;

    // Not vsync'd to the display -- this window is driven cooperatively by
    // Qt's event loop via a plain timer tick, not a blocking render loop,
    // so ~60Hz is just a fixed cadence rather than a real frame pacing
    // mechanism. This is deliberately different from Session::exec()'s
    // streaming loop, which owns the thread outright.
    m_Timer.start(16);

    return true;
}

void ImGuiWindow::handleEvent(const SDL_Event& event)
{
    ImGui_ImplSDL2_ProcessEvent(&event);

    switch (event.type) {
    case SDL_QUIT:
        emit closed();
        break;
    case SDL_WINDOWEVENT:
        if (event.window.windowID == m_WindowId &&
                event.window.event == SDL_WINDOWEVENT_CLOSE) {
            emit closed();
        }
        break;
    default:
        break;
    }
}

void ImGuiWindow::tick()
{
    if (!m_Initialized) {
        return;
    }

    // Defensive: a just-finished stream's StreamOverlay uses its own
    // separate ImGui context (see streamoverlay.h) but the global "current
    // context" pointer is shared process-wide, so make sure it's pointed
    // back at ours before we touch ImGui again.
    ImGui::SetCurrentContext(m_Context);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        handleEvent(event);
    }

    renderFrame();
}

void ImGuiWindow::renderFrame()
{
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (m_SessionInFlight) {
        renderSessionStatusOverlay();
    } else {
        // A real main menu bar (rather than the old floating auto-resize
        // "##menubar" window) reserves its height from the main viewport's
        // work area automatically -- GetMainViewport()->WorkPos/WorkSize
        // below already exclude it, so the PC list / app list panes never
        // have to know about it explicitly.
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::MenuItem(m_ShowSettings ? "Hide Settings" : "Settings")) {
                m_ShowSettings = !m_ShowSettings;
            }
            ImGui::EndMainMenuBar();
        }

        // Side-by-side layout: once a PC is selected, its app list docks
        // into the remaining space to the right of a narrower PC list
        // column, instead of replacing it outright -- switching PCs (or
        // just glancing at what else is available) no longer requires
        // leaving the app list first.
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        constexpr float pcListWidth = 320.0f;

        if (m_PcListScreen) {
            const ImVec2 size = m_AppListScreen
                    ? ImVec2(pcListWidth, viewport->WorkSize.y)
                    : viewport->WorkSize;
            ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(size, ImGuiCond_Always);
            m_PcListScreen->render();
        }

        if (m_AppListScreen) {
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + pcListWidth, viewport->WorkPos.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - pcListWidth, viewport->WorkSize.y), ImGuiCond_Always);
            m_AppListScreen->render();
        }

        if (m_SettingsScreen) {
            m_SettingsScreen->render(&m_ShowSettings);
        }

        // Shown once a session that failed (or logged a launch error)
        // returns control to us -- same text StreamSegue.qml's
        // streamSegueErrorDialog would have shown.
        // OpenPopup() and BeginPopupModal() must run at the same ID-stack
        // level and inside ImGui's NewFrame()/Render() bracket (see
        // PcListScreen::renderAddPcPopup() for the full explanation), so
        // this can't live in handleSessionFinished() below -- that slot can
        // run synchronously off the end of Session::exec() without going
        // through tick()'s ImGui::SetCurrentContext(m_Context)/NewFrame(),
        // where ImGui::OpenPopup() would touch a stale or null context.
        if (m_ShowSessionErrorDialog) {
            ImGui::OpenPopup("Session Error");
        }

        // AlwaysAutoResize + TextWrapped with no width hint computes the
        // wrap width from the popup's pre-layout size on its first frame,
        // which is too narrow -- producing a tall, single-word-per-line
        // dialog. Pin a sane width; height still auto-fits the content.
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);

        if (ImGui::BeginPopupModal("Session Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!m_ShowSessionErrorDialog) {
                ImGui::CloseCurrentPopup();
            } else {
                const QByteArray text = m_SessionErrorText.toUtf8();
                ImGui::TextWrapped("%s", text.constData());
                if (ImGui::Button("OK")) {
                    m_ShowSessionErrorDialog = false;
                    m_SessionErrorText.clear();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    ImGui::Render();

    SDL_SetRenderDrawColor(m_Renderer, 30, 30, 30, 255);
    SDL_RenderClear(m_Renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_Renderer);
    SDL_RenderPresent(m_Renderer);
}

void ImGuiWindow::renderSessionStatusOverlay()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("##sessionstatus", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    const QByteArray text = m_SessionStageText.toUtf8();
    ImGui::Text("%s", text.constData());
    ImGui::End();
}

void ImGuiWindow::handleComputerSelected(NvComputer* computer)
{
    // The PC list stays visible now (side-by-side layout), so re-clicking
    // the PC already shown on the right is a normal, frequent interaction
    // rather than a one-shot navigation -- don't tear down and recreate the
    // panel (losing scroll position or an open Remote Run/quit dialog) for
    // a no-op selection.
    if (m_AppListScreen && m_AppListScreen->computer() == computer) {
        return;
    }

    delete m_AppListScreen;
    m_AppListScreen = new AppListScreen(m_ComputerManager, computer, this);
    connect(m_AppListScreen, &AppListScreen::backRequested,
            this, &ImGuiWindow::handleAppListBackRequested);
    connect(m_AppListScreen, &AppListScreen::launchRequested,
            this, &ImGuiWindow::handleLaunchRequested);
}

void ImGuiWindow::handleAppListBackRequested()
{
    delete m_AppListScreen;
    m_AppListScreen = nullptr;

    if (m_PcListScreen) {
        m_PcListScreen->clearSelection();
    }
}

void ImGuiWindow::handleLaunchRequested(Session* session, QString appName)
{
    if (m_SessionInFlight) {
        // A launch is already in progress; drop this one rather than
        // starting a second concurrent Session.
        delete session;
        return;
    }

    m_ActiveSession = session;
    m_ActiveAppName = appName;
    m_SessionStageText = QStringLiteral("Starting %1...").arg(appName);
    m_SessionInFlight = true;
    m_SessionErrorText.clear();

    connect(session, &Session::stageStarting, this, &ImGuiWindow::handleSessionStageStarting);
    connect(session, &Session::stageFailed, this, &ImGuiWindow::handleSessionStageFailed);
    connect(session, &Session::connectionStarted, this, &ImGuiWindow::handleSessionConnectionStarted);
    connect(session, &Session::displayLaunchError, this, &ImGuiWindow::handleSessionDisplayLaunchError);
    connect(session, &Session::sessionFinished, this, &ImGuiWindow::handleSessionFinished);
    connect(session, &Session::readyForDeletion, this, &ImGuiWindow::handleSessionReadyForDeletion);

    // Required before Session::initialize() -- see SystemProperties'
    // class comment; it may still be using the SDL video subsystem.
    m_SystemProperties->waitForAsyncLoad();

    // We have no QQuickWindow to hand Session::initialize(); every use of
    // it inside Session is guarded by a null check (m_QtWindow != nullptr)
    // after a debug-only Q_ASSERT, so passing nullptr just means Session
    // skips minimize/restore-syncing with a Qt browsing window, which we
    // don't have.
    if (!session->initialize(nullptr)) {
        handleSessionFinished(0);
        handleSessionReadyForDeletion();
        return;
    }

    session->start();
}

void ImGuiWindow::handleSessionStageStarting(QString stage)
{
    m_SessionStageText = QStringLiteral("Starting %1...").arg(stage);
}

void ImGuiWindow::handleSessionStageFailed(QString stage, int errorCode, QString failingPorts)
{
    m_SessionErrorText = QStringLiteral("Starting %1 failed: Error %2").arg(stage).arg(errorCode);
    if (!failingPorts.isEmpty()) {
        m_SessionErrorText += QStringLiteral("\n\nCheck your firewall and port forwarding rules for port(s): %1")
                .arg(failingPorts);
    }
}

void ImGuiWindow::handleSessionConnectionStarted()
{
    // Hide the browsing window now that streaming has begun, same as
    // StreamSegue.qml's connectionStarted() hiding the Qt window.
    SDL_HideWindow(m_Window);
}

void ImGuiWindow::handleSessionDisplayLaunchError(QString text)
{
    m_SessionErrorText = text;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.toUtf8().constData());
}

void ImGuiWindow::handleSessionFinished(int portTestResult)
{
    if (portTestResult != 0 && portTestResult != -1 && !m_SessionErrorText.isEmpty()) {
        m_SessionErrorText += QStringLiteral(
            "\n\nThis PC's Internet connection is blocking Moonlight. "
            "Streaming over the Internet may not work while connected to this network.");
    }

    SDL_ShowWindow(m_Window);

    // Session::start() also unconditionally calls SDL_StopTextInput() before
    // it creates its own decoder-probe test window (same rationale as the
    // one in initialize() above), which leaves our InputText widgets unable
    // to receive keystrokes for the rest of the process once a stream ends.
    // Undo it now that we're back in the browsing window.
    SDL_StartTextInput();

    m_SessionInFlight = false;

    if (!m_SessionErrorText.isEmpty()) {
        m_ShowSessionErrorDialog = true;
    }
}

void ImGuiWindow::handleSessionReadyForDeletion()
{
    // Session is heavyweight (keeps SDL_ttf etc. alive until destroyed),
    // same rationale as StreamSegue.qml's sessionReadyForDeletion() gc().
    if (m_ActiveSession) {
        m_ActiveSession->deleteLater();
        m_ActiveSession = nullptr;
    }
}

void ImGuiWindow::shutdown()
{
    if (!m_Initialized) {
        return;
    }

    m_Timer.stop();

    ImGui::SetCurrentContext(m_Context);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(m_Context);
    m_Context = nullptr;

    SDL_DestroyRenderer(m_Renderer);
    SDL_DestroyWindow(m_Window);
    m_Renderer = nullptr;
    m_Window = nullptr;

    m_Initialized = false;
}
