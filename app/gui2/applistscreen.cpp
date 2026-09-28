#include "applistscreen.h"

#include "backend/computermanager.h"
#include "backend/nvcomputer.h"
#include "backend/nvhttp.h"
#include "streaming/session.h"

#include <QReadLocker>

#include <algorithm>

#include <imgui.h>

AppListScreen::AppListScreen(ComputerManager* computerManager, NvComputer* computer, QObject* parent)
    : QObject(parent),
      m_ComputerManager(computerManager),
      m_Computer(computer),
      m_ShowRemoteRunPopup(false),
      m_ShowErrorDialog(false),
      m_ShowQuitConfirm(false),
      m_QuitInProgress(false)
{
    m_RemoteRunPathBuf[0] = '\0';

    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &AppListScreen::handleComputerStateChanged);
    connect(m_ComputerManager, &ComputerManager::quitAppCompleted,
            this, &AppListScreen::handleQuitAppCompleted);
}

AppListScreen::~AppListScreen()
{
}

void AppListScreen::handleComputerStateChanged(NvComputer* computer)
{
    if (computer != m_Computer) {
        return;
    }

    // Mirrors AppModel::handleComputerStateChanged()'s computerLost():
    // bounce back to the PC list if we've gone offline or been unpaired
    // out from under the user.
    if (m_Computer->state == NvComputer::CS_OFFLINE ||
            m_Computer->pairState == NvComputer::PS_NOT_PAIRED) {
        emit backRequested();
    }
}

void AppListScreen::render()
{
    renderAppList();
    renderRemoteRunPopup();
    renderErrorDialog();
    renderQuitConfirmDialog();
}

void AppListScreen::requestLaunch(const NvApp& app, const QString& remoteRunPath)
{
    int runningId;
    {
        QReadLocker lock(&m_Computer->lock);
        runningId = m_Computer->currentGameId;
    }

    if (runningId != 0 && runningId != app.id) {
        // Mirrors AppView.qml's launchOrResumeSelectedApp(): a different
        // app is already running, so confirm quitting it first rather
        // than letting the host reject (or otherwise misbehave on) a
        // /launch while another app is active.
        m_RunningAppName.clear();
        {
            QReadLocker lock(&m_Computer->lock);
            for (const NvApp& existing : std::as_const(m_Computer->appList)) {
                if (existing.id == runningId) {
                    m_RunningAppName = existing.name;
                    break;
                }
            }
        }

        m_PendingLaunchApp = app;
        m_PendingRemoteRunPath = remoteRunPath;
        m_ShowQuitConfirm = true;
        return;
    }

    // Session's constructor takes NvApp& (non-const), so route through a
    // member lvalue rather than the const-ref parameter.
    m_PendingLaunchApp = app;
    emit launchRequested(new Session(m_Computer, m_PendingLaunchApp, nullptr, remoteRunPath), app.name);
}

void AppListScreen::renderAppList()
{
    QString title;
    {
        QReadLocker lock(&m_Computer->lock);
        title = m_Computer->name;
    }

    const QByteArray windowTitle = title.toUtf8();
    ImGui::Begin(windowTitle.constData());

    if (ImGui::Button("<- Back")) {
        emit backRequested();
    }

    ImGui::SameLine();
    if (ImGui::Button("Remote Run...")) {
        m_RemoteRunPathBuf[0] = '\0';
        m_ShowRemoteRunPopup = true;
    }

    ImGui::Separator();

    QVector<NvApp> apps;
    int currentGameId;
    {
        QReadLocker lock(&m_Computer->lock);
        apps = m_Computer->appList;
        currentGameId = m_Computer->currentGameId;
    }

    std::sort(apps.begin(), apps.end(), [](const NvApp& a, const NvApp& b) {
        return a.name.toLower() < b.name.toLower();
    });

    for (NvApp& app : apps) {
        if (app.hidden) {
            continue;
        }

        const bool running = currentGameId == app.id;
        QString label = app.name;
        if (running) {
            label += QStringLiteral("  (Running)");
        }

        const QByteArray labelUtf8 = label.toUtf8();
        ImGui::PushID(app.id);
        if (ImGui::Selectable(labelUtf8.constData())) {
            requestLaunch(app);
        }
        ImGui::PopID();
    }

    ImGui::End();
}

