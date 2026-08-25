#include "code_plugin/PluginHost.h"
#include "code_plugin/CoreExpressionDataService.h"

#include "DockManager.h"
#include "code_viewer/base/trace_logger.h"
#include "code_viewer/datamgr/data_manager.h"
#include "code_viewer/viewer/viewer_lib.h"

#include <QAction>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <mutex>
#include <utility>

using namespace viewer::plugin;

namespace
{

class DataSnapshotImpl final : public IDataSnapshot
{
public:
    DataSnapshotImpl(quint64 sessionId,
                     QStringList names,
                     std::vector<std::shared_ptr<const viewer::Column>> columns)
        : m_sessionId(sessionId)
        , m_names(std::move(names))
        , m_columns(std::move(columns))
    {
        for (int i = 0; i < m_names.size(); ++i)
            m_indices.insert(m_names[i], i);
    }

    quint64 sessionId() const noexcept override { return m_sessionId; }
    qsizetype rowCount() const noexcept override
    {
        return m_columns.empty() ? 0 : static_cast<qsizetype>(m_columns.front()->size());
    }
    QStringList columnNames() const override { return m_names; }
    bool contains(const QString& name) const override { return m_indices.contains(name); }
    ColumnView column(const QString& name) const override
    {
        const auto it = m_indices.constFind(name);
        if (it == m_indices.constEnd())
            return {};
        const auto& column = m_columns[static_cast<size_t>(it.value())];
        return {column->data(), static_cast<qsizetype>(column->size())};
    }

private:
    quint64 m_sessionId = 0;
    QStringList m_names;
    QHash<QString, int> m_indices;
    std::vector<std::shared_ptr<const viewer::Column>> m_columns;
};

QString normalizedEntryPath(QString path)
{
    path.replace('\\', '/');
    while (path.startsWith('/'))
        path.remove(0, 1);
    return path;
}

ads::DockWidgetArea toAdsDockArea(DockArea area)
{
    switch (area)
    {
    case DockArea::Left: return ads::LeftDockWidgetArea;
    case DockArea::Right: return ads::RightDockWidgetArea;
    case DockArea::Top: return ads::TopDockWidgetArea;
    case DockArea::Bottom: return ads::BottomDockWidgetArea;
    case DockArea::Center: return ads::CenterDockWidgetArea;
    }
    return ads::RightDockWidgetArea;
}

} // namespace

class DerivedColumnWriterImpl final : public IDerivedColumnWriter
{
public:
    DerivedColumnWriterImpl(PluginHost* host,
                            quint64 sessionId,
                            QString name,
                            qsizetype rowCount)
        : m_host(host)
        , m_sessionId(sessionId)
        , m_name(std::move(name))
        , m_values(static_cast<size_t>(rowCount))
    {
    }

    quint64 sessionId() const noexcept override { return m_sessionId; }
    QString columnName() const override { return m_name; }
    qsizetype size() const noexcept override { return static_cast<qsizetype>(m_values.size()); }
    double* data() noexcept override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_committed ? nullptr : m_values.data();
    }
    DataCommitResult commit() override
    {
        std::vector<double> values;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_committed)
            {
                return {DataCommitStatus::InvalidWriter, {},
                        QStringLiteral("This derived-column writer has already been committed.")};
            }
            m_committed = true;
            values = std::move(m_values);
        }
        if (!m_host)
        {
            return {DataCommitStatus::HostShuttingDown, {},
                    QStringLiteral("The Viewer plugin host is no longer available.")};
        }
        return m_host->commitDerivedColumn(m_sessionId, m_name, std::move(values));
    }

private:
    QPointer<PluginHost> m_host;
    quint64 m_sessionId = 0;
    QString m_name;
    std::vector<double> m_values;
    std::mutex m_mutex;
    bool m_committed = false;
};

class DerivedColumnBatchWriterImpl final : public IDerivedColumnBatchWriter
{
public:
    DerivedColumnBatchWriterImpl(PluginHost* host,
                                 QString providerPluginId,
                                 quint64 sessionId,
                                 QStringList names,
                                 qsizetype rowCount)
        : m_host(host)
        , m_providerPluginId(std::move(providerPluginId))
        , m_sessionId(sessionId)
        , m_names(std::move(names))
        , m_values(static_cast<size_t>(m_names.size()))
        , m_active(static_cast<size_t>(m_names.size()), 1)
    {
        for (size_t index = 0; index < m_values.size(); ++index)
        {
            m_indices.insert(m_names[static_cast<qsizetype>(index)],
                             static_cast<int>(index));
            m_values[index].resize(static_cast<size_t>(rowCount));
        }
        m_rowCount = rowCount;
    }

