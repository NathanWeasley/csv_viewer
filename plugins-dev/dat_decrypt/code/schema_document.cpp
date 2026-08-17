#include "schema_document.h"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace datconv
{

const DatConverter::Value* DatConverter::Value::find(std::string_view key) const noexcept
{
    const auto iterator = std::find_if(
        children.begin(),
        children.end(),
        [key](const Value& child) { return child.field != nullptr && child.field->key == key; });
    return iterator == children.end() ? nullptr : &*iterator;
}

bool DatConverter::Value::isMessage() const noexcept
{
    return schema != nullptr && (field == nullptr || field->type == T::Msg);
}

bool DatConverter::Value::isRepeatedMessage() const noexcept
{
    return field != nullptr && field->type == T::Rep;
}

namespace detail
{

namespace
{

bool wireTypeMatches(T type, const WireValue& value)
{
    switch (type)
    {
    case T::Str:
    case T::F64Arr:
    case T::I32Arr:
    case T::Msg:
    case T::Rep:
        return value.wireType == WT::Bytes;
    case T::F64:
        return value.wireType == WT::Fixed64 || value.wireType == WT::Fixed32;
    default:
        return value.wireType == WT::Varint;
    }
}

const char* wireTypeName(WT type)
{
    switch (type)
    {
    case WT::Varint:
        return "varint";
    case WT::Fixed64:
        return "fixed64";
    case WT::Bytes:
        return "bytes";
    case WT::Fixed32:
        return "fixed32";
    }
    return "unknown";
}

std::string fieldPath(const std::string& parent, const Field& field)
{
    if (parent.empty())
    {
        return field.key;
    }
    return parent + "." + field.key;
}

void addDiagnostic(std::vector<DatConverter::Diagnostic>& diagnostics,
                   const DatConverter::ParseOptions& options,
                   DatConverter::DiagnosticCode code,
                   std::size_t recordIndex,
                   std::string path,
                   std::uint32_t fieldNumber,
                   std::string message)
{
    DatConverter::Diagnostic diagnostic;
    diagnostic.severity = options.strictSchema ? DatConverter::DiagnosticSeverity::Error
                                               : DatConverter::DiagnosticSeverity::Warning;
    diagnostic.code = code;
    diagnostic.recordIndex = recordIndex;
    diagnostic.path = std::move(path);
    diagnostic.fieldNumber = fieldNumber;
    diagnostic.message = std::move(message);
    diagnostics.push_back(std::move(diagnostic));
}

DatConverter::UnknownField copyUnknownField(std::uint32_t number, const WireValue& source)
{
    DatConverter::UnknownField destination;
    destination.number = number;
    destination.wireType = source.wireType;
    destination.unsignedValue = source.unsignedValue;
    destination.doubleValue = source.doubleValue;
    destination.bytes.assign(source.bytes.begin(), source.bytes.end());
    return destination;
}

} // namespace

void materializeMessage(const Schema& schema,
                        std::string_view payload,
                        DatConverter::Value& destination,
                        std::size_t recordIndex,
                        const std::string& path,
                        const DatConverter::ParseOptions& options,
                        std::vector<DatConverter::Diagnostic>& diagnostics)
{
    const Fields wireFields = decodeMessage(payload);

    destination.schema = &schema;
    destination.children.clear();
    destination.children.reserve(schema.size());
    destination.unknownFields.clear();

    for (const Field& definition : schema)
    {
        DatConverter::Value parsed;
        parsed.field = &definition;

        std::vector<const WireValue*> values;
        const auto range = wireFields.equal_range(definition.no);
        std::size_t wireOccurrences = 0;
        for (auto iterator = range.first; iterator != range.second; ++iterator)
        {
            ++wireOccurrences;
            if (wireTypeMatches(definition.type, iterator->second))
            {
                values.push_back(&iterator->second);
            }
        }

        parsed.present = !values.empty();
        parsed.sourceOccurrences = values.size();
        const std::string currentPath = fieldPath(path, definition);

        if (wireOccurrences != values.size())
        {
            addDiagnostic(diagnostics,
                          options,
                          DatConverter::DiagnosticCode::WireTypeMismatch,
                          recordIndex,
                          currentPath,
                          definition.no,
                          "field #" + std::to_string(definition.no) + " contains " +
                              std::to_string(wireOccurrences - values.size()) +
                              " value(s) with an incompatible wire type");
        }

        switch (definition.type)
        {
        case T::Str:
            if (parsed.present)
            {
                parsed.stringValue.assign(values.back()->bytes);
            }
            break;

        case T::I32:
            if (parsed.present)
            {
                parsed.int32Value = decodeZigZag(values.back()->unsignedValue);
            }
            break;

        case T::U32:
            if (parsed.present)
            {
                if (values.back()->unsignedValue > std::numeric_limits<std::uint32_t>::max())
                {
                    addDiagnostic(diagnostics,
                                  options,
                                  DatConverter::DiagnosticCode::UInt32Overflow,
                                  recordIndex,
                                  currentPath,
                                  definition.no,
                                  "field value exceeds uint32 and will be truncated");
                }
                parsed.uint32Value = static_cast<std::uint32_t>(values.back()->unsignedValue);
            }
            break;

        case T::U64S:
            if (parsed.present)
            {
                parsed.uint64Value = values.back()->unsignedValue;
            }
            break;

        case T::F64:
            if (parsed.present)
            {
                parsed.doubleValue = values.back()->doubleValue;
            }
            break;

        case T::F64Arr:
            for (const WireValue* value : values)
            {
                if (value->bytes.size() % 8 != 0)
                {
                    throw std::runtime_error("field '" + currentPath +
                                             "' has an invalid packed-double length of " +
                                             std::to_string(value->bytes.size()));
                }
                const auto part = unpackDoubles(value->bytes);
                parsed.doubleValues.insert(parsed.doubleValues.end(), part.begin(), part.end());
            }
            parsed.sourceElementCount = parsed.doubleValues.size();
            if (parsed.present &&
                parsed.sourceElementCount != static_cast<std::size_t>(definition.count))
            {
                addDiagnostic(diagnostics,
                              options,
                              DatConverter::DiagnosticCode::ArrayElementCountMismatch,
                              recordIndex,
                              currentPath,
                              definition.no,
                              "expected " + std::to_string(definition.count) +
                                  " double value(s), found " +
                                  std::to_string(parsed.sourceElementCount));
            }
            break;

        case T::I32Arr:
            for (const WireValue* value : values)
            {
                const auto part = unpackSint32(value->bytes);
                parsed.int32Values.insert(parsed.int32Values.end(), part.begin(), part.end());
            }
            parsed.sourceElementCount = parsed.int32Values.size();
            if (parsed.present &&
                parsed.sourceElementCount != static_cast<std::size_t>(definition.count))
            {
                addDiagnostic(diagnostics,
                              options,
                              DatConverter::DiagnosticCode::ArrayElementCountMismatch,
                              recordIndex,
                              currentPath,
                              definition.no,
                              "expected " + std::to_string(definition.count) +
                                  " sint32 value(s), found " +
                                  std::to_string(parsed.sourceElementCount));
            }
            break;

        case T::Msg:
            if (definition.sub == nullptr)
            {
                throw std::runtime_error("field '" + currentPath + "' has no child schema");
            }
            materializeMessage(*definition.sub,
                               parsed.present ? values.back()->bytes : std::string_view{},
                               parsed,
                               recordIndex,
                               currentPath,
                               options,
                               diagnostics);
            break;

        case T::Rep:
        {
            if (definition.sub == nullptr)
            {
                throw std::runtime_error("field '" + currentPath + "' has no child schema");
            }
            const std::size_t expectedCount =
                definition.count > 0 ? static_cast<std::size_t>(definition.count) : 0;
            if (parsed.present && values.size() != expectedCount)
            {
                addDiagnostic(diagnostics,
                              options,
                              DatConverter::DiagnosticCode::RepeatedMessageCountMismatch,
                              recordIndex,
                              currentPath,
                              definition.no,
                              "expected " + std::to_string(expectedCount) + " message(s), found " +
                                  std::to_string(values.size()));
            }

            const std::size_t storedCount = std::max(values.size(), expectedCount);
            parsed.children.reserve(storedCount);
            for (std::size_t i = 0; i < storedCount; ++i)
            {
                DatConverter::Value item;
                item.present = i < values.size();
                item.sourceOccurrences = item.present ? 1 : 0;
                materializeMessage(*definition.sub,
                                   item.present ? values[i]->bytes : std::string_view{},
                                   item,
                                   recordIndex,
                                   currentPath + "[" + std::to_string(i) + "]",
                                   options,
                                   diagnostics);
                parsed.children.push_back(std::move(item));
            }
            break;
        }
        }

        destination.children.push_back(std::move(parsed));
    }

    std::set<std::uint32_t> reportedUnknownNumbers;
    for (const auto& entry : wireFields)
    {
        bool recognized = false;
        bool numberKnown = false;
        for (const Field& definition : schema)
        {
            if (definition.no == entry.first)
            {
                numberKnown = true;
                if (wireTypeMatches(definition.type, entry.second))
                {
                    recognized = true;
                    break;
                }
            }
        }

        if (!recognized && options.preserveUnknownFields)
        {
            destination.unknownFields.push_back(copyUnknownField(entry.first, entry.second));
        }

        if (!numberKnown && reportedUnknownNumbers.insert(entry.first).second)
        {
            addDiagnostic(diagnostics,
                          options,
                          DatConverter::DiagnosticCode::UnknownField,
                          recordIndex,
                          path,
                          entry.first,
                          "unmapped field #" + std::to_string(entry.first) + " (" +
                              wireTypeName(entry.second.wireType) + ")");
        }
    }
}

} // namespace detail
} // namespace datconv
