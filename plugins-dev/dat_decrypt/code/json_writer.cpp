#include "dat_converter.h"

#include <cmath>
#include <fstream>
#include <limits>

#include "num.h"

namespace datconv
{

namespace
{

void appendIndent(std::string& output, int depth, bool pretty)
{
    if (pretty)
    {
        output.append(static_cast<std::size_t>(depth), '\t');
    }
}

void appendMessage(const DatConverter::Value& message,
                   int depth,
                   const DatConverter::JsonOptions& options,
                   std::string& output);

const char* wireTypeName(WT wireType) noexcept
{
    switch (wireType)
    {
    case WT::Varint: return "varint";
    case WT::Fixed64: return "fixed64";
    case WT::Bytes: return "bytes";
    case WT::Fixed32: return "fixed32";
    }
    return "unknown";
}

std::string base64(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char digits[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3)
    {
        const std::uint32_t first = bytes[index];
        const std::uint32_t second = index + 1 < bytes.size() ? bytes[index + 1] : 0;
        const std::uint32_t third = index + 2 < bytes.size() ? bytes[index + 2] : 0;
        const std::uint32_t value = (first << 16) | (second << 8) | third;
        output.push_back(digits[(value >> 18) & 0x3f]);
        output.push_back(digits[(value >> 12) & 0x3f]);
        output.push_back(index + 1 < bytes.size() ? digits[(value >> 6) & 0x3f] : '=');
        output.push_back(index + 2 < bytes.size() ? digits[value & 0x3f] : '=');
    }
    return output;
}

void appendUnknownField(const DatConverter::UnknownField& field, std::string& output)
{
    output += "{\"number\":";
    output += std::to_string(field.number);
    output += ",\"wireType\":";
    output += jsonEscape(wireTypeName(field.wireType));
    switch (field.wireType)
    {
    case WT::Varint:
        output += ",\"value\":";
        output += jsonEscape(std::to_string(field.unsignedValue));
        break;
    case WT::Fixed64:
        output += ",\"value\":";
        output += std::isfinite(field.doubleValue) ? printNumber(field.doubleValue) : "null";
        break;
    case WT::Bytes:
        output += ",\"base64\":";
        output += jsonEscape(base64(field.bytes));
        break;
    case WT::Fixed32:
        output += ",\"value\":";
        output += std::to_string(field.unsignedValue);
        break;
    }
    output += '}';
}

void appendField(const DatConverter::Value& value,
                 int depth,
                 const DatConverter::JsonOptions& options,
                 std::string& output)
{
    const Field& field = *value.field;
    switch (field.type)
    {
    case T::Msg:
        appendMessage(value, depth + 1, options, output);
        break;

    case T::Rep:
    {
        output += '[';
        const std::size_t count = field.count > 0 ? static_cast<std::size_t>(field.count) : 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i != 0)
            {
                output += options.pretty ? ", " : ",";
            }
            appendMessage(value.children[i], depth + 2, options, output);
        }
        output += ']';
        break;
    }

    case T::Str:
        output += jsonEscape(value.stringValue);
        break;

    case T::I32:
        output += std::to_string(value.int32Value);
        break;

    case T::U32:
        output += std::to_string(value.uint32Value);
        break;

    case T::U64S:
        output += jsonEscape(std::to_string(value.uint64Value));
        break;

    case T::F64:
        output += printNumber(value.doubleValue);
        break;

    case T::F64Arr:
    {
        output += '[';
        const std::size_t count = field.count > 0 ? static_cast<std::size_t>(field.count) : 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i != 0)
            {
                output += options.pretty ? ", " : ",";
            }
            output += printNumber(i < value.doubleValues.size() ? value.doubleValues[i] : 0.0);
        }
        output += ']';
        break;
    }

    case T::I32Arr:
    {
        output += '[';
        const std::size_t count = field.count > 0 ? static_cast<std::size_t>(field.count) : 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i != 0)
            {
                output += options.pretty ? ", " : ",";
            }
            output += std::to_string(i < value.int32Values.size() ? value.int32Values[i] : 0);
        }
        output += ']';
        break;
    }
    }
}