    quint64 sessionId() const noexcept override { return m_sessionId; }
    QString providerPluginId() const override { return m_providerPluginId; }
    QStringList columnNames() const override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        QStringList result;
        for (qsizetype index = 0; index < m_names.size(); ++index)
        {
            if (m_active[static_cast<size_t>(index)])
                result.push_back(m_names[index]);
        }
        return result;
    }
    qsizetype rowCount() const noexcept override { return m_rowCount; }
    double* data(const QString& columnName) noexcept override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_committed)
            return nullptr;
        const auto index = m_indices.constFind(columnName);
        if (index == m_indices.constEnd()
            || !m_active[static_cast<size_t>(index.value())])
        {
            return nullptr;
        }
        return m_values[static_cast<size_t>(index.value())].data();
    }
    bool discard(const QString& columnName) noexcept override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_committed)
            return false;
        const auto index = m_indices.constFind(columnName);
        if (index == m_indices.constEnd())
            return false;
        m_active[static_cast<size_t>(index.value())] = 0;
        return true;
    }
    DerivedColumnBatchCommitResult commit() override
    {
        QStringList names;
        std::vector<std::vector<double>> values;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_committed)
            {
                return {DerivedColumnBatchCommitStatus::InvalidWriter, {},
                        QStringLiteral("This derived-column batch has already been committed.")};
            }
            m_committed = true;
            for (qsizetype index = 0; index < m_names.size(); ++index)
            {
                if (!m_active[static_cast<size_t>(index)])
                    continue;
                names.push_back(m_names[index]);
                values.push_back(std::move(m_values[static_cast<size_t>(index)]));
            }
        }
        if (!m_host)
        {
            return {DerivedColumnBatchCommitStatus::HostShuttingDown, {},
                    QStringLiteral("The Viewer plugin host is no longer available.")};
        }
        return m_host->commitDerivedColumnBatch(
            m_providerPluginId, m_sessionId, std::move(names), std::move(values));
    }

private:
    QPointer<PluginHost> m_host;
    QString m_providerPluginId;
    quint64 m_sessionId = 0;
    QStringList m_names;
    QHash<QString, int> m_indices;
    std::vector<std::vector<double>> m_values;
    std::vector<unsigned char> m_active;
    qsizetype m_rowCount = 0;
    mutable std::mutex m_mutex;
    bool m_committed = false;
};

PluginHost::PluginHost(viewer::Viewer& viewer,
                       QMainWindow* mainWindow,
                       ads::CDockManager* dockManager,
                       QMenu* pluginMenu,
                       std::function<void()> rebuildDataTree,
                       std::function<void(const QStringList&,
                                          const QStringList&,
                                          const QStringList&)> refreshDataColumns,
                       QObject* parent)
    : QObject(parent)
    , m_viewer(viewer)
    , m_mainWindow(mainWindow)
    , m_dockManager(dockManager)
    , m_pluginMenu(pluginMenu)
    , m_rebuildDataTree(std::move(rebuildDataTree))
    , m_refreshDataColumns(std::move(refreshDataColumns))
{
    m_coreExpressionDataService = new CoreExpressionDataService(this, this);
    m_services.insert(
        serviceKey(QString::fromLatin1(kViewerCoreProviderId),
                   QString::fromLatin1(kExpressionDataServiceId)),
        {QString::fromLatin1(kViewerCoreProviderId),
         QString::fromLatin1(kExpressionDataServiceId),
         kExpressionDataServiceVersion,
         m_coreExpressionDataService});

    connect(&m_viewer, &viewer::Viewer::DataLoaded, this,
        [this](quint64)
        {
            const LoadSessionInfo session = m_viewer.GetLoadSessionInfo();
            const auto subscriptions = m_dataLoadedSubscriptions;
            for (const auto& record : subscriptions)
            {
                try { if (record.callback) record.callback(session); }
                catch (...) { write(record.ownerPluginId, LogLevel::Error,
                                    QStringLiteral("Unhandled exception in DataLoaded callback.")); }
            }
        });
    connect(&m_viewer, &viewer::Viewer::DataAboutToUnload, this,
        [this](quint64 sessionId)
        {
            const auto subscriptions = m_dataUnloadSubscriptions;
            for (const auto& record : subscriptions)
            {
                try { if (record.callback) record.callback(sessionId); }
                catch (...) { write(record.ownerPluginId, LogLevel::Error,
                                    QStringLiteral("Unhandled exception in DataAboutToUnload callback.")); }
            }
        });
    connect(&m_viewer, &viewer::Viewer::DataColumnAdded, this,
        [this](quint64 sessionId, const QString& name)
        {
            if (m_rebuildDataTree)
                m_rebuildDataTree();
            const auto subscriptions = m_columnAddedSubscriptions;
            for (const auto& record : subscriptions)
            {
                try { if (record.callback) record.callback(sessionId, name); }
                catch (...) { write(record.ownerPluginId, LogLevel::Error,
                                    QStringLiteral("Unhandled exception in ColumnAdded callback.")); }
            }
        });
    connect(&m_viewer, &viewer::Viewer::DataColumnsChanged, this,
        [this](quint64,
               const QStringList& oldNames,
               const QStringList& newNames,
               const QStringList& affectedNames)
        {
            if (m_refreshDataColumns)
                m_refreshDataColumns(oldNames, newNames, affectedNames);
            else if (m_rebuildDataTree)
                m_rebuildDataTree();
        });
    connect(&m_viewer, &viewer::Viewer::JsonDocumentsChanged, this,
        [this](quint64 sessionId, const QString& providerPluginId)
        {
            const auto subscriptions = m_jsonChangedSubscriptions;
            for (const auto& record : subscriptions)
            {
                try
                {
                    if (record.callback)
                        record.callback(sessionId, providerPluginId);
                }
                catch (...)
                {
                    write(record.ownerPluginId, LogLevel::Error,
                          QStringLiteral("Unhandled exception in JSON documents callback."));
                }
            }
        });
}

