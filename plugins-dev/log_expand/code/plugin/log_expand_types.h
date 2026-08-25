#pragma once

#include <QJsonValue>
#include <QString>
#include <QStringList>

struct MappingDefinition
{
    QString source;
    QString name;
};

struct MappedVariable
{
    QString source;
    QString name;
    QJsonValue value;
    QString typeName;
    QString displayValue;
    bool expressionEligible = false;
    double numericValue = 0.0;
};

struct ExpansionDefinition
{
    bool enabled = true;
    QString name;
    QString expression;
};

enum class PluginDiagnosticSeverity
{
    Warning,
    Error
};

struct PluginDiagnostic
{
    PluginDiagnosticSeverity severity = PluginDiagnosticSeverity::Warning;
    QString category;
    QString itemName;
    QString message;
};

struct ExpansionResult
{
    QString name;
    bool success = false;
    QString message;
    QStringList missingSymbols;
};
