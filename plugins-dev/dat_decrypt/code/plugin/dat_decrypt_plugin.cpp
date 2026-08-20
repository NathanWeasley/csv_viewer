#include "dat_decrypt_plugin.h"

#include "dat_json_viewer.h"
#include "dat_converter.h"
#include "qt_json_adapter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QSaveFile>
#include <QSettings>

#include <limits>

using namespace viewer::plugin;

namespace
{

constexpr quint64 kMaximumDatSize = 128ULL * 1024ULL * 1024ULL;

QString settingsPath()
{
    const QString directory = QCoreApplication::applicationDirPath()
        + QStringLiteral("/user/plugins");
    QDir().mkpath(directory);
    return directory + QStringLiteral("/dat_decrypt.ini");
}

QString normalizedDatName(const QString& entryPath)
{
    QString name = QFileInfo(entryPath).fileName();
    if (name.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive))
        name.chop(4);
    return name;
}

qint64 safeIndex(std::size_t value)
{
    if (value == datconv::DatConverter::npos
        || value > static_cast<std::size_t>(std::numeric_limits<qint64>::max()))
    {
        return -1;
    }
    return static_cast<qint64>(value);
}

JsonDiagnostic toJsonDiagnostic(const datconv::DatConverter::Diagnostic& source)
{
    JsonDiagnostic result;
    result.severity = source.severity == datconv::DatConverter::DiagnosticSeverity::Error
        ? JsonDiagnosticSeverity::Error : JsonDiagnosticSeverity::Warning;
    result.code = datconv::qt::diagnosticCodeName(source.code);
    result.message = QString::fromUtf8(source.message);
    result.path = QString::fromUtf8(source.path);
    result.recordIndex = safeIndex(source.recordIndex);
    return result;
}

QString stateText(JsonDocumentState state)
{
    switch (state)
    {
    case JsonDocumentState::Ready: return QStringLiteral("OK");
    case JsonDocumentState::Missing: return QStringLiteral("MISSING");
    case JsonDocumentState::Invalid: return QStringLiteral("INVALID");
    }
    return QStringLiteral("UNKNOWN");
}

QString exportFileName(const QString& documentId)
{
    if (documentId == QStringLiteral("robot.capa"))
        return QStringLiteral("ROBOT_CAPA.json");
    if (documentId == QStringLiteral("robot.calib"))
        return QStringLiteral("ROBOT_CALIB.json");
    if (documentId == QStringLiteral("robot.config"))
        return QStringLiteral("ROBOT_CONFIG.json");
    return documentId + QStringLiteral(".json");
}

PluginMenuItemSpec menuItem(
    const QString& id,
    const QString& parentId,
    PluginMenuItemType type,
    const QString& text,
    bool enabled = true,
    bool checked = false)
{
    PluginMenuItemSpec item;
    item.id = id;
    item.parentId = parentId;
    item.type = type;
    item.text = text;
    item.enabled = enabled;
    item.checked = checked;
    return item;
}

} // namespace

QString DatDecryptPlugin::id() const
{
    return QStringLiteral("dat_decrypt");
}

QString DatDecryptPlugin::name() const
{
    return QStringLiteral("dat_decrypt");
}

QString DatDecryptPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

bool DatDecryptPlugin::initialize(IViewerHost* host)
{
    if (!host || host->sdkVersion() != kViewerPluginSdkVersion
        || !host->data() || !host->archive() || !host->events() || !host->jsonDocuments()
        || !host->ui() || !host->log())
    {
        return false;
    }

    m_host = host;
    m_workers.setMaxThreadCount(1);
    m_workers.setExpiryTimeout(-1);
    loadSettings();
    createMenu();
    if (!m_menu)
        return false;

    m_dataLoadedSubscription = m_host->events()->subscribeDataLoaded(
        id(), [this](const LoadSessionInfo& session) { handleDataLoaded(session); });
    m_dataUnloadSubscription = m_host->events()->subscribeDataAboutToUnload(
        id(), [this](quint64 sessionId) { handleDataAboutToUnload(sessionId); });
    m_jsonChangedSubscription = m_host->jsonDocuments()->subscribeDocumentsChanged(
        id(), [this](quint64 sessionId, const QString& providerPluginId)
        {
            if (sessionId == m_currentSessionId
                && (providerPluginId.isEmpty() || providerPluginId == id()))
            {
                refreshMenuState();
                if (m_jsonViewer)
                    refreshJsonViewer();
            }
        });

    if (!m_dataLoadedSubscription || !m_dataUnloadSubscription
        || !m_jsonChangedSubscription)
    {
        return false;
    }

    const LoadSessionInfo session = m_host->data()->currentSession();
    if (session.isValid())
        handleDataLoaded(session);
    else
        refreshMenuState();
    log(LogLevel::Info, QStringLiteral("dat_decrypt initialized."));
    return true;
}

