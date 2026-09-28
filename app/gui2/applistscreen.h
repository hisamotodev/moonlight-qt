#pragma once

#include <QObject>
#include <QString>

class ComputerManager;
class NvComputer;
class Session;

// Phase 2 of the ImGui frontend: the per-PC app list, a direct port of
// AppView.qml's launch logic onto NvComputer::appList (refreshed in the
// background by ComputerManager's polling, same as PcListScreen reads
// NvComputer's other fields directly -- no AppModel/QAbstractListModel
// adapter needed). Also adds the "Remote Run" action AppView.qml doesn't
// have: resolving a host-side path to an app via NvHTTP::remoteRun(),
// the same call CliStartStream::Launcher makes for --remote-run
// (app/cli/startstream.cpp), just triggered from a button instead of a
// CLI flag.
class AppListScreen : public QObject
{
    Q_OBJECT

public:
    AppListScreen(ComputerManager* computerManager, NvComputer* computer, QObject* parent = nullptr);
    ~AppListScreen() override;

    void render();

signals:
    // The user asked to go back to the PC list (Back button, or the
    // computer went offline/unpaired while we were viewing it).
    void backRequested();

    // A Session is constructed and ready to be started. The caller (
    // ImGuiWindow) owns the actual session lifecycle (initialize/start/
    // signal wiring) since that's shared with however Phase 2 wires up
    // "normal" launches too.
    void launchRequested(Session* session, QString appName);

private slots:
    void handleComputerStateChanged(NvComputer* computer);

private:
    void renderAppList();
    void renderRemoteRunPopup();
    void renderErrorDialog();

    ComputerManager* m_ComputerManager;
    NvComputer* m_Computer;

    bool m_ShowRemoteRunPopup;
    char m_RemoteRunPathBuf[512];

    bool m_ShowErrorDialog;
    QString m_ErrorText;
};
