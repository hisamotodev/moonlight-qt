#include "streamoverlay.h"

#include "streaming/video/overlaymanager.h"
#include "streaming/input/input.h"
#include "settings/streamingpreferences.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <algorithm>

using namespace Overlay;

namespace {
constexpr float kButtonWidth = 56.0f;
constexpr float kButtonHeight = 32.0f;
constexpr uint32_t kRenderIntervalMs = 33; // ~30 Hz
}

StreamOverlay::StreamOverlay(SDL_Window* window, OverlayManager* overlayManager,
                             StreamingPreferences* prefs, SdlInputHandler* inputHandler)
    : m_Window(window),
      m_OverlayManager(overlayManager),
      m_Prefs(prefs),
      m_InputHandler(inputHandler),
      m_Context(nullptr),
      m_Surface(nullptr),
      m_SoftRenderer(nullptr),
      m_SurfaceWidth(0),
      m_SurfaceHeight(0),
      m_LastRenderTicks(0),
      m_NeedsRender(true),
      m_MenuOpen(false),
      m_ActiveSubmenu(Submenu::None),
      m_Dragging(false),
      m_DragOffsetX(0),
      m_DragOffsetY(0),
      m_DebugOverlayEnabledByUs(false)
{
    m_Context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_Context);
    ImGui::StyleColorsDark();

    // A small, semi-transparent look befitting an overlay rather than a
    // full opaque window (the rest of the surface stays fully transparent
    // -- see renderFrame()'s clear color).
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg].w = 0.85f;
    style.WindowRounding = 4.0f;
}

StreamOverlay::~StreamOverlay()
{
    ImGui::SetCurrentContext(m_Context);

    if (m_Surface) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
    }

    ImGui::DestroyContext(m_Context);
    m_Context = nullptr;

    if (m_SoftRenderer) {
        SDL_DestroyRenderer(m_SoftRenderer);
        m_SoftRenderer = nullptr;
    }
    if (m_Surface) {
        SDL_FreeSurface(m_Surface);
        m_Surface = nullptr;
    }

    // Clear the overlay slot so nothing stale lingers for whatever comes next.
    m_OverlayManager->updateOverlaySurface(Overlay::OverlayMenu, nullptr);
}

void StreamOverlay::ensureSurface(int width, int height)
{
    if (m_Surface != nullptr && m_SurfaceWidth == width && m_SurfaceHeight == height) {
        return;
    }

    ImGui::SetCurrentContext(m_Context);

    bool firstInit = (m_Surface == nullptr);
    if (!firstInit) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        SDL_DestroyRenderer(m_SoftRenderer);
        m_SoftRenderer = nullptr;
        SDL_FreeSurface(m_Surface);
        m_Surface = nullptr;
    }

    m_Surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    m_SoftRenderer = SDL_CreateSoftwareRenderer(m_Surface);
    m_SurfaceWidth = width;
    m_SurfaceHeight = height;

    ImGui_ImplSDL2_InitForSDLRenderer(m_Window, m_SoftRenderer);
    ImGui_ImplSDLRenderer2_Init(m_SoftRenderer);

    m_NeedsRender = true;
}

bool StreamOverlay::processEvent(const SDL_Event& event)
{
    if (m_Surface == nullptr) {
        // Nothing rendered yet (e.g. before the first maybeRender()) --
        // there's no widget to have captured this event.
        return false;
    }

    ImGui::SetCurrentContext(m_Context);
    ImGui_ImplSDL2_ProcessEvent(&event);
    m_NeedsRender = true;

    // io.WantCaptureMouse/Keyboard reflect the *last completed frame*'s
    // hit-testing (widgets aren't declared until the next renderFrame()),
    // which is the standard one-frame-latency approach real-time ImGui
    // integrations use when events arrive outside of a NewFrame/Render
    // bracket. A single frame (~33ms at our throttle) of latency on the
    // capture decision is imperceptible here.
    ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
        return io.WantCaptureMouse;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        // Only steal keyboard input while the menu is actually open --
        // the collapsed button alone has no use for it.
        return m_MenuOpen && io.WantCaptureKeyboard;
    default:
        return false;
    }
}

void StreamOverlay::maybeRender()
{
    int w, h;
    SDL_GetWindowSize(m_Window, &w, &h);
    if (w <= 0 || h <= 0) {
        return;
    }
    ensureSurface(w, h);

    uint32_t now = SDL_GetTicks();
    if (!m_NeedsRender && (now - m_LastRenderTicks) < kRenderIntervalMs) {
        return;
    }

    renderFrame();
    m_LastRenderTicks = now;
    m_NeedsRender = false;
}

