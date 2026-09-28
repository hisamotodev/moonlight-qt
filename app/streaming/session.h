#pragma once

#include <QSemaphore>
#include <QQuickWindow>
#include <QByteArray>

#include <Limelight.h>
#include <opus_multistream.h>
#include "settings/streamingpreferences.h"
#include "input/input.h"
#include "video/decoder.h"
#include "audio/renderers/renderer.h"
#include "video/overlaymanager.h"

class NvHTTP;
class QThread;
class StreamOverlay;

class SupportedVideoFormatList : public QList<int>
{
public:
    operator int() const
    {
        int value = 0;

        for (const int v : *this) {
            value |= v;
        }

        return value;
    }

    void
    removeByMask(int mask)
    {
        int i = 0;
        while (i < this->length()) {
            if (this->value(i) & mask) {
                this->removeAt(i);
            }
            else {
                i++;
            }
        }
    }

    void
    deprioritizeByMask(int mask)
    {
        QList<int> deprioritizedList;

        int i = 0;
        while (i < this->length()) {
            if (this->value(i) & mask) {
                deprioritizedList.append(this->takeAt(i));
            }
            else {
                i++;
            }
        }

        this->append(std::move(deprioritizedList));
    }

    int maskByServerCodecModes(int serverCodecModes)
    {
        int mask = 0;

        const QMap<int, int> mapping = {
            {SCM_H264, VIDEO_FORMAT_H264},
            {SCM_H264_HIGH8_444, VIDEO_FORMAT_H264_HIGH8_444},
            {SCM_HEVC, VIDEO_FORMAT_H265},
            {SCM_HEVC_MAIN10, VIDEO_FORMAT_H265_MAIN10},
            {SCM_HEVC_REXT8_444, VIDEO_FORMAT_H265_REXT8_444},
            {SCM_HEVC_REXT10_444, VIDEO_FORMAT_H265_REXT10_444},
            {SCM_AV1_MAIN8, VIDEO_FORMAT_AV1_MAIN8},
            {SCM_AV1_MAIN10, VIDEO_FORMAT_AV1_MAIN10},
            {SCM_AV1_HIGH8_444, VIDEO_FORMAT_AV1_HIGH8_444},
            {SCM_AV1_HIGH10_444, VIDEO_FORMAT_AV1_HIGH10_444},
        };

        for (QMap<int, int>::const_iterator it = mapping.cbegin(); it != mapping.cend(); ++it) {
            if (serverCodecModes & it.key()) {
                mask |= it.value();
                serverCodecModes &= ~it.key();
            }
        }

        // Make sure nobody forgets to update this for new SCM values
        SDL_assert(serverCodecModes == 0);

        int val = *this;
        return val & mask;
    }
};

class Session : public QObject
{
    Q_OBJECT

    friend class SdlInputHandler;
    friend class DeferredSessionCleanupTask;
    friend class AsyncConnectionStartThread;
    friend class CaptureWindowPollThread;

public:
    explicit Session(NvComputer* computer, NvApp& app, StreamingPreferences *preferences = nullptr, QString remoteRunPath = QString());
    virtual ~Session();

    Q_INVOKABLE bool initialize(QQuickWindow* qtWindow);
    Q_INVOKABLE void start();
    Q_INVOKABLE void interrupt();
    Q_PROPERTY(QStringList launchWarnings MEMBER m_LaunchWarnings NOTIFY launchWarningsChanged);

    static
    void getDecoderInfo(SDL_Window* window,
                        bool& isHardwareAccelerated, bool& isFullScreenOnly,
                        bool& isHdrSupported, QSize& maxResolution);

    static Session* get()
    {
        return s_ActiveSession;
    }

    Overlay::OverlayManager& getOverlayManager()
    {
        return m_OverlayManager;
    }

    // Non-null only while actually streaming (Session::exec()'s loop owns
    // its lifetime, session.cpp) -- see keyboard.cpp's
    // KeyComboToggleOverlayButton handler for why this needs to be public.
    StreamOverlay* getStreamOverlay()
    {
        return m_StreamOverlay;
    }

    void flushWindowEvents();

    void setShouldExit(bool quitHostApp = false);

