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
      m_ShowErrorDialog(false)
{
    m_RemoteRunPathBuf[0] = '\0';

    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &AppListScreen::handleComputerStateChanged);
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
        ImGui::OpenPopup("Remote Run");
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
            emit launchRequested(new Session(m_Computer, app), app.name);
        }
        ImGui::PopID();
    }

    ImGui::End();
}

void AppListScreen::renderRemoteRunPopup()
{
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
                        ImGui::OpenPopup("Error");
                    }

                    if (!appName.isEmpty()) {
                        NvApp* found = nullptr;
                        {
                            QReadLocker lock(&m_Computer->lock);
                            for (NvApp& app : m_Computer->appList) {
                                if (app.name == appName) {
                                    found = &app;
                                    break;
                                }
                            }
                        }

                        if (found) {
                            emit launchRequested(new Session(m_Computer, *found, nullptr, path), appName);
                        } else {
                            m_ErrorText = QStringLiteral(
                                "Resolved \"%1\" to app \"%2\", but it isn't in this PC's app "
                                "list yet. Wait a moment for the app list to refresh and try again.")
                                    .arg(path, appName);
                            m_ShowErrorDialog = true;
                            ImGui::OpenPopup("Error");
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
