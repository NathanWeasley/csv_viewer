#include "expansion_dependency.h"

#include <QHash>
#include <QRegularExpression>

#include <utility>

namespace
{
QStringList expressionIdentifiers(const QString& expression)
{
    static const QRegularExpression identifier(
        QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));
    QStringList result;
    QRegularExpressionMatchIterator matches = identifier.globalMatch(expression);
    while (matches.hasNext())
    {
        const QString value = matches.next().captured();
        if (!result.contains(value))
            result.push_back(value);
    }
    return result;
}
}

QList<ExpansionDependencyInfo> analyzeExpansionDependencies(
    const QList<ExpansionDefinition>& definitions)
{
    QHash<QString, qsizetype> order;
    for (qsizetype index = 0; index < definitions.size(); ++index)
        order.insert(definitions[index].name, index);

    QList<ExpansionDependencyInfo> result;
    result.reserve(definitions.size());
    for (qsizetype index = 0; index < definitions.size(); ++index)
    {
        ExpansionDependencyInfo info;
        for (const QString& identifier : expressionIdentifiers(
                 definitions[index].expression))
        {
            const auto found = order.constFind(identifier);
            if (found == order.constEnd())
                continue;
            info.dependencies.push_back(identifier);
            if (found.value() >= index)
                info.invalidDependencies.push_back(identifier);
        }
        result.push_back(std::move(info));
    }
    return result;
}
