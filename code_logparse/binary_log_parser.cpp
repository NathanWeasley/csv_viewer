#include "code_logparse/binary_log_parser.h"

#include "code_logparse/binary_reader.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace viewer::logparse
{
namespace
{

constexpr uint8_t kFileMagic0 = 0x90;
constexpr uint8_t kFileMagic1 = 0xEB;
constexpr uint16_t kTypeDefinitionFrame = 1;
constexpr uint16_t kTimestampFrame = 2;
constexpr uint16_t kDateFrame = 3;
constexpr uint64_t kMaxTypeTextBytes = 64ull * 1024ull * 1024ull;

struct FormatToken
{
    char code = 0;
    ValueType valueType = ValueType::Double;
    size_t elementCount = 1;
    size_t elementByteWidth = 0;
    size_t payloadOffset = 0;
};

struct FileSchema
{
    std::unordered_map<uint32_t, PacketDefinition> definitions;
    std::vector<uint32_t> definitionOrder;
    std::unordered_map<uint32_t, PacketDefinition> lateDefinitions;
};

struct ParseSession
{
    ParseResult result;
    std::unordered_map<uint32_t, size_t> masterById;
    std::unordered_set<std::string> usedColumnNames;
    std::vector<uint8_t> payloadBuffer;
    bool masterReady = false;
};

void addDiagnostic(ParseSession& session,
                   DiagnosticSeverity severity,
                   const std::filesystem::path& filePath,
                   uint64_t byteOffset,
                   std::string message)
{
    session.result.diagnostics.push_back(
        {severity, filePath, byteOffset, std::move(message)});
}

bool checkedAdd(uint64_t lhs, uint64_t rhs, uint64_t& result) noexcept
{
    if (rhs > std::numeric_limits<uint64_t>::max() - lhs)
        return false;
    result = lhs + rhs;
    return true;
}

bool checkedMultiply(size_t lhs, size_t rhs, size_t& result) noexcept
{
    if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs)
        return false;
    result = lhs * rhs;
    return true;
}

bool decodeSingleString(const std::vector<uint8_t>& bytes, std::string& value)
{
    if (bytes.empty())
    {
        value.clear();
        return true;
    }
    if (bytes.back() != 0)
        return false;

    value.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size() - 1);
    return value.find('\0') == std::string::npos;
}

bool decodeNameList(const std::vector<uint8_t>& bytes,
                    std::vector<std::string>& names)
{
    names.clear();
    if (bytes.empty() || bytes.back() != 0)
        return false;

    size_t begin = 0;
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        if (bytes[i] != 0)
            continue;

        const std::string block(
            reinterpret_cast<const char*>(bytes.data() + begin), i - begin);
        size_t itemBegin = 0;
        while (itemBegin <= block.size())
        {
            const size_t comma = block.find(',', itemBegin);
            const size_t itemEnd = (comma == std::string::npos) ? block.size() : comma;

            size_t trimmedBegin = itemBegin;
            while (trimmedBegin < itemEnd
                   && std::isspace(static_cast<unsigned char>(block[trimmedBegin])))
            {
                ++trimmedBegin;
            }
            size_t trimmedEnd = itemEnd;
            while (trimmedEnd > trimmedBegin
                   && std::isspace(static_cast<unsigned char>(block[trimmedEnd - 1])))
            {
                --trimmedEnd;
            }

            if (trimmedBegin == trimmedEnd)
                return false;
            names.emplace_back(block.substr(trimmedBegin, trimmedEnd - trimmedBegin));

            if (comma == std::string::npos)
                break;
            itemBegin = comma + 1;
        }
        begin = i + 1;
    }
    return begin == bytes.size();
}

