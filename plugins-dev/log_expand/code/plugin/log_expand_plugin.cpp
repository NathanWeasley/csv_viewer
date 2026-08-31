#include "log_expand_plugin.h"

#include "expansion_dependency.h"
#include "log_expand_dialogs.h"
#include "mapping_engine.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

using namespace viewer::plugin;

namespace
{

const QString kDatDecryptPluginId = QStringLiteral("dat_decrypt");

struct ExpansionCandidate
{
    ExpansionDefinition definition;
    QStringList dependencies;
};

PluginMenuItemSpec menuItem(const QString& id,
                            PluginMenuItemType type,
                            const char* text,
                            int order)
{
    PluginMenuItemSpec item;
    item.id = id;
    item.type = type;
    item.text = QString::fromUtf8(text);
    item.order = order;
    return item;
}

} // namespace

QString LogExpandPlugin::id() const
{
    return QStringLiteral("log_expand");
}

QString LogExpandPlugin::name() const
{
    return QStringLiteral("log_expand");
}

QString LogExpandPlugin::version() const
{
    return QStringLiteral("1.1.0");
}

bool LogExpandPlugin::initialize(IViewerHost* host)
{
    if (!host || host->sdkVersion() != kViewerPluginSdkVersion
        || !host->data() || !host->events() || !host->jsonDocuments()
        || !host->plugins() || !host->ui() || !host->log())
    {
        return false;
    }
    if (host->plugins()->pluginState(kDatDecryptPluginId) != PluginState::Started)
    {
        host->log()->write(id(), LogLevel::Error,
            QStringLiteral("Required plugin 'dat_decrypt' is not started."));
        return false;
    }

    QObject* serviceObject = host->plugins()->queryService(
        QString::fromLatin1(kViewerCoreProviderId),
        QString::fromLatin1(kExpressionDataServiceId),
        kExpressionDataServiceVersion);
    m_expressionData = qobject_cast<IExpressionDataService*>(serviceObject);
    if (!m_expressionData)
    {
        host->log()->write(id(), LogLevel::Error,
            QStringLiteral("Viewer core expression-data service v1 is unavailable."));
        return false;
    }

    m_host = host;
    m_shuttingDown = false;
    m_workerPool.setMaxThreadCount(1);
    m_workerPool.setExpiryTimeout(-1);

    QString rootDirectory = property(kPluginRootDirectoryProperty).toString();
    if (rootDirectory.isEmpty())
        rootDirectory = QCoreApplication::applicationDirPath();
    m_mappingPath = QDir(rootDirectory).absoluteFilePath(
        QStringLiteral("data/log_expand_mapping.json"));
    m_expressionsPath = QDir(rootDirectory).absoluteFilePath(
        QStringLiteral("data/log_expand_expressions.json"));

    loadExpansionDefinitions();
    createMenu();
    createToolbarButtons();
    m_dataLoadedSubscription = m_host->events()->subscribeDataLoaded(
        id(), [this](const LoadSessionInfo& session) { handleDataLoaded(session); });
    m_dataUnloadSubscription = m_host->events()->subscribeDataAboutToUnload(
        id(), [this](quint64 sessionId) { handleDataAboutToUnload(sessionId); });
    m_jsonChangedSubscription = m_host->jsonDocuments()->subscribeDocumentsChanged(
        id(), [this](quint64 sessionId, const QString& providerPluginId)
        {
            handleJsonDocumentsChanged(sessionId, providerPluginId);
        });

    if (!m_menu || !m_dataLoadedSubscription || !m_dataUnloadSubscription
        || !m_jsonChangedSubscription)
    {
        shutdown();
        return false;
    }

    const LoadSessionInfo session = m_host->data()->currentSession();
    m_currentSessionId = session.isValid() ? session.sessionId : 0;
    if (m_currentSessionId && refreshMappings(false))
        scheduleRecompute(QStringLiteral("plugin initialization"));
    refreshMenuState();
    log(LogLevel::Info, QStringLiteral("log_expand initialized."));
    return true;
}