PluginHost::~PluginHost()
{
    beginShutdown();
}

int PluginHost::sdkVersion() const noexcept
{
    return kViewerPluginSdkVersion;
}

LoadSessionInfo PluginHost::currentSession() const
{
    if (QThread::currentThread() == thread())
        return m_viewer.GetLoadSessionInfo();

    LoadSessionInfo result;
    QMetaObject::invokeMethod(const_cast<PluginHost*>(this),
        [this, &result]() { result = m_viewer.GetLoadSessionInfo(); },
        Qt::BlockingQueuedConnection);
    return result;
}

DataSnapshotPtr PluginHost::acquireSnapshot() const
{
    if (QThread::currentThread() != thread())
    {
        DataSnapshotPtr result;
        QMetaObject::invokeMethod(const_cast<PluginHost*>(this),
            [this, &result]() { result = acquireSnapshot(); },
            Qt::BlockingQueuedConnection);
        return result;
    }

    const auto session = m_viewer.GetLoadSessionInfo();
    const auto& dm = m_viewer.GetDataManager();
    if (!session.isValid() || dm.GetColumnCount() == 0)
        return {};

    QStringList names;
    std::vector<std::shared_ptr<const viewer::Column>> columns;
    names.reserve(static_cast<qsizetype>(dm.GetColumnCount()));
    columns.reserve(dm.GetColumnCount());
    for (size_t i = 0; i < dm.GetColumnCount(); ++i)
    {
        auto column = dm.GetColumnShared(i);
        if (!column)
            return {};
        names.push_back(QString::fromUtf8(dm.GetColumnNames()[i].c_str()));
        columns.push_back(std::move(column));
    }
    return std::make_shared<DataSnapshotImpl>(session.sessionId,
                                               std::move(names),
                                               std::move(columns));
}

DerivedColumnCreateResult PluginHost::createDerivedColumn(
    quint64 sessionId,
    const QString& name,
    qsizetype rowCount)
{
    if (QThread::currentThread() != thread())
    {
        DerivedColumnCreateResult result;
        QMetaObject::invokeMethod(this,
            [this, sessionId, name, rowCount, &result]()
            { result = createDerivedColumn(sessionId, name, rowCount); },
            Qt::BlockingQueuedConnection);
        return result;
    }
    if (m_shuttingDown)
        return {{}, QStringLiteral("The Viewer plugin host is shutting down.")};
    if (!m_viewer.GetLoadSessionInfo().isValid()
        || sessionId != m_viewer.GetLoadSessionInfo().sessionId)
    {
        return {{}, QStringLiteral("The requested dataset is no longer active.")};
    }
    if (name.trimmed().isEmpty())
        return {{}, QStringLiteral("The derived column name is empty.")};
    if (m_viewer.GetDataManager().GetColumnIndex(name.toUtf8().toStdString())
        != static_cast<size_t>(-1))
    {
        return {{}, QStringLiteral("A data item with the same name already exists.")};
    }
    if (rowCount <= 0
        || static_cast<size_t>(rowCount) != m_viewer.GetDataManager().GetRowCount())
    {
        return {{}, QStringLiteral("The derived column row count does not match the loaded dataset.")};
    }
    try
    {
        return {std::make_shared<DerivedColumnWriterImpl>(this, sessionId, name, rowCount), {}};
    }
    catch (const std::bad_alloc&)
    {
        return {{}, QStringLiteral("Not enough memory to allocate the derived column.")};
    }
}

