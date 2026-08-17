#include "dat_converter.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "crc.h"
#include "io.hpp"
#include "schema_document.h"

namespace datconv
{

namespace
{

constexpr std::size_t kHeaderSize = 0x50;
constexpr std::size_t kRecordCountOffset = 4;
constexpr std::size_t kTableNameOffset = 8;
constexpr std::size_t kTableNameSize = 32;
constexpr std::size_t kCrcOffset = 44;
constexpr std::size_t kVersionOffset = 48;

std::uint32_t readUint32(const std::uint8_t* pointer)
{
    return static_cast<std::uint32_t>(pointer[0]) | (static_cast<std::uint32_t>(pointer[1]) << 8) |
           (static_cast<std::uint32_t>(pointer[2]) << 16) |
           (static_cast<std::uint32_t>(pointer[3]) << 24);
}

struct RecordSlice
{
    std::size_t offset = 0;
    std::size_t length = 0;
};

} // namespace

DatConverter::Status
DatConverter::parse(const void* data, std::size_t size, const ParseOptions& options)
{
    if (data == nullptr)
    {
        clear();
        return fail(ErrorCode::InvalidArgument, "input data is null");
    }

    const auto* first = static_cast<const std::uint8_t*>(data);
    return parseOwned(std::vector<std::uint8_t>(first, first + size), options);
}

DatConverter::Status DatConverter::parse(std::vector<std::uint8_t> data,
                                         const ParseOptions& options)
{
    return parseOwned(std::move(data), options);
}

DatConverter::Status DatConverter::loadFile(const std::string& path, const ParseOptions& options)
{
    std::vector<std::uint8_t> data;
    if (!readFile(path.c_str(), data))
    {
        clear();
        return fail(ErrorCode::FileReadError, "cannot read " + path);
    }
    return parseOwned(std::move(data), options);
}

DatConverter::Status DatConverter::parseOwned(std::vector<std::uint8_t> data,
                                              const ParseOptions& options)
{
    clear();

    if (data.size() < kHeaderSize)
    {
        return fail(ErrorCode::NoHeader, "input is shorter than the 80-byte header", data.size());
    }
    if (std::memcmp(data.data(), "HKPR", 4) != 0)
    {
        return fail(ErrorCode::BadMagic, "bad HKPR magic", 0);
    }

    const std::uint32_t recordCount = readUint32(data.data() + kRecordCountOffset);
    const std::uint32_t storedCrc = readUint32(data.data() + kCrcOffset);
    const std::uint32_t version = readUint32(data.data() + kVersionOffset);
    const std::uint32_t calculatedCrc = crc32(data.data() + kHeaderSize, data.size() - kHeaderSize);
    if (calculatedCrc != storedCrc)
    {
        return fail(ErrorCode::CrcMismatch, "payload CRC mismatch", kCrcOffset);
    }

    std::size_t tableNameLength = 0;
    while (tableNameLength < kTableNameSize && data[kTableNameOffset + tableNameLength] != 0)
    {
        ++tableNameLength;
    }
    const std::string parsedTableName(reinterpret_cast<const char*>(data.data() + kTableNameOffset),
                                      tableNameLength);

    const std::size_t maximumRecordCount = (data.size() - kHeaderSize) / sizeof(std::uint32_t);
    if (recordCount > maximumRecordCount)
    {
        return fail(ErrorCode::TruncatedRecordLength,
                    "declared record count cannot fit in the payload",
                    kRecordCountOffset);
    }

    std::vector<RecordSlice> slices;
    slices.reserve(recordCount);
    std::size_t offset = kHeaderSize;
    for (std::uint32_t recordIndex = 0; recordIndex < recordCount; ++recordIndex)
    {
        if (offset > data.size() || data.size() - offset < sizeof(std::uint32_t))
        {
            return fail(
                ErrorCode::TruncatedRecordLength, "truncated record length", offset, recordIndex);
        }

        const std::uint32_t recordLength = readUint32(data.data() + offset);
        offset += sizeof(std::uint32_t);
        if (recordLength > data.size() - offset)
        {
            return fail(
                ErrorCode::TruncatedRecordPayload, "truncated record payload", offset, recordIndex);
        }

        slices.push_back({offset, recordLength});
        offset += recordLength;
    }

    const Schema* parsedSchema = lookupTable(parsedTableName);
    if (parsedSchema == nullptr)
    {
        return fail(ErrorCode::UnknownTable,
                    "no converter for table '" + parsedTableName + "'",
                    kTableNameOffset);
    }

    std::vector<Diagnostic> parsedDiagnostics;
    if (offset != data.size())
    {
        Diagnostic diagnostic;
        diagnostic.severity =
            options.strictSchema ? DiagnosticSeverity::Error : DiagnosticSeverity::Warning;
        diagnostic.code = DiagnosticCode::TrailingData;
        diagnostic.path = parsedTableName;
        diagnostic.message = std::to_string(data.size() - offset) +
                             " trailing byte(s) remain after the declared records";
        parsedDiagnostics.push_back(std::move(diagnostic));
    }

    std::vector<Record> parsedRecords;
    parsedRecords.reserve(slices.size());
    for (std::size_t recordIndex = 0; recordIndex < slices.size(); ++recordIndex)
    {
        const RecordSlice& slice = slices[recordIndex];
        const std::string_view payload(reinterpret_cast<const char*>(data.data() + slice.offset),
                                       slice.length);

        try
        {
            Record record;
            record.present = true;
            record.sourceOccurrences = 1;
            detail::materializeMessage(*parsedSchema,
                                       payload,
                                       record,
                                       recordIndex,
                                       parsedTableName,
                                       options,
                                       parsedDiagnostics);
            parsedRecords.push_back(std::move(record));
        }
        catch (const std::runtime_error& error)
        {
            return fail(ErrorCode::InvalidWireData, error.what(), slice.offset, recordIndex);
        }
    }

    if (options.strictSchema)
    {
        const bool hasSchemaError = std::any_of(
            parsedDiagnostics.begin(),
            parsedDiagnostics.end(),
            [](const Diagnostic& d) { return d.severity == DiagnosticSeverity::Error; });
        if (hasSchemaError)
        {
            diagnostics_ = std::move(parsedDiagnostics);
            return fail(ErrorCode::SchemaMismatch,
                        "strict schema validation found " + std::to_string(diagnostics_.size()) +
                            " issue(s)");
        }
    }

    header_.declaredRecordCount = recordCount;
    header_.payloadCrc32 = storedCrc;
    header_.version = version;
    tableName_ = parsedTableName;
    schema_ = parsedSchema;
    records_ = std::move(parsedRecords);
    diagnostics_ = std::move(parsedDiagnostics);
    if (options.keepRawData)
    {
        rawData_ = std::move(data);
    }

    valid_ = true;
    lastStatus_ = {};
    return lastStatus_;
}

void DatConverter::clear() noexcept
{
    valid_ = false;
    header_ = {};
    tableName_.clear();
    schema_ = nullptr;
    rawData_.clear();
    records_.clear();
    diagnostics_.clear();
    lastStatus_ = {};
}

bool DatConverter::isValid() const noexcept
{
    return valid_;
}

std::string_view DatConverter::tableName() const noexcept
{
    return tableName_;
}

const DatConverter::HeaderInfo& DatConverter::header() const noexcept
{
    return header_;
}

const Schema* DatConverter::schema() const noexcept
{
    return schema_;
}

const std::vector<std::uint8_t>& DatConverter::rawData() const noexcept
{
    return rawData_;
}

std::size_t DatConverter::recordCount() const noexcept
{
    return records_.size();
}

const DatConverter::Record* DatConverter::record(std::size_t index) const noexcept
{
    return index < records_.size() ? &records_[index] : nullptr;
}

const std::vector<DatConverter::Record>& DatConverter::records() const noexcept
{
    return records_;
}

const DatConverter::Status& DatConverter::lastStatus() const noexcept
{
    return lastStatus_;
}

const std::vector<DatConverter::Diagnostic>& DatConverter::diagnostics() const noexcept
{
    return diagnostics_;
}

DatConverter::Status
DatConverter::fail(ErrorCode code, std::string message, std::size_t offset, std::size_t recordIndex)
{
    valid_ = false;
    lastStatus_.code = code;
    lastStatus_.message = std::move(message);
    lastStatus_.offset = offset;
    lastStatus_.recordIndex = recordIndex;
    return lastStatus_;
}

const char* errorCodeName(DatConverter::ErrorCode code) noexcept
{
    switch (code)
    {
    case DatConverter::ErrorCode::Ok:
        return "ok";
    case DatConverter::ErrorCode::InvalidArgument:
        return "invalid argument";
    case DatConverter::ErrorCode::FileReadError:
        return "file read error";
    case DatConverter::ErrorCode::FileWriteError:
        return "file write error";
    case DatConverter::ErrorCode::OutputTooLarge:
        return "output too large";
    case DatConverter::ErrorCode::NoHeader:
        return "missing header";
    case DatConverter::ErrorCode::BadMagic:
        return "bad magic";
    case DatConverter::ErrorCode::CrcMismatch:
        return "CRC mismatch";
    case DatConverter::ErrorCode::TruncatedRecordLength:
        return "truncated record length";
    case DatConverter::ErrorCode::TruncatedRecordPayload:
        return "truncated record payload";
    case DatConverter::ErrorCode::UnknownTable:
        return "unknown table";
    case DatConverter::ErrorCode::InvalidWireData:
        return "invalid wire data";
    case DatConverter::ErrorCode::SchemaMismatch:
        return "schema mismatch";
    case DatConverter::ErrorCode::RecordIndexOutOfRange:
        return "record index out of range";
    }
    return "unknown error";
}

} // namespace datconv