bool typeInfo(char code, ValueType& valueType, size_t& byteWidth) noexcept
{
    switch (code)
    {
    case 'b': valueType = ValueType::Int8;      byteWidth = 1;   return true;
    case 'B': valueType = ValueType::UInt8;     byteWidth = 1;   return true;
    case 'h': valueType = ValueType::Int16;     byteWidth = 2;   return true;
    case 'H': valueType = ValueType::UInt16;    byteWidth = 2;   return true;
    case 'i': valueType = ValueType::Int32;     byteWidth = 4;   return true;
    case 'I': valueType = ValueType::UInt32;    byteWidth = 4;   return true;
    case 'q': valueType = ValueType::Int64;     byteWidth = 8;   return true;
    case 'Q': valueType = ValueType::UInt64;    byteWidth = 8;   return true;
    case 'f': valueType = ValueType::Float;     byteWidth = 4;   return true;
    case 'd': valueType = ValueType::Double;    byteWidth = 8;   return true;
    case 'c': valueType = ValueType::Character; byteWidth = 1;   return true;
    case 'n': valueType = ValueType::String4;   byteWidth = 4;   return true;
    case 'N': valueType = ValueType::String16;  byteWidth = 16;  return true;
    case 'Z': valueType = ValueType::String64;  byteWidth = 64;  return true;
    case 'T': valueType = ValueType::String256; byteWidth = 256; return true;
    default: return false;
    }
}

bool parseFormatString(const std::string& format,
                       std::vector<FormatToken>& tokens,
                       size_t& calculatedPayloadLength,
                       std::string& error)
{
    tokens.clear();
    calculatedPayloadLength = 0;

    size_t position = 0;
    while (position < format.size())
    {
        FormatToken token;
        token.code = format[position++];
        if (!typeInfo(token.code, token.valueType, token.elementByteWidth))
        {
            error = "unknown format character '" + std::string(1, token.code) + "'";
            return false;
        }

        const bool hasArray = position < format.size() && format[position] == '[';
        if (hasArray)
        {
            ++position;
            if (position >= format.size()
                || !std::isdigit(static_cast<unsigned char>(format[position])))
            {
                error = "array length is missing after format character '"
                      + std::string(1, token.code) + "'";
                return false;
            }

            size_t count = 0;
            while (position < format.size()
                   && std::isdigit(static_cast<unsigned char>(format[position])))
            {
                const unsigned digit = static_cast<unsigned>(format[position] - '0');
                if (count > (std::numeric_limits<size_t>::max() - digit) / 10)
                {
                    error = "array length overflows size_t";
                    return false;
                }
                count = count * 10 + digit;
                ++position;
            }

            if (position >= format.size() || format[position] != ']')
            {
                error = "array length is missing closing ']'";
                return false;
            }
            ++position;

            if (count == 0)
            {
                error = "array length must be greater than zero";
                return false;
            }
            token.elementCount = count;
        }
        else if (token.code == 'c')
        {
            error = "format character 'c' must have an explicit [length]";
            return false;
        }

        size_t tokenBytes = 0;
        if (!checkedMultiply(token.elementCount, token.elementByteWidth, tokenBytes)
            || tokenBytes > std::numeric_limits<size_t>::max() - calculatedPayloadLength)
        {
            error = "calculated payload length overflows size_t";
            return false;
        }

        token.payloadOffset = calculatedPayloadLength;
        calculatedPayloadLength += tokenBytes;
        tokens.push_back(token);
    }

    if (tokens.empty())
    {
        error = "format string is empty";
        return false;
    }
    return true;
}

