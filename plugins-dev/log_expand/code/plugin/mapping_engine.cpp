#include "mapping_engine.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace
{

void addDiagnostic(QList<PluginDiagnostic>& diagnostics,
                   PluginDiagnosticSeverity severity,
                   const QString& itemName,
                   const QString& message)
{
    diagnostics.push_back({severity, QStringLiteral("mapping"), itemName, message});
}

bool splitRoot(const QString& source, QString& rootName, qsizetype& position)
{
    position = 0;
    if (source.startsWith(QLatin1Char('<')))
    {
        // Accept the original placeholder spelling for compatibility with
        // already-deployed configuration files. New files use plain roots.
        const qsizetype end = source.indexOf(QLatin1Char('>'));
        if (end <= 1)
            return false;
        rootName = source.mid(1, end - 1).toLower();
        position = end + 1;
    }
    else
    {
        while (position < source.size()
               && source[position] != QLatin1Char('.')
               && source[position] != QLatin1Char('['))
        {
            ++position;
        }
        rootName = source.left(position).toLower();
    }
    return rootName == QStringLiteral("capa")
        || rootName == QStringLiteral("calib")
        || rootName == QStringLiteral("config");
}

QString documentIdForRoot(const QString& rootName)
{
    if (rootName == QStringLiteral("capa"))
        return QStringLiteral("robot.capa");
    if (rootName == QStringLiteral("calib"))
        return QStringLiteral("robot.calib");
    return QStringLiteral("robot.config");
}

bool payloadRoot(const viewer::plugin::JsonDocumentPtr& document,
                 QJsonValue& value,
                 QString& error)
{
    if (!document || !document->isObject())
    {
        error = QStringLiteral("The dat_decrypt document root is not an object.");
        return false;
    }
    const QJsonArray records = document->object()
        .value(QStringLiteral("records")).toArray();
    if (records.isEmpty())
    {
        error = QStringLiteral("The dat_decrypt document has no records[0] payload.");
        return false;
    }
    value = records.first();
    return true;
}

bool resolvePath(const QString& source,
                 const QHash<QString, viewer::plugin::JsonDocumentPtr>& documents,
                 QJsonValue& result,
                 QString& error)
{
    QString rootName;
    qsizetype position = 0;
    if (!splitRoot(source, rootName, position))
    {
        error = QStringLiteral("The source must begin with capa, calib, or config.");
        return false;
    }

    const QString documentId = documentIdForRoot(rootName);
    const auto document = documents.constFind(documentId);
    if (document == documents.constEnd())
    {
        error = QStringLiteral("The required dat_decrypt document '%1' is not ready.")
                    .arg(documentId);
        return false;
    }
    if (!payloadRoot(document.value(), result, error))
        return false;

    while (position < source.size())
    {
        if (source[position] == QLatin1Char('.'))
        {
            ++position;
            const qsizetype memberStart = position;
            while (position < source.size()
                   && source[position] != QLatin1Char('.')
                   && source[position] != QLatin1Char('['))
            {
                ++position;
            }
            const QString member = source.mid(memberStart, position - memberStart);
            if (member.isEmpty() || !result.isObject())
            {
                error = QStringLiteral("Member access '%1' does not target an object.")
                            .arg(member);
                return false;
            }
            const QJsonObject object = result.toObject();
            if (!object.contains(member))
            {
                error = QStringLiteral("Member '%1' was not found.").arg(member);
                return false;
            }
            result = object.value(member);
            continue;
        }
        if (source[position] == QLatin1Char('['))
        {
            const qsizetype closing = source.indexOf(QLatin1Char(']'), position + 1);
            if (closing < 0)
            {
                error = QStringLiteral("An array index is missing its closing bracket.");
                return false;
            }
            bool ok = false;
            const int index = source.mid(position + 1, closing - position - 1).toInt(&ok);
            if (!ok || index < 0 || !result.isArray())
            {
                error = QStringLiteral("Array index access is invalid.");
                return false;
            }
            const QJsonArray array = result.toArray();
            if (index >= array.size())
            {
                error = QStringLiteral("Array index %1 is out of range.").arg(index);
                return false;
            }
            result = array.at(index);
            position = closing + 1;
            continue;
        }

        error = QStringLiteral("Unexpected path character at offset %1.").arg(position);
        return false;
    }
    return true;
}

} // namespace

bool MappingEngine::loadDefinitions(
    const QString& filePath,
    QList<MappingDefinition>& definitions,
    QList<PluginDiagnostic>& diagnostics)
{
    definitions.clear();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        addDiagnostic(diagnostics, PluginDiagnosticSeverity::Error, {},
                      QStringLiteral("Cannot open mapping file: %1").arg(filePath));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        addDiagnostic(diagnostics, PluginDiagnosticSeverity::Error, {},
                      QStringLiteral("Mapping JSON is invalid: %1").arg(parseError.errorString()));
        return false;
    }

    const QJsonArray mappings = document.object()
        .value(QStringLiteral("mappings")).toArray();
    const QRegularExpression identifier(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    QSet<QString> names;
    for (qsizetype index = 0; index < mappings.size(); ++index)
    {
        const QJsonObject object = mappings[index].toObject();
        const QString source = object.value(QStringLiteral("source")).toString().trimmed();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        if (source.isEmpty() || !identifier.match(name).hasMatch())
        {
            addDiagnostic(diagnostics, PluginDiagnosticSeverity::Error, name,
                          QStringLiteral("Mapping %1 has an invalid source or variable name.")
                              .arg(index));
            continue;
        }
        if (names.contains(name))
        {
            addDiagnostic(diagnostics, PluginDiagnosticSeverity::Error, name,
                          QStringLiteral("The mapped variable name is duplicated."));
            continue;
        }
        names.insert(name);
        definitions.push_back({source, name});
    }
    return true;
}

QList<MappedVariable> MappingEngine::resolve(
    const QList<MappingDefinition>& definitions,
    const QHash<QString, viewer::plugin::JsonDocumentPtr>& documents,
    QList<PluginDiagnostic>& diagnostics)
{
    QList<MappedVariable> variables;
    for (const MappingDefinition& definition : definitions)
    {
        QJsonValue value;
        QString error;
        if (!resolvePath(definition.source, documents, value, error))
        {
            addDiagnostic(diagnostics, PluginDiagnosticSeverity::Warning,
                          definition.name,
                          QStringLiteral("%1: %2").arg(definition.source, error));
            continue;
        }

        MappedVariable variable;
        variable.source = definition.source;
        variable.name = definition.name;
        variable.value = value;
        if (value.isDouble())
        {
            variable.typeName = QStringLiteral("number");
            variable.numericValue = value.toDouble();
            variable.displayValue = QString::number(variable.numericValue, 'g', 17);
            variable.expressionEligible = true;
        }
        else if (value.isBool())
        {
            variable.typeName = QStringLiteral("bool");
            variable.numericValue = value.toBool() ? 1.0 : 0.0;
            variable.displayValue = value.toBool()
                ? QStringLiteral("true") : QStringLiteral("false");
            variable.expressionEligible = true;
        }
        else if (value.isString())
        {
            variable.typeName = QStringLiteral("string");
            variable.displayValue = value.toString();
        }
        else
        {
            addDiagnostic(diagnostics, PluginDiagnosticSeverity::Warning,
                          definition.name,
                          QStringLiteral("%1 resolves to a non-scalar JSON value.")
                              .arg(definition.source));
            continue;
        }
        variables.push_back(std::move(variable));
    }
    return variables;
}
