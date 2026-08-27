#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QWidget>

#include <functional>
#include <memory>

#include "viewer_json_sdk.h"

namespace viewer::plugin
{

inline constexpr int kViewerPluginSdkVersion = 2;
inline constexpr const char* kViewerPluginInterfaceIid =
    "com.weekendbuild.csvviewer.IViewerPlugin/2.0";
inline constexpr const char* kPluginRootDirectoryProperty =
    "viewerPluginRootDirectory";

enum class SourceType
{
    None,
    Csv,
    Zip,
    BinaryLog
};

struct LoadSessionInfo
{
    quint64 sessionId = 0;
    SourceType sourceType = SourceType::None;
    QString sourcePath;
    QString sourceFileName;

    bool isValid() const noexcept { return sessionId != 0; }
    bool isZip() const noexcept { return sourceType == SourceType::Zip; }
};

struct ArchiveEntryInfo
{
    quint64 index = 0;
    QString path;
    quint64 uncompressedSize = 0;
    quint64 compressedSize = 0;
    bool readable = false;
};

struct ArchiveReadResult
{
    bool success = false;
    QString error;
    QSharedPointer<const QByteArray> data;
};

struct ColumnView
{
    const double* data = nullptr;
    qsizetype size = 0;

    bool isValid() const noexcept { return data != nullptr && size > 0; }
};

class IDataSnapshot
{
public:
    virtual ~IDataSnapshot() = default;
    virtual quint64 sessionId() const noexcept = 0;
    virtual qsizetype rowCount() const noexcept = 0;
    virtual QStringList columnNames() const = 0;
    virtual bool contains(const QString& name) const = 0;
    virtual ColumnView column(const QString& name) const = 0;
};

using DataSnapshotPtr = std::shared_ptr<const IDataSnapshot>;

enum class DataCommitStatus
{
    Success,
    InvalidWriter,
    StaleSession,
    InvalidName,
    DuplicateName,
    RowCountMismatch,
    HostShuttingDown,
    InternalError
};

struct DataCommitResult
{
    DataCommitStatus status = DataCommitStatus::InternalError;
    QString columnName;
    QString error;

    bool success() const noexcept { return status == DataCommitStatus::Success; }
};

class IDerivedColumnWriter
{
public:
    virtual ~IDerivedColumnWriter() = default;
    virtual quint64 sessionId() const noexcept = 0;
    virtual QString columnName() const = 0;
    virtual qsizetype size() const noexcept = 0;
    virtual double* data() noexcept = 0;
    virtual DataCommitResult commit() = 0;
};

using DerivedColumnWriterPtr = std::shared_ptr<IDerivedColumnWriter>;

struct DerivedColumnCreateResult
{
    DerivedColumnWriterPtr writer;
    QString error;

    bool success() const noexcept { return static_cast<bool>(writer); }
};

class IDataService
{
public:
    virtual ~IDataService() = default;
    virtual LoadSessionInfo currentSession() const = 0;
    virtual DataSnapshotPtr acquireSnapshot() const = 0;
    virtual DerivedColumnCreateResult createDerivedColumn(
        quint64 sessionId,
        const QString& name,
        qsizetype rowCount) = 0;
};

class IArchiveService
{
public:
    virtual ~IArchiveService() = default;
    virtual QList<ArchiveEntryInfo> listCurrentZipEntries(
        quint64 sessionId,
        QString* error = nullptr) const = 0;
    virtual ArchiveReadResult readCurrentZipEntry(
        quint64 sessionId,
        const QString& entryPath) const = 0;
};

using SubscriptionId = quint64;

class IEventService
{
public:
    using DataLoadedCallback = std::function<void(const LoadSessionInfo&)>;
    using DataAboutToUnloadCallback = std::function<void(quint64)>;
    using ColumnAddedCallback = std::function<void(quint64, const QString&)>;

