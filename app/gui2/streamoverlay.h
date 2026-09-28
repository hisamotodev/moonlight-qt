#pragma once

#include <cstdint>

struct SDL_Window;
struct SDL_Surface;
struct SDL_Renderer;
union SDL_Event;
struct ImGuiContext;

namespace Overlay { class OverlayManager; }
class StreamingPreferences;
class SdlInputHandler;

// Part 2, Phases 5-9 of the Hunter frontend plan: the Parsec-style
// in-stream overlay -- a draggable button that opens a menu with
// Hide Button / Immersive Mode / Settings / Stats / Disconnect. Owned by
// Session and driven from inside Session::exec()'s own SDL event loop
// (session.cpp), since that loop already owns the only SDL event pump
// active during a stream (it deliberately suspends Qt's own loop for the
// duration -- see ImGuiWindow's class comment for the contrast).
//
// Renders into its own offscreen SDL_Surface, sized to the stream window,
// via SDL2's built-in software renderer (SDL_CreateSoftwareRenderer) and a
// *separate* Dear ImGui context from ImGuiWindow's (that context's
// imgui_impl_sdl2/imgui_impl_sdlrenderer2 backend is bound to a different
// SDL_Window/SDL_Renderer pair and must not be touched while it's paused
// during a stream). The resulting surface is handed to the existing
// Overlay::OverlayManager surface-swap mechanism (Overlay::OverlayMenu)
// that every video renderer backend already knows how to composite --
// see overlaymanager.h/.cpp and the OverlayMenu position case added to
// each ffmpeg-renderers/*.cpp file.
class StreamOverlay
{
public:
    StreamOverlay(SDL_Window* window, Overlay::OverlayManager* overlayManager,
                  StreamingPreferences* prefs, SdlInputHandler* inputHandler);
    ~StreamOverlay();

    // Feeds one SDL event to this overlay's ImGui IO state. Returns true
    // if the overlay is currently capturing input (menu open, or actively
    // dragging the button) and the event should NOT also be forwarded to
    // the game's normal input handler.
    bool processEvent(const SDL_Event& event);

    // Re-renders (throttled to ~30 Hz internally) and pushes a new surface
    // to OverlayManager if the button/menu is visible. Cheap to call every
    // loop iteration -- does nothing most calls.
    void maybeRender();

    // Bound to Ctrl+Alt+Shift+O (KeyComboToggleOverlayButton, input.cpp/
    // keyboard.cpp) since hiding the button from its own menu would
    // otherwise leave no way to bring it back.
    void toggleButtonHidden();

private:
    enum class Submenu {
        None,
        Immersive,
        Settings,
        Stats,
    };

    void renderFrame();
    void ensureSurface(int width, int height);
    void renderButtonAndMenu();
    void renderImmersiveSubmenu();
    void renderSettingsSubmenu();
    void renderStatsSubmenu();
    void closeMenu();

    SDL_Window* m_Window;
    Overlay::OverlayManager* m_OverlayManager;
    StreamingPreferences* m_Prefs;
    SdlInputHandler* m_InputHandler;

    ImGuiContext* m_Context;
    SDL_Surface* m_Surface;
    SDL_Renderer* m_SoftRenderer;
    int m_SurfaceWidth;
    int m_SurfaceHeight;

    uint32_t m_LastRenderTicks;
    bool m_NeedsRender;

    bool m_MenuOpen;
    Submenu m_ActiveSubmenu;
    bool m_Dragging;
    float m_DragOffsetX;
    float m_DragOffsetY;

    // Whether we turned OverlayDebug on ourselves to source Stats submenu
    // text (see FFmpegVideoDecoder::stringifyVideoStats(), ffmpeg.cpp) --
    // if so, we turn it back off when the Stats submenu closes rather than
    // leaving the classic debug HUD on if the user didn't already have
    // showPerformanceOverlay enabled.
    bool m_DebugOverlayEnabledByUs;
};
