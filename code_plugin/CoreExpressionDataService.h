#pragma once

#include "sdk/viewer_plugin_sdk/include/viewer_plugin/viewer_expression_data_sdk.h"

#include <QObject>

class PluginHost;

class CoreExpressionDataService final
    : public QObject
    , public viewer::plugin::IExpressionDataService
{
    Q_OBJECT
    Q_INTERFACES(viewer::plugin::IExpressionDataService)

public:
    explicit CoreExpressionDataService(PluginHost* host, QObject* parent = nullptr);

    viewer::plugin::ExpressionEvaluationResult evaluate(
        const viewer::plugin::DataSnapshotPtr& snapshot,
        const QString& expression,
        const QList<viewer::plugin::ExpressionScalar>& scalars,
        const QList<viewer::plugin::ExpressionColumn>& temporaryColumns,
        const QStringList& excludedSnapshotColumns,
        double* output,
        qsizetype outputSize) const override;

    viewer::plugin::DerivedColumnBatchCreateResult createDerivedColumnBatch(
        const QString& providerPluginId,
        quint64 sessionId,
        const QStringList& columnNames,
        qsizetype rowCount) override;

private:
    PluginHost* m_host = nullptr;
};