DataCommitResult PluginHost::commitDerivedColumn(
    quint64 sessionId,
    QString name,
    std::vector<double>&& values)
{
    if (QThread::currentThread() != thread())
    {
        DataCommitResult result;
        QMetaObject::invokeMethod(this,
            [this, sessionId, name = std::move(name), values = std::move(values), &result]() mutable
            { result = commitDerivedColumn(sessionId, std::move(name), std::move(values)); },
            Qt::BlockingQueuedConnection);
        return result;
    }
    if (m_shuttingDown)
        return {DataCommitStatus::HostShuttingDown, {}, QStringLiteral("Viewer is shutting down.")};
    if (!m_viewer.GetLoadSessionInfo().isValid()
        || sessionId != m_viewer.GetLoadSessionInfo().sessionId)
    {
        return {DataCommitStatus::StaleSession, {},
                QStringLiteral("The loaded dataset changed before the result was committed.")};
    }
    if (name.trimmed().isEmpty())
        return {DataCommitStatus::InvalidName, {}, QStringLiteral("The derived column name is empty.")};
    if (m_viewer.GetDataManager().GetColumnIndex(name.toUtf8().toStdString())
        != static_cast<size_t>(-1))
    {
        return {DataCommitStatus::DuplicateName, {},
                QStringLiteral("A data item with the same name already exists.")};
    }
    if (values.size() != m_viewer.GetDataManager().GetRowCount())
    {
        return {DataCommitStatus::RowCountMismatch, {},
                QStringLiteral("The derived column row count does not match the loaded dataset.")};
    }

    QString error;
    if (!m_viewer.AddDerivedColumn(sessionId, name, std::move(values), &error))
        return {DataCommitStatus::InternalError, {}, error};
    return {DataCommitStatus::Success, name, {}};
}

DerivedColumnBatchCreateResult PluginHost::createDerivedColumnBatch(
    const QString& providerPluginId,
    quint64 sessionId,
    const QStringList& columnNames,
    qsizetype rowCount)
{
    if (QThread::currentThread() != thread())
    {
        DerivedColumnBatchCreateResult result;
        QMetaObject::invokeMethod(this,
            [this, &result, providerPluginId, sessionId, columnNames, rowCount]()
            {
                result = createDerivedColumnBatch(
                    providerPluginId, sessionId, columnNames, rowCount);
            },
            Qt::BlockingQueuedConnection);
        return result;
    }
    if (m_shuttingDown)
        return {{}, QStringLiteral("The Viewer plugin host is shutting down.")};
    if (providerPluginId.isEmpty() || !isPluginLoaded(providerPluginId))
        return {{}, QStringLiteral("The derived-column provider is not loaded.")};

    const LoadSessionInfo session = m_viewer.GetLoadSessionInfo();
    if (!session.isValid() || session.sessionId != sessionId)
        return {{}, QStringLiteral("The loaded dataset changed before the batch was created.")};
    if (rowCount <= 0
        || static_cast<size_t>(rowCount) != m_viewer.GetDataManager().GetRowCount())
    {
        return {{}, QStringLiteral("The derived-column row count does not match the loaded dataset.")};
    }

    QSet<QString> seen;
    const auto& dataManager = m_viewer.GetDataManager();
    const std::string owner = providerPluginId.toUtf8().toStdString();
    for (const QString& name : columnNames)
    {
        if (name.trimmed().isEmpty() || seen.contains(name))
            return {{}, QStringLiteral("A derived-column name is empty or duplicated.")};
        seen.insert(name);
        const size_t index = dataManager.GetColumnIndex(name.toUtf8().toStdString());
        if (index == static_cast<size_t>(-1))
            continue;
        const viewer::ColumnMetadata* metadata = dataManager.GetColumnMetadata(index);
        if (!metadata || metadata->origin != viewer::ColumnOrigin::PluginDerived
            || metadata->ownerPluginId != owner)
        {
            return {{}, QStringLiteral("A data item named '%1' is owned by the loaded data or another provider.")
                            .arg(name)};
        }
    }

    try
    {
        return {std::make_shared<DerivedColumnBatchWriterImpl>(
                    this, providerPluginId, sessionId, columnNames, rowCount), {}};
    }
    catch (const std::bad_alloc&)
    {
        return {{}, QStringLiteral("Not enough memory to allocate the derived-column batch.")};
    }
}