    // Called from CaptureWindowPollThread (session.cpp) when Titan's own
    // window-size measurement (GetWindowRect on the resolved HWND, via
    // /api/custom/remote-run/status -- see nvhttp.cpp on the Titan side)
    // reports a different size than last observed. Pushes an SDL event
    // rather than touching the window directly, since this runs on a
    // background thread. No-ops if there's no active session.
    static
    void notifyVideoContentSizeChanged(int width, int height);

    // Called from CaptureWindowPollThread (session.cpp) when the
    // capture-window target's title and/or icon (32x32 RGBA8888, from
    // Titan's /api/custom/remote-run/icon) has changed since the last poll.
    // Takes ownership of `iconRgba` (may be empty if only the title
    // changed). Same background-thread-to-SDL-event pattern as
    // notifyVideoContentSizeChanged() above.
    static
    void notifyWindowInfoChanged(QString title, QByteArray iconRgba);

signals:
    void stageStarting(QString stage);

    void stageFailed(QString stage, int errorCode, QString failingPorts);

    void connectionStarted();

    void displayLaunchError(QString text);

    void quitStarting();

    void sessionFinished(int portTestResult);

    // Emitted after sessionFinished() when the session is ready to be destroyed
    void readyForDeletion();

    void launchWarningsChanged();

private:
    void exec();

    bool startConnectionAsync();

    // Live-testing finding: WGC capture-window sessions can occasionally
    // come up wedged, never delivering a decodable frame (moonlight-common-c
    // logs "Video decode unit queue overflow" / "Waiting for IDR frame" in
    // an endless loop) -- observed to recover if the user manually resizes
    // the Hunter window, which happens to force a decoder recreation. This
    // watches FFmpegVideoDecoder's live decode-frame counter (via
    // IVideoDecoder::getVideoStats()) and, if it hasn't moved for
    // DECODE_STALL_TIMEOUT_MS, does that same recreation automatically by
    // pushing a synthetic SDL_RENDER_DEVICE_RESET (the same event type
    // already used for real GPU device-loss recovery, session.cpp's main
    // loop). Called once per main-loop iteration, alongside
    // m_StreamOverlay->maybeRender() -- see the .cpp for the retry budget.
    void checkDecodeStall();

    // agent.md sections 8.3/11.4: for any capture_window app (window-class
    // configured in apps.json on the Titan side -- not just remote-run
    // launches, see this session's fix), polls Titan's launch-readiness
    // state (capture-window target resolved, etc.) after startApp() actually
    // launches the app but before proceeding with the rest of the connection
    // sequence. For a non-capture_window app, Titan's endpoint reports
    // "ready" immediately, so this is a single fast round trip, not a real
    // wait. Called on startConnectionAsync()'s background thread, so a
    // synchronous poll loop here doesn't block the UI -- see
    // NvHTTP::remoteRunStatus().
    bool waitForCaptureWindowReady(NvHTTP& http, int appId);

    // Called from CaptureWindowPollThread (session.cpp) when the real window
    // size has changed and the negotiated stream resolution should follow.
    // GameStream fixes STREAM_CONFIGURATION for the life of a connection, so
    // this tears the current connection down and renegotiates a new one
    // (reusing startConnectionAsync()'s existing "resume" support) rather
    // than trying to change anything in place. Runs on the poll thread, not
    // the main thread -- see the .cpp for the decoder-teardown handshake
    // this needs with the main thread before it's safe to call
    // LiStopConnection().
    bool reconnectAtResolution(int width, int height);

    bool validateLaunch(SDL_Window* testWindow);

    void emitLaunchWarning(QString text);

    bool populateDecoderProperties(SDL_Window* window);

    IAudioRenderer* createAudioRenderer(const POPUS_MULTISTREAM_CONFIGURATION opusConfig);

    bool initializeAudioRenderer();

    bool testAudio(int audioConfiguration);

    int getAudioRendererCapabilities(int audioConfiguration);

    void getWindowDimensions(int& x, int& y,
                             int& width, int& height);

    void toggleFullscreen();

    void notifyMouseEmulationMode(bool enabled);

    void updateOptimalWindowDisplayMode();

    enum class DecoderAvailability {
        None,
        Software,
        Hardware
    };

    static
    DecoderAvailability getDecoderAvailability(SDL_Window* window,
                                               StreamingPreferences::VideoDecoderSelection vds,
                                               int videoFormat, int width, int height, int frameRate);

