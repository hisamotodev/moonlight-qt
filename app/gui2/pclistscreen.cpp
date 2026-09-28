#include "pclistscreen.h"

#include "backend/computermanager.h"
#include "backend/nvcomputer.h"

#include <QReadLocker>

#include <imgui.h>

PcListScreen::PcListScreen(ComputerManager* computerManager, QObject* parent)
    : QObject(parent),
      m_ComputerManager(computerManager),
      m_ShowAddPcPopup(false),
      m_ShowPairDialog(false),
      m_ShowErrorDialog(false),
      m_ShowDeleteConfirm(false),
      m_DeleteTarget(nullptr)
{
    m_AddPcAddressBuf[0] = '\0';

    // Mirrors PcView.qml's createModel(): ComputerManager is a plain
    // QObject, so we talk to it directly instead of going through
    // ComputerModel (which only exists to adapt it to QAbstractListModel
    // for QML's ListView).
    connect(m_ComputerManager, &ComputerManager::pairingCompleted,
            this, &PcListScreen::handlePairingCompleted);
    connect(m_ComputerManager, &ComputerManager::computerAddCompleted,
            this, &PcListScreen::handleComputerAddCompleted);
}

PcListScreen::~PcListScreen()
{
}

void PcListScreen::render()
{
    renderPcList();
    renderAddPcPopup();
    renderPairDialog();
    renderErrorDialog();
    renderDeleteConfirmDialog();
}