DerivedColumnBatchCommitResult PluginHost::commitDerivedColumnBatch(
    const QString& providerPluginId,
    quint64 sessionId,
    QStringList names,
    std::vector<std::vector<double>> values)
{
    if (QThread::currentThread() != thread())
    {
        DerivedColumnBatchCommitResult result;
        QMetaObject::invokeMethod(this,
            [this, &result, providerPluginId, sessionId,
             names = std::move(names), values = std::move(values)]() mutable
            {
                result = commitDerivedColumnBatch(
                    providerPluginId, sessionId, std::move(names), std::move(values));
            },
            Qt::BlockingQueuedConnection);
        return result;
    }
    if (m_shuttingDown)
    {
        return {DerivedColumnBatchCommitStatus::HostShuttingDown, {},
                QStringLiteral("The Viewer plugin host is shutting down.")};
    }
    const LoadSessionInfo session = m_viewer.GetLoadSessionInfo();
    if (!session.isValid() || session.sessionId != sessionId)
    {
        return {DerivedColumnBatchCommitStatus::StaleSession, {},
                QStringLiteral("The loaded dataset changed before the batch was committed.")};
    }

    QString error;
    if (!m_viewer.PublishPluginDerivedColumns(
            sessionId, providerPluginId, names, std::move(values), &error))
    {
        return {DerivedColumnBatchCommitStatus::InternalError, {}, error};
    }
    return {DerivedColumnBatchCommitStatus::Success, names, {}};
}

QList<ArchiveEntryInfo> PluginHost::listCurrentZipEntries(
    quint64 sessionId,
    QString* error) const
{
    const auto session = currentSession();
    if (!session.isValid() || session.sessionId != sessionId || !session.isZip())
    {
        if (error) *error = QStringLiteral("The requested ZIP load session is not active.");
        return {};
    }

    std::vector<viewer::logparse::ziplog::ZipEntryInfo> entries;
    std::string archiveError;
    if (!viewer::Viewer::ReadZipCatalog(
            std::filesystem::path(session.sourcePath.toStdWString()), entries, archiveError))
    {
        if (error) *error = QString::fromUtf8(archiveError.c_str());
        return {};
    }

    QList<ArchiveEntryInfo> result;
    result.reserve(static_cast<qsizetype>(entries.size()));
    for (const auto& entry : entries)
    {
        result.push_back({entry.index,
                          QString::fromUtf8(entry.pathUtf8.c_str()),
                          entry.uncompressedSize,
                          entry.compressedSize,
                          entry.canRead()});
    }
    if (error) error->clear();
    return result;
}

ArchiveReadResult PluginHost::readCurrentZipEntry(
    quint64 sessionId,
    const QString& entryPath) const
{
    const auto session = currentSession();
    if (!session.isValid() || session.sessionId != sessionId || !session.isZip())
        return {false, QStringLiteral("The requested ZIP load session is not active."), {}};

    const QString requested = normalizedEntryPath(entryPath);
    const viewer::logparse::ziplog::ZipEntryInfo* selected = nullptr;
    std::vector<viewer::logparse::ziplog::ZipEntryInfo> entries;
    std::string catalogError;
    if (!viewer::Viewer::ReadZipCatalog(
            std::filesystem::path(session.sourcePath.toStdWString()), entries, catalogError))
    {
        return {false, QString::fromUtf8(catalogError.c_str()), {}};
    }
    for (const auto& entry : entries)
    {
        if (QString::fromUtf8(entry.pathUtf8.c_str()) == requested)
        {
            selected = &entry;
            break;
        }
    }
    if (!selected)
        return {false, QStringLiteral("The requested ZIP entry does not exist: %1").arg(requested), {}};
    if (!selected->canRead())
        return {false, QStringLiteral("The requested ZIP entry is not a readable regular file."), {}};
    auto bytes = QSharedPointer<QByteArray>::create();
    std::string readError;
    if (!viewer::Viewer::ReadZipEntry(
            std::filesystem::path(session.sourcePath.toStdWString()),
            selected->index,
            *bytes,
            readError))
    {
        return {false, QString::fromUtf8(readError.c_str()), {}};
    }
    return {true, {}, bytes};
}

JsonPublishResult PluginHost::publishBatch(
    const QString& providerPluginId,
    quint64 sessionId,
    const QList<JsonDocumentPublishItem>& documents)
{
    if (QThread::currentThread() != thread())
    {
        JsonPublishResult result;
        QMetaObject::invokeMethod(this,
            [this, &result, providerPluginId, sessionId, documents]()
            {
                result = publishBatch(providerPluginId, sessionId, documents);
            },
            Qt::BlockingQueuedConnection);
        return result;
    }
    if (m_shuttingDown)
    {
        return {JsonPublishStatus::HostShuttingDown,
                QStringLiteral("The Viewer plugin host is shutting down.")};
    }
    return m_viewer.PublishJsonDocuments(providerPluginId, sessionId, documents);
}

QList<JsonDocumentInfo> PluginHost::listDocuments(
    quint64 sessionId,
    const QString& providerPluginId) const
{
    if (QThread::currentThread() != thread())
    {
        QList<JsonDocumentInfo> result;
        QMetaObject::invokeMethod(const_cast<PluginHost*>(this),
            [this, &result, sessionId, providerPluginId]()
            {
                result = listDocuments(sessionId, providerPluginId);
            },
            Qt::BlockingQueuedConnection);
        return result;
    }
    return m_viewer.ListJsonDocuments(sessionId, providerPluginId);
}