    virtual ~IEventService() = default;
    virtual SubscriptionId subscribeDataLoaded(
        const QString& ownerPluginId,
        DataLoadedCallback callback) = 0;
    virtual SubscriptionId subscribeDataAboutToUnload(
        const QString& ownerPluginId,
        DataAboutToUnloadCallback callback) = 0;
    virtual SubscriptionId subscribeColumnAdded(
        const QString& ownerPluginId,
        ColumnAddedCallback callback) = 0;
    virtual void unsubscribe(SubscriptionId subscription) = 0;
};

enum class PluginState
{
    NotFound,
    Discovered,
    Loaded,
    Started,
    Failed
};

class IPluginRegistry
{
public:
    virtual ~IPluginRegistry() = default;
    virtual bool isPluginLoaded(const QString& pluginId) const = 0;
    virtual PluginState pluginState(const QString& pluginId) const = 0;
    virtual bool registerService(
        const QString& providerPluginId,
        const QString& serviceId,
        int version,
        QObject* service) = 0;
    virtual void unregisterService(
        const QString& providerPluginId,
        const QString& serviceId) = 0;
    virtual QObject* queryService(
        const QString& providerPluginId,
        const QString& serviceId,
        int minimumVersion,
        int* actualVersion = nullptr) const = 0;
};

enum class DockArea
{
    Left,
    Right,
    Top,
    Bottom,
    Center
};

using PluginActionHandle = quint64;
using PluginDockHandle = quint64;
using PluginMenuHandle = quint64;
using PluginProgressHandle = quint64;

enum class PluginMenuItemType
{
    Menu,
    Action,
    CheckableAction,
    Separator
};

struct PluginMenuItemSpec
{
    QString id;
    QString parentId;
    PluginMenuItemType type = PluginMenuItemType::Action;
    QString text;
    int order = 0;
    bool enabled = true;
    bool checked = false;
    bool visible = true;
};

using PluginMenuCallback = std::function<void(const QString& itemId, bool checked)>;

class IUiService
{
public:
    virtual ~IUiService() = default;
    virtual PluginActionHandle addPluginAction(
        const QString& ownerPluginId,
        const QString& text,
        std::function<void()> callback) = 0;
    virtual PluginMenuHandle addPluginMenu(
        const QString& ownerPluginId,
        const QString& rootTitle,
        const QList<PluginMenuItemSpec>& items,
        PluginMenuCallback callback) = 0;
    virtual bool setPluginMenuItemEnabled(
        PluginMenuHandle menu, const QString& itemId, bool enabled) = 0;
    virtual bool setPluginMenuItemChecked(
        PluginMenuHandle menu, const QString& itemId, bool checked) = 0;
    virtual bool setPluginMenuItemVisible(
        PluginMenuHandle menu, const QString& itemId, bool visible) = 0;
    virtual PluginDockHandle createDock(
        const QString& ownerPluginId,
        const QString& dockId,
        const QString& title,
        QWidget* content,
        DockArea preferredArea = DockArea::Right) = 0;
    virtual bool showDock(PluginDockHandle dock) = 0;
    virtual bool closeDock(PluginDockHandle dock) = 0;
    // Reports a plugin stage that belongs to the active data-load session.
    // Progress is normalized to 0.0..1.0 and is displayed by Viewer using the
    // same status-bar progress control as the main data loader.
    virtual PluginProgressHandle beginLoadProgress(
        const QString& ownerPluginId,
        quint64 sessionId,
        const QString& title) = 0;
    virtual bool reportLoadProgress(
        PluginProgressHandle progress,
        float value,
        const QString& detail = {}) = 0;
    virtual void finishLoadProgress(PluginProgressHandle progress) = 0;
    virtual void showError(const QString& title, const QString& message) = 0;
    virtual void showInformation(const QString& title, const QString& message) = 0;
};

enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error
};

class ILogService
{
public:
    virtual ~ILogService() = default;
    virtual void write(
        const QString& ownerPluginId,
        LogLevel level,
        const QString& message) = 0;
};

class IViewerHost
{
public:
    virtual ~IViewerHost() = default;
    virtual int sdkVersion() const noexcept = 0;
    virtual IDataService* data() noexcept = 0;
    virtual IArchiveService* archive() noexcept = 0;
    virtual IJsonDocumentService* jsonDocuments() noexcept = 0;
    virtual IEventService* events() noexcept = 0;
    virtual IPluginRegistry* plugins() noexcept = 0;
    virtual IUiService* ui() noexcept = 0;
    virtual ILogService* log() noexcept = 0;
};

class IViewerPlugin
{
public:
    virtual ~IViewerPlugin() = default;
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual bool initialize(IViewerHost* host) = 0;
    virtual void shutdown() = 0;
};

} // namespace viewer::plugin

#define VIEWER_PLUGIN_INTERFACE_IID "com.weekendbuild.csvviewer.IViewerPlugin/2.0"
Q_DECLARE_INTERFACE(viewer::plugin::IViewerPlugin, VIEWER_PLUGIN_INTERFACE_IID)