void AppListScreen::renderRemoteRunPopup()
{
    // OpenPopup() and BeginPopupModal() must run at the same ID-stack level
    // (see PcListScreen::renderAddPcPopup() for the full explanation), so
    // this lives here rather than at the "Remote Run..." button click site.
    if (m_ShowRemoteRunPopup) {
        ImGui::OpenPopup("Remote Run");
    }

    if (ImGui::BeginPopupModal("Remote Run", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!m_ShowRemoteRunPopup) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("Enter the full path to the executable on the host PC:");
            ImGui::SetNextItemWidth(500);
            ImGui::InputText("##remoterunpath", m_RemoteRunPathBuf, sizeof(m_RemoteRunPathBuf));

            if (ImGui::Button("Run")) {
                const QString path = QString::fromUtf8(m_RemoteRunPathBuf).trimmed();
                m_ShowRemoteRunPopup = false;
                ImGui::CloseCurrentPopup();

                if (!path.isEmpty()) {
                    // Same call CliStartStream::Launcher makes for
                    // --remote-run (startstream.cpp): resolve the path to
                    // an app name via Titan's /api/custom/remote-run, then
                    // proceed through the normal by-name app flow. This is
                    // a blocking network call on the calling (GUI) thread,
                    // same as the CLI path -- a brief freeze here matches
                    // existing precedent rather than introducing new
                    // async-callback plumbing for v1.
                    QString appName;
                    try {
                        NvHTTP http(m_Computer);
                        appName = http.remoteRun(path);
                    } catch (const std::exception& e) {
                        m_ErrorText = QStringLiteral("Remote-run failed to resolve \"%1\": %2")
                                .arg(path, QString::fromUtf8(e.what()));
                        m_ShowErrorDialog = true;
                    }

                    if (!appName.isEmpty()) {
                        bool found = false;
                        NvApp foundApp;
                        {
                            QReadLocker lock(&m_Computer->lock);
                            for (const NvApp& app : std::as_const(m_Computer->appList)) {
                                if (app.name == appName) {
                                    found = true;
                                    foundApp = app;
                                    break;
                                }
                            }
                        }

                        if (found) {
                            requestLaunch(foundApp, path);
                        } else {
                            m_ErrorText = QStringLiteral(
                                "Resolved \"%1\" to app \"%2\", but it isn't in this PC's app "
                                "list yet. Wait a moment for the app list to refresh and try again.")
                                    .arg(path, appName);
                            m_ShowErrorDialog = true;
                        }
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                m_ShowRemoteRunPopup = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void AppListScreen::renderErrorDialog()
{
    // See renderRemoteRunPopup() / PcListScreen::renderAddPcPopup() -- this
    // must be the only place that calls OpenPopup("Error") so it always
    // runs at the same ID-stack level as BeginPopupModal() below, and never
    // from the quitAppCompleted signal handler, which can run outside
    // ImGui's NewFrame()/Render() bracket.
    if (m_ShowErrorDialog) {
        ImGui::OpenPopup("Error");
    }

    // AlwaysAutoResize + TextWrapped with no width hint computes the wrap
    // width from the popup's pre-layout size on its first frame, which is
    // too narrow -- producing a tall, single-word-per-line dialog. Pin a
    // sane width; height still auto-fits the content.
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!m_ShowErrorDialog) {
            ImGui::CloseCurrentPopup();
        } else {
            const QByteArray text = m_ErrorText.toUtf8();
            ImGui::TextWrapped("%s", text.constData());

            if (ImGui::Button("OK")) {
                m_ShowErrorDialog = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void AppListScreen::renderQuitConfirmDialog()
{
    // See renderErrorDialog() above for why OpenPopup() lives here.
    if (m_ShowQuitConfirm) {
        ImGui::OpenPopup("Quit Running App");
    }

    // See renderErrorDialog() above for why this needs a width hint.
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Quit Running App", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!m_ShowQuitConfirm) {
            ImGui::CloseCurrentPopup();
        } else if (m_QuitInProgress) {
            ImGui::Text("Quitting %s...", m_RunningAppName.toUtf8().constData());
        } else {
            const QByteArray text = QStringLiteral("%1 is currently running. Quit it and launch %2?")
                    .arg(m_RunningAppName, m_PendingLaunchApp.name).toUtf8();
            ImGui::TextWrapped("%s", text.constData());

            if (ImGui::Button("Quit and Launch")) {
                m_QuitInProgress = true;
                m_ComputerManager->quitRunningApp(m_Computer);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                m_ShowQuitConfirm = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void AppListScreen::handleQuitAppCompleted(QVariant error)
{
    if (!m_QuitInProgress) {
        return;
    }

    m_QuitInProgress = false;
    m_ShowQuitConfirm = false;

    if (!error.toString().isEmpty()) {
        m_ErrorText = QStringLiteral("Quitting %1 failed: %2").arg(m_RunningAppName, error.toString());
        m_ShowErrorDialog = true;
        return;
    }

    emit launchRequested(new Session(m_Computer, m_PendingLaunchApp, nullptr, m_PendingRemoteRunPath),
                          m_PendingLaunchApp.name);
}