JsonDocumentPtr PluginHost::acquireDocument(
    quint64 sessionId,
    const QString& providerPluginId,
    const QString& documentId,
    JsonDocumentInfo* info) const
{
    if (QThread::currentThread() != thread())
    {
        JsonDocumentPtr result;
        JsonDocumentInfo resultInfo;
        QMetaObject::invokeMethod(const_cast<PluginHost*>(this),
            [this, &result, &resultInfo, sessionId, providerPluginId, documentId]()
            {
                result = acquireDocument(
                    sessionId, providerPluginId, documentId, &resultInfo);
            },
            Qt::BlockingQueuedConnection);
        if (info)
            *info = resultInfo;
        return result;
    }
    return m_viewer.AcquireJsonDocument(
        sessionId, providerPluginId, documentId, info);
}

JsonSubscriptionId PluginHost::subscribeDocumentsChanged(
    const QString& ownerPluginId,
    JsonDocumentsChangedCallback callback)
{
    if (m_shuttingDown || ownerPluginId.isEmpty() || !callback
        || QThread::currentThread() != thread())
    {
        return 0;
    }
    const quint64 id = nextHandle();
    m_jsonChangedSubscriptions.insert(id, {ownerPluginId, std::move(callback)});
    return id;
}

void PluginHost::unsubscribeDocumentsChanged(JsonSubscriptionId subscription)
{
    m_jsonChangedSubscriptions.remove(subscription);
}

SubscriptionId PluginHost::subscribeDataLoaded(
    const QString& ownerPluginId,
    DataLoadedCallback callback)
{
    const quint64 id = nextHandle();
    m_dataLoadedSubscriptions.insert(id, {ownerPluginId, std::move(callback)});
    return id;
}

SubscriptionId PluginHost::subscribeDataAboutToUnload(
    const QString& ownerPluginId,
    DataAboutToUnloadCallback callback)
{
    const quint64 id = nextHandle();
    m_dataUnloadSubscriptions.insert(id, {ownerPluginId, std::move(callback)});
    return id;
}

SubscriptionId PluginHost::subscribeColumnAdded(
    const QString& ownerPluginId,
    ColumnAddedCallback callback)
{
    const quint64 id = nextHandle();
    m_columnAddedSubscriptions.insert(id, {ownerPluginId, std::move(callback)});
    return id;
}

void PluginHost::unsubscribe(SubscriptionId subscription)
{
    m_dataLoadedSubscriptions.remove(subscription);
    m_dataUnloadSubscriptions.remove(subscription);
    m_columnAddedSubscriptions.remove(subscription);
}

bool PluginHost::isPluginLoaded(const QString& pluginId) const
{
    const PluginState state = pluginState(pluginId);
    return state == PluginState::Loaded || state == PluginState::Started;
}

PluginState PluginHost::pluginState(const QString& pluginId) const
{
    return m_pluginStates.value(pluginId, PluginState::NotFound);
}

bool PluginHost::registerService(const QString& providerPluginId,
                                 const QString& serviceId,
                                 int version,
                                 QObject* service)
{
    if (providerPluginId.isEmpty() || serviceId.isEmpty() || version <= 0 || !service)
        return false;
    if (!isPluginLoaded(providerPluginId))
        return false;
    const QString key = serviceKey(providerPluginId, serviceId);
    if (m_services.contains(key))
        return false;
    m_services.insert(key, {providerPluginId, serviceId, version, service});
    return true;
}

void PluginHost::unregisterService(const QString& providerPluginId,
                                   const QString& serviceId)
{
    m_services.remove(serviceKey(providerPluginId, serviceId));
}

QObject* PluginHost::queryService(const QString& providerPluginId,
                                  const QString& serviceId,
                                  int minimumVersion,
                                  int* actualVersion) const
{
    const auto it = m_services.constFind(serviceKey(providerPluginId, serviceId));
    if (it == m_services.constEnd() || !it->service || it->version < minimumVersion)
        return nullptr;
    if (actualVersion)
        *actualVersion = it->version;
    return it->service.data();
}

PluginActionHandle PluginHost::addPluginAction(
    const QString& ownerPluginId,
    const QString& text,
    std::function<void()> callback)
{
    if (m_shuttingDown || !m_pluginMenu || text.isEmpty() || !callback)
        return 0;
    const quint64 handle = nextHandle();
    auto* action = m_pluginMenu->addAction(text);
    connect(action, &QAction::triggered, this,
        [this, ownerPluginId, callback = std::move(callback)]()
        {
            try { callback(); }
            catch (...) { write(ownerPluginId, LogLevel::Error,
                                QStringLiteral("Unhandled exception in plugin action.")); }
        });
    m_actions.insert(handle, {ownerPluginId, action});
    return handle;
}