void LogExpandPlugin::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    ++m_generation;
    m_workerPool.clear();
    m_workerPool.waitForDone();
    if (m_toolbarService)
    {
        if (m_mappedVariablesToolbarButton)
            m_toolbarService->removeButton(m_mappedVariablesToolbarButton);
        if (m_editExpansionsToolbarButton)
            m_toolbarService->removeButton(m_editExpansionsToolbarButton);
    }
    m_mappedVariablesToolbarButton = 0;
    m_editExpansionsToolbarButton = 0;
    m_toolbarService = nullptr;
    if (m_host)
    {
        if (m_progress)
            m_host->ui()->finishLoadProgress(m_progress);
        if (m_dataLoadedSubscription)
            m_host->events()->unsubscribe(m_dataLoadedSubscription);
        if (m_dataUnloadSubscription)
            m_host->events()->unsubscribe(m_dataUnloadSubscription);
        if (m_jsonChangedSubscription)
            m_host->jsonDocuments()->unsubscribeDocumentsChanged(
                m_jsonChangedSubscription);
    }
    m_dataLoadedSubscription = 0;
    m_dataUnloadSubscription = 0;
    m_jsonChangedSubscription = 0;
    m_progress = 0;
    m_workerPool.clear();
    m_mappedVariables.clear();
    m_publishedNames.clear();
    m_currentSessionId = 0;
    m_menu = 0;
    m_expressionData = nullptr;
    m_host = nullptr;
}

void LogExpandPlugin::createToolbarButtons()
{
    if (!m_host || !m_host->plugins() || !m_menu)
        return;
    QObject* toolbarObject = m_host->plugins()->queryService(
        QString::fromLatin1(kPluginToolbarProviderId),
        QString::fromLatin1(kPluginToolbarServiceId),
        kPluginToolbarServiceVersion);
    m_toolbarService = qobject_cast<IPluginToolbarService*>(toolbarObject);
    if (!m_toolbarService)
        return;

    PluginToolbarButtonSpec mappedButton;
    mappedButton.placeholderText = QStringLiteral("MAP");
    mappedButton.toolTip = QString::fromUtf8(u8"查看映射变量");
    mappedButton.order = 300;
    m_mappedVariablesToolbarButton = m_toolbarService->addMenuItemButton(
        id(), m_menu, QStringLiteral("mapped_variables"), mappedButton);

    PluginToolbarButtonSpec editButton;
    editButton.placeholderText = QStringLiteral("EXP");
    editButton.toolTip = QString::fromUtf8(u8"编辑扩充数据项");
    editButton.order = 310;
    m_editExpansionsToolbarButton = m_toolbarService->addMenuItemButton(
        id(), m_menu, QStringLiteral("edit_expansions"), editButton);
}

void LogExpandPlugin::createMenu()
{
    QList<PluginMenuItemSpec> items;
    items.push_back(menuItem(QStringLiteral("mapped_variables"),
        PluginMenuItemType::Action, u8"查看映射变量...", 10));
    items.push_back(menuItem(QStringLiteral("edit_expansions"),
        PluginMenuItemType::Action, u8"编辑扩充数据项...", 20));
    items.push_back(menuItem(QStringLiteral("recompute"),
        PluginMenuItemType::Action, u8"重新计算", 30));
    items.push_back(menuItem(QStringLiteral("separator"),
        PluginMenuItemType::Separator, "", 40));
    items.push_back(menuItem(QStringLiteral("diagnostics"),
        PluginMenuItemType::Action, u8"查看告警和状态...", 50));
    m_menu = m_host->ui()->addPluginMenu(
        id(), name(), items,
        [this](const QString& itemId, bool) { handleMenuCommand(itemId); });
}

void LogExpandPlugin::handleMenuCommand(const QString& itemId)
{
    if (itemId == QStringLiteral("mapped_variables"))
        showMappedVariables();
    else if (itemId == QStringLiteral("edit_expansions"))
        editExpansionDefinitions();
    else if (itemId == QStringLiteral("recompute"))
    {
        const bool expressionsReady = loadExpansionDefinitions();
        if (refreshMappings(false) && expressionsReady)
            scheduleRecompute(QStringLiteral("manual request"));
    }
    else if (itemId == QStringLiteral("diagnostics"))
        showDiagnostics();
}

void LogExpandPlugin::handleDataLoaded(const LoadSessionInfo& session)
{
    if (m_host && m_progress)
        m_host->ui()->finishLoadProgress(m_progress);
    m_progress = 0;
    ++m_generation;
    m_workerPool.clear();
    m_currentSessionId = session.isValid() ? session.sessionId : 0;
    m_datBatchAvailable = false;
    m_mappingConfigValid = false;
    m_mappedVariables.clear();
    m_mappingDiagnostics.clear();
    m_expansionResults.clear();
    m_publishedNames.clear();
    refreshMenuState();
}