void DatDecryptPlugin::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    ++m_generation;
    m_workers.clear();
    m_workers.waitForDone();
    QCoreApplication::removePostedEvents(this);

    if (m_host)
    {
        if (m_dataLoadedSubscription)
            m_host->events()->unsubscribe(m_dataLoadedSubscription);
        if (m_dataUnloadSubscription)
            m_host->events()->unsubscribe(m_dataUnloadSubscription);
        if (m_jsonChangedSubscription)
            m_host->jsonDocuments()->unsubscribeDocumentsChanged(m_jsonChangedSubscription);
    }
    m_dataLoadedSubscription = 0;
    m_dataUnloadSubscription = 0;
    m_jsonChangedSubscription = 0;
    m_host = nullptr;
    m_menu = 0;
    m_jsonDock = 0;
    m_jsonViewer = nullptr;
}

void DatDecryptPlugin::createMenu()
{
    QList<PluginMenuItemSpec> items;
    items.push_back(menuItem(QStringLiteral("convert"), {}, PluginMenuItemType::Action,
                             QString::fromUtf8(u8"转换当前 ZIP"), false));
    items.push_back(menuItem(QStringLiteral("view"), {}, PluginMenuItemType::Action,
                             QString::fromUtf8(u8"查看 JSON…"), false));
    items.push_back(menuItem(QStringLiteral("export"), {}, PluginMenuItemType::Action,
                             QString::fromUtf8(u8"导出 JSON…"), false));
    items.push_back(menuItem(QStringLiteral("validate"), {}, PluginMenuItemType::Action,
                             QString::fromUtf8(u8"校验 JSON…"), false));
    items.push_back(menuItem(QStringLiteral("separator"), {}, PluginMenuItemType::Separator, {}));
    items.push_back(menuItem(QStringLiteral("settings"), {}, PluginMenuItemType::Menu,
                             QString::fromUtf8(u8"设置")));
    items.push_back(menuItem(QStringLiteral("auto"), QStringLiteral("settings"),
                             PluginMenuItemType::CheckableAction,
                             QString::fromUtf8(u8"自动转换"), true, m_autoConvert));
    items.push_back(menuItem(QStringLiteral("strict"), QStringLiteral("settings"),
                             PluginMenuItemType::CheckableAction,
                             QString::fromUtf8(u8"严格 Schema 校验"), true, m_strictSchema));
    items.push_back(menuItem(QStringLiteral("unknown"), QStringLiteral("settings"),
                             PluginMenuItemType::CheckableAction,
                             QString::fromUtf8(u8"保留未知字段"), true,
                             m_preserveUnknownFields));

    m_menu = m_host->ui()->addPluginMenu(
        id(), name(), items,
        [this](const QString& itemId, bool checked)
        {
            handleMenuCommand(itemId, checked);
        });
}

void DatDecryptPlugin::handleMenuCommand(const QString& itemId, bool checked)
{
    if (itemId == QStringLiteral("convert"))
        startConversion(true);
    else if (itemId == QStringLiteral("view"))
        showJsonViewer();
    else if (itemId == QStringLiteral("export"))
        exportJson();
    else if (itemId == QStringLiteral("validate"))
        validateJson();
    else if (itemId == QStringLiteral("auto"))
    {
        m_autoConvert = checked;
        saveSettings();
    }
    else if (itemId == QStringLiteral("strict"))
    {
        m_strictSchema = checked;
        saveSettings();
    }
    else if (itemId == QStringLiteral("unknown"))
    {
        m_preserveUnknownFields = checked;
        saveSettings();
    }
}