PluginMenuHandle PluginHost::addPluginMenu(
    const QString& ownerPluginId,
    const QString& rootTitle,
    const QList<PluginMenuItemSpec>& items,
    PluginMenuCallback callback)
{
    if (m_shuttingDown || !m_pluginMenu || ownerPluginId.isEmpty()
        || rootTitle.isEmpty() || !callback || QThread::currentThread() != thread())
    {
        return 0;
    }

    const quint64 handle = nextHandle();
    auto* root = m_pluginMenu->addMenu(rootTitle);
    MenuRecord record;
    record.ownerPluginId = ownerPluginId;
    record.root = root;

    QHash<QString, QMenu*> menus;
    QSet<QString> itemIds;
    for (const PluginMenuItemSpec& item : items)
    {
        const QString itemId = item.id.trimmed();
        const QString parentId = item.parentId.trimmed();
        if (itemId.isEmpty() || itemIds.contains(itemId))
        {
            delete root;
            return 0;
        }

        QMenu* parent = root;
        if (!parentId.isEmpty())
        {
            parent = menus.value(parentId, nullptr);
            if (!parent)
            {
                delete root;
                return 0;
            }
        }

        QAction* action = nullptr;
        switch (item.type)
        {
        case PluginMenuItemType::Menu:
        {
            if (item.text.isEmpty())
            {
                delete root;
                return 0;
            }
            QMenu* menu = parent->addMenu(item.text);
            menus.insert(itemId, menu);
            action = menu->menuAction();
            break;
        }
        case PluginMenuItemType::Separator:
            action = parent->addSeparator();
            break;
        case PluginMenuItemType::Action:
        case PluginMenuItemType::CheckableAction:
            if (item.text.isEmpty())
            {
                delete root;
                return 0;
            }
            action = parent->addAction(item.text);
            action->setCheckable(item.type == PluginMenuItemType::CheckableAction);
            action->setChecked(item.checked);
            connect(action, &QAction::triggered, this,
                [this, ownerPluginId, itemId, callback](bool checked)
                {
                    try { callback(itemId, checked); }
                    catch (...)
                    {
                        write(ownerPluginId, LogLevel::Error,
                              QStringLiteral("Unhandled exception in plugin menu action."));
                    }
                });
            break;
        }

        action->setEnabled(item.enabled);
        action->setVisible(item.visible);
        record.actions.insert(itemId, action);
        itemIds.insert(itemId);
    }

    m_menus.insert(handle, std::move(record));
    return handle;
}

bool PluginHost::setPluginMenuItemEnabled(
    PluginMenuHandle menu, const QString& itemId, bool enabled)
{
    const auto menuIt = m_menus.find(menu);
    if (menuIt == m_menus.end())
        return false;
    const auto actionIt = menuIt->actions.find(itemId);
    if (actionIt == menuIt->actions.end() || !actionIt.value())
        return false;
    actionIt.value()->setEnabled(enabled);
    return true;
}

bool PluginHost::setPluginMenuItemChecked(
    PluginMenuHandle menu, const QString& itemId, bool checked)
{
    const auto menuIt = m_menus.find(menu);
    if (menuIt == m_menus.end())
        return false;
    const auto actionIt = menuIt->actions.find(itemId);
    if (actionIt == menuIt->actions.end() || !actionIt.value()
        || !actionIt.value()->isCheckable())
    {
        return false;
    }
    actionIt.value()->setChecked(checked);
    return true;
}

bool PluginHost::setPluginMenuItemVisible(
    PluginMenuHandle menu, const QString& itemId, bool visible)
{
    const auto menuIt = m_menus.find(menu);
    if (menuIt == m_menus.end())
        return false;
    const auto actionIt = menuIt->actions.find(itemId);
    if (actionIt == menuIt->actions.end() || !actionIt.value())
        return false;
    actionIt.value()->setVisible(visible);
    return true;
}

PluginDockHandle PluginHost::createDock(
    const QString& ownerPluginId,
    const QString& dockId,
    const QString& title,
    QWidget* content,
    DockArea preferredArea)
{
    if (m_shuttingDown || !m_dockManager || !content
        || ownerPluginId.isEmpty() || dockId.isEmpty())
    {
        return 0;
    }
    const quint64 handle = nextHandle();
    auto* dock = new ads::CDockWidget(title);
    dock->setObjectName(QStringLiteral("plugin.%1.%2").arg(ownerPluginId, dockId));
    dock->setWidget(content);
    m_dockManager->addDockWidget(toAdsDockArea(preferredArea), dock);
    m_docks.insert(handle, {ownerPluginId, dock});
    connect(dock, &QObject::destroyed, this,
        [this, handle]() { m_docks.remove(handle); });
    return handle;
}

