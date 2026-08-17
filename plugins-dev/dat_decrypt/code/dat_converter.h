#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "schema.h"
#include "wire.h"

namespace datconv
{

class DatConverter final
{
public:
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    enum class ErrorCode
    {
        Ok,
        InvalidArgument,
        FileReadError,
        FileWriteError,
        OutputTooLarge,
        NoHeader,
        BadMagic,
        CrcMismatch,
        TruncatedRecordLength,
        TruncatedRecordPayload,
        UnknownTable,
        InvalidWireData,
        SchemaMismatch,
        RecordIndexOutOfRange
    };

    struct Status
    {
        ErrorCode code = ErrorCode::Ok;
        std::string message;
        std::size_t offset = npos;
        std::size_t recordIndex = npos;

        explicit operator bool() const noexcept
        {
            return code == ErrorCode::Ok;
        }
    };

    enum class DiagnosticSeverity
    {
        Warning,
        Error
    };

    enum class DiagnosticCode
    {
        UnknownField,
        WireTypeMismatch,
        ArrayElementCountMismatch,
        RepeatedMessageCountMismatch,
        UInt32Overflow,
        TrailingData
    };

    struct Diagnostic
    {
        DiagnosticSeverity severity = DiagnosticSeverity::Warning;
        DiagnosticCode code = DiagnosticCode::UnknownField;
        std::size_t recordIndex = npos;
        std::string path;
        std::uint32_t fieldNumber = 0;
        std::string message;
    };

    struct UnknownField
    {
        std::uint32_t number = 0;
        WT wireType = WT::Varint;
        std::uint64_t unsignedValue = 0;
        double doubleValue = 0.0;
        std::vector<std::uint8_t> bytes;
    };

    struct HeaderInfo
    {
        std::uint32_t declaredRecordCount = 0;
        std::uint32_t payloadCrc32 = 0;
        std::uint32_t version = 0;
    };

    struct Value
    {
        const Field* field = nullptr;
        const Schema* schema = nullptr;
        bool present = false;
        std::size_t sourceOccurrences = 0;
        std::size_t sourceElementCount = 0;

        std::int32_t int32Value = 0;
        std::uint32_t uint32Value = 0;
        std::uint64_t uint64Value = 0;
        double doubleValue = 0.0;
        std::string stringValue;
        std::vector<double> doubleValues;
        std::vector<std::int32_t> int32Values;

        // For Msg this contains fields. For Rep this contains message items;
        // each item then contains its own fields in children.
        std::vector<Value> children;
        std::vector<UnknownField> unknownFields;

        const Value* find(std::string_view key) const noexcept;
        bool isMessage() const noexcept;
        bool isRepeatedMessage() const noexcept;
    };

    using Record = Value;

    struct ParseOptions
    {
        bool strictSchema;
        bool keepRawData;
        bool preserveUnknownFields;

        constexpr ParseOptions(bool strict = false,
                               bool keepRaw = true,
                               bool preserveUnknown = true) noexcept
            : strictSchema(strict), keepRawData(keepRaw), preserveUnknownFields(preserveUnknown)
        {
        }
    };

    struct JsonOptions
    {
        bool pretty;

        constexpr JsonOptions(bool usePrettyFormat = true) noexcept : pretty(usePrettyFormat) {}
    };

    Status parse(const void* data, std::size_t size, const ParseOptions& options = ParseOptions{});
    Status parse(std::vector<std::uint8_t> data, const ParseOptions& options = ParseOptions{});
    Status loadFile(const std::string& path, const ParseOptions& options = ParseOptions{});

    void clear() noexcept;
    bool isValid() const noexcept;

    std::string_view tableName() const noexcept;
    const HeaderInfo& header() const noexcept;
    const Schema* schema() const noexcept;
    const std::vector<std::uint8_t>& rawData() const noexcept;

    std::size_t recordCount() const noexcept;
    const Record* record(std::size_t index) const noexcept;
    const std::vector<Record>& records() const noexcept;

    const Status& lastStatus() const noexcept;
    const std::vector<Diagnostic>& diagnostics() const noexcept;

    Status toJson(std::size_t recordIndex,
                  std::string& output,
                  const JsonOptions& options = JsonOptions{}) const;
    Status writeJsonFile(const std::string& path,
                         std::size_t recordIndex = 0,
                         const JsonOptions& options = JsonOptions{}) const;
    Status writeAllJsonFiles(const std::string& basePath,
                             const JsonOptions& options = JsonOptions{},
                             std::vector<std::string>* writtenPaths = nullptr) const;

private:
    Status parseOwned(std::vector<std::uint8_t> data, const ParseOptions& options);
    Status fail(ErrorCode code,
                std::string message,
                std::size_t offset = npos,
                std::size_t recordIndex = npos);

    bool valid_ = false;
    HeaderInfo header_;
    std::string tableName_;
    const Schema* schema_ = nullptr;
    std::vector<std::uint8_t> rawData_;
    std::vector<Record> records_;
    std::vector<Diagnostic> diagnostics_;
    Status lastStatus_;
};

const char* errorCodeName(DatConverter::ErrorCode code) noexcept;

} // namespace datconv