void LogExpandPlugin::handleDataAboutToUnload(quint64 sessionId)
{
    if (sessionId != m_currentSessionId)
        return;
    if (m_host && m_progress)
        m_host->ui()->finishLoadProgress(m_progress);
    m_progress = 0;
    ++m_generation;
    m_workerPool.clear();
    m_currentSessionId = 0;
    m_datBatchAvailable = false;
    m_mappingConfigValid = false;
    m_mappedVariables.clear();
    m_mappingDiagnostics.clear();
    m_expansionResults.clear();
    m_publishedNames.clear();
    refreshMenuState();
}

void LogExpandPlugin::handleJsonDocumentsChanged(
    quint64 sessionId, const QString& providerPluginId)
{
    if (sessionId != m_currentSessionId
        || providerPluginId != kDatDecryptPluginId)
    {
        return;
    }
    refreshMappings(true);
}

bool LogExpandPlugin::loadExpansionDefinitions()
{
    m_expressionConfigValid = false;
    m_configDiagnostics.clear();
    QList<ExpansionDefinition> loaded;
    QFile file(m_expressionsPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        m_configDiagnostics.push_back({PluginDiagnosticSeverity::Error,
            QStringLiteral("configuration"), {},
            QStringLiteral("Cannot open expression file: %1").arg(m_expressionsPath)});
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        m_configDiagnostics.push_back({PluginDiagnosticSeverity::Error,
            QStringLiteral("configuration"), {},
            QStringLiteral("Expression JSON is invalid: %1").arg(parseError.errorString())});
        return false;
    }

    const QRegularExpression identifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    QSet<QString> names;
    const QJsonArray items = document.object().value(QStringLiteral("items")).toArray();
    for (qsizetype index = 0; index < items.size(); ++index)
    {
        const QJsonObject object = items[index].toObject();
        const QString itemName = object.value(QStringLiteral("name")).toString().trimmed();
        const QString expression = object.value(QStringLiteral("expression")).toString().trimmed();
        if (!identifier.match(itemName).hasMatch() || expression.isEmpty()
            || names.contains(itemName))
        {
            m_configDiagnostics.push_back({PluginDiagnosticSeverity::Error,
                QStringLiteral("configuration"), itemName,
                QStringLiteral("Expression item %1 has an invalid or duplicate name/expression.")
                    .arg(index)});
            continue;
        }
        names.insert(itemName);
        loaded.push_back({object.value(QStringLiteral("enabled")).toBool(true),
                          itemName, expression});
    }
    m_expansionDefinitions = std::move(loaded);
    m_expressionConfigValid = true;
    return true;
}

bool LogExpandPlugin::saveExpansionDefinitions(
    const QList<ExpansionDefinition>& definitions)
{
    QJsonArray items;
    for (const ExpansionDefinition& definition : definitions)
    {
        QJsonObject object;
        object.insert(QStringLiteral("enabled"), definition.enabled);
        object.insert(QStringLiteral("name"), definition.name);
        object.insert(QStringLiteral("expression"), definition.expression);
        items.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("items"), items);

    QSaveFile file(m_expressionsPath);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
        || !file.commit())
    {
        if (m_host)
            m_host->ui()->showError(name(),
                QString::fromUtf8(u8"无法保存扩充数据项配置：") + m_expressionsPath);
        return false;
    }
    return true;
}