void DatDecryptPlugin::handleDataLoaded(const LoadSessionInfo& session)
{
    ++m_generation;
    m_workers.clear();
    m_currentSessionId = session.isZip() ? session.sessionId : 0;
    m_conversionRunning = false;
    refreshMenuState();
    if (m_currentSessionId && m_autoConvert)
        startConversion(false);
}

void DatDecryptPlugin::handleDataAboutToUnload(quint64 sessionId)
{
    if (sessionId != m_currentSessionId)
        return;
    ++m_generation;
    m_workers.clear();
    m_currentSessionId = 0;
    m_conversionRunning = false;
    refreshMenuState();
}

void DatDecryptPlugin::startConversion(bool userInitiated)
{
    if (!m_host || m_shuttingDown || m_conversionRunning || !m_currentSessionId)
        return;

    const LoadSessionInfo session = m_host->data()->currentSession();
    if (!session.isZip() || session.sessionId != m_currentSessionId)
        return;

    const QList<InputItem> inputs = collectInputs(session.sessionId);
    const quint64 generation = ++m_generation;
    const bool strictSchema = m_strictSchema;
    const bool preserveUnknownFields = m_preserveUnknownFields;
    m_conversionRunning = true;
    refreshMenuState();
    log(LogLevel::Info,
        QStringLiteral("DAT conversion started for session %1.").arg(session.sessionId));

    m_workers.start(
        [this, inputs, strictSchema, preserveUnknownFields,
         sessionId = session.sessionId, generation, userInitiated]() mutable
        {
            ConversionBatch batch = convertInputs(
                inputs, strictSchema, preserveUnknownFields, generation);
            if (m_generation.load() != generation)
                return;
            QMetaObject::invokeMethod(
                this,
                [this, sessionId, generation, userInitiated,
                 batch = std::move(batch)]() mutable
                {
                    finishConversion(
                        sessionId, generation, userInitiated, std::move(batch));
                },
                Qt::QueuedConnection);
        });
}

QList<DatDecryptPlugin::InputItem> DatDecryptPlugin::collectInputs(quint64 sessionId) const
{
    QList<InputItem> inputs;
    InputItem capa;
    capa.archiveName = QStringLiteral("ROBOT_CAPA");
    capa.expectedTable = QStringLiteral("ROBOT_CAPA");
    capa.documentId = QStringLiteral("robot.capa");
    capa.displayName = QStringLiteral("ROBOT_CAPA");
    inputs.push_back(capa);

    InputItem calib;
    calib.archiveName = QStringLiteral("ROBOT_CALIB");
    calib.expectedTable = QStringLiteral("RIU_CALIB_PARA");
    calib.documentId = QStringLiteral("robot.calib");
    calib.displayName = QStringLiteral("ROBOT_CALIB");
    inputs.push_back(calib);

    InputItem config;
    config.archiveName = QStringLiteral("ROBOT_CONFIG");
    config.expectedTable = QStringLiteral("ROBOT_CONFIG");
    config.documentId = QStringLiteral("robot.config");
    config.displayName = QStringLiteral("ROBOT_CONFIG");
    inputs.push_back(config);

    QString catalogError;
    const QList<ArchiveEntryInfo> entries =
        m_host->archive()->listCurrentZipEntries(sessionId, &catalogError);
    if (!catalogError.isEmpty())
    {
        for (InputItem& input : inputs)
        {
            input.unavailableState = JsonDocumentState::Invalid;
            input.inputError = catalogError;
        }
        return inputs;
    }

    for (InputItem& input : inputs)
    {
        QList<ArchiveEntryInfo> matches;
        for (const ArchiveEntryInfo& entry : entries)
        {
            if (normalizedDatName(entry.path).compare(
                    input.archiveName, Qt::CaseInsensitive) == 0)
            {
                matches.push_back(entry);
            }
        }

        if (matches.isEmpty())
        {
            input.inputError = QStringLiteral("DAT entry was not found in the ZIP archive.");
            continue;
        }
        if (matches.size() != 1)
        {
            input.unavailableState = JsonDocumentState::Invalid;
            input.inputError = QStringLiteral("Multiple matching DAT entries were found in the ZIP archive.");
            continue;
        }
        input.sourceEntryPath = matches.front().path;
        if (!matches.front().readable)
        {
            input.unavailableState = JsonDocumentState::Invalid;
            input.inputError = QStringLiteral("The DAT entry is not readable.");
            continue;
        }
        if (matches.front().uncompressedSize > kMaximumDatSize)
        {
            input.unavailableState = JsonDocumentState::Invalid;
            input.inputError = QStringLiteral("DAT entry exceeds the 128 MiB safety limit.");
            continue;
        }

        input.present = true;
        const ArchiveReadResult read =
            m_host->archive()->readCurrentZipEntry(sessionId, input.sourceEntryPath);
        if (!read.success || !read.data)
        {
            input.inputError = read.error.isEmpty()
                ? QStringLiteral("Unable to read the DAT entry.") : read.error;
            continue;
        }
        input.bytes = read.data;
    }
    return inputs;
}