    static
    bool chooseDecoder(StreamingPreferences::VideoDecoderSelection vds,
                       StreamingPreferences::RendererSelection renderer,
                       SDL_Window* window, int videoFormat, int width, int height,
                       int frameRate, bool enableVsync, bool enableFramePacing,
                       bool testOnly,
                       IVideoDecoder*& chosenDecoder);

    static
    void clStageStarting(int stage);

    static
    void clStageFailed(int stage, int errorCode);

    static
    void clConnectionTerminated(int errorCode);

    static
    void clLogMessage(const char* format, ...);

    static
    void clRumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor);

    static
    void clConnectionStatusUpdate(int connectionStatus);

    static
    void clSetHdrMode(bool enabled);

    static
    void clRumbleTriggers(uint16_t controllerNumber, uint16_t leftTrigger, uint16_t rightTrigger);

    static
    void clSetMotionEventState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz);

    static
    void clSetControllerLED(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b);

    static
    void clSetAdaptiveTriggers(uint16_t controllerNumber, uint8_t eventFlags, uint8_t typeLeft, uint8_t typeRight, uint8_t *left, uint8_t *right);

    static
    int arInit(int audioConfiguration,
               const POPUS_MULTISTREAM_CONFIGURATION opusConfig,
               void* arContext, int arFlags);

    static
    void arCleanup();

    static
    void arDecodeAndPlaySample(char* sampleData, int sampleLength);

    static
    int drSetup(int videoFormat, int width, int height, int frameRate, void*, int);

    static
    void drCleanup();

    static
    int drSubmitDecodeUnit(PDECODE_UNIT du);

    StreamingPreferences* m_Preferences;
    bool m_IsFullScreen;
    SupportedVideoFormatList m_SupportedVideoFormats; // Sorted in order of descending priority
    STREAM_CONFIGURATION m_StreamConfig;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacks;
    AUDIO_RENDERER_CALLBACKS m_AudioCallbacks;
    NvComputer* m_Computer;
    NvApp m_App;
    QString m_RemoteRunPath;
    SDL_Window* m_Window;
    // Named for what it polls (any capture_window app's real window size/
    // title/icon), not for how the app was launched -- it runs for every
    // session, remote-run or not (see this session's fix; Titan's endpoint
    // is a cheap near-instant no-op for non-capture_window apps).
    QThread* m_CaptureWindowPollThread = nullptr;
    QSemaphore m_ReconnectDecoderTornDownSem {0};
    IVideoDecoder* m_VideoDecoder;
    SDL_mutex* m_DecoderLock;

    // checkDecodeStall()'s state. Reset whenever the decoder is (re)created
    // (session.cpp's SDL_RENDER_DEVICE_RESET case) so each decoder instance
    // gets its own fresh grace period.
    uint32_t m_LastDecodedFrameCount = 0;
    uint32_t m_LastDecodeProgressTicks = 0;
    int m_DecodeStallRecoveryAttempts = 0;
    bool m_AudioDisabled;
    bool m_AudioMuted;
    Uint32 m_FullScreenFlag;
    QQuickWindow* m_QtWindow;
    bool m_UnexpectedTermination;
    SdlInputHandler* m_InputHandler;
    StreamOverlay* m_StreamOverlay = nullptr;
    int m_MouseEmulationRefCount;
    int m_FlushingWindowEventsRef;
    QStringList m_LaunchWarnings;
    bool m_ShouldExit;

    bool m_AsyncConnectionSuccess;
    int m_PortTestResults;

    int m_ActiveVideoFormat;
    int m_ActiveVideoWidth;
    int m_ActiveVideoHeight;
    int m_ActiveVideoFrameRate;

    OpusMSDecoder* m_OpusDecoder;
    IAudioRenderer* m_AudioRenderer;
    OPUS_MULTISTREAM_CONFIGURATION m_ActiveAudioConfig;
    OPUS_MULTISTREAM_CONFIGURATION m_OriginalAudioConfig;
    int m_AudioSampleCount;
    Uint32 m_DropAudioEndTime;

    Overlay::OverlayManager m_OverlayManager;

    static CONNECTION_LISTENER_CALLBACKS k_ConnCallbacks;
    static Session* s_ActiveSession;
    static QSemaphore s_ActiveSessionSemaphore;
};
