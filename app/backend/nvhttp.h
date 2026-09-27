#pragma once

#include "identitymanager.h"
#include "nvapp.h"
#include "nvaddress.h"

#include <Limelight.h>

#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class NvComputer;

class NvDisplayMode
{
public:
    bool operator==(const NvDisplayMode& other) const
    {
        return width == other.width &&
                height == other.height &&
                refreshRate == other.refreshRate;
    }

    int width;
    int height;
    int refreshRate;
};
Q_DECLARE_TYPEINFO(NvDisplayMode, Q_PRIMITIVE_TYPE);

class GfeHttpResponseException : public std::exception
{
public:
    GfeHttpResponseException(int statusCode, QString message) :
        m_StatusCode(statusCode),
        m_StatusMessage(message.toUtf8())
    {

    }

    const char* what() const throw()
    {
        return m_StatusMessage.constData();
    }

    const char* getStatusMessage() const
    {
        return m_StatusMessage.constData();
    }

    int getStatusCode() const
    {
        return m_StatusCode;
    }

    QString toQString() const
    {
        return QString::fromUtf8(m_StatusMessage) + " (Error " + QString::number(m_StatusCode) + ")";
    }

private:
    int m_StatusCode;
    QByteArray m_StatusMessage;
};

class QtNetworkReplyException : public std::exception
{
public:
    QtNetworkReplyException(QNetworkReply::NetworkError error, QString errorText) :
        m_Error(error),
        m_ErrorText(errorText.toUtf8())
    {

    }

    const char* what() const throw()
    {
        return m_ErrorText.constData();
    }

    const char* getErrorText() const
    {
        return m_ErrorText.constData();
    }

    QNetworkReply::NetworkError getError() const
    {
        return m_Error;
    }

    QString toQString() const
    {
        return QString::fromUtf8(m_ErrorText) + " (Error " + QString::number(m_Error) + ")";
    }

private:
    QNetworkReply::NetworkError m_Error;
    QByteArray m_ErrorText;
};

class NvHTTP : public QObject
{
    Q_OBJECT

public:
    enum NvLogLevel {
        NVLL_NONE,
        NVLL_ERROR,
        NVLL_VERBOSE
    };

    explicit NvHTTP(NvAddress address, uint16_t httpsPort, QSslCertificate serverCert, bool useTrueUid, QNetworkAccessManager* nam = nullptr);

    explicit NvHTTP(NvComputer* computer, QNetworkAccessManager* nam = nullptr);

    static
    int
    getCurrentGame(QString serverInfo);

    QString
    getServerInfo(NvLogLevel logLevel, bool fastFail = false);

    static
    void
    verifyResponseStatus(QString xml);

    static
    QString
    getXmlString(QString xml,
                 QString tagName);

    static
    QByteArray
    getXmlStringFromHex(QString xml,
                        QString tagName);

    QString
    openConnectionToString(QUrl baseUrl,
                           QString command,
                           QString arguments,
                           int timeoutMs,
                           NvLogLevel logLevel = NvLogLevel::NVLL_VERBOSE);

    void setServerCert(QSslCertificate serverCert);
    void setAddress(NvAddress address);
    void setHttpsPort(uint16_t port);
    void setTrueUid(bool useTrueUid);

    NvAddress address();

    QSslCertificate serverCert();

    uint16_t httpPort();

    uint16_t httpsPort();

    static
    QVector<int>
    parseQuad(QString quad);

    void
    quitApp();

    // PoC 6 (agent.md section 11 / 8): resolves remotePath to a registered
    // app's name via Titan's POST /api/custom/remote-run. Throws
    // GfeHttpResponseException on a well-formed error response (e.g. 404
    // REMOTE_RUN_NOT_REGISTERED) or QtNetworkReplyException on a lower-level
    // network failure. See docs/research/poc6-remote-run.md (Destiny
    // superproject) for the endpoint's exact contract and why this returns
    // a name rather than just an app ID. When appIdOut is non-null, it's
    // populated with the resolved app_id so the caller can poll
    // remoteRunStatus() before starting the stream (agent.md section 11.4).
    QString
    remoteRun(QString remotePath, int* appIdOut = nullptr);

    // agent.md sections 8.3/11.4: polls Titan's launch-readiness state for a
    // remote-run-resolved app_id ("starting"/"ready"/"failed") once /launch
    // has been called, so the caller can wait for the capture-window target
    // to actually be resolved before starting the stream. Throws the same
    // exceptions as remoteRun() on network/protocol failure. When the
    // state is "ready" for a capture-window app, widthOut/heightOut (if
    // non-null) are populated with the resolved window's real size, so the
    // caller can size its own stream/client window to match instead of
    // stretching the app's actual content into a mismatched resolution.
    // titleOut (if non-null) is set to the target window's live title, and
    // iconCrc32Out (if non-null) to a checksum of its current icon -- both
    // left untouched (titleOut cleared, iconCrc32Out set to 0) when Titan
    // doesn't report them (e.g. a non-capture-window app), so the caller can
    // tell "unavailable" from "unchanged". See remoteRunIcon() for fetching
    // the actual icon pixels when iconCrc32Out changes between polls.
    QString
    remoteRunStatus(int appId, int* widthOut = nullptr, int* heightOut = nullptr, QString* titleOut = nullptr, quint32* iconCrc32Out = nullptr);

    // Fetches the remote-run target window's current icon as a raw 32x32
    // RGBA8888 buffer (1024 pixels / 4096 bytes, no PNG or other image
    // codec involved -- see docs/research/poc6-remote-run.md's window
    // title/icon mirroring notes) from Titan's
    // GET /api/custom/remote-run/icon. Callers should only call this when
    // remoteRunStatus()'s iconCrc32Out has changed since the last call, to
    // avoid re-fetching an unchanged icon every poll. Throws the same
    // exceptions as remoteRun() on network/protocol failure; returns an
    // empty QByteArray if Titan has no icon available for this app_id.
    QByteArray
    remoteRunIcon(int appId);

    void
    startApp(QString verb,
             bool isGfe,
             int appId,
             PSTREAM_CONFIGURATION streamConfig,
             bool sops,
             bool localAudio,
             int gamepadMask,
             bool persistGameControllersOnDisconnect,
             QString& rtspSessionUrl);

    QVector<NvApp>
    getAppList();

    QImage
    getBoxArt(int appId);

    static
    QVector<NvDisplayMode>
    getDisplayModeList(QString serverInfo);

    QUrl m_BaseUrlHttp;
    QUrl m_BaseUrlHttps;
private:
    void
    handleSslErrors(QNetworkReply* reply, const QList<QSslError>& errors);

    QNetworkReply*
    openConnection(QUrl baseUrl,
                   QString command,
                   QString arguments,
                   int timeoutMs,
                   NvLogLevel logLevel);

    NvAddress m_Address;
    QNetworkAccessManager* m_Nam;
    QSslCertificate m_ServerCert;
    bool m_UseTrueUid;
};