bool LogExpandPlugin::refreshMappings(bool scheduleCalculation)
{
    ++m_generation;
    m_workerPool.clear();
    m_mappingDiagnostics.clear();
    m_mappedVariables.clear();
    m_datBatchAvailable = false;
    m_mappingConfigValid = false;
    if (!m_host || !m_currentSessionId)
    {
        refreshMenuState();
        return false;
    }

    QList<MappingDefinition> definitions;
    m_mappingConfigValid = MappingEngine::loadDefinitions(
        m_mappingPath, definitions, m_mappingDiagnostics);

    const QList<JsonDocumentInfo> documentInfos =
        m_host->jsonDocuments()->listDocuments(
            m_currentSessionId, kDatDecryptPluginId);

    QHash<QString, JsonDocumentPtr> documents;
    for (const JsonDocumentInfo& info : documentInfos)
    {
        if (!info.isReady())
            continue;
        JsonDocumentInfo acquiredInfo;
        JsonDocumentPtr document = m_host->jsonDocuments()->acquireDocument(
            m_currentSessionId, kDatDecryptPluginId, info.documentId, &acquiredInfo);
        if (document && acquiredInfo.isReady())
            documents.insert(info.documentId, std::move(document));
    }
    m_datBatchAvailable = documents.contains(QStringLiteral("robot.capa"))
        && documents.contains(QStringLiteral("robot.calib"))
        && documents.contains(QStringLiteral("robot.config"));

    if (m_mappingConfigValid && m_datBatchAvailable)
    {
        m_mappedVariables = MappingEngine::resolve(
            definitions, documents, m_mappingDiagnostics);

        const DataSnapshotPtr snapshot = m_host->data()->acquireSnapshot();
        if (snapshot)
        {
            for (MappedVariable& variable : m_mappedVariables)
            {
                if (variable.expressionEligible && snapshot->contains(variable.name))
                {
                    variable.expressionEligible = false;
                    m_mappingDiagnostics.push_back({PluginDiagnosticSeverity::Warning,
                        QStringLiteral("mapping"), variable.name,
                        QStringLiteral("The mapped variable name collides with a Viewer data item and is view-only.")});
                }
            }
        }
    }

    for (const PluginDiagnostic& diagnostic : m_mappingDiagnostics)
    {
        log(diagnostic.severity == PluginDiagnosticSeverity::Error
                ? LogLevel::Error : LogLevel::Warning,
            QStringLiteral("%1: %2").arg(diagnostic.itemName, diagnostic.message));
    }
    refreshMenuState();
    const bool ready = m_mappingConfigValid && m_datBatchAvailable;
    if (ready && scheduleCalculation)
        scheduleRecompute(QStringLiteral("dat_decrypt documents changed"));
    return ready;
}