bool readTypeDefinition(BinaryReader& reader,
                        uint64_t frameOffset,
                        ParseSession& session,
                        PacketDefinition& definition)
{
    const auto& path = reader.filePath();
    uint32_t packetId = 0;
    uint32_t payloadLength = 0;
    uint32_t packetNameLength = 0;
    uint32_t formatLength = 0;
    uint32_t fieldNamesLength = 0;

    if (!reader.readUInt32(packetId)
        || !reader.readUInt32(payloadLength)
        || !reader.readUInt32(packetNameLength)
        || !reader.readUInt32(formatLength)
        || !reader.readUInt32(fieldNamesLength))
    {
        addDiagnostic(session, DiagnosticSeverity::Error, path, frameOffset,
                      "truncated type-definition frame header");
        return false;
    }

    if ((packetId != kTimestampFrame && packetId != kDateFrame && packetId < 900)
        || packetId > std::numeric_limits<uint16_t>::max())
    {
        addDiagnostic(session, DiagnosticSeverity::Error, path, frameOffset,
                      "type definition uses unsupported packet ID " + std::to_string(packetId));
        return false;
    }

    uint64_t stringBytes = packetNameLength;
    if (!checkedAdd(stringBytes, formatLength, stringBytes)
        || !checkedAdd(stringBytes, fieldNamesLength, stringBytes)
        || stringBytes > reader.remaining()
        || stringBytes > kMaxTypeTextBytes
        || stringBytes > std::numeric_limits<size_t>::max())
    {
        addDiagnostic(session, DiagnosticSeverity::Error, path, frameOffset,
                      "invalid or truncated B/C/D string lengths in type definition");
        return false;
    }

    std::vector<uint8_t> packetNameBytes;
    std::vector<uint8_t> formatBytes;
    std::vector<uint8_t> fieldNameBytes;
    if (!reader.readBytes(packetNameBytes, static_cast<size_t>(packetNameLength))
        || !reader.readBytes(formatBytes, static_cast<size_t>(formatLength))
        || !reader.readBytes(fieldNameBytes, static_cast<size_t>(fieldNamesLength)))
    {
        addDiagnostic(session, DiagnosticSeverity::Error, path, frameOffset,
                      "truncated strings in type-definition frame");
        return false;
    }

    std::string packetName;
    std::string format;
    std::vector<std::string> fieldNames;
    if (!decodeSingleString(packetNameBytes, packetName))
    {
        addDiagnostic(session, DiagnosticSeverity::Error, path, frameOffset,
                      "packet name is not a single NUL-terminated string");
        return false;
    }
    if (!decodeSingleString(formatBytes, format))
    {
        addDiagnostic(session, DiagnosticSeverity::Error, path, frameOffset,
                      "format is not a single NUL-terminated string");
        return false;
    }
    if (!decodeNameList(fieldNameBytes, fieldNames))
    {
        addDiagnostic(session, DiagnosticSeverity::Error, path, frameOffset,
                      "field-name block is not a NUL-terminated string list");
        return false;
    }

    std::vector<FormatToken> tokens;
    size_t calculatedLength = 0;
    std::string formatError;
    if (!parseFormatString(format, tokens, calculatedLength, formatError))
    {
        addDiagnostic(session, DiagnosticSeverity::Error, path, frameOffset,
                      "invalid format string: " + formatError);
        return false;
    }
    if (tokens.size() != fieldNames.size())
    {
        addDiagnostic(session, DiagnosticSeverity::Error, path, frameOffset,
                      "format item count does not match data-item name count");
        return false;
    }
    if (calculatedLength != payloadLength)
    {
        addDiagnostic(session, DiagnosticSeverity::Error, path, frameOffset,
                      "declared payload length does not match format string");
        return false;
    }
    if ((packetId == kTimestampFrame && payloadLength != 8)
        || (packetId == kDateFrame && payloadLength != 64))
    {
        addDiagnostic(session, DiagnosticSeverity::Error, path, frameOffset,
                      "timestamp/date payload length does not match its fixed frame size");
        return false;
    }

    definition = {};
    definition.id = packetId;
    definition.payloadLength = payloadLength;
    definition.packetName = std::move(packetName);
    definition.formatString = std::move(format);
    definition.fields.reserve(tokens.size());
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        FieldDefinition field;
        field.name = std::move(fieldNames[i]);
        field.valueType = tokens[i].valueType;
        field.formatCode = tokens[i].code;
        field.payloadElementCount = tokens[i].elementCount;
        field.outputColumnCount = (field.valueType == ValueType::Character
                                || field.valueType == ValueType::String4
                                || field.valueType == ValueType::String16
                                || field.valueType == ValueType::String64
                                || field.valueType == ValueType::String256)
            ? 1
            : tokens[i].elementCount;
        field.elementByteWidth = tokens[i].elementByteWidth;
        field.payloadOffset = tokens[i].payloadOffset;
        definition.fields.push_back(std::move(field));
    }
    return true;
}