bool PluginHost::showDock(PluginDockHandle dockHandle)
{
    const auto it = m_docks.find(dockHandle);
    if (it == m_docks.end() || !it->dock)
        return false;
    it->dock->toggleView(true);
    if (m_dockManager)
        m_dockManager->setDockWidgetFocused(it->dock);
    return true;
}

bool PluginHost::closeDock(PluginDockHandle dockHandle)
{
    const auto it = m_docks.find(dockHandle);
    if (it == m_docks.end() || !it->dock)
        return false;
    it->dock->toggleView(false);
    return true;
}

void PluginHost::showError(const QString& title, const QString& message)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, title, message]() { showError(title, message); });
        return;
    }
    if (!m_shuttingDown && m_mainWindow)
        QMessageBox::critical(m_mainWindow, title, message);
}

void PluginHost::showInformation(const QString& title, const QString& message)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, title, message]() { showInformation(title, message); });
        return;
    }
    if (!m_shuttingDown && m_mainWindow)
        QMessageBox::information(m_mainWindow, title, message);
}

void PluginHost::write(const QString& ownerPluginId,
                       LogLevel level,
                       const QString& message)
{
    const char* levelText = "info";
    switch (level)
    {
    case LogLevel::Debug: levelText = "debug"; break;
    case LogLevel::Info: levelText = "info"; break;
    case LogLevel::Warning: levelText = "warning"; break;
    case LogLevel::Error: levelText = "error"; break;
    }
    viewer::trace::write(viewer::trace::Category::Operation,
        QStringLiteral("plugin[%1] %2: %3")
            .arg(ownerPluginId, QString::fromLatin1(levelText), message));
}

void PluginHost::setPluginState(const QString& pluginId, PluginState state)
{
    if (!pluginId.isEmpty())
        m_pluginStates.insert(pluginId, state);
}

void PluginHost::removeOwnedResources(const QString& pluginId)
{
    for (auto it = m_services.begin(); it != m_services.end(); )
        it = it->providerPluginId == pluginId ? m_services.erase(it) : ++it;
    for (auto it = m_dataLoadedSubscriptions.begin(); it != m_dataLoadedSubscriptions.end(); )
        it = it->ownerPluginId == pluginId ? m_dataLoadedSubscriptions.erase(it) : ++it;
    for (auto it = m_dataUnloadSubscriptions.begin(); it != m_dataUnloadSubscriptions.end(); )
        it = it->ownerPluginId == pluginId ? m_dataUnloadSubscriptions.erase(it) : ++it;
    for (auto it = m_columnAddedSubscriptions.begin(); it != m_columnAddedSubscriptions.end(); )
        it = it->ownerPluginId == pluginId ? m_columnAddedSubscriptions.erase(it) : ++it;
    for (auto it = m_jsonChangedSubscriptions.begin();
         it != m_jsonChangedSubscriptions.end(); )
    {
        it = it->ownerPluginId == pluginId
            ? m_jsonChangedSubscriptions.erase(it) : ++it;
    }

    m_viewer.RemoveJsonDocuments(pluginId);

    QList<quint64> actionHandles;
    for (auto it = m_actions.constBegin(); it != m_actions.constEnd(); ++it)
        if (it->ownerPluginId == pluginId) actionHandles.push_back(it.key());
    for (quint64 handle : actionHandles)
    {
        auto record = m_actions.take(handle);
        if (record.action) delete record.action;
    }

    QList<quint64> menuHandles;
    for (auto it = m_menus.constBegin(); it != m_menus.constEnd(); ++it)
        if (it->ownerPluginId == pluginId) menuHandles.push_back(it.key());
    for (quint64 handle : menuHandles)
    {
        auto record = m_menus.take(handle);
        if (record.root)
            delete record.root;
    }

    QList<quint64> dockHandles;
    for (auto it = m_docks.constBegin(); it != m_docks.constEnd(); ++it)
        if (it->ownerPluginId == pluginId) dockHandles.push_back(it.key());
    for (quint64 handle : dockHandles)
    {
        auto record = m_docks.take(handle);
        if (record.dock)
        {
            if (m_dockManager)
                m_dockManager->removeDockWidget(record.dock);
            delete record.dock;
        }
    }
}

void PluginHost::beginShutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    disconnect(&m_viewer, nullptr, this, nullptr);
}

QString PluginHost::serviceKey(const QString& providerPluginId,
                               const QString& serviceId) const
{
    return providerPluginId + QChar(0x1f) + serviceId;
}

quint64 PluginHost::nextHandle()
{
    if (m_nextHandle == 0)
        m_nextHandle = 1;
    return m_nextHandle++;
}
