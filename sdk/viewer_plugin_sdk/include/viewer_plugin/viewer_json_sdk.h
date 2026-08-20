#pragma once

#include <QJsonDocument>
#include <QList>
#include <QSharedPointer>
#include <QString>
#include <QtGlobal>

#include <functional>

namespace viewer::plugin
{

enum class JsonDocumentState
{
    Ready,
    Missing,
    Invalid
};

enum class JsonDiagnosticSeverity
{
    Information,
    Warning,
    Error
};

struct JsonDiagnostic
{
    JsonDiagnosticSeverity severity = JsonDiagnosticSeverity::Information;
    QString code;
    QString message;
    QString path;
    qint64 byteOffset = -1;
    qint64 recordIndex = -1;
};

using JsonDocumentPtr = QSharedPointer<const QJsonDocument>;

struct JsonDocumentPublishItem
{
    QString documentId;
    QString displayName;
    QString sourceEntryPath;
    QString sourceTableName;
    QString producerVersion;
    JsonDocumentState state = JsonDocumentState::Invalid;
    QString error;
    QList<JsonDiagnostic> diagnostics;
    JsonDocumentPtr document;
};

struct JsonDocumentInfo
{
    quint64 sessionId = 0;
    QString providerPluginId;
    QString documentId;
    QString displayName;
    QString sourceEntryPath;
    QString sourceTableName;
    QString producerVersion;
    JsonDocumentState state = JsonDocumentState::Invalid;
    QString error;
    QList<JsonDiagnostic> diagnostics;

    bool isReady() const noexcept { return state == JsonDocumentState::Ready; }
};

enum class JsonPublishStatus
{
    Success,
    InvalidArgument,
    StaleSession,
    DuplicateDocumentId,
    InvalidDocument,
    HostShuttingDown,
    InternalError
};

struct JsonPublishResult
{
    JsonPublishStatus status = JsonPublishStatus::InternalError;
    QString error;

    bool success() const noexcept { return status == JsonPublishStatus::Success; }
};

using JsonSubscriptionId = quint64;
using JsonDocumentsChangedCallback =
    std::function<void(quint64 sessionId, const QString& providerPluginId)>;

class IJsonDocumentService
{
public:
    virtual ~IJsonDocumentService() = default;

    // Atomically replaces every document published by providerPluginId for the
    // active session. The method may be called from a worker thread.
    virtual JsonPublishResult publishBatch(
        const QString& providerPluginId,
        quint64 sessionId,
        const QList<JsonDocumentPublishItem>& documents) = 0;

    virtual QList<JsonDocumentInfo> listDocuments(
        quint64 sessionId,
        const QString& providerPluginId = {}) const = 0;

    virtual JsonDocumentPtr acquireDocument(
        quint64 sessionId,
        const QString& providerPluginId,
        const QString& documentId,
        JsonDocumentInfo* info = nullptr) const = 0;

    virtual JsonSubscriptionId subscribeDocumentsChanged(
        const QString& ownerPluginId,
        JsonDocumentsChangedCallback callback) = 0;
    virtual void unsubscribeDocumentsChanged(JsonSubscriptionId subscription) = 0;
};

} // namespace viewer::plugin