bool sameSchema(const PacketDefinition& lhs, const PacketDefinition& rhs) noexcept
{
    if (lhs.id != rhs.id
        || lhs.payloadLength != rhs.payloadLength
        || lhs.packetName != rhs.packetName
        || lhs.formatString != rhs.formatString
        || lhs.fields.size() != rhs.fields.size())
    {
        return false;
    }

    for (size_t i = 0; i < lhs.fields.size(); ++i)
    {
        const auto& a = lhs.fields[i];
        const auto& b = rhs.fields[i];
        if (a.name != b.name
            || a.valueType != b.valueType
            || a.formatCode != b.formatCode
            || a.payloadElementCount != b.payloadElementCount
            || a.outputColumnCount != b.outputColumnCount
            || a.elementByteWidth != b.elementByteWidth
            || a.payloadOffset != b.payloadOffset)
        {
            return false;
        }
    }
    return true;
}

std::string sanitizeName(std::string_view raw)
{
    std::string cleaned;
    cleaned.reserve(raw.size());
    for (const unsigned char c : raw)
    {
        if (std::isalnum(c) || c == '_')
            cleaned.push_back(static_cast<char>(c));
    }
    return cleaned;
}

std::string makeUniqueName(std::string base,
                           std::unordered_set<std::string>& usedNames)
{
    if (base.empty())
        base = "column";

    if (usedNames.insert(base).second)
        return base;

    for (size_t suffix = 2;; ++suffix)
    {
        std::string candidate = base + "_" + std::to_string(suffix);
        if (usedNames.insert(candidate).second)
            return candidate;
    }
}

void establishMasterSchema(ParseSession& session,
                           const FileSchema& fileSchema,
                           const std::filesystem::path& filePath,
                           uint64_t byteOffset)
{
    if (session.masterReady)
        return;

    for (const uint32_t id : fileSchema.definitionOrder)
    {
        const auto it = fileSchema.definitions.find(id);
        if (it == fileSchema.definitions.end())
            continue;

        PacketDefinition definition = it->second;
        for (auto& field : definition.fields)
        {
            field.columnIndices.clear();
            field.columnIndices.reserve(field.outputColumnCount);

            for (size_t element = 0; element < field.outputColumnCount; ++element)
            {
                std::string rawName = definition.packetName.empty()
                    ? field.name
                    : definition.packetName + "_" + field.name;
                if (field.outputColumnCount > 1)
                    rawName += "_" + std::to_string(element);

                ParsedColumn column;
                column.name = makeUniqueName(sanitizeName(rawName), session.usedColumnNames);
                column.packetId = definition.id;
                column.fieldName = field.name;
                column.valueType = field.valueType;
                column.elementIndex = element;
                column.values.push_back(0.0); // Keep one trailing seed row.

                field.columnIndices.push_back(session.result.columns.size());
                session.result.columns.push_back(std::move(column));
            }
        }

        session.masterById[definition.id] = session.result.packetTypes.size();
        session.result.packetTypes.push_back(std::move(definition));
    }

    session.masterReady = !session.result.packetTypes.empty();
    if (!session.masterReady)
    {
        addDiagnostic(session, DiagnosticSeverity::Error, filePath, byteOffset,
                      "no valid data type was defined before data frames");
    }
}