DatDecryptPlugin::ConversionBatch DatDecryptPlugin::convertInputs(
    const QList<InputItem>& inputs,
    bool strictSchema,
    bool preserveUnknownFields,
    quint64 generation) const
{
    ConversionBatch batch;
    for (const InputItem& input : inputs)
    {
        if (m_generation.load() != generation)
            return {};

        JsonDocumentPublishItem item;
        item.documentId = input.documentId;
        item.displayName = input.displayName;
        item.sourceEntryPath = input.sourceEntryPath;
        item.sourceTableName = input.expectedTable;
        item.producerVersion = version();

        if (!input.present)
        {
            item.state = input.unavailableState;
            item.error = input.inputError;
            batch.documents.push_back(std::move(item));
            ++batch.failedCount;
            continue;
        }
        if (!input.inputError.isEmpty() || !input.bytes)
        {
            item.state = JsonDocumentState::Invalid;
            item.error = input.inputError;
            batch.documents.push_back(std::move(item));
            ++batch.failedCount;
            continue;
        }

        datconv::DatConverter converter;
        const datconv::DatConverter::ParseOptions options(
            strictSchema, false, preserveUnknownFields);
        const auto status = converter.parse(
            input.bytes->constData(), static_cast<std::size_t>(input.bytes->size()), options);
        for (const auto& diagnostic : converter.diagnostics())
            item.diagnostics.push_back(toJsonDiagnostic(diagnostic));

        if (!status)
        {
            item.state = JsonDocumentState::Invalid;
            item.error = QStringLiteral("%1: %2")
                .arg(QString::fromLatin1(datconv::errorCodeName(status.code)),
                     QString::fromUtf8(status.message));
            JsonDiagnostic diagnostic;
            diagnostic.severity = JsonDiagnosticSeverity::Error;
            diagnostic.code = QString::fromLatin1(datconv::errorCodeName(status.code));
            diagnostic.message = QString::fromUtf8(status.message);
            diagnostic.byteOffset = safeIndex(status.offset);
            diagnostic.recordIndex = safeIndex(status.recordIndex);
            item.diagnostics.push_back(std::move(diagnostic));
            batch.documents.push_back(std::move(item));
            ++batch.failedCount;
            continue;
        }

        const QString actualTable = QString::fromUtf8(
            converter.tableName().data(),
            static_cast<qsizetype>(converter.tableName().size()));
        item.sourceTableName = actualTable;
        if (actualTable != input.expectedTable)
        {
            item.state = JsonDocumentState::Invalid;
            item.error = QStringLiteral("Expected DAT table '%1', found '%2'.")
                             .arg(input.expectedTable, actualTable);
            batch.documents.push_back(std::move(item));
            ++batch.failedCount;
            continue;
        }

        item.state = JsonDocumentState::Ready;
        item.document = QSharedPointer<QJsonDocument>::create(
            datconv::qt::toQJsonDocument(converter));
        batch.documents.push_back(std::move(item));
        ++batch.readyCount;
    }
    return batch;
}

