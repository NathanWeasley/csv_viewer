#pragma once

#include <cmath>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "dat_converter.h"

namespace datconv::qt
{

inline QString diagnosticCodeName(DatConverter::DiagnosticCode code)
{
    switch (code)
    {
    case DatConverter::DiagnosticCode::UnknownField: return QStringLiteral("unknown_field");
    case DatConverter::DiagnosticCode::WireTypeMismatch: return QStringLiteral("wire_type_mismatch");
    case DatConverter::DiagnosticCode::ArrayElementCountMismatch: return QStringLiteral("array_element_count_mismatch");
    case DatConverter::DiagnosticCode::RepeatedMessageCountMismatch: return QStringLiteral("repeated_message_count_mismatch");
    case DatConverter::DiagnosticCode::UInt32Overflow: return QStringLiteral("uint32_overflow");
    case DatConverter::DiagnosticCode::TrailingData: return QStringLiteral("trailing_data");
    }
    return QStringLiteral("unknown");
}

inline QString wireTypeName(WT wireType)
{
    switch (wireType)
    {
    case WT::Varint: return QStringLiteral("varint");
    case WT::Fixed64: return QStringLiteral("fixed64");
    case WT::Bytes: return QStringLiteral("bytes");
    case WT::Fixed32: return QStringLiteral("fixed32");
    }
    return QStringLiteral("unknown");
}

inline QJsonObject toQJsonObject(const DatConverter::Value& message);

inline QJsonValue toQJsonValue(const DatConverter::Value& value)
{
    if (value.field == nullptr)
        return toQJsonObject(value);

    switch (value.field->type)
    {
    case T::Str:
        return QString::fromUtf8(value.stringValue.data(),
                                 static_cast<qsizetype>(value.stringValue.size()));
    case T::I32:
        return value.int32Value;
    case T::U32:
        return static_cast<double>(value.uint32Value);
    case T::U64S:
        return QString::number(static_cast<qulonglong>(value.uint64Value));
    case T::F64:
        return std::isfinite(value.doubleValue) ? QJsonValue(value.doubleValue)
                                                : QJsonValue(QJsonValue::Null);
    case T::F64Arr:
    {
        QJsonArray array;
        const std::size_t count = value.field->count > 0
            ? static_cast<std::size_t>(value.field->count) : 0;
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
        const std::size_t count = value.field->count > 0
            ? static_cast<std::size_t>(value.field->count) : 0;
        for (std::size_t i = 0; i < count; ++i)
            array.append(i < value.int32Values.size() ? value.int32Values[i] : 0);
        return array;
    }
    case T::Msg:
        return toQJsonObject(value);
    case T::Rep:
    {
        QJsonArray array;
        const std::size_t count = value.field->count > 0
            ? static_cast<std::size_t>(value.field->count) : 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            array.append(i < value.children.size()
                ? toQJsonObject(value.children[i]) : QJsonObject{});
        }
        return array;
    }
    }
    return QJsonValue(QJsonValue::Null);
}

inline QJsonObject unknownFieldObject(const DatConverter::UnknownField& field)
{
    QJsonObject object;
    object.insert(QStringLiteral("number"), static_cast<double>(field.number));
    object.insert(QStringLiteral("wireType"), wireTypeName(field.wireType));
    switch (field.wireType)
    {
    case WT::Varint:
        object.insert(QStringLiteral("value"),
                      QString::number(static_cast<qulonglong>(field.unsignedValue)));
        break;
    case WT::Fixed64:
        object.insert(QStringLiteral("value"),
                      std::isfinite(field.doubleValue)
                          ? QJsonValue(field.doubleValue) : QJsonValue(QJsonValue::Null));
        break;
    case WT::Bytes:
        object.insert(
            QStringLiteral("base64"),
            QString::fromLatin1(QByteArray(
                reinterpret_cast<const char*>(field.bytes.data()),
                static_cast<qsizetype>(field.bytes.size())).toBase64()));
        break;
    case WT::Fixed32:
        object.insert(QStringLiteral("value"), static_cast<double>(field.unsignedValue));
        break;
    }
    return object;
}

inline QJsonObject toQJsonObject(const DatConverter::Value& message)
{
    QJsonObject object;
    for (const DatConverter::Value& child : message.children)
    {
        if (child.field != nullptr)
            object.insert(QString::fromUtf8(child.field->key), toQJsonValue(child));
    }
    if (!message.unknownFields.empty())
    {
        QJsonArray unknownFields;
        for (const DatConverter::UnknownField& field : message.unknownFields)
            unknownFields.append(unknownFieldObject(field));
        object.insert(QStringLiteral("_unknownFields"), unknownFields);
    }
    return object;
}

inline QJsonObject diagnosticObject(const DatConverter::Diagnostic& diagnostic)
{
    QJsonObject object;
    object.insert(QStringLiteral("severity"),
                  diagnostic.severity == DatConverter::DiagnosticSeverity::Error
                      ? QStringLiteral("error") : QStringLiteral("warning"));
    object.insert(QStringLiteral("code"), diagnosticCodeName(diagnostic.code));
    object.insert(QStringLiteral("message"), QString::fromUtf8(diagnostic.message));
    object.insert(QStringLiteral("path"), QString::fromUtf8(diagnostic.path));
    object.insert(QStringLiteral("fieldNumber"), static_cast<double>(diagnostic.fieldNumber));
    if (diagnostic.recordIndex != DatConverter::npos)
        object.insert(QStringLiteral("recordIndex"), static_cast<double>(diagnostic.recordIndex));
    return object;
}

inline QJsonDocument toQJsonDocument(const DatConverter& converter)
{
    QJsonObject root;
    root.insert(QStringLiteral("table"),
                QString::fromUtf8(converter.tableName().data(),
                                  static_cast<qsizetype>(converter.tableName().size())));

    QJsonObject header;
    header.insert(QStringLiteral("version"), static_cast<double>(converter.header().version));
    header.insert(QStringLiteral("recordCount"),
                  static_cast<double>(converter.header().declaredRecordCount));
    header.insert(QStringLiteral("payloadCrc32"),
                  static_cast<double>(converter.header().payloadCrc32));
    root.insert(QStringLiteral("header"), header);

    QJsonArray records;
    for (const DatConverter::Record& record : converter.records())
        records.append(toQJsonObject(record));
    root.insert(QStringLiteral("records"), records);

    QJsonArray diagnostics;
    for (const DatConverter::Diagnostic& diagnostic : converter.diagnostics())
        diagnostics.append(diagnosticObject(diagnostic));
    root.insert(QStringLiteral("diagnostics"), diagnostics);
    return QJsonDocument(root);
}

} // namespace datconv::qt