void registerDefinition(ParseSession& session,
                        FileSchema& fileSchema,
                        PacketDefinition definition,
                        bool definitionsLocked,
                        const std::filesystem::path& filePath,
                        uint64_t frameOffset)
{
    const uint32_t id = definition.id;
    if (definitionsLocked)
    {
        fileSchema.lateDefinitions[id] = std::move(definition);
        addDiagnostic(session, DiagnosticSeverity::Warning, filePath, frameOffset,
                      "type definition encountered after data state; definition is ignored");
        return;
    }

    const auto existing = fileSchema.definitions.find(id);
    if (existing != fileSchema.definitions.end())
    {
        if (!sameSchema(existing->second, definition))
        {
            addDiagnostic(session, DiagnosticSeverity::Warning, filePath, frameOffset,
                          "conflicting duplicate definition for packet ID " + std::to_string(id)
                          + "; later definition is ignored");
        }
        return;
    }

    fileSchema.definitionOrder.push_back(id);
    fileSchema.definitions.emplace(id, std::move(definition));

    if (!session.masterReady)
        return;

    const auto masterIt = session.masterById.find(id);
    if (masterIt == session.masterById.end())
    {
        addDiagnostic(session, DiagnosticSeverity::Warning, filePath, frameOffset,
                      "additional packet ID " + std::to_string(id)
                      + " is not present in the first-file schema and will be ignored");
        return;
    }

    const auto& local = fileSchema.definitions.at(id);
    const auto& master = session.result.packetTypes[masterIt->second];
    if (!sameSchema(master, local))
    {
        addDiagnostic(session, DiagnosticSeverity::Warning, filePath, frameOffset,
                      "packet ID " + std::to_string(id)
                      + " does not match the first-file schema and will be ignored");
    }
}

bool isStringType(ValueType type) noexcept
{
    return type == ValueType::Character
        || type == ValueType::String4
        || type == ValueType::String16
        || type == ValueType::String64
        || type == ValueType::String256;
}

uint16_t loadUInt16(const uint8_t* data) noexcept
{
    return static_cast<uint16_t>(data[0])
         | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t loadUInt32(const uint8_t* data) noexcept
{
    return static_cast<uint32_t>(data[0])
         | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16)
         | (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t loadUInt64(const uint8_t* data) noexcept
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(data[i]) << (i * 8);
    return value;
}

double decodeNumber(const uint8_t* data, ValueType type) noexcept
{
    switch (type)
    {
    case ValueType::Int8:
        return static_cast<double>(static_cast<int8_t>(data[0]));
    case ValueType::UInt8:
        return static_cast<double>(data[0]);
    case ValueType::Int16:
        return static_cast<double>(static_cast<int16_t>(loadUInt16(data)));
    case ValueType::UInt16:
        return static_cast<double>(loadUInt16(data));
    case ValueType::Int32:
        return static_cast<double>(static_cast<int32_t>(loadUInt32(data)));
    case ValueType::UInt32:
        return static_cast<double>(loadUInt32(data));
    case ValueType::Int64:
    {
        const uint64_t bits = loadUInt64(data);
        int64_t value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        return static_cast<double>(value);
    }
    case ValueType::UInt64:
        return static_cast<double>(loadUInt64(data));
    case ValueType::Float:
    {
        const uint32_t bits = loadUInt32(data);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return static_cast<double>(value);
    }
    case ValueType::Double:
    {
        const uint64_t bits = loadUInt64(data);
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    default:
        return std::numeric_limits<double>::quiet_NaN();
    }
}

void decodePacket(ParseSession& session,
                  const PacketDefinition& definition,
                  const std::vector<uint8_t>& payload)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto& field : definition.fields)
    {
        for (size_t element = 0; element < field.outputColumnCount; ++element)
        {
            if (element >= field.columnIndices.size())
                continue;
            const size_t columnIndex = field.columnIndices[element];
            if (columnIndex >= session.result.columns.size()
                || session.result.columns[columnIndex].values.empty())
            {
                continue;
            }

            const size_t valueOffset = field.payloadOffset + element * field.elementByteWidth;
            double value = nan;
            if (!isStringType(field.valueType)
                && valueOffset + field.elementByteWidth <= payload.size())
            {
                value = decodeNumber(payload.data() + valueOffset, field.valueType);
            }
            session.result.columns[columnIndex].values.back() = value;
        }
    }
}

