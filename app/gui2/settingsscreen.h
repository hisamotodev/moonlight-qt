#pragma once

#include <QObject>

class StreamingPreferences;

// Phase 3 of the ImGui frontend: a flat property-editing Settings window,
// the same "bind the struct fields directly to widgets, no per-field
// validation" approach Titan's own ui_settings.cpp uses for its Settings
// tab. StreamingPreferences' fields are plain public members (not just
// Q_PROPERTY accessors), so widgets bind straight to them; Save() persists
// via StreamingPreferences::save() (QSettings-backed, unchanged).
class SettingsScreen : public QObject
{
    Q_OBJECT

public:
    explicit SettingsScreen(QObject* parent = nullptr);

    // Renders the window only while *open is true; *open follows ImGui's
    // usual "close button clears the bool" convention.
    void render(bool* open);

private:
    StreamingPreferences* m_Prefs;
};
