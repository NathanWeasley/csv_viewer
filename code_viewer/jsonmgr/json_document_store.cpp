#include "code_viewer/jsonmgr/json_document_store.h"

#include <QSet>

#include <algorithm>

namespace viewer
{

plugin::JsonPublishResult JsonDocumentStore::publishBatch(
    const QString& providerPluginId,
    quint64 sessionId,
    const QList<plugin::JsonDocumentPublishItem>& documents)
{
    using namespace plugin;

    if (providerPluginId.trimmed().isEmpty() || sessionId == 0)
    {
        return {JsonPublishStatus::InvalidArgument,
                QStringLiteral("Provider plugin id and session id are required.")};
    }

    QSet<QString> documentIds;
    DocumentMap replacement;
    for (const JsonDocumentPublishItem& item : documents)
    {
        const QString documentId = item.documentId.trimmed();
        if (documentId.isEmpty())
        {
            return {JsonPublishStatus::InvalidArgument,
                    QStringLiteral("A JSON document id is empty.")};
        }
        if (documentIds.contains(documentId))
        {
            return {JsonPublishStatus::DuplicateDocumentId,
                    QStringLiteral("Duplicate JSON document id: %1").arg(documentId)};
        }
        if (item.state == JsonDocumentState::Ready
            && (!item.document || item.document->isNull()))
        {
            return {JsonPublishStatus::InvalidDocument,
                    QStringLiteral("Ready JSON document '%1' has no content.")
                        .arg(documentId)};
        }

        documentIds.insert(documentId);
        JsonDocumentInfo info;
        info.sessionId = sessionId;
        info.providerPluginId = providerPluginId;
        info.documentId = documentId;
        info.displayName = item.displayName;
        info.sourceEntryPath = item.sourceEntryPath;
        info.sourceTableName = item.sourceTableName;
        info.producerVersion = item.producerVersion;
        info.state = item.state;
        info.error = item.error;
        info.diagnostics = item.diagnostics;
        replacement.insert(documentId, {std::move(info), item.document});
    }

    m_sessions[sessionId].insert(providerPluginId, std::move(replacement));
    return {JsonPublishStatus::Success, {}};
}

QList<plugin::JsonDocumentInfo> JsonDocumentStore::listDocuments(
    quint64 sessionId,
    const QString& providerPluginId) const
{
    QList<plugin::JsonDocumentInfo> result;
    const auto sessionIt = m_sessions.constFind(sessionId);
    if (sessionIt == m_sessions.constEnd())
        return result;

    auto appendProvider = [&result](const DocumentMap& documents)
    {
        for (const StoredDocument& stored : documents)
            result.push_back(stored.info);
    };

    if (!providerPluginId.isEmpty())
    {
        const auto providerIt = sessionIt->constFind(providerPluginId);
        if (providerIt != sessionIt->constEnd())
            appendProvider(providerIt.value());
    }
    else
    {
        for (const DocumentMap& documents : *sessionIt)
            appendProvider(documents);
    }

    std::sort(result.begin(), result.end(),
        [](const plugin::JsonDocumentInfo& left,
           const plugin::JsonDocumentInfo& right)
        {
            if (left.providerPluginId != right.providerPluginId)
                return left.providerPluginId < right.providerPluginId;
            return left.documentId < right.documentId;
        });
    return result;
}

plugin::JsonDocumentPtr JsonDocumentStore::acquireDocument(
    quint64 sessionId,
    const QString& providerPluginId,
    const QString& documentId,
    plugin::JsonDocumentInfo* info) const
{
    const auto sessionIt = m_sessions.constFind(sessionId);
    if (sessionIt == m_sessions.constEnd())
        return {};
    const auto providerIt = sessionIt->constFind(providerPluginId);
    if (providerIt == sessionIt->constEnd())
        return {};
    const auto documentIt = providerIt->constFind(documentId);
    if (documentIt == providerIt->constEnd())
        return {};
    if (info)
        *info = documentIt->info;
    return documentIt->document;
}

bool JsonDocumentStore::clearSession(quint64 sessionId)
{
    return m_sessions.remove(sessionId);
}

bool JsonDocumentStore::removeProvider(const QString& providerPluginId)
{
    bool changed = false;
    for (auto sessionIt = m_sessions.begin(); sessionIt != m_sessions.end(); )
    {
        changed = sessionIt->remove(providerPluginId) || changed;
        if (sessionIt->isEmpty())
            sessionIt = m_sessions.erase(sessionIt);
        else
            ++sessionIt;
    }
    return changed;
}

} // namespace viewer