void DatDecryptPlugin::finishConversion(
    quint64 sessionId,
    quint64 generation,
    bool userInitiated,
    ConversionBatch batch)
{
    if (m_shuttingDown || generation != m_generation.load()
        || sessionId != m_currentSessionId || !m_host)
    {
        return;
    }

    m_conversionRunning = false;
    const JsonPublishResult published =
        m_host->jsonDocuments()->publishBatch(id(), sessionId, batch.documents);
    refreshMenuState();
    if (!published.success())
    {
        log(LogLevel::Error,
            QStringLiteral("Unable to publish DAT JSON results: %1").arg(published.error));
        if (userInitiated)
            m_host->ui()->showError(name(), published.error);
        return;
    }

    log(batch.failedCount == 0 ? LogLevel::Info : LogLevel::Warning,
        QStringLiteral("DAT conversion finished: ready=%1 failed=%2 session=%3.")
            .arg(batch.readyCount).arg(batch.failedCount).arg(sessionId));
    if (userInitiated)
    {
        m_host->ui()->showInformation(
            name(),
            QString::fromUtf8(u8"转换完成：%1 个成功，%2 个缺失或失败。")
                .arg(batch.readyCount).arg(batch.failedCount));
    }
}

void DatDecryptPlugin::showJsonViewer()
{
    if (!m_host || !m_currentSessionId)
        return;
    if (!m_jsonViewer)
    {
        auto* viewer = new DatJsonViewer;
        m_jsonDock = m_host->ui()->createDock(
            id(), QStringLiteral("json"), QStringLiteral("dat_decrypt JSON"),
            viewer, DockArea::Right);
        if (!m_jsonDock)
        {
            delete viewer;
            m_host->ui()->showError(name(), QString::fromUtf8(u8"无法创建 JSON 查看面板。"));
            return;
        }
        m_jsonViewer = viewer;
    }
    refreshJsonViewer();
    m_host->ui()->showDock(m_jsonDock);
}

void DatDecryptPlugin::refreshJsonViewer()
{
    if (!m_host || !m_jsonViewer || !m_currentSessionId)
        return;
    const QList<JsonDocumentInfo> documents =
        m_host->jsonDocuments()->listDocuments(m_currentSessionId, id());
    QHash<QString, JsonDocumentPtr> contents;
    for (const JsonDocumentInfo& info : documents)
    {
        if (info.isReady())
        {
            contents.insert(
                info.documentId,
                m_host->jsonDocuments()->acquireDocument(
                    m_currentSessionId, id(), info.documentId));
        }
    }
    m_jsonViewer->setDocuments(documents, contents);
}

void DatDecryptPlugin::exportJson()
{
    if (!m_host || !m_currentSessionId)
        return;
    const QString directory = QFileDialog::getExistingDirectory(
        nullptr, QString::fromUtf8(u8"选择 JSON 导出目录"));
    if (directory.isEmpty())
        return;

    const QList<JsonDocumentInfo> documents =
        m_host->jsonDocuments()->listDocuments(m_currentSessionId, id());
    int written = 0;
    QStringList errors;
    for (const JsonDocumentInfo& info : documents)
    {
        if (!info.isReady())
            continue;
        const JsonDocumentPtr document = m_host->jsonDocuments()->acquireDocument(
            m_currentSessionId, id(), info.documentId);
        if (!document)
            continue;

        const QString path = QDir(directory).filePath(exportFileName(info.documentId));
        QSaveFile file(path);
        const QByteArray json = document->toJson(QJsonDocument::Indented);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(json) != json.size()
            || !file.commit())
        {
            errors.push_back(QStringLiteral("%1: %2").arg(path, file.errorString()));
            continue;
        }
        ++written;
    }

    if (!errors.isEmpty())
    {
        m_host->ui()->showError(
            name(),
            QString::fromUtf8(u8"部分 JSON 导出失败：\n") + errors.join('\n'));
    }
    else
    {
        m_host->ui()->showInformation(
            name(), QString::fromUtf8(u8"已导出 %1 个 JSON 文件。").arg(written));
    }
}

