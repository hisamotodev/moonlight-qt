#include "cliactionwindow.h"

#include <QCoreApplication>

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

CliActionWindow::CliActionWindow(QObject* parent)
    : QObject(parent),
      m_Window(nullptr),
      m_Renderer(nullptr),
      m_WindowId(0),
      m_Initialized(false),
      m_ShowError(false),
      m_ShowInfo(false),
      m_ShowYesNo(false)
{
    connect(&m_Timer, &QTimer::timeout, this, &CliActionWindow::tick);
}

CliActionWindow::~CliActionWindow()
{
    shutdown();
}

bool CliActionWindow::initialize()
{
    // No SDL_WINDOW_ALLOW_HIGHDPI -- same reasoning as ImGuiWindow::initialize()
    // (imguiwindow.cpp): it can desync SDL's mouse-coordinate math from
    // whatever DPI-awareness mode Qt's QGuiApplication already locked in
    // for the process, silently breaking every ImGui widget's click
    // hit-test on a scaled display (found in live testing).
    m_Window = SDL_CreateWindow("Moonlight",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 480, 200,
                                 0);
    if (!m_Window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CliActionWindow: SDL_CreateWindow() failed: %s", SDL_GetError());
        return false;
    }

    m_Renderer = SDL_CreateRenderer(m_Window, -1, SDL_RENDERER_ACCELERATED);
    if (!m_Renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CliActionWindow: SDL_CreateRenderer() failed: %s", SDL_GetError());
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
        return false;
    }

    m_WindowId = SDL_GetWindowID(m_Window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForSDLRenderer(m_Window, m_Renderer);
    ImGui_ImplSDLRenderer2_Init(m_Renderer);

    m_Initialized = true;
    m_Timer.start(16);

    return true;
}

void CliActionWindow::setStatusText(const QString& text)
{
    m_StatusText = text;
}

void CliActionWindow::showErrorAndQuitOnClose(const QString& text)
{
    m_ErrorText = text;
    m_ShowError = true;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.toUtf8().constData());
    // ImGui::OpenPopup() is deliberately not called here: this can be
    // invoked by a driver outside CliActionWindow::tick()'s NewFrame()/
    // Render() bracket, where ImGui's current-window pointer is null and
    // OpenPopup() would crash. renderFrame() below opens it instead, from
    // the same ID-stack level as its BeginPopupModal().
}

void CliActionWindow::showInfoAndQuitOnClose(const QString& text)
{
    m_InfoText = text;
    m_ShowInfo = true;
}

void CliActionWindow::showYesNo(const QString& text, std::function<void()> onYes, std::function<void()> onNo)
{
    m_YesNoText = text;
    m_OnYes = std::move(onYes);
    m_OnNo = std::move(onNo);
    m_ShowYesNo = true;
}

void CliActionWindow::handleEvent(const SDL_Event& event)
{
    ImGui_ImplSDL2_ProcessEvent(&event);

    const bool isOurWindowClose = event.type == SDL_WINDOWEVENT &&
            event.window.windowID == m_WindowId &&
            event.window.event == SDL_WINDOWEVENT_CLOSE;

    if (event.type == SDL_QUIT || isOurWindowClose) {
        // Mirrors CliPair.qml's Keys.onEscapePressed/onBackPressed/
        // onCancelPressed: closing this window cancels the CLI action.
        QCoreApplication::quit();
    }
}

void CliActionWindow::tick()
{
    if (!m_Initialized) {
        return;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        handleEvent(event);
    }

    renderFrame();
}

void CliActionWindow::renderFrame()
{
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("##clistatus", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    const QByteArray text = m_StatusText.toUtf8();
    ImGui::Text("%s", text.constData());
    ImGui::End();

    if (m_ShowError) {
        ImGui::OpenPopup("Error");
    }
    // See PcListScreen::renderPairDialog() (pclistscreen.cpp) for why this
    // needs a width hint: without it, TextWrapped() below computes its wrap
    // width from the popup's too-narrow pre-layout size, producing a tall,
    // single-word-per-line dialog.
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const QByteArray errText = m_ErrorText.toUtf8();
        ImGui::TextWrapped("%s", errText.constData());
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
            QCoreApplication::quit();
        }
        ImGui::EndPopup();
    }

    if (m_ShowInfo) {
        ImGui::OpenPopup("Info");
    }
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const QByteArray infoText = m_InfoText.toUtf8();
        ImGui::TextWrapped("%s", infoText.constData());
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
            QCoreApplication::quit();
        }
        ImGui::EndPopup();
    }

    if (m_ShowYesNo) {
        ImGui::OpenPopup("Confirm");
    }
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const QByteArray confirmText = m_YesNoText.toUtf8();
        ImGui::TextWrapped("%s", confirmText.constData());
        if (ImGui::Button("Yes")) {
            ImGui::CloseCurrentPopup();
            m_ShowYesNo = false;
            if (m_OnYes) {
                m_OnYes();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("No")) {
            ImGui::CloseCurrentPopup();
            m_ShowYesNo = false;
            if (m_OnNo) {
                m_OnNo();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Render();

    SDL_SetRenderDrawColor(m_Renderer, 30, 30, 30, 255);
    SDL_RenderClear(m_Renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_Renderer);
    SDL_RenderPresent(m_Renderer);
}

void CliActionWindow::shutdown()
{
    if (!m_Initialized) {
        return;
    }

    m_Timer.stop();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(m_Renderer);
    SDL_DestroyWindow(m_Window);
    m_Renderer = nullptr;
    m_Window = nullptr;

    m_Initialized = false;
}
