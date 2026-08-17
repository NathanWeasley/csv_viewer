#include "dat_converter.h"

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
    if (message.schema == nullptr || message.schema->empty())
    {
        output += '{';
        if (options.pretty)
        {
            output += '\n';
            appendIndent(output, depth, true);
        }
        output += '}';
        return;
    }

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