void LogExpandPlugin::scheduleRecompute(const QString& reason)
{
    if (m_shuttingDown || !m_host || !m_expressionData || !m_currentSessionId
        || !m_mappingConfigValid || !m_expressionConfigValid
        || !m_datBatchAvailable)
    {
        return;
    }
    const DataSnapshotPtr snapshot = m_host->data()->acquireSnapshot();
    if (!snapshot || snapshot->sessionId() != m_currentSessionId
        || snapshot->rowCount() <= 0)
    {
        return;
    }

    QList<ExpressionScalar> scalars;
    for (const MappedVariable& variable : m_mappedVariables)
    {
        if (variable.expressionEligible)
            scalars.push_back({variable.name, variable.numericValue});
    }

    const QSet<QString> previousNames(m_publishedNames.begin(), m_publishedNames.end());
    QSet<QString> allExpansionNames;
    for (qsizetype index = 0; index < m_expansionDefinitions.size(); ++index)
    {
        const QString& name = m_expansionDefinitions[index].name;
        allExpansionNames.insert(name);
    }
    const QList<ExpansionDependencyInfo> dependencyInfo =
        analyzeExpansionDependencies(m_expansionDefinitions);
    QSet<QString> excludedNames = allExpansionNames;
    excludedNames.unite(previousNames);
    const QStringList excludedSnapshotColumns(excludedNames.begin(), excludedNames.end());

    QList<ExpansionCandidate> candidates;
    QList<ExpansionResult> initialResults;
    QStringList outputNames;
    QSet<QString> scheduledNames;
    for (qsizetype definitionIndex = 0;
         definitionIndex < m_expansionDefinitions.size(); ++definitionIndex)
    {
        const ExpansionDefinition& definition = m_expansionDefinitions[definitionIndex];
        if (!definition.enabled)
        {
            initialResults.push_back({definition.name, false,
                QStringLiteral("Disabled."), {}});
            continue;
        }
        if (snapshot->contains(definition.name)
            && !previousNames.contains(definition.name))
        {
            initialResults.push_back({definition.name, false,
                QStringLiteral("The output name collides with a Viewer data item."), {}});
            continue;
        }

        const QStringList& dependentOutputs =
            dependencyInfo[definitionIndex].dependencies;
        const QStringList& invalidDependencies =
            dependencyInfo[definitionIndex].invalidDependencies;
        if (!invalidDependencies.isEmpty())
        {
            initialResults.push_back({definition.name, false,
                QStringLiteral("An expansion item may reference only items defined earlier in the JSON array."),
                invalidDependencies});
            continue;
        }
        QStringList unavailableDependencies;
        for (const QString& dependency : dependentOutputs)
        {
            if (!scheduledNames.contains(dependency))
                unavailableDependencies.push_back(dependency);
        }
        if (!unavailableDependencies.isEmpty())
        {
            initialResults.push_back({definition.name, false,
                QStringLiteral("A referenced earlier expansion item is disabled or unavailable."),
                unavailableDependencies});
            continue;
        }
        candidates.push_back({definition, dependentOutputs});
        scheduledNames.insert(definition.name);
        outputNames.push_back(definition.name);
    }

    const DerivedColumnBatchCreateResult created =
        m_expressionData->createDerivedColumnBatch(
            id(), snapshot->sessionId(), outputNames, snapshot->rowCount());
    if (!created.success())
    {
        m_expansionResults = std::move(initialResults);
        m_expansionResults.push_back({QStringLiteral("<batch>"), false,
                                      created.error, {}});
        log(LogLevel::Error, QStringLiteral("Cannot create derived-column batch: %1")
                                .arg(created.error));
        refreshMenuState();
        return;
    }

    if (m_progress)
        m_host->ui()->finishLoadProgress(m_progress);
    m_progress = m_host->ui()->beginLoadProgress(
        id(), snapshot->sessionId(), QString::fromUtf8(u8"计算扩充表达式"));
    if (m_progress)
    {
        m_host->ui()->reportLoadProgress(
            m_progress, 0.0f, QString::fromUtf8(u8"正在准备表达式输入…"));
    }

    const quint64 generation = ++m_generation;
    m_workerPool.clear();
    const auto* expressionService = m_expressionData;
    auto* progressUi = m_host->ui();
    const PluginProgressHandle progress = m_progress;
    const DerivedColumnBatchWriterPtr writer = created.writer;
    log(LogLevel::Info, QStringLiteral("Recomputing %1 expansion item(s): %2")
                            .arg(candidates.size()).arg(reason));
    m_workerPool.start([this, generation, snapshot, scalars,
                        excludedSnapshotColumns,
                        candidates, initialResults = std::move(initialResults),
                        expressionService, progressUi, progress, writer]() mutable
    {
        QList<ExpansionResult> results = std::move(initialResults);
        QList<ExpressionColumn> temporaryColumns;
        QSet<QString> completedNames;
        qsizetype processedCount = 0;
        const auto reportProgress = [&]()
        {
            ++processedCount;
            if (progress && progressUi)
            {
                // 计算只占前 90%，批量发布和界面刷新完成后才能显示 100%。
                const float value = candidates.isEmpty() ? 0.9f
                    : 0.9f * static_cast<float>(processedCount)
                        / static_cast<float>(candidates.size());
                progressUi->reportLoadProgress(
                    progress, value,
                    QString::fromUtf8(u8"已计算 %1/%2 项")
                        .arg(processedCount).arg(candidates.size()));
            }
        };
        for (const ExpansionCandidate& candidate : candidates)
        {
            if (m_generation.load() != generation)
                return;
            const ExpansionDefinition& definition = candidate.definition;
            QStringList failedDependencies;
            for (const QString& dependency : candidate.dependencies)
            {
                if (!completedNames.contains(dependency))
                    failedDependencies.push_back(dependency);
            }
            if (!failedDependencies.isEmpty())
            {
                writer->discard(definition.name);
                results.push_back({definition.name, false,
                    QStringLiteral("A referenced earlier expansion item failed to calculate."),
                    failedDependencies});
                reportProgress();
                continue;
            }
            double* output = writer->data(definition.name);
            const ExpressionEvaluationResult evaluated = expressionService->evaluate(
                snapshot, definition.expression, scalars, temporaryColumns,
                excludedSnapshotColumns,
                output, writer->rowCount());
            if (!evaluated.success())
            {
                writer->discard(definition.name);
                results.push_back({definition.name, false,
                    evaluated.error, evaluated.missingSymbols});
            }
            else
            {
                temporaryColumns.push_back(
                    {definition.name, output, writer->rowCount()});
                completedNames.insert(definition.name);
                results.push_back({definition.name, true,
                    QStringLiteral("OK"), {}});
            }
            reportProgress();
        }
        if (candidates.isEmpty() && progress && progressUi)
            progressUi->reportLoadProgress(
                progress, 0.9f, QString::fromUtf8(u8"表达式计算完成"));
        if (m_generation.load() != generation)
            return;
        QMetaObject::invokeMethod(this,
            [this, generation, progress, writer,
             results = std::move(results)]() mutable
            {
                finishRecompute(generation, progress, writer, std::move(results));
            },
            Qt::QueuedConnection);
    });
}