void DatDecryptPlugin::validateJson()
{
    if (!m_host || !m_currentSessionId)
        return;
    const QList<JsonDocumentInfo> documents =
        m_host->jsonDocuments()->listDocuments(m_currentSessionId, id());
    QStringList lines;
    int diagnosticCount = 0;
    for (const JsonDocumentInfo& info : documents)
    {
        lines.push_back(QStringLiteral("%1: %2")
                            .arg(info.displayName, stateText(info.state)));
        if (!info.error.isEmpty())
            lines.push_back(QStringLiteral("  %1").arg(info.error));

        if (info.isReady())
        {
            const JsonDocumentPtr document = m_host->jsonDocuments()->acquireDocument(
                m_currentSessionId, id(), info.documentId);
            if (!document || !document->isObject())
            {
                lines.push_back(QStringLiteral("  ERROR: JSON root is not an object."));
            }
            else
            {
                const QJsonObject root = document->object();
                const QJsonObject header = root.value(QStringLiteral("header")).toObject();
                const QJsonArray records = root.value(QStringLiteral("records")).toArray();
                const int declared = header.value(QStringLiteral("recordCount")).toInt(-1);
                if (root.value(QStringLiteral("table")).toString() != info.sourceTableName)
                    lines.push_back(QStringLiteral("  ERROR: table metadata does not match."));
                if (declared != records.size())
                    lines.push_back(QStringLiteral("  ERROR: recordCount does not match records."));
            }
        }

        for (const JsonDiagnostic& diagnostic : info.diagnostics)
        {
            if (diagnosticCount++ >= 100)
                continue;
            lines.push_back(QStringLiteral("  [%1] %2")
                                .arg(diagnostic.code, diagnostic.message));
        }
    }
    if (diagnosticCount > 100)
        lines.push_back(QStringLiteral("... %1 more diagnostics omitted.")
                            .arg(diagnosticCount - 100));
    m_host->ui()->showInformation(
        QString::fromUtf8(u8"JSON 校验结果"), lines.join('\n'));
}

void DatDecryptPlugin::refreshMenuState()
{
    if (!m_host || !m_menu)
        return;
    const bool hasZip = m_currentSessionId != 0;
    const QList<JsonDocumentInfo> documents = hasZip
        ? m_host->jsonDocuments()->listDocuments(m_currentSessionId, id())
        : QList<JsonDocumentInfo>{};
    bool hasReady = false;
    for (const JsonDocumentInfo& info : documents)
        hasReady = hasReady || info.isReady();
    m_host->ui()->setPluginMenuItemEnabled(
        m_menu, QStringLiteral("convert"), hasZip && !m_conversionRunning);
    m_host->ui()->setPluginMenuItemEnabled(
        m_menu, QStringLiteral("view"), hasReady);
    m_host->ui()->setPluginMenuItemEnabled(
        m_menu, QStringLiteral("export"), hasReady);
    m_host->ui()->setPluginMenuItemEnabled(
        m_menu, QStringLiteral("validate"), !documents.isEmpty());
}

void DatDecryptPlugin::loadSettings()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    m_autoConvert = settings.value(QStringLiteral("autoConvert"), true).toBool();
    m_strictSchema = settings.value(QStringLiteral("strictSchema"), false).toBool();
    m_preserveUnknownFields =
        settings.value(QStringLiteral("preserveUnknownFields"), true).toBool();
}

void DatDecryptPlugin::saveSettings() const
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("autoConvert"), m_autoConvert);
    settings.setValue(QStringLiteral("strictSchema"), m_strictSchema);
    settings.setValue(QStringLiteral("preserveUnknownFields"), m_preserveUnknownFields);
}

void DatDecryptPlugin::log(LogLevel level, const QString& message) const
{
    if (m_host)
        m_host->log()->write(id(), level, message);
}

#include "moc_dat_decrypt_plugin.cpp"