void PcListScreen::renderPcList()
{
    ImGui::Begin("Computers");

    if (ImGui::Button("Add PC by IP...")) {
        m_AddPcAddressBuf[0] = '\0';
        m_ShowAddPcPopup = true;
    }

    ImGui::Separator();

    // getComputers() takes ComputerManager's own read lock and returns a
    // fresh sorted snapshot (computermanager.cpp), so it's cheap enough to
    // call every frame -- no need to cache/manually invalidate on
    // computerStateChanged the way ComputerModel does for QML.
    const QVector<NvComputer*> computers = m_ComputerManager->getComputers();
    for (NvComputer* computer : computers) {
        QString name;
        bool online;
        bool paired;
        bool supported;
        {
            QReadLocker lock(&computer->lock);
            name = computer->name;
            online = computer->state == NvComputer::CS_ONLINE;
            paired = computer->pairState == NvComputer::PS_PAIRED;
            supported = computer->isSupportedServerVersion;
        }

        QString status;
        if (!online) {
            status = QStringLiteral("Offline");
        } else if (!supported) {
            status = QStringLiteral("Unsupported version");
        } else if (paired) {
            status = QStringLiteral("Paired");
        } else {
            status = QStringLiteral("Not paired");
        }

        ImGui::PushID(computer);

        // Selectable() with no explicit size stretches its hit-box to the
        // full remaining window width by default -- which, on this same
        // row, extends underneath the "Delete" SmallButton placed via
        // SameLine() below. ImGui resolves overlapping widgets in
        // submission order (the first one to claim the hover for a given
        // frame blocks later ones unless AllowOverlap is set), and since
        // this Selectable is submitted first, it silently swallowed every
        // click meant for Delete. Reserve Delete's column width up front so
        // the two hit-boxes don't overlap at all.
        const float deleteButtonWidth = ImGui::CalcTextSize("Delete").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float selectableWidth = ImGui::GetContentRegionAvail().x - deleteButtonWidth - ImGui::GetStyle().ItemSpacing.x;

        const QByteArray label = (name + QStringLiteral("  --  ") + status).toUtf8();
        if (ImGui::Selectable(label.constData(), false, 0, ImVec2(selectableWidth, 0))) {
            if (online) {
                if (!supported) {
                    m_ErrorText = QStringLiteral(
                        "The version of GeForce Experience/Sunshine on %1 is not "
                        "supported by this build of Moonlight.").arg(name);
                    m_ErrorHelpText.clear();
                    m_ShowErrorDialog = true;
                } else if (paired) {
                    emit computerSelected(computer);
                } else {
                    const QString pin = m_ComputerManager->generatePinString();

                    // Kick off pairing in the background (async, same as
                    // ComputerManager::pairHost() -- it punts to
                    // QThreadPool internally).
                    m_ComputerManager->pairHost(computer, pin);

                    m_PairPin = pin;
                    m_ShowPairDialog = true;
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            m_DeleteTarget = computer;
            m_DeleteTargetName = name;
            m_ShowDeleteConfirm = true;
        }

        ImGui::PopID();
    }

    ImGui::End();
}

void PcListScreen::renderAddPcPopup()
{
    // OpenPopup() and BeginPopupModal() must run at the same ID-stack level
    // (they hash the popup name against the current window's ID stack), so
    // this call has to live right next to BeginPopupModal() below rather
    // than at the button click site, which runs under a different context
    // (inside "Computers"'s Begin/End, possibly under a per-row PushID) --
    // otherwise the two calls compute different popup IDs and the popup can
    // never actually open.
    if (m_ShowAddPcPopup) {
        ImGui::OpenPopup("Add PC");
    }

    if (ImGui::BeginPopupModal("Add PC", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!m_ShowAddPcPopup) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("Enter the IP address of your host PC:");
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("##addpcaddress", m_AddPcAddressBuf, sizeof(m_AddPcAddressBuf));

            const bool submit = ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter);

            if (ImGui::Button("OK") || submit) {
                const QString address = QString::fromUtf8(m_AddPcAddressBuf).trimmed();
                if (!address.isEmpty()) {
                    m_ComputerManager->addNewHostManually(address);
                }
                m_ShowAddPcPopup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                m_ShowAddPcPopup = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void PcListScreen::renderPairDialog()
{
    // See renderAddPcPopup() for why OpenPopup() lives here, next to
    // BeginPopupModal(), instead of at the request site.
    if (m_ShowPairDialog) {
        ImGui::OpenPopup("Pairing");
    }

    // AlwaysAutoResize with no width hint lets TextWrapped() below compute
    // its wrap width from whatever the popup's width happens to be on its
    // very first (pre-layout) frame, which tends to be far too narrow --
    // producing a tall, single-word-per-line dialog. Pin a sane width so
    // the paragraph wraps normally; height still auto-fits the content.
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Pairing", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!m_ShowPairDialog) {
            ImGui::CloseCurrentPopup();
        } else {
            const QByteArray text = QStringLiteral(
                "Please enter %1 on your host PC. This dialog will close "
                "when pairing is completed.\n\n"
                "If your host PC is running Sunshine, navigate to its "
                "management UI to enter the PIN.").arg(m_PairPin).toUtf8();
            ImGui::TextWrapped("%s", text.constData());

            if (ImGui::Button("Cancel")) {
                m_ShowPairDialog = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void PcListScreen::renderErrorDialog()
{
    // See renderAddPcPopup() for why OpenPopup() lives here, next to
    // BeginPopupModal(), instead of at the request site -- this also keeps
    // it off the pairingCompleted/computerAddCompleted signal handlers
    // below, which can run outside ImGui's NewFrame()/Render() bracket
    // (queued cross-thread signals), where ImGui::OpenPopup() would
    // dereference a null current-window.
    if (m_ShowErrorDialog) {
        ImGui::OpenPopup("Error");
    }

    // See renderPairDialog() for why this needs a width hint.
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!m_ShowErrorDialog) {
            ImGui::CloseCurrentPopup();
        } else {
            const QByteArray text = m_ErrorText.toUtf8();
            ImGui::TextWrapped("%s", text.constData());
            if (!m_ErrorHelpText.isEmpty()) {
                ImGui::Spacing();
                const QByteArray help = m_ErrorHelpText.toUtf8();
                ImGui::TextWrapped("%s", help.constData());
            }

            if (ImGui::Button("OK")) {
                m_ShowErrorDialog = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void PcListScreen::renderDeleteConfirmDialog()
{
    // See renderAddPcPopup() for why OpenPopup() lives here, next to
    // BeginPopupModal(), instead of at the Delete button (which runs inside
    // this row's PushID(computer) -- a different ID-stack level than this
    // function, so the popup IDs would never match and Delete would appear
    // to do nothing).
    if (m_ShowDeleteConfirm) {
        ImGui::OpenPopup("Delete PC");
    }

    if (ImGui::BeginPopupModal("Delete PC", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!m_ShowDeleteConfirm) {
            ImGui::CloseCurrentPopup();
        } else {
            const QByteArray text = QStringLiteral("Delete %1?").arg(m_DeleteTargetName).toUtf8();
            ImGui::Text("%s", text.constData());

            if (ImGui::Button("Delete")) {
                if (m_DeleteTarget) {
                    // deleteHost() deletes the NvComputer object itself, so
                    // we must not touch m_DeleteTarget again after this.
                    m_ComputerManager->deleteHost(m_DeleteTarget);
                    m_DeleteTarget = nullptr;
                }
                m_ShowDeleteConfirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                m_DeleteTarget = nullptr;
                m_ShowDeleteConfirm = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void PcListScreen::handlePairingCompleted(NvComputer* computer, QString error)
{
    Q_UNUSED(computer);

    m_ShowPairDialog = false;

    if (!error.isEmpty()) {
        m_ErrorText = error;
        m_ErrorHelpText.clear();
        m_ShowErrorDialog = true;
    }
}

void PcListScreen::handleComputerAddCompleted(QVariant success, QVariant detectedPortBlocking)
{
    if (!success.toBool()) {
        m_ErrorText = QStringLiteral("Unable to connect to the specified PC.");
        m_ErrorHelpText.clear();

        if (detectedPortBlocking.toBool()) {
            m_ErrorText += QStringLiteral(
                "\n\nThis PC's Internet connection is blocking Moonlight. "
                "Streaming over the Internet may not work while connected "
                "to this network.");
        } else {
            m_ErrorHelpText = QStringLiteral("See Moonlight's documentation for possible solutions.");
        }

        m_ShowErrorDialog = true;
    }
}
