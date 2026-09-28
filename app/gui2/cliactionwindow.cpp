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
    m_Window = SDL_CreateWindow("Moonlight",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 480, 200,
                                 SDL_WINDOW_ALLOW_HIGHDPI);
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
    if (m_Initialized) {
        ImGui::OpenPopup("Error");
    }
}

void CliActionWindow::showInfoAndQuitOnClose(const QString& text)
{
    m_InfoText = text;
    m_ShowInfo = true;
    if (m_Initialized) {
        ImGui::OpenPopup("Info");
    }
}

void CliActionWindow::showYesNo(const QString& text, std::function<void()> onYes, std::function<void()> onNo)
{
    m_YesNoText = text;
    m_OnYes = std::move(onYes);
    m_OnNo = std::move(onNo);
    m_ShowYesNo = true;
    if (m_Initialized) {
        ImGui::OpenPopup("Confirm");
    }
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

    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const QByteArray errText = m_ErrorText.toUtf8();
        ImGui::TextWrapped("%s", errText.constData());
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
            QCoreApplication::quit();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const QByteArray infoText = m_InfoText.toUtf8();
        ImGui::TextWrapped("%s", infoText.constData());
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
            QCoreApplication::quit();
        }
        ImGui::EndPopup();
    }

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