void appendMessage(const DatConverter::Value& message,
                   int depth,
                   const DatConverter::JsonOptions& options,
                   std::string& output)
{
    output += '{';
    if (options.pretty)
    {
        output += '\n';
    }

    for (std::size_t i = 0; i < message.children.size(); ++i)
    {
        if (i != 0)
        {
            output += options.pretty ? ",\n" : ",";
        }
        appendIndent(output, depth + 1, options.pretty);

        const DatConverter::Value& child = message.children[i];
        output += jsonEscape(child.field->key);
        output += options.pretty ? ":\t" : ":";
        appendField(child, depth, options, output);
    }

    if (!message.unknownFields.empty())
    {
        if (!message.children.empty())
            output += options.pretty ? ",\n" : ",";
        appendIndent(output, depth + 1, options.pretty);
        output += "\"_unknownFields\":";
        if (options.pretty)
            output += '\t';
        output += '[';
        for (std::size_t index = 0; index < message.unknownFields.size(); ++index)
        {
            if (index != 0)
                output += options.pretty ? ", " : ",";
            appendUnknownField(message.unknownFields[index], output);
        }
        output += ']';
    }

    if (options.pretty)
    {
        output += '\n';
        appendIndent(output, depth, true);
    }
    output += '}';
}

DatConverter::Status makeStatus(DatConverter::ErrorCode code,
                                std::string message,
                                std::size_t recordIndex = DatConverter::npos)
{
    DatConverter::Status status;
    status.code = code;
    status.message = std::move(message);
    status.recordIndex = recordIndex;
    return status;
}

const char* diagnosticCodeName(DatConverter::DiagnosticCode code) noexcept
{
    switch (code)
    {
    case DatConverter::DiagnosticCode::UnknownField:
        return "unknown_field";
    case DatConverter::DiagnosticCode::WireTypeMismatch:
        return "wire_type_mismatch";
    case DatConverter::DiagnosticCode::ArrayElementCountMismatch:
        return "array_element_count_mismatch";
    case DatConverter::DiagnosticCode::RepeatedMessageCountMismatch:
        return "repeated_message_count_mismatch";
    case DatConverter::DiagnosticCode::UInt32Overflow:
        return "uint32_overflow";
    case DatConverter::DiagnosticCode::TrailingData:
        return "trailing_data";
    }
    return "unknown";
}

void appendDiagnostic(const DatConverter::Diagnostic& diagnostic,
                      int depth,
                      const DatConverter::JsonOptions& options,
                      std::string& output)
{
    const auto member = [&](const char* key, const std::string& value, bool first)
    {
        if (!first)
            output += options.pretty ? ",\n" : ",";
        appendIndent(output, depth + 1, options.pretty);
        output += jsonEscape(key);
        output += options.pretty ? ":\t" : ":";
        output += value;
    };

    output += '{';
    if (options.pretty)
        output += '\n';
    member("severity",
           jsonEscape(diagnostic.severity == DatConverter::DiagnosticSeverity::Error
                          ? "error" : "warning"),
           true);
    member("code", jsonEscape(diagnosticCodeName(diagnostic.code)), false);
    member("message", jsonEscape(diagnostic.message), false);
    member("path", jsonEscape(diagnostic.path), false);
    member("fieldNumber", std::to_string(diagnostic.fieldNumber), false);
    if (diagnostic.recordIndex != DatConverter::npos)
        member("recordIndex", std::to_string(diagnostic.recordIndex), false);
    if (options.pretty)
    {
        output += '\n';
        appendIndent(output, depth, true);
    }
    output += '}';
}

std::string outputPath(const std::string& basePath, std::size_t recordIndex)
{
    if (recordIndex == 0)
    {
        return basePath;
    }

    const std::string extension = ".json";
    if (basePath.size() >= extension.size() &&
        basePath.compare(basePath.size() - extension.size(), extension.size(), extension) == 0)
    {
        return basePath.substr(0, basePath.size() - extension.size()) +
               std::to_string(recordIndex) + extension;
    }
    return basePath + std::to_string(recordIndex);
}

DatConverter::Status writeFile(const std::string& path, const std::string& content)
{
    if (content.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return makeStatus(DatConverter::ErrorCode::OutputTooLarge,
                          "output is too large for " + path);
    }

    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        return makeStatus(DatConverter::ErrorCode::FileWriteError, "cannot write " + path);
    }
    if (!content.empty())
    {
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    if (!file)
    {
        return makeStatus(DatConverter::ErrorCode::FileWriteError, "short write on " + path);
    }
    file.close();
    if (!file)
    {
        return makeStatus(DatConverter::ErrorCode::FileWriteError, "cannot close " + path);
    }
    return {};
}

} // namespace