void StreamOverlay::renderFrame()
{
    ImGui::SetCurrentContext(m_Context);

    if (m_Prefs->overlayButtonHidden && !m_MenuOpen) {
        m_OverlayManager->updateOverlaySurface(Overlay::OverlayMenu, nullptr);
        return;
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    renderButtonAndMenu();

    if (m_MenuOpen) {
        switch (m_ActiveSubmenu) {
        case Submenu::Immersive:
            renderImmersiveSubmenu();
            break;
        case Submenu::Settings:
            renderSettingsSubmenu();
            break;
        case Submenu::Stats:
            renderStatsSubmenu();
            break;
        case Submenu::None:
            break;
        }
    }

    ImGui::Render();

    SDL_SetRenderDrawColor(m_SoftRenderer, 0, 0, 0, 0);
    SDL_RenderClear(m_SoftRenderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_SoftRenderer);
    SDL_RenderPresent(m_SoftRenderer);

    // OverlayManager::updateOverlaySurface() takes ownership of (and later
    // frees) whatever we hand it, but we need to keep drawing on m_Surface
    // next frame, so hand over a copy rather than m_Surface itself.
    SDL_Surface* copy = SDL_ConvertSurface(m_Surface, m_Surface->format, 0);
    m_OverlayManager->updateOverlaySurface(Overlay::OverlayMenu, copy);
}

void StreamOverlay::renderButtonAndMenu()
{
    ImGuiIO& io = ImGui::GetIO();

    float x = (float)(m_Prefs->overlayButtonX * m_SurfaceWidth);
    float y = (float)(m_Prefs->overlayButtonY * m_SurfaceHeight);

    ImGui::SetNextWindowPos(ImVec2(x, y), m_Dragging ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::Begin("##overlaybutton", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar |
                  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                  ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

    bool clicked = ImGui::Button("Menu", ImVec2(kButtonWidth, kButtonHeight));

    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
        m_Dragging = true;

        ImVec2 winPos = ImGui::GetWindowPos();
        winPos.x += io.MouseDelta.x;
        winPos.y += io.MouseDelta.y;
        winPos.x = std::clamp(winPos.x, 0.0f, (float)m_SurfaceWidth - kButtonWidth);
        winPos.y = std::clamp(winPos.y, 0.0f, (float)m_SurfaceHeight - kButtonHeight);
        ImGui::SetWindowPos(winPos);

        m_Prefs->overlayButtonX = winPos.x / m_SurfaceWidth;
        m_Prefs->overlayButtonY = winPos.y / m_SurfaceHeight;
    }
    else if (m_Dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_Dragging = false;
        m_Prefs->save();
    }
    else if (clicked && !m_Dragging) {
        m_MenuOpen = !m_MenuOpen;
        if (!m_MenuOpen) {
            closeMenu();
        } else {
            m_ActiveSubmenu = Submenu::None;
        }
    }

    ImGui::End();

    if (m_MenuOpen) {
        ImGui::SetNextWindowPos(ImVec2(x, y + kButtonHeight + 4), ImGuiCond_Always);
        ImGui::Begin("##overlaymenu", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

        if (ImGui::MenuItem("Hide Button")) {
            m_Prefs->overlayButtonHidden = true;
            m_Prefs->save();
            closeMenu();
        }
        if (ImGui::MenuItem("Immersive Mode", nullptr, m_ActiveSubmenu == Submenu::Immersive)) {
            m_ActiveSubmenu = (m_ActiveSubmenu == Submenu::Immersive) ? Submenu::None : Submenu::Immersive;
        }
        if (ImGui::MenuItem("Settings", nullptr, m_ActiveSubmenu == Submenu::Settings)) {
            m_ActiveSubmenu = (m_ActiveSubmenu == Submenu::Settings) ? Submenu::None : Submenu::Settings;
        }
        if (ImGui::MenuItem("Stats", nullptr, m_ActiveSubmenu == Submenu::Stats)) {
            bool opening = (m_ActiveSubmenu != Submenu::Stats);
            m_ActiveSubmenu = opening ? Submenu::Stats : Submenu::None;

            if (opening && !m_OverlayManager->isOverlayEnabled(Overlay::OverlayDebug)) {
                m_OverlayManager->setOverlayState(Overlay::OverlayDebug, true);
                m_DebugOverlayEnabledByUs = true;
            }
            else if (!opening && m_DebugOverlayEnabledByUs) {
                m_OverlayManager->setOverlayState(Overlay::OverlayDebug, false);
                m_DebugOverlayEnabledByUs = false;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Disconnect")) {
            SDL_Event quitEvent;
            quitEvent.type = SDL_QUIT;
            quitEvent.quit.timestamp = SDL_GetTicks();
            SDL_PushEvent(&quitEvent);
            closeMenu();
        }

        ImGui::End();
    }
}

void StreamOverlay::renderImmersiveSubmenu()
{
    ImGui::Begin("##immersivesubmenu", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

    // Session-local quick toggle only -- does NOT touch the persistent
    // StreamingPreferences::absoluteMouseMode/captureSysKeysMode settings,
    // per this session's design decision. Resets to whatever those say on
    // the next stream launch since SdlInputHandler is reconstructed fresh
    // from them each time.
    bool mouseImmersive = !m_InputHandler->isAbsoluteMouseMode();
    bool keyboardImmersive = m_InputHandler->captureSystemKeysMode() != StreamingPreferences::CSK_OFF;

    if (ImGui::MenuItem("Off", nullptr, !mouseImmersive && !keyboardImmersive)) {
        m_InputHandler->setAbsoluteMouseMode(true);
        m_InputHandler->setCaptureSystemKeysMode(StreamingPreferences::CSK_OFF);
    }
    if (ImGui::MenuItem("Keyboard", nullptr, !mouseImmersive && keyboardImmersive)) {
        m_InputHandler->setAbsoluteMouseMode(true);
        m_InputHandler->setCaptureSystemKeysMode(StreamingPreferences::CSK_ALWAYS);
    }
    if (ImGui::MenuItem("Mouse", nullptr, mouseImmersive && !keyboardImmersive)) {
        m_InputHandler->setAbsoluteMouseMode(false);
        m_InputHandler->setCaptureSystemKeysMode(StreamingPreferences::CSK_OFF);
    }
    if (ImGui::MenuItem("Both", nullptr, mouseImmersive && keyboardImmersive)) {
        m_InputHandler->setAbsoluteMouseMode(false);
        m_InputHandler->setCaptureSystemKeysMode(StreamingPreferences::CSK_ALWAYS);
    }

    ImGui::End();
}

void StreamOverlay::renderSettingsSubmenu()
{
    ImGui::Begin("##settingssubmenu", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

    // Same StreamingPreferences bindings as SettingsScreen's full Settings
    // window (app/gui2/settingsscreen.cpp) -- just the quick subset.
    // Changes apply to future launches, same as that screen; there's no
    // live-apply-mid-stream mechanism for v1.
    ImGui::TextUnformatted("Applies to future launches:");
    const int maxBitrate = m_Prefs->unlockBitrate ? 500000 : 150000;
    ImGui::SliderInt("Bitrate (Kbps)", &m_Prefs->bitrateKbps, 500, maxBitrate);
    ImGui::InputInt("Width", &m_Prefs->width);
    ImGui::InputInt("Height", &m_Prefs->height);
    ImGui::InputInt("FPS", &m_Prefs->fps);
    if (ImGui::Button("Save")) {
        m_Prefs->save();
    }

    ImGui::End();
}

void StreamOverlay::renderStatsSubmenu()
{
    ImGui::Begin("##statssubmenu", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

    // Reuses FFmpegVideoDecoder::stringifyVideoStats()'s already-formatted
    // text (ffmpeg.cpp), written into this same OverlayDebug slot once per
    // second while it's enabled -- not a second stats-collection path.
    ImGui::TextUnformatted(m_OverlayManager->getOverlayText(Overlay::OverlayDebug));

    ImGui::End();
}

void StreamOverlay::closeMenu()
{
    if (m_DebugOverlayEnabledByUs) {
        m_OverlayManager->setOverlayState(Overlay::OverlayDebug, false);
        m_DebugOverlayEnabledByUs = false;
    }

    m_MenuOpen = false;
    m_ActiveSubmenu = Submenu::None;
}

void StreamOverlay::toggleButtonHidden()
{
    m_Prefs->overlayButtonHidden = !m_Prefs->overlayButtonHidden;
    m_Prefs->save();
    m_NeedsRender = true;

    if (m_Prefs->overlayButtonHidden) {
        closeMenu();
    }
}