void appendSeedRow(ParseSession& session)
{
    for (auto& column : session.result.columns)
    {
        const double latest = column.values.empty() ? 0.0 : column.values.back();
        column.values.push_back(latest);
    }
    ++session.result.timestampCount;
}

const PacketDefinition* resolveMasterPacket(ParseSession& session,
                                            const FileSchema& fileSchema,
                                            uint32_t id,
                                            uint32_t& payloadLength,
                                            bool& mustDiscard)
{
    mustDiscard = false;

    const auto lateIt = fileSchema.lateDefinitions.find(id);
    if (lateIt != fileSchema.lateDefinitions.end()
        && fileSchema.definitions.find(id) == fileSchema.definitions.end())
    {
        payloadLength = lateIt->second.payloadLength;
        mustDiscard = true;
        return nullptr;
    }

    const auto localIt = fileSchema.definitions.find(id);
    const auto masterIt = session.masterById.find(id);
    if (localIt != fileSchema.definitions.end())
    {
        payloadLength = localIt->second.payloadLength;
        if (masterIt == session.masterById.end())
        {
            mustDiscard = true;
            return nullptr;
        }

        const auto& master = session.result.packetTypes[masterIt->second];
        if (!sameSchema(master, localIt->second))
        {
            mustDiscard = true;
            return nullptr;
        }
        return &master;
    }

    if (masterIt != session.masterById.end())
    {
        const auto& master = session.result.packetTypes[masterIt->second];
        payloadLength = master.payloadLength;
        return &master;
    }

    if (id == kTimestampFrame)
        payloadLength = 8;
    else if (id == kDateFrame)
        payloadLength = 64;
    else
        payloadLength = 0;
    mustDiscard = true;
    return nullptr;
}

bool readFileHeader(BinaryReader& reader, ParseSession& session)
{
    uint8_t magic0 = 0;
    uint8_t magic1 = 0;
    if (!reader.readUInt8(magic0) || !reader.readUInt8(magic1))
    {
        addDiagnostic(session, DiagnosticSeverity::Error, reader.filePath(), 0,
                      "file is too short to contain a file header");
        return false;
    }
    if (magic0 != kFileMagic0 || magic1 != kFileMagic1)
    {
        addDiagnostic(session, DiagnosticSeverity::Error, reader.filePath(), 0,
                      "invalid file-header magic; expected 90 EB");
        return false;
    }
    if (!reader.skip(10))
    {
        addDiagnostic(session, DiagnosticSeverity::Error, reader.filePath(), reader.offset(),
                      "truncated version/type section in file header");
        return false;
    }

    uint32_t jsonLength = 0;
    if (!reader.readUInt32(jsonLength) || !reader.skip(jsonLength))
    {
        addDiagnostic(session, DiagnosticSeverity::Error, reader.filePath(), reader.offset(),
                      "invalid or truncated JSON header length");
        return false;
    }
    return true;
}

