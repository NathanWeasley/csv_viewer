#pragma once

#include "sdk/viewer_plugin_sdk/include/viewer_plugin/viewer_json_sdk.h"

#include <QHash>
#include <QList>

namespace viewer
{

// Viewer-owned, session-scoped storage for immutable JSON documents produced
// by plugins. Thread marshalling and active-session validation are performed by
// Viewer/PluginHost; this class deliberately contains no UI or plugin logic.
class JsonDocumentStore final
{
public:
    plugin::JsonPublishResult publishBatch(
        const QString& providerPluginId,
        quint64 sessionId,
        const QList<plugin::JsonDocumentPublishItem>& documents);

    QList<plugin::JsonDocumentInfo> listDocuments(
        quint64 sessionId,
        const QString& providerPluginId = {}) const;

    plugin::JsonDocumentPtr acquireDocument(
        quint64 sessionId,
        const QString& providerPluginId,
        const QString& documentId,
        plugin::JsonDocumentInfo* info = nullptr) const;

    bool clearSession(quint64 sessionId);
    bool removeProvider(const QString& providerPluginId);

private:
    struct StoredDocument
    {
        plugin::JsonDocumentInfo info;
        plugin::JsonDocumentPtr document;
    };

    using DocumentMap = QHash<QString, StoredDocument>;
    using ProviderMap = QHash<QString, DocumentMap>;
    QHash<quint64, ProviderMap> m_sessions;
};

} // namespace viewer
