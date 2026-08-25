#include "code_plugin/CoreExpressionDataService.h"

#include "code_plugin/PluginHost.h"
#include "code_viewer/datamgr/expression_engine.h"

#include <exception>
#include <new>
#include <utility>
#include <vector>

using namespace viewer::plugin;

CoreExpressionDataService::CoreExpressionDataService(
    PluginHost* host, QObject* parent)
    : QObject(parent)
    , m_host(host)
{
}

ExpressionEvaluationResult CoreExpressionDataService::evaluate(
    const DataSnapshotPtr& snapshot,
    const QString& expression,
    const QList<ExpressionScalar>& scalars,
    double* output,
    qsizetype outputSize) const
{
    if (!snapshot)
    {
        return {ExpressionEvaluationStatus::InvalidRequest,
                QStringLiteral("No Viewer data snapshot is available."), {}};
    }

    try
    {
        std::vector<viewer::ExpressionColumnView> columns;
        const QStringList names = snapshot->columnNames();
        columns.reserve(static_cast<size_t>(names.size()));
        for (const QString& name : names)
        {
            const ColumnView column = snapshot->column(name);
            columns.push_back({name.toUtf8().toStdString(), column.data,
                               static_cast<size_t>(column.size)});
        }

        std::vector<viewer::ExpressionScalarValue> scalarValues;
        scalarValues.reserve(static_cast<size_t>(scalars.size()));
        for (const ExpressionScalar& scalar : scalars)
            scalarValues.push_back({scalar.name.toUtf8().toStdString(), scalar.value});

        const viewer::ExpressionEngineResult result = viewer::ExpressionEngine::evaluate(
            expression.toUtf8().toStdString(), columns, scalarValues,
            static_cast<size_t>(snapshot->rowCount()), output,
            output ? static_cast<size_t>(outputSize) : 0);

        ExpressionEvaluationStatus status = ExpressionEvaluationStatus::InvalidRequest;
        switch (result.status)
        {
        case viewer::ExpressionEngineStatus::Success:
            status = ExpressionEvaluationStatus::Success;
            break;
        case viewer::ExpressionEngineStatus::InvalidRequest:
            status = ExpressionEvaluationStatus::InvalidRequest;
            break;
        case viewer::ExpressionEngineStatus::MissingSymbol:
            status = ExpressionEvaluationStatus::MissingSymbol;
            break;
        case viewer::ExpressionEngineStatus::CompileError:
            status = ExpressionEvaluationStatus::CompileError;
            break;
        case viewer::ExpressionEngineStatus::EvaluationError:
            status = ExpressionEvaluationStatus::EvaluationError;
            break;
        }

        QStringList missing;
        missing.reserve(static_cast<qsizetype>(result.missingSymbols.size()));
        for (const std::string& symbol : result.missingSymbols)
            missing.push_back(QString::fromUtf8(symbol.c_str()));
        return {status, QString::fromUtf8(result.error.c_str()), missing};
    }
    catch (const std::bad_alloc&)
    {
        return {ExpressionEvaluationStatus::EvaluationError,
                QStringLiteral("Not enough memory to evaluate the expression."), {}};
    }
    catch (const std::exception& exception)
    {
        return {ExpressionEvaluationStatus::EvaluationError,
                QStringLiteral("Expression evaluation failed: %1")
                    .arg(QString::fromUtf8(exception.what())), {}};
    }
    catch (...)
    {
        return {ExpressionEvaluationStatus::EvaluationError,
                QStringLiteral("Expression evaluation failed unexpectedly."), {}};
    }
}

DerivedColumnBatchCreateResult CoreExpressionDataService::createDerivedColumnBatch(
    const QString& providerPluginId,
    quint64 sessionId,
    const QStringList& columnNames,
    qsizetype rowCount)
{
    if (!m_host)
        return {{}, QStringLiteral("The Viewer core data service is unavailable.")};
    return m_host->createDerivedColumnBatch(
        providerPluginId, sessionId, columnNames, rowCount);
}
