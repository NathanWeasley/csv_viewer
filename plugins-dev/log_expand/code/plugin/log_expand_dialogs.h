#pragma once

#include "log_expand_types.h"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QStringList>

class QListWidget;
class QPlainTextEdit;
class QTableWidget;

class MappedVariablesDialog final : public QDialog
{
public:
    explicit MappedVariablesDialog(
        const QList<MappedVariable>& variables,
        QWidget* parent = nullptr);
};

class DiagnosticsDialog final : public QDialog
{
public:
    DiagnosticsDialog(const QList<PluginDiagnostic>& diagnostics,
                      const QList<ExpansionResult>& expansionResults,
                      QWidget* parent = nullptr);
};

class ExpansionEditorDialog final : public QDialog
{
public:
    ExpansionEditorDialog(
        const QList<ExpansionDefinition>& definitions,
        const QStringList& viewerDataItems,
        const QList<MappedVariable>& variables,
        const QHash<QString, QString>& lastStatuses,
        const QStringList& reservedOutputNames,
        QWidget* parent = nullptr);

    QList<ExpansionDefinition> definitions() const;

protected:
    void accept() override;

private:
    void addRow(const ExpansionDefinition& definition, const QString& status = {});
    void insertSymbol(const QString& symbol);

    QTableWidget* m_table = nullptr;
    QListWidget* m_dataItems = nullptr;
    QListWidget* m_mappedVariables = nullptr;
    QStringList m_reservedOutputNames;
};
