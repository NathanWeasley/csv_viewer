#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace viewer::logparse
{

enum class ParserState
{
    ExpectFileHeader,
    CollectTypeDefinitions,
    ReadData
};

enum class ValueType
{
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float,
    Double,
    Character,
    String4,
    String16,
    String64,
    String256
};

enum class DiagnosticSeverity
{
    Warning,
    Error
};

struct FieldDefinition
{
    std::string name;
    ValueType valueType = ValueType::Double;
    char formatCode = 'd';
    // Payload element count and output column count are intentionally separate.
    // A string consumes its full payload and emits one independent string column.
    size_t payloadElementCount = 1;
    size_t outputColumnCount = 1;
    size_t elementByteWidth = sizeof(double);
    size_t payloadOffset = 0;
    std::vector<size_t> columnIndices;
    std::vector<size_t> stringColumnIndices;
};

struct PacketDefinition
{
    uint32_t id = 0;
    uint32_t payloadLength = 0;
    std::string packetName;
    std::string formatString;
    std::vector<FieldDefinition> fields;
};

struct ParsedColumn
{
    std::string name;
    uint32_t packetId = 0;
    std::string fieldName;
    ValueType valueType = ValueType::Double;
    size_t elementIndex = 0;
    std::vector<double> values;
};

struct ParsedStringColumn
{
    std::string name;
    uint32_t packetId = 0;
    std::string fieldName;
    ValueType valueType = ValueType::Character;
    std::vector<std::string> values;
};

struct ParseDiagnostic
{
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::filesystem::path filePath;
    uint64_t byteOffset = 0;
    std::string message;
};

// Row interval contributed by one input file in a sequential parse. Ranges
// follow the caller-provided file order and use [firstRow, firstRow+rowCount).
struct ParsedFileRange
{
    std::filesystem::path filePath;
    size_t firstRow = 0;
    size_t rowCount = 0;
};

struct ParseResult
{
    std::vector<PacketDefinition> packetTypes;
    std::vector<ParsedColumn> columns;
    std::vector<ParsedStringColumn> stringColumns;
    std::vector<ParseDiagnostic> diagnostics;
    std::vector<ParsedFileRange> fileRanges;
    size_t timestampCount = 0;
    bool cancelled = false;

    bool success() const noexcept
    {
        for (const auto& diagnostic : diagnostics)
        {
            if (diagnostic.severity == DiagnosticSeverity::Error)
                return false;
        }
        return !packetTypes.empty();
    }
};

} // namespace viewer::logparse