DatConverter::Status
DatConverter::toJson(std::size_t recordIndex, std::string& output, const JsonOptions& options) const
{
    output.clear();
    if (!valid_)
    {
        return makeStatus(ErrorCode::InvalidArgument, "no valid DAT document is loaded");
    }
    if (recordIndex >= records_.size())
    {
        return makeStatus(ErrorCode::RecordIndexOutOfRange,
                          "record index " + std::to_string(recordIndex) + " is out of range",
                          recordIndex);
    }

    output.reserve(1 << 20);
    appendMessage(records_[recordIndex], 0, options, output);
    return {};
}

DatConverter::Status
DatConverter::toDocumentJson(std::string& output, const JsonOptions& options) const
{
    output.clear();
    if (!valid_)
        return makeStatus(ErrorCode::InvalidArgument, "no valid DAT document is loaded");

    output.reserve(1 << 20);
    output += '{';
    if (options.pretty)
        output += '\n';

    appendIndent(output, 1, options.pretty);
    output += options.pretty ? "\"records\":\t[" : "\"records\":[";
    if (!records_.empty() && options.pretty)
        output += '\n';
    for (std::size_t recordIndex = 0; recordIndex < records_.size(); ++recordIndex)
    {
        if (recordIndex != 0)
            output += options.pretty ? ",\n" : ",";
        appendIndent(output, 2, options.pretty);
        appendMessage(records_[recordIndex], 1, options, output);
    }
    if (!records_.empty() && options.pretty)
    {
        output += '\n';
        appendIndent(output, 1, true);
    }
    output += ']';
    output += options.pretty ? ",\n" : ",";

    appendIndent(output, 1, options.pretty);
    output += "\"table\":";
    if (options.pretty)
        output += '\t';
    output += jsonEscape(tableName_);
    output += options.pretty ? ",\n" : ",";

    appendIndent(output, 1, options.pretty);
    output += options.pretty ? "\"header\":\t{\n" : "\"header\":{";
    appendIndent(output, 2, options.pretty);
    output += "\"version\":";
    if (options.pretty)
        output += '\t';
    output += std::to_string(header_.version);
    output += options.pretty ? ",\n" : ",";
    appendIndent(output, 2, options.pretty);
    output += "\"recordCount\":";
    if (options.pretty)
        output += '\t';
    output += std::to_string(header_.declaredRecordCount);
    output += options.pretty ? ",\n" : ",";
    appendIndent(output, 2, options.pretty);
    output += "\"payloadCrc32\":";
    if (options.pretty)
        output += '\t';
    output += std::to_string(header_.payloadCrc32);
    if (options.pretty)
    {
        output += '\n';
        appendIndent(output, 1, true);
    }
    output += '}';
    output += options.pretty ? ",\n" : ",";

    appendIndent(output, 1, options.pretty);
    output += options.pretty ? "\"diagnostics\":\t[" : "\"diagnostics\":[";
    if (!diagnostics_.empty() && options.pretty)
        output += '\n';
    for (std::size_t index = 0; index < diagnostics_.size(); ++index)
    {
        if (index != 0)
            output += options.pretty ? ",\n" : ",";
        appendIndent(output, 2, options.pretty);
        appendDiagnostic(diagnostics_[index], 2, options, output);
    }
    if (!diagnostics_.empty() && options.pretty)
    {
        output += '\n';
        appendIndent(output, 1, true);
    }
    output += ']';
    if (options.pretty)
        output += '\n';
    output += '}';
    if (options.pretty)
        output += '\n';
    return {};
}

DatConverter::Status DatConverter::writeJsonFile(const std::string& path,
                                                 std::size_t recordIndex,
                                                 const JsonOptions& options) const
{
    std::string output;
    const Status status = toJson(recordIndex, output, options);
    if (!status)
    {
        return status;
    }
    return writeFile(path, output);
}

DatConverter::Status DatConverter::writeAllJsonFiles(const std::string& basePath,
                                                     const JsonOptions& options,
                                                     std::vector<std::string>* writtenPaths) const
{
    if (writtenPaths != nullptr)
    {
        writtenPaths->clear();
    }
    if (!valid_)
    {
        return makeStatus(ErrorCode::InvalidArgument, "no valid DAT document is loaded");
    }

    for (std::size_t recordIndex = 0; recordIndex < records_.size(); ++recordIndex)
    {
        const std::string path = outputPath(basePath, recordIndex);
        const Status status = writeJsonFile(path, recordIndex, options);
        if (!status)
        {
            return status;
        }
        if (writtenPaths != nullptr)
        {
            writtenPaths->push_back(path);
        }
    }
    return {};
}

} // namespace datconv