void LogExpandPlugin::finishRecompute(
    quint64 generation,
    PluginProgressHandle progress,
    const DerivedColumnBatchWriterPtr& writer,
    QList<ExpansionResult> results)
{
    if (m_shuttingDown || generation != m_generation.load()
        || !m_host || !writer)
    {
        return;
    }
    if (progress && m_progress == progress)
    {
        m_host->ui()->reportLoadProgress(
            progress, 0.95f,
            QString::fromUtf8(u8"正在发布扩充数据并刷新界面…"));
    }
    QElapsedTimer commitTimer;
    commitTimer.start();
    const DerivedColumnBatchCommitResult committed = writer->commit();
    const qint64 commitElapsedMs = commitTimer.elapsed();
    if (!committed.success())
    {
        for (ExpansionResult& result : results)
        {
            if (result.success)
            {
                result.success = false;
                result.message = QStringLiteral("Batch commit failed: %1")
                                     .arg(committed.error);
            }
        }
        results.push_back({QStringLiteral("<batch>"), false,
                           committed.error, {}});
        log(LogLevel::Error, QStringLiteral("Derived-column batch commit failed: %1")
                                .arg(committed.error));
    }
    else
    {
        m_publishedNames = committed.columnNames;
        log(LogLevel::Info,
            QStringLiteral("Published %1 expansion item(s); commit and UI refresh took %2 ms.")
                .arg(committed.columnNames.size()).arg(commitElapsedMs));
    }
    m_expansionResults = std::move(results);
    for (const ExpansionResult& result : m_expansionResults)
    {
        if (!result.success && result.message != QStringLiteral("Disabled."))
        {
            log(LogLevel::Warning, QStringLiteral("%1: %2 %3")
                .arg(result.name, result.message,
                     result.missingSymbols.join(QStringLiteral(", "))));
        }
    }
    if (m_host && progress && m_progress == progress)
    {
        m_host->ui()->finishLoadProgress(progress);
        m_progress = 0;
    }
    refreshMenuState();
}

void LogExpandPlugin::showMappedVariables()
{
    MappedVariablesDialog dialog(m_mappedVariables, QApplication::activeWindow());
    dialog.exec();
}

void LogExpandPlugin::editExpansionDefinitions()
{
    QStringList viewerItems;
    QStringList reservedNames;
    if (m_host)
    {
        const DataSnapshotPtr snapshot = m_host->data()->acquireSnapshot();
        if (snapshot)
        {
            viewerItems = snapshot->columnNames();
            reservedNames = viewerItems;
            for (const QString& publishedName : m_publishedNames)
                reservedNames.removeAll(publishedName);
        }
    }
    ExpansionEditorDialog dialog(
        m_expansionDefinitions, viewerItems, m_mappedVariables,
        lastStatusMap(), reservedNames, QApplication::activeWindow());
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QList<ExpansionDefinition> definitions = dialog.definitions();
    if (!saveExpansionDefinitions(definitions))
        return;
    m_expansionDefinitions = definitions;
    m_expressionConfigValid = true;
    m_configDiagnostics.clear();
    if (m_mappingConfigValid && m_datBatchAvailable)
        scheduleRecompute(QStringLiteral("expression definitions changed"));
}

void LogExpandPlugin::showDiagnostics()
{
    DiagnosticsDialog dialog(
        allDiagnostics(), m_expansionResults, QApplication::activeWindow());
    dialog.exec();
}

void LogExpandPlugin::refreshMenuState()
{
    if (!m_host || !m_menu)
        return;
    m_host->ui()->setPluginMenuItemEnabled(
        m_menu, QStringLiteral("mapped_variables"), !m_mappedVariables.isEmpty());
    m_host->ui()->setPluginMenuItemEnabled(
        m_menu, QStringLiteral("recompute"),
        m_currentSessionId && m_datBatchAvailable && m_mappingConfigValid
            && m_expressionConfigValid);
}

void LogExpandPlugin::log(LogLevel level, const QString& message) const
{
    if (m_host)
        m_host->log()->write(id(), level, message);
}

QList<PluginDiagnostic> LogExpandPlugin::allDiagnostics() const
{
    QList<PluginDiagnostic> result = m_configDiagnostics;
    result.append(m_mappingDiagnostics);
    return result;
}

QHash<QString, QString> LogExpandPlugin::lastStatusMap() const
{
    QHash<QString, QString> result;
    for (const ExpansionResult& item : m_expansionResults)
        result.insert(item.name, item.message);
    return result;
}

#include "moc_log_expand_plugin.cpp"
