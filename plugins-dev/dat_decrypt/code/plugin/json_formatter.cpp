#include "json_formatter.h"

#include <QJsonArray>
#include <QJsonObject>

namespace datdecrypt::json
{
namespace
{

constexpr int kIndentWidth = 2;

void appendIndent(QString& output, int depth)
{
    output += QString(depth * kIndentWidth, QLatin1Char(' '));
}

QString scalarJson(const QJsonValue& value)
{
    QJsonArray wrapper;
    wrapper.append(value);
    QByteArray encoded = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    if (encoded.size() >= 2)
        encoded = encoded.mid(1, encoded.size() - 2);
    return QString::fromUtf8(encoded);
}

bool isNumericArray(const QJsonArray& array)
{
    for (const QJsonValue& value : array)
    {
        if (!value.isDouble())
            return false;
    }
    return true;
}

bool isMetadataKey(const QString& key)
{
    return key == QStringLiteral("table")
        || key == QStringLiteral("header")
        || key == QStringLiteral("metadata")
        || key == QStringLiteral("_metadata");
}

bool isDiagnosticKey(const QString& key)
{
    return key == QStringLiteral("diagnostics")
        || key == QStringLiteral("warnings")
        || key == QStringLiteral("errors")
        || key == QStringLiteral("_diagnostics");
}

void appendIfPresent(QStringList& result,
                     const QJsonObject& object,
                     const QString& key)
{
    if (object.contains(key) && !result.contains(key))
        result.push_back(key);
}

QStringList orderedKeys(const QJsonObject& object, bool documentRoot)
{
    const QStringList source = object.keys();
    if (!documentRoot)
        return source;

    QStringList result;
    result.reserve(source.size());
    appendIfPresent(result, object, QStringLiteral("records"));

    for (const QString& key : source)
    {
        if (key != QStringLiteral("records")
            && !isMetadataKey(key) && !isDiagnosticKey(key))
        {
            result.push_back(key);
        }
    }

    appendIfPresent(result, object, QStringLiteral("table"));
    appendIfPresent(result, object, QStringLiteral("header"));
    appendIfPresent(result, object, QStringLiteral("metadata"));
    appendIfPresent(result, object, QStringLiteral("_metadata"));

    for (const QString& key : source)
    {
        if (isDiagnosticKey(key))
            appendIfPresent(result, object, key);
    }
    return result;
}

void appendValue(QString& output,
                 const QJsonValue& value,
                 int depth,
                 bool documentRoot);

void appendObject(QString& output,
                  const QJsonObject& object,
                  int depth,
                  bool documentRoot)
{
    if (object.isEmpty())
    {
        output += QStringLiteral("{}");
        return;
    }

    output += QStringLiteral("{\n");
    const QStringList keys = orderedKeys(object, documentRoot);
    for (qsizetype index = 0; index < keys.size(); ++index)
    {
        if (index != 0)
            output += QStringLiteral(",\n");
        appendIndent(output, depth + 1);
        output += scalarJson(keys[index]);
        output += QStringLiteral(": ");
        appendValue(output, object.value(keys[index]), depth + 1, false);
    }
    output += QLatin1Char('\n');
    appendIndent(output, depth);
    output += QLatin1Char('}');
}

void appendArray(QString& output, const QJsonArray& array, int depth)
{
    if (array.isEmpty())
    {
        output += QStringLiteral("[]");
        return;
    }
    if (isNumericArray(array))
    {
        output += QLatin1Char('[');
        for (qsizetype index = 0; index < array.size(); ++index)
        {
            if (index != 0)
                output += QStringLiteral(", ");
            output += scalarJson(array[index]);
        }
        output += QLatin1Char(']');
        return;
    }

    output += QStringLiteral("[\n");
    for (qsizetype index = 0; index < array.size(); ++index)
    {
        if (index != 0)
            output += QStringLiteral(",\n");
        appendIndent(output, depth + 1);
        appendValue(output, array[index], depth + 1, false);
    }
    output += QLatin1Char('\n');
    appendIndent(output, depth);
    output += QLatin1Char(']');
}

void appendValue(QString& output,
                 const QJsonValue& value,
                 int depth,
                 bool documentRoot)
{
    if (value.isObject())
        appendObject(output, value.toObject(), depth, documentRoot);
    else if (value.isArray())
        appendArray(output, value.toArray(), depth);
    else
        output += scalarJson(value);
}

} // namespace

QString formatDocument(const QJsonDocument& document)
{
    if (document.isNull())
        return {};

    QString output;
    output.reserve(4096);
    if (document.isObject())
        appendObject(output, document.object(), 0, true);
    else
        appendArray(output, document.array(), 0);
    output += QLatin1Char('\n');
    return output;
}

} // namespace datdecrypt::json