void parseOneFile(ParseSession& session, const std::filesystem::path& filePath)
{
    BinaryReader reader(filePath);
    if (!reader.open())
    {
        addDiagnostic(session, DiagnosticSeverity::Error, filePath, 0,
                      "failed to open binary file");
        return;
    }

    ParserState state = ParserState::ExpectFileHeader;
    if (!readFileHeader(reader, session))
        return;
    state = ParserState::CollectTypeDefinitions;

    FileSchema fileSchema;
    while (reader.remaining() > 0)
    {
        const uint64_t searchOffset = reader.offset();
        uint64_t frameOffset = 0;
        if (!reader.seekNextFrameMagic(frameOffset))
        {
            if (reader.size() > searchOffset)
            {
                addDiagnostic(session, DiagnosticSeverity::Warning, filePath, searchOffset,
                              "trailing bytes do not contain another complete frame header");
            }
            break;
        }

        if (frameOffset > searchOffset)
        {
            addDiagnostic(session, DiagnosticSeverity::Warning, filePath, searchOffset,
                          "skipped " + std::to_string(frameOffset - searchOffset)
                          + " byte(s) while resynchronizing to frame magic");
        }

        uint16_t frameType = 0;
        if (!reader.readUInt16(frameType))
        {
            addDiagnostic(session, DiagnosticSeverity::Error, filePath, frameOffset,
                          "truncated frame type");
            break;
        }

        if (frameType == kTypeDefinitionFrame)
        {
            PacketDefinition definition;
            if (readTypeDefinition(reader, frameOffset, session, definition))
            {
                registerDefinition(session, fileSchema, std::move(definition),
                                   state == ParserState::ReadData, filePath, frameOffset);
            }
            continue;
        }

        if (state == ParserState::CollectTypeDefinitions)
        {
            establishMasterSchema(session, fileSchema, filePath, frameOffset);
            state = ParserState::ReadData;
        }

        if (!session.masterReady)
        {
            addDiagnostic(session, DiagnosticSeverity::Warning, filePath, frameOffset,
                          "data frame is ignored because no master schema is available");
            continue;
        }

        uint32_t payloadLength = 0;
        bool mustDiscard = false;
        const PacketDefinition* master = resolveMasterPacket(
            session, fileSchema, frameType, payloadLength, mustDiscard);

        if (payloadLength == 0)
        {
            addDiagnostic(session, DiagnosticSeverity::Warning, filePath, frameOffset,
                          "unknown packet ID " + std::to_string(frameType)
                          + "; scanning for the next frame magic");
            // Unknown frames have no length; scan their payload for A5 5A.
            continue;
        }

        if (payloadLength > reader.remaining())
        {
            addDiagnostic(session, DiagnosticSeverity::Error, filePath, frameOffset,
                          "truncated payload for packet ID " + std::to_string(frameType));
            break;
        }

        if (mustDiscard || !master)
        {
            if (!reader.skip(payloadLength))
            {
                addDiagnostic(session, DiagnosticSeverity::Error, filePath, frameOffset,
                              "failed to skip ignored packet payload");
                break;
            }
            continue;
        }

        if (!reader.readBytes(session.payloadBuffer, payloadLength))
        {
            addDiagnostic(session, DiagnosticSeverity::Error, filePath, frameOffset,
                          "failed to read packet payload");
            break;
        }
        decodePacket(session, *master, session.payloadBuffer);

        if (frameType == kTimestampFrame)
            appendSeedRow(session);
    }

    if (state == ParserState::CollectTypeDefinitions && !session.masterReady)
        establishMasterSchema(session, fileSchema, filePath, reader.offset());
}

} // namespace

ParseResult BinaryLogParser::parseFiles(
    const std::vector<std::filesystem::path>& filePaths) const
{
    ParseSession session;
    if (filePaths.empty())
    {
        addDiagnostic(session, DiagnosticSeverity::Error, {}, 0,
                      "no binary input file was provided");
        return std::move(session.result);
    }

    for (const auto& filePath : filePaths)
    {
        const size_t firstRow = session.result.timestampCount;
        try
        {
            parseOneFile(session, filePath);
        }
        catch (const std::bad_alloc&)
        {
            addDiagnostic(session, DiagnosticSeverity::Error, filePath, 0,
                          "memory allocation failed while parsing file");
        }
        catch (const std::exception& error)
        {
            addDiagnostic(session, DiagnosticSeverity::Error, filePath, 0,
                          "unexpected parser failure: " + std::string(error.what()));
        }
        session.result.fileRanges.push_back(
            {filePath, firstRow, session.result.timestampCount - firstRow});
    }

    // Every column keeps one trailing seed row; remove it after all files.
    for (auto& column : session.result.columns)
    {
        if (!column.values.empty())
            column.values.pop_back();
    }

    return std::move(session.result);
}

} // namespace viewer::logparse
