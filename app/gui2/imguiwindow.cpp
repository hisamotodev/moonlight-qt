#include "imguiwindow.h"
#include "pclistscreen.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

ImGuiWindow::ImGuiWindow(QObject* parent)
    : QObject(parent),
      m_Window(nullptr),
      m_Renderer(nullptr),
      m_WindowId(0),
      m_Initialized(false),
      m_PcListScreen(nullptr)
{
    connect(&m_Timer, &QTimer::timeout, this, &ImGuiWindow::tick);
}

ImGuiWindow::~ImGuiWindow()
{
    shutdown();
}

bool ImGuiWindow::initialize()
{
    // SDL_INIT_VIDEO is already brought up unconditionally on Windows by
    // main.cpp before the QQmlApplicationEngine (or, in the future, this
    // window) is created, so we don't call SDL_InitSubSystem() again here.
    m_Window = SDL_CreateWindow("Moonlight",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 1280, 800,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
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
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForSDLRenderer(m_Window, m_Renderer);
    ImGui_ImplSDLRenderer2_Init(m_Renderer);

    m_PcListScreen = new PcListScreen(this);
    m_PcListScreen->start();

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

    if (m_PcListScreen) {
        m_PcListScreen->render();
    }

    ImGui::Render();

    SDL_SetRenderDrawColor(m_Renderer, 30, 30, 30, 255);
    SDL_RenderClear(m_Renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_Renderer);
    SDL_RenderPresent(m_Renderer);
}

void ImGuiWindow::shutdown()
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
