#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

class ComputerManager;
class NvComputer;

// Phase 1 of the ImGui frontend: the PC list + pairing screen, a direct
// port of PcView.qml's logic onto ComputerManager (which is already a
// plain QObject -- no QML-specific glue needed). Owns its own
// ComputerManager instance and renders every frame from ImGuiWindow.
class PcListScreen : public QObject
{
    Q_OBJECT

public:
    // computerManager is owned by the caller (ImGuiWindow), which also
    // hands the same instance to AppListScreen once a paired PC is picked
    // -- NvComputer pointers are only valid against the ComputerManager
    // that owns them.
    explicit PcListScreen(ComputerManager* computerManager, QObject* parent = nullptr);
    ~PcListScreen() override;

    // Draws this frame's ImGui widgets. Called every tick.
    void render();

    // Clears the row highlight applied to the last-selected PC. Called by
    // ImGuiWindow when the app list panel closes (Back/Close button, or the
    // selected PC went offline/unpaired), so a stale highlight doesn't
    // linger on a PC that's no longer being browsed.
    void clearSelection();

signals:
    // The user picked an online, paired PC to browse its app list.
    void computerSelected(NvComputer* computer);

private slots:
    void handlePairingCompleted(NvComputer* computer, QString error);
    void handleComputerAddCompleted(QVariant success, QVariant detectedPortBlocking);

private:
    void renderPcList();
    void renderAddPcPopup();
    void renderPairDialog();
    void renderErrorDialog();
    void renderDeleteConfirmDialog();

    ComputerManager* m_ComputerManager;

    bool m_ShowAddPcPopup;
    char m_AddPcAddressBuf[256];

    bool m_ShowPairDialog;
    QString m_PairPin;

    bool m_ShowErrorDialog;
    QString m_ErrorText;
    QString m_ErrorHelpText;

    bool m_ShowDeleteConfirm;
    NvComputer* m_DeleteTarget;
    QString m_DeleteTargetName;

    // The PC currently being browsed in the (now always-visible) app list
    // panel, purely for row highlighting -- ImGuiWindow owns the actual
    // AppListScreen lifecycle.
    NvComputer* m_SelectedComputer;
};
