#include "settingsscreen.h"

#include "settings/streamingpreferences.h"

#include <imgui.h>

SettingsScreen::SettingsScreen(QObject* parent)
    : QObject(parent),
      m_Prefs(StreamingPreferences::get())
{
}

void SettingsScreen::render(bool* open)
{
    if (!*open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", open)) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Resolution / FPS", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputInt("Width", &m_Prefs->width);
        ImGui::InputInt("Height", &m_Prefs->height);
        ImGui::InputInt("FPS", &m_Prefs->fps);
    }

    if (ImGui::CollapsingHeader("Bitrate", ImGuiTreeNodeFlags_DefaultOpen)) {
        const int maxBitrate = m_Prefs->unlockBitrate ? 500000 : 150000;
        ImGui::SliderInt("Bitrate (Kbps)", &m_Prefs->bitrateKbps, 500, maxBitrate);
        ImGui::Checkbox("Unlock bitrate limit", &m_Prefs->unlockBitrate);
        ImGui::Checkbox("Automatically adjust bitrate", &m_Prefs->autoAdjustBitrate);
    }

    if (ImGui::CollapsingHeader("Video")) {
        static const char* codecItems[] = { "Auto", "Force H.264", "Force HEVC", "(deprecated)", "Force AV1" };
        ImGui::Combo("Video codec", reinterpret_cast<int*>(&m_Prefs->videoCodecConfig),
                     codecItems, IM_ARRAYSIZE(codecItems));

        static const char* decoderItems[] = { "Auto", "Force hardware", "Force software" };
        ImGui::Combo("Video decoder", reinterpret_cast<int*>(&m_Prefs->videoDecoderSelection),
                     decoderItems, IM_ARRAYSIZE(decoderItems));

        ImGui::Checkbox("Enable HDR", &m_Prefs->enableHdr);
        ImGui::Checkbox("Enable YUV 4:4:4", &m_Prefs->enableYUV444);
        ImGui::Checkbox("V-Sync", &m_Prefs->enableVsync);
        ImGui::Checkbox("Frame pacing", &m_Prefs->framePacing);
        ImGui::Checkbox("Show performance overlay", &m_Prefs->showPerformanceOverlay);
    }

    if (ImGui::CollapsingHeader("Audio")) {
        static const char* audioItems[] = { "Stereo", "5.1 Surround", "7.1 Surround" };
        ImGui::Combo("Audio configuration", reinterpret_cast<int*>(&m_Prefs->audioConfig),
                     audioItems, IM_ARRAYSIZE(audioItems));
        ImGui::Checkbox("Play audio on host PC", &m_Prefs->playAudioOnHost);
        ImGui::Checkbox("Mute on focus loss", &m_Prefs->muteOnFocusLoss);
    }

    if (ImGui::CollapsingHeader("Window")) {
        static const char* windowItems[] = { "Full screen", "Full screen (desktop)", "Windowed" };
        ImGui::Combo("Window mode", reinterpret_cast<int*>(&m_Prefs->windowMode),
                     windowItems, IM_ARRAYSIZE(windowItems));
    }

    if (ImGui::CollapsingHeader("Input")) {
        ImGui::Checkbox("Optimize mouse for remote desktop instead of games", &m_Prefs->absoluteMouseMode);
        ImGui::Checkbox("Swap left/right mouse buttons", &m_Prefs->swapMouseButtons);
        ImGui::Checkbox("Reverse scroll direction", &m_Prefs->reverseScrollDirection);
        ImGui::Checkbox("Multiple controller support", &m_Prefs->multiController);
        ImGui::Checkbox("Swap A/B and X/Y face buttons", &m_Prefs->swapFaceButtons);
    }

    if (ImGui::CollapsingHeader("Misc")) {
        ImGui::Checkbox("Optimize game settings for streaming", &m_Prefs->gameOptimizations);
        ImGui::Checkbox("Quit app on host PC after ending stream", &m_Prefs->quitAppAfter);
        ImGui::Checkbox("Keep host PC awake while streaming", &m_Prefs->keepAwake);
        ImGui::Checkbox("Show Discord Rich Presence", &m_Prefs->richPresence);
        ImGui::Checkbox("Enable mDNS discovery", &m_Prefs->enableMdns);
        ImGui::Checkbox("Warn me about connection quality issues", &m_Prefs->connectionWarnings);
    }

    ImGui::Separator();
    if (ImGui::Button("Save")) {
        m_Prefs->save();
    }

    ImGui::Separator();
    // VERSION_STR/VERSION_COMMIT_STR are qmake compiler defines
    // (app.pro/app/version.txt + git rev-parse) -- same values
    // SystemProperties::versionString/versionCommitString expose, shown
    // here directly since this screen doesn't otherwise need a
    // SystemProperties instance.
    ImGui::TextDisabled("Moonlight " VERSION_STR " (" VERSION_COMMIT_STR ")");

    ImGui::End();
}
