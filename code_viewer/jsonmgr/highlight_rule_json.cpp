#include "code_viewer/jsonmgr/highlight_rule_json.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace viewer
{
namespace
{
constexpr int kHighlightFileVersion = 1;

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

bool isValidCondition(int value)
{
    return value >= static_cast<int>(HighlightCondition::Greater)
        && value <= static_cast<int>(HighlightCondition::ChangeExceeds);
}
}

QJsonObject HighlightRuleJson::toJson(const HighlightRule& rule)
{
    QJsonObject object;
    object[QStringLiteral("dataColumn")] = QString::fromStdString(rule.dataColumn);
    object[QStringLiteral("condition")] = static_cast<int>(rule.condition);
    object[QStringLiteral("value1")] = rule.value1;
    object[QStringLiteral("value2")] = rule.value2;
    object[QStringLiteral("color")] = rule.color.name();
    object[QStringLiteral("alpha")] = rule.alpha;
    object[QStringLiteral("label")] = QString::fromStdString(rule.label);
    return object;
}

bool HighlightRuleJson::fromJson(const QJsonObject& object, HighlightRule* rule)
{
    if (!rule)
        return false;

    const QString dataColumn = object.value(QStringLiteral("dataColumn")).toString().trimmed();
    const int condition = object.value(QStringLiteral("condition")).toInt(-1);
    const double value1 = object.value(QStringLiteral("value1")).toDouble(0.0);
    const double value2 = object.value(QStringLiteral("value2")).toDouble(0.0);
    if (dataColumn.isEmpty() || !isValidCondition(condition)
        || !std::isfinite(value1) || !std::isfinite(value2))
    {
        return false;
    }

    QColor color(object.value(QStringLiteral("color")).toString(QStringLiteral("#ffff00")));
    if (!color.isValid())
        color = QColor(255, 255, 0);

    rule->dataColumn = dataColumn.toStdString();
    rule->condition = static_cast<HighlightCondition>(condition);
    rule->value1 = value1;
    rule->value2 = value2;
    rule->color = color;
    rule->alpha = std::clamp(object.value(QStringLiteral("alpha")).toInt(100), 0, 255);
    rule->label = object.value(QStringLiteral("label")).toString().toStdString();
    return true;
}

bool HighlightRuleJson::loadFile(const std::string& path,
                                 std::vector<HighlightRule>* rules,
                                 QString* error)
{
    if (!rules)
    {
        setError(error, QStringLiteral("Missing output rule list."));
        return false;
    }

    rules->clear();
    QFile file(QString::fromStdString(path));
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(error, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        setError(error, parseError.errorString());
        return false;
    }

    QJsonArray array;
    if (document.isObject())
        array = document.object().value(QStringLiteral("rules")).toArray();
    else if (document.isArray())
        array = document.array();
    else
    {
        setError(error, QStringLiteral("The root must be an object or an array."));
        return false;
    }

    for (const QJsonValue& value : array)
    {
        if (!value.isObject())
            continue;
        HighlightRule rule;
        if (!fromJson(value.toObject(), &rule))
            continue;
        if (std::find(rules->begin(), rules->end(), rule) == rules->end())
            rules->push_back(std::move(rule));
    }
    return true;
}

bool HighlightRuleJson::saveFile(const std::string& path,
                                 const std::vector<HighlightRule>& rules,
                                 QString* error)
{
    const QString filePath = QString::fromStdString(path);
    const QFileInfo fileInfo(filePath);
    if (!QDir().mkpath(fileInfo.absolutePath()))
    {
        setError(error, QStringLiteral("Cannot create directory: %1").arg(fileInfo.absolutePath()));
        return false;
    }

    QJsonArray array;
    for (const HighlightRule& rule : rules)
        array.append(toJson(rule));

    QJsonObject root;
    root[QStringLiteral("version")] = kHighlightFileVersion;
    root[QStringLiteral("rules")] = array;

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(error, file.errorString());
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0)
    {
        setError(error, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

} // namespace viewer
