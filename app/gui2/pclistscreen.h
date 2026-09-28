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
    explicit PcListScreen(QObject* parent = nullptr);
    ~PcListScreen() override;

    // Must be called once after construction, on the Qt thread that will
    // keep pumping events for the lifetime of this screen.
    void start();

    // Draws this frame's ImGui widgets. Called every tick.
    void render();

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
};
