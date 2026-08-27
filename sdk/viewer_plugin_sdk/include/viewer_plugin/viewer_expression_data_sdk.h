#pragma once

#include "viewer_plugin_sdk.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

namespace viewer::plugin
{

inline constexpr const char* kViewerCoreProviderId = "viewer.core";
inline constexpr const char* kExpressionDataServiceId = "expression-data";
inline constexpr int kExpressionDataServiceVersion = 1;

struct ExpressionScalar
{
    QString name;
    double value = 0.0;
};

// A caller-owned row-wise input that is available only for the current
// evaluation. The data buffer must remain valid until evaluate() returns.
// This is primarily used by a derived-column batch to make outputs computed
// earlier in the same batch available to later expressions.
struct ExpressionColumn
{
    QString name;
    const double* data = nullptr;
    qsizetype size = 0;
};

enum class ExpressionEvaluationStatus
{
    Success,
    InvalidRequest,
    MissingSymbol,
    CompileError,
    EvaluationError
};

struct ExpressionEvaluationResult
{
    ExpressionEvaluationStatus status = ExpressionEvaluationStatus::InvalidRequest;
    QString error;
    QStringList missingSymbols;

    bool success() const noexcept
    {
        return status == ExpressionEvaluationStatus::Success;
    }
};

enum class DerivedColumnBatchCommitStatus
{
    Success,
    InvalidWriter,
    StaleSession,
    InvalidProvider,
    InvalidName,
    NameCollision,
    RowCountMismatch,
    HostShuttingDown,
    InternalError
};

struct DerivedColumnBatchCommitResult
{
    DerivedColumnBatchCommitStatus status =
        DerivedColumnBatchCommitStatus::InternalError;
    QStringList columnNames;
    QString error;

    bool success() const noexcept
    {
        return status == DerivedColumnBatchCommitStatus::Success;
    }
};

class IDerivedColumnBatchWriter
{
public:
    virtual ~IDerivedColumnBatchWriter() = default;
    virtual quint64 sessionId() const noexcept = 0;
    virtual QString providerPluginId() const = 0;
    virtual QStringList columnNames() const = 0;
    virtual qsizetype rowCount() const noexcept = 0;
    virtual double* data(const QString& columnName) noexcept = 0;
    virtual bool discard(const QString& columnName) noexcept = 0;
    virtual DerivedColumnBatchCommitResult commit() = 0;
};

using DerivedColumnBatchWriterPtr =
    std::shared_ptr<IDerivedColumnBatchWriter>;

struct DerivedColumnBatchCreateResult
{
    DerivedColumnBatchWriterPtr writer;
    QString error;

    bool success() const noexcept { return static_cast<bool>(writer); }
};

class IExpressionDataService
{
public:
    virtual ~IExpressionDataService() = default;

    // When output is null, the expression is validated without publishing any
    // data. Otherwise outputSize must match snapshot->rowCount().
    virtual ExpressionEvaluationResult evaluate(
        const DataSnapshotPtr& snapshot,
        const QString& expression,
        const QList<ExpressionScalar>& scalars,
        const QList<ExpressionColumn>& temporaryColumns,
        const QStringList& excludedSnapshotColumns,
        double* output,
        qsizetype outputSize) const = 0;

    // A commit atomically replaces every column previously owned by
    // providerPluginId. Items discarded before commit are omitted and any old
    // columns with those names are removed.
    virtual DerivedColumnBatchCreateResult createDerivedColumnBatch(
        const QString& providerPluginId,
        quint64 sessionId,
        const QStringList& columnNames,
        qsizetype rowCount) = 0;
};

} // namespace viewer::plugin

#define VIEWER_EXPRESSION_DATA_SERVICE_IID \
    "com.weekendbuild.csvviewer.IExpressionDataService/1.0"
Q_DECLARE_INTERFACE(viewer::plugin::IExpressionDataService,
                    VIEWER_EXPRESSION_DATA_SERVICE_IID)
