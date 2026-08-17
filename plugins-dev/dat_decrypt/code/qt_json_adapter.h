#pragma once

// This adapter is intentionally excluded from the standalone build. When the
// converter is moved into a Qt host project, remove the #if 0 / #endif pair and
// link the target with Qt5::Core or Qt6::Core.
#if 0

#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "dat_converter.h"

namespace datconv::qt
{

inline QJsonObject toQJsonObject(const DatConverter::Value& message);

inline QJsonValue toQJsonValue(const DatConverter::Value& value)
{
    if (value.field == nullptr)
    {
        return toQJsonObject(value);
    }

    switch (value.field->type)
    {
    case T::Str:
        return QString::fromUtf8(value.stringValue.data(),
                                 static_cast<int>(value.stringValue.size()));

    case T::I32:
        return value.int32Value;

    case T::U32:
        return static_cast<double>(value.uint32Value);

    case T::U64S:
        // QJsonValue stores numbers as double. Preserve all uint64 bits by
        // following the standalone JSON writer and exposing this as a string.
        return QString::number(static_cast<qulonglong>(value.uint64Value));

    case T::F64:
        return std::isfinite(value.doubleValue) ? QJsonValue(value.doubleValue)
                                                : QJsonValue(QJsonValue::Null);

    case T::F64Arr:
    {
        QJsonArray array;
        const std::size_t count =
            value.field->count > 0 ? static_cast<std::size_t>(value.field->count) : 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const double item = i < value.doubleValues.size() ? value.doubleValues[i] : 0.0;
            array.append(std::isfinite(item) ? QJsonValue(item)
                                             : QJsonValue(QJsonValue::Null));
        }
        return array;
    }

    case T::I32Arr:
    {
        QJsonArray array;
        const std::size_t count =
            value.field->count > 0 ? static_cast<std::size_t>(value.field->count) : 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            array.append(i < value.int32Values.size() ? value.int32Values[i] : 0);
        }
        return array;
    }

    case T::Msg:
        return toQJsonObject(value);

    case T::Rep:
    {
        QJsonArray array;
        const std::size_t count =
            value.field->count > 0 ? static_cast<std::size_t>(value.field->count) : 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            array.append(i < value.children.size() ? toQJsonObject(value.children[i])
                                                   : QJsonObject{});
        }
        return array;
    }
    }

    return QJsonValue(QJsonValue::Null);
}

inline QJsonObject toQJsonObject(const DatConverter::Value& message)
{
    QJsonObject object;
    for (const DatConverter::Value& child : message.children)
    {
        if (child.field == nullptr)
        {
            continue;
        }
        object.insert(QString::fromUtf8(child.field->key), toQJsonValue(child));
    }
    return object;
}

inline QJsonDocument toQJsonDocument(const DatConverter::Record& record)
{
    return QJsonDocument(toQJsonObject(record));
}

} // namespace datconv::qt

#endif
